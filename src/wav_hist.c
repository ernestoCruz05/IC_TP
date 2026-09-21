#include "wav_hist.h"
#include "wav.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HISTOGRAM_BINS UINT64_C(16777216)

#define CHANNEL_L    (1u << 0)
#define CHANNEL_R    (1u << 1)
#define CHANNEL_MID  (1u << 2)
#define CHANNEL_DIFF (1u << 3)
#define CHANNEL_MONO (1u << 4)
#define CHANNEL_ALL  (1u << 5)

#define STEREO_CHANNELS (CHANNEL_L | CHANNEL_R | CHANNEL_MID | CHANNEL_DIFF)

typedef struct {
    const char *input_path;
    const char *output_path;
    uint32_t bin_power;
    unsigned channels;
    bool channels_given;
} OPTIONS;

typedef struct {
    HIST *mono;
    HIST *left;
    HIST *right;
    HIST *mid;
    HIST *diff;
} HIST_TARGETS;

int32_t calc_mid(int32_t left, int32_t right)
{
    return (int32_t)(((int64_t)left + right) / 2);
}

int32_t calc_diff(int32_t left, int32_t right)
{
    return (int32_t)(((int64_t)left - right) / 2);
}

size_t calc_bin(int32_t sample, int32_t min, uint64_t bin_width)
{
    if (bin_width == 0 || sample < min)
        return SIZE_MAX;

    return (size_t)(((uint64_t)((int64_t)sample - min)) / bin_width);
}

