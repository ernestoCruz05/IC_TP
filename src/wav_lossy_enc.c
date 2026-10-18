#include "lossless/crc32.h"
#include "lossless/pcm.h"
#include "lossless/rice.h"
#include "lossy/dct.h"
#include "wav.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 24u
#define DEFAULT_N 256u
#define DEFAULT_G 64u
#define TRANSFORM_DCT2 1u

static void put_le(uint8_t *p, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
        p[i] = (uint8_t)(value >> (8u * i));
}

static int encode(FILE *in, FILE *out, const WAV_INFO *info, unsigned n,
                  unsigned g, unsigned trial_stride) {
    uint8_t header[HEADER_SIZE] = {0};
    uint64_t frames = info->data_size / info->block_align;
    uint64_t block_count = (frames + n - 1) / n;
    int32_t *pcm = NULL;
    double *samples = NULL, *coeff = NULL;
    uint64_t *mapped = NULL;
    uint8_t *payload = NULL;
    size_t payload_capacity = (size_t)n * 16u + 4096u;
    uint32_t crc = 0;
    int ok = 0;

    memcpy(header, "ICLY", 4); header[4] = 1;
    header[5] = (uint8_t)info->num_channels;
    header[6] = (uint8_t)info->bits_per_sample;
    header[7] = TRANSFORM_DCT2;
    put_le(header + 8, info->sample_rate, 4);
    put_le(header + 12, frames, 8);
    put_le(header + 20, n, 2);
    header[22] = (uint8_t)g;
    if (fwrite(header, 1, sizeof header, out) != sizeof header) goto done;

    pcm = calloc((size_t)n * info->num_channels, sizeof(*pcm));
    samples = calloc(n, sizeof(*samples));
    coeff = malloc(n * sizeof(*coeff));
    mapped = malloc(n * sizeof(*mapped));
    payload = malloc(payload_capacity);
    if (!pcm || !samples || !coeff || !mapped || !payload) goto done;

    double scale = ldexp(1.0, (int)info->bits_per_sample - 1);
    double step = exp2(-(double)g / 8.0);
    for (uint64_t block = 0; block < block_count; ++block) {
        size_t count;
        uint32_t block_size;
        BIT_WRITER writer;
        size_t input_length = n;
        if (trial_stride > 1 && block % trial_stride != 0) continue;
        memset(pcm, 0, input_length * info->num_channels * sizeof(*pcm));
        count = (size_t)((frames - block * n) < n ?
                         frames - block * n : n);
        if (fseek(in, info->data_offset + (long)(block * n * info->block_align),
                  SEEK_SET) != 0) goto done;
        for (size_t i = 0; i < count; ++i)
            for (unsigned c = 0; c < info->num_channels; ++c)
                if (!wav_read_sample(in, info,
                     &pcm[i * info->num_channels + c])) goto done;
        bit_writer_init(&writer, payload, payload_capacity);
        for (unsigned c = 0; c < info->num_channels; ++c) {
            RICE_PLAN plan;
            for (size_t i = 0; i < input_length; ++i)
                samples[i] = (double)pcm[i * info->num_channels + c] / scale;
            dct2_forward(samples, coeff, n);
            for (size_t i = 0; i < n; ++i) {
                double qd = nearbyint(coeff[i] / step);
                if (!isfinite(qd) || fabs(qd) >= 5.0e11) goto done;
                mapped[i] = rice_map((int64_t)qd);
            }
            rice_plan_choose(mapped, n, 0, 6, &plan);
            rice_write_residuals(&writer, mapped, n, 0, &plan);
        }
        if (!bit_writer_finish(&writer, &payload_capacity) ||
            payload_capacity > UINT32_MAX) goto done;
        block_size = (uint32_t)payload_capacity;
        uint8_t size_bytes[4]; put_le(size_bytes, block_size, 4);
        if (fwrite(size_bytes, 1, 4, out) != 4 ||
            fwrite(payload, 1, block_size, out) != block_size) goto done;
        crc = crc32_update(crc, size_bytes, 4);
        crc = crc32_update(crc, payload, block_size);
        payload_capacity = (size_t)n * 16u + 4096u;
    }
    uint8_t crc_bytes[4]; put_le(crc_bytes, crc, 4);
    if (fwrite(crc_bytes, 1, 4, out) != 4 || fflush(out) != 0) goto done;
    ok = 1;
done:
    free(payload); free(mapped); free(coeff); free(samples); free(pcm);
    return ok;
}

