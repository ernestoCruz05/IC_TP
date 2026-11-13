#ifndef WAV_CMP_H
#define WAV_CMP_H

#include "wav.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double mse;        
    double rmse;       
    double max_error;   
    double snr;         
    uint64_t samples;   
} CMP_METRICS;

typedef struct {
    CMP_METRICS *channels;     
    CMP_METRICS average_signal;
    uint16_t num_channels;
    uint64_t frames_compared;
    double scale_factor;        
} CMP_REPORT;

bool cmp_calculate(FILE *file_orig, const WAV_INFO *info_orig,
                   FILE *file_test, const WAV_INFO *info_test,
                   CMP_REPORT *report);

void cmp_report_free(CMP_REPORT *report);

void cmp_report_print(const CMP_REPORT *report,
                      const WAV_INFO *info_orig, const char *path_orig,
                      const WAV_INFO *info_test, const char *path_test);

#endif
