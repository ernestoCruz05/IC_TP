#include "wav.h"

#include <limits.h>
#include <string.h>

static bool read_u16_le(FILE *file, uint16_t *value)
{
    uint8_t b[2];

    if (fread(b, 1, sizeof b, file) != sizeof b)
        return false;

    *value = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return true;
}

static bool read_u32_le(FILE *file, uint32_t *value)
{
    uint8_t b[4];

    if (fread(b, 1, sizeof b, file) != sizeof b)
        return false;

    *value = (uint32_t)b[0]
           | ((uint32_t)b[1] << 8)
           | ((uint32_t)b[2] << 16)
           | ((uint32_t)b[3] << 24);
    return true;
}

static bool validate_format(const WAV_INFO *info)
{
    uint32_t bytes_per_sample;
    uint32_t expected_block_align;
    uint64_t expected_byte_rate;

    if (info->audio_format != 1 || info->num_channels == 0
            || info->sample_rate == 0)
        return false;

    if (info->bits_per_sample != 8 && info->bits_per_sample != 16
            && info->bits_per_sample != 24 && info->bits_per_sample != 32)
        return false;

    bytes_per_sample = info->bits_per_sample / 8;
    expected_block_align = (uint32_t)info->num_channels * bytes_per_sample;
    expected_byte_rate = (uint64_t)info->sample_rate * expected_block_align;

    if (expected_block_align > UINT16_MAX
            || expected_byte_rate > UINT32_MAX
            || info->block_align != expected_block_align
            || info->byte_rate != expected_byte_rate)
        return false;

    return info->data_size % info->block_align == 0;
}

bool wav_parse(FILE *file, WAV_INFO *info)
{
    WAV_INFO parsed = {0};
    char id[4];
    uint32_t riff_size;
    bool found_format = false;
    bool found_data = false;
    long file_size;
    uint64_t riff_end;
    uint64_t offset;

    if (!file || !info)
        return false;

    if (fseek(file, 0, SEEK_END) != 0)
        return false;
    file_size = ftell(file);
    if (file_size < 12 || fseek(file, 0, SEEK_SET) != 0)
        return false;

    if (fread(id, 1, sizeof id, file) != sizeof id
            || memcmp(id, "RIFF", sizeof id) != 0
            || !read_u32_le(file, &riff_size)
            || fread(id, 1, sizeof id, file) != sizeof id
            || memcmp(id, "WAVE", sizeof id) != 0)
        return false;

    riff_end = UINT64_C(8) + riff_size;
    if (riff_size < 4 || riff_end > (uint64_t)file_size
            || riff_end > (uint64_t)LONG_MAX)
        return false;

    offset = 12;
    while (offset < riff_end) {
        uint32_t chunk_size;
        uint64_t payload_offset;
        uint64_t payload_end;
        uint64_t next_offset;

        if (riff_end - offset < 8 || fseek(file, (long)offset, SEEK_SET) != 0
                || fread(id, 1, sizeof id, file) != sizeof id
                || !read_u32_le(file, &chunk_size))
            return false;

        payload_offset = offset + 8;
        payload_end = payload_offset + chunk_size;
        if (payload_end > riff_end)
            return false;

        next_offset = payload_end;
        if ((chunk_size & 1u) && payload_end < riff_end)
            ++next_offset;
        if (next_offset > riff_end)
            return false;

        if (memcmp(id, "fmt ", sizeof id) == 0 && !found_format) {
            if (chunk_size < 16
                    || !read_u16_le(file, &parsed.audio_format)
                    || !read_u16_le(file, &parsed.num_channels)
                    || !read_u32_le(file, &parsed.sample_rate)
                    || !read_u32_le(file, &parsed.byte_rate)
                    || !read_u16_le(file, &parsed.block_align)
                    || !read_u16_le(file, &parsed.bits_per_sample))
                return false;
            found_format = true;
        } else if (memcmp(id, "data", sizeof id) == 0 && !found_data) {
            parsed.data_size = chunk_size;
            parsed.data_offset = (long)payload_offset;
            found_data = true;
        }

        offset = next_offset;
    }

    if (!found_format || !found_data || !validate_format(&parsed))
        return false;

    *info = parsed;
    return wav_seek_data(file, info);
}

bool wav_seek_data(FILE *file, const WAV_INFO *info)
{
    if (!file || !info || info->data_offset < 0)
        return false;

    return fseek(file, info->data_offset, SEEK_SET) == 0;
}

bool wav_read_sample(FILE *file, const WAV_INFO *info, int32_t *sample)
{
    uint8_t bytes[4];
    uint32_t raw = 0;
    unsigned byte_count;
    unsigned i;

    if (!file || !info || !sample)
        return false;

    byte_count = info->bits_per_sample / 8;
    if ((info->bits_per_sample != 8 && info->bits_per_sample != 16
                && info->bits_per_sample != 24
                && info->bits_per_sample != 32)
            || fread(bytes, 1, byte_count, file) != byte_count)
        return false;

    for (i = 0; i < byte_count; ++i)
        raw |= (uint32_t)bytes[i] << (8 * i);

    if (info->bits_per_sample == 8) {
        *sample = (int32_t)raw - 128;
    } else if (raw & (UINT32_C(1) << (info->bits_per_sample - 1))) {
        *sample = (int32_t)((int64_t)raw
                - (INT64_C(1) << info->bits_per_sample));
    } else {
        *sample = (int32_t)raw;
    }

    return true;
}

bool wav_sample_range(const WAV_INFO *info, int32_t *min_value,
                      int32_t *max_value)
{
    if (!info || !min_value || !max_value)
        return false;

    switch (info->bits_per_sample) {
    case 8:
        *min_value = -128;
        *max_value = 127;
        return true;
    case 16:
        *min_value = -32768;
        *max_value = 32767;
        return true;
    case 24:
        *min_value = -8388608;
        *max_value = 8388607;
        return true;
    case 32:
        *min_value = INT32_MIN;
        *max_value = INT32_MAX;
        return true;
    default:
        return false;
    }
}
