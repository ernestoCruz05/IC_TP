#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t crc32_update(uint32_t crc, const void *data, size_t size);

#endif
