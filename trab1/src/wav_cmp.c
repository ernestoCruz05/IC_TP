#include "wav_cmp.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool cmp_calculate(FILE *file_orig, const WAV_INFO *info_orig,
                   FILE *file_test, const WAV_INFO *info_test,
                   CMP_REPORT *report) {
    uint16_t num_channels;
    uint64_t frames_orig;
    uint64_t frames_test;
    uint64_t frames;
    double scale;
    double *sum_err_sq = NULL;
    double *sum_sig_sq = NULL;
    double *max_err = NULL;
    double sum_avg_err_sq = 0.0;
    double sum_avg_sig_sq = 0.0;
    double max_avg_err = 0.0;

    if (!file_orig || !info_orig || !file_test || !info_test || !report)
        return false;

    if (info_orig->num_channels != info_test->num_channels) {
        fprintf(stderr,
                "Error: Channel count mismatch (original: %u, test: %u)\n",
                info_orig->num_channels, info_test->num_channels);
        return false;
    }

    num_channels = info_orig->num_channels;
    if (num_channels == 0)
        return false;

    frames_orig = info_orig->data_size / info_orig->block_align;
    frames_test = info_test->data_size / info_test->block_align;
    frames = frames_orig < frames_test ? frames_orig : frames_test;

    if (frames == 0) {
        fprintf(stderr, "Error: One or both files have 0 audio frames\n");
        return false;
    }

    if (frames_orig != frames_test) {
        fprintf(stderr,
                "Warning: Frame count mismatch (original: %" PRIu64
                ", test: %" PRIu64 "). Comparing first %" PRIu64 " frames.\n",
                frames_orig, frames_test, frames);
    }

    if (info_orig->sample_rate != info_test->sample_rate) {
        fprintf(stderr,
                "Warning: Sample rate mismatch (original: %u Hz, test: %u Hz)\n",
                info_orig->sample_rate, info_test->sample_rate);
    }

    if (info_orig->bits_per_sample == info_test->bits_per_sample) {
        scale = 1.0;
    } else {
        scale = pow(2.0, (double)info_orig->bits_per_sample -
                             (double)info_test->bits_per_sample);
    }

    if (!wav_seek_data(file_orig, info_orig) ||
        !wav_seek_data(file_test, info_test)) {
        fprintf(stderr, "Error: Failed to seek to audio data\n");
        return false;
    }

    report->channels = calloc(num_channels, sizeof(CMP_METRICS));
    sum_err_sq = calloc(num_channels, sizeof(double));
    sum_sig_sq = calloc(num_channels, sizeof(double));
    max_err = calloc(num_channels, sizeof(double));

    if (!report->channels || !sum_err_sq || !sum_sig_sq || !max_err) {
        free(report->channels);
        free(sum_err_sq);
        free(sum_sig_sq);
        free(max_err);
        return false;
    }

    report->num_channels = num_channels;
    report->frames_compared = frames;
    report->scale_factor = scale;

    for (uint64_t f = 0; f < frames; ++f) {
        double frame_sum_orig = 0.0;
        double frame_sum_test = 0.0;

        for (uint16_t c = 0; c < num_channels; ++c) {
            int32_t s_orig = 0;
            int32_t s_test = 0;

            if (!wav_read_sample(file_orig, info_orig, &s_orig) ||
                !wav_read_sample(file_test, info_test, &s_test)) {
                fprintf(stderr,
                        "Error: Reading sample failed at frame %" PRIu64
                        ", channel %u\n",
                        f, c);
                free(sum_err_sq);
                free(sum_sig_sq);
                free(max_err);
                cmp_report_free(report);
                return false;
            }

            double x = (double)s_orig;
            double y = (double)s_test * scale;
            double diff = x - y;
            double abs_diff = fabs(diff);

            sum_err_sq[c] += diff * diff;
            sum_sig_sq[c] += x * x;
            if (abs_diff > max_err[c])
                max_err[c] = abs_diff;

            frame_sum_orig += x;
            frame_sum_test += y;
        }

        double avg_x = frame_sum_orig / (double)num_channels;
        double avg_y = frame_sum_test / (double)num_channels;
        double avg_diff = avg_x - avg_y;
        double abs_avg_diff = fabs(avg_diff);

        sum_avg_err_sq += avg_diff * avg_diff;
        sum_avg_sig_sq += avg_x * avg_x;
        if (abs_avg_diff > max_avg_err)
            max_avg_err = abs_avg_diff;
    }

    for (uint16_t c = 0; c < num_channels; ++c) {
        CMP_METRICS *m = &report->channels[c];
        m->samples = frames;
        m->mse = sum_err_sq[c] / (double)frames;
        m->rmse = sqrt(m->mse);
        m->max_error = max_err[c];

        if (sum_err_sq[c] == 0.0)
            m->snr = INFINITY;
        else if (sum_sig_sq[c] == 0.0)
            m->snr = -INFINITY;
        else
            m->snr = 10.0 * log10(sum_sig_sq[c] / sum_err_sq[c]);
    }

    {
        CMP_METRICS *m = &report->average_signal;
        m->samples = frames;
        m->mse = sum_avg_err_sq / (double)frames;
        m->rmse = sqrt(m->mse);
        m->max_error = max_avg_err;

        if (sum_avg_err_sq == 0.0)
            m->snr = INFINITY;
        else if (sum_avg_sig_sq == 0.0)
            m->snr = -INFINITY;
        else
            m->snr = 10.0 * log10(sum_avg_sig_sq / sum_avg_err_sq);
    }

    free(sum_err_sq);
    free(sum_sig_sq);
    free(max_err);
    return true;
}

