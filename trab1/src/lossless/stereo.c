#include "stereo.h"

static int64_t floor_half(int64_t value) {
    return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

void stereo_split(const int64_t *left, const int64_t *right, size_t n,
                  int64_t *mid, int64_t *side) {
    for (size_t i = 0; i < n; ++i) {
        mid[i] = floor_half(left[i] + right[i]);
        side[i] = left[i] - right[i];
    }
}

void stereo_join(unsigned int mode, int64_t *first, int64_t *second,
                 size_t n) {
    switch (mode) {
    case STEREO_LS:
        for (size_t i = 0; i < n; ++i)
            second[i] = first[i] - second[i];
        break;
    case STEREO_SR:
        for (size_t i = 0; i < n; ++i)
            first[i] += second[i];
        break;
    case STEREO_MS:
        for (size_t i = 0; i < n; ++i) {
            int64_t side = second[i];
            int64_t sum = 2 * first[i] + (int64_t)((uint64_t)side & 1u);

            first[i] = (sum + side) / 2;
            second[i] = (sum - side) / 2;
        }
        break;
    default:
        break;
    }
}
