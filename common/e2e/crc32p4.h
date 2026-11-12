#ifndef M2_E2E_CRC32P4_H
#define M2_E2E_CRC32P4_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Computes CRC-32P4 with init/xorout 0xFFFFFFFF, reflected input/output,
 * and reflected polynomial 0xC8DF352F. Returns false for invalid pointers.
 */
bool crc32p4_compute(const uint8_t *data, size_t length, uint32_t *crc_out);

#ifdef __cplusplus
}
#endif

#endif /* M2_E2E_CRC32P4_H */
