#include "wav.h"
#include "wav_quant.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int32_t quant_sample(int32_t sample, unsigned int source_bits,
                     unsigned int target_bits) {
    unsigned shift = source_bits - target_bits;
    int64_t min = -(INT64_C(1) << (source_bits - 1));
    int64_t step = INT64_C(1) << shift;
    int64_t index = ((int64_t)sample - min) / step;
    int64_t max_index = (INT64_C(1) << target_bits) - 1;

    if (index < 0)
        index = 0;
    else if (index > max_index)
        index = max_index;

    int64_t quantized = min + step / 2 + index * step;

    return (int32_t)quantized;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s -b BITS input.wav output.wav\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "-b") != 0 && strcmp(argv[1], "--bits") != 0) {
        fprintf(stderr, "Expected -b or --bits\n");
        return EXIT_FAILURE;
    }

    char *end;
    unsigned long bits = strtoul(argv[2], &end, 10);

    if (*argv[2] == '\0' || *end != '\0' || bits == 0 || bits > 32) {
        fprintf(stderr, "BITS must be an integer between 1 and 32\n");
        return EXIT_FAILURE;
    }

    const char *input_path = argv[3];
    const char *output_path = argv[4];

    if (strcmp(input_path, output_path) == 0) {
        fprintf(stderr, "Input and output must be different \n");
        return EXIT_FAILURE;
    }

    FILE *in = fopen(input_path, "rb");
    if (!in) {
        perror("Failed to open input file");
        return EXIT_FAILURE;
    }

    WAV_INFO info;
    if (!wav_parse(in, &info)) {
        fprintf(stderr, "Failed to parse WAV file: %s\n", input_path);
        fclose(in);
        return EXIT_FAILURE;
    }

    if (bits >= info.bits_per_sample) {
        fprintf(stderr,
                "Target bits (%lu) must be less than the source bits (%u)\n",
                bits, (unsigned)info.bits_per_sample);
        fclose(in);
        return EXIT_FAILURE;
    }

    FILE *out = fopen(output_path, "wb");
    if (!out) {
        perror("Failed to open output file");
        fclose(in);
        return EXIT_FAILURE;
    }
    if (!wav_seek_data(in, &info)) {
        fprintf(stderr, "Failed to seek data in input file \n");
        fclose(in);
        fclose(out);
        return EXIT_FAILURE;
    }

    if (!wav_write_header(out, &info)) {
        fprintf(stderr, "Failed to write WAV header \n");
        fclose(in);
        fclose(out);
        return EXIT_FAILURE;
    }

    uint32_t bytes_per_sample = info.bits_per_sample / 8;
    uint64_t total_samples = info.data_size / bytes_per_sample;

    for (uint64_t i = 0; i < total_samples; i++) {
        int32_t sample = 0;
        if (!wav_read_sample(in, &info, &sample)) {
            fprintf(stderr, "Failed to read sample %" PRIu64 "\n", i);
            fclose(in);
            fclose(out);
            return EXIT_FAILURE;
        }

        int32_t quantized =
            quant_sample(sample, info.bits_per_sample, (unsigned)bits);

        if (!wav_write_sample(out, &info, quantized)) {
            fprintf(stderr, "Failed to write sample %" PRIu64 "\n", i);
            fclose(in);
            fclose(out);
            return EXIT_FAILURE;
        }
    }

    if (info.data_size & 1)
        fputc(0, out);

    fclose(in);
    if (fclose(out) != 0) {
        perror("Failed to close output file");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
