#include "wav_hist.h"
#include "hist_plot.h"
#include "wav.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_HISTOGRAM_BINS UINT64_C(16777216)

#define CHANNEL_L (1u << 0)
#define CHANNEL_R (1u << 1)
#define CHANNEL_MID (1u << 2)
#define CHANNEL_DIFF (1u << 3)
#define CHANNEL_MONO (1u << 4)
#define CHANNEL_ALL (1u << 5)

#define STEREO_CHANNELS (CHANNEL_L | CHANNEL_R | CHANNEL_MID | CHANNEL_DIFF)

typedef struct {
    const char *input_path;
    const char *output_dir;
    uint32_t bin_power;
    unsigned channels;
    bool channels_given;
    bool bin_power_given;
    bool debug;
} OPTIONS;

typedef struct {
    HIST *mono;
    HIST *left;
    HIST *right;
    HIST *mid;
    HIST *diff;
} HIST_TARGETS;

int32_t calc_mid(int32_t left, int32_t right) {
    return (int32_t)(((int64_t)left + right) / 2);
}

int32_t calc_diff(int32_t left, int32_t right) {
    return (int32_t)(((int64_t)left - right) / 2);
}

size_t calc_bin(int32_t sample, int32_t min, uint64_t bin_width) {
    if (bin_width == 0 || sample < min)
        return SIZE_MAX;

    return (size_t)(((uint64_t)((int64_t)sample - min)) / bin_width);
}

bool init(HIST *hist, int32_t min_value, int32_t max_value,
          uint64_t bin_width) {
    uint64_t range;
    uint64_t bin_count;
    uint32_t *bins;

    if (!hist || bin_width == 0 || min_value > max_value)
        return false;

    range = (uint64_t)((int64_t)max_value - min_value) + 1;
    bin_count = (range - 1) / bin_width + 1;
    if (bin_count > SIZE_MAX || bin_count > SIZE_MAX / sizeof *bins)
        return false;

    bins = calloc((size_t)bin_count, sizeof *bins);
    if (!bins)
        return false;

    hist->min_val = min_value;
    hist->max_val = max_value;
    hist->bin_width = bin_width;
    hist->bin_count = (size_t)bin_count;
    hist->bins = bins;
    return true;
}

void hist_add(HIST *hist, int32_t sample) {
    size_t bin;

    if (!hist || !hist->bins || sample < hist->min_val ||
        sample > hist->max_val)
        return;

    bin = calc_bin(sample, hist->min_val, hist->bin_width);
    if (bin < hist->bin_count)
        hist->bins[bin]++;
}

void hist_free(HIST *hist) {
    if (!hist)
        return;

    free(hist->bins);
    hist->bins = NULL;
    hist->bin_count = 0;
}

static uint64_t hist_total(const HIST *hist) {
    uint64_t total = 0;
    size_t i;

    for (i = 0; i < hist->bin_count; ++i)
        total += hist->bins[i];
    return total;
}

static bool print_hist_total(const char *name, const HIST *hist,
                             uint64_t frame_count) {
    uint64_t total;

    if (!hist)
        return true;

    total = hist_total(hist);
    fprintf(stderr, "%s: %" PRIu64 "%s\n", name, total,
            total == frame_count ? "" : " MISMATCH");
    return total == frame_count;
}

