#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "histogram_stats.h"
#include "histogram_svg.h"
#include "img_hist_io.h"

// ideia geral : abrir um file PGM -> ler infos do cabecalho -> contar pixeis -> calcular entropia
int main(int argc, char **argv)
{
    unsigned first_bit = 0;
    unsigned last_bit = 0;
    int use_bit_range = 0;
    int is_raw = 0;
    int rows_set = 0;
    int columns_set = 0;
    int bits_set = 0;
    int endian_set = 0;
    int big_endian = 0;
    size_t raw_rows = 0;
    size_t raw_columns = 0;
    size_t raw_bits = 0;
    const char *input_path = NULL;

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
        } else if (strcmp(argv[i], "--bit-range") == 0 && i + 1 < argc) {
            if (use_bit_range ||
                !parse_bit_range(argv[++i], &first_bit, &last_bit)) {
                fprintf(stderr, "%s: invalid or repeated --bit-range\n",
                        argv[0]);
                return 2;
            }
            use_bit_range = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown or incomplete option '%s'\n",
                    argv[0], argv[i]);
            return 2;
        } else if (input_path == NULL) {
            input_path = argv[i];
        } else {
            fprintf(stderr, "%s: more than one input file\n", argv[0]);
            return 2;
        }
    }

    if (input_path == NULL ||
        (is_raw && (!rows_set || !columns_set || !bits_set ||
                    (raw_bits == 16 && !endian_set) ||
                    (raw_bits != 8 && raw_bits != 16) ||
                    (raw_bits == 8 && endian_set))) ||
        (!is_raw && (rows_set || columns_set || bits_set || endian_set))) {
        fprintf(stderr,
                "Usage: %s [--bit-range FIRST-LAST] image.pgm\n"
                "       %s --format raw --rows N --cols N --bits 8 image.raw\n"
                "       %s --format raw --rows N --cols N --bits 16 "
                "--endian big|little image.raw\n",
                argv[0], argv[0], argv[0]);
        return 2;
    }

    FILE *file = fopen(input_path, "rb");
    if (file == NULL) {
        perror(input_path);
        return 1;
    }

    int is_ppm = 0;
    size_t channel_count = 1;
    size_t columns = is_raw ? raw_columns : 0;
    size_t rows = is_raw ? raw_rows : 0;
    size_t max_value = is_raw ? (raw_bits == 8 ? 255 : 65535) : 0;
    if (!is_raw) {
        char magic[TOKEN_CAPACITY];
        if (!read_pnm_token(file, magic) ||
            (strcmp(magic, "P5") != 0 && strcmp(magic, "P6") != 0) ||
            !parse_positive_integer(file, &columns) ||
            !parse_positive_integer(file, &rows) ||
            !parse_positive_integer(file, &max_value)) {
            fprintf(stderr, "%s: invalid or unsupported PGM/PPM header\n",
                    input_path);
            fclose(file);
            return 1;
        }
        is_ppm = strcmp(magic, "P6") == 0;
        channel_count = is_ppm ? 3 : 1;
        if (max_value > 65535) {
            fprintf(stderr,
                    "%s: Maxval above 65535 is not supported\n",
                    input_path);
            fclose(file);
            return 1;
        }
    }

    if (columns > SIZE_MAX / rows) {
        fprintf(stderr, "%s: image dimensions are too large\n", input_path);
        fclose(file);
        return 1;
    }

    size_t pixel_count = columns * rows;
    if (pixel_count > SIZE_MAX / sizeof(uint16_t)) {
        fprintf(stderr, "%s: image is too large to store in memory\n", input_path);
        fclose(file);
        return 1;
    }

    uint16_t *pixels[3] = {NULL, NULL, NULL};
    for (size_t channel = 0; channel < channel_count; channel++) {
        pixels[channel] = malloc(pixel_count * sizeof(*pixels[channel]));
        if (pixels[channel] == NULL) {
            fprintf(stderr, "%s: not enough memory for image channels\n",
                    input_path);
            for (size_t allocated = 0; allocated < channel_count; allocated++) {
                free(pixels[allocated]);
            }
            fclose(file);
            return 1;
        }
    }

    int pixels_read;
    if (is_raw) {
        pixels_read = read_raw_pixels(file, pixels[0], pixel_count,
                                      raw_bits, big_endian);
    } else if (is_ppm) {
        pixels_read = read_ppm_pixels(file, pixels[0], pixels[1], pixels[2],
                                      pixel_count, max_value);
    } else {
        pixels_read = read_pgm_pixels(file, pixels[0], pixel_count, max_value);
    }
    if (!pixels_read) {
        fprintf(stderr, "%s: invalid, incomplete, or excess pixel data\n",
                input_path);
        for (size_t channel = 0; channel < channel_count; channel++) {
            free(pixels[channel]);
        }
        fclose(file);
        return 1;
    }
    fclose(file);

    size_t analyzed_max = max_value;
    if (use_bit_range) {
        unsigned storage_bits = is_raw ? (unsigned)raw_bits :
                                (max_value <= 255 ? 8u : 16u);
        if (last_bit >= storage_bits) {
            fprintf(stderr,
                    "%s: bit range %u-%u is outside the %u-bit pixel data\n",
                    input_path, first_bit, last_bit, storage_bits);
            for (size_t channel = 0; channel < channel_count; channel++) {
                free(pixels[channel]);
            }
            return 1;
        }

        unsigned width = last_bit - first_bit + 1;// 0-5 -> 6
        uint32_t mask = ((uint32_t)1 << width) - 1;//00111111 fica so 111111 se for de 0-5 o bit-range
        for (size_t channel = 0; channel < channel_count; channel++) {
            for (size_t i = 0; i < pixel_count; i++) {
                pixels[channel][i] = (uint16_t)(
                    ((uint32_t)pixels[channel][i] >> first_bit) & mask);
            }
        }
        analyzed_max = mask;
    }

    size_t pixel_bin_count = analyzed_max + 1;
    size_t difference_bin_count = 2 * analyzed_max + 1;
    size_t *histograms[3] = {NULL, NULL, NULL};
    size_t *horizontal_histograms[3] = {NULL, NULL, NULL};
    size_t *vertical_histograms[3] = {NULL, NULL, NULL};
    int allocation_failed = 0;
    for (size_t channel = 0; channel < channel_count; channel++) {
        histograms[channel] = calloc(pixel_bin_count,
                                     sizeof(*histograms[channel]));
        horizontal_histograms[channel] = calloc(
            difference_bin_count, sizeof(*horizontal_histograms[channel]));
        vertical_histograms[channel] = calloc(
            difference_bin_count, sizeof(*vertical_histograms[channel]));
        if (histograms[channel] == NULL ||
            horizontal_histograms[channel] == NULL ||
            vertical_histograms[channel] == NULL) {
            allocation_failed = 1;
            break;
        }
    }
    if (allocation_failed) {
        fprintf(stderr, "%s: not enough memory for histograms\n", input_path);
        for (size_t channel = 0; channel < channel_count; channel++) {
            free(pixels[channel]);
            free(histograms[channel]);
            free(horizontal_histograms[channel]);
            free(vertical_histograms[channel]);
        }
        return 1;
    }

    size_t horizontal_pair_count = rows * (columns - 1);
    size_t vertical_pair_count = (rows - 1) * columns;
    static const char *channel_names[3] = {"Red", "Green", "Blue"};
    for (size_t channel = 0; channel < channel_count; channel++) {
        for (size_t i = 0; i < pixel_count; i++) {
            histograms[channel][pixels[channel][i]]++;
        }

        size_t histogram_total;
        if (!histogram_count_matches(histograms[channel], pixel_bin_count,
                                     pixel_count, &histogram_total)) {
            fprintf(stderr,
                    "%s: %s histogram contains %zu pixels, expected %zu\n",
                    input_path, is_ppm ? channel_names[channel] : "pixel",
                    histogram_total, pixel_count);
            for (size_t i = 0; i < channel_count; i++) {
                free(pixels[i]);
                free(histograms[i]);
                free(horizontal_histograms[i]);
                free(vertical_histograms[i]);
            }
            return 1;
        }
        count_horizontal_differences(pixels[channel], rows, columns,
                                     analyzed_max,
                                     horizontal_histograms[channel]);
        count_vertical_differences(pixels[channel], rows, columns,
                                   analyzed_max,
                                   vertical_histograms[channel]);
    }

    int svg_ok;
    if (is_ppm) {
        svg_ok = write_ppm_histogram_svgs(input_path, histograms,
                                          pixel_bin_count, use_bit_range,
                                          first_bit, last_bit);
    } else {
        svg_ok = write_histogram_svg(input_path, is_raw, histograms[0],
                                     pixel_bin_count, use_bit_range,
                                     first_bit, last_bit);
    }
    if (!svg_ok) {
        for (size_t channel = 0; channel < channel_count; channel++) {
            free(pixels[channel]);
            free(histograms[channel]);
            free(horizontal_histograms[channel]);
            free(vertical_histograms[channel]);
        }
        return 1;
    }

    printf("File: %s\n", input_path);
    if (is_raw) {
        if (raw_bits == 16) {
            printf("Format: RAW, 16-bit grayscale, %s-endian\n",
                   big_endian ? "big" : "little");
        } else {
            puts("Format: RAW, 8-bit grayscale");
        }
    } else if (is_ppm) {
        printf("Format: PPM P6, %zu-bit RGB\n",
               max_value <= 255 ? (size_t)8 : (size_t)16);
    } else {
        printf("Format: PGM P5, %zu-bit grayscale\n",
               max_value <= 255 ? (size_t)8 : (size_t)16);
    }
    printf("Dimensions: %zu columns x %zu rows\n", columns, rows);
    printf("Maxval: %zu\n", max_value);
    if (use_bit_range) {
        printf("Bit range: %u-%u\n", first_bit, last_bit);
    }
    printf("Pixels read: %zu\n", pixel_count);
    printf("Horizontal pairs: %zu\n", horizontal_pair_count);
    printf("Vertical pairs: %zu\n", vertical_pair_count);

    for (size_t channel = 0; channel < channel_count; channel++) {
        size_t histogram_total = 0;
        histogram_count_matches(histograms[channel], pixel_bin_count,
                                pixel_count, &histogram_total);
        if (is_ppm) {
            printf("\n%s channel histogram total: %zu\n",
                   channel_names[channel], histogram_total);
            printf("%s channel histogram (value: count):\n",
                   channel_names[channel]);
        } else {
            printf("Histogram total: %zu\n", histogram_total);
            puts("\nHistogram (value: count):");
        }
        for (size_t value = 0; value < pixel_bin_count; value++) {
            if (histograms[channel][value] != 0) {
                printf("%3zu: %zu\n", value, histograms[channel][value]);
            }
        }
        if (is_ppm) {
            printf("%s pixel entropy: %.6f bits/pixel\n",
                   channel_names[channel],
                   entropy_from_histogram(histograms[channel], pixel_bin_count,
                                          pixel_count));
            if (horizontal_pair_count == 0) {
                printf("%s horizontal difference entropy: n/a\n",
                       channel_names[channel]);
            } else {
                printf("%s horizontal difference entropy: %.6f bits/pair\n",
                       channel_names[channel],
                       entropy_from_histogram(horizontal_histograms[channel],
                                              difference_bin_count,
                                              horizontal_pair_count));
            }
            if (vertical_pair_count == 0) {
                printf("%s vertical difference entropy: n/a\n",
                       channel_names[channel]);
            } else {
                printf("%s vertical difference entropy: %.6f bits/pair\n",
                       channel_names[channel],
                       entropy_from_histogram(vertical_histograms[channel],
                                              difference_bin_count,
                                              vertical_pair_count));
            }
        } else {
            printf("\nPixel entropy: %.6f bits/pixel\n",
                   entropy_from_histogram(histograms[channel], pixel_bin_count,
                                          pixel_count));
            if (horizontal_pair_count == 0) {
                puts("Horizontal difference entropy: n/a (no neighbor pairs)");
            } else {
                printf("Horizontal difference entropy: %.6f bits/pair\n",
                       entropy_from_histogram(horizontal_histograms[channel],
                                              difference_bin_count,
                                              horizontal_pair_count));
            }
            if (vertical_pair_count == 0) {
                puts("Vertical difference entropy: n/a (no neighbor pairs)");
            } else {
                printf("Vertical difference entropy: %.6f bits/pair\n",
                       entropy_from_histogram(vertical_histograms[channel],
                                              difference_bin_count,
                                              vertical_pair_count));
            }
        }
    }

    for (size_t channel = 0; channel < channel_count; channel++) {
        free(pixels[channel]);
        free(histograms[channel]);
        free(horizontal_histograms[channel]);
        free(vertical_histograms[channel]);
    }
    return 0;
}
