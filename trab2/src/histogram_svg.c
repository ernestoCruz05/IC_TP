#include "histogram_svg.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int ensure_output_directory(void)
{
    // cria a pasta se ainda nao existir
    if (mkdir("out", 0777) == 0 || errno == EEXIST) return 1;
    perror("out");
    return 0;
}

static char *make_svg_path(const char *input_path,
                           const char *suffix,
                           int use_bit_range,
                           unsigned first_bit,
                           unsigned last_bit)
{
    // usa o nome do ficheiro sem a extensao para criar o nome do svg
    const char *base_name = strrchr(input_path, '/');
    base_name = base_name == NULL ? input_path : base_name + 1;
    const char *extension = strrchr(base_name, '.');
    size_t stem_length = extension == NULL ? strlen(base_name) :
                         (size_t)(extension - base_name);
    size_t capacity = stem_length + strlen(suffix) + 64;
    char *path = malloc(capacity);
    if (path == NULL) return NULL;

    if (use_bit_range) {
        snprintf(path, capacity, "out/%.*s_%s_bits%u-%u.svg",
                 (int)stem_length, base_name, suffix, first_bit, last_bit);
    } else {
        snprintf(path, capacity, "out/%.*s_%s.svg",
                 (int)stem_length, base_name, suffix);
    }
    return path;
}

static size_t grouped_count(const size_t *histogram,
                            size_t bin_count,
                            size_t display_bins,
                            size_t column)
{
    // junta bins q sao vizinhos so para o grafico ficar + pequeno
    size_t first = column * bin_count / display_bins;
    size_t end = (column + 1) * bin_count / display_bins;
    size_t count = 0;
    for (size_t value = first; value < end; value++) {
        count += histogram[value];
    }
    return count;
}

static size_t maximum_count(const size_t *histogram, size_t bin_count)
{
    // procura a maior contagem para definir a escala vertical
    size_t maximum = 0;
    for (size_t value = 0; value < bin_count; value++) {
        if (histogram[value] > maximum) maximum = histogram[value];
    }
    return maximum;
}

static void write_grid(FILE *svg, double x, double y, double width,
                       double height, unsigned divisions, size_t maximum,
                       unsigned font_size)
{
    // desenha as linhas horizontais e os valores do eixo y
    for (unsigned grid = 0; grid <= divisions; grid++) {
        double line_y = y + height * grid / divisions;
        size_t label = maximum - maximum * grid / divisions;
        fprintf(svg,
                "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" "
                "y2=\"%.1f\" stroke=\"#dddddd\"/>\n"
                "<text x=\"%.1f\" y=\"%.1f\" text-anchor=\"end\" "
                "font-family=\"sans-serif\" font-size=\"%u\">%zu</text>\n",
                x, line_y, x + width, line_y, x - 8.0, line_y + 4.0,
                font_size, label);
    }
}

static void write_x_axis(FILE *svg, double x, double y, double width,
                         size_t last_value, unsigned font_size,
                         double label_offset)
{
    // desenha o eixo x e marca o primeiro e ultimo valor
    fprintf(svg,
            "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" "
            "y2=\"%.1f\" stroke=\"black\"/>\n"
            "<text x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\" "
            "font-family=\"sans-serif\" font-size=\"%u\">0</text>\n"
            "<text x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\" "
            "font-family=\"sans-serif\" font-size=\"%u\">%zu</text>\n",
            x, y, x + width, y, x, y + label_offset, font_size,
            x + width, y + label_offset, font_size, last_value);
}