static long double estimate_full_size(long trial_size, uint64_t block_count,
                                     unsigned stride) {
    uint64_t sampled_blocks = (block_count + stride - 1) / stride;
    const long double fixed_size = HEADER_SIZE + 4.0L;
    if (trial_size < (long)fixed_size || sampled_blocks == 0) return INFINITY;
    return fixed_size + ((long double)trial_size - fixed_size) *
                        (long double)block_count / (long double)sampled_blocks;
}

static int parse_uint(const char *s, unsigned long low, unsigned long high,
                      unsigned long *out) {
    char *end;
    unsigned long value;
    errno = 0;
    value = strtoul(s, &end, 10);
    if (s[0] == '\0' || *end != '\0' || errno != 0 || value < low ||
        value > high)
        return 0;
    *out = value;
    return 1;
}

int main(int argc, char **argv) {
    unsigned long n = DEFAULT_N, g = DEFAULT_G, bitrate = 0;
    const char *input_path = NULL, *output_path = NULL;
    FILE *in = NULL, *out = NULL;
    WAV_INFO info;
    int q_seen = 0, b_seen = 0;
    int result = EXIT_FAILURE;
    int arg = 1;

    while (arg < argc && argv[arg][0] == '-') {
        unsigned long value;
        if (arg + 1 >= argc) goto usage;
        if (strcmp(argv[arg], "-n") == 0) {
            if (!parse_uint(argv[arg + 1], 256, 4096, &value) ||
                (value & (value - 1)) != 0) goto usage;
            n = value;
        } else if (strcmp(argv[arg], "-q") == 0) {
            if (!parse_uint(argv[arg + 1], 0, 240, &g)) goto usage;
            q_seen = 1;
        } else if (strcmp(argv[arg], "-b") == 0) {
            if (!parse_uint(argv[arg + 1], 8, 100000, &bitrate)) goto usage;
            b_seen = 1;
        } else {
            goto usage;
        }
        arg += 2;
    }
    if (q_seen && b_seen) goto usage;
    if (argc - arg != 2) goto usage;
    input_path = argv[arg]; output_path = argv[arg + 1];
    if (strcmp(input_path, output_path) == 0) goto usage;
    in = fopen(input_path, "rb");
    if (!in || !wav_parse(in, &info) || info.num_channels > 8 ||
        !wav_seek_data(in, &info)) {
        fprintf(stderr, "%s: unsupported or invalid PCM WAV\n", input_path);
        goto done;
    }
    if (b_seen && info.data_size == 0) {
        fprintf(stderr, "%s: cannot target a bitrate for an empty WAV\n",
                input_path);
        goto done;
    }
    out = fopen(output_path, "wb");
    if (!out) goto done;
    if (b_seen) {
        uint64_t frames = info.data_size / info.block_align;
        uint64_t blocks = (frames + n - 1) / n;
        unsigned stride = (unsigned)((blocks + 15) / 16);
        if (stride == 0) stride = 1;
        long double duration = (long double)frames / info.sample_rate;
        long double target_bytes = (long double)bitrate * 1000.0L * duration / 8.0L;
        unsigned low = 0, high = 240, best_g = 0;
        long double best_distance = INFINITY;
        /* Binary search the monotonic quality/size curve, then compare its
           two neighbours to account for whole-byte rounding. */
        for (unsigned attempt = 0; attempt < 9; ++attempt) {
            unsigned candidate = low + (high - low) / 2;
            FILE *trial = tmpfile();
            long trial_size;
            if (!trial || !encode(in, trial, &info, (unsigned)n, candidate,
                                  stride) ||
                (trial_size = ftell(trial)) < 0) {
                if (trial) fclose(trial);
                goto done;
            }
            fclose(trial);
            long double estimate = estimate_full_size(trial_size, blocks, stride);
            long double distance = fabsl(estimate - target_bytes);
            if (distance < best_distance) { best_distance = distance; best_g = candidate; }
            if (estimate > target_bytes) {
                if (candidate == 0) break;
                high = candidate - 1;
            } else {
                if (candidate == 240) break;
                low = candidate + 1;
            }
            if (low > high) break;
        }
        for (int offset = -1; offset <= 1; ++offset) {
            long candidate = (long)best_g + offset;
            if (candidate < 0 || candidate > 240) continue;
            FILE *trial = tmpfile();
            long trial_size;
            if (!trial || !encode(in, trial, &info, (unsigned)n,
                                  (unsigned)candidate, stride) ||
                (trial_size = ftell(trial)) < 0) {
                if (trial) fclose(trial);
                goto done;
            }
            fclose(trial);
            long double estimate = estimate_full_size(trial_size, blocks, stride);
            long double distance = fabsl(estimate - target_bytes);
            if (distance < best_distance) { best_distance = distance; best_g = (unsigned)candidate; }
        }
        /* Check the sampled estimate against a full encode. If sampling
           missed an atypical region, walk q one step at a time while the
           complete output gets closer to the requested size. */
        {
            FILE *trial = tmpfile();
            long actual_size;
            if (!trial || !encode(in, trial, &info, (unsigned)n, best_g, 1) ||
                (actual_size = ftell(trial)) < 0) {
                if (trial) fclose(trial);
                goto done;
            }
            fclose(trial);
            best_distance = fabsl((long double)actual_size - target_bytes);
            for (unsigned attempt = 0; attempt < 4; ++attempt) {
                long candidate = (long)best_g +
                                 ((long double)actual_size > target_bytes ? -1 : 1);
                long candidate_size;
                if (candidate < 0 || candidate > 240) break;
                trial = tmpfile();
                if (!trial || !encode(in, trial, &info, (unsigned)n,
                                      (unsigned)candidate, 1) ||
                    (candidate_size = ftell(trial)) < 0) {
                    if (trial) fclose(trial);
                    goto done;
                }
                fclose(trial);
                long double candidate_distance =
                    fabsl((long double)candidate_size - target_bytes);
                if (candidate_distance >= best_distance) break;
                best_g = (unsigned)candidate;
                actual_size = candidate_size;
                best_distance = candidate_distance;
            }
        }
        if (!encode(in, out, &info, (unsigned)n, best_g, 1)) goto done;
        long output_size = ftell(out);
        if (output_size < 0) goto done;
        long double actual_rate = (long double)output_size * 8.0L *
                                 info.sample_rate /
                                 ((long double)frames * 1000.0L);
        fprintf(stderr, "Target: %lu kbps; selected q=%u; achieved %.2Lf kbps\n",
                bitrate, best_g, actual_rate);
    } else if (!encode(in, out, &info, (unsigned)n, (unsigned)g, 1)) {
        goto done;
    }
    result = EXIT_SUCCESS;
    goto done;

usage:
    fprintf(stderr, "Usage: %s [-n 256..4096] ([-q 0..240] | [-b 8..100000 kbps]) input.wav compressed_file\n", argv[0]);
done:
    if (in) fclose(in);
    if (out && fclose(out) != 0) result = EXIT_FAILURE;
    if (result != EXIT_SUCCESS && output_path) remove(output_path);
    return result;
}
