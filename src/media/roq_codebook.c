#include "qa/media.h"
#include "qa/binary.h"

#include <string.h>

static int32_t shift(int32_t value, unsigned bits) {
    return value >= 0 ? value / (INT32_C(1) << bits) : -((-value + (INT32_C(1) << bits) - 1) / (INT32_C(1) << bits));
}
static int32_t chroma(float coefficient, int32_t value, int32_t bias) {
    float factor = (coefficient / 2) * 64 + 0.5f;
    return (int32_t)(factor * (float)value + (float)bias);
}
static uint32_t channel(int32_t value, unsigned bits, int32_t maximum) {
    int32_t shifted = shift(value, bits);
    return (uint32_t)(shifted < 0 ? 0 : shifted > maximum ? maximum : shifted);
}
uint16_t qa_roq_yuv565(uint8_t y, uint8_t u, uint8_t v) {
    int32_t yy = ((int32_t)y << 6) | (y >> 2), xu = 2 * (int32_t)u - 255, xv = 2 * (int32_t)v - 255;
    uint32_t r = channel(yy + chroma(1.402f, xv, 32), 9, 31);
    uint32_t g = channel(yy + chroma(0.34414f, -xu, 0) + chroma(0.71414f, -xv, 32), 8, 63);
    uint32_t b = channel(yy + chroma(1.772f, xu, 32), 9, 31);
    return (uint16_t)((r << 11) | (g << 5) | b);
}
uint32_t qa_roq_yuv_rgba(uint8_t y, uint8_t u, uint8_t v) {
    int32_t yy = ((int32_t)y << 6) | (y >> 2), xu = 2 * (int32_t)u - 255, xv = 2 * (int32_t)v - 255;
    uint32_t r = channel(yy + chroma(1.402f, xv, 32), 6, 255);
    uint32_t g = channel(yy + chroma(0.34414f, -xu, 0) + chroma(0.71414f, -xv, 32), 6, 255);
    uint32_t b = channel(yy + chroma(1.772f, xu, 32), 6, 255);
    return r | (g << 8) | (b << 16) | UINT32_C(0xff000000);
}
static void pixel(uint8_t *out, unsigned width, qa_bytes gray, uint8_t y, uint8_t u, uint8_t v) {
    if (width == 1) *out = gray.data[y];
    else if (width == 2) qa_store_u16le(out, qa_roq_yuv565(y, u, v));
    else qa_store_u32le(out, qa_roq_yuv_rgba(y, u, v));
}
bool qa_roq_codebook_decode(qa_roq_codebooks *books, qa_bytes bytes, uint16_t flags,
                            qa_roq_book_mode mode, unsigned width, qa_bytes gray,
                            bool two_only, size_t *consumed, qa_error *error) {
    if (!books || !consumed || !bytes.data || mode < QA_ROQ_BOOK_NORMAL || mode > QA_ROQ_BOOK_DOUBLE ||
        (width != 1 && width != 2 && width != 4) || (width == 1 && (!gray.data || gray.size < 256))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid RoQ codebook configuration"); return false;
    }
    unsigned count2 = flags >> 8;
    if (!count2) count2 = 256;
    unsigned count4 = flags ? flags & 255u : 256;
    size_t yuv_bytes = (size_t)count2 * 6;
    if (two_only && !flags && bytes.size == yuv_bytes) count4 = 0;
    size_t required = yuv_bytes + (size_t)count4 * 4;
    if (bytes.size < required) { qa_error_set(error, QA_ERROR_FORMAT, bytes.size, "Truncated RoQ codebook"); return false; }
    unsigned cell_pixels = mode == QA_ROQ_BOOK_HALF ? 2 : mode == QA_ROQ_BOOK_NORMAL ? 4 : 8;
    size_t destination = 0;
    for (unsigned index = 0; index < count2; ++index) {
        const uint8_t *source = bytes.data + (size_t)index * 6;
        uint8_t rows[8] = {source[0], source[1], source[2], source[3]};
        if (mode == QA_ROQ_BOOK_HALF) rows[1] = source[2];
        else if (mode == QA_ROQ_BOOK_DOUBLE) {
            rows[2] = (uint8_t)((source[0] * 3u + source[2]) / 4);
            rows[3] = (uint8_t)((source[1] * 3u + source[3]) / 4);
            rows[4] = (uint8_t)((source[0] + source[2] * 3u) / 4);
            rows[5] = (uint8_t)((source[1] + source[3] * 3u) / 4);
            rows[6] = source[2]; rows[7] = source[3];
        }
        for (unsigned i = 0; i < cell_pixels; ++i) {
            pixel(books->book2 + destination, width, gray, rows[i], source[4], source[5]); destination += width;
        }
    }
    unsigned row_pixels = mode == QA_ROQ_BOOK_HALF ? 1 : 2, rows = cell_pixels / row_pixels;
    size_t destination4 = 0, destination8 = 0, cursor = yuv_bytes;
    for (unsigned half = 0; half < count4 * 2; ++half) {
        size_t left = (size_t)bytes.data[cursor++] * cell_pixels * width;
        size_t right = (size_t)bytes.data[cursor++] * cell_pixels * width;
        for (unsigned row = 0; row < rows; ++row) {
            size_t starts[2] = {left, right};
            for (unsigned side = 0; side < 2; ++side) for (unsigned column = 0; column < row_pixels; ++column) {
                const uint8_t *value = books->book2 + starts[side] + (size_t)column * width;
                memcpy(books->book4 + destination4, value, width); destination4 += width;
                memcpy(books->book8 + destination8, value, width);
                memcpy(books->book8 + destination8 + width, value, width); destination8 += width * 2;
            }
            size_t row_bytes = (size_t)row_pixels * width * 4;
            memcpy(books->book8 + destination8, books->book8 + destination8 - row_bytes, row_bytes);
            destination8 += row_bytes; left += row_pixels * width; right += row_pixels * width;
        }
    }
    *consumed = required; return true;
}
