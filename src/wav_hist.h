#ifndef WAV_HIST_H
#define WAV_HIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t min_val;
    int32_t max_val;
    uint64_t bin_width;
    size_t bin_count;
    uint32_t *bins;
} HIST;

int32_t calc_mid(int32_t left, int32_t right);
int32_t calc_diff(int32_t left, int32_t right);

size_t calc_bin(int32_t sample, int32_t min, uint64_t bin_width);
bool init(HIST *hist, int32_t min_value, int32_t max_value, uint64_t bin_width);
void hist_add(HIST *hist, int32_t sample);
void hist_free(HIST *hist);

#endif
