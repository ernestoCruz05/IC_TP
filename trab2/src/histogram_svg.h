#ifndef HISTOGRAM_SVG_H
#define HISTOGRAM_SVG_H

#include <stddef.h>

int write_histogram_svg(const char *input_path, int is_raw, const size_t *histogram, size_t bin_count, int use_bit_range, unsigned first_bit, unsigned last_bit);
int write_ppm_histogram_svgs(const char *input_path, size_t *histograms[3], size_t bin_count, int use_bit_range, unsigned first_bit, unsigned last_bit);

#endif
