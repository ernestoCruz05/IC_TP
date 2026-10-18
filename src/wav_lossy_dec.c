#include "lossless/crc32.h"
#include "lossless/pcm.h"
#include "lossless/rice.h"
#include "lossy/dct.h"
#include "wav.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 24u
#define TRANSFORM_DCT2 1u

static uint64_t get_le(const uint8_t *p, unsigned bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= (uint64_t)p[i] << (8u * i);
    return value;
}

static int read_exact(FILE *file, void *data, size_t size) {
    return fread(data, 1, size, file) == size;
}

int main(int argc, char **argv) {
    FILE *in = NULL, *out = NULL;
    const char *input_path = NULL, *output_path = NULL;
    uint8_t header[HEADER_SIZE];
    uint8_t *payload = NULL;
    int64_t *quantized = NULL;
    double *coeff = NULL, *samples = NULL;
    int32_t *pcm_block = NULL;
    uint64_t frames, block_count;
    unsigned channels, bits, n, g;
    uint32_t sample_rate, crc = 0;
    uint64_t frame = 0;
    int result = EXIT_FAILURE;

    if (argc != 3 || strcmp(argv[1], argv[2]) == 0) {
        fprintf(stderr, "Usage: %s compressed_file output.wav\n", argv[0]);
        return EXIT_FAILURE;
    }
    input_path = argv[1]; output_path = argv[2];
    in = fopen(input_path, "rb");
    if (!in || !read_exact(in, header, sizeof header) ||
        memcmp(header, "ICLY", 4) != 0 || header[4] != 1 ||
        header[7] != TRANSFORM_DCT2 ||
        header[23] != 0) {
        fprintf(stderr, "%s: invalid or unsupported lossy file\n", input_path);
        goto done;
    }
    channels = header[5]; bits = header[6];
    sample_rate = (uint32_t)get_le(header + 8, 4);
    frames = get_le(header + 12, 8); n = (unsigned)get_le(header + 20, 2); g = header[22];
    if (channels < 1 || channels > 8 ||
        (bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
        sample_rate == 0 || n < 256 || n > 4096 || (n & (n - 1)) != 0 || g > 240 ||
        frames > (UINT32_MAX - 37u) / (channels * (bits / 8u))) {
        fprintf(stderr, "%s: invalid header fields\n", input_path);
        goto done;
    }
    WAV_INFO info = {.audio_format = 1, .num_channels = (uint16_t)channels,
                     .sample_rate = sample_rate,
                     .block_align = (uint16_t)(channels * (bits / 8u)),
                     .byte_rate = (uint32_t)((uint64_t)sample_rate * channels * (bits / 8u)),
                     .bits_per_sample = (uint16_t)bits,
                     .data_size = (uint32_t)(frames * channels * (bits / 8u))};
    if ((uint64_t)sample_rate * channels * (bits / 8u) > UINT32_MAX) goto done;
    out = fopen(output_path, "wb");
    if (!out || !wav_write_header(out, &info)) goto done;
    quantized = malloc((size_t)n * sizeof(*quantized));
    coeff = malloc((size_t)n * sizeof(*coeff));
    samples = malloc((size_t)n * sizeof(*samples));
    pcm_block = malloc((size_t)n * channels * sizeof(*pcm_block));
    payload = malloc((size_t)n * 16u + 4096u);
    if (!quantized || !coeff || !samples || !pcm_block || !payload) goto done;
    block_count = (frames + n - 1) / n;
    double step = exp2(-(double)g / 8.0);
    double scale = ldexp(1.0, (int)bits - 1);
    int32_t min_sample, max_sample;
    if (!wav_sample_range(&info, &min_sample, &max_sample)) goto done;

    for (uint64_t block = 0; block < block_count; ++block) {
        uint8_t size_bytes[4];
        uint32_t payload_size;
        BIT_READER reader;
        size_t count;
        count = (size_t)((frames - frame) < n ? frames - frame : n);
        if (!read_exact(in, size_bytes, sizeof size_bytes)) goto done;
        payload_size = (uint32_t)get_le(size_bytes, 4);
        if (payload_size == 0 || payload_size > (size_t)n * 16u + 4096u ||
            !read_exact(in, payload, payload_size)) goto done;
        crc = crc32_update(crc, size_bytes, 4);
        crc = crc32_update(crc, payload, payload_size);
        bit_reader_init(&reader, payload, payload_size);
        for (unsigned c = 0; c < channels; ++c) {
            if (!rice_read_residuals(&reader, n, 0, quantized)) goto done;
            for (unsigned i = 0; i < n; ++i) {
                coeff[i] = (double)quantized[i] * step;
                if (!isfinite(coeff[i])) goto done;
            }
            dct2_inverse(coeff, samples, n);
            for (size_t i = 0; i < count; ++i) {
                double reconstructed = samples[i];
                double raw = nearbyint(reconstructed * scale);
                int32_t sample;
                if (!isfinite(raw)) goto done;
                if (raw < min_sample) raw = min_sample;
                if (raw > max_sample) raw = max_sample;
                sample = (int32_t)raw;
                pcm_block[i * channels + c] = sample;
            }
        }
        if (!bit_reader_finish(&reader)) goto done;
        for (size_t i = 0; i < count; ++i)
            for (unsigned c = 0; c < channels; ++c)
                if (!wav_write_sample(out, &info, pcm_block[i * channels + c]))
                    goto done;
        frame += count;
    }
    uint8_t crc_bytes[4];
    if (!read_exact(in, crc_bytes, 4) || get_le(crc_bytes, 4) != crc ||
        fgetc(in) != EOF || ferror(in)) goto done;
    if ((info.data_size & 1u) && fputc(0, out) == EOF) goto done;
    result = EXIT_SUCCESS;
done:
    if (in) fclose(in);
    if (out && fclose(out) != 0) result = EXIT_FAILURE;
    if (result != EXIT_SUCCESS && output_path) remove(output_path);
    free(pcm_block); free(payload); free(samples); free(coeff); free(quantized);
    return result;
}
