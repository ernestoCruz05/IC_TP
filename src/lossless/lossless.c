#include "lossless.h"

#include "block.h"
#include "crc32.h"

#include <stdlib.h>
#include <string.h>

#define FORMAT_VERSION 1
#define HEADER_SIZE 34
#define BLOCK_HEADER_SIZE 6
#define CRC_SIZE 4
#define COPY_SIZE 65536

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

static bool encode_blocks(FILE *in, FILE *out, const HEADER *h, uint32_t *crc,
                          const char **error) {
    BLOCK_FORMAT format = {h->channels, h->bits, h->block_size};
    size_t frame_bytes = (size_t)h->channels * (h->bits / 8);
    size_t capacity = block_payload_bound(&format, h->block_size);
    uint64_t frames_left = h->pcm_size / frame_bytes;
    uint8_t *pcm = malloc(frame_bytes * h->block_size);
    uint8_t *block = malloc(BLOCK_HEADER_SIZE + capacity);
    BLOCK_WORK *work = block_work_create(&format);
    bool ok = false;

    if (!pcm || !block || !work) {
        *error = "out of memory";
        goto cleanup;
    }

    while (frames_left > 0) {
        size_t frames =
            frames_left < h->block_size ? (size_t)frames_left : h->block_size;
        size_t pcm_size = frames * frame_bytes;
        size_t size;

        if (!read_exact(in, pcm, pcm_size, error))
            goto cleanup;
        *crc = crc32_update(*crc, pcm, pcm_size);

        if (!block_encode(work, pcm, pcm_size, block + BLOCK_HEADER_SIZE,
                          capacity, &size)) {
            *error = "block encoding failed";
            goto cleanup;
        }
        put_le(block, size, 4);
        put_le(block + 4, frames, 2);
        if (!write_exact(out, block, BLOCK_HEADER_SIZE + size, error))
            goto cleanup;

        frames_left -= frames;
    }
    ok = true;

cleanup:
    block_work_destroy(work);
    free(block);
    free(pcm);
    return ok;
}

static bool decode_blocks(FILE *in, FILE *out, const HEADER *h, uint32_t *crc,
                          const char **error) {
    BLOCK_FORMAT format = {h->channels, h->bits, h->block_size};
    size_t frame_bytes = (size_t)h->channels * (h->bits / 8);
    uint64_t frames_left = h->pcm_size / frame_bytes;
    uint8_t *pcm = malloc(frame_bytes * h->block_size);
    uint8_t *payload = malloc(block_payload_bound(&format, h->block_size));
    BLOCK_WORK *work = block_work_create(&format);
    bool ok = false;

    if (!pcm || !payload || !work) {
        *error = "out of memory";
        goto cleanup;
    }

    while (frames_left > 0) {
        size_t frames =
            frames_left < h->block_size ? (size_t)frames_left : h->block_size;
        size_t pcm_size = frames * frame_bytes;
        uint8_t block[BLOCK_HEADER_SIZE];
        uint64_t size;

        if (!read_exact(in, block, sizeof block, error))
            goto cleanup;
        size = get_le(block, 4);
        if (get_le(block + 4, 2) != frames ||
            size > block_payload_bound(&format, frames)) {
            *error = "corrupt block header";
            goto cleanup;
        }

        if (!read_exact(in, payload, (size_t)size, error))
            goto cleanup;
        if (!block_decode(work, payload, (size_t)size, pcm, pcm_size)) {
            *error = "corrupt block";
            goto cleanup;
        }

        *crc = crc32_update(*crc, pcm, pcm_size);
        if (!write_exact(out, pcm, pcm_size, error))
            goto cleanup;

        frames_left -= frames;
    }
    ok = true;

cleanup:
    block_work_destroy(work);
    free(payload);
    free(pcm);
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
        !encode_blocks(in, out, &header, &crc, error))
        goto cleanup;
    if (!copy_bytes(in, out, header.suffix_size, buffer, &crc, error))
        goto cleanup;

    put_le(bytes, crc, CRC_SIZE);
    ok = write_exact(out, bytes, CRC_SIZE, error);

cleanup:
    free(buffer);
    return ok;
}

bool lossless_decode(FILE *in, FILE *out, const char **error) {
    HEADER header = {0};
    uint8_t bytes[HEADER_SIZE];
    uint8_t *buffer = NULL;
    uint32_t crc = 0;
    bool ok = false;

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
        !decode_blocks(in, out, &header, &crc, error))
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
