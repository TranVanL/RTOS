/*
 * motion_sup (M3 WP3): QNX central supervisor, thread/priority/timer skeleton of
 * ARCH 3.2 without the resmgr (WP4). Build: see tools/qnxvm/run_m3_wp3.sh.
 *
 *   E-stop      SCHED_FIFO 60  event   sends ESTOP 3x, 1 ms apart, own socket
 *   HB monitor  SCHED_FIFO 55  5 ms    checks telemetry against position limits
 *   Control     SCHED_FIFO 50  10 ms   steps the safety FSM, writes SHM, kicks TX
 *   Frame RX    SCHED_FIFO 45  event   UDP -> frame -> E2E -> shared counters
 *   Frame TX    SCHED_FIFO 45  event   sends CMD
 *
 * Pass/fail uses counts (ticks, frames), never milliseconds. SIL, QEMU/KVM VM.
 */
#include "can_matrix.h"
#include "motion_ipc.h"
#include "motion_shm.h"
#include "proto_link.h"
#include "safety_fsm.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define SHM_NAME "/motion_state"
#define PULSE_TICK (_PULSE_CODE_MINAVAIL + 1)
#define PULSE_EVENT (_PULSE_CODE_MINAVAIL + 2)
#define PULSE_QUIT (_PULSE_CODE_MINAVAIL + 3)

typedef struct {
    const char *peer_ip;
    int peer_port, estop_port, local_port;
    unsigned ticks;          /* control ticks to run, then exit */
    bool autoenable;         /* test only: enable once READY and a heartbeat was seen */
    unsigned estop_at;       /* test only: request E-stop at this tick (0 = never) */
    int32_t target_pos;
    uint32_t vel_rpm;
} cfg_t;

typedef struct {
    pthread_mutex_t lock;     /* PTHREAD_PRIO_INHERIT */
    unsigned hb_count, e2e_err_count;       /* since last control tick */
    uint32_t hb_good_total, e2e_err_total, hb_miss_total, rx_frames;
    uint32_t node_state, fault_code;
    int32_t position, velocity;
    bool fault_record_seen;
} shared_t;

static cfg_t cfg;
static shared_t sh;
static motion_shm_t *shm;
static _Atomic int running = 1;
static _Atomic int limit_violation, estop_request;
static _Atomic uint32_t monitor_ticks, estop_sent, tx_cmd, ctl_ticks;
static _Atomic int last_state, last_cause;
static int chid_estop, chid_tx;

static uint32_t now_ms(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + (unsigned)(t.tv_nsec / 1000000));
}

static int udp_socket(int bind_port)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("socket"); exit(2); }
    if (bind_port > 0) {
        struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)bind_port),
                                 .sin_addr.s_addr = INADDR_ANY };
        if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) { perror("bind"); exit(2); }
    }
    return s;
}

static struct sockaddr_in peer_addr(int port)
{
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    inet_pton(AF_INET, cfg.peer_ip, &a.sin_addr);
    return a;
}

static void log_thread(const char *name)
{
    int policy; struct sched_param sp;
    pthread_getschedparam(pthread_self(), &policy, &sp);
    printf("THREAD %-10s tid=%d policy=%s prio=%d\n", name, (int)pthread_self(),
           policy == SCHED_FIFO ? "FIFO" : policy == SCHED_RR ? "RR" : "OTHER", sp.sched_priority);
    fflush(stdout);
}

static int start_timer(int chid, int pulse_prio, long period_ns)
{
    struct sigevent ev; timer_t t;
    int coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    SIGEV_PULSE_INIT(&ev, coid, pulse_prio, PULSE_TICK, 0);   /* explicit: receiver runs at its own priority */
    if (timer_create(CLOCK_MONOTONIC, &ev, &t) != 0) { perror("timer_create"); exit(2); }
    struct itimerspec its = { .it_value = { 0, period_ns }, .it_interval = { 0, period_ns } };
    if (timer_settime(t, 0, &its, NULL) != 0) { perror("timer_settime"); exit(2); }
    return coid;
}

