#include "hist_plot.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>

static void write_xml_text(FILE *file, const char *text) {
    while (*text) {
        switch (*text) {
        case '&':
            fputs("&amp;", file);
            break;
        case '<':
            fputs("&lt;", file);
            break;
        case '>':
            fputs("&gt;", file);
            break;
        case '"':
            fputs("&quot;", file);
            break;
        case '\'':
            fputs("&apos;", file);
            break;
        default:
            fputc((unsigned char)*text, file);
            break;
        }
        ++text;
    }
}

static uint64_t column_total(const HIST *hist, size_t first, size_t visible,
                             size_t columns, size_t column) {
    size_t begin = first + column * visible / columns;
    size_t end = first + (column + 1) * visible / columns;
    uint64_t total = 0;
    size_t bin;

    for (bin = begin; bin < end; ++bin)
        total += hist->bins[bin];
    return total;
}

static uint64_t rendered_maximum(const HIST *hist) {
    const size_t max_columns = 1060;
    size_t first = 0;
    size_t last;
    size_t visible;
    size_t columns;
    size_t column;
    uint64_t maximum = 0;

    while (first < hist->bin_count && hist->bins[first] == 0)
        ++first;
    if (first == hist->bin_count) {
        first = 0;
        last = hist->bin_count - 1;
    } else {
        last = hist->bin_count - 1;
        while (last > first && hist->bins[last] == 0)
            --last;
    }

    visible = last - first + 1;
    columns = visible < max_columns ? visible : max_columns;
    for (column = 0; column < columns; ++column) {
        uint64_t total = column_total(hist, first, visible, columns, column);
        if (total > maximum)
            maximum = total;
    }
    return maximum;
}

static void render_histogram(FILE *file, const HIST *hist, const char *title,
                             int panel_top, uint64_t maximum) {
    const int left = 100;
    const int plot_top = panel_top + 65;
    const int plot_width = 1060;
    const int plot_height = 290;
    const size_t max_columns = 1060;
    size_t first = 0;
    size_t last;
    size_t visible;
    size_t columns;
    size_t column;
    int64_t range_min;
    int64_t range_max;

    while (first < hist->bin_count && hist->bins[first] == 0)
        ++first;

    if (first == hist->bin_count) {
        first = 0;
        last = hist->bin_count - 1;
    } else {
        last = hist->bin_count - 1;
        while (last > first && hist->bins[last] == 0)
            --last;
    }

    visible = last - first + 1;
    columns = visible < max_columns ? visible : max_columns;

    range_min =
        (int64_t)hist->min_val + (int64_t)((uint64_t)first * hist->bin_width);
    range_max = (int64_t)hist->min_val +
                (int64_t)(((uint64_t)last + 1) * hist->bin_width) - 1;
    if (range_max > hist->max_val)
        range_max = hist->max_val;

    fprintf(file,
            "<text x=\"100\" y=\"%d\" font-size=\"20\" "
            "font-weight=\"bold\">",
            panel_top + 24);
    write_xml_text(file, title);
    fprintf(file,
            "</text>\n<text x=\"100\" y=\"%d\" font-size=\"13\">"
            "bin width: %" PRIu64 " · %zu bins in occupied range",
            panel_top + 47, hist->bin_width, visible);
    if (columns < visible)
        fprintf(file, " · display aggregated into %zu columns", columns);
    fputs("</text>\n", file);

    for (int tick = 0; tick <= 4; ++tick) {
        int y = plot_top + tick * plot_height / 4;
        uint64_t value = maximum * (uint64_t)(4 - tick) / 4;
        fprintf(file,
                "<line class=\"grid\" x1=\"%d\" y1=\"%d\" x2=\"%d\" "
                "y2=\"%d\"/>\n"
                "<text x=\"%d\" y=\"%d\" font-size=\"11\" "
                "text-anchor=\"end\">%" PRIu64 "</text>\n",
                left, y, left + plot_width, y, left - 10, y + 4, value);
    }

    for (column = 0; column < columns; ++column) {
        uint64_t total = column_total(hist, first, visible, columns, column);
        double x = left + (double)column * plot_width / (double)columns;
        double next_x =
            left + (double)(column + 1) * plot_width / (double)columns;
        double bar_height =
            maximum == 0 ? 0.0 : (double)total * plot_height / (double)maximum;

        if (bar_height > 0.0) {
            fprintf(file,
                    "<rect x=\"%.3f\" y=\"%.3f\" width=\"%.3f\" "
                    "height=\"%.3f\" fill=\"#2563eb\"/>\n",
                    x, plot_top + plot_height - bar_height, next_x - x + 0.15,
                    bar_height);
        }
    }

    fprintf(file,
            "<line class=\"axis\" x1=\"%d\" y1=\"%d\" x2=\"%d\" "
            "y2=\"%d\"/>\n"
            "<line class=\"axis\" x1=\"%d\" y1=\"%d\" x2=\"%d\" "
            "y2=\"%d\"/>\n",
            left, plot_top, left, plot_top + plot_height, left,
            plot_top + plot_height, left + plot_width, plot_top + plot_height);

    if (range_min <= 0 && range_max >= 0) {
        double zero_x = range_min == range_max
                            ? left + plot_width / 2.0
                            : left + (double)(-range_min) * plot_width /
                                         (double)(range_max - range_min);
        bool label_on_left = zero_x > left + plot_width - 45;

        fprintf(file,
                "<line class=\"zero-marker\" x1=\"%.3f\" y1=\"%d\" "
                "x2=\"%.3f\" y2=\"%d\">"
                "<title>Sample value 0</title></line>\n"
                "<text class=\"zero-label\" x=\"%.3f\" y=\"%d\" "
                "font-size=\"12\" text-anchor=\"%s\">0</text>\n",
                zero_x, plot_top, zero_x, plot_top + plot_height,
                zero_x + (label_on_left ? -5.0 : 5.0), plot_top + 16,
                label_on_left ? "end" : "start");
    }

    for (int tick = 0; tick <= 4; ++tick) {
        if (range_min == range_max && tick != 2)
            continue;

        int x = left + tick * plot_width / 4;
        int64_t value = range_min + (range_max - range_min) * tick / 4;
        fprintf(file,
                "<line class=\"axis\" x1=\"%d\" y1=\"%d\" x2=\"%d\" "
                "y2=\"%d\"/>\n"
                "<text x=\"%d\" y=\"%d\" font-size=\"11\" "
                "text-anchor=\"middle\">%" PRId64 "</text>\n",
                x, plot_top + plot_height, x, plot_top + plot_height + 6, x,
                plot_top + plot_height + 21, value);
    }

    fprintf(file,
            "<text x=\"%d\" y=\"%d\" font-size=\"13\" "
            "text-anchor=\"middle\">Sample value</text>\n"
            "<text x=\"24\" y=\"%d\" font-size=\"13\" "
            "text-anchor=\"middle\" transform=\"rotate(-90 24 %d)\">"
            "Count</text>\n",
            left + plot_width / 2, plot_top + plot_height + 43,
            plot_top + plot_height / 2, plot_top + plot_height / 2);
}

