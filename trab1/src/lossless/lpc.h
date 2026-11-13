#ifndef LPC_H
#define LPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LPC_MAX_ORDER 32
#define LPC_MAX_PRECISION 15
#define LPC_MAX_SHIFT 31

void lpc_window_tukey(double *window, size_t n, double alpha);

void lpc_apply_window(const int64_t *x, const double *window, size_t n,
                      double *data);

void lpc_autocorrelation(const double *data, size_t n, unsigned int max_order,
                         double *autoc);

unsigned int lpc_levinson(const double *autoc, unsigned int max_order,
                          double coefs[LPC_MAX_ORDER][LPC_MAX_ORDER],
                          double *errors);

unsigned int lpc_estimate_order(const double *errors, unsigned int orders,
                                size_t n, unsigned int bits_per_order);

bool lpc_quantize(const double *coefs, unsigned int order,
                  unsigned int precision, int32_t *qcoefs,
                  unsigned int *shift);

bool lpc_residuals(const int64_t *x, size_t n, const int32_t *qcoefs,
                   unsigned int order, unsigned int shift, int64_t limit,
                   int64_t *residuals);

bool lpc_restore(int64_t *x, size_t n, const int32_t *qcoefs,
                 unsigned int order, unsigned int shift,
                 const int64_t *residuals, unsigned int width);

#endif
