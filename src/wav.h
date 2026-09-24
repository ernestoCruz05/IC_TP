#ifndef WAV_H
#define WAV_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_size;
    long data_offset;
} WAV_INFO;

bool wav_parse(FILE *file, WAV_INFO *info);

bool wav_seek_data(FILE *file, const WAV_INFO *info);

bool wav_read_sample(FILE *file, const WAV_INFO *info, int32_t *sample);

bool wav_sample_range(const WAV_INFO *info, int32_t *min_value,
                      int32_t *max_value);

bool write_u16_le(FILE *file, uint16_t value);

bool write_u32_le(FILE *file, uint32_t value);

bool wav_write_header(FILE *file, const WAV_INFO *info);

bool wav_write_sample(FILE *file, const WAV_INFO *info, int32_t sample);

#endif
