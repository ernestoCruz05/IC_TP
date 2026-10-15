#ifndef LOSSLESS_H
#define LOSSLESS_H

#include "block.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define LOSSLESS_MAX_CHANNELS 8
#define LOSSLESS_MIN_BLOCK_SIZE 16
#define LOSSLESS_MAX_BLOCK_SIZE 65535
#define LOSSLESS_DEFAULT_BLOCK_SIZE 4096
#define LOSSLESS_DEFAULT_LPC_ORDER 12
#define LOSSLESS_DEFAULT_LPC_PRECISION 12
#define LOSSLESS_MIN_LPC_PRECISION 5
#define LOSSLESS_DEFAULT_PARTITION_ORDER 4
#define LOSSLESS_MAX_THREADS 64

typedef struct {
    unsigned int channels;
    unsigned int bits;
    uint64_t prefix_size;
    uint64_t pcm_size;
    uint64_t file_size;
} LOSSLESS_SOURCE;

typedef struct {
    unsigned int block_size;
    BLOCK_OPTIONS block;
    unsigned int threads;
} LOSSLESS_OPTIONS;

bool lossless_encode(FILE *in, const LOSSLESS_SOURCE *source,
                     const LOSSLESS_OPTIONS *options, FILE *out,
                     const char **error);

bool lossless_decode(FILE *in, FILE *out, unsigned int threads,
                     const char **error);

unsigned int lossless_default_threads(void);

#endif
