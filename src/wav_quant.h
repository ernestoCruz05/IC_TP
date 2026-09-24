#ifndef WAV_QUANT_H
#define WAV_QUANT_H

#include <stdint.h>

int32_t quant_sample(int32_t sample, unsigned source_bits,
                     unsigned target_bits);

#endif
