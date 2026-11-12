#include "motion_shm.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define WRITES 300000U
#define READERS 3

/* Every word is derived from n, so a torn snapshot is detectable. */
static void make_state(motion_state_t *s, uint32_t n)
{
    uint32_t *w = (uint32_t *)s;
    for (unsigned i = 0; i < MOTION_STATE_WORDS; ++i) { w[i] = n * 2654435761u + i; }
}
static bool consistent(const motion_state_t *s, uint32_t *n_out)
{
    const uint32_t *w = (const uint32_t *)s;
    uint32_t n = (w[0]) * 0x0E8B2F51u;      /* inverse of 2654435761 mod 2^32 */
    for (unsigned i = 0; i < MOTION_STATE_WORDS; ++i) {
        if (w[i] != n * 2654435761u + i) { return false; }
    }
    *n_out = n;
    return true;
}

static motion_shm_t shm;
static _Atomic int done;
static _Atomic unsigned torn, ok_reads, bad_order, give_up;

static void *writer(void *arg)
{
    (void)arg;
    motion_state_t s;
    for (uint32_t n = 1; n <= WRITES; ++n) { make_state(&s, n); motion_shm_write(&shm, &s); }
    atomic_store(&done, 1);
    return NULL;
}
static void *reader(void *arg)
{
    (void)arg;
    uint32_t last_n = 0, last_seq = 0;
    while (!atomic_load(&done)) {
        motion_state_t s; uint32_t seq, n;
        if (!motion_shm_read(&shm, &s, 1000, &seq)) { atomic_fetch_add(&give_up, 1); continue; }
        if (!consistent(&s, &n)) { atomic_fetch_add(&torn, 1); continue; }
        if (n != 0 && n < last_n) { atomic_fetch_add(&bad_order, 1); }
        if ((seq & 1u) || seq < last_seq) { atomic_fetch_add(&bad_order, 1); }
        last_n = n; last_seq = seq;
        atomic_fetch_add(&ok_reads, 1);
    }
    return NULL;
}

static void test_single_thread(void)
{
    motion_state_t in, out; uint32_t seq = 99;
    motion_shm_init(&shm);
    CHECK(motion_shm_validate(&shm));
    CHECK(motion_shm_read(&shm, &out, 0, &seq) && seq == 0);
    memset(&in, 0, sizeof in); in.tick = 7; in.target_pos = -5; in.fsm_state = 3;
    motion_shm_write(&shm, &in);
    CHECK(motion_shm_read(&shm, &out, 0, &seq));
    CHECK(seq == 2 && out.tick == 7 && out.target_pos == -5 && out.fsm_state == 3);
    /* writer "mid-update" (odd seq): reader must give up, never block */
    atomic_store(&shm.seq, 3u);
    CHECK(!motion_shm_read(&shm, &out, 5, &seq));
    atomic_store(&shm.seq, 4u);
    CHECK(motion_shm_read(&shm, &out, 0, NULL));
    CHECK(!motion_shm_read(NULL, &out, 0, NULL) && !motion_shm_read(&shm, NULL, 0, NULL));
    shm.magic = 0; CHECK(!motion_shm_validate(&shm)); CHECK(!motion_shm_validate(NULL));
    motion_shm_init(NULL); motion_shm_write(NULL, &in); motion_shm_write(&shm, NULL);
}

static void test_concurrent(void)
{
    pthread_t w, r[READERS];
    motion_state_t s0; make_state(&s0, 0);
    motion_shm_init(&shm); motion_shm_write(&shm, &s0);
    atomic_store(&done, 0);
    for (int i = 0; i < READERS; ++i) { pthread_create(&r[i], NULL, reader, NULL); }
    pthread_create(&w, NULL, writer, NULL);
    pthread_join(w, NULL);
    for (int i = 0; i < READERS; ++i) { pthread_join(r[i], NULL); }
    printf("concurrent: ok_reads=%u torn=%u bad_order=%u give_up=%u\n",
           atomic_load(&ok_reads), atomic_load(&torn), atomic_load(&bad_order), atomic_load(&give_up));
    CHECK(atomic_load(&torn) == 0);
    CHECK(atomic_load(&bad_order) == 0);
    CHECK(atomic_load(&ok_reads) > 0);
    CHECK(atomic_load(&shm.seq) == 2u * (WRITES + 1u));
}

int main(void)
{
    test_single_thread();
    test_concurrent();
    if (failures == 0) { printf("motion_shm: all tests passed\n"); return 0; }
    printf("motion_shm: %d failures\n", failures);
    return 1;
}