void cmp_report_free(CMP_REPORT *report) {
    if (!report)
        return;
    free(report->channels);
    report->channels = NULL;
    report->num_channels = 0;
}

static void print_snr(double snr) {
    if (isinf(snr)) {
        if (snr > 0.0)
            printf("%14s", "+inf dB (exact)");
        else
            printf("%14s", "-inf dB");
    } else {
        printf("%11.2f dB", snr);
    }
}

void cmp_report_print(const CMP_REPORT *report,
                      const WAV_INFO *info_orig, const char *path_orig,
                      const WAV_INFO *info_test, const char *path_test) {
    double duration = (double)report->frames_compared / (double)info_orig->sample_rate;

    printf("\nComparing audio files:\n");
    printf("  Original : %s\n", path_orig);
    printf("             %u Hz, %u-bit, %u ch, %" PRIu64 " frames (%.2f s)\n",
           info_orig->sample_rate, info_orig->bits_per_sample,
           info_orig->num_channels, (uint64_t)(info_orig->data_size / info_orig->block_align),
           duration);
    printf("  Test     : %s\n", path_test);
    printf("             %u Hz, %u-bit, %u ch, %" PRIu64 " frames\n",
           info_test->sample_rate, info_test->bits_per_sample,
           info_test->num_channels, (uint64_t)(info_test->data_size / info_test->block_align));

    if (report->scale_factor != 1.0) {
        printf("  Scale    : %.4f (adjusted for bit-depth alignment)\n",
               report->scale_factor);
    }

    printf("\n%-18s %15s %15s %18s %16s\n", "Channel", "MSE (L2)", "RMSE",
           "Max Error (Linf)", "SNR");
    printf("--------------------------------------------------------------------------------------\n");

    for (uint16_t c = 0; c < report->num_channels; ++c) {
        char name[32];
        if (report->num_channels == 1) {
            snprintf(name, sizeof(name), "Mono");
        } else if (report->num_channels == 2) {
            snprintf(name, sizeof(name), "Channel %u (%s)", c + 1,
                     c == 0 ? "Left" : "Right");
        } else {
            snprintf(name, sizeof(name), "Channel %u", c + 1);
        }

        const CMP_METRICS *m = &report->channels[c];
        printf("%-18s %15.4e %15.4f %18.4f ", name, m->mse, m->rmse,
               m->max_error);
        print_snr(m->snr);
        printf("\n");
    }

    if (report->num_channels > 1) {
        const CMP_METRICS *m = &report->average_signal;
        printf("--------------------------------------------------------------------------------------\n");
        printf("%-18s %15.4e %15.4f %18.4f ", "Average (MID)", m->mse, m->rmse,
               m->max_error);
        print_snr(m->snr);
        printf("\n");
    }
    printf("\n");
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [OPTIONS] original.wav test.wav\n"
            "Compare audio files and compute MSE (L2), Max Error (Linf), and SNR.\n\n"
            "Options:\n"
            "  -h, --help    Show this help message and exit\n",
            prog);
}

int main(int argc, char **argv) {
    const char *path_orig = NULL;
    const char *path_test = NULL;
    FILE *file_orig = NULL;
    FILE *file_test = NULL;
    WAV_INFO info_orig;
    WAV_INFO info_test;
    CMP_REPORT report = {0};
    int exit_code = EXIT_FAILURE;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        } else if (!path_orig) {
            path_orig = argv[i];
        } else if (!path_test) {
            path_test = argv[i];
        } else {
            fprintf(stderr, "Unexpected extra argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!path_orig || !path_test) {
        fprintf(stderr, "Error: Both original.wav and test.wav are required.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    file_orig = fopen(path_orig, "rb");
    if (!file_orig) {
        fprintf(stderr, "Error opening original file '%s': %s\n", path_orig,
                strerror(errno));
        goto cleanup;
    }

    if (!wav_parse(file_orig, &info_orig)) {
        fprintf(stderr, "Error: Failed to parse WAV header in '%s'\n", path_orig);
        goto cleanup;
    }

    file_test = fopen(path_test, "rb");
    if (!file_test) {
        fprintf(stderr, "Error opening test file '%s': %s\n", path_test,
                strerror(errno));
        goto cleanup;
    }

    if (!wav_parse(file_test, &info_test)) {
        fprintf(stderr, "Error: Failed to parse WAV header in '%s'\n", path_test);
        goto cleanup;
    }

    if (!cmp_calculate(file_orig, &info_orig, file_test, &info_test, &report)) {
        goto cleanup;
    }

    cmp_report_print(&report, &info_orig, path_orig, &info_test, path_test);
    exit_code = EXIT_SUCCESS;

cleanup:
    cmp_report_free(&report);
    if (file_orig)
        fclose(file_orig);
    if (file_test)
        fclose(file_test);

    return exit_code;
}
