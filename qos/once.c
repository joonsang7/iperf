#include "iperf_config.h"
#include "iperf_api.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    struct iperf_test *test = iperf_new_test();
    if (test == NULL) {
        fprintf(stderr, "Could not create iperf test\n");
        return EXIT_FAILURE;
    }

    if (iperf_defaults(test) < 0 || set_protocol(test, Pudp) < 0) {
        fprintf(stderr, "Could not configure UDP: %s\n", iperf_strerror(i_errno));
        iperf_free_test(test);
        return EXIT_FAILURE;
    }

    iperf_set_test_role(test, 'c');
    iperf_set_test_server_hostname(test, "127.0.0.1");
    iperf_set_test_server_port(test, 5201);
    iperf_set_test_rate(test, 3000000);  // 1 Mbps
    iperf_set_test_duration(test, 3);   // 3초
    iperf_set_test_blksize(test, 1200);  // 데이터그램 길이
    iperf_set_test_num_streams(test, 1);
    iperf_set_test_json_output(test, 1);

    int result = iperf_run_client(test);
    if (result < 0)
        fprintf(stderr, "UDP test failed: %s\n", iperf_strerror(i_errno));

    iperf_free_test(test);
    return result < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}