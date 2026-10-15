#include "lossless.h"

#include "block.h"
#include "crc32.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FORMAT_VERSION 1
#define HEADER_SIZE 34
#define BLOCK_HEADER_SIZE 6
#define CRC_SIZE 4
#define COPY_SIZE 65536
#define BLOCKS_PER_THREAD 4
#define BATCH_MEMORY ((size_t)64 << 20u)

enum { MODE_STORED, MODE_PCM };

static const uint8_t magic[4] = {'I', 'C', 'L', 'L'};

typedef struct {
    unsigned int mode;
    unsigned int channels;
    unsigned int bits;
    unsigned int block_size;
    uint64_t prefix_size;
    uint64_t pcm_size;
    uint64_t suffix_size;
} HEADER;

static void put_le(uint8_t *p, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i)
        p[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t get_le(const uint8_t *p, size_t bytes) {
    uint64_t value = 0;

    for (size_t i = 0; i < bytes; ++i)
        value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static bool read_exact(FILE *in, void *data, size_t size, const char **error) {
    if (fread(data, 1, size, in) == size)
        return true;

    *error = ferror(in) != 0 ? "read error" : "unexpected end of file";
    return false;
}

static bool write_exact(FILE *out, const void *data, size_t size,
                        const char **error) {
    if (fwrite(data, 1, size, out) == size)
        return true;

    *error = "write error";
    return false;
}

static bool copy_bytes(FILE *in, FILE *out, uint64_t size, uint8_t *buffer,
                       uint32_t *crc, const char **error) {
    while (size > 0) {
        size_t chunk = size < COPY_SIZE ? (size_t)size : COPY_SIZE;

        if (!read_exact(in, buffer, chunk, error))
            return false;
        *crc = crc32_update(*crc, buffer, chunk);
        if (!write_exact(out, buffer, chunk, error))
            return false;
        size -= chunk;
    }
    return true;
}

static bool valid_pcm(const HEADER *h) {
    uint64_t frame_bytes = (uint64_t)h->channels * (h->bits / 8);

    return h->channels >= 1 && h->channels <= LOSSLESS_MAX_CHANNELS &&
           (h->bits == 8 || h->bits == 16 || h->bits == 24 || h->bits == 32) &&
           h->block_size >= LOSSLESS_MIN_BLOCK_SIZE &&
           h->block_size <= LOSSLESS_MAX_BLOCK_SIZE &&
           h->pcm_size % frame_bytes == 0;
}

static void header_pack(const HEADER *h, uint8_t *p) {
    memcpy(p, magic, sizeof magic);
    p[4] = FORMAT_VERSION;
    p[5] = (uint8_t)h->mode;
    p[6] = (uint8_t)h->channels;
    p[7] = (uint8_t)h->bits;
    put_le(p + 8, h->block_size, 2);
    put_le(p + 10, h->prefix_size, 8);
    put_le(p + 18, h->pcm_size, 8);
    put_le(p + 26, h->suffix_size, 8);
}

static bool header_unpack(const uint8_t *p, HEADER *h, const char **error) {
    if (memcmp(p, magic, sizeof magic) != 0) {
        *error = "not a compressed file";
        return false;
    }
    if (p[4] != FORMAT_VERSION) {
        *error = "unsupported format version";
        return false;
    }

    h->mode = p[5];
    h->channels = p[6];
    h->bits = p[7];
    h->block_size = (unsigned int)get_le(p + 8, 2);
    h->prefix_size = get_le(p + 10, 8);
    h->pcm_size = get_le(p + 18, 8);
    h->suffix_size = get_le(p + 26, 8);

    if ((h->mode == MODE_STORED && h->channels == 0 && h->bits == 0 &&
         h->block_size == 0 && h->pcm_size == 0) ||
        (h->mode == MODE_PCM && valid_pcm(h)))
        return true;

    *error = "invalid header";
    return false;
}

typedef struct {
    BLOCK_FORMAT format;
    const BLOCK_OPTIONS *options;
    size_t pcm_capacity;
    size_t payload_capacity;
    size_t capacity;
    size_t count;
    unsigned int threads;
    uint8_t *pcm;
    uint8_t *payload;
    size_t *pcm_sizes;
    size_t *payload_sizes;
    bool *ok;
    BLOCK_WORK **works;
} BATCH;

typedef struct {
    BATCH *batch;
    unsigned int index;
} WORKER;

static void batch_finish(BATCH *b) {
    if (b->works) {
        for (unsigned int t = 0; t < b->threads; ++t)
            block_work_destroy(b->works[t]);
    }
    free(b->works);
    free(b->ok);
    free(b->payload_sizes);
    free(b->pcm_sizes);
    free(b->payload);
    free(b->pcm);
}

static bool batch_init(BATCH *b, const HEADER *h, const BLOCK_OPTIONS *options,
                       unsigned int threads, uint64_t frames) {
    uint64_t blocks = (frames + h->block_size - 1) / h->block_size;
    size_t slot;

    *b = (BATCH){.format = {h->channels, h->bits, h->block_size},
                 .options = options};
    b->pcm_capacity = (size_t)h->channels * (h->bits / 8) * h->block_size;
    b->payload_capacity = block_payload_bound(&b->format, h->block_size);
    slot = b->pcm_capacity + b->payload_capacity;

    b->capacity = (size_t)threads * BLOCKS_PER_THREAD;
    while (b->capacity > threads && b->capacity * slot > BATCH_MEMORY)
        --b->capacity;
    if (b->capacity > blocks)
        b->capacity = (size_t)blocks;
    b->threads = threads < b->capacity ? threads : (unsigned int)b->capacity;

    b->pcm = malloc(b->capacity * b->pcm_capacity);
    b->payload = malloc(b->capacity * b->payload_capacity);
    b->pcm_sizes = malloc(b->capacity * sizeof(*b->pcm_sizes));
    b->payload_sizes = malloc(b->capacity * sizeof(*b->payload_sizes));
    b->ok = malloc(b->capacity * sizeof(*b->ok));
    b->works = calloc(b->threads, sizeof(*b->works));
    if (!b->pcm || !b->payload || !b->pcm_sizes || !b->payload_sizes ||
        !b->ok || !b->works)
        return false;

    for (unsigned int t = 0; t < b->threads; ++t) {
        b->works[t] = block_work_create(&b->format);
        if (!b->works[t])
            return false;
    }
    return true;
}

static void *batch_worker(void *arg) {
    WORKER *worker = arg;
    BATCH *b = worker->batch;
    BLOCK_WORK *work = b->works[worker->index];

    for (size_t i = worker->index; i < b->count; i += b->threads) {
        uint8_t *pcm = b->pcm + i * b->pcm_capacity;
        uint8_t *payload = b->payload + i * b->payload_capacity;

        if (b->options)
            b->ok[i] = block_encode(work, b->options, pcm, b->pcm_sizes[i],
                                    payload, b->payload_capacity,
                                    &b->payload_sizes[i]);
        else
            b->ok[i] = block_decode(work, payload, b->payload_sizes[i], pcm,
                                    b->pcm_sizes[i]);
    }
    return NULL;
}

static void batch_run(BATCH *b) {
    WORKER workers[LOSSLESS_MAX_THREADS];
    pthread_t ids[LOSSLESS_MAX_THREADS];
    bool started[LOSSLESS_MAX_THREADS] = {false};
    unsigned int active =
        b->count < b->threads ? (unsigned int)b->count : b->threads;

    for (unsigned int t = 0; t < active; ++t) {
        workers[t] = (WORKER){b, t};
        if (t > 0)
            started[t] =
                pthread_create(&ids[t], NULL, batch_worker, &workers[t]) == 0;
    }

    if (active > 0)
        (void)batch_worker(&workers[0]);
    for (unsigned int t = 1; t < active; ++t) {
        if (started[t])
            (void)pthread_join(ids[t], NULL);
        else
            (void)batch_worker(&workers[t]);
    }
}

static bool encode_blocks(FILE *in, FILE *out, const HEADER *h,
                          const LOSSLESS_OPTIONS *options, uint32_t *crc,
                          const char **error) {
    size_t frame_bytes = (size_t)h->channels * (h->bits / 8);
    uint64_t frames_left = h->pcm_size / frame_bytes;
    BATCH batch;
    bool ok = false;

    if (frames_left == 0)
        return true;

    if (!batch_init(&batch, h, &options->block, options->threads,
                    frames_left)) {
        *error = "out of memory";
        goto cleanup;
    }

    while (frames_left > 0) {
        for (batch.count = 0; batch.count < batch.capacity && frames_left > 0;
             ++batch.count) {
            size_t i = batch.count;
            size_t frames = frames_left < h->block_size ? (size_t)frames_left
                                                        : h->block_size;
            uint8_t *pcm = batch.pcm + i * batch.pcm_capacity;

            batch.pcm_sizes[i] = frames * frame_bytes;
            if (!read_exact(in, pcm, batch.pcm_sizes[i], error))
                goto cleanup;
            *crc = crc32_update(*crc, pcm, batch.pcm_sizes[i]);
            frames_left -= frames;
        }

        batch_run(&batch);

        for (size_t i = 0; i < batch.count; ++i) {
            uint8_t block[BLOCK_HEADER_SIZE];

            if (!batch.ok[i]) {
                *error = "block encoding failed";
                goto cleanup;
            }
            put_le(block, batch.payload_sizes[i], 4);
            put_le(block + 4, batch.pcm_sizes[i] / frame_bytes, 2);
            if (!write_exact(out, block, sizeof block, error) ||
                !write_exact(out, batch.payload + i * batch.payload_capacity,
                             batch.payload_sizes[i], error))
                goto cleanup;
        }
    }
    ok = true;

cleanup:
    batch_finish(&batch);
    return ok;
}

static bool decode_blocks(FILE *in, FILE *out, const HEADER *h,
                          unsigned int threads, uint32_t *crc,
                          const char **error) {
    size_t frame_bytes = (size_t)h->channels * (h->bits / 8);
    uint64_t frames_left = h->pcm_size / frame_bytes;
    BATCH batch;
    bool ok = false;

    if (frames_left == 0)
        return true;

    if (!batch_init(&batch, h, NULL, threads, frames_left)) {
        *error = "out of memory";
        goto cleanup;
    }

    while (frames_left > 0) {
        for (batch.count = 0; batch.count < batch.capacity && frames_left > 0;
             ++batch.count) {
            size_t i = batch.count;
            size_t frames = frames_left < h->block_size ? (size_t)frames_left
                                                        : h->block_size;
            uint8_t block[BLOCK_HEADER_SIZE];
            uint64_t size;

            if (!read_exact(in, block, sizeof block, error))
                goto cleanup;
            size = get_le(block, 4);
            if (get_le(block + 4, 2) != frames ||
                size > block_payload_bound(&batch.format, frames)) {
                *error = "corrupt block header";
                goto cleanup;
            }
            if (!read_exact(in, batch.payload + i * batch.payload_capacity,
                            (size_t)size, error))
                goto cleanup;

            batch.payload_sizes[i] = (size_t)size;
            batch.pcm_sizes[i] = frames * frame_bytes;
            frames_left -= frames;
        }

        batch_run(&batch);

        for (size_t i = 0; i < batch.count; ++i) {
            uint8_t *pcm = batch.pcm + i * batch.pcm_capacity;

            if (!batch.ok[i]) {
                *error = "corrupt block";
                goto cleanup;
            }
            *crc = crc32_update(*crc, pcm, batch.pcm_sizes[i]);
            if (!write_exact(out, pcm, batch.pcm_sizes[i], error))
                goto cleanup;
        }
    }
    ok = true;

cleanup:
    batch_finish(&batch);
    return ok;
}

bool lossless_encode(FILE *in, const LOSSLESS_SOURCE *source,
                     const LOSSLESS_OPTIONS *options, FILE *out,
                     const char **error) {
    HEADER header = {0};
    uint8_t bytes[HEADER_SIZE];
    uint8_t *buffer = NULL;
    uint32_t crc = 0;
    bool ok = false;

    if (options->threads == 0 || options->threads > LOSSLESS_MAX_THREADS) {
        *error = "invalid thread count";
        return false;
    }

    if (source->channels == 0) {
        header.mode = MODE_STORED;
        header.prefix_size = source->file_size;
    } else {
        header.mode = MODE_PCM;
        header.channels = source->channels;
        header.bits = source->bits;
        header.block_size = options->block_size;
        header.prefix_size = source->prefix_size;
        header.pcm_size = source->pcm_size;
        if (!valid_pcm(&header) || source->prefix_size > source->file_size ||
            source->pcm_size > source->file_size - source->prefix_size) {
            *error = "invalid source layout or options";
            return false;
        }
        header.suffix_size =
            source->file_size - source->prefix_size - source->pcm_size;
    }

    if (fseek(in, 0, SEEK_SET) != 0) {
        *error = "cannot seek input";
        return false;
    }

    header_pack(&header, bytes);
    if (!write_exact(out, bytes, HEADER_SIZE, error))
        return false;

    buffer = malloc(COPY_SIZE);
    if (!buffer) {
        *error = "out of memory";
        return false;
    }

    if (!copy_bytes(in, out, header.prefix_size, buffer, &crc, error))
        goto cleanup;
    if (header.mode == MODE_PCM &&
        !encode_blocks(in, out, &header, options, &crc, error))
        goto cleanup;
    if (!copy_bytes(in, out, header.suffix_size, buffer, &crc, error))
        goto cleanup;

    put_le(bytes, crc, CRC_SIZE);
    ok = write_exact(out, bytes, CRC_SIZE, error);

cleanup:
    free(buffer);
    return ok;
}

bool lossless_decode(FILE *in, FILE *out, unsigned int threads,
                     const char **error) {
    HEADER header = {0};
    uint8_t bytes[HEADER_SIZE];
    uint8_t *buffer = NULL;
    uint32_t crc = 0;
    bool ok = false;

    if (threads == 0 || threads > LOSSLESS_MAX_THREADS) {
        *error = "invalid thread count";
        return false;
    }

    if (!read_exact(in, bytes, HEADER_SIZE, error) ||
        !header_unpack(bytes, &header, error))
        return false;

    buffer = malloc(COPY_SIZE);
    if (!buffer) {
        *error = "out of memory";
        return false;
    }

    if (!copy_bytes(in, out, header.prefix_size, buffer, &crc, error))
        goto cleanup;
    if (header.mode == MODE_PCM &&
        !decode_blocks(in, out, &header, threads, &crc, error))
        goto cleanup;
    if (!copy_bytes(in, out, header.suffix_size, buffer, &crc, error))
        goto cleanup;

    if (!read_exact(in, bytes, CRC_SIZE, error))
        goto cleanup;
    if (get_le(bytes, CRC_SIZE) != crc) {
        *error = "CRC mismatch";
        goto cleanup;
    }
    if (fgetc(in) != EOF) {
        *error = "unexpected data after the end";
        goto cleanup;
    }
    if (ferror(in) != 0) {
        *error = "read error";
        goto cleanup;
    }
    ok = true;

cleanup:
    free(buffer);
    return ok;
}

unsigned int lossless_default_threads(void) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);

    if (count < 1)
        return 1;
    return count > LOSSLESS_MAX_THREADS ? LOSSLESS_MAX_THREADS
                                        : (unsigned int)count;
}
