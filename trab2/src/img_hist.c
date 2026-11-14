#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TOKEN_CAPACITY 64 //espaço max para guardar um el do cabeçalho

// ideia geral : abrir um file PGM -> ler infos do cabecalho -> contar pixeis -> calcular entropia

static void skip_comment(FILE *file, int ch)
{
    while (ch != EOF && ch != '\n' && ch != '\r') {
        ch = fgetc(file); //se for "#imagem" o ch é "i" depois "m",...
    }
    //em alguns ficheiros o fim da linha é /r/n
    if (ch == '\r') { 
        int next = fgetc(file);
        if (next != '\n' && next != EOF) {
            ungetc(next, file); //se n for /n a proxima letra ja importa e nao vai ser skipada
        }
    }
} //funcao chamada quando existe uma linha com "#"

static int read_pnm_token(FILE *file, char token[TOKEN_CAPACITY])
{
    int ch;

    for (;;) {
        ch = fgetc(file);
        if (ch == EOF) {
            return 0; //chegou ao final do file e nao encontrou token, devolve 0
        }
        if (isspace((unsigned char)ch)) {
            continue;//ignora espaço ou mudança de linha
        }
        if (ch == '#') {
            skip_comment(file, fgetc(file));
            continue;//ignora comentario
        }
        break;
    }
    // encontrou o 1 caracter do token

    size_t length = 0;
    while (ch != EOF && !isspace((unsigned char)ch) && ch != '#') {
        if (length + 1 >= TOKEN_CAPACITY) {
            return 0;
        }
        token[length++] = (char)ch;
        ch = fgetc(file);
    }
    //*1 -> EOF , *2 -> espaço ou mudança de linha, *3 -> inicio de comentario
    //copia os caracter para token ate encontrar *1 ou *2 ou *3

    //ao ler 123 vai ser guardado os car "1", "2", "3"

    if (length == 0) {
        return 0; //nenhum token
    } 
    token[length] = '\0'; //acrescenta '\0' ao fim do token

    //qnd acaba a copia, ch é o car que acabou o token
    if (ch == '#') {
        skip_comment(file, fgetc(file));
    } else if (ch == '\r') {
        int next = fgetc(file);
        if (next != '\n' && next != EOF) {
            ungetc(next, file);
        }
    }

    return 1; //token encontrado e guardado
    //esta func pode ser chamada por ex 4 vezes para ler P5, larg, alt e o Maxval
}

static int parse_positive_integer(FILE *file, size_t *value) //func para transformar por ex "123" em 123
{
    //value -> onde vai ser guardado o num convertido
    char token[TOKEN_CAPACITY];
    if (!read_pnm_token(file, token)) {
        return 0;
    }
    for (size_t i = 0; token[i] != '\0'; i++) {
        if (!isdigit((unsigned char)token[i])) {
            return 0;
        }
    }//larg, alt e Maxval sao inteiros positivos 

    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(token, &end, 10); //string -> unsi long long
    // token = "123", parsed = 123
    if (errno != 0 || end == token || *end != '\0' || parsed == 0 ||
        parsed > SIZE_MAX) {
        return 0;
    }

    *value = (size_t)parsed; 
    return 1;//convertido c sucesso
}


static int parse_positive_argument(const char *text, size_t *value)
{
    if (text[0] == '\0') {
        return 0;
    }
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (!isdigit((unsigned char)text[i])) {
            return 0;
        }
    }

    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);//texto -> inteiro sem sinal em decimal
    if (errno != 0 || end == text || *end != '\0' || parsed == 0 ||
        parsed > SIZE_MAX) {
        return 0;
    }

    *value = (size_t)parsed;
    return 1;
}

