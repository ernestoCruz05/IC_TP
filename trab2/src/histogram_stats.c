#include "histogram_stats.h"

#include <math.h>

double entropy_from_histogram(const size_t *histogram,
                                     size_t bin_count,
                                     size_t sample_count)
{
    double entropy = 0.0;

    for (size_t value = 0; value < bin_count; value++) {
        if (histogram[value] == 0) {
            continue;//se a contagem de um val for 0 ignora
        }

        double probability = (double)histogram[value] / (double)sample_count;
        // prob = contagem do val / total pixeis
        entropy -= probability * log2(probability);
    }

    return entropy;
}

size_t count_horizontal_differences(const uint16_t *pixels,
                                           size_t rows,
                                           size_t columns,
                                           size_t difference_offset,
                                           size_t *histogram)
{
    size_t pair_count = 0;

    for (size_t row = 0; row < rows; row++) {
        for (size_t column = 1; column < columns; column++) {
            //começa na col 1 pq a 0 n tem pixel a esquerda
            size_t current_index = row * columns + column; //pos do pixel atual
            size_t left_index = current_index - 1;//pos do pixel a esquerda
            int difference = (int)pixels[current_index] -
                             (int)pixels[left_index];//val da esquerda - val atual
            size_t bin = (size_t)(difference + (int)difference_offset);//diferença é convertida num indice da tabela
            histogram[bin]++;//aumenta o contador do indice da tabela q deu antes
            pair_count++;//+ um par contado
        }
    }

    return pair_count; //pair_count vai ser igual a rows *(columns - 1)
}

size_t count_vertical_differences(const uint16_t *pixels,
                                         size_t rows,
                                         size_t columns,
                                         size_t difference_offset,
                                         size_t *histogram)
{
    size_t pair_count = 0;

    for (size_t row = 1; row < rows; row++) {
        //começa na 2 linha pq a primeira n tem nada acima
        for (size_t column = 0; column < columns; column++) {
            size_t current_index = row * columns + column;//pos do pixel atual
            size_t above_index = current_index - columns;//pos do pixel de cima
            int difference = (int)pixels[current_index] -
                             (int)pixels[above_index];//pixel atual - pixel de cima
            size_t bin = (size_t)(difference + (int)difference_offset);//diferença é convertida num indice da tabela
            histogram[bin]++;//aumenta o contador do indice da tabela q deu antes
            pair_count++;//+ um par contado
        }
    }

    return pair_count;//pair_count vai ser igual a (rows - 1) *(columns - 1)
}

//func q devolve 1 se o total = nº esperado de pixeis
int histogram_count_matches(const size_t *histogram,
                                   size_t bin_count,
                                   size_t expected_count,
                                   size_t *actual_count)
{
    size_t total = 0;
    for (size_t value = 0; value < bin_count; value++) {
        total += histogram[value];
    }

    *actual_count = total;
    return total == expected_count;
}
