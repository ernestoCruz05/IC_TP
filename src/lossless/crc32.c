#include "crc32.h"

#include <pthread.h>

static uint32_t crc32_tables[8][256];
static pthread_once_t crc32_once = PTHREAD_ONCE_INIT;

static void crc32_init(void) {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t crc = n;

        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) != 0 ? (crc >> 1u) ^ UINT32_C(0xedb88320)
                                  : crc >> 1u;
        crc32_tables[0][n] = crc;
    }
    for (uint32_t n = 0; n < 256; ++n) {
        for (size_t k = 1; k < 8; ++k) {
            uint32_t previous = crc32_tables[k - 1][n];

            crc32_tables[k][n] =
                crc32_tables[0][previous & 0xffu] ^ (previous >> 8u);
        }
    }
}

static uint32_t load_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t size) {
    const uint8_t *bytes = data;

    (void)pthread_once(&crc32_once, crc32_init);

    crc = ~crc;
    for (; size >= 8; size -= 8, bytes += 8) {
        uint32_t low = crc ^ load_le32(bytes);
        uint32_t high = load_le32(bytes + 4);

        crc = crc32_tables[7][low & 0xffu] ^
              crc32_tables[6][(low >> 8u) & 0xffu] ^
              crc32_tables[5][(low >> 16u) & 0xffu] ^
              crc32_tables[4][low >> 24u] ^ crc32_tables[3][high & 0xffu] ^
              crc32_tables[2][(high >> 8u) & 0xffu] ^
              crc32_tables[1][(high >> 16u) & 0xffu] ^
              crc32_tables[0][high >> 24u];
    }
    for (; size > 0; --size, ++bytes)
        crc = crc32_tables[0][(crc ^ (uint32_t)*bytes) & 0xffu] ^ (crc >> 8u);
    return ~crc;
}
