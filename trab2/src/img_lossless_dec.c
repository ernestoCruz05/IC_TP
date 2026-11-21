#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitstream.h"

static int read_u32_be(FILE *file, uint32_t *value)
{
    uint8_t b[4];
    if (fread(b, 1, 4, file) != 4) {
        return 0;
    }
    *value = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
             ((uint32_t)b[2] << 8) | (uint32_t)b[3];
    return 1;
}

static inline int32_t zigzag_decode(uint32_t u)
{
    return (u & 1u) ? -(int32_t)((u + 1u) / 2) : (int32_t)(u / 2);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s compressed_file output.pgm\n", argv[0]);
        return 2;
    }

    int result = 1;
    FILE *in = fopen(argv[1], "rb");
    FILE *out = NULL;
    uint8_t *orig_header = NULL;
    uint8_t *pixels = NULL;

    if (in == NULL) {
        perror(argv[1]);
        return 1;
    }

    char magic[4];
    int rice_k;
    uint32_t rows = 0;
    uint32_t cols = 0;
    uint32_t header_len = 0;

    if (fread(magic, 1, 4, in) != 4 || memcmp(magic, "IL00", 4) != 0 ||
        (rice_k = fgetc(in)) == EOF || rice_k < 1 || rice_k > 7 ||
        !read_u32_be(in, &rows) || !read_u32_be(in, &cols) ||
        !read_u32_be(in, &header_len) || rows == 0 || cols == 0) {
        fprintf(stderr, "%s: invalid compressed header\n", argv[1]);
        goto cleanup;
    }

    orig_header = malloc(header_len);
    size_t pixel_count = (size_t)rows * (size_t)cols;
    pixels = malloc(pixel_count);
    if (orig_header == NULL || pixels == NULL ||
        fread(orig_header, 1, header_len, in) != header_len) {
        fprintf(stderr, "%s: failed to read header or allocate memory\n",
                argv[1]);
        goto cleanup;
    }

    BIT_READER br;
    br_init(&br, in);

    for (size_t row = 0; row < rows; row++) {
        int32_t prev = 0;
        for (size_t col = 0; col < cols; col++) {
            uint32_t q = 0;
            uint32_t r = 0;
            if (!br_read_unary(&br, 1024, &q) ||
                !br_read_bits(&br, rice_k, &r)) {
                fprintf(stderr, "%s: corrupted bitstream\n", argv[1]);
                goto cleanup;
            }
            int32_t cur = prev + zigzag_decode((q << rice_k) | r);
            if (cur < 0 || cur > 255) {
                fprintf(stderr, "%s: decoded pixel out of range\n", argv[1]);
                goto cleanup;
            }
            pixels[row * cols + col] = (uint8_t)cur;
            prev = cur;
        }
    }

    out = fopen(argv[2], "wb");
    if (out == NULL) {
        perror(argv[2]);
        goto cleanup;
    }
    if (fwrite(orig_header, 1, header_len, out) != header_len ||
        fwrite(pixels, 1, pixel_count, out) != pixel_count ||
        fclose(out) != 0) {
        out = NULL;
        fprintf(stderr, "%s: failed to write output PGM\n", argv[2]);
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