/* ---- E-stop thread (60) ---- */
static void *estop_thread(void *arg)
{
    (void)arg;
    log_thread("estop");
    int sock = udp_socket(0);
    struct sockaddr_in dst = peer_addr(cfg.estop_port);
    uint16_t counter = 0; struct _pulse p;
    for (;;) {
        if (MsgReceivePulse(chid_estop, &p, sizeof p, NULL) < 0) { continue; }
        if (p.code == PULSE_QUIT) { break; }
        if (p.code != PULSE_EVENT) { continue; }
        for (int copy = 0; copy < 3; ++copy) {
            struct can_matrix_estop_t m = { .command = 1, .reason = (uint8_t)p.value.sival_int };
            uint8_t raw[8], dg[PROTO_DATAGRAM_MAX]; size_t n = 0;
            can_matrix_estop_pack(raw, &m, sizeof raw);
            if (proto_encode(0x010, raw, 2, counter++, dg, sizeof dg, &n) == PROTO_OK) {
                sendto(sock, dg, n, 0, (struct sockaddr *)&dst, sizeof dst);
                atomic_fetch_add(&estop_sent, 1);
            }
            if (copy < 2) { struct timespec ts = { 0, 1000000 }; nanosleep(&ts, NULL); }
        }
    }
    close(sock);
    return NULL;
}

/* ---- HB monitor (55): fast limit check on the latest telemetry ---- */
static void *monitor_thread(void *arg)
{
    (void)arg;
    log_thread("hb_monitor");
    int chid = ChannelCreate(0);
    start_timer(chid, 55, 5000000);
    struct _pulse p;
    while (atomic_load(&running)) {
        if (MsgReceivePulse(chid, &p, sizeof p, NULL) < 0 || p.code != PULSE_TICK) { continue; }
        pthread_mutex_lock(&sh.lock);
        int32_t pos = sh.position;
        pthread_mutex_unlock(&sh.lock);
        if (pos < MOTION_POS_MIN || pos > MOTION_POS_MAX) { atomic_store(&limit_violation, 1); }
        atomic_fetch_add(&monitor_ticks, 1);
    }
    return NULL;
}

