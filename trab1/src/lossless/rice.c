#include "rice.h"

#define ORDER_BITS 4
#define PARAM_BITS 5

uint64_t rice_bits(size_t count, uint64_t sum, unsigned int k) {
    return (uint64_t)count * (k + 1) + (sum >> k);
}

unsigned int rice_best_param(size_t count, uint64_t sum, uint64_t *bits) {
    uint64_t mean = count == 0 ? 0 : sum / count;
    unsigned int k = 0;

    while (k < RICE_MAX_PARAM && (mean >> (k + 1)) > 0)
        ++k;
    while (k > 0 && rice_bits(count, sum, k - 1) <= rice_bits(count, sum, k))
        --k;
    while (k < RICE_MAX_PARAM &&
           rice_bits(count, sum, k + 1) < rice_bits(count, sum, k))
        ++k;

    *bits = rice_bits(count, sum, k);
    return k;
}

static bool valid_order(size_t frames, size_t warmup, unsigned int order) {
    return frames % ((size_t)1 << order) == 0 && (frames >> order) > warmup;
}

void rice_plan_choose(const uint64_t *mapped, size_t frames, size_t warmup,
                      unsigned int max_order, RICE_PLAN *plan) {
    uint64_t sums[1u << RICE_MAX_PARTITION_ORDER];
    uint8_t params[1u << RICE_MAX_PARTITION_ORDER];
    unsigned int top =
        max_order < RICE_MAX_PARTITION_ORDER ? max_order
                                             : RICE_MAX_PARTITION_ORDER;
    size_t size;

    while (top > 0 && !valid_order(frames, warmup, top))
        --top;

    size = frames >> top;
    for (size_t p = 0; p < ((size_t)1 << top); ++p) {
        size_t count = p == 0 ? size - warmup : size;
        uint64_t sum = 0;

        for (size_t i = 0; i < count; ++i)
            sum += *mapped++;
        sums[p] = sum;
    }

    plan->bits = UINT64_MAX;
    for (unsigned int order = top + 1; order-- > 0;) {
        size_t partitions = (size_t)1 << order;
        uint64_t bits = ORDER_BITS;

        size = frames >> order;
        for (size_t p = 0; p < partitions; ++p) {
            size_t count = p == 0 ? size - warmup : size;
            uint64_t part_bits;

            params[p] = (uint8_t)rice_best_param(count, sums[p], &part_bits);
            bits += PARAM_BITS + part_bits;
        }

        if (bits <= plan->bits) {
            plan->order = order;
            plan->bits = bits;
            for (size_t p = 0; p < partitions; ++p)
                plan->params[p] = params[p];
        }

        for (size_t p = 0; p < partitions / 2; ++p)
            sums[p] = sums[2 * p] + sums[2 * p + 1];
    }
}

void rice_write_residuals(BIT_WRITER *w, const uint64_t *mapped,
                          size_t frames, size_t warmup,
                          const RICE_PLAN *plan) {
    size_t partitions;
    size_t size;

    if (plan->order > RICE_MAX_PARTITION_ORDER ||
        !valid_order(frames, warmup, plan->order)) {
        w->error = true;
        return;
    }

    partitions = (size_t)1 << plan->order;
    size = frames >> plan->order;
    bit_write(w, plan->order, ORDER_BITS);

    for (size_t p = 0; p < partitions; ++p) {
        unsigned int k = plan->params[p];
        size_t count = p == 0 ? size - warmup : size;

        if (k > RICE_MAX_PARAM) {
            w->error = true;
            return;
        }

        bit_write(w, k, PARAM_BITS);
        for (size_t i = 0; i < count; ++i, ++mapped) {
            bit_write_unary(w, *mapped >> k);
            bit_write(w, *mapped, k);
        }
    }
}

bool rice_read_residuals(BIT_READER *r, size_t frames, size_t warmup,
                         int64_t *residuals) {
    unsigned int order = (unsigned int)bit_read(r, ORDER_BITS);
    size_t partitions;
    size_t size;

    if (r->error || !valid_order(frames, warmup, order))
        return false;

    partitions = (size_t)1 << order;
    size = frames >> order;

    for (size_t p = 0; p < partitions; ++p) {
        unsigned int k = (unsigned int)bit_read(r, PARAM_BITS);
        uint64_t limit = RICE_MAX_MAPPED >> k;
        size_t count = p == 0 ? size - warmup : size;

        for (size_t i = 0; i < count; ++i) {
            uint64_t q = bit_read_unary(r);

            if (r->error || q >= limit)
                return false;
            *residuals++ = rice_unmap((q << k) | bit_read(r, k));
        }
    }
    return !r->error;
}
