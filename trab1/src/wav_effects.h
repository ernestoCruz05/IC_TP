#ifndef WAV_EFFECTS_H
#define WAV_EFFECTS_H

#include "wav.h"
#include <stdint.h>
#include <stdbool.h>

void effect_reverse(int32_t *samples, uint64_t frame_count, uint16_t num_channels);

void effect_echo(const int32_t *input, int32_t *output, uint64_t frame_count,
                 uint16_t num_channels, uint64_t delay_frames, double gain,
                 unsigned repeats, int32_t min_value, int32_t max_value);

void effect_amod(int32_t *samples, uint64_t frame_count, uint16_t num_channels,
                 double freq_hz, double depth, uint32_t sample_rate,
                 int32_t min_value, int32_t max_value);
#endif