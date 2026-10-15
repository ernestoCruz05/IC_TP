#include "block.h"

#include "bitstream.h"
#include "lpc.h"
#include "pcm.h"
#include "predict.h"
#include "rice.h"
#include "stereo.h"

#include <stdlib.h>

#define MAX_CHANNELS 8
#define MAX_BLOCK_SIZE 65535
#define MODE_BITS 2
#define TYPE_BITS 2
#define FIXED_ORDER_BITS 3
#define LPC_ORDER_BITS 5
#define LPC_PRECISION_BITS 4
#define LPC_SHIFT_BITS 5
#define LPC_WINDOW_ALPHA 0.5
#define RESIDUAL_HEADROOM 4

enum { STREAM_CONSTANT, STREAM_VERBATIM, STREAM_FIXED, STREAM_LPC };

enum { LEFT, RIGHT, MID, SIDE };

static const unsigned int mode_streams[4][2] = {
    [STEREO_LR] = {LEFT, RIGHT},
    [STEREO_LS] = {LEFT, SIDE},
    [STEREO_SR] = {SIDE, RIGHT},
    [STEREO_MS] = {MID, SIDE},
};

typedef struct {
    unsigned int type;
    unsigned int order;
    unsigned int precision;
    unsigned int shift;
    int32_t qcoefs[LPC_MAX_ORDER];
    RICE_PLAN rice;
    uint64_t bits;
} STREAM_CHOICE;

struct BLOCK_WORK {
    BLOCK_FORMAT format;
    int64_t *planes;
    int64_t *mid;
    int64_t *side;
    int64_t *residuals;
    uint64_t *mapped;
    double *window;
    double *windowed;
    size_t window_length;
};

static unsigned int stream_width(unsigned int stream, unsigned int bits) {
    return stream == SIDE ? bits + 1 : bits;
}

static bool valid_format(const BLOCK_FORMAT *format) {
    return format->channels >= 1 && format->channels <= MAX_CHANNELS &&
           (format->bits == 8 || format->bits == 16 || format->bits == 24 ||
            format->bits == 32) &&
           format->block_size >= 1 && format->block_size <= MAX_BLOCK_SIZE;
}

static bool valid_options(const BLOCK_OPTIONS *options) {
    return options->max_lpc_order <= LPC_MAX_ORDER &&
           options->lpc_precision >= 1 &&
           options->lpc_precision <= LPC_MAX_PRECISION &&
           options->max_partition_order <= RICE_MAX_PARTITION_ORDER;
}

BLOCK_WORK *block_work_create(const BLOCK_FORMAT *format) {
    BLOCK_WORK *work;
    size_t size;

    if (!format || !valid_format(format))
        return NULL;

    work = calloc(1, sizeof(*work));
    if (!work)
        return NULL;

    work->format = *format;
    size = format->block_size;
    work->planes =
        malloc((size_t)format->channels * size * sizeof(*work->planes));
    work->residuals = malloc(size * sizeof(*work->residuals));
    work->mapped = malloc(size * sizeof(*work->mapped));
    work->window = malloc(size * sizeof(*work->window));
    work->windowed = malloc(size * sizeof(*work->windowed));
    if (format->channels == 2) {
        work->mid = malloc(size * sizeof(*work->mid));
        work->side = malloc(size * sizeof(*work->side));
    }

    if (!work->planes || !work->residuals || !work->mapped || !work->window ||
        !work->windowed ||
        (format->channels == 2 && (!work->mid || !work->side))) {
        block_work_destroy(work);
        return NULL;
    }
    return work;
}

