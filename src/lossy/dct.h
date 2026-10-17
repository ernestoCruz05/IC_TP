#ifndef LOSSY_DCT_H
#define LOSSY_DCT_H

#include <stddef.h>

void dct2_forward(const double *input, double *output, size_t n);
void dct2_inverse(const double *input, double *output, size_t n);
void mdct_forward(const double *input, double *output, size_t n);
void mdct_inverse(const double *input, double *output, size_t n);

#endif
