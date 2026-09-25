#include "wav.h"
#include "wav_quant.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool supported_width(unsigned int bits) {
    return bits == 8 || bits == 16 || bits == 24 || bits == 32;
}

int32_t quant_sample(int32_t sample, unsigned int source_bits,
                     unsigned int target_bits) {
    int64_t step = INT64_C(1) << (source_bits - target_bits);
    int64_t quantized = (int64_t)sample / step;
    int64_t remainder = (int64_t)sample % step;
    int64_t half_step = step / 2;
    int64_t target_min = -(INT64_C(1) << (target_bits - 1));
    int64_t target_max = (INT64_C(1) << (target_bits - 1)) - 1;

    if (remainder >= half_step)
        ++quantized;
    else if (remainder <= -half_step)
        --quantized;

    if (quantized < target_min)
        quantized = target_min;
    else if (quantized > target_max)
        quantized = target_max;

    return (int32_t)quantized;
}

static bool make_output_info(const WAV_INFO *input, unsigned int target_bits,
                             WAV_INFO *output, uint64_t *frame_count) {
    uint64_t frames;
    uint64_t output_block_align;
    uint64_t output_byte_rate;
    uint64_t output_data_size;

    if (!input || !output || !frame_count || input->block_align == 0 ||
        input->data_size % input->block_align != 0)
        return false;

    frames = input->data_size / input->block_align;
    output_block_align = (uint64_t)input->num_channels * (target_bits / 8);
    output_byte_rate = (uint64_t)input->sample_rate * output_block_align;
    output_data_size = frames * output_block_align;

    if (output_block_align > UINT16_MAX || output_byte_rate > UINT32_MAX ||
        output_data_size > UINT32_MAX)
        return false;

    *output = *input;
    output->bits_per_sample = (uint16_t)target_bits;
    output->block_align = (uint16_t)output_block_align;
    output->byte_rate = (uint32_t)output_byte_rate;
    output->data_size = (uint32_t)output_data_size;
    *frame_count = frames;
    return true;
}

int main(int argc, char **argv) {
    WAV_INFO input_info;
    WAV_INFO output_info;
    uint64_t frame_count;
    uint64_t total_samples;
    unsigned long bits;
    char *end;
    const char *input_path;
    const char *output_path;
    FILE *in = NULL;
    FILE *out = NULL;
    int result = EXIT_FAILURE;

    if (argc != 5) {
        fprintf(stderr, "Usage: %s -b BITS input.wav output.wav\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "-b") != 0 && strcmp(argv[1], "--bits") != 0) {
        fprintf(stderr, "Expected -b or --bits\n");
        return EXIT_FAILURE;
    }

    errno = 0;
    bits = strtoul(argv[2], &end, 10);
    if (*argv[2] == '\0' || *end != '\0' || errno == ERANGE || bits > 32 ||
        !supported_width((unsigned int)bits)) {
        fprintf(stderr, "BITS must be one of 8, 16, 24, or 32\n");
        return EXIT_FAILURE;
    }

    input_path = argv[3];
    output_path = argv[4];
    if (strcmp(input_path, output_path) == 0) {
        fprintf(stderr, "Input and output must be different\n");
        return EXIT_FAILURE;
    }

    in = fopen(input_path, "rb");
    if (!in) {
        perror("Failed to open input file");
        goto cleanup;
    }

    if (!wav_parse(in, &input_info)) {
        fprintf(stderr, "Failed to parse WAV file: %s\n", input_path);
        goto cleanup;
    }

    if (bits >= input_info.bits_per_sample) {
        fprintf(stderr,
                "Target bits (%lu) must be less than the source bits (%u)\n",
                bits, (unsigned)input_info.bits_per_sample);
        goto cleanup;
    }

    if (!make_output_info(&input_info, (unsigned int)bits, &output_info,
                          &frame_count)) {
        fprintf(stderr, "Could not calculate output WAV metadata\n");
        goto cleanup;
    }

    if (!wav_seek_data(in, &input_info)) {
        fprintf(stderr, "Failed to seek data in input file\n");
        goto cleanup;
    }

    out = fopen(output_path, "wb");
    if (!out) {
        perror("Failed to open output file");
        goto cleanup;
    }

    if (!wav_write_header(out, &output_info)) {
        fprintf(stderr, "Failed to write WAV header\n");
        goto cleanup;
    }

    total_samples = frame_count * input_info.num_channels;
    for (uint64_t i = 0; i < total_samples; ++i) {
        int32_t sample;
        int32_t quantized;

        if (!wav_read_sample(in, &input_info, &sample)) {
            fprintf(stderr, "Failed to read sample %" PRIu64 "\n", i);
            goto cleanup;
        }

        quantized = quant_sample(sample, input_info.bits_per_sample,
                                 output_info.bits_per_sample);
        if (!wav_write_sample(out, &output_info, quantized)) {
            fprintf(stderr, "Failed to write sample %" PRIu64 "\n", i);
            goto cleanup;
        }
    }

    if ((output_info.data_size & 1u) && fputc(0, out) == EOF) {
        fprintf(stderr, "Failed to write RIFF padding byte\n");
        goto cleanup;
    }

    result = EXIT_SUCCESS;

cleanup:
    if (in && fclose(in) != 0) {
        perror("Failed to close input file");
        result = EXIT_FAILURE;
    }
    if (out && fclose(out) != 0) {
        perror("Failed to close output file");
        result = EXIT_FAILURE;
    }
    return result;
}
