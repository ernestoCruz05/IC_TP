#include "block.h"

#include "bitstream.h"
#include "pcm.h"

#include <stdlib.h>

#define MAX_CHANNELS 8
#define MAX_BLOCK_SIZE 65535

enum { CHANNELS_INDEPENDENT = 0, CHANNELS_MID_SIDE = 1 };

enum { STREAM_CONSTANT = 0, STREAM_VERBATIM = 1, STREAM_FIXED = 2, STREAM_LPC = 3 };

typedef struct {
    int mode;
    unsigned int order;
    unsigned int k;
    uint64_t cost;
} STREAM_PLAN;

struct BLOCK_WORK {
    BLOCK_FORMAT format;
    int64_t *planes;
    int64_t *mid;
    int64_t *side;
    uint64_t *folded;
};

static bool valid_format(const BLOCK_FORMAT *format) {
    return format->channels >= 1 && format->channels <= MAX_CHANNELS &&
           (format->bits == 8 || format->bits == 16 || format->bits == 24 ||
            format->bits == 32) &&
           format->block_size >= 1 && format->block_size <= MAX_BLOCK_SIZE;
}

BLOCK_WORK *block_work_create(const BLOCK_FORMAT *format) {
    BLOCK_WORK *work;

    if (!format || !valid_format(format))
        return NULL;

    work = calloc(1, sizeof(*work));
    if (!work)
        return NULL;

    work->format = *format;
    work->planes = malloc((size_t)format->channels * format->block_size *
                          sizeof(*work->planes));
    if (!work->planes) {
        free(work);
        return NULL;
    }

    if (format->channels == 2) {
        work->mid = malloc((size_t)format->block_size * sizeof(*work->mid));
        work->side = malloc((size_t)format->block_size * sizeof(*work->side));
        if (!work->mid || !work->side) {
            free(work->mid);
            free(work->side);
            free(work->planes);
            free(work);
            return NULL;
        }
    }

    work->folded = malloc((size_t)format->block_size * sizeof(*work->folded));
    if (!work->folded) {
        free(work->mid);
        free(work->side);
        free(work->planes);
        free(work);
        return NULL;
    }

    return work;
}

void block_work_destroy(BLOCK_WORK *work) {
    if (!work)
        return;

    free(work->folded);
    free(work->side);
    free(work->mid);
    free(work->planes);
    free(work);
}

size_t block_payload_bound(const BLOCK_FORMAT *format, size_t frames) {
    size_t bits = 2 + format->channels * (2 + frames * (format->bits + 1));

    return (bits + 7) / 8;
}

static size_t frame_count(const BLOCK_FORMAT *format, size_t pcm_size) {
    size_t frame_bytes = (size_t)format->channels * (format->bits / 8);

    if (pcm_size == 0 || pcm_size % frame_bytes != 0 ||
        pcm_size / frame_bytes > format->block_size)
        return 0;
    return pcm_size / frame_bytes;
}

static inline int64_t predict_sample(const int64_t *x, size_t i, unsigned int order) {
    switch (order) {
    case 0:
        return 0;
    case 1:
        return x[i - 1];
    case 2:
        return 2 * x[i - 1] - x[i - 2];
    case 3:
        return 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3];
    case 4:
        return 4 * x[i - 1] - 6 * x[i - 2] + 4 * x[i - 3] - x[i - 4];
    default:
        return 0;
    }
}

static unsigned int find_best_rice_k(const uint64_t *folded, size_t count,
                                     unsigned int width, uint64_t *out_cost) {
    if (count == 0) {
        *out_cost = 0;
        return 0;
    }

    unsigned int max_k = width > 31 ? 31 : width;
    unsigned int best_k = 0;
    uint64_t best_cost = UINT64_MAX;

    for (unsigned int k = 0; k <= max_k; ++k) {
        uint64_t cost = (uint64_t)count * (k + 1);
        for (size_t i = 0; i < count; ++i) {
            cost += folded[i] >> k;
            if (cost >= best_cost)
                break;
        }
        if (cost < best_cost) {
            best_cost = cost;
            best_k = k;
        }
    }

    *out_cost = best_cost;
    return best_k;
}

