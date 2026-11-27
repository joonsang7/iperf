#include "iperf_config.h"
#include "iperf_api.h"
#include "qos.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void die(const char *msg) { fprintf(stderr, "qos: %s\n", msg); exit(EXIT_FAILURE); }
const cJSON *field(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!v) { fprintf(stderr, "Missing field: %s\n", key); die("invalid JSON record"); }
    return v;
}
double number(const cJSON *v, double min, double max)
{
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) ||
        v->valuedouble < min || v->valuedouble > max) die("invalid JSON number");
    return v->valuedouble;
}
cJSON *read_json(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(EXIT_FAILURE); }
    if (fseek(f, 0, SEEK_END)) die("cannot seek JSON");
    long len = ftell(f);
    if (len < 1 || len > 4*1024*1024) die("unexpected JSON size");
    rewind(f);
    char *buf = malloc((size_t)len + 1);
    if (!buf || fread(buf, 1, len, f) != (size_t)len || fclose(f)) die("JSON read failed");
    if (memchr(buf, 0, len)) die("unexpected NUL in JSON");
    buf[len] = 0;
    cJSON *root = cJSON_ParseWithOpts(buf, NULL, 1);
    free(buf);
    if (!root) die("malformed JSON");
    return root;
}
/* Publish complete files only. An existing experiment is never overwritten. */
void save_json(const char *path, cJSON *root)
{
    size_t size = strlen(path) + sizeof(".XXXXXX");
    char *tmp = malloc(size);
    char *json = cJSON_Print(root);
    if (!tmp || !json) die("out of memory");
    snprintf(tmp, size, "%s.XXXXXX", path);
    int fd = mkstemp(tmp);
    if (fd < 0) { perror(path); exit(EXIT_FAILURE); }
    FILE *f = fdopen(fd, "w");
    int ok = 0;
    if (f) {
        ok = fprintf(f, "%s\n", json) >= 0;
        if (fclose(f)) ok = 0;
    } else close(fd);
    if (!ok || link(tmp, path)) { perror(path); unlink(tmp); die("could not save; use a fresh filename"); }
    unlink(tmp);
    free(tmp);
    cJSON_free(json);
}
cJSON *make_plan(const Plan *p)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "schema", "iperf-qos-plan-v1");
    cJSON_AddStringToObject(j, "libiperf", iperf_get_iperf_version());
    cJSON_AddStringToObject(j, "server_ipv4", p->server);
    cJSON_AddStringToObject(j, "client_ipv4", p->client);
    cJSON_AddStringToObject(j, "direction", "client-to-server");
    cJSON_AddNumberToObject(j, "load_port", LOAD_PORT);
    cJSON_AddNumberToObject(j, "echo_port", ECHO_PORT);
    cJSON_AddNumberToObject(j, "streams", 1);
    cJSON_AddNumberToObject(j, "payload_bytes", PAYLOAD);
    cJSON_AddNumberToObject(j, "load_seconds", SECONDS);
    cJSON_AddNumberToObject(j, "trials", TRIALS);
    cJSON_AddNumberToObject(j, "probes_per_trial", PROBES);
    cJSON_AddNumberToObject(j, "interval_ms", PROBE_INTERVAL_MS);
    cJSON_AddNumberToObject(j, "deadline_ms", TIMEOUT_MS);
    cJSON_AddNumberToObject(j, "warmup_ms", WARMUP_MS);
    cJSON_AddNumberToObject(j, "sender_tolerance_pct", 5);
    cJSON_AddNumberToObject(j, "loss_limit_pct", p->loss_limit);
    cJSON_AddNumberToObject(j, "p95_limit_ms", p->rtt_limit);
    cJSON *rates = cJSON_AddArrayToObject(j, "target_bps");
    for (int i = 0; i < STAGES; i++) cJSON_AddItemToArray(rates, cJSON_CreateNumber(p->rate[i]));
    return j;
}
static void get_ip(const cJSON *j, const char *key, char *out)
{
    const char *s = cJSON_GetStringValue(field(j, key));
    if (!s || !address_ok(s)) die("invalid plan IP");
    strcpy(out, s);
}
Plan parse_plan(const cJSON *j)
{
    Plan p = {0};
    get_ip(j, "server_ipv4", p.server);
    get_ip(j, "client_ipv4", p.client);
    p.loss_limit = number(field(j, "loss_limit_pct"), 0, 100);
    p.rtt_limit = number(field(j, "p95_limit_ms"), 0, TIMEOUT_MS);
    const cJSON *a = field(j, "target_bps");
    if (!cJSON_IsArray(a) || cJSON_GetArraySize(a) != STAGES) die("invalid load grid");
    for (int i = 0; i < STAGES; ++i) {
        double n = number(cJSON_GetArrayItem(a, i), 0, 1e10);
        if (floor(n) != n) die("expected integer bps");
        p.rate[i] = (uint64_t)n;
    }
    if (!p.rate[4]) die("invalid selected rate");
    for (int i = 0; i < STAGES; ++i)
        if (p.rate[i] != p.rate[4] * i / 4) die("invalid load grid");
    cJSON *expected = make_plan(&p);
    if (!cJSON_Compare(expected, j, 1)) die("incompatible plan, libiperf version or test settings");
    cJSON_Delete(expected);
    return p;
}
