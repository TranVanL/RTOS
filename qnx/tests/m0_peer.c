/* M0 acceptance peer (QNX process). Both directions over UDP against the FreeRTOS node:
 *   dir 1 (QNX -> node -> QNX): send numbered datagrams to node:5000 at --rate Hz, node echoes, count replies.
 *   dir 2 (node -> QNX -> node): node sends numbered probes to us on :5100; we echo them and count gaps in seq.
 * PASS iff dir1 lost==0 && mismatch==0 && dir2 lost==0 (counts, not milliseconds).
 * Build: qcc -Vgcc_ntox86_64 -o m0_peer m0_peer.c -lsocket       Usage: m0_peer <node_ip> <duration_s> <rate_hz> */
#include <arpa/inet.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NODE_PORT 5000
#define PROBE_PORT 5100
#define PROBE_MAGIC 0x4D305052u
#define PKT_WORDS 8

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static int udp_bind(int port)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = INADDR_ANY };
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) != 0) { perror("bind"); exit(2); }
    return s;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s <node_ip> <duration_s> <rate_hz>\n", argv[0]); return 2; }
    struct sockaddr_in node = { .sin_family = AF_INET, .sin_port = htons(NODE_PORT) };
    if (inet_pton(AF_INET, argv[1], &node.sin_addr) != 1) { fprintf(stderr, "bad ip\n"); return 2; }
    double duration = atof(argv[2]), period = 1.0 / atof(argv[3]);

    int s1 = udp_bind(0), s2 = udp_bind(PROBE_PORT);
    struct pollfd pf[2] = { { s1, POLLIN, 0 }, { s2, POLLIN, 0 } };
    uint32_t sent = 0, recv1 = 0, bad = 0;
    uint32_t first2 = 0, last2 = 0, recv2 = 0; int have2 = 0;
    double t0 = now(), t_next = t0, t_end = t0 + duration, t_drain = t_end + 2.0, t_rep = t0 + 30;

    for (;;) {
        double t = now();
        if (t >= t_drain) break;
        if (t < t_end && t >= t_next) {
            uint32_t p[PKT_WORDS];
            for (int i = 0; i < PKT_WORDS; i++) p[i] = sent * 2654435761u + i;
            p[0] = sent;
            if (sendto(s1, p, sizeof p, 0, (struct sockaddr *)&node, sizeof node) == (ssize_t)sizeof p) sent++;
            t_next += period;
        }
        double wake = (t < t_end ? t_next : t_drain);
        int ms = (int)((wake - now()) * 1000.0); if (ms < 0) ms = 0;
        if (poll(pf, 2, ms) > 0) {
            uint32_t p[PKT_WORDS]; struct sockaddr_in from; socklen_t fl;
            if (pf[0].revents & POLLIN) {
                fl = sizeof from; ssize_t n = recvfrom(s1, p, sizeof p, 0, (struct sockaddr *)&from, &fl);
                if (n == (ssize_t)sizeof p) {
                    int ok = 1; for (int i = 1; i < PKT_WORDS; i++) if (p[i] != p[0] * 2654435761u + i) ok = 0;
                    if (ok) recv1++; else bad++;
                } else if (n >= 0) bad++;
            }
            if (pf[1].revents & POLLIN) {
                fl = sizeof from; ssize_t n = recvfrom(s2, p, sizeof p, 0, (struct sockaddr *)&from, &fl);
                if (n == (ssize_t)sizeof p && p[0] == PROBE_MAGIC) {
                    if (!have2) { first2 = p[1]; have2 = 1; }
                    last2 = p[1]; recv2++;
                    sendto(s2, p, sizeof p, 0, (struct sockaddr *)&from, fl);
                }
            }
        }
        if (now() >= t_rep) { t_rep += 30; printf("t=%.0fs dir1 sent=%u recv=%u | dir2 recv=%u\n", now() - t0, sent, recv1, recv2); fflush(stdout); }
    }
    uint32_t exp2 = have2 ? last2 - first2 + 1 : 0, lost1 = sent - recv1, lost2 = exp2 - recv2;
    printf("dir1 (QNX->node->QNX): sent=%u received=%u lost=%u mismatch=%u\n", sent, recv1, lost1, bad);
    printf("dir2 (node->QNX):      expected=%u received=%u lost=%u\n", exp2, recv2, lost2);
    int pass = sent > 0 && lost1 == 0 && bad == 0 && recv2 > 0 && lost2 == 0;
    printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
