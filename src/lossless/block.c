#include "block.h"

#include "bitstream.h"
#include "pcm.h"

#include <stdlib.h>

#define MAX_CHANNELS 8
#define MAX_BLOCK_SIZE 65535

enum { CHANNELS_INDEPENDENT };

enum { STREAM_CONSTANT, STREAM_VERBATIM, STREAM_FIXED, STREAM_LPC };

struct BLOCK_WORK {
    BLOCK_FORMAT format;
    int64_t *planes;
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
    return work;
}

void block_work_destroy(BLOCK_WORK *work) {
    if (!work)
        return;

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

static void write_stream(BIT_WRITER *w, const int64_t *x, size_t n,
                         unsigned int width) {
    size_t i = 1;

    while (i < n && x[i] == x[0])
        ++i;

    if (i == n) {
        bit_write(w, STREAM_CONSTANT, 2);
        bit_write_signed(w, x[0], width);
        return;
    }

    bit_write(w, STREAM_VERBATIM, 2);
    for (i = 0; i < n; ++i)
        bit_write_signed(w, x[i], width);
}

static bool read_stream(BIT_READER *r, int64_t *x, size_t n,
                        unsigned int width) {
    switch (bit_read(r, 2)) {
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
    bit_write(&w, CHANNELS_INDEPENDENT, 2);
    for (unsigned int c = 0; c < format->channels; ++c)
        write_stream(&w, work->planes + (size_t)c * format->block_size, frames,
                     format->bits);
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
    if (bit_read(&r, 2) != CHANNELS_INDEPENDENT)
        return false;

    for (unsigned int c = 0; c < format->channels; ++c) {
        if (!read_stream(&r, work->planes + (size_t)c * format->block_size,
                         frames, format->bits))
            return false;
    }

    if (!bit_reader_finish(&r))
        return false;

    pcm_pack(work->planes, format->block_size, format->channels, format->bits,
             pcm, pcm_size);
    return true;
}