/* ---- Frame RX (45) ---- */
static void *rx_thread(void *arg)
{
    int sock = *(int *)arg;
    log_thread("frame_rx");
    e2e_rx_state_t st_hb, st_tm, st_fr;
    e2e_rx_reset(&st_hb, now_ms()); e2e_rx_reset(&st_tm, now_ms()); e2e_rx_reset(&st_fr, now_ms());
    struct timeval tv = { 0, 100000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    while (atomic_load(&running)) {
        uint8_t dg[128], pl[PROTO_PAYLOAD_MAX]; uint16_t id = 0; size_t plen = 0;
        ssize_t n = recv(sock, dg, sizeof dg, 0);
        if (n <= 0) { continue; }
        e2e_rx_state_t *st = NULL;
        if (!proto_peek_can_id(dg, (size_t)n, &id)) { continue; }
        st = id == 0x1F0 ? &st_hb : id == 0x180 ? &st_tm : id == 0x1F1 ? &st_fr : NULL;
        proto_result_t r = st ? proto_decode(dg, (size_t)n, st, now_ms(), &id, pl, &plen) : PROTO_ERR_UNKNOWN_ID;
        pthread_mutex_lock(&sh.lock);
        sh.rx_frames++;
        if (r != PROTO_OK) {
            sh.e2e_err_count++; sh.e2e_err_total++;
        } else if (id == 0x1F0) {
            struct can_matrix_heartbeat_t m;
            if (can_matrix_heartbeat_unpack(&m, pl, plen) == 0) {
                sh.hb_count++; sh.hb_good_total++;
                sh.node_state = m.node_state; sh.fault_code = m.fault_code;
            }
        } else if (id == 0x180) {
            struct can_matrix_telem_t m;
            if (can_matrix_telem_unpack(&m, pl, plen) == 0) { sh.position = m.position; sh.velocity = m.velocity; }
        } else if (id == 0x1F1) {
            sh.fault_record_seen = true;
        }
        pthread_mutex_unlock(&sh.lock);
    }
    return NULL;
}

/* ---- Frame TX (45): one CMD per pulse ---- */
static void *tx_thread(void *arg)
{
    (void)arg;
    log_thread("frame_tx");
    int sock = udp_socket(0);
    struct sockaddr_in dst = peer_addr(cfg.peer_port);
    uint16_t counter = 0; struct _pulse p;
    for (;;) {
        if (MsgReceivePulse(chid_tx, &p, sizeof p, NULL) < 0) { continue; }
        if (p.code == PULSE_QUIT) { break; }
        if (p.code != PULSE_EVENT) { continue; }
        uint32_t pct = (uint32_t)p.value.sival_int & 0xFFu, enable = ((uint32_t)p.value.sival_int >> 8) & 1u;
        struct can_matrix_cmd_t m = { .mode = enable ? 1 : 0,
                                      .vel_limit = (uint16_t)(cfg.vel_rpm * pct / 100u),
                                      .target_pos = cfg.target_pos };
        uint8_t raw[16], dg[PROTO_DATAGRAM_MAX]; size_t n = 0;
        can_matrix_cmd_pack(raw, &m, sizeof raw);
        if (proto_encode(0x100, raw, 7, counter++, dg, sizeof dg, &n) == PROTO_OK) {
            sendto(sock, dg, n, 0, (struct sockaddr *)&dst, sizeof dst);
            atomic_fetch_add(&tx_cmd, 1);
        }
    }
    close(sock);
    return NULL;
}

/* ---- Control (50): the only thread that steps the FSM and writes SHM ---- */
static void *control_thread(void *arg)
{
    (void)arg;
    log_thread("control");
    int chid = ChannelCreate(0);
    int tx_coid = ConnectAttach(0, 0, chid_tx, _NTO_SIDE_CHANNEL, 0);
    int es_coid = ConnectAttach(0, 0, chid_estop, _NTO_SIDE_CHANNEL, 0);
    start_timer(chid, 50, 10000000);
    safety_fsm_t fsm; safety_fsm_init(&fsm);
    motion_state_t ms; memset(&ms, 0, sizeof ms);
    struct _pulse p; unsigned tick = 0; bool prev_safe = false;

    while (atomic_load(&running)) {
        if (MsgReceivePulse(chid, &p, sizeof p, NULL) < 0 || p.code != PULSE_TICK) { continue; }
        tick++;
        safety_inputs_t in; memset(&in, 0, sizeof in);
        pthread_mutex_lock(&sh.lock);
        in.hb_valid = sh.hb_count > 0;
        in.e2e_errors = (uint8_t)(sh.e2e_err_count > 255 ? 255 : sh.e2e_err_count);
        in.node_fault = sh.fault_record_seen || sh.fault_code != 0;
        if (!in.hb_valid && fsm.state >= SAFETY_RUN && fsm.state <= SAFETY_DEGRADED) { sh.hb_miss_total++; }
        sh.hb_count = 0; sh.e2e_err_count = 0;
        ms.node_state = sh.node_state; ms.fault_code = sh.fault_code;
        ms.position = sh.position; ms.velocity = sh.velocity;
        ms.hb_good = sh.hb_good_total; ms.hb_missed = sh.hb_miss_total; ms.e2e_errors = sh.e2e_err_total;
        pthread_mutex_unlock(&sh.lock);
        in.self_test_ok = motion_shm_validate(shm);
        in.limit_violation = atomic_load(&limit_violation) != 0;
        in.estop = atomic_load(&estop_request) != 0 || (cfg.estop_at && tick == cfg.estop_at);
        in.cmd_enable = cfg.autoenable && fsm.state == SAFETY_READY && in.hb_valid;

        safety_outputs_t out;
        safety_fsm_step(&fsm, &in, &out);

        ms.fsm_state = (uint32_t)out.state; ms.fsm_cause = (uint32_t)out.cause;
        ms.tick = tick; ms.target_pos = cfg.target_pos; ms.cmd_counter = atomic_load(&tx_cmd);
        motion_shm_write(shm, &ms);
        atomic_store(&last_state, (int)out.state); atomic_store(&last_cause, (int)out.cause);
        atomic_store(&ctl_ticks, tick);

        if (out.send_estop) {
            MsgSendPulse(es_coid, 60, PULSE_EVENT, (int)out.cause);
        }
        bool safe = out.state == SAFETY_SAFE_STOP;
        if (!safe || !prev_safe) {   /* keep commanding (disable) once on entry, then stop */
            MsgSendPulse(tx_coid, 45, PULSE_EVENT,
                         (int)(out.vel_limit_pct | (out.output_enable ? 0x100u : 0u)));
        }
        prev_safe = safe;
        if (cfg.ticks && tick >= cfg.ticks) { atomic_store(&running, 0); }
    }
    return NULL;
}

static pthread_t spawn(void *(*fn)(void *), void *arg, int prio)
{
    pthread_attr_t a; struct sched_param sp = { .sched_priority = prio }; pthread_t t;
    pthread_attr_init(&a);
    pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&a, SCHED_FIFO);
    pthread_attr_setschedparam(&a, &sp);
    int rc = pthread_create(&t, &a, fn, arg);
    if (rc != 0) { fprintf(stderr, "pthread_create prio %d failed: %s\n", prio, strerror(rc)); exit(2); }
    return t;
}

