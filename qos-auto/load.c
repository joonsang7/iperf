#include "iperf_config.h"
#include "iperf_api.h"
#include "qos.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* libiperf runs one test at a time. Only the probe worker touches its socket. */
typedef struct { const Plan *plan; Rtt *out; double start; pthread_t thread; int created; } Worker;
static Worker *current;
static void *sample(void *arg)
{
    Worker *w = arg;
    probe(w->plan, w->start, w->out);
    return NULL;
}
static void started(struct iperf_test *test)
{
    iperf_on_test_start(test);
    current->start = now_ms();
    current->created = pthread_create(&current->thread, NULL, sample, current) == 0;
}
static void quiet(struct iperf_test *test, char *json) { (void)test; (void)json; }
int load_ok(double actual, uint64_t target)
{
    return fabs(actual - (double)target) <= 0.05 * target;
}
double median3(double a, double b, double c)
{
    if (a > b) { double t = a; a = b; b = t; }
    return fmax(a, fmin(b, c));
}
void measure(const Plan *p, uint64_t bps, Load *out, Rtt *rtt)
{
    *out = (Load){0};
    if (!bps) {
        probe(p, now_ms(), rtt);
        if (!rtt->ok) die("probe failed; check echo process, port and client IPv4");
        return;
    }
    struct iperf_test *test = iperf_new_test();
    if (!test) die("iperf_new_test failed");
    if (iperf_defaults(test) < 0 || set_protocol(test, Pudp) < 0)
        die(iperf_strerror(i_errno));
    iperf_set_test_role(test, 'c');
    iperf_set_test_server_hostname(test, p->server);
    iperf_set_test_server_port(test, LOAD_PORT);
    iperf_set_test_bind_address(test, p->client);
    iperf_set_test_connect_timeout(test, 4000);
    iperf_set_test_rate(test, bps);
    iperf_set_test_duration(test, SECONDS);
    iperf_set_test_blksize(test, PAYLOAD);
    iperf_set_test_num_streams(test, 1);
    iperf_set_test_json_output(test, 1);
    iperf_set_test_json_callback(test, quiet);
    Worker w = {.plan=p, .out=rtt};
    if (rtt) {
        current = &w;
        iperf_set_on_test_start_callback(test, started);
    }
    int rc = iperf_run_client(test);
    if (w.created && pthread_join(w.thread, NULL)) die("probe thread join failed");
    current = NULL;
    if (rc < 0) die(iperf_strerror(i_errno));
    if (rtt && (!w.created || !rtt->ok)) die("probe failed; check echo server or local scheduling");
    const char *s = iperf_get_test_json_output_string(test);
    cJSON *root = s ? cJSON_Parse(s) : NULL;
    const cJSON *end = field(root, "end");
    const cJSON *sender = field(end, "sum_sent"), *receiver = field(end, "sum_received");
    (void)number(field(receiver, "packets"), 1, 1e15);
    out->sent = number(field(sender, "bits_per_second"), 0, 1e15);
    out->received = number(field(receiver, "bits_per_second"), 0, 1e15);
    out->loss = number(field(receiver, "lost_percent"), 0, 100);
    cJSON_Delete(root);
    iperf_free_test(test);
}
