#include "lpc.h"

#include <math.h>

void lpc_window_tukey(double *window, size_t n, double alpha) {
    double width;
    double pi = acos(-1.0);

    if (n == 0)
        return;

    width = alpha * (double)(n - 1) / 2.0;
    for (size_t i = 0; i < n; ++i) {
        double edge = (double)(i < n - 1 - i ? i : n - 1 - i);

        window[i] = edge < width ? 0.5 * (1.0 - cos(pi * edge / width)) : 1.0;
    }
}

void lpc_apply_window(const int64_t *x, const double *window, size_t n,
                      double *data) {
    for (size_t i = 0; i < n; ++i)
        data[i] = (double)x[i] * window[i];
}

void lpc_autocorrelation(const double *data, size_t n, unsigned int max_order,
                         double *autoc) {
    size_t start = max_order < n ? max_order : n;

    for (unsigned int lag = 0; lag <= max_order; ++lag)
        autoc[lag] = 0.0;

    for (size_t i = 0; i < start; ++i) {
        for (size_t lag = 0; lag <= i; ++lag)
            autoc[lag] += data[i] * data[i - lag];
    }
    for (size_t i = start; i < n; ++i) {
        for (unsigned int lag = 0; lag <= max_order; ++lag)
            autoc[lag] += data[i] * data[i - lag];
    }
}

unsigned int lpc_levinson(const double *autoc, unsigned int max_order,
                          double coefs[LPC_MAX_ORDER][LPC_MAX_ORDER],
                          double *errors) {
    double lpc[LPC_MAX_ORDER];
    double error = autoc[0];

    if (max_order > LPC_MAX_ORDER || !(error > 0.0))
        return 0;

    for (unsigned int i = 0; i < max_order; ++i) {
        double reflection = -autoc[i + 1];
        unsigned int j;

        for (j = 0; j < i; ++j)
            reflection -= lpc[j] * autoc[i - j];
        reflection /= error;

        lpc[i] = reflection;
        for (j = 0; j < i / 2; ++j) {
            double saved = lpc[j];

            lpc[j] += reflection * lpc[i - 1 - j];
            lpc[i - 1 - j] += reflection * saved;
        }
        if (i % 2 != 0)
            lpc[j] += lpc[j] * reflection;

        error *= 1.0 - reflection * reflection;
        for (j = 0; j <= i; ++j)
            coefs[i][j] = -lpc[j];
        errors[i] = error;

        if (!(error > 0.0))
            return i + 1;
    }
    return max_order;
}

unsigned int lpc_estimate_order(const double *errors, unsigned int orders,
                                size_t n, unsigned int bits_per_order) {
    unsigned int best = 1;
    double best_bits = HUGE_VAL;

    for (unsigned int order = 1; order <= orders; ++order) {
        double error = errors[order - 1];
        double per_sample =
            error > 0.0 ? 0.5 * log2(0.5 * error / (double)n) : 0.0;
        double bits;

        if (per_sample < 0.0)
            per_sample = 0.0;
        bits = per_sample * (double)(n - order) +
               (double)order * (double)bits_per_order;
        if (bits < best_bits) {
            best = order;
            best_bits = bits;
        }
    }
    return best;
}

bool lpc_quantize(const double *coefs, unsigned int order,
                  unsigned int precision, int32_t *qcoefs,
                  unsigned int *shift) {
    double largest = 0.0;
    double error = 0.0;
    int32_t max;
    int exponent;
    int bits;

    if (order == 0 || order > LPC_MAX_ORDER || precision == 0 ||
        precision > LPC_MAX_PRECISION)
        return false;

    for (unsigned int i = 0; i < order; ++i) {
        if (!isfinite(coefs[i]))
            return false;
        if (fabs(coefs[i]) > largest)
            largest = fabs(coefs[i]);
    }
    if (!(largest > 0.0))
        return false;

    (void)frexp(largest, &exponent);
    bits = (int)precision - 1 - exponent;
    if (bits < 0)
        return false;
    if (bits > LPC_MAX_SHIFT)
        bits = LPC_MAX_SHIFT;

    max = (int32_t)((UINT32_C(1) << (precision - 1)) - 1);
    for (unsigned int i = 0; i < order; ++i) {
        long q;

        error += ldexp(coefs[i], bits);
        q = lround(error);
        if (q > max)
            q = max;
        else if (q < -max - 1)
            q = -max - 1;
        error -= (double)q;
        qcoefs[i] = (int32_t)q;
    }

    *shift = (unsigned int)bits;
    return true;
}

#define FLOOR_OFFSET (UINT64_C(1) << 62u)

static int64_t floor_shift(int64_t value, unsigned int shift) {
    return (int64_t)(((uint64_t)value + FLOOR_OFFSET) >> shift) -
           (int64_t)(FLOOR_OFFSET >> shift);
}

static int64_t predict(const int64_t *x, size_t i, const int32_t *qcoefs,
                       unsigned int order, unsigned int shift) {
    int64_t sum = 0;

    for (unsigned int j = 0; j < order; ++j)
        sum += (int64_t)qcoefs[j] * x[i - 1 - j];
    return floor_shift(sum, shift);
}

bool lpc_residuals(const int64_t *x, size_t n, const int32_t *qcoefs,
                   unsigned int order, unsigned int shift, int64_t limit,
                   int64_t *residuals) {
    if (order == 0 || order > LPC_MAX_ORDER || order > n ||
        shift > LPC_MAX_SHIFT)
        return false;

    for (size_t i = order; i < n; ++i) {
        int64_t residual = x[i] - predict(x, i, qcoefs, order, shift);

        if (residual >= limit || residual <= -limit)
            return false;
        residuals[i - order] = residual;
    }
    return true;
}

bool lpc_restore(int64_t *x, size_t n, const int32_t *qcoefs,
                 unsigned int order, unsigned int shift,
                 const int64_t *residuals, unsigned int width) {
    int64_t max = (INT64_C(1) << (width - 1)) - 1;
    int64_t min = -max - 1;

    if (order == 0 || order > LPC_MAX_ORDER || order > n ||
        shift > LPC_MAX_SHIFT)
        return false;

    for (size_t i = order; i < n; ++i) {
        int64_t value =
            residuals[i - order] + predict(x, i, qcoefs, order, shift);

        if (value < min || value > max)
            return false;
        x[i] = value;
    }
    return true;
}
