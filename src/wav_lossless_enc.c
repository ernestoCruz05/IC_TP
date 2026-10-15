#include "lossless.h"
#include "lpc.h"
#include "rice.h"
#include "wav.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    LOSSLESS_OPTIONS codec;
    const char *input_path;
    const char *output_path;
} OPTIONS;

static void print_usage(FILE *stream, const char *program) {
    fprintf(stream,
            "Usage: %s [-b BLOCK_SIZE] [-l LPC_ORDER] [-q PRECISION] "
            "[-p PARTITION_ORDER] [-j THREADS] input.wav compressed_file\n",
            program);
}

static bool parse_unsigned(const char *text, unsigned long min,
                           unsigned long max, unsigned int *value) {
    char *end;
    unsigned long parsed;

    if (text[0] < '0' || text[0] > '9')
        return false;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (*end != '\0' || errno == ERANGE || parsed < min || parsed > max)
        return false;

    *value = (unsigned int)parsed;
    return true;
}

static bool parse_options(int argc, char **argv, OPTIONS *options) {
    int i = 1;

    *options = (OPTIONS){
        .codec = {.block_size = LOSSLESS_DEFAULT_BLOCK_SIZE,
                  .block = {.max_lpc_order = LOSSLESS_DEFAULT_LPC_ORDER,
                            .lpc_precision = LOSSLESS_DEFAULT_LPC_PRECISION,
                            .max_partition_order =
                                LOSSLESS_DEFAULT_PARTITION_ORDER},
                  .threads = lossless_default_threads()}};

    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        const char *flag = argv[i];
        unsigned int *value;
        unsigned long min;
        unsigned long max;

        if (strcmp(flag, "-b") == 0) {
            value = &options->codec.block_size;
            min = LOSSLESS_MIN_BLOCK_SIZE;
            max = LOSSLESS_MAX_BLOCK_SIZE;
        } else if (strcmp(flag, "-l") == 0) {
            value = &options->codec.block.max_lpc_order;
            min = 0;
            max = LPC_MAX_ORDER;
        } else if (strcmp(flag, "-q") == 0) {
            value = &options->codec.block.lpc_precision;
            min = LOSSLESS_MIN_LPC_PRECISION;
            max = LPC_MAX_PRECISION;
        } else if (strcmp(flag, "-p") == 0) {
            value = &options->codec.block.max_partition_order;
            min = 0;
            max = RICE_MAX_PARTITION_ORDER;
        } else if (strcmp(flag, "-j") == 0) {
            value = &options->codec.threads;
            min = 1;
            max = LOSSLESS_MAX_THREADS;
        } else {
            fprintf(stderr, "Unknown option: %s\n", flag);
            return false;
        }

        if (i + 1 >= argc) {
            fprintf(stderr, "Option %s needs a value\n", flag);
            return false;
        }
        if (!parse_unsigned(argv[i + 1], min, max, value)) {
            fprintf(stderr, "%s must be an integer between %lu and %lu\n",
                    flag, min, max);
            return false;
        }
        i += 2;
    }

    if (argc - i != 2)
        return false;

    options->input_path = argv[i];
    options->output_path = argv[i + 1];
    if (strcmp(options->input_path, options->output_path) == 0) {
        fprintf(stderr, "Input and output must be different\n");
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    OPTIONS options;
    LOSSLESS_SOURCE source = {0};
    WAV_INFO info;
    FILE *in = NULL;
    FILE *out = NULL;
    const char *error = NULL;
    long file_size;
    int result = EXIT_FAILURE;

    if (!parse_options(argc, argv, &options)) {
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    in = fopen(options.input_path, "rb");
    if (!in) {
        fprintf(stderr, "%s: %s\n", options.input_path, strerror(errno));
        goto cleanup;
    }

    if (fseek(in, 0, SEEK_END) != 0) {
        fprintf(stderr, "%s: cannot seek\n", options.input_path);
        goto cleanup;
    }
    file_size = ftell(in);
    if (file_size < 0) {
        fprintf(stderr, "%s: cannot determine the file size\n",
                options.input_path);
        goto cleanup;
    }
    source.file_size = (uint64_t)file_size;

    if (wav_parse(in, &info) && info.num_channels <= LOSSLESS_MAX_CHANNELS) {
        source.channels = info.num_channels;
        source.bits = info.bits_per_sample;
        source.prefix_size = (uint64_t)info.data_offset;
        source.pcm_size = info.data_size;
    } else {
        fprintf(stderr,
                "%s: not a supported PCM WAV file, storing it uncompressed\n",
                options.input_path);
    }

    out = fopen(options.output_path, "wb");
    if (!out) {
        fprintf(stderr, "%s: %s\n", options.output_path, strerror(errno));
        goto cleanup;
    }

    if (!lossless_encode(in, &source, &options.codec, out, &error)) {
        fprintf(stderr, "%s: %s\n", options.input_path, error);
        goto cleanup;
    }
    result = EXIT_SUCCESS;

cleanup:
    if (in)
        (void)fclose(in);
    if (out) {
        if (fclose(out) != 0 && result == EXIT_SUCCESS) {
            fprintf(stderr, "%s: %s\n", options.output_path, strerror(errno));
            result = EXIT_FAILURE;
        }
        if (result != EXIT_SUCCESS)
            (void)remove(options.output_path);
    }
    return result;
}