void block_work_destroy(BLOCK_WORK *work) {
    if (!work)
        return;

    free(work->windowed);
    free(work->window);
    free(work->mapped);
    free(work->residuals);
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

static bool in_range(const int64_t *x, size_t n, unsigned int width) {
    int64_t max = (INT64_C(1) << (width - 1)) - 1;
    int64_t min = -max - 1;

    for (size_t i = 0; i < n; ++i) {
        if (x[i] < min || x[i] > max)
            return false;
    }
    return true;
}

static void map_residuals(BLOCK_WORK *work, size_t count) {
    for (size_t i = 0; i < count; ++i)
        work->mapped[i] = rice_map(work->residuals[i]);
}

static bool lpc_stream_residuals(BLOCK_WORK *work, const int64_t *x, size_t n,
                                 unsigned int width,
                                 const STREAM_CHOICE *choice) {
    int64_t limit = INT64_C(1) << (width + RESIDUAL_HEADROOM);

    if (!lpc_residuals(x, n, choice->qcoefs, choice->order, choice->shift,
                       limit, work->residuals))
        return false;
    map_residuals(work, n - choice->order);
    return true;
}

static void choose_fixed(BLOCK_WORK *work, const BLOCK_OPTIONS *options,
                         const int64_t *x, size_t n, unsigned int width,
                         STREAM_CHOICE *choice) {
    unsigned int max_order =
        n - 1 < FIXED_MAX_ORDER ? (unsigned int)(n - 1) : FIXED_MAX_ORDER;

    for (unsigned int order = 0; order <= max_order; ++order) {
        RICE_PLAN plan;
        uint64_t bits;

        fixed_residuals(x, n, order, work->residuals);
        map_residuals(work, n - order);
        rice_plan_choose(work->mapped, n, order, options->max_partition_order,
                         &plan);
        bits = TYPE_BITS + FIXED_ORDER_BITS + (uint64_t)order * width +
               plan.bits;
        if (bits < choice->bits) {
            choice->type = STREAM_FIXED;
            choice->order = order;
            choice->rice = plan;
            choice->bits = bits;
        }
    }
}

static void choose_lpc(BLOCK_WORK *work, const BLOCK_OPTIONS *options,
                       const int64_t *x, size_t n, unsigned int width,
                       STREAM_CHOICE *choice) {
    double coefs[LPC_MAX_ORDER][LPC_MAX_ORDER];
    double autoc[LPC_MAX_ORDER + 1];
    double errors[LPC_MAX_ORDER];
    unsigned int max_order = options->max_lpc_order;
    unsigned int orders;
    STREAM_CHOICE candidate = {.type = STREAM_LPC,
                               .precision = options->lpc_precision};

    if (max_order == 0 || n < 2)
        return;
    if (max_order > n - 1)
        max_order = (unsigned int)(n - 1);

    if (work->window_length != n) {
        lpc_window_tukey(work->window, n, LPC_WINDOW_ALPHA);
        work->window_length = n;
    }
    lpc_apply_window(x, work->window, n, work->windowed);
    lpc_autocorrelation(work->windowed, n, max_order, autoc);
    orders = lpc_levinson(autoc, max_order, coefs, errors);
    if (orders == 0)
        return;

    candidate.order = lpc_estimate_order(errors, orders, n,
                                         candidate.precision + width);
    if (!lpc_quantize(coefs[candidate.order - 1], candidate.order,
                      candidate.precision, candidate.qcoefs,
                      &candidate.shift) ||
        !lpc_stream_residuals(work, x, n, width, &candidate))
        return;

    rice_plan_choose(work->mapped, n, candidate.order,
                     options->max_partition_order, &candidate.rice);
    candidate.bits = TYPE_BITS + LPC_ORDER_BITS + LPC_PRECISION_BITS +
                     LPC_SHIFT_BITS +
                     (uint64_t)candidate.order * (candidate.precision + width) +
                     candidate.rice.bits;
    if (candidate.bits < choice->bits)
        *choice = candidate;
}

static void choose_stream(BLOCK_WORK *work, const BLOCK_OPTIONS *options,
                          const int64_t *x, size_t n, unsigned int width,
                          STREAM_CHOICE *choice) {
    size_t i = 1;

    while (i < n && x[i] == x[0])
        ++i;

    if (i == n) {
        *choice = (STREAM_CHOICE){.type = STREAM_CONSTANT,
                                  .bits = TYPE_BITS + width};
        return;
    }

    *choice = (STREAM_CHOICE){.type = STREAM_VERBATIM,
                              .bits = TYPE_BITS + (uint64_t)n * width};
    choose_fixed(work, options, x, n, width, choice);
    choose_lpc(work, options, x, n, width, choice);
}

static void write_stream(BIT_WRITER *w, BLOCK_WORK *work, const int64_t *x,
                         size_t n, unsigned int width,
                         const STREAM_CHOICE *choice) {
    bit_write(w, choice->type, TYPE_BITS);
    switch (choice->type) {
    case STREAM_CONSTANT:
        bit_write_signed(w, x[0], width);
        return;
    case STREAM_VERBATIM:
        for (size_t i = 0; i < n; ++i)
            bit_write_signed(w, x[i], width);
        return;
    case STREAM_FIXED:
        bit_write(w, choice->order, FIXED_ORDER_BITS);
        fixed_residuals(x, n, choice->order, work->residuals);
        map_residuals(work, n - choice->order);
        break;
    case STREAM_LPC:
        bit_write(w, choice->order - 1, LPC_ORDER_BITS);
        bit_write(w, choice->precision - 1, LPC_PRECISION_BITS);
        bit_write(w, choice->shift, LPC_SHIFT_BITS);
        for (unsigned int j = 0; j < choice->order; ++j)
            bit_write_signed(w, choice->qcoefs[j], choice->precision);
        if (!lpc_stream_residuals(work, x, n, width, choice)) {
            w->error = true;
            return;
        }
        break;
    default:
        w->error = true;
        return;
    }

    for (size_t i = 0; i < choice->order; ++i)
        bit_write_signed(w, x[i], width);
    rice_write_residuals(w, work->mapped, n, choice->order, &choice->rice);
}

static bool read_stream(BIT_READER *r, BLOCK_WORK *work, int64_t *x, size_t n,
                        unsigned int width) {
    int32_t qcoefs[LPC_MAX_ORDER];
    unsigned int type = (unsigned int)bit_read(r, TYPE_BITS);
    unsigned int order;
    unsigned int precision = 0;
    unsigned int shift = 0;

    switch (type) {
    case STREAM_CONSTANT: {
        int64_t value = bit_read_signed(r, width);

        for (size_t i = 0; i < n; ++i)
            x[i] = value;
        return !r->error;
    }
    case STREAM_VERBATIM:
        for (size_t i = 0; i < n; ++i)
            x[i] = bit_read_signed(r, width);
        return !r->error;
    case STREAM_FIXED:
        order = (unsigned int)bit_read(r, FIXED_ORDER_BITS);
        if (order > FIXED_MAX_ORDER)
            return false;
        break;
    case STREAM_LPC:
        order = (unsigned int)bit_read(r, LPC_ORDER_BITS) + 1;
        precision = (unsigned int)bit_read(r, LPC_PRECISION_BITS) + 1;
        shift = (unsigned int)bit_read(r, LPC_SHIFT_BITS);
        if (precision > LPC_MAX_PRECISION || order >= n)
            return false;
        for (unsigned int j = 0; j < order; ++j)
            qcoefs[j] = (int32_t)bit_read_signed(r, precision);
        break;
    default:
        return false;
    }

    if (order >= n)
        return false;
    for (size_t i = 0; i < order; ++i)
        x[i] = bit_read_signed(r, width);
    if (!rice_read_residuals(r, n, order, work->residuals))
        return false;

    if (type == STREAM_FIXED)
        return fixed_restore(x, n, order, work->residuals, width);
    return lpc_restore(x, n, qcoefs, order, shift, work->residuals, width);
}

bool block_encode(BLOCK_WORK *work, const BLOCK_OPTIONS *options,
                  const uint8_t *pcm, size_t pcm_size, uint8_t *payload,
                  size_t capacity, size_t *size) {
    const BLOCK_FORMAT *format = &work->format;
    size_t frames = frame_count(format, pcm_size);
    BIT_WRITER w;

    if (frames == 0 || !valid_options(options))
        return false;

    pcm_unpack(pcm, pcm_size, format->channels, format->bits, work->planes,
               format->block_size);

    bit_writer_init(&w, payload, capacity);

    if (format->channels == 2) {
        const int64_t *streams[4] = {work->planes,
                                     work->planes + format->block_size,
                                     work->mid, work->side};
        STREAM_CHOICE choices[4];
        unsigned int best = STEREO_LR;
        uint64_t best_bits = UINT64_MAX;

        stereo_split(streams[LEFT], streams[RIGHT], frames, work->mid,
                     work->side);
        for (unsigned int s = LEFT; s <= SIDE; ++s)
            choose_stream(work, options, streams[s], frames,
                          stream_width(s, format->bits), &choices[s]);

        for (unsigned int mode = STEREO_LR; mode <= STEREO_MS; ++mode) {
            uint64_t bits = choices[mode_streams[mode][0]].bits +
                            choices[mode_streams[mode][1]].bits;

            if (bits < best_bits) {
                best = mode;
                best_bits = bits;
            }
        }

        bit_write(&w, best, MODE_BITS);
        for (unsigned int i = 0; i < 2; ++i) {
            unsigned int s = mode_streams[best][i];

            write_stream(&w, work, streams[s], frames,
                         stream_width(s, format->bits), &choices[s]);
        }
    } else {
        bit_write(&w, STEREO_LR, MODE_BITS);
        for (unsigned int c = 0; c < format->channels; ++c) {
            const int64_t *x = work->planes + (size_t)c * format->block_size;
            STREAM_CHOICE choice;

            choose_stream(work, options, x, frames, format->bits, &choice);
            write_stream(&w, work, x, frames, format->bits, &choice);
        }
    }

    return bit_writer_finish(&w, size);
}

bool block_decode(BLOCK_WORK *work, const uint8_t *payload, size_t size,
                  uint8_t *pcm, size_t pcm_size) {
    const BLOCK_FORMAT *format = &work->format;
    size_t frames = frame_count(format, pcm_size);
    BIT_READER r;
    uint64_t mode;

    if (frames == 0 || size > block_payload_bound(format, frames))
        return false;

    bit_reader_init(&r, payload, size);
    mode = bit_read(&r, MODE_BITS);

    if (format->channels == 2) {
        int64_t *left = work->planes;
        int64_t *right = work->planes + format->block_size;
        unsigned int first = mode_streams[mode][0];
        unsigned int second = mode_streams[mode][1];

        if (!read_stream(&r, work, left, frames,
                         stream_width(first, format->bits)) ||
            !read_stream(&r, work, right, frames,
                         stream_width(second, format->bits)))
            return false;

        stereo_join((unsigned int)mode, left, right, frames);
        if (!in_range(left, frames, format->bits) ||
            !in_range(right, frames, format->bits))
            return false;
    } else {
        if (mode != STEREO_LR)
            return false;

        for (unsigned int c = 0; c < format->channels; ++c) {
            if (!read_stream(&r, work,
                             work->planes + (size_t)c * format->block_size,
                             frames, format->bits))
                return false;
        }
    }

    if (!bit_reader_finish(&r))
        return false;

    pcm_pack(work->planes, format->block_size, format->channels, format->bits,
             pcm, pcm_size);
    return true;
}
