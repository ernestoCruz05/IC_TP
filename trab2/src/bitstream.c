#include "bitstream.h"

static int bw_flush_bytes(BIT_WRITER *bw)
{
    if (bw->buf_pos == 0) {
        return 1;
    }
    if (fwrite(bw->buf, 1, bw->buf_pos, bw->file) != bw->buf_pos) {
        bw->error = 1;
        return 0;
    }
    bw->buf_pos = 0;
    return 1;
}

static inline int bw_put_byte(BIT_WRITER *bw, uint8_t byte)
{
    bw->buf[bw->buf_pos++] = byte;
    if (bw->buf_pos == BITSTREAM_BUF_SIZE) {
        return bw_flush_bytes(bw);
    }
    return 1;
}

void bw_init(BIT_WRITER *bw, FILE *file)
{
    bw->file = file;
    bw->buf_pos = 0;
    bw->bit_buf = 0;
    bw->bit_count = 0;
    bw->error = 0;
}

int bw_write_bits(BIT_WRITER *bw, uint32_t value, int nbits)
{
    if (nbits == 0) {
        return 1;
    }
    uint64_t mask = (nbits == 32) ? UINT64_C(0xffffffff)
                                  : ((UINT64_C(1) << nbits) - 1);
    bw->bit_buf = (bw->bit_buf << nbits) | ((uint64_t)value & mask);
    bw->bit_count += nbits;

    while (bw->bit_count >= 8) {
        bw->bit_count -= 8;
        uint8_t byte = (uint8_t)((bw->bit_buf >> bw->bit_count) & 0xffu);
        if (!bw_put_byte(bw, byte)) {
            return 0;
        }
    }
    return 1;
}

int bw_write_bit(BIT_WRITER *bw, uint32_t bit)
{
    return bw_write_bits(bw, bit & 1u, 1);
}

int bw_write_unary(BIT_WRITER *bw, uint32_t zeros)
{
    while (zeros >= 32) {
        if (!bw_write_bits(bw, 0, 32)) {
            return 0;
        }
        zeros -= 32;
    }
    if (zeros > 0) {
        if (!bw_write_bits(bw, 0, (int)zeros)) {
            return 0;
        }
    }
    return bw_write_bits(bw, 1, 1);
}

int bw_flush(BIT_WRITER *bw)
{
    if (bw->bit_count > 0) {
        uint8_t byte = (uint8_t)((bw->bit_buf << (8 - bw->bit_count)) & 0xffu);
        bw->bit_count = 0;
        bw->bit_buf = 0;
        if (!bw_put_byte(bw, byte)) {
            return 0;
        }
    }
    return bw_flush_bytes(bw);
}

void br_init(BIT_READER *br, FILE *file)
{
    br->file = file;
    br->buf_pos = 0;
    br->buf_len = 0;
    br->bit_buf = 0;
    br->bit_count = 0;
    br->eof = 0;
    br->error = 0;
}

static int br_fill(BIT_READER *br, int needed)
{
    while (br->bit_count < needed) {
        if (br->buf_pos == br->buf_len) {
            br->buf_len = fread(br->buf, 1, BITSTREAM_BUF_SIZE, br->file);
            br->buf_pos = 0;
            if (br->buf_len == 0) {
                if (ferror(br->file)) {
                    br->error = 1;
                } else {
                    br->eof = 1;
                }
                return 0;
            }
        }
        br->bit_buf = (br->bit_buf << 8) | br->buf[br->buf_pos++];
        br->bit_count += 8;
    }
    return 1;
}

int br_read_bits(BIT_READER *br, int nbits, uint32_t *value)
{
    if (nbits == 0) {
        *value = 0;
        return 1;
    }
    if (br->bit_count < nbits && !br_fill(br, nbits)) {
        return 0;
    }
    br->bit_count -= nbits;
    uint64_t mask = (nbits == 32) ? UINT64_C(0xffffffff)
                                  : ((UINT64_C(1) << nbits) - 1);
    *value = (uint32_t)((br->bit_buf >> br->bit_count) & mask);
    return 1;
}

int br_read_bit(BIT_READER *br, uint32_t *bit)
{
    return br_read_bits(br, 1, bit);
}

int br_read_unary(BIT_READER *br, uint32_t max_zeros, uint32_t *zeros)
{
    uint32_t count = 0;
    for (;;) {
        uint32_t bit = 0;
        if (!br_read_bit(br, &bit)) {
            return 0;
        }
        if (bit == 1) {
            *zeros = count;
            return 1;
        }
        count++;
        if (count > max_zeros) {
            br->error = 1;
            return 0;
        }
    }
}
