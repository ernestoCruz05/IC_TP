#ifndef HIST_PLOT_H
#define HIST_PLOT_H

#include "wav_hist.h"

#include <stdbool.h>
#include <stddef.h>

bool hist_plot_svg(const char *path, const HIST histograms[],
                   const char *const labels[], size_t count,
                   const char *source);

#endif