int write_histogram_svg(const char *input_path,
                               int is_raw,
                               const size_t *histogram,
                               size_t bin_count,
                               int use_bit_range,
                               unsigned first_bit,
                               unsigned last_bit)
{
    // cada histograma pode ter no max 256 colunas no svg
    if (!ensure_output_directory()) return 0;
    char *output_path = make_svg_path(input_path,
                                      is_raw ? "raw_hist" : "hist",
                                      use_bit_range, first_bit, last_bit);
    if (output_path == NULL) {
        fprintf(stderr, "could not allocate memory for SVG path\n");
        return 0;
    }

    FILE *svg = fopen(output_path, "w");
    if (svg == NULL) {
        perror(output_path);
        free(output_path);
        return 0;
    }

    size_t display_bins = bin_count < 256 ? bin_count : 256;
    size_t max_count = 0;
    for (size_t column = 0; column < display_bins; column++) {
        // encontra a contagem + alta dps de agrupar os bins
        size_t count = grouped_count(histogram, bin_count, display_bins, column);
        if (count > max_count) max_count = count;
    }

    const double chart_x = 75.0;
    const double chart_y = 55.0;
    const double chart_width = 870.0;
    const double chart_height = 370.0;
    fprintf(svg,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" "
            "width=\"1000\" height=\"520\" viewBox=\"0 0 1000 520\">\n"
            "<rect width=\"1000\" height=\"520\" fill=\"white\"/>\n"
            "<text x=\"500\" y=\"30\" text-anchor=\"middle\" "
            "font-family=\"sans-serif\" font-size=\"20\">"
            "Pixel histogram</text>\n"
            "<text x=\"20\" y=\"240\" text-anchor=\"middle\" "
            "font-family=\"sans-serif\" font-size=\"14\" "
            "transform=\"rotate(-90 20 240)\">Count</text>\n"
            "<text x=\"510\" y=\"490\" text-anchor=\"middle\" "
            "font-family=\"sans-serif\" font-size=\"14\">Pixel value</text>\n");

    write_grid(svg, chart_x, chart_y, chart_width, chart_height, 4,
               max_count, 11);

    for (size_t column = 0; column < display_bins; column++) {
        // altura de cada barra depende da contagem desse grupo
        size_t count = grouped_count(histogram, bin_count, display_bins, column);
        double bar_width = chart_width / display_bins;
        double bar_height = max_count == 0 ? 0.0 :
                            chart_height * (double)count / (double)max_count;
        double x = chart_x + column * bar_width;
        double y = chart_y + chart_height - bar_height;
        fprintf(svg,
                "<rect x=\"%.3f\" y=\"%.3f\" width=\"%.3f\" "
                "height=\"%.3f\" fill=\"#3976a8\"/>\n",
                x, y, bar_width + 0.1, bar_height);
    }

    write_x_axis(svg, chart_x, chart_y + chart_height, chart_width,
                 bin_count - 1, 11, 18.0);

    if (display_bins < bin_count) {
        fprintf(svg,
                "<text x=\"500\" y=\"515\" text-anchor=\"middle\" "
                "font-family=\"sans-serif\" font-size=\"11\">"
                "Display grouped into %zu columns; calculations use exact values"
                "</text>\n",
                display_bins);
    }
    fputs("</svg>\n", svg);

    int success = fclose(svg) == 0;
    if (!success) {
        perror(output_path);
    } else {
        printf("Histogram SVG: %s\n", output_path);
    }
    free(output_path);
    return success;
}

int write_ppm_histogram_svgs(const char *input_path,
                                    size_t *histograms[3],
                                    size_t bin_count,
                                    int use_bit_range,
                                    unsigned first_bit,
                                    unsigned last_bit)
{
    // no ppm sao criados 2 graficos: 1 com paineis e outro com linhas sobrepostas
    if (!ensure_output_directory()) return 0;
    char *panels_path = make_svg_path(input_path, "ppm_hist_channels",
                                      use_bit_range, first_bit, last_bit);
    char *overlay_path = make_svg_path(input_path, "ppm_hist_overlay",
                                       use_bit_range, first_bit, last_bit);
    if (panels_path == NULL || overlay_path == NULL) {
        fprintf(stderr, "could not allocate memory for PPM SVG paths\n");
        free(panels_path);
        free(overlay_path);
        return 0;
    }

    const char *channel_names[3] = {"Red", "Green", "Blue"};
    const char *channel_colors[3] = {"#d62728", "#2ca02c", "#1f77b4"};
    const double chart_x = 85.0;
    const double chart_width = 850.0;
    const double chart_height = 190.0;
    size_t display_bins = bin_count < 256 ? bin_count : 256;
    FILE *svg = fopen(panels_path, "w");
    if (svg == NULL) {
        perror(panels_path);
        free(panels_path);
        free(overlay_path);
        return 0;
    }

    fprintf(svg,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" "
            "width=\"1000\" height=\"900\" viewBox=\"0 0 1000 900\">\n"
            "<rect width=\"1000\" height=\"900\" fill=\"white\"/>\n"
            "<text x=\"500\" y=\"28\" text-anchor=\"middle\" "
            "font-family=\"sans-serif\" font-size=\"20\">"
            "PPM channel histograms</text>\n");

    for (size_t channel = 0; channel < 3; channel++) {
        // cada canal tem a sua propria escala neste grafico
        size_t max_count = maximum_count(histograms[channel], bin_count);

        double chart_y = 65.0 + channel * 270.0;
        fprintf(svg,
                "<text x=\"500\" y=\"%.1f\" text-anchor=\"middle\" "
                "font-family=\"sans-serif\" font-size=\"16\">%s</text>\n",
                chart_y - 12.0, channel_names[channel]);
        write_grid(svg, chart_x, chart_y, chart_width, chart_height, 2,
                   max_count, 10);

        double bar_width = chart_width / display_bins;
        for (size_t column = 0; column < display_bins; column++) {
            size_t count = grouped_count(histograms[channel], bin_count,
                                         display_bins, column);
            double bar_height = max_count == 0 ? 0.0 :
                chart_height * (double)count / (double)max_count;
            fprintf(svg,
                    "<rect x=\"%.3f\" y=\"%.3f\" width=\"%.3f\" "
                    "height=\"%.3f\" fill=\"%s\"/>\n",
                    chart_x + column * bar_width,
                    chart_y + chart_height - bar_height,
                    bar_width + 0.1, bar_height, channel_colors[channel]);
        }
        write_x_axis(svg, chart_x, chart_y + chart_height, chart_width,
                     bin_count - 1, 10, 15.0);
    }
    fputs("</svg>\n", svg);
    int panels_ok = fclose(svg) == 0;

    svg = fopen(overlay_path, "w");
    if (svg == NULL) {
        perror(overlay_path);
        free(panels_path);
        free(overlay_path);
        return 0;
    }

    const double overlay_x = 85.0;
    const double overlay_y = 65.0;
    const double overlay_width = 850.0;
    const double overlay_height = 700.0;
    size_t max_count = 0;
    for (size_t channel = 0; channel < 3; channel++) {
        // a escala sobreposta usa a maior contagem entre os tres canais
        size_t channel_max = maximum_count(histograms[channel], bin_count);
        if (channel_max > max_count) max_count = channel_max;
    }

    fprintf(svg,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" "
            "width=\"1000\" height=\"850\" viewBox=\"0 0 1000 850\">\n"
            "<rect width=\"1000\" height=\"850\" fill=\"white\"/>\n"
            "<text x=\"500\" y=\"28\" text-anchor=\"middle\" "
            "font-family=\"sans-serif\" font-size=\"20\">"
            "Overlaid RGB histograms</text>\n");
    for (size_t channel = 0; channel < 3; channel++) {
        fprintf(svg,
                "<line x1=\"%zu\" y1=\"48\" x2=\"%zu\" y2=\"48\" "
                "stroke=\"%s\" stroke-width=\"3\"/>\n"
                "<text x=\"%zu\" y=\"52\" font-family=\"sans-serif\" "
                "font-size=\"13\">%s</text>\n",
                350 + channel * 110, 380 + channel * 110,
                channel_colors[channel], 386 + channel * 110,
                channel_names[channel]);
    }
    write_grid(svg, overlay_x, overlay_y, overlay_width, overlay_height,
               4, max_count, 11);
    for (size_t channel = 0; channel < 3; channel++) {
        // cada ponto da linha representa a contagem de um valor do canal
        fprintf(svg, "<polyline fill=\"none\" stroke=\"%s\" "
                     "stroke-width=\"1.5\" points=\"",
                channel_colors[channel]);
        for (size_t value = 0; value < bin_count; value++) {
            double x = bin_count == 1 ? overlay_x :
                overlay_x + overlay_width * value / (bin_count - 1);
            double y = max_count == 0 ? overlay_y + overlay_height :
                overlay_y + overlay_height - overlay_height *
                (double)histograms[channel][value] / (double)max_count;
            fprintf(svg, "%.2f,%.2f%s", x, y,
                    value + 1 == bin_count ? "" : " ");
        }
        fputs("\"/>\n", svg);
    }
    write_x_axis(svg, overlay_x, overlay_y + overlay_height,
                 overlay_width, bin_count - 1, 11, 18.0);
    fprintf(svg,
            "<text x=\"510\" y=\"820\" text-anchor=\"middle\" "
            "font-family=\"sans-serif\" font-size=\"14\">Pixel value</text>\n"
            "</svg>\n");
    int overlay_ok = fclose(svg) == 0;

    if (!panels_ok || !overlay_ok) {
        fprintf(stderr, "could not finish writing PPM SVG files\n");
    } else {
        printf("Histogram SVGs: %s, %s\n", panels_path, overlay_path);
    }
    free(panels_path);
    free(overlay_path);
    return panels_ok && overlay_ok;
}