static int parse_bit_range(const char *text, unsigned *first, unsigned *last)
{
    char *separator = strchr(text, '-'); // antes do '-' é o first_bit e dps o second
    if (separator == NULL || separator == text || separator[1] == '\0') {
        return 0;
    }

    errno = 0;
    char *end = NULL;
    unsigned long parsed_first = strtoul(text, &end, 10);
    if (errno != 0 || end != separator) {
        return 0;
    }

    errno = 0;
    unsigned long parsed_last = strtoul(separator + 1, &end, 10);
    if (errno != 0 || end == separator + 1 || *end != '\0' ||
        parsed_first > parsed_last || parsed_last > 15) {
        return 0;
    }

    *first = (unsigned)parsed_first;
    *last = (unsigned)parsed_last;
    return 1;
}

static int read_pgm_pixels(FILE *file,
                           uint16_t *pixels,
                           size_t pixel_count,
                           size_t max_value)
{
    for (size_t i = 0; i < pixel_count; i++) {
        int first_byte = fgetc(file);
        if (first_byte == EOF) {
            return 0;
        }

        uint16_t sample = (uint16_t)first_byte;
        if (max_value > 255) {//dois bytes por pixel
            //big endian, 1 byte é o + significativo
            int second_byte = fgetc(file);
            if (second_byte == EOF) {
                return 0;
            }
            sample = (uint16_t)(((uint16_t)first_byte << 8) |
                                (uint16_t)second_byte);
        }

        if (sample > max_value) {
            return 0;
        }
        pixels[i] = sample;
    }

    return 1;
}

static int read_raw_pixels(FILE *file,
                           uint16_t *pixels,
                           size_t pixel_count,
                           size_t bits,
                           int big_endian)
{
    for (size_t i = 0; i < pixel_count; i++) {
        int first_byte = fgetc(file);
        if (first_byte == EOF) {
            return 0;
        }

        if (bits == 8) {
            pixels[i] = (uint16_t)first_byte;
            continue;
        }

        int second_byte = fgetc(file);
        if (second_byte == EOF) {
            return 0;
        }
        if (big_endian) {
            pixels[i] = (uint16_t)(((uint16_t)first_byte << 8) |
                                   (uint16_t)second_byte);
        } else {
            pixels[i] = (uint16_t)((uint16_t)first_byte |
                                   ((uint16_t)second_byte << 8));
        }
    }

    return fgetc(file) == EOF && !ferror(file);
}

