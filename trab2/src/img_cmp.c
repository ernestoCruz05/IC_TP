#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "img_hist_io.h"

static void print_metrics(const char *label, unsigned max_error,
                          uint64_t squared_sum, size_t sample_count,
                          size_t max_value)
{
    double mse = (double)squared_sum / (double)sample_count;

    printf("%s\n", label);
    printf("  Max absolute error: %u\n", max_error);
    printf("  MSE: %.6f\n", mse);
    if (squared_sum == 0) {
        puts("  PSNR: inf dB");
    } else {
        double peak = (double)max_value;
        printf("  PSNR: %.4f dB\n", 10.0 * log10(peak * peak / mse));
    }
}

int main(int argc, char **argv)
{
    int orig_is_raw = 0;
    int test_is_raw = 0;
    int rows_set = 0;
    int columns_set = 0;
    int bits_set = 0;
    int endian_set = 0;
    int big_endian = 0;
    size_t raw_rows = 0;
    size_t raw_columns = 0;
    size_t raw_bits = 0;
    const char *original_path = NULL;
    const char *test_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            if (strcmp(argv[++i], "raw") != 0 || orig_is_raw || test_is_raw) {
                fprintf(stderr, "%s: invalid or repeated --format raw\n",
                        argv[0]);
                return 2;
            }
            orig_is_raw = 1;
            test_is_raw = 1;
        } else if (strcmp(argv[i], "--orig-raw") == 0) {
            if (orig_is_raw) {
                fprintf(stderr, "%s: repeated --orig-raw option\n", argv[0]);
                return 2;
            }
            orig_is_raw = 1;
        } else if (strcmp(argv[i], "--test-raw") == 0) {
            if (test_is_raw) {
                fprintf(stderr, "%s: repeated --test-raw option\n", argv[0]);
                return 2;
            }
            test_is_raw = 1;
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
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown or incomplete option '%s'\n",
                    argv[0], argv[i]);
            return 2;
        } else if (original_path == NULL) {
            original_path = argv[i];
        } else if (test_path == NULL) {
            test_path = argv[i];
        } else {
            fprintf(stderr, "%s: too many file arguments\n", argv[0]);
            return 2;
        }
    }

    int any_raw = orig_is_raw || test_is_raw;
    if (original_path == NULL || test_path == NULL ||
        (any_raw && (!rows_set || !columns_set || !bits_set ||
                     (raw_bits == 16 && !endian_set) ||
                     (raw_bits != 8 && raw_bits != 16) ||
                     (raw_bits == 8 && endian_set))) ||
        (!any_raw && (rows_set || columns_set || bits_set || endian_set))) {
        fprintf(stderr,
                "Usage: %s original.pgm|original.ppm test.pgm|test.ppm\n"
                "       %s --format raw|--orig-raw|--test-raw "
                "--rows N --cols N --bits 8 original test\n"
                "       %s --format raw|--orig-raw|--test-raw "
                "--rows N --cols N --bits 16 --endian big|little "
                "original test\n",
                argv[0], argv[0], argv[0]);
        return 2;
    }

    IMAGE original;
    IMAGE test;
    if (!img_load(original_path, orig_is_raw, raw_rows, raw_columns, raw_bits,
                  big_endian, &original)) {
        return 1;
    }
    if (!img_load(test_path, test_is_raw, raw_rows, raw_columns, raw_bits,
                  big_endian, &test)) {
        img_free(&original);
        return 1;
    }

    if (original.is_ppm != test.is_ppm || original.columns != test.columns ||
        original.rows != test.rows || original.max_value != test.max_value) {
        fprintf(stderr,
                "Images are not comparable: original is %zux%zu (%s, "
                "Maxval %zu), test is %zux%zu (%s, Maxval %zu)\n",
                original.columns, original.rows,
                original.is_ppm ? "PPM" : "gray", original.max_value,
                test.columns, test.rows, test.is_ppm ? "PPM" : "gray",
                test.max_value);
        img_free(&original);
        img_free(&test);
        return 1;
    }

    size_t pixel_count = original.columns * original.rows;
    static const char *channel_names[3] = {"Red", "Green", "Blue"};

    printf("Original: %s\n", original_path);
    printf("Test: %s\n", test_path);
    printf("Dimensions: %zu columns x %zu rows\n", original.columns,
           original.rows);
    printf("Maxval (PSNR peak): %zu\n\n", original.max_value);

    unsigned global_max_error = 0;
    uint64_t global_squared_sum = 0;
    for (size_t channel = 0; channel < original.channel_count; channel++) {
        unsigned max_error = 0;
        uint64_t squared_sum = 0;
        for (size_t i = 0; i < pixel_count; i++) {
            int difference = (int)original.pixels[channel][i] -
                             (int)test.pixels[channel][i];
            unsigned error = (unsigned)(difference < 0 ? -difference
                                                       : difference);
            if (error > max_error) {
                max_error = error;
            }
            squared_sum += (uint64_t)error * error;
        }

        print_metrics(original.is_ppm ? channel_names[channel] : "Image",
                      max_error, squared_sum, pixel_count,
                      original.max_value);
        puts("");
        if (max_error > global_max_error) {
            global_max_error = max_error;
        }
        global_squared_sum += squared_sum;
    }

    if (original.is_ppm) {
        print_metrics("All channels", global_max_error, global_squared_sum,
                      pixel_count * original.channel_count,
                      original.max_value);
    }

    img_free(&original);
    img_free(&test);
    return 0;
}
