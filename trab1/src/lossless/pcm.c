#include "pcm.h"

void pcm_unpack(const uint8_t *pcm, size_t pcm_size, unsigned int channels,
                unsigned int bits, int64_t *planes, size_t stride) {
    size_t bytes = bits / 8;
    size_t frames = pcm_size / (channels * bytes);
    int64_t offset = bits == 8 ? 128 : 0;
    uint32_t sign = UINT32_C(1) << (bits - 1);

    for (size_t i = 0; i < frames; ++i) {
        for (unsigned int c = 0; c < channels; ++c) {
            uint32_t raw = 0;

            for (size_t b = 0; b < bytes; ++b)
                raw |= (uint32_t)pcm[b] << (8 * b);
            pcm += bytes;

            if (bits == 8 || (raw & sign) == 0)
                planes[c * stride + i] = (int64_t)raw - offset;
            else
                planes[c * stride + i] =
                    (int64_t)raw - (INT64_C(1) << bits);
        }
    }
}

void pcm_pack(const int64_t *planes, size_t stride, unsigned int channels,
              unsigned int bits, uint8_t *pcm, size_t pcm_size) {
    size_t bytes = bits / 8;
    size_t frames = pcm_size / (channels * bytes);
    int64_t offset = bits == 8 ? 128 : 0;

    for (size_t i = 0; i < frames; ++i) {
        for (unsigned int c = 0; c < channels; ++c) {
            uint64_t raw = (uint64_t)(planes[c * stride + i] + offset);

            for (size_t b = 0; b < bytes; ++b)
                pcm[b] = (uint8_t)(raw >> (8 * b));
            pcm += bytes;
        }
    }
}
