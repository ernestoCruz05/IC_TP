#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "img_hist_io.h"
#include "img_quant.h"

static int write_sample(FILE *file, uint16_t sample, int two_bytes,
                        int big_endian)
{
    int high = sample >> 8;
    int low = sample & 0xff;

    if (!two_bytes) {
        return fputc(low, file) != EOF;
    }
    if (big_endian) {
        return fputc(high, file) != EOF && fputc(low, file) != EOF;
    }
    return fputc(low, file) != EOF && fputc(high, file) != EOF;
}

static int write_pixels(FILE *file, uint16_t *const channels[3],
                        size_t channel_count, size_t pixel_count,
                        int two_bytes, int big_endian)
{
    for (size_t i = 0; i < pixel_count; i++) {
        for (size_t channel = 0; channel < channel_count; channel++) {
            if (!write_sample(file, channels[channel][i], two_bytes,
                              big_endian)) {
                return 0;
            }
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    int is_raw = 0;
    int rows_set = 0;
    int columns_set = 0;
    int bits_set = 0;
    int endian_set = 0;
    int quant_set = 0;
    int big_endian = 0;
    size_t raw_rows = 0;
    size_t raw_columns = 0;
    size_t raw_bits = 0;
    size_t quant_bits = 0;
    const char *input_path = NULL;
    const char *output_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            if (strcmp(argv[++i], "raw") != 0 || is_raw) {
                fprintf(stderr, "%s: only --format raw is supported here\n",
                        argv[0]);
                return 2;
            }
            is_raw = 1;
        } else if (strcmp(argv[i], "--rows") == 0 && i + 1 < argc) {
            if (rows_set || !parse_positive_argument(argv[++i], &raw_rows)) {
                fprintf(stderr, "%s: invalid or repeated --rows value\n",
                        argv[0]);
                return 2;
            }
            rows_set = 1;
        } else if (strcmp(argv[i], "--cols") == 0 && i + 1 < argc) {
            if (columns_set ||
                !parse_positive_argument(argv[++i], &raw_columns)) {
                fprintf(stderr, "%s: invalid or repeated --cols value\n",
                        argv[0]);
                return 2;
            }
            columns_set = 1;
        } else if (strcmp(argv[i], "--bits") == 0 && i + 1 < argc) {
            if (bits_set || !parse_positive_argument(argv[++i], &raw_bits)) {
                fprintf(stderr, "%s: invalid or repeated --bits value\n",
                        argv[0]);
                return 2;
            }
            bits_set = 1;
        } else if (strcmp(argv[i], "--endian") == 0 && i + 1 < argc) {
            if (endian_set) {
                fprintf(stderr, "%s: repeated --endian option\n", argv[0]);
                return 2;
            }
            const char *endian = argv[++i];
            if (strcmp(endian, "big") == 0) {
                big_endian = 1;
            } else if (strcmp(endian, "little") == 0) {
                big_endian = 0;
            } else {
                fprintf(stderr, "%s: --endian must be big or little\n",
                        argv[0]);
                return 2;
            }
            endian_set = 1;
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            if (quant_set ||
                !parse_positive_argument(argv[++i], &quant_bits)) {
                fprintf(stderr, "%s: invalid or repeated -b value\n",
                        argv[0]);
                return 2;
            }
            quant_set = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown or incomplete option '%s'\n",
                    argv[0], argv[i]);
            return 2;
        } else if (input_path == NULL) {
            input_path = argv[i];
        } else if (output_path == NULL) {
            output_path = argv[i];
        } else {
            fprintf(stderr, "%s: too many file arguments\n", argv[0]);
            return 2;
        }
    }

    if (input_path == NULL || output_path == NULL || !quant_set ||
        (is_raw && (!rows_set || !columns_set || !bits_set ||
                    (raw_bits == 16 && !endian_set) ||
                    (raw_bits != 8 && raw_bits != 16) ||
                    (raw_bits == 8 && endian_set))) ||
        (!is_raw && (rows_set || columns_set || bits_set || endian_set))) {
        fprintf(stderr,
                "Usage: %s -b BITS input.pgm|input.ppm output\n"
                "       %s -b BITS --format raw --rows N --cols N --bits 8 "
                "input.raw output.raw\n"
                "       %s -b BITS --format raw --rows N --cols N --bits 16 "
                "--endian big|little input.raw output.raw\n",
                argv[0], argv[0], argv[0]);
        return 2;
    }
    if (strcmp(input_path, output_path) == 0) {
        fprintf(stderr, "%s: input and output must be different files\n",
                argv[0]);
        return 2;
    }

    IMAGE image;
    if (!img_load(input_path, is_raw, raw_rows, raw_columns, raw_bits,
                  big_endian, &image)) {
        return 1;
    }

    int result = 1;
    FILE *output = NULL;
    int is_ppm = image.is_ppm;
    size_t channel_count = image.channel_count;
    size_t columns = image.columns;
    size_t rows = image.rows;
    size_t max_value = image.max_value;
    size_t pixel_count = columns * rows;

    unsigned source_bits = sample_bits(max_value);
    if (quant_bits >= source_bits) {
        fprintf(stderr,
                "%s: -b %zu must be less than the %u bits of the input\n",
                input_path, quant_bits, source_bits);
        goto cleanup;
    }

    for (size_t channel = 0; channel < channel_count; channel++) {
        for (size_t i = 0; i < pixel_count; i++) {
            image.pixels[channel][i] = quant_sample(image.pixels[channel][i],
                                                    source_bits,
                                                    (unsigned)quant_bits,
                                                    max_value);
        }
    }

    output = fopen(output_path, "wb");
    if (output == NULL) {
        perror(output_path);
        goto cleanup;
    }

    int header_ok = is_raw ||
                    fprintf(output, "%s\n%zu %zu\n%zu\n", is_ppm ? "P6" : "P5",
                            columns, rows, max_value) > 0;
    int pixels_ok = header_ok &&
                    write_pixels(output, image.pixels, channel_count,
                                 pixel_count, max_value > 255,
                                 is_raw ? big_endian : 1);
    int close_ok = fclose(output) == 0;
    output = NULL;
    if (!pixels_ok || !close_ok) {
        fprintf(stderr, "%s: failed to write the output image\n", output_path);
        goto cleanup;
    }

    result = 0;

cleanup:
    if (output != NULL) {
        fclose(output);
    }
    img_free(&image);
    return result;
}
