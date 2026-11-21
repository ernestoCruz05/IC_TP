#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define BITSTREAM_BUF_SIZE 65536

typedef struct {
    FILE *file;
    uint8_t buf[BITSTREAM_BUF_SIZE];
    size_t buf_pos;
    uint64_t bit_buf;
    int bit_count;
    int error;
} BIT_WRITER;

typedef struct {
    FILE *file;
    uint8_t buf[BITSTREAM_BUF_SIZE];
    size_t buf_pos;
    size_t buf_len;
    uint64_t bit_buf;
    int bit_count;
    int eof;
    int error;
} BIT_READER;

void bw_init(BIT_WRITER *bw, FILE *file);
int bw_write_bit(BIT_WRITER *bw, uint32_t bit);
int bw_write_bits(BIT_WRITER *bw, uint32_t value, int nbits);
int bw_write_unary(BIT_WRITER *bw, uint32_t zeros);
int bw_flush(BIT_WRITER *bw);

void br_init(BIT_READER *br, FILE *file);
int br_read_bit(BIT_READER *br, uint32_t *bit);
int br_read_bits(BIT_READER *br, int nbits, uint32_t *value);
int br_read_unary(BIT_READER *br, uint32_t max_zeros, uint32_t *zeros);

#endif
