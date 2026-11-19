#include "iperf_config.h"
#include "iperf_api.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int run_udp_once(uint64_t rate_bps)
{
    struct iperf_test *test = iperf_new_test();
    if (test == NULL) {
        fprintf(stderr, "Could not create iperf test\n");
        return -1;
    }

    if (iperf_defaults(test) < 0 || set_protocol(test, Pudp) < 0) {
        fprintf(stderr, "Could not configure UDP: %s\n", iperf_strerror(i_errno));
        iperf_free_test(test);
        return -1;
    }

    iperf_set_test_role(test, 'c');
    iperf_set_test_server_hostname(test, "127.0.0.1");
    iperf_set_test_server_port(test, 5201);
    iperf_set_test_rate(test, rate_bps);
    iperf_set_test_duration(test, 3);
    iperf_set_test_blksize(test, 1200);
    iperf_set_test_num_streams(test, 1);

    int result = iperf_run_client(test);
    if (result < 0)
        fprintf(stderr, "UDP test failed: %s\n", iperf_strerror(i_errno));

    iperf_free_test(test);
    return result;
}

int main(void)
{
const uint64_t rates[] = {1000000, 2000000, 3000000};
    const size_t rate_count = sizeof(rates) / sizeof(rates[0]);
    const int repeats = 3;

    for (size_t i = 0; i < rate_count; i++) {
        for (int trial = 1; trial <= repeats; trial++) {
            printf("\n=== Target: %" PRIu64 " bps | Trial: %d/%d ===\n",
                   rates[i], trial, repeats);
            fflush(stdout);

            if (run_udp_once(rates[i]) < 0)
                return EXIT_FAILURE;

            if (trial < repeats || i + 1 < rate_count)
                sleep(1);
        }
    }

    return EXIT_SUCCESS;
}