bool init(HIST *hist, int32_t min_value, int32_t max_value,
          uint64_t bin_width)
{
    uint64_t range;
    uint64_t bin_count;
    uint64_t *bins;

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

void hist_add(HIST *hist, int32_t sample)
{
    size_t bin;

    if (!hist || !hist->bins || sample < hist->min_val
            || sample > hist->max_val)
        return;

    bin = calc_bin(sample, hist->min_val, hist->bin_width);
    if (bin < hist->bin_count)
        hist->bins[bin]++;
}

void hist_free(HIST *hist)
{
    if (!hist)
        return;

    free(hist->bins);
    hist->bins = NULL;
    hist->bin_count = 0;
}

bool hist_print(FILE *stream, const HIST *histograms,
                const char *const labels[], size_t count)
{
    const size_t bar_width = 60;
    size_t h;

    if (!stream || !histograms || !labels || count == 0) {
        errno = EINVAL;
        return false;
    }

    for (h = 0; h < count; ++h) {
        const HIST *hist = &histograms[h];
        uint64_t maximum = 0;
        size_t first = 0;
        size_t last;
        size_t bin;

        if (!hist->bins || hist->bin_count == 0) {
            errno = EINVAL;
            return false;
        }

        while (first < hist->bin_count && hist->bins[first] == 0)
            ++first;

        fprintf(stream, "%s (bin width: %" PRIu64 ")\n",
                labels[h], hist->bin_width);
        if (first == hist->bin_count) {
            fputs("(empty)\n", stream);
        } else {
            last = hist->bin_count - 1;
            while (last > first && hist->bins[last] == 0)
                --last;

            for (bin = first; bin <= last; ++bin) {
                if (hist->bins[bin] > maximum)
                    maximum = hist->bins[bin];
            }

            for (bin = first; bin <= last; ++bin) {
                uint64_t value = hist->bins[bin];
                int64_t low = (int64_t)hist->min_val
                        + (int64_t)((uint64_t)bin * hist->bin_width);
                int64_t high = low + (int64_t)hist->bin_width - 1;
                size_t length = value == 0 ? 0
                        : (size_t)((long double)value * bar_width / maximum);
                size_t i;

                if (high > hist->max_val)
                    high = hist->max_val;
                if (value != 0 && length == 0)
                    length = 1;

                fprintf(stream, "bin %zu [%" PRId64 "..%" PRId64 "] | ",
                        bin - first + 1, low, high);
                for (i = 0; i < length; ++i)
                    fputc('#', stream);
                fprintf(stream, " %" PRIu64 "\n", value);
            }
        }

        if (h + 1 < count)
            fputc('\n', stream);
    }

    return !ferror(stream);
}

static bool same_word(const char *left, const char *right)
{
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

static bool add_channel(OPTIONS *options, const char *name)
{
    unsigned channel;

    if (same_word(name, "l") || same_word(name, "left"))
        channel = CHANNEL_L;
    else if (same_word(name, "r") || same_word(name, "right"))
        channel = CHANNEL_R;
    else if (same_word(name, "mid"))
        channel = CHANNEL_MID;
    else if (same_word(name, "diff") || same_word(name, "side"))
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

static bool parse_bin_power(const char *text, uint32_t *value)
{
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

static void print_usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage: %s [OPTIONS] INPUT.wav\n"
            "Print trimmed PCM WAV histograms in the terminal.\n\n"
            "  -c, --channel MODE     L, R, MID, DIFF, MONO, or ALL; may repeat\n"
            "  -k, --bin-power K      group 2^K sample values per bin (default: 0)\n"
            "  -o, --output FILE      write the histogram to a text file\n"
            "  -h, --help             show this help\n",
            program);
}

static int parse_options(int argc, char **argv, OPTIONS *options)
{
    int i;

    *options = (OPTIONS){0};
    for (i = 1; i < argc; ++i) {
        const char *argument = argv[i];
        const char *value = NULL;

        if (strcmp(argument, "-h") == 0 || strcmp(argument, "--help") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        }

        if (strcmp(argument, "-c") == 0
                || strcmp(argument, "--channel") == 0) {
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
        } else if (strcmp(argument, "-k") == 0
                || strcmp(argument, "--bin-power") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s requires a value\n", argument);
                return -1;
            }
            value = argv[i];
            if (!parse_bin_power(value, &options->bin_power)) {
                fprintf(stderr, "invalid bin power: %s\n", value);
                return -1;
            }
        } else if (strncmp(argument, "--bin-power=", 12) == 0) {
            value = argument + 12;
            if (!parse_bin_power(value, &options->bin_power)) {
                fprintf(stderr, "invalid bin power: %s\n", value);
                return -1;
            }
        } else if (strcmp(argument, "-o") == 0
                || strcmp(argument, "--output") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s requires a value\n", argument);
                return -1;
            }
            options->output_path = argv[i];
        } else if (strncmp(argument, "--output=", 9) == 0) {
            options->output_path = argument + 9;
            if (!options->output_path[0]) {
                fputs("output path cannot be empty\n", stderr);
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
    if (options->output_path
            && strcmp(options->input_path, options->output_path) == 0) {
        fputs("input and output paths must be different\n", stderr);
        return -1;
    }
    return 1;
}

static bool add_histogram(HIST histograms[], const char *labels[],
                          size_t *count, HIST **target, const char *label,
                          int32_t min_value, int32_t max_value,
                          uint64_t bin_width)
{
    if (!init(&histograms[*count], min_value, max_value, bin_width))
        return false;

    labels[*count] = label;
    *target = &histograms[*count];
    ++*count;
    return true;
}

static bool process_audio(FILE *file, const WAV_INFO *info,
                          const HIST_TARGETS *targets)
{
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

int main(int argc, char **argv)
{
    OPTIONS options;
    WAV_INFO info;
    HIST histograms[4] = {0};
    const char *labels[4] = {0};
    HIST_TARGETS targets = {0};
    FILE *input = NULL;
    FILE *output = NULL;
    int32_t min_value;
    int32_t max_value;
    uint64_t value_count;
    uint64_t bin_width;
    uint64_t bin_count;
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
    if (options.bin_power > info.bits_per_sample) {
        fprintf(stderr, "bin power must be between 0 and %" PRIu16
                " for this file\n", info.bits_per_sample);
        goto cleanup;
    }

    valid_channels = info.num_channels == 1 ? CHANNEL_MONO : STEREO_CHANNELS;
    selected = (!options.channels_given || (options.channels & CHANNEL_ALL))
            ? valid_channels : options.channels;
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
                "histogram would require %" PRIu64
                " bins; choose a larger K\n", bin_count);
        goto cleanup;
    }

    if ((selected & CHANNEL_MONO)
            && !add_histogram(histograms, labels, &histogram_count,
                &targets.mono, "Mono", min_value, max_value, bin_width)) {
        fputs("could not allocate the histogram\n", stderr);
        goto cleanup;
    }
    if ((selected & CHANNEL_L)
            && !add_histogram(histograms, labels, &histogram_count,
                &targets.left, "Left (L)", min_value, max_value,
                bin_width)) {
        fputs("could not allocate the left histogram\n", stderr);
        goto cleanup;
    }
    if ((selected & CHANNEL_R)
            && !add_histogram(histograms, labels, &histogram_count,
                &targets.right, "Right (R)", min_value, max_value,
                bin_width)) {
        fputs("could not allocate the right histogram\n", stderr);
        goto cleanup;
    }
    if ((selected & CHANNEL_MID)
            && !add_histogram(histograms, labels, &histogram_count,
                &targets.mid, "MID: (L + R) / 2", min_value, max_value,
                bin_width)) {
        fputs("could not allocate the MID histogram\n", stderr);
        goto cleanup;
    }
    if ((selected & CHANNEL_DIFF)
            && !add_histogram(histograms, labels, &histogram_count,
                &targets.diff, "DIFF: (L - R) / 2", min_value, max_value,
                bin_width)) {
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

    if (fclose(input) != 0) {
        input = NULL;
        fprintf(stderr, "%s: failed to close input file\n",
                options.input_path);
        goto cleanup;
    }
    input = NULL;

    if (options.output_path) {
        output = fopen(options.output_path, "w");
        if (!output) {
            fprintf(stderr, "%s: %s\n", options.output_path,
                    strerror(errno));
            goto cleanup;
        }
    } else {
        output = stdout;
    }

    if (!hist_print(output, histograms, labels, histogram_count)) {
        fputs("could not print histogram\n", stderr);
        goto cleanup;
    }

    if (output == stdout) {
        if (fflush(output) != 0) {
            fputs("could not flush histogram output\n", stderr);
            goto cleanup;
        }
    } else {
        int close_result = fclose(output);
        output = NULL;
        if (close_result != 0) {
            fprintf(stderr, "%s: could not finish writing output\n",
                    options.output_path);
            goto cleanup;
        }
    }

    result = EXIT_SUCCESS;

cleanup:
    if (input)
        fclose(input);
    if (output && output != stdout)
        fclose(output);
    for (i = 0; i < histogram_count; ++i)
        hist_free(&histograms[i]);
    return result;
}
