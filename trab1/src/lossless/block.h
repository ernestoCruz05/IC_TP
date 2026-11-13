#ifndef BLOCK_H
#define BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned int channels;
    unsigned int bits;
    unsigned int block_size;
} BLOCK_FORMAT;

typedef struct {
    unsigned int max_lpc_order;
    unsigned int lpc_precision;
    unsigned int max_partition_order;
} BLOCK_OPTIONS;

typedef struct BLOCK_WORK BLOCK_WORK;

BLOCK_WORK *block_work_create(const BLOCK_FORMAT *format);

void block_work_destroy(BLOCK_WORK *work);

size_t block_payload_bound(const BLOCK_FORMAT *format, size_t frames);

bool block_encode(BLOCK_WORK *work, const BLOCK_OPTIONS *options,
                  const uint8_t *pcm, size_t pcm_size, uint8_t *payload,
                  size_t capacity, size_t *size);

bool block_decode(BLOCK_WORK *work, const uint8_t *payload, size_t size,
                  uint8_t *pcm, size_t pcm_size);

#endif
