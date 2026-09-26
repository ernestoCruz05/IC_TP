#include "wav.h"
#include "wav_effects.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//para nao deixar que o resultado apos um efeito de audio nao ultrapasse o intervalo válido de amostras
static int32_t clamp_sample(double value, int32_t min_value, int32_t max_value) {
    if (value < (double)min_value)
        return min_value;
    if (value > (double)max_value)
        return max_value;
    return (int32_t)lround(value);
}


void effect_reverse(int32_t *samples, uint64_t frame_count, uint16_t num_channels) {
    uint64_t i = 0;
    uint64_t j = frame_count - 1; //ult frame

    if (frame_count == 0)
        return;

    //troca o 1 frame c o ultimo, 2 com penultimo,... 
    while (i < j) {
        for (uint16_t c = 0; c < num_channels; ++c) {
            int32_t tmp = samples[i * num_channels + c];
            samples[i * num_channels + c] = samples[j * num_channels + c];
            samples[j * num_channels + c] = tmp;
        }
        ++i;
        --j;
    }
}

//eco simples e múltiplos ecos
void effect_echo(const int32_t *input, int32_t *output, uint64_t frame_count,
                 uint16_t num_channels, uint64_t delay_frames, double gain,
                 unsigned repeats, int32_t min_value, int32_t max_value) {
    uint64_t frame;
    uint16_t c;

    //output fica c uma copia do input
    memcpy(output, input, frame_count * num_channels * sizeof *output);

    for (frame = 0; frame < frame_count; ++frame) {
        for (c = 0; c < num_channels; ++c) {
            double value = (double)output[frame * num_channels + c];
            double echo_gain = gain;
            uint64_t echo_delay = delay_frames;
            unsigned r;
            //repeats = 1, eco simples
            for (r = 0; r < repeats; ++r) {
                if (frame >= echo_delay) {
                    uint64_t source_frame = frame - echo_delay;
                    value += echo_gain *
                            (double)input[source_frame * num_channels + c];
                }
                echo_gain *= gain;
                echo_delay += delay_frames;
            }

            output[frame * num_channels + c] =
                clamp_sample(value, min_value, max_value); //garantir q nao é ultrapassado o limite permitido
        }
    }
}


void effect_amod(int32_t *samples, uint64_t frame_count, uint16_t num_channels,
                 double freq_hz, double depth, uint32_t sample_rate,
                 int32_t min_value, int32_t max_value) {
    uint64_t frame;
    uint16_t c;
    //cada amostra é transformada isoladamente com base na sua posição no tempo

    for (frame = 0; frame < frame_count; ++frame) {
        double t = (double)frame / (double)sample_rate; //tempo real do frame
        double factor = 1.0 + depth * sin(2.0 * M_PI * freq_hz * t); //sin = 0 normal

        for (c = 0; c < num_channels; ++c) {
            double value = (double)samples[frame * num_channels + c] * factor;
            samples[frame * num_channels + c] =
                clamp_sample(value, min_value, max_value);
        }
    }
}



static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s -e reverse input.wav output.wav\n"
            "  %s -e echo -d SECONDS -g GAIN [-r REPEATS] input.wav output.wav\n"
            "  %s -e amod -f HZ -p DEPTH input.wav output.wav\n",
            prog, prog, prog);
}

int main(int argc, char **argv) {
    const char *effect_name = NULL;
    const char *input_path = NULL;
    const char *output_path = NULL;
    //vals padrao se nao forem especificados vals
    double delay_seconds = 0.3;
    double gain = 0.5;
    unsigned repeats = 1;
    double freq_hz = 5.0;
    double depth = 0.5;

    WAV_INFO info;
    FILE *in = NULL;
    FILE *out = NULL;
    int32_t *samples = NULL;
    int32_t *output_samples = NULL; //aud final
    int32_t min_value, max_value;
    uint64_t frame_count;
    uint64_t total_samples;
    int result = EXIT_FAILURE;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            effect_name = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            delay_seconds = atof(argv[++i]);
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            gain = atof(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            repeats = (unsigned)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            freq_hz = atof(argv[++i]);
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            depth = atof(argv[++i]);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        } else if (!input_path) {
            input_path = argv[i];
        } else if (!output_path) {
            output_path = argv[i];
        } else {
            fprintf(stderr, "Unexpected extra argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!effect_name || !input_path || !output_path) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    in = fopen(input_path, "rb");
    if (!in) {
        perror("Failed to open input file");
        goto cleanup;
    }

    if (!wav_parse(in, &info)) {
        fprintf(stderr, "Failed to parse WAV file: %s\n", input_path);
        goto cleanup;
    }

    if (!wav_sample_range(&info, &min_value, &max_value)) {
        fputs("Unsupported PCM sample depth\n", stderr);
        goto cleanup;
    }

    if (info.block_align == 0 || info.data_size % info.block_align != 0) {
        fputs("Invalid audio data size\n", stderr);
        goto cleanup;
    }

    frame_count = info.data_size / info.block_align;
    total_samples = frame_count * info.num_channels;

    if (!wav_seek_data(in, &info)) {
        fprintf(stderr, "Failed to seek to audio data\n");
        goto cleanup;
    }

    
    samples = malloc(total_samples * sizeof *samples);
    if (!samples) {
        fputs("Out of memory\n", stderr);
        goto cleanup;
    }

    for (uint64_t i = 0; i < total_samples; ++i) {
        if (!wav_read_sample(in, &info, &samples[i])) {
            fprintf(stderr, "Failed to read sample %" PRIu64 "\n", i);
            goto cleanup;
        }
    }

    if (strcmp(effect_name, "reverse") == 0) {
        effect_reverse(samples, frame_count, info.num_channels);
        output_samples = samples;
    } else if (strcmp(effect_name, "echo") == 0) {
        uint64_t delay_frames = (uint64_t)(delay_seconds * info.sample_rate);

        if (delay_frames == 0) {
            fputs("Delay must be greater than 0\n", stderr);
            goto cleanup;
        }

        output_samples = malloc(total_samples * sizeof *output_samples);
        if (!output_samples) {
            fputs("Out of memory\n", stderr);
            goto cleanup;
        }
        effect_echo(samples, output_samples, frame_count, info.num_channels,
                   delay_frames, gain, repeats, min_value, max_value);
    } else if (strcmp(effect_name, "amod") == 0) {
        effect_amod(samples, frame_count, info.num_channels, freq_hz, depth,
                   info.sample_rate, min_value, max_value);
        output_samples = samples;
    } else {
        fprintf(stderr, "Unknown effect: %s\n", effect_name);
        print_usage(argv[0]);
        goto cleanup;
    }

    out = fopen(output_path, "wb");
    if (!out) {
        perror("Failed to open output file");
        goto cleanup;
    }

    if (!wav_write_header(out, &info)) {
        fprintf(stderr, "Failed to write WAV header\n");
        goto cleanup;
    }

    for (uint64_t i = 0; i < total_samples; ++i) {
        if (!wav_write_sample(out, &info, output_samples[i])) {
            fprintf(stderr, "Failed to write sample %" PRIu64 "\n", i);
            goto cleanup;
        }
    }

    if ((info.data_size & 1u) && fputc(0, out) == EOF) {
        fprintf(stderr, "Failed to write RIFF padding byte\n");
        goto cleanup;
    }

    result = EXIT_SUCCESS;

cleanup:
    if (output_samples && output_samples != samples)
        free(output_samples);
    free(samples);
    if (in)
        fclose(in);
    if (out)
        fclose(out);
    return result;
}