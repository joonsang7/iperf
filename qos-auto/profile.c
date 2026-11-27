#include "qos.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double sent[TRIALS], max_loss, p95;
    int replies, rate_valid, all_trials_replied;
} Stats;
static int by_number(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static const cJSON *array_of(const cJSON *obj, const char *key, int count)
{
    const cJSON *a = field(obj, key);
    if (!cJSON_IsArray(a) || cJSON_GetArraySize(a) != count) die("unexpected array length");
    return a;
}
static Stats summarize(const cJSON *stage, uint64_t target)
{
    Stats s = {.rate_valid=1, .all_trials_replied=1, .p95=NAN};
    double rtt[PROBES * TRIALS];
    number(field(stage, "target_bps"), target, target);
    const cJSON *trials = array_of(stage, "trials", TRIALS);
    for (int i = 0; i < TRIALS; ++i) {
        const cJSON *t = cJSON_GetArrayItem(trials, i);
        s.sent[i] = number(field(t, "sent_bps"), 0, target ? 1e15 : 0);
        number(field(t, "received_bps"), 0, target ? 1e15 : 0);
        s.max_loss = fmax(s.max_loss, number(field(t, "loss_pct"), 0, target ? 100 : 0));
        if (target && !load_ok(s.sent[i], target)) s.rate_valid = 0;
        const cJSON *a = array_of(t, "rtt_ms", PROBES);
        int previous = s.replies;
        for (int k = 0; k < PROBES; ++k) {
            const cJSON *item = cJSON_GetArrayItem(a,k);
            if (!cJSON_IsNull(item)) rtt[s.replies++] = number(item, 0, TIMEOUT_MS);
        }
        if (s.replies == previous) s.all_trials_replied = 0;
    }
    if (s.replies) {
        qsort(rtt, s.replies, sizeof(*rtt), by_number);
        s.p95 = rtt[(95*s.replies+99)/100 - 1];
    }
    return s;
}
static const char *judge(const Stats *s, const Plan *p)
{
    if (!s->rate_valid || !s->all_trials_replied) return "INCONCLUSIVE";
    return s->max_loss <= p->loss_limit && s->p95 <= p->rtt_limit ? "PASS" : "FAIL";
}
static void print_stage(const Plan *p, int index, const Stats *s)
{
    const int total = PROBES * TRIALS;
    printf("%3d%% | target %.4f Mbps | actual median %.4f Mbps | ", index*25,
           p->rate[index]/1e6, median3(s->sent[0],s->sent[1],s->sent[2])/1e6);
    if (index) printf("max UDP loss %.3f%% | ", s->max_loss);
    else printf("no UDP load | ");
    if (s->replies) printf("RTT p95 %.3f ms | ", s->p95);
    else printf("RTT p95 N/A | ");
    printf("probe missing %d/%d (%.2f%%) | %s\n", total-s->replies, total,
           100.0*(total-s->replies)/total, judge(s, p));
    if (index && s->rate_valid && s->all_trials_replied &&
        s->max_loss <= p->loss_limit && s->p95 > p->rtt_limit)
        puts("  UDP loss within limit; RTT p95 exceeds limit.");
}
void profile(const char *plan_path, const char *output)
{
    cJSON *plan_json = read_json(plan_path);
    Plan p = parse_plan(plan_json);
    char client[INET_ADDRSTRLEN];
    if (source_address(p.server, client) || strcmp(client, p.client))
        die("client address differs from the saved plan");
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "schema", "iperf-qos-profile-v1");
    cJSON_AddItemToObject(result, "plan", plan_json);
    cJSON *stages = cJSON_AddArrayToObject(result, "stages");
    double idle_p95 = NAN;
    for (int i = 0; i < STAGES; i++) {
        cJSON *stage = cJSON_CreateObject();
        cJSON_AddItemToArray(stages, stage);
        cJSON_AddNumberToObject(stage, "target_bps", p.rate[i]);
        cJSON *trials = cJSON_AddArrayToObject(stage, "trials");
        for (int t = 0; t < TRIALS; t++) {
            printf("Measuring %d%% (%.4f Mbps), trial %d/%d\n",
                   i*25,p.rate[i]/1e6,t+1,TRIALS);
            Rtt rtt;
            Load load;
            measure(&p,p.rate[i],&load,&rtt);
            cJSON *row = cJSON_CreateObject();
            cJSON_AddItemToArray(trials, row);
            cJSON_AddNumberToObject(row,"sent_bps",load.sent);
            cJSON_AddNumberToObject(row,"received_bps",load.received);
            cJSON_AddNumberToObject(row,"loss_pct",load.loss);
            cJSON *samples = cJSON_AddArrayToObject(row,"rtt_ms");
            for (int k = 0; k < PROBES; k++)
                cJSON_AddItemToArray(samples, rtt.ms[k] < 0 ? cJSON_CreateNull() :
                                                              cJSON_CreateNumber(rtt.ms[k]));
            sleep_ms(200);
        }
        Stats s = summarize(stage, p.rate[i]);
        print_stage(&p, i, &s);
        if (i == 0 && s.all_trials_replied) idle_p95 = s.p95;
        if (i > 0 && isfinite(idle_p95) && s.all_trials_replied && s.rate_valid)
            printf("  RTT p95 change from idle: %+.3f ms\n", s.p95 - idle_p95);
    }
    save_json(output,result);
    cJSON_Delete(result);
}
static const cJSON *get_stages(const cJSON *j)
{
    const char *schema = cJSON_GetStringValue(field(j,"schema"));
    if (!schema || strcmp(schema,"iperf-qos-profile-v1")) die("invalid profile type");
    return array_of(j,"stages",STAGES);
}
void compare(const char *before, const char *after)
{
    cJSON *a = read_json(before), *b = read_json(after);
    const cJSON *pa = field(a,"plan"), *pb = field(b,"plan");
    if (!cJSON_Compare(pa,pb,1)) die("plans differ; rerun with the same plan");
    Plan p = parse_plan(pa);
    const cJSON *sa = get_stages(a), *sb = get_stages(b);
    uint64_t best_before = 0, best_after = 0;
    int comparable = 0;
    for (int i = 0; i < STAGES; i++) {
        uint64_t rate = p.rate[i];
        Stats x = summarize(cJSON_GetArrayItem(sa,i),rate);
        Stats y = summarize(cJSON_GetArrayItem(sb,i),rate);
        printf("BEFORE "); print_stage(&p,i,&x);
        printf("AFTER  "); print_stage(&p,i,&y);
        int matched = x.rate_valid && y.rate_valid;
        for (int t = 0; t < TRIALS; t++)
            if (fabs(x.sent[t]-y.sent[t]) > 0.05*rate) matched = 0;
        if (!matched || !x.all_trials_replied || !y.all_trials_replied) {
            puts("  Excluded: actual loads differ or insufficient RTT data.");
            continue;
        }
        printf("  RTT p95 change (after - before): %+.3f ms\n",y.p95-x.p95);
        if (!rate) continue;
        comparable++;
        if (!strcmp(judge(&x,&p),"PASS")) best_before = rate;
        if (!strcmp(judge(&y,&p),"PASS")) best_after = rate;
    }
    printf("Comparable loaded stages: %d of %d\n",comparable,STAGES-1);
    if (best_before) printf("Highest joint PASS before: %.4f Mbps\n",best_before/1e6);
    else puts("Highest joint PASS before: none among comparable stages");
    if (best_after) printf("Highest joint PASS after: %.4f Mbps\n",best_after/1e6);
    else puts("Highest joint PASS after: none among comparable stages");
    if (best_before && best_after)
        printf("Highest joint PASS change: %+.4f Mbps\n",
               ((double)best_after-(double)best_before)/1e6);
    cJSON_Delete(a); cJSON_Delete(b);
}
