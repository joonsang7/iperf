#include "qos.h"
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void usage(FILE *out)
{
    fputs("Usage:\n"
          "  qos scan SERVER_IPV4 --loss PCT --rtt-p95 MS [--cap MBPS] [--out PLAN.json]\n"
          "  qos echo SERVER_IPV4 ALLOWED_CLIENT_IPV4\n"
          "  qos profile PLAN.json RESULT.json\n"
          "  qos compare BEFORE.json AFTER.json\n"
          "  qos scan SERVER_IPV4 LOSS_PCT CAP_MBPS P95_LIMIT_MS PLAN.json (legacy grid)\n"
          "  Loss: 0..100%; RTT limit: 0..500 ms; optional cap: 0.1..10000 Mbps.\n",out);
}
static double argument(const char *name, const char *s, double min, double max)
{
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (errno || end == s || *end || !isfinite(v) || v < min || v > max) {
        fprintf(stderr, "%s must be a number in [%g, %g]\n", name, min, max);
        exit(EXIT_FAILURE);
    }
    return v;
}
static uint64_t cap_bps(const char *value)
{
    return (uint64_t)llround(argument("cap (Mbps)", value, 0.1, 10000) * 1e6);
}
static void new_output(const char *path)
{
    if (!*path) die("empty output filename");
    if (access(path, F_OK) == 0) die("output already exists; choose another filename");
    if (errno != ENOENT) { perror(path); exit(EXIT_FAILURE); }
}
static void run_scan(int argc, char **argv)
{
    /* argv[0] is the server address; the remaining elements are options. */
    Plan p = {0};
    if (!address_ok(argv[0])) die("expected a unicast server IPv4 address");
    strcpy(p.server, argv[0]);
    uint64_t cap = 0;
    const char *output = NULL;
    int legacy = argc == 5 && argv[1][0] != '-';
    if (legacy) {
        p.loss_limit = argument("loss (%)", argv[1], 0, 100);
        cap = cap_bps(argv[2]);
        p.rtt_limit = argument("p95 (ms)", argv[3], 0, TIMEOUT_MS);
        output = argv[4];
    } else {
        static const struct option options[] = {
            {"loss", required_argument, NULL, 'l'},
            {"rtt-p95", required_argument, NULL, 'r'},
            {"cap", required_argument, NULL, 'c'},
            {"out", required_argument, NULL, 'o'},
            {NULL, 0, NULL, 0}
        };
        p.loss_limit = p.rtt_limit = -1;
        int opt;
        while ((opt = getopt_long(argc, argv, "", options, NULL)) != -1) {
            switch (opt) {
            case 'l': p.loss_limit = argument("--loss", optarg, 0, 100); break;
            case 'r': p.rtt_limit = argument("--rtt-p95", optarg, 0, TIMEOUT_MS); break;
            case 'c': cap = cap_bps(optarg); break;
            case 'o': output = optarg; break;
            default: usage(stderr); exit(EXIT_FAILURE);
            }
        }
        if (optind != argc || p.loss_limit < 0 || p.rtt_limit < 0)
            die("expected --loss PCT and --rtt-p95 MS; use --help for usage");
    }
    if (!output) {
        if (mkdir("results", 0755) < 0 && errno != EEXIST) die("cannot create results directory");
        output = "results/plan.json";
    }
    new_output(output);
    if (source_address(p.server, p.client)) die("cannot determine client address");
    printf("Client %s -> server %s | loss <= %.3f%% | profile p95 <= %.3f ms\n",
           p.client, p.server, p.loss_limit, p.rtt_limit);
    if (legacy) scan(&p, cap, output);
    else scan_auto(&p, cap, output);
    printf("Saved plan: %s\n", output);
}
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        usage(stdout);
    } else if (argc >= 3 && !strcmp(argv[1], "scan")) {
        run_scan(argc-2, argv+2);
    } else if (argc == 4 && !strcmp(argv[1], "echo")) {
        echo_server(argv[2], argv[3]);
    } else if (argc == 4 && !strcmp(argv[1], "profile")) {
        new_output(argv[3]);
        profile(argv[2], argv[3]);
    } else if (argc == 4 && !strcmp(argv[1], "compare")) {
        compare(argv[2], argv[3]);
    } else {
        usage(stderr);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
