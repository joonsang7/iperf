#include "qos.h"
#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Three network-order words. Only echo exact requests from the chosen peer. */
enum { MAGIC = 0x514f5331 };
double now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) die("monotonic clock unavailable");
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
void sleep_ms(int ms)
{
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {}
}
int address_ok(const char *ip)
{
    struct in_addr a;
    return inet_pton(AF_INET, ip, &a) == 1 && a.s_addr != INADDR_ANY &&
           a.s_addr != INADDR_BROADCAST && !IN_MULTICAST(ntohl(a.s_addr));
}
static struct sockaddr_in address(const char *ip, int port)
{
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (!address_ok(ip) || inet_pton(AF_INET, ip, &a.sin_addr) != 1)
        die("expected a unicast IPv4 address");
    return a;
}
static int udp_connect(const char *server, const char *bind_ip)
{
    struct sockaddr_in to = address(server, ECHO_PORT);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    if (bind_ip) {
        struct sockaddr_in local = address(bind_ip, 0);
        if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) goto fail;
    }
    if (!connect(fd, (struct sockaddr *)&to, sizeof(to))) return fd;
fail:
    close(fd);
    return -1;
}
int source_address(const char *server, char *out)
{
    int fd = udp_connect(server, NULL);
    if (fd < 0) return -1;
    struct sockaddr_in local;
    socklen_t len = sizeof(local);
    int ok = getsockname(fd, (struct sockaddr *)&local, &len) == 0 &&
             inet_ntop(AF_INET, &local.sin_addr, out, INET_ADDRSTRLEN);
    close(fd);
    return ok ? 0 : -1;
}
void echo_server(const char *bind_ip, const char *peer)
{
    struct sockaddr_in local = address(bind_ip, ECHO_PORT), allowed = address(peer, 0);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0 || bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("echo bind"); die("check local IPv4 address and UDP port 5202");
    }
    printf("UDP echo %s:%d, client %s\n", bind_ip, ECHO_PORT, peer);
    for (;;) {
        uint32_t words[4]; /* Room to detect oversized packets. */
        struct sockaddr_in from;
        socklen_t size = sizeof(from);
        ssize_t n = recvfrom(fd, words, sizeof(words), 0, (struct sockaddr *)&from, &size);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom"); die("echo receive failed");
        }
        if (n == 12 && from.sin_addr.s_addr == allowed.sin_addr.s_addr &&
            ntohl(words[0]) == MAGIC)
            (void)sendto(fd, words, 12, 0, (struct sockaddr *)&from, size);
    }
}
void probe(const Plan *p, double anchor, Rtt *out)
{
    double sent[PROBES];
    uint32_t token = (uint32_t)(uint64_t)(anchor * 1000) ^ (uint32_t)getpid();
    double first = anchor + WARMUP_MS;
    int next = 0;
    out->ok = 0;
    for (int i = 0; i < PROBES; i++) out->ms[i] = -1;
    int fd = udp_connect(p->server, p->client);
    if (fd < 0) { perror("probe connect"); return; }
    for (;;) {
        double now = now_ms();
        if (next < PROBES && now >= first + next * PROBE_INTERVAL_MS) {
            /* A stalled sender must not catch up by emitting a burst. */
            if (now - (first + next * PROBE_INTERVAL_MS) >= PROBE_INTERVAL_MS) break;
            uint32_t msg[3] = {htonl(MAGIC), htonl(token), htonl((uint32_t)next)};
            sent[next] = now_ms();
            if (send(fd, msg, sizeof(msg), 0) != sizeof(msg)) { perror("probe send"); break; }
            next++;
        }
        double wake = next < PROBES ? first + next * PROBE_INTERVAL_MS :
                                      sent[PROBES - 1] + TIMEOUT_MS;
        if (next == PROBES && now_ms() >= wake) { out->ok = 1; break; }
        struct pollfd pollfd = {fd, POLLIN, 0};
        int ready = poll(&pollfd, 1, (int)ceil(fmax(0, wake - now_ms())));
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) { perror("probe poll"); break; }
        if (ready > 0) {
            uint32_t msg[4];
            ssize_t len = recv(fd, msg, sizeof(msg), 0);
            double received = now_ms();
            if (len < 0) { perror("probe receive"); break; }
            if (len != 12 || ntohl(msg[0]) != MAGIC || ntohl(msg[1]) != token) continue;
            uint32_t seq = ntohl(msg[2]);
            if (seq < (uint32_t)next && out->ms[seq] < 0 &&
                received - sent[seq] <= TIMEOUT_MS)
                out->ms[seq] = received - sent[seq];
        }
    }
    close(fd);
}
