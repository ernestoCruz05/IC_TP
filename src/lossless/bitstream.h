#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t size;
    uint64_t acc;
    unsigned int count;
    bool error;
} BIT_WRITER;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint64_t acc;
    unsigned int count;
    bool error;
} BIT_READER;

void bit_writer_init(BIT_WRITER *w, uint8_t *data, size_t capacity);

void bit_write(BIT_WRITER *w, uint64_t value, unsigned int bits);

void bit_write_signed(BIT_WRITER *w, int64_t value, unsigned int bits);

void bit_write_unary(BIT_WRITER *w, uint64_t q);

uint64_t bit_writer_bits(const BIT_WRITER *w);

bool bit_writer_finish(BIT_WRITER *w, size_t *size);

void bit_reader_init(BIT_READER *r, const uint8_t *data, size_t size);

uint64_t bit_read(BIT_READER *r, unsigned int bits);

int64_t bit_read_signed(BIT_READER *r, unsigned int bits);

uint64_t bit_read_unary(BIT_READER *r);

bool bit_reader_finish(const BIT_READER *r);

#endif
