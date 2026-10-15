#ifndef RICE_H
#define RICE_H

#include "bitstream.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RICE_MAX_PARAM 31
#define RICE_MAX_PARTITION_ORDER 8u
#define RICE_MAX_MAPPED (UINT64_C(1) << 40u)

typedef struct {
    unsigned int order;
    uint8_t params[1u << RICE_MAX_PARTITION_ORDER];
    uint64_t bits;
} RICE_PLAN;

static inline uint64_t rice_map(int64_t value) {
    uint64_t bits = (uint64_t)value;

    return (bits << 1u) ^ (0 - (bits >> 63u));
}

static inline int64_t rice_unmap(uint64_t value) {
    int64_t half = (int64_t)(value >> 1u);
    int64_t odd = (int64_t)(value & 1u);

    return -odd - half * (2 * odd - 1);
}

uint64_t rice_bits(size_t count, uint64_t sum, unsigned int k);

unsigned int rice_best_param(size_t count, uint64_t sum, uint64_t *bits);

void rice_plan_choose(const uint64_t *mapped, size_t frames, size_t warmup,
                      unsigned int max_order, RICE_PLAN *plan);

void rice_write_residuals(BIT_WRITER *w, const uint64_t *mapped,
                          size_t frames, size_t warmup,
                          const RICE_PLAN *plan);

bool rice_read_residuals(BIT_READER *r, size_t frames, size_t warmup,
                         int64_t *residuals);

#endif