bool hist_plot_svg(const char *path, const HIST histograms[],
                   const char *const labels[], size_t count,
                   const char *source) {
    const int width = 1200;
    const int panel_height = 430;
    FILE *file;
    int height;
    size_t i;
    bool ok;
    uint64_t maximum = 0;

    if (!path || !histograms || !labels || count == 0 || count > 4 || !source) {
        errno = EINVAL;
        return false;
    }

    for (i = 0; i < count; ++i) {
        uint64_t histogram_maximum;

        if (!histograms[i].bins || histograms[i].bin_count == 0 || !labels[i]) {
            errno = EINVAL;
            return false;
        }
        histogram_maximum = rendered_maximum(&histograms[i]);
        if (histogram_maximum > maximum)
            maximum = histogram_maximum;
    }

    file = fopen(path, "w");
    if (!file)
        return false;

    height = 70 + (int)count * panel_height;
    fprintf(file,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" "
            "height=\"%d\" viewBox=\"0 0 %d %d\">\n"
            "<rect width=\"100%%\" height=\"100%%\" fill=\"#ffffff\"/>\n"
            "<style>text{font-family:sans-serif;fill:#111827}"
            ".grid{stroke:#e5e7eb;stroke-width:1}"
            ".axis{stroke:#374151;stroke-width:1.5}"
            ".zero-marker{stroke:#b91c1c;stroke-width:1.5;stroke-dasharray:5 4}"
            ".zero-label{fill:#b91c1c;paint-order:stroke;stroke:white;"
            "stroke-width:3}</style>\n"
            "<text x=\"100\" y=\"36\" font-size=\"24\" "
            "font-weight=\"bold\">",
            width, height, width, height);
    write_xml_text(file, source);
    fputs("</text>\n", file);

    for (i = 0; i < count; ++i)
        render_histogram(file, &histograms[i], labels[i],
                         55 + (int)i * panel_height, maximum);

    fputs("</svg>\n", file);
    ok = !ferror(file);
    if (fclose(file) != 0)
        ok = false;
    return ok;
}
