#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitstream.h"
#include "img_hist_io.h"

static int write_u32_be(FILE *file, uint32_t value)
{
    uint8_t b[4];
    b[0] = (uint8_t)((value >> 24) & 0xffu);
    b[1] = (uint8_t)((value >> 16) & 0xffu);
    b[2] = (uint8_t)((value >> 8) & 0xffu);
    b[3] = (uint8_t)(value & 0xffu);
    return fwrite(b, 1, 4, file) == 4;
}

static inline uint32_t zigzag_encode(int32_t err)
{
    return (err >= 0) ? (uint32_t)(2 * err) : (uint32_t)(-2 * err - 1);
}

int main(int argc, char **argv)
{
    int rice_k = 3;
    const char *input_path = NULL;
    const char *output_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            size_t parsed = 0;
            if (!parse_positive_argument(argv[++i], &parsed) || parsed > 7) {
                fprintf(stderr, "%s: -k must be between 1 and 7\n", argv[0]);
                return 2;
            }
            rice_k = (int)parsed;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], argv[i]);
            return 2;
        } else if (input_path == NULL) {
            input_path = argv[i];
        } else if (output_path == NULL) {
            output_path = argv[i];
        } else {
            fprintf(stderr, "%s: too many arguments\n", argv[0]);
            return 2;
        }
    }

    if (input_path == NULL || output_path == NULL) {
        fprintf(stderr, "Usage: %s [-k 1..7] input.pgm compressed_file\n",
                argv[0]);
        return 2;
    }

    int result = 1;
    FILE *in = fopen(input_path, "rb");
    FILE *out = NULL;
    uint8_t *orig_header = NULL;
    uint16_t *pixels = NULL;

    if (in == NULL) {
        perror(input_path);
        return 1;
    }

    char magic[TOKEN_CAPACITY];
    size_t cols = 0;
    size_t rows = 0;
    size_t max_value = 0;
    if (!read_pnm_token(in, magic) || strcmp(magic, "P5") != 0 ||
        !parse_positive_integer(in, &cols) ||
        !parse_positive_integer(in, &rows) ||
        !parse_positive_integer(in, &max_value) ||
        max_value > 255) {
        fprintf(stderr, "%s: only 8-bit PGM (P5) is supported in this phase\n",
                input_path);
        goto cleanup;
    }

    long header_end = ftell(in);
    if (header_end <= 0 || (uint64_t)header_end > UINT32_MAX) {
        fprintf(stderr, "%s: failed to read PGM header length\n", input_path);
        goto cleanup;
    }
    size_t header_len = (size_t)header_end;
    orig_header = malloc(header_len);
    if (orig_header == NULL || fseek(in, 0, SEEK_SET) != 0 ||
        fread(orig_header, 1, header_len, in) != header_len) {
        fprintf(stderr, "%s: failed to copy PGM header\n", input_path);
        goto cleanup;
    }

    size_t pixel_count = rows * cols;
    pixels = malloc(pixel_count * sizeof(uint16_t));
    if (pixels == NULL) {
        fprintf(stderr, "%s: out of memory\n", input_path);
        goto cleanup;
    }
    if (!read_pgm_pixels(in, pixels, pixel_count, max_value)) {
        fprintf(stderr, "%s: invalid or incomplete pixel data\n", input_path);
        goto cleanup;
    }

    out = fopen(output_path, "wb");
    if (out == NULL) {
        perror(output_path);
        goto cleanup;
    }

    if (fwrite("IL00", 1, 4, out) != 4 ||
        fputc(rice_k, out) == EOF ||
        !write_u32_be(out, (uint32_t)rows) ||
        !write_u32_be(out, (uint32_t)cols) ||
        !write_u32_be(out, (uint32_t)header_len) ||
        fwrite(orig_header, 1, header_len, out) != header_len) {
        fprintf(stderr, "%s: failed to write header\n", output_path);
        goto cleanup;
    }

    BIT_WRITER bw;
    bw_init(&bw, out);

    // Predicao simples 1D (vizinho da esquerda) + Golomb-Rice com k fixo
    for (size_t row = 0; row < rows; row++) {
        int32_t prev = 0;
        for (size_t col = 0; col < cols; col++) {
            int32_t cur = (int32_t)pixels[row * cols + col];
            uint32_t u = zigzag_encode(cur - prev);
            uint32_t q = u >> rice_k;
            if (!bw_write_unary(&bw, q) || !bw_write_bits(&bw, u, rice_k)) {
                fprintf(stderr, "%s: failed to write bitstream\n", output_path);
                goto cleanup;
            }
            prev = cur;
        }
    }

    if (!bw_flush(&bw) || fclose(out) != 0) {
        out = NULL;
        fprintf(stderr, "%s: failed to finalize output\n", output_path);
        goto cleanup;
    }
    out = NULL;
    result = 0;

cleanup:
    if (out != NULL) fclose(out);
    fclose(in);
    free(orig_header);
    free(pixels);
    return result;
}
