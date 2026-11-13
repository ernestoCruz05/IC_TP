#include "lossless.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(FILE *stream, const char *program) {
    fprintf(stream, "Usage: %s [-j THREADS] compressed_file output.wav\n",
            program);
}

static bool parse_threads(const char *text, unsigned int *threads) {
    char *end;
    unsigned long parsed;

    if (text[0] < '0' || text[0] > '9')
        return false;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (*end != '\0' || errno == ERANGE || parsed < 1 ||
        parsed > LOSSLESS_MAX_THREADS)
        return false;

    *threads = (unsigned int)parsed;
    return true;
}

int main(int argc, char **argv) {
    unsigned int threads = lossless_default_threads();
    const char *input_path;
    const char *output_path;
    FILE *in = NULL;
    FILE *out = NULL;
    const char *error = NULL;
    int result = EXIT_FAILURE;
    int i = 1;

    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (strcmp(argv[i], "-j") != 0) {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(stderr, argv[0]);
            return EXIT_FAILURE;
        }
        if (i + 1 >= argc || !parse_threads(argv[i + 1], &threads)) {
            fprintf(stderr, "-j must be an integer between 1 and %d\n",
                    LOSSLESS_MAX_THREADS);
            print_usage(stderr, argv[0]);
            return EXIT_FAILURE;
        }
        i += 2;
    }

    if (argc - i != 2) {
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }
    input_path = argv[i];
    output_path = argv[i + 1];
    if (strcmp(input_path, output_path) == 0) {
        fprintf(stderr, "Input and output must be different\n");
        return EXIT_FAILURE;
    }

    in = fopen(input_path, "rb");
    if (!in) {
        fprintf(stderr, "%s: %s\n", input_path, strerror(errno));
        goto cleanup;
    }

    out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "%s: %s\n", output_path, strerror(errno));
        goto cleanup;
    }

    if (!lossless_decode(in, out, threads, &error)) {
        fprintf(stderr, "%s: %s\n", input_path, error);
        goto cleanup;
    }
    result = EXIT_SUCCESS;

cleanup:
    if (in)
        (void)fclose(in);
    if (out) {
        if (fclose(out) != 0 && result == EXIT_SUCCESS) {
            fprintf(stderr, "%s: %s\n", output_path, strerror(errno));
            result = EXIT_FAILURE;
        }
        if (result != EXIT_SUCCESS)
            (void)remove(output_path);
    }
    return result;
}
