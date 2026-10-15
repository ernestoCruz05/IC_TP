#ifndef STEREO_H
#define STEREO_H

#include <stddef.h>
#include <stdint.h>

enum { STEREO_LR, STEREO_LS, STEREO_SR, STEREO_MS };

void stereo_split(const int64_t *left, const int64_t *right, size_t n,
                  int64_t *mid, int64_t *side);

void stereo_join(unsigned int mode, int64_t *first, int64_t *second,
                 size_t n);

#endif
