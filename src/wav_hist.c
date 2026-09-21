#include "wav_hist.h"

int32_t calc_mid(int32_t left, int32_t right){
    return ((int64_t)left+right) / 2;
}

int32_t calc_diff(int32_t left, int32_t right){
    return ((int64_t)left-right) / 2;
}

bool init(HIST *hist, int32_t min_value, int32_t max_value, uint32_t bin_width){
   if(!hist || bin_width == 0 || min_value > max_value){
    return false;
   }

   uint64_t range = (int64_t)max_value - min_value + 1;
   size_t bin_count = (size_t)(range + bin_width - 1) /bin_width;
   hist->bins= calloc(bin_count,sizeof(*hist->bins));
   if(!hist->bins){
    return false;
   }
    
   hist->min_val = min_value;
   hist->max_val = max_value;
   hist->bin_width = bin_width;
   hist->bin_count = bin_count;
   
   return true;
}

void hist_add(HIST *hist, int32_t sample){
    if (!hist || !hist->bins){
        return;
    }

    if (sample < hist->min_val || sample > hist->max_val){
        return;
    }

    uint64_t offset = (int64_t)sample - hist->min_val;
    size_t bin= offset / hist->bin_width;

    if (bin >= hist->bin_count){
        return;
    }

    hist->bins[bin]++;
}

void hist_free(HIST *hist){
    free(hist->bins);
    hist->bins = NULL;
    hist->bin_count = 0;
}

int main(void)
{
    printf("wav_hist\n");
    return 0;
}
