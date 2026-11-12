/* M3 fake node: sends HEARTBEAT + TELEM every period, receives CMD (cmd_port) and
 * ESTOP (estop_port) from motion_sup and counts them. Stand-in for the FreeRTOS node
 * until M4. Usage: m3_peer [--sup-ip IP] [--sup-port 5000] [--cmd-port 5100]
 *   [--estop-port 5101] [--ticks N] [--stop-after K] [--period-ms 10] [--linger-ms 1500]
 *   [--pos P] [--fault-code C]
 * --stop-after K: send no more heartbeat/telemetry after tick K (0 = send all N). */
#include "can_matrix.h"
#include "proto_link.h"

#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int udp_bind(int port)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port), .sin_addr.s_addr = INADDR_ANY };
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) != 0) { perror("bind"); exit(2); }
    return s;
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv)
{
    const char *ip = "127.0.0.1"; int sup_port = 5000, cmd_port = 5100, es_port = 5101;
    unsigned ticks = 100, stop_after = 0, period_ms = 10, linger_ms = 1500; int32_t pos = 0; unsigned fault = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--sup-ip")) ip = argv[i + 1];
        else if (!strcmp(argv[i], "--sup-port")) sup_port = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--cmd-port")) cmd_port = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--estop-port")) es_port = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--ticks")) ticks = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--stop-after")) stop_after = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--period-ms")) period_ms = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--linger-ms")) linger_ms = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--pos")) pos = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--fault-code")) fault = (unsigned)atoi(argv[i + 1]);
        else { fprintf(stderr, "bad arg %s\n", argv[i]); return 2; }
    }
    int cs = udp_bind(cmd_port), es = udp_bind(es_port), tx = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in dst = { .sin_family = AF_INET, .sin_port = htons((uint16_t)sup_port) };
    inet_pton(AF_INET, ip, &dst.sin_addr);
    e2e_rx_state_t st_cmd, st_es; e2e_rx_reset(&st_cmd, 0); e2e_rx_reset(&st_es, 0);
    unsigned hb_sent = 0, cmd_ok = 0, cmd_bad = 0, es_ok = 0, es_bad = 0, last_mode = 99;
    uint16_t c_hb = 0, c_tm = 0;
    double t0 = now(), end = t0 + (ticks * period_ms + linger_ms) / 1000.0, next = t0;
    unsigned tick = 0;

    while (now() < end) {
        if (tick < ticks && now() >= next) {
            next += period_ms / 1000.0; tick++;
            if (!stop_after || tick <= stop_after) {
                struct can_matrix_heartbeat_t h = { .node_state = 2, .alive_counter = (uint8_t)tick, .cpu_load = 10, .fault_code = (uint16_t)fault };
                struct can_matrix_telem_t t = { .position = pos, .velocity = 0, .current = 0, .output_state = 1 };
                uint8_t raw[16], dg[PROTO_DATAGRAM_MAX]; size_t n;
                can_matrix_heartbeat_pack(raw, &h, sizeof raw);
                if (proto_encode(0x1F0, raw, 5, c_hb++, dg, sizeof dg, &n) == PROTO_OK) { sendto(tx, dg, n, 0, (struct sockaddr *)&dst, sizeof dst); hb_sent++; }
                can_matrix_telem_pack(raw, &t, sizeof raw);
                if (proto_encode(0x180, raw, 9, c_tm++, dg, sizeof dg, &n) == PROTO_OK) { sendto(tx, dg, n, 0, (struct sockaddr *)&dst, sizeof dst); }
            }
        }
        struct pollfd pf[2] = { { cs, POLLIN, 0 }, { es, POLLIN, 0 } };
        if (poll(pf, 2, 1) <= 0) continue;
        for (int k = 0; k < 2; ++k) {
            if (!(pf[k].revents & POLLIN)) continue;
            uint8_t dg[128], pl[PROTO_PAYLOAD_MAX]; uint16_t id; size_t pn;
            ssize_t n = recv(pf[k].fd, dg, sizeof dg, 0);
            proto_result_t r = proto_decode(dg, (size_t)n, k == 0 ? &st_cmd : &st_es, 0, &id, pl, &pn);
            if (k == 0) {
                if (r == PROTO_OK && id == 0x100) { struct can_matrix_cmd_t m; can_matrix_cmd_unpack(&m, pl, pn); cmd_ok++; last_mode = m.mode; } else cmd_bad++;
            } else {
                if (r == PROTO_OK && id == 0x010) es_ok++; else es_bad++;
            }
        }
    }
    printf("PEER ticks=%u hb_sent=%u cmd_ok=%u cmd_bad=%u last_cmd_mode=%u estop_ok=%u estop_bad=%u\n",
           tick, hb_sent, cmd_ok, cmd_bad, last_mode, es_ok, es_bad);
    return 0;
}
