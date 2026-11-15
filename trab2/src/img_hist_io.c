#include "img_hist_io.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

//funcao chamada quando existe uma linha com "#"
void skip_comment(FILE *file, int ch)
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
}

int read_pnm_token(FILE *file, char token[TOKEN_CAPACITY])
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

int parse_positive_integer(FILE *file, size_t *value) //func para transformar por ex "123" em 123
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

int parse_positive_argument(const char *text, size_t *value)
{
    // ve se o argumento tem apenas digitos e representa um valor positivo
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

int parse_bit_range(const char *text, unsigned *first, unsigned *last)
{
    
    char *separator = strchr(text, '-'); // antes do '-' é o first_bit e dps o second
    if (separator == NULL || separator == text || separator[1] == '\0') {
        return 0;
    }

    errno = 0;
    char *end = NULL;
    unsigned long parsed_first = strtoul(text, &end, 10); // converte o bit inicial
    if (errno != 0 || end != separator) {
        return 0;
    }

    errno = 0;
    unsigned long parsed_last = strtoul(separator + 1, &end, 10); // converte o bit final
    if (errno != 0 || end == separator + 1 || *end != '\0' ||
        parsed_first > parsed_last || parsed_last > 15) {
        return 0;
    }

    *first = (unsigned)parsed_first;
    *last = (unsigned)parsed_last;
    return 1;
}

int read_pgm_pixels(FILE *file,
                           uint16_t *pixels,
                           size_t pixel_count,
                           size_t max_value)
{
    // PGM guarda um valor por pixel, em 16 bits os bytes estao em big-endian
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

int read_ppm_pixels(FILE *file,
                           uint16_t *red,
                           uint16_t *green,
                           uint16_t *blue,
                           size_t pixel_count,
                           size_t max_value)
{
    // no ficheiro os valores estao intercalados, mas ficam em vetores separados
    uint16_t *channels[3] = {red, green, blue};

    for (size_t i = 0; i < pixel_count; i++) {
        for (size_t channel = 0; channel < 3; channel++) {
            // le R, G e B para o pixel atual
            int first_byte = fgetc(file);
            if (first_byte == EOF) {
                return 0;
            }

            uint16_t sample = (uint16_t)first_byte;
            if (max_value > 255) {
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
            channels[channel][i] = sample;
        }
    }

    return fgetc(file) == EOF && !ferror(file);
}

int read_raw_pixels(FILE *file,
                           uint16_t *pixels,
                           size_t pixel_count,
                           size_t bits,
                           int big_endian)
{
    // RAW nao tem cabecalho, por isso a leitura segue os parametros recebidos
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
            // big-endian: primeiro vem o byte mais significativo
            pixels[i] = (uint16_t)(((uint16_t)first_byte << 8) |
                                   (uint16_t)second_byte);
        } else {
            // little-endian: primeiro vem o byte menos significativo
            pixels[i] = (uint16_t)((uint16_t)first_byte |
                                   ((uint16_t)second_byte << 8));
        }
    }

    return fgetc(file) == EOF && !ferror(file);
}
