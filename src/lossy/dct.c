#include "dct.h"

#include <math.h>

static void fft_positive(double *real, double *imag, size_t n) {
    size_t j = 0;
    const double pi = acos(-1.0);

    for (size_t i = 1; i < n; ++i) {
        size_t bit = n >> 1;
        while ((j & bit) != 0) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            double temp = real[i]; real[i] = real[j]; real[j] = temp;
            temp = imag[i]; imag[i] = imag[j]; imag[j] = temp;
        }
    }

    for (size_t length = 2; length <= n; length <<= 1) {
        double angle = 2.0 * pi / (double)length;
        double step_real = cos(angle), step_imag = sin(angle);
        size_t half = length >> 1;
        for (size_t start = 0; start < n; start += length) {
            double twiddle_real = 1.0, twiddle_imag = 0.0;
            for (size_t k = 0; k < half; ++k) {
                size_t even = start + k, odd = even + half;
                double odd_real = real[odd] * twiddle_real -
                                  imag[odd] * twiddle_imag;
                double odd_imag = real[odd] * twiddle_imag +
                                  imag[odd] * twiddle_real;
                double even_real = real[even], even_imag = imag[even];
                real[even] = even_real + odd_real;
                imag[even] = even_imag + odd_imag;
                real[odd] = even_real - odd_real;
                imag[odd] = even_imag - odd_imag;
                double next_real = twiddle_real * step_real -
                                   twiddle_imag * step_imag;
                twiddle_imag = twiddle_imag * step_real +
                               twiddle_real * step_imag;
                twiddle_real = next_real;
            }
        }
        if (length == n) break;
    }
}

void dct2_forward(const double *input, double *output, size_t n) {
    const double pi = acos(-1.0);
    double real[8192], imag[8192];
    size_t fft_size;
    if (n == 0 || n > 4096) return;
    fft_size = 2 * n;
    for (size_t i = 0; i < n; ++i) {
        real[i] = input[i];
        imag[i] = 0.0;
        real[fft_size - 1 - i] = input[i];
        imag[fft_size - 1 - i] = 0.0;
    }
    fft_positive(real, imag, fft_size);
    for (size_t k = 0; k < n; ++k) {
        double phase = pi * (double)k / (double)fft_size;
        double sum = 0.5 * (real[k] * cos(phase) - imag[k] * sin(phase));
        double scale = k == 0 ? sqrt(1.0 / (double)n)
                              : sqrt(2.0 / (double)n);
        output[k] = sum * scale;
    }
}

void dct2_inverse(const double *input, double *output, size_t n) {
    const double pi = acos(-1.0);
    double real[8192] = {0.0}, imag[8192] = {0.0};
    size_t fft_size;
    if (n == 0 || n > 4096) return;
    fft_size = 2 * n;
    real[0] = input[0] * sqrt(1.0 / (double)n);
    for (size_t k = 1; k < n; ++k) {
        double scale = sqrt(2.0 / (double)n);
        double phase = pi * (double)k / (double)fft_size;
        double magnitude = input[k] * scale * 0.5;
        real[k] = magnitude * cos(phase);
        imag[k] = magnitude * sin(phase);
        real[fft_size - k] = real[k];
        imag[fft_size - k] = -imag[k];
    }
    fft_positive(real, imag, fft_size);
    for (size_t i = 0; i < n; ++i) output[i] = real[i];
}

void mdct_forward(const double *input, double *output, size_t n) {
    const double pi = acos(-1.0);
    double real[8192], imag[8192];
    size_t fft_size;
    if (n > 4096) return;
    fft_size = 2 * n;
    for (size_t i = 0; i < fft_size; ++i) {
        double value = input[i] * sin(pi * ((double)i + 0.5) /
                                     (2.0 * (double)n));
        double phase = pi * (double)i / (2.0 * (double)n);
        real[i] = value * cos(phase);
        imag[i] = value * sin(phase);
    }
    fft_positive(real, imag, fft_size);
    for (size_t k = 0; k < n; ++k) {
        double phase = pi / (double)n * (0.5 + (double)n / 2.0) *
                       ((double)k + 0.5);
        output[k] = (real[k] * cos(phase) - imag[k] * sin(phase)) *
                    sqrt(2.0 / (double)n);
    }
}

void mdct_inverse(const double *input, double *output, size_t n) {
    const double pi = acos(-1.0);
    double real[8192] = {0.0}, imag[8192] = {0.0};
    double window[8192];
    if (n > 4096) return;
    size_t fft_size = 2 * n;
    for (size_t i = 0; i < 2 * n; ++i)
        window[i] = sin(pi * ((double)i + 0.5) / (2.0 * (double)n));
    for (size_t k = 0; k < n; ++k) {
        double phase = pi / (double)n * (0.5 + (double)n / 2.0) * (double)k;
        real[k] = input[k] * cos(phase);
        imag[k] = input[k] * sin(phase);
    }
    fft_positive(real, imag, fft_size);
    for (size_t i = 0; i < fft_size; ++i) {
        double phase = pi * (double)i / (2.0 * (double)n) +
                       pi / (double)n * (0.5 + (double)n / 2.0) * 0.5;
        double sum = real[i] * cos(phase) - imag[i] * sin(phase);
        output[i] = sqrt(2.0 / (double)n) * sum * window[i];
    }
}
