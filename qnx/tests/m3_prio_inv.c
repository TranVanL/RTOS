/* M3 WP6: priority inversion with and without priority inheritance (REQ-PERF-003).
 *   m3_prio_inv pi|nopi [--iters N] [--crit C]
 * All threads are pinned to CPU 0 (run mask) so a medium thread really can starve a low one.
 *   L (SCHED_FIFO 10) takes the mutex and works inside the critical section.
 *   H (SCHED_FIFO 50) then blocks on the mutex.  M (SCHED_FIFO 30) is CPU-bound for N iterations.
 * Deterministic criterion (counts/order, not ms):
 *   PI    : H acquires while M has done ~0 iterations; order  L_unlock, H_acquire, M_done
 *   NO PI : H acquires only after M finished all N;    order  M_done, L_unlock, H_acquire
 * Cycles are informational SIL data (QNX VM, QEMU/KVM). */
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <unistd.h>

static pthread_mutex_t mtx;
static sem_t l_locked, h_done, m_done_sem;
static volatile uint64_t m_progress;
static uint64_t n_iters = 20000000ULL, crit_iters = 5000000ULL;
static _Atomic int ev_idx;
static const char *ev_name[8];
static uint64_t ev_cyc[8];
static uint64_t m_at_h_acquire, t_h_start, t_h_acquire;

static void pin_cpu0(void)
{
    unsigned mask = 1U;
    ThreadCtl(_NTO_TCTL_RUNMASK, (void *)(uintptr_t)mask);
}
static void event(const char *n)
{
    int i = atomic_fetch_add(&ev_idx, 1);
    if (i < 8) { ev_name[i] = n; ev_cyc[i] = ClockCycles(); }
}

static void *low(void *a)
{
    (void)a; pin_cpu0();
    pthread_mutex_lock(&mtx);
    sem_post(&l_locked);                       /* main (prio 60) preempts here and starts H and M */
    for (volatile uint64_t i = 0; i < crit_iters; ++i) { }
    event("L_unlock");
    pthread_mutex_unlock(&mtx);
    return NULL;
}
static void *high(void *a)
{
    (void)a; pin_cpu0();
    t_h_start = ClockCycles();
    pthread_mutex_lock(&mtx);                  /* blocks while L holds it */
    t_h_acquire = ClockCycles();
    m_at_h_acquire = m_progress;
    event("H_acquire");
    pthread_mutex_unlock(&mtx);
    sem_post(&h_done);
    return NULL;
}
static void *medium(void *a)
{
    (void)a; pin_cpu0();
    for (uint64_t i = 0; i < n_iters; ++i) { m_progress = i + 1; }
    event("M_done");
    sem_post(&m_done_sem);
    return NULL;
}
static pthread_t start(void *(*fn)(void *), int prio)
{
    pthread_attr_t at; struct sched_param sp = { .sched_priority = prio }; pthread_t t;
    pthread_attr_init(&at);
    pthread_attr_setinheritsched(&at, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&at, SCHED_FIFO);
    pthread_attr_setschedparam(&at, &sp);
    if (pthread_create(&t, &at, fn, NULL) != 0) { perror("pthread_create"); exit(2); }
    return t;
}

int main(int argc, char **argv)
{
    if (argc < 2 || (strcmp(argv[1], "pi") && strcmp(argv[1], "nopi"))) { fprintf(stderr, "usage: m3_prio_inv pi|nopi\n"); return 2; }
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--iters")) n_iters = strtoull(argv[i + 1], NULL, 10);
        else if (!strcmp(argv[i], "--crit")) crit_iters = strtoull(argv[i + 1], NULL, 10);
    }
    int pi = !strcmp(argv[1], "pi");
    struct sched_param sp = { .sched_priority = 60 };
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) { perror("main sched"); return 2; }
    pin_cpu0();
    pthread_mutexattr_t ma; pthread_mutexattr_init(&ma);
    pthread_mutexattr_setprotocol(&ma, pi ? PTHREAD_PRIO_INHERIT : PTHREAD_PRIO_NONE);
    pthread_mutex_init(&mtx, &ma);
    sem_init(&l_locked, 0, 0); sem_init(&h_done, 0, 0); sem_init(&m_done_sem, 0, 0);

    pthread_t tl = start(low, 10);
    sem_wait(&l_locked);                       /* L holds the mutex */
    pthread_t th = start(high, 50);            /* main (60) keeps the CPU until it blocks on sem_wait below */
    pthread_t tm = start(medium, 30);
    sem_wait(&h_done);
    sem_wait(&m_done_sem);
    pthread_join(tl, NULL); pthread_join(th, NULL); pthread_join(tm, NULL);

    char order[128] = ""; int n = atomic_load(&ev_idx);
    for (int i = 0; i < n && i < 8; ++i) { strcat(order, i ? "," : ""); strcat(order, ev_name[i]); }
    printf("PRIO_INV pid=%d mode=%s N=%llu m_at_H_acquire=%llu order=%s H_wait_cycles=%llu\n", (int)getpid(), argv[1],
           (unsigned long long)n_iters, (unsigned long long)m_at_h_acquire, order,
           (unsigned long long)(t_h_acquire - t_h_start));
    return 0;
}
