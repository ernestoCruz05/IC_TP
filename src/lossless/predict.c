#include "predict.h"

static int64_t predict(const int64_t *x, size_t i, unsigned int order) {
    switch (order) {
    case 1:
        return x[i - 1];
    case 2:
        return 2 * x[i - 1] - x[i - 2];
    case 3:
        return 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3];
    case 4:
        return 4 * x[i - 1] - 6 * x[i - 2] + 4 * x[i - 3] - x[i - 4];
    default:
        return 0;
    }
}

void fixed_residuals(const int64_t *x, size_t n, unsigned int order,
                     int64_t *residuals) {
    for (size_t i = order; i < n; ++i)
        residuals[i - order] = x[i] - predict(x, i, order);
}

bool fixed_restore(int64_t *x, size_t n, unsigned int order,
                   const int64_t *residuals, unsigned int width) {
    int64_t max = (INT64_C(1) << (width - 1)) - 1;
    int64_t min = -max - 1;

    if (order > FIXED_MAX_ORDER || order > n)
        return false;

    for (size_t i = order; i < n; ++i) {
        int64_t value = residuals[i - order] + predict(x, i, order);

        if (value < min || value > max)
            return false;
        x[i] = value;
    }
    return true;
}