static bool same_word(const char *left, const char *right) {
    while (*left && *right) {
        char a = *left;
        char b = *right;

        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static bool add_channel(OPTIONS *options, const char *name) {
    unsigned channel;

    if (same_word(name, "l") || same_word(name, "left"))
        channel = CHANNEL_L;
    else if (same_word(name, "r") || same_word(name, "right"))
        channel = CHANNEL_R;
    else if (same_word(name, "mid"))
        channel = CHANNEL_MID;
    else if (same_word(name, "diff"))
        channel = CHANNEL_DIFF;
    else if (same_word(name, "mono"))
        channel = CHANNEL_MONO;
    else if (same_word(name, "all"))
        channel = CHANNEL_ALL;
    else
        return false;

    options->channels |= channel;
    options->channels_given = true;
    return true;
}

static bool parse_bin_power(const char *text, uint32_t *value) {
    char *end;
    unsigned long parsed;

    if (!text[0] || text[0] == '-' || text[0] == '+')
        return false;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno || *end != '\0' || parsed > 32)
        return false;

    *value = (uint32_t)parsed;
    return true;
}

static void print_usage(FILE *stream, const char *program) {
    fprintf(
        stream,
        "Usage: %s [OPTIONS] INPUT.wav\n"
        "Compute PCM WAV histograms and plot them as SVG images.\n\n"
        "  -c, --channel MODE     L, R, MID, DIFF, MONO, or ALL; may repeat\n"
        "  -k, --bin-power K      group 2^K values per bin (default: auto)\n"
        "  -o, --output-dir DIR   output directory (default: out)\n"
        "      --debug            print and verify histogram totals\n"
        "  -h, --help             show this help\n",
        program);
}

static int parse_options(int argc, char **argv, OPTIONS *options) {
    int i;

    *options = (OPTIONS){.output_dir = "out"};
    for (i = 1; i < argc; ++i) {
        const char *argument = argv[i];
        const char *value = NULL;

        if (strcmp(argument, "-h") == 0 || strcmp(argument, "--help") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        }

        if (strcmp(argument, "--debug") == 0) {
            options->debug = true;
        } else if (strcmp(argument, "-c") == 0 ||
                   strcmp(argument, "--channel") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s requires a value\n", argument);
                return -1;
            }
            value = argv[i];
            if (!add_channel(options, value)) {
                fprintf(stderr, "unknown channel mode: %s\n", value);
                return -1;
            }
        } else if (strncmp(argument, "--channel=", 10) == 0) {
            value = argument + 10;
            if (!add_channel(options, value)) {
                fprintf(stderr, "unknown channel mode: %s\n", value);
                return -1;
            }
        } else if (strcmp(argument, "-k") == 0 ||
                   strcmp(argument, "--bin-power") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s requires a value\n", argument);
                return -1;
            }
            value = argv[i];
            if (!parse_bin_power(value, &options->bin_power)) {
                fprintf(stderr, "invalid bin power: %s\n", value);
                return -1;
            }
            options->bin_power_given = true;
        } else if (strncmp(argument, "--bin-power=", 12) == 0) {
            value = argument + 12;
            if (!parse_bin_power(value, &options->bin_power)) {
                fprintf(stderr, "invalid bin power: %s\n", value);
                return -1;
            }
            options->bin_power_given = true;
        } else if (strcmp(argument, "-o") == 0 ||
                   strcmp(argument, "--output-dir") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s requires a value\n", argument);
                return -1;
            }
            options->output_dir = argv[i];
        } else if (strncmp(argument, "--output-dir=", 13) == 0) {
            options->output_dir = argument + 13;
            if (!options->output_dir[0]) {
                fputs("output directory cannot be empty\n", stderr);
                return -1;
            }
        } else if (argument[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argument);
            return -1;
        } else if (options->input_path) {
            fprintf(stderr, "unexpected argument: %s\n", argument);
            return -1;
        } else {
            options->input_path = argument;
        }
    }

    if (!options->input_path) {
        fputs("an input WAV file is required\n", stderr);
        return -1;
    }
    if (!options->output_dir[0]) {
        fputs("output directory cannot be empty\n", stderr);
        return -1;
    }
    return 1;
}

static bool add_histogram(HIST histograms[], const char *labels[],
                          size_t *count, HIST **target, const char *label,
                          int32_t min_value, int32_t max_value,
                          uint64_t bin_width) {
    if (!init(&histograms[*count], min_value, max_value, bin_width))
        return false;

    labels[*count] = label;
    *target = &histograms[*count];
    ++*count;
    return true;
}

static bool ensure_output_directory(const char *path) {
    struct stat info;

    if (stat(path, &info) == 0) {
        if (S_ISDIR(info.st_mode))
            return true;
        errno = ENOTDIR;
        return false;
    }

    if (errno != ENOENT)
        return false;
    return mkdir(path, 0775) == 0;
}

