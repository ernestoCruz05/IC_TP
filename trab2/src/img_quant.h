#ifndef IMG_QUANT_H
#define IMG_QUANT_H

#include <stddef.h>
#include <stdint.h>

static inline unsigned sample_bits(size_t max_value)
{
    unsigned bits = 0;
    while (max_value != 0) {
        bits++;
        max_value >>= 1;
    }
    return bits;
}

static inline uint16_t quant_sample(uint16_t sample, unsigned source_bits,
                                    unsigned quant_bits, size_t max_value)
{
    uint32_t step = (uint32_t)1 << (source_bits - quant_bits);
    uint32_t value = (sample / step) * step + step / 2;
    if (value > max_value) {
        return (uint16_t)max_value;
    }
    return (uint16_t)value;
}

#endif
