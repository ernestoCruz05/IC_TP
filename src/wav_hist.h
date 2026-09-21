#ifndef WAV_HIST_H
#define WAV_HIST_H


#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct{
    int32_t min_val;
    int32_t max_val;
    uint32_t bin_width;
    size_t bin_count;
    uint64_t *bins;
} HIST;

typedef struct{
    char file_id[5];
    uint32_t file_size;
    char format[5];
    char subchunk_id[5];
    uint32_t subchunk_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_id[5];
    uint32_t data_size;
} WAVHEADER;

typedef struct {
    WAVHEADER header;
    uint8_t const* data;
    uint32_t data_length;
} WAVFILE;

int32_t calc_mid(int32_t left, int32_t right);
int32_t calc_diff(int32_t left, int32_t right);

size_t calc_bin_i(int32_t sample, int32_t min, uint32_t bin_width);
bool init(HIST *hist, int32_t min_value, int32_t max_value, uint32_t bin_width);
void hist_add(HIST *hist, int32_t sample);
void hist_free(HIST *hist);

#endif