static char *make_output_path(const char *directory, const char *input_path,
                              const char *suffix) {
    const char *base = strrchr(input_path, '/');
    const char *dot;
    size_t directory_length = strlen(directory);
    size_t base_length;
    size_t suffix_length = strlen(suffix);
    bool separator =
        directory_length > 0 && directory[directory_length - 1] != '/';
    char *path;
    size_t position = 0;

    base = base ? base + 1 : input_path;
    dot = strrchr(base, '.');
    base_length = dot && dot != base ? (size_t)(dot - base) : strlen(base);

    path =
        malloc(directory_length + separator + base_length + suffix_length + 6);
    if (!path)
        return NULL;

    memcpy(path + position, directory, directory_length);
    position += directory_length;
    if (separator)
        path[position++] = '/';
    memcpy(path + position, base, base_length);
    position += base_length;
    path[position++] = '_';
    memcpy(path + position, suffix, suffix_length);
    position += suffix_length;
    memcpy(path + position, ".svg", 5);
    return path;
}

static bool process_audio(FILE *file, const WAV_INFO *info,
                          const HIST_TARGETS *targets) {
    uint64_t frame_count = info->data_size / info->block_align;
    uint64_t frame;

    for (frame = 0; frame < frame_count; ++frame) {
        int32_t left;
        int32_t right;

        if (!wav_read_sample(file, info, &left))
            return false;

        if (info->num_channels == 1) {
            hist_add(targets->mono, left);
            continue;
        }

        if (!wav_read_sample(file, info, &right))
            return false;

        hist_add(targets->left, left);
        hist_add(targets->right, right);
        if (targets->mid)
            hist_add(targets->mid, calc_mid(left, right));
        if (targets->diff)
            hist_add(targets->diff, calc_diff(left, right));
    }

    return true;
}