int main(int argc, char **argv)
{
    cfg = (cfg_t){ .peer_ip = "127.0.0.1", .peer_port = 5100, .estop_port = 5101, .local_port = 5000,
                   .ticks = 100, .target_pos = 1000, .vel_rpm = 1000 };
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--autoenable")) { cfg.autoenable = true; }
        else if (i + 1 < argc && !strcmp(argv[i], "--peer-ip")) { cfg.peer_ip = argv[++i]; }
        else if (i + 1 < argc && !strcmp(argv[i], "--peer-port")) { cfg.peer_port = atoi(argv[++i]); }
        else if (i + 1 < argc && !strcmp(argv[i], "--estop-port")) { cfg.estop_port = atoi(argv[++i]); }
        else if (i + 1 < argc && !strcmp(argv[i], "--local-port")) { cfg.local_port = atoi(argv[++i]); }
        else if (i + 1 < argc && !strcmp(argv[i], "--ticks")) { cfg.ticks = (unsigned)atoi(argv[++i]); }
        else if (i + 1 < argc && !strcmp(argv[i], "--estop-at")) { cfg.estop_at = (unsigned)atoi(argv[++i]); }
        else { fprintf(stderr, "usage: motion_sup [--autoenable] [--peer-ip IP] [--peer-port P] [--estop-port P] [--local-port P] [--ticks N] [--estop-at N]\n"); return 2; }
    }

    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&sh.lock, &ma);

    int fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0644);
    if (fd < 0 || ftruncate(fd, sizeof(motion_shm_t)) != 0) { perror("shm"); return 2; }
    shm = mmap(NULL, sizeof(motion_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); return 2; }
    motion_shm_init(shm);

    int rx_sock = udp_socket(cfg.local_port);
    chid_estop = ChannelCreate(0);
    chid_tx = ChannelCreate(0);

    pthread_t te = spawn(estop_thread, NULL, 60);
    pthread_t tm = spawn(monitor_thread, NULL, 55);
    pthread_t tr = spawn(rx_thread, &rx_sock, 45);
    pthread_t tt = spawn(tx_thread, NULL, 45);
    pthread_t tc = spawn(control_thread, NULL, 50);

    pthread_join(tc, NULL);
    pthread_join(tm, NULL);
    pthread_join(tr, NULL);
    int c = ConnectAttach(0, 0, chid_estop, _NTO_SIDE_CHANNEL, 0);
    MsgSendPulse(c, 60, PULSE_QUIT, 0);
    c = ConnectAttach(0, 0, chid_tx, _NTO_SIDE_CHANNEL, 0);
    MsgSendPulse(c, 60, PULSE_QUIT, 0);
    pthread_join(te, NULL);
    pthread_join(tt, NULL);

    pthread_mutex_lock(&sh.lock);
    printf("SUMMARY ticks=%u monitor_ticks=%u rx_frames=%u hb_good=%u hb_miss=%u e2e_err=%u "
           "tx_cmd=%u estop_sent=%u final_state=%s cause=%d\n",
           atomic_load(&ctl_ticks), atomic_load(&monitor_ticks), sh.rx_frames, sh.hb_good_total,
           sh.hb_miss_total, sh.e2e_err_total, atomic_load(&tx_cmd), atomic_load(&estop_sent),
           safety_state_name((safety_state_t)atomic_load(&last_state)), atomic_load(&last_cause));
    pthread_mutex_unlock(&sh.lock);
    shm_unlink(SHM_NAME);
    return 0;
}
