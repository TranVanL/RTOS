#include "crc32p4.h"

#define CRC32P4_REFLECTED_POLY 0xC8DF352FUL
#define CRC32P4_INITIAL_VALUE  0xFFFFFFFFUL
#define CRC32P4_FINAL_XOR      0xFFFFFFFFUL

static uint32_t crc32p4_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0U; i < length; ++i) {
        crc ^= (uint32_t)data[i];
        for (unsigned int bit = 0U; bit < 8U; ++bit) {
            crc = ((crc & 1U) != 0U)
                ? ((crc >> 1U) ^ CRC32P4_REFLECTED_POLY)
                : (crc >> 1U);
        }
    }

    return crc;
}

bool crc32p4_compute(const uint8_t *data, size_t length, uint32_t *crc_out)
{
    if (crc_out == NULL || (data == NULL && length != 0U)) {
        return false;
    }

    const uint32_t crc = crc32p4_update(CRC32P4_INITIAL_VALUE, data, length);
    *crc_out = crc ^ CRC32P4_FINAL_XOR;
    return true;
}
