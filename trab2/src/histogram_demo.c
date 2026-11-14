#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define GRAY_LEVELS 256 // 256 vals de cinzento possiveis

static double entropy_from_histogram(const size_t histogram[GRAY_LEVELS],
                                     size_t pixel_count) //args sao hist ja contado, nº de pixeis
{
    double entropy = 0.0;

    for (size_t value = 0; value < GRAY_LEVELS; value++) {
        if (histogram[value] == 0) {
            continue;
        } //se a contagem de um val for 0 ignora

        double probability = (double)histogram[value] / (double)pixel_count;
        // prob = contagem do val / total pixeis
        entropy -= probability * log2(probability);
    }

    return entropy;
}

int main(void)
{
    const uint8_t image[2][3] = {
        {10, 10, 20},
        {20, 20, 30},
    }; //2 linhas e 3 col e cada val ocupa 8 bits (uint8_t)
    size_t histogram[GRAY_LEVELS] = {0};
    const size_t rows = 2;
    const size_t columns = 3;
    const size_t pixel_count = rows * columns;

    for (size_t row = 0; row < rows; row++) {
        for (size_t column = 0; column < columns; column++) {
            uint8_t value = image[row][column];
            histogram[value]++;
        }
    }

    printf("Imagem exemplo (%zu x %zu):\n", columns, rows);
    for (size_t row = 0; row < rows; row++) {
        for (size_t column = 0; column < columns; column++) {
            printf("%3u ", (unsigned int)image[row][column]);
        }
        putchar('\n');
    }

    puts("\nHist (valor: contagem):");
    for (size_t value = 0; value < GRAY_LEVELS; value++) {
        if (histogram[value] != 0) {
            printf("%3zu: %zu\n", value, histogram[value]);
        }
    }

    printf("\nEntropia: %.6f bits/pixel\n",
           entropy_from_histogram(histogram, pixel_count));

    return 0;
}