static double entropy_from_histogram(const size_t *histogram,
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

static size_t count_horizontal_differences(const uint16_t *pixels,
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

static size_t count_vertical_differences(const uint16_t *pixels,
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
static int histogram_count_matches(const size_t *histogram,
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

static int write_histogram_svg(const char *input_path,
                               int is_raw,
                               const size_t *histogram,
                               size_t bin_count,
                               int use_bit_range,
                               unsigned first_bit,
                               unsigned last_bit)
{
    if (mkdir("out", 0777) != 0 && errno != EEXIST) {
        perror("out");
        return 0;
    }

    const char *base_name = strrchr(input_path, '/');
    base_name = base_name == NULL ? input_path : base_name + 1;
    const char *extension = strrchr(base_name, '.');
    size_t stem_length = extension == NULL ? strlen(base_name) :
                         (size_t)(extension - base_name);
    size_t path_capacity = stem_length + 64;
    char *output_path = malloc(path_capacity);
    if (output_path == NULL) {
        fprintf(stderr, "could not allocate memory for SVG path\n");
        return 0;
    }

    if (is_raw && use_bit_range) {
        snprintf(output_path, path_capacity,
                 "out/%.*s_raw_hist_bits%u-%u.svg",
                 (int)stem_length, base_name, first_bit, last_bit);
    } else if (is_raw) {
        snprintf(output_path, path_capacity, "out/%.*s_raw_hist.svg",
                 (int)stem_length, base_name);
    } else if (use_bit_range) {
        snprintf(output_path, path_capacity, "out/%.*s_hist_bits%u-%u.svg",
                 (int)stem_length, base_name, first_bit, last_bit);
    } else {
        snprintf(output_path, path_capacity, "out/%.*s_hist.svg",
                 (int)stem_length, base_name);
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
        size_t first_value = column * bin_count / display_bins;
        size_t end_value = (column + 1) * bin_count / display_bins;
        size_t count = 0;
        for (size_t value = first_value; value < end_value; value++) {
            count += histogram[value];
        }
        if (count > max_count) {
            max_count = count;
        }
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

    for (unsigned grid = 0; grid <= 4; grid++) {
        double y = chart_y + chart_height * grid / 4.0;
        size_t label = max_count - max_count * grid / 4;
        fprintf(svg,
                "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" "
                "y2=\"%.1f\" stroke=\"#dddddd\"/>\n"
                "<text x=\"%.1f\" y=\"%.1f\" text-anchor=\"end\" "
                "font-family=\"sans-serif\" font-size=\"11\">%zu</text>\n",
                chart_x, y, chart_x + chart_width, y,
                chart_x - 8.0, y + 4.0, label);
    }

    for (size_t column = 0; column < display_bins; column++) {
        size_t first_value = column * bin_count / display_bins;
        size_t end_value = (column + 1) * bin_count / display_bins;
        size_t count = 0;
        for (size_t value = first_value; value < end_value; value++) {
            count += histogram[value];
        }
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

    fprintf(svg,
            "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" "
            "y2=\"%.1f\" stroke=\"black\"/>\n"
            "<text x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\" "
            "font-family=\"sans-serif\" font-size=\"11\">0</text>\n"
            "<text x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\" "
            "font-family=\"sans-serif\" font-size=\"11\">%zu</text>\n",
            chart_x, chart_y + chart_height,
            chart_x + chart_width, chart_y + chart_height,
            chart_x, chart_y + chart_height + 18.0,
            chart_x + chart_width, chart_y + chart_height + 18.0,
            bin_count - 1);

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

int main(int argc, char **argv)
{
    unsigned first_bit = 0;
    unsigned last_bit = 0;
    int use_bit_range = 0;
    int is_raw = 0;
    int rows_set = 0;
    int columns_set = 0;
    int bits_set = 0;
    int endian_set = 0;
    int big_endian = 0;
    size_t raw_rows = 0;
    size_t raw_columns = 0;
    size_t raw_bits = 0;
    const char *input_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            if (strcmp(argv[++i], "raw") != 0 || is_raw) {
                fprintf(stderr, "%s: only --format raw is supported here\n",
                        argv[0]);
                return 2;
            }
            is_raw = 1;
        } else if (strcmp(argv[i], "--rows") == 0 && i + 1 < argc) {
            if (rows_set || !parse_positive_argument(argv[++i], &raw_rows)) {
                fprintf(stderr, "%s: invalid or repeated --rows value\n",
                        argv[0]);
                return 2;
            }
            rows_set = 1;
        } else if (strcmp(argv[i], "--cols") == 0 && i + 1 < argc) {
            if (columns_set ||
                !parse_positive_argument(argv[++i], &raw_columns)) {
                fprintf(stderr, "%s: invalid or repeated --cols value\n",
                        argv[0]);
                return 2;
            }
            columns_set = 1;
        } else if (strcmp(argv[i], "--bits") == 0 && i + 1 < argc) {
            if (bits_set || !parse_positive_argument(argv[++i], &raw_bits)) {
                fprintf(stderr, "%s: invalid or repeated --bits value\n",
                        argv[0]);
                return 2;
            }
            bits_set = 1;
        } else if (strcmp(argv[i], "--endian") == 0 && i + 1 < argc) {
            if (endian_set) {
                fprintf(stderr, "%s: repeated --endian option\n", argv[0]);
                return 2;
            }
            const char *endian = argv[++i];
            if (strcmp(endian, "big") == 0) {
                big_endian = 1;
            } else if (strcmp(endian, "little") == 0) {
                big_endian = 0;
            } else {
                fprintf(stderr, "%s: --endian must be big or little\n",
                        argv[0]);
                return 2;
            }
            endian_set = 1;
        } else if (strcmp(argv[i], "--bit-range") == 0 && i + 1 < argc) {
            if (use_bit_range ||
                !parse_bit_range(argv[++i], &first_bit, &last_bit)) {
                fprintf(stderr, "%s: invalid or repeated --bit-range\n",
                        argv[0]);
                return 2;
            }
            use_bit_range = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown or incomplete option '%s'\n",
                    argv[0], argv[i]);
            return 2;
        } else if (input_path == NULL) {
            input_path = argv[i];
        } else {
            fprintf(stderr, "%s: more than one input file\n", argv[0]);
            return 2;
        }
    }

    if (input_path == NULL ||
        (is_raw && (!rows_set || !columns_set || !bits_set ||
                    (raw_bits == 16 && !endian_set) ||
                    (raw_bits != 8 && raw_bits != 16) ||
                    (raw_bits == 8 && endian_set))) ||
        (!is_raw && (rows_set || columns_set || bits_set || endian_set))) {
        fprintf(stderr,
                "Usage: %s [--bit-range FIRST-LAST] image.pgm\n"
                "       %s --format raw --rows N --cols N --bits 8 image.raw\n"
                "       %s --format raw --rows N --cols N --bits 16 "
                "--endian big|little image.raw\n",
                argv[0], argv[0], argv[0]);
        return 2;
    }

    FILE *file = fopen(input_path, "rb");
    if (file == NULL) {
        perror(input_path);
        return 1;
    }

    size_t columns = is_raw ? raw_columns : 0;
    size_t rows = is_raw ? raw_rows : 0;
    size_t max_value = is_raw ? (raw_bits == 8 ? 255 : 65535) : 0;
    if (!is_raw) {
        char magic[TOKEN_CAPACITY];
        if (!read_pnm_token(file, magic) || strcmp(magic, "P5") != 0 ||
            !parse_positive_integer(file, &columns) ||
            !parse_positive_integer(file, &rows) ||
            !parse_positive_integer(file, &max_value)) {
            fprintf(stderr, "%s: invalid or unsupported PGM header\n",
                    input_path);
            fclose(file);
            return 1;
        }
        if (max_value > 65535) {
            fprintf(stderr, "%s: PGM Maxval cannot be greater than 65535\n",
                    input_path);
            fclose(file);
            return 1;
        }
    }

    if (columns > SIZE_MAX / rows) {
        fprintf(stderr, "%s: image dimensions are too large\n", input_path);
        fclose(file);
        return 1;
    }

    size_t pixel_count = columns * rows;
    if (pixel_count > SIZE_MAX / sizeof(uint16_t)) {
        fprintf(stderr, "%s: image is too large to store in memory\n", input_path);
        fclose(file);
        return 1;
    }

    uint16_t *pixels = malloc(pixel_count * sizeof(*pixels));
    if (pixels == NULL) {
        fprintf(stderr, "%s: not enough memory for the image\n", input_path);
        fclose(file);
        return 1;
    }

    int pixels_read = is_raw ?
        read_raw_pixels(file, pixels, pixel_count, raw_bits, big_endian) :
        read_pgm_pixels(file, pixels, pixel_count, max_value);
    if (!pixels_read) {
        fprintf(stderr, "%s: invalid, incomplete, or excess pixel data\n",
                input_path);
        free(pixels);
        fclose(file);
        return 1;
    }
    fclose(file);

    size_t analyzed_max = max_value;
    if (use_bit_range) {
        unsigned storage_bits = is_raw ? (unsigned)raw_bits :
                                (max_value <= 255 ? 8u : 16u);
        if (last_bit >= storage_bits) {
            fprintf(stderr,
                    "%s: bit range %u-%u is outside the %u-bit pixel data\n",
                    input_path, first_bit, last_bit, storage_bits);
            free(pixels);
            return 1;
        }

        unsigned width = last_bit - first_bit + 1;// 0-5 -> 6
        uint32_t mask = ((uint32_t)1 << width) - 1;//00111111 fica so 111111 se for de 0-5 o bit-range
        for (size_t i = 0; i < pixel_count; i++) {
            pixels[i] = (uint16_t)(((uint32_t)pixels[i] >> first_bit) & mask);
        }
        analyzed_max = mask;
    }

    size_t pixel_bin_count = analyzed_max + 1;
    size_t difference_bin_count = 2 * analyzed_max + 1;
    size_t *histogram = calloc(pixel_bin_count, sizeof(*histogram));
    size_t *horizontal_histogram =
        calloc(difference_bin_count, sizeof(*horizontal_histogram));
    size_t *vertical_histogram =
        calloc(difference_bin_count, sizeof(*vertical_histogram));
    if (histogram == NULL || horizontal_histogram == NULL ||
        vertical_histogram == NULL) {
        fprintf(stderr, "%s: not enough memory for histograms\n", input_path);
        free(pixels);
        free(histogram);
        free(horizontal_histogram);
        free(vertical_histogram);
        return 1;
    }

    for (size_t i = 0; i < pixel_count; i++) {
        histogram[pixels[i]]++;
    }


    //soma das contagens do histograma e comparaçao da soma com rows × columns
    size_t histogram_total;
    if (!histogram_count_matches(histogram, pixel_bin_count, pixel_count,
                                 &histogram_total)) {
        fprintf(stderr,
                "%s: histogram contains %zu pixels, expected %zu\n",
                input_path, histogram_total, pixel_count);
        free(pixels);
        free(histogram);
        free(horizontal_histogram);
        free(vertical_histogram);
        return 1;
    }

    size_t horizontal_pair_count = count_horizontal_differences(
        pixels, rows, columns, analyzed_max, horizontal_histogram);
    size_t vertical_pair_count = count_vertical_differences(
        pixels, rows, columns, analyzed_max, vertical_histogram);

    if (!write_histogram_svg(input_path, is_raw, histogram, pixel_bin_count,
                             use_bit_range, first_bit, last_bit)) {
        free(pixels);
        free(histogram);
        free(horizontal_histogram);
        free(vertical_histogram);
        return 1;
    }


    printf("File: %s\n", input_path);
    if (is_raw) {
        if (raw_bits == 16) {
            printf("Format: RAW, 16-bit grayscale, %s-endian\n",
                   big_endian ? "big" : "little");
        } else {
            puts("Format: RAW, 8-bit grayscale");
        }
    } else {
        printf("Format: PGM P5, %zu-bit grayscale\n",
               max_value <= 255 ? (size_t)8 : (size_t)16);
    }
    printf("Dimensions: %zu columns x %zu rows\n", columns, rows);
    printf("Maxval: %zu\n", max_value);
    if (use_bit_range) {
        printf("Bit range: %u-%u\n", first_bit, last_bit);
    }
    printf("Pixels read: %zu\n", pixel_count);
    printf("Histogram total: %zu\n", histogram_total);
    printf("Horizontal pairs: %zu\n", horizontal_pair_count);
    printf("Vertical pairs: %zu\n", vertical_pair_count);
    puts("\nHistogram (value: count):");
    for (size_t value = 0; value < pixel_bin_count; value++) {
        if (histogram[value] != 0) {
            printf("%3zu: %zu\n", value, histogram[value]);
        }
    }
    printf("\nPixel entropy: %.6f bits/pixel\n",
           entropy_from_histogram(histogram, pixel_bin_count, pixel_count));
    if (horizontal_pair_count == 0) {
        puts("Horizontal difference entropy: n/a (no neighbor pairs)");
    } else {
        printf("Horizontal difference entropy: %.6f bits/pair\n",
               entropy_from_histogram(horizontal_histogram,
                                      difference_bin_count,
                                      horizontal_pair_count));
    }
    if (vertical_pair_count == 0) {
        puts("Vertical difference entropy: n/a (no neighbor pairs)");
    } else {
        printf("Vertical difference entropy: %.6f bits/pair\n",
               entropy_from_histogram(vertical_histogram,
                                      difference_bin_count,
                                      vertical_pair_count));
    }

    free(pixels);
    free(histogram);
    free(horizontal_histogram);
    free(vertical_histogram);
    return 0;
}
