#ifndef IMG_HIST_IO_H
#define IMG_HIST_IO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TOKEN_CAPACITY 64

void skip_comment(FILE *file, int ch);
int read_pnm_token(FILE *file, char token[TOKEN_CAPACITY]);
int parse_positive_integer(FILE *file, size_t *value);
int parse_positive_argument(const char *text, size_t *value);
int parse_bit_range(const char *text, unsigned *first, unsigned *last);
int read_pgm_pixels(FILE *file, uint16_t *pixels, size_t pixel_count, size_t max_value);
int read_ppm_pixels(FILE *file, uint16_t *red, uint16_t *green, uint16_t *blue, size_t pixel_count, size_t max_value);
int read_raw_pixels(FILE *file, uint16_t *pixels, size_t pixel_count, size_t bits, int big_endian);

#endif