static STREAM_PLAN plan_stream(const int64_t *x, size_t n, unsigned int width,
                               uint64_t *scratch_folded) {
    STREAM_PLAN plan;

    size_t i = 1;
    while (i < n && x[i] == x[0])
        ++i;
    if (i == n) {
        plan.mode = STREAM_CONSTANT;
        plan.order = 0;
        plan.k = 0;
        plan.cost = 2 + width;
        return plan;
    }

    plan.mode = STREAM_VERBATIM;
    plan.order = 0;
    plan.k = 0;
    plan.cost = 2 + (uint64_t)n * width;

    for (unsigned int order = 0; order <= 4; ++order) {
        if (order >= n)
            break;

        size_t m = n - order;
        for (size_t j = 0; j < m; ++j) {
            size_t idx = order + j;
            int64_t pred = predict_sample(x, idx, order);
            int64_t e = x[idx] - pred;
            scratch_folded[j] = (e >= 0) ? (uint64_t)(2 * e)
                                         : (uint64_t)(-2 * e - 1);
        }

        uint64_t res_cost;
        unsigned int k = find_best_rice_k(scratch_folded, m, width, &res_cost);
        uint64_t total_cost = 2 + 3 + (uint64_t)order * width + 5 + res_cost;

        if (total_cost < plan.cost) {
            plan.mode = STREAM_FIXED;
            plan.order = order;
            plan.k = k;
            plan.cost = total_cost;
        }
    }

    return plan;
}

static void write_stream_plan(BIT_WRITER *w, const int64_t *x, size_t n,
                              unsigned int width, const STREAM_PLAN *plan) {
    bit_write(w, (uint64_t)plan->mode, 2);
    switch (plan->mode) {
    case STREAM_CONSTANT:
        bit_write_signed(w, x[0], width);
        break;
    case STREAM_VERBATIM:
        for (size_t i = 0; i < n; ++i)
            bit_write_signed(w, x[i], width);
        break;
    case STREAM_FIXED: {
        bit_write(w, plan->order, 3);
        for (unsigned int i = 0; i < plan->order; ++i)
            bit_write_signed(w, x[i], width);
        bit_write(w, plan->k, 5);
        uint64_t mask = (plan->k > 0) ? ((UINT64_C(1) << plan->k) - 1) : 0;
        for (size_t i = plan->order; i < n; ++i) {
            int64_t pred = predict_sample(x, i, plan->order);
            int64_t e = x[i] - pred;
            uint64_t u = (e >= 0) ? (uint64_t)(2 * e) : (uint64_t)(-2 * e - 1);
            bit_write_unary(w, u >> plan->k);
            if (plan->k > 0)
                bit_write(w, u & mask, plan->k);
        }
        break;
    }
    default:
        break;
    }
}

static bool read_stream(BIT_READER *r, int64_t *x, size_t n,
                        unsigned int width) {
    uint64_t mode = bit_read(r, 2);
    switch (mode) {
    case STREAM_CONSTANT: {
        int64_t value = bit_read_signed(r, width);
        for (size_t i = 0; i < n; ++i)
            x[i] = value;
        break;
    }
    case STREAM_VERBATIM:
        for (size_t i = 0; i < n; ++i)
            x[i] = bit_read_signed(r, width);
        break;
    case STREAM_FIXED: {
        unsigned int order = (unsigned int)bit_read(r, 3);
        if (order > 4 || order > n)
            return false;
        for (unsigned int i = 0; i < order; ++i)
            x[i] = bit_read_signed(r, width);
        unsigned int k = (unsigned int)bit_read(r, 5);
        if (k > 32)
            return false;
        for (size_t i = order; i < n; ++i) {
            int64_t pred = predict_sample(x, i, order);
            uint64_t q = bit_read_unary(r);
            uint64_t rem = (k > 0) ? bit_read(r, k) : 0;
            if (r->error)
                return false;
            if (k < 64 && q > (UINT64_MAX >> k))
                return false;
            uint64_t u = (q << k) | rem;
            int64_t e = (u & 1) ? -(int64_t)((u + 1) >> 1) : (int64_t)(u >> 1);
            x[i] = pred + e;
        }
        break;
    }
    default:
        return false;
    }
    return !r->error;
}

