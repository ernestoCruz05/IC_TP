#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRAY_LEVELS 256 //pixel de 8 bits pode ter val de 0 a 255
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

static double entropy_from_histogram(const size_t histogram[GRAY_LEVELS],
                                     size_t pixel_count)
{
    double entropy = 0.0;

    for (size_t value = 0; value < GRAY_LEVELS; value++) {
        if (histogram[value] == 0) {
            continue;//se a contagem de um val for 0 ignora
        }

        double probability = (double)histogram[value] / (double)pixel_count;
        // prob = contagem do val / total pixeis
        entropy -= probability * log2(probability);
    }

    return entropy;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s image.pgm\n", argv[0]);
        return 2;
    }

    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) {
        perror(argv[1]);
        return 1;
    }

    char magic[TOKEN_CAPACITY];
    size_t columns;
    size_t rows;
    size_t max_value;
    if (!read_pnm_token(file, magic) || strcmp(magic, "P5") != 0 ||
        !parse_positive_integer(file, &columns) ||
        !parse_positive_integer(file, &rows) ||
        !parse_positive_integer(file, &max_value)) {
        fprintf(stderr, "%s: invalid or unsupported PGM header\n", argv[1]);
        fclose(file);
        return 1;
    }
    if (max_value != 255) {
        fprintf(stderr, "%s: this first version supports Maxval=255 only\n",
                argv[1]);
        fclose(file);
        return 1;
    }
    if (columns > SIZE_MAX / rows) {
        fprintf(stderr, "%s: image dimensions are too large\n", argv[1]);
        fclose(file);
        return 1;
    }

    size_t pixel_count = columns * rows;
    uint8_t *pixels = malloc(pixel_count);
    if (pixels == NULL) {
        fprintf(stderr, "%s: not enough memory for the image\n", argv[1]);
        fclose(file);
        return 1;
    }

    if (fread(pixels, 1, pixel_count, file) != pixel_count) {
        fprintf(stderr, "%s: incomplete pixel data\n", argv[1]);
        free(pixels);
        fclose(file);
        return 1;
    }
    fclose(file);

    size_t histogram[GRAY_LEVELS] = {0};
    for (size_t i = 0; i < pixel_count; i++) {
        histogram[pixels[i]]++;
    }

    printf("File: %s\n", argv[1]);
    printf("Format: PGM P5, 8-bit grayscale\n");
    printf("Dimensions: %zu columns x %zu rows\n", columns, rows);
    printf("Maxval: %zu\n", max_value);
    printf("Pixels read: %zu\n", pixel_count);
    puts("\nHistogram (value: count):");
    for (size_t value = 0; value < GRAY_LEVELS; value++) {
        if (histogram[value] != 0) {
            printf("%3zu: %zu\n", value, histogram[value]);
        }
    }
    printf("\nPixel entropy: %.6f bits/pixel\n",
           entropy_from_histogram(histogram, pixel_count));

    free(pixels);
    return 0;
}