int main(int argc, char **argv) {
    OPTIONS options;
    WAV_INFO info;
    HIST histograms[4] = {0};
    const char *labels[4] = {0};
    HIST_TARGETS targets = {0};
    FILE *input = NULL;
    int32_t min_value;
    int32_t max_value;
    uint64_t value_count;
    uint64_t bin_width;
    uint64_t bin_count;
    uint64_t frame_count;
    unsigned selected;
    unsigned valid_channels;
    size_t histogram_count = 0;
    size_t i;
    int parse_result;
    int result = EXIT_FAILURE;

    parse_result = parse_options(argc, argv, &options);
    if (parse_result <= 0) {
        if (parse_result < 0)
            print_usage(stderr, argv[0]);
        return parse_result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    input = fopen(options.input_path, "rb");
    if (!input) {
        fprintf(stderr, "%s: %s\n", options.input_path, strerror(errno));
        goto cleanup;
    }

    if (!wav_parse(input, &info)) {
        fprintf(stderr, "%s: corrupt or unsupported PCM WAV file\n",
                options.input_path);
        goto cleanup;
    }
    if (info.num_channels != 1 && info.num_channels != 2) {
        fprintf(stderr, "%s: only mono and stereo WAV files are supported\n",
                options.input_path);
        goto cleanup;
    }
    if (!wav_sample_range(&info, &min_value, &max_value)) {
        fputs("unsupported PCM sample depth\n", stderr);
        goto cleanup;
    }
    if (!options.bin_power_given)
        options.bin_power =
            info.bits_per_sample > 8 ? info.bits_per_sample - 8 : 0;
    if (options.bin_power > info.bits_per_sample) {
        fprintf(stderr,
                "bin power must be between 0 and %" PRIu16 " for this file\n",
                info.bits_per_sample);
        goto cleanup;
    }

    valid_channels = info.num_channels == 1 ? CHANNEL_MONO : STEREO_CHANNELS;
    selected = (!options.channels_given || (options.channels & CHANNEL_ALL))
                   ? valid_channels
                   : options.channels;
    if (options.channels & ~(valid_channels | CHANNEL_ALL)) {
        fprintf(stderr, "selected channel mode is not valid for a %s file\n",
                info.num_channels == 1 ? "mono" : "stereo");
        goto cleanup;
    }

    bin_width = UINT64_C(1) << options.bin_power;
    value_count = (uint64_t)((int64_t)max_value - min_value) + 1;
    bin_count = (value_count - 1) / bin_width + 1;
    if (bin_count > MAX_HISTOGRAM_BINS) {
        fprintf(stderr,
                "histogram would require %" PRIu64 " bins; choose a larger K\n",
                bin_count);
        goto cleanup;
    }

    if ((selected & CHANNEL_MONO) &&
        !add_histogram(histograms, labels, &histogram_count, &targets.mono,
                       "Mono", min_value, max_value, bin_width)) {
        fputs("could not allocate the histogram\n", stderr);
        goto cleanup;
    }
    if ((selected & CHANNEL_L) &&
        !add_histogram(histograms, labels, &histogram_count, &targets.left,
                       "Left (L)", min_value, max_value, bin_width)) {
        fputs("could not allocate the left histogram\n", stderr);
        goto cleanup;
    }
    if ((selected & CHANNEL_R) &&
        !add_histogram(histograms, labels, &histogram_count, &targets.right,
                       "Right (R)", min_value, max_value, bin_width)) {
        fputs("could not allocate the right histogram\n", stderr);
        goto cleanup;
    }
    if ((selected & CHANNEL_MID) &&
        !add_histogram(histograms, labels, &histogram_count, &targets.mid,
                       "MID: (L + R) / 2", min_value, max_value, bin_width)) {
        fputs("could not allocate the MID histogram\n", stderr);
        goto cleanup;
    }
    if ((selected & CHANNEL_DIFF) &&
        !add_histogram(histograms, labels, &histogram_count, &targets.diff,
                       "DIFF: (L - R) / 2", min_value, max_value, bin_width)) {
        fputs("could not allocate the DIFF histogram\n", stderr);
        goto cleanup;
    }

    if (!wav_seek_data(input, &info)) {
        fprintf(stderr, "%s: could not seek to audio data\n",
                options.input_path);
        goto cleanup;
    }
    if (!process_audio(input, &info, &targets)) {
        fprintf(stderr, "%s: failed while reading audio samples\n",
                options.input_path);
        goto cleanup;
    }

    frame_count = info.data_size / info.block_align;
    if (options.debug) {
        bool totals_match;

        fprintf(stderr, "frames: %" PRIu64 "\n", frame_count);
        totals_match = print_hist_total("MONO", targets.mono, frame_count);
        totals_match &= print_hist_total("L", targets.left, frame_count);
        totals_match &= print_hist_total("R", targets.right, frame_count);
        totals_match &= print_hist_total("MID", targets.mid, frame_count);
        totals_match &= print_hist_total("DIFF", targets.diff, frame_count);
        if (!totals_match)
            goto cleanup;
    }

    if (fclose(input) != 0) {
        input = NULL;
        fprintf(stderr, "%s: failed to close input file\n", options.input_path);
        goto cleanup;
    }
    input = NULL;

    if (!ensure_output_directory(options.output_dir)) {
        fprintf(stderr, "%s: could not create output directory: %s\n",
                options.output_dir, strerror(errno));
        goto cleanup;
    }

    {
        char *path = make_output_path(options.output_dir, options.input_path,
                                      "histograms");

        if (!path) {
            fputs("could not allocate output path\n", stderr);
            goto cleanup;
        }
        if (!hist_plot_svg(path, histograms, labels, histogram_count,
                           options.input_path)) {
            fprintf(stderr, "%s: could not write SVG: %s\n", path,
                    strerror(errno));
            free(path);
            goto cleanup;
        }
        printf("Wrote %s\n", path);
        free(path);
    }

    result = EXIT_SUCCESS;

cleanup:
    if (input)
        fclose(input);
    for (i = 0; i < histogram_count; ++i)
        hist_free(&histograms[i]);
    return result;
}