bool block_encode(BLOCK_WORK *work, const uint8_t *pcm, size_t pcm_size,
                  uint8_t *payload, size_t capacity, size_t *size) {
    const BLOCK_FORMAT *format = &work->format;
    size_t frames = frame_count(format, pcm_size);
    BIT_WRITER w;

    if (frames == 0)
        return false;

    pcm_unpack(pcm, pcm_size, format->channels, format->bits, work->planes,
               format->block_size);

    bit_writer_init(&w, payload, capacity);

    if (format->channels == 2) {
        int64_t *left = work->planes;
        int64_t *right = work->planes + format->block_size;

        STREAM_PLAN plan_l = plan_stream(left, frames, format->bits, work->folded);
        STREAM_PLAN plan_r = plan_stream(right, frames, format->bits, work->folded);
        uint64_t cost_lr = 2 + plan_l.cost + plan_r.cost;

        for (size_t i = 0; i < frames; ++i) {
            work->mid[i] = (left[i] + right[i]) >> 1;
            work->side[i] = left[i] - right[i];
        }

        STREAM_PLAN plan_m = plan_stream(work->mid, frames, format->bits, work->folded);
        STREAM_PLAN plan_s = plan_stream(work->side, frames, format->bits + 1, work->folded);
        uint64_t cost_ms = 2 + plan_m.cost + plan_s.cost;

        if (cost_ms < cost_lr) {
            bit_write(&w, CHANNELS_MID_SIDE, 2);
            write_stream_plan(&w, work->mid, frames, format->bits, &plan_m);
            write_stream_plan(&w, work->side, frames, format->bits + 1, &plan_s);
        } else {
            bit_write(&w, CHANNELS_INDEPENDENT, 2);
            write_stream_plan(&w, left, frames, format->bits, &plan_l);
            write_stream_plan(&w, right, frames, format->bits, &plan_r);
        }
    } else {
        bit_write(&w, CHANNELS_INDEPENDENT, 2);
        for (unsigned int c = 0; c < format->channels; ++c) {
            const int64_t *ch_samples = work->planes + (size_t)c * format->block_size;
            STREAM_PLAN plan = plan_stream(ch_samples, frames, format->bits, work->folded);
            write_stream_plan(&w, ch_samples, frames, format->bits, &plan);
        }
    }

    return bit_writer_finish(&w, size);
}

bool block_decode(BLOCK_WORK *work, const uint8_t *payload, size_t size,
                  uint8_t *pcm, size_t pcm_size) {
    const BLOCK_FORMAT *format = &work->format;
    size_t frames = frame_count(format, pcm_size);
    BIT_READER r;

    if (frames == 0 || size > block_payload_bound(format, frames))
        return false;

    bit_reader_init(&r, payload, size);
    uint64_t ch_mode = bit_read(&r, 2);

    if (ch_mode == CHANNELS_MID_SIDE) {
        if (format->channels != 2)
            return false;

        if (!read_stream(&r, work->mid, frames, format->bits))
            return false;
        if (!read_stream(&r, work->side, frames, format->bits + 1))
            return false;

        int64_t *left = work->planes;
        int64_t *right = work->planes + format->block_size;
        for (size_t i = 0; i < frames; ++i) {
            int64_t m = work->mid[i];
            int64_t s = work->side[i];
            left[i] = m + ((s + 1) >> 1);
            right[i] = m - (s >> 1);
        }
    } else if (ch_mode == CHANNELS_INDEPENDENT) {
        for (unsigned int c = 0; c < format->channels; ++c) {
            if (!read_stream(&r, work->planes + (size_t)c * format->block_size,
                             frames, format->bits))
                return false;
        }
    } else {
        return false;
    }

    if (!bit_reader_finish(&r))
        return false;

    pcm_pack(work->planes, format->block_size, format->channels, format->bits,
             pcm, pcm_size);
    return true;
}
