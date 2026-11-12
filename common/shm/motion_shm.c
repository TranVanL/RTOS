#include "motion_shm.h"

#include <stddef.h>
#include <string.h>

void motion_shm_init(motion_shm_t *shm)
{
    if (shm == NULL) {
        return;
    }
    shm->magic = (uint32_t)MOTION_SHM_MAGIC;
    shm->version = (uint32_t)MOTION_SHM_VERSION;
    shm->reserved = 0U;
    atomic_store_explicit(&shm->seq, 0U, memory_order_relaxed);
    for (unsigned i = 0U; i < MOTION_STATE_WORDS; ++i) {
        atomic_store_explicit(&shm->words[i], 0U, memory_order_relaxed);
    }
    atomic_thread_fence(memory_order_release);
}

bool motion_shm_validate(const motion_shm_t *shm)
{
    return shm != NULL && shm->magic == (uint32_t)MOTION_SHM_MAGIC &&
           shm->version == (uint32_t)MOTION_SHM_VERSION;
}

void motion_shm_write(motion_shm_t *shm, const motion_state_t *state)
{
    uint32_t words[MOTION_STATE_WORDS];
    if (shm == NULL || state == NULL) {
        return;
    }
    memcpy(words, state, sizeof words);

    uint32_t s = atomic_load_explicit(&shm->seq, memory_order_relaxed);
    atomic_store_explicit(&shm->seq, s + 1U, memory_order_relaxed);   /* odd  */
    atomic_thread_fence(memory_order_release);
    for (unsigned i = 0U; i < MOTION_STATE_WORDS; ++i) {
        atomic_store_explicit(&shm->words[i], words[i], memory_order_relaxed);
    }
    atomic_store_explicit(&shm->seq, s + 2U, memory_order_release);   /* even */
}

bool motion_shm_read(const motion_shm_t *shm, motion_state_t *out,
                     unsigned max_retries, uint32_t *seq_out)
{
    uint32_t words[MOTION_STATE_WORDS];
    if (shm == NULL || out == NULL) {
        return false;
    }
    for (unsigned attempt = 0U; attempt <= max_retries; ++attempt) {
        uint32_t s1 = atomic_load_explicit(&shm->seq, memory_order_acquire);
        if ((s1 & 1U) != 0U) {
            continue;
        }
        for (unsigned i = 0U; i < MOTION_STATE_WORDS; ++i) {
            words[i] = atomic_load_explicit(&shm->words[i], memory_order_relaxed);
        }
        atomic_thread_fence(memory_order_acquire);
        uint32_t s2 = atomic_load_explicit(&shm->seq, memory_order_relaxed);
        if (s1 == s2) {
            memcpy(out, words, sizeof words);
            if (seq_out != NULL) {
                *seq_out = s1;
            }
            return true;
        }
    }
    return false;
}
