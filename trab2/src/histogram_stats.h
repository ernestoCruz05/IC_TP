#ifndef HISTOGRAM_STATS_H
#define HISTOGRAM_STATS_H

#include <stddef.h>
#include <stdint.h>

double entropy_from_histogram(const size_t *histogram, size_t bin_count, size_t sample_count);
size_t count_horizontal_differences(const uint16_t *pixels, size_t rows, size_t columns, size_t difference_offset, size_t *histogram);
size_t count_vertical_differences(const uint16_t *pixels, size_t rows, size_t columns, size_t difference_offset, size_t *histogram);
int histogram_count_matches(const size_t *histogram, size_t bin_count, size_t expected_count, size_t *actual_count);

#endif
