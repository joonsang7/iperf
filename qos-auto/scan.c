    #include "qos.h"
    #include <stdio.h>
    typedef enum { PASS, FAIL, UNCERTAIN } Verdict;
    typedef struct { uint64_t rate; double receive; } Best;

    static Verdict candidate(const Plan *p, uint64_t rate, Best *best)
    {
        Load t[TRIALS];
        Verdict v = PASS;
        for (int i = 0; i < TRIALS; i++) {
            measure(p, rate, &t[i], NULL);
            printf("target %.4f Mbps  %d/3  sent %.4f  received %.4f Mbps  loss %.3f%%\n",
                rate/1e6, i+1, t[i].sent/1e6, t[i].received/1e6, t[i].loss);
            if (!load_ok(t[i].sent, rate)) v = UNCERTAIN;
            else if (t[i].loss > p->loss_limit && v == PASS) v = FAIL;
            sleep_ms(200);
        }
        double median = median3(t[0].received, t[1].received, t[2].received);
        printf("  %s  median received %.4f Mbps\n",
            v == PASS ? "PASS" : v == FAIL ? "FAIL" : "INCONCLUSIVE", median/1e6);
        if (v == PASS && (!best->rate || median > best->receive ||
                        (median == best->receive && rate < best->rate)))
            *best = (Best){rate, median};
        return v;
    }
    static void finish_scan(Plan *p, Best best, const char *output)
    {
        if (best.rate == 0) die("no passing candidate among tested rates; no plan written");
        printf("Selected target %.4f Mbps; median received %.4f Mbps\n",
            best.rate/1e6, best.receive/1e6);
        for (int i = 0; i < STAGES; i++) p->rate[i] = best.rate*i/4;
        cJSON *j = make_plan(p);
        save_json(output, j);
        cJSON_Delete(j);
    }

    void scan(Plan *p, uint64_t cap, const char *output)
    {
        uint64_t grid[4];
        Verdict v[4];
        Best best = {0};
        for (int i = 0; i < 4; i++) {
            grid[i] = cap * (i+1) / 4;
            v[i] = candidate(p, grid[i], &best);
        }
        /* Recheck between different pass/fail neighbours at half-step resolution. */
        for (int i = 0; i < 3; i++)
            if (v[i] != UNCERTAIN && v[i+1] != UNCERTAIN && v[i] != v[i+1])
                candidate(p, (grid[i]+grid[i+1])/2, &best);
        if (v[0] == FAIL) candidate(p, grid[0]/2, &best);
        if (v[3] == PASS) puts("PASS through the test cap (시험 상한까지 합격).");
        finish_scan(p, best, output);
    }

    /* A bounded number of experiments even if loss never appears.
    * A lower search limit keeps five-second tests from using just a few packets. */
    enum { START_BPS = 1000000, FLOOR_BPS = 100000, MAX_CANDIDATES = 24 };
    static const uint64_t supported_max = UINT64_C(10000000000);

    void scan_auto(Plan *p, uint64_t optional_cap, const char *output)
    {
        const uint64_t limit = optional_cap ? optional_cap : supported_max;
        uint64_t rate = limit < START_BPS ? limit : START_BPS;
        uint64_t passed = 0, failed = 0;
        Best best = {0};
        const char *reason = "candidate budget reached";

        for (int step = 1; step <= MAX_CANDIDATES; step++) {
            printf("\nCandidate %d: %.4f Mbps\n", step, rate / 1e6);
            Verdict v = candidate(p, rate, &best);
            if (v == UNCERTAIN) {
                reason = "sender missed target; loss boundary not established at this rate";
                break;
            }
            if (v == PASS) passed = rate;
            else failed = rate;

            if (passed && failed) {
                /* Use a finer resolution on sub-Mbps links. */
                uint64_t resolution = passed / 8;
                if (resolution > 1000000) resolution = 1000000;
                if (resolution < 25000) resolution = 25000;
                printf("Loss boundary: PASS %.4f / FAIL %.4f Mbps\n", passed/1e6, failed/1e6);
                if (failed - passed <= resolution) {
                    reason = "pass/fail interval within resolution";
                    break;
                }
                rate = passed + (failed - passed) / 2;
            } else if (failed) {
                if (rate == FLOOR_BPS) {
                    reason = "lowest tested rate failed";
                    break;
                }
                rate /= 2;
                if (rate < FLOOR_BPS) rate = FLOOR_BPS;
            } else {
                if (rate == limit) {
                    reason = optional_cap ? "PASS through the user test cap (시험 상한까지 합격)"
                                        : "PASS through the supported range; upper boundary unknown";
                    break;
                }
                rate *= 2;
                if (rate > limit) rate = limit;
            }
        }
        printf("Scan stopped: %s\n", reason);
        finish_scan(p, best, output);
    }
