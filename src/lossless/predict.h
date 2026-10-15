#ifndef PREDICT_H
#define PREDICT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FIXED_MAX_ORDER 4

void fixed_residuals(const int64_t *x, size_t n, unsigned int order,
                     int64_t *residuals);

bool fixed_restore(int64_t *x, size_t n, unsigned int order,
                   const int64_t *residuals, unsigned int width);

#endif
