#ifndef QOS_H
#define QOS_H
#include <stdint.h>
#include <netinet/in.h>
#include "cjson.h"

enum { TRIALS = 3, STAGES = 5, SECONDS = 5, PAYLOAD = 1200,
       LOAD_PORT = 5201, ECHO_PORT = 5202, PROBES = 40,
       PROBE_INTERVAL_MS = 75, TIMEOUT_MS = 500, WARMUP_MS = 750 };
typedef struct {
    double sent, received, loss; /* bits/s, bits/s, receiver loss in percent */
} Load;
typedef struct {
    double ms[PROBES]; /* negative means deadline expired */
    int ok;
} Rtt;
typedef struct {
    char server[INET_ADDRSTRLEN], client[INET_ADDRSTRLEN];
    double loss_limit, rtt_limit;
    uint64_t rate[STAGES];
} Plan;
void die(const char *msg);
double now_ms(void);
void sleep_ms(int ms);
int address_ok(const char *ip);
int source_address(const char *server, char *out);
void echo_server(const char *bind_ip, const char *peer);
void probe(const Plan *plan, double anchor, Rtt *out);
void measure(const Plan *plan, uint64_t bps, Load *load, Rtt *rtt);
int load_ok(double actual, uint64_t target);
double median3(double a, double b, double c);
cJSON *read_json(const char *path);
void save_json(const char *path, cJSON *root);
const cJSON *field(const cJSON *obj, const char *key);
double number(const cJSON *node, double min, double max);
cJSON *make_plan(const Plan *plan);
Plan parse_plan(const cJSON *json);
void scan(Plan *plan, uint64_t cap, const char *output);
void scan_auto(Plan *plan, uint64_t optional_cap, const char *output);
void profile(const char *plan_path, const char *output);
void compare(const char *before, const char *after);
#endif
