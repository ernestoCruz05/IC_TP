#include "lossless.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    FILE *in = NULL;
    FILE *out = NULL;
    const char *error = NULL;
    int result = EXIT_FAILURE;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s compressed_file output.wav\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], argv[2]) == 0) {
        fprintf(stderr, "Input and output must be different\n");
        return EXIT_FAILURE;
    }

    in = fopen(argv[1], "rb");
    if (!in) {
        fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
        goto cleanup;
    }

    out = fopen(argv[2], "wb");
    if (!out) {
        fprintf(stderr, "%s: %s\n", argv[2], strerror(errno));
        goto cleanup;
    }

    if (!lossless_decode(in, out, &error)) {
        fprintf(stderr, "%s: %s\n", argv[1], error);
        goto cleanup;
    }
    result = EXIT_SUCCESS;

cleanup:
    if (in)
        (void)fclose(in);
    if (out) {
        if (fclose(out) != 0 && result == EXIT_SUCCESS) {
            fprintf(stderr, "%s: %s\n", argv[2], strerror(errno));
            result = EXIT_FAILURE;
        }
        if (result != EXIT_SUCCESS)
            (void)remove(argv[2]);
    }
    return result;
}
