#ifndef WAV_QUANT_H
#define WAV_QUANT_H

#include <stdint.h>

int32_t quant_sample(int32_t sample, unsigned int source_bits,
                     unsigned int quant_bits, unsigned int storage_bits);

#endif
