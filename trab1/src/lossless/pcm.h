#ifndef PCM_H
#define PCM_H

#include <stddef.h>
#include <stdint.h>

void pcm_unpack(const uint8_t *pcm, size_t pcm_size, unsigned int channels,
                unsigned int bits, int64_t *planes, size_t stride);

void pcm_pack(const int64_t *planes, size_t stride, unsigned int channels,
              unsigned int bits, uint8_t *pcm, size_t pcm_size);

#endif
