#include "bitstream.h"

static uint64_t low_mask(unsigned int bits) {
    return bits >= 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
}

static void push_bits(BIT_WRITER *w, uint64_t value, unsigned int bits) {
    w->acc = (w->acc << bits) | value;
    w->count += bits;
    while (w->count >= 8) {
        w->count -= 8;
        if (w->size < w->capacity)
            w->data[w->size++] = (uint8_t)(w->acc >> w->count);
        else
            w->error = true;
    }
    w->acc &= low_mask(w->count);
}

void bit_writer_init(BIT_WRITER *w, uint8_t *data, size_t capacity) {
    *w = (BIT_WRITER){.data = data, .capacity = capacity};
}

void bit_write(BIT_WRITER *w, uint64_t value, unsigned int bits) {
    if (bits > 64) {
        w->error = true;
        return;
    }

    value &= low_mask(bits);
    if (bits > 32) {
        push_bits(w, value >> 32u, bits - 32);
        push_bits(w, value & UINT32_MAX, 32);
    } else {
        push_bits(w, value, bits);
    }
}

void bit_write_signed(BIT_WRITER *w, int64_t value, unsigned int bits) {
    if (bits == 0 || bits > 64) {
        w->error = true;
        return;
    }

    if (bits < 64) {
        int64_t limit = INT64_C(1) << (bits - 1);

        if (value < -limit || value >= limit) {
            w->error = true;
            return;
        }
    }
    bit_write(w, (uint64_t)value, bits);
}

void bit_write_unary(BIT_WRITER *w, uint64_t q) {
    for (; q >= 32; q -= 32)
        push_bits(w, 0, 32);
    push_bits(w, 1, (unsigned int)q + 1);
}

uint64_t bit_writer_bits(const BIT_WRITER *w) {
    return (uint64_t)w->size * 8 + w->count;
}

bool bit_writer_finish(BIT_WRITER *w, size_t *size) {
    if (w->count > 0)
        push_bits(w, 0, 8 - w->count);
    *size = w->size;
    return !w->error;
}

void bit_reader_init(BIT_READER *r, const uint8_t *data, size_t size) {
    *r = (BIT_READER){.data = data, .size = size};
}

static uint64_t pull_bits(BIT_READER *r, unsigned int bits) {
    uint64_t value;

    if (r->error)
        return 0;

    while (r->count < bits) {
        if (r->pos >= r->size) {
            r->error = true;
            return 0;
        }
        r->acc = (r->acc << 8u) | (uint64_t)r->data[r->pos++];
        r->count += 8;
    }

    r->count -= bits;
    value = r->acc >> r->count;
    r->acc &= low_mask(r->count);
    return value;
}

uint64_t bit_read(BIT_READER *r, unsigned int bits) {
    uint64_t value;

    if (bits > 64) {
        r->error = true;
        return 0;
    }

    if (bits <= 32)
        return pull_bits(r, bits);

    value = pull_bits(r, bits - 32) << 32u;
    value |= pull_bits(r, 32);
    return r->error ? 0 : value;
}

int64_t bit_read_signed(BIT_READER *r, unsigned int bits) {
    uint64_t value;
    uint64_t sign;

    if (bits == 0 || bits > 64) {
        r->error = true;
        return 0;
    }

    value = bit_read(r, bits);
    sign = UINT64_C(1) << (bits - 1);
    if ((value & sign) == 0)
        return (int64_t)value;
    return -(int64_t)(~value & (sign - 1)) - 1;
}

static unsigned int bit_length(uint64_t value) {
    static const uint8_t lengths[256] = {
        0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    };

    return lengths[value & 0xffu];
}

uint64_t bit_read_unary(BIT_READER *r) {
    uint64_t q = 0;
    unsigned int length;

    if (r->error)
        return 0;

    while (r->acc == 0) {
        q += r->count;
        if (r->pos >= r->size) {
            r->error = true;
            return 0;
        }
        r->acc = r->data[r->pos++];
        r->count = 8;
    }

    length = bit_length(r->acc);
    q += r->count - length;
    r->count = length - 1;
    r->acc &= low_mask(r->count);
    return q;
}

bool bit_reader_finish(const BIT_READER *r) {
    return !r->error && r->pos == r->size && r->count < 8 && r->acc == 0;
}
