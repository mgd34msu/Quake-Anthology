/* SPDX-License-Identifier: GPL-2.0-or-later
 * Format and source-policy behavior derived from Anthology's indexed.ts. */
#include "internal.h"

bool qa_image_decode_qpic(qa_bytes b, qa_image *out, qa_error *e) {
    if (!qa_img_input(b, 8, out, e))
        return false;
    int32_t w = qa_load_i32le(b.data), h = qa_load_i32le(b.data + 4);
    size_t count;
    if (w <= 0 || h <= 0 || !qa_img_size((uint32_t)w, (uint32_t)h, 1, &count, e))
        return qa_img_fail(e, QA_ERROR_FORMAT, 0, "Invalid QPIC dimensions");
    if (!qa_img_range(b, 8, count))
        return qa_img_fail(e, QA_ERROR_FORMAT, 8, "Truncated QPIC pixels");
    qa_image result = {
        .width = (uint32_t)w, .height = (uint32_t)h, .index_bytes = 1, .srgb_intent = -1};
    if (!qa_img_copy((qa_bytes){b.data + 8, count}, &result.indices, e))
        return false;
    *out = result;
    return true;
}
bool qa_image_decode_pcx(qa_bytes b, qa_image_policy policy, qa_image *out, qa_error *e) {
    if (!qa_img_input(b, 128, out, e))
        return false;
    if (policy != QA_IMAGE_FORMAT && policy != QA_IMAGE_Q3)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "Invalid PCX policy");
    if (b.data[0] != 10 || b.data[1] != 5 || b.data[2] != 1 || b.data[3] != 8)
        return qa_img_fail(e, QA_ERROR_UNSUPPORTED, 0, "Expected version 5 single-plane RLE8 PCX");
    unsigned xmax = qa_load_u16le(b.data + 8), ymax = qa_load_u16le(b.data + 10);
    unsigned xmin = policy == QA_IMAGE_Q3 ? 0 : qa_load_u16le(b.data + 4),
             ymin = policy == QA_IMAGE_Q3 ? 0 : qa_load_u16le(b.data + 6);
    if (xmax < xmin || ymax < ymin)
        return qa_img_fail(e, QA_ERROR_FORMAT, 4, "Invalid PCX bounds");
    uint32_t w = xmax - xmin + 1, h = ymax - ymin + 1;
    size_t count;
    if (policy == QA_IMAGE_Q3 && (xmax >= 1024 || ymax >= 1024))
        return qa_img_fail(e, QA_ERROR_UNSUPPORTED, 8, "Q3 PCX dimension policy rejected image");
    uint32_t stride = policy == QA_IMAGE_Q3 ? w : qa_load_u16le(b.data + 66);
    if (policy == QA_IMAGE_FORMAT && (b.data[65] != 1 || stride < w))
        return qa_img_fail(e, QA_ERROR_FORMAT, 65, "PCX requires one complete plane");
    if (!qa_img_size(w, h, 1, &count, e))
        return false;
    size_t palette_at = b.size >= 769 ? b.size - 769 : 0, end = b.size;
    bool palette = policy == QA_IMAGE_Q3 || (palette_at >= 128 && b.data[palette_at] == 12);
    if (policy == QA_IMAGE_Q3) {
        if (b.size < 768)
            return qa_img_fail(e, QA_ERROR_FORMAT, b.size, "Truncated Q3 PCX palette");
        palette_at = b.size - 768;
    } else if (palette)
        end = palette_at++;
    if (end < 128 || ((uint64_t)stride * h + 62) / 63 > end - 128)
        return qa_img_fail(e, QA_ERROR_FORMAT, 128, "Truncated PCX scanlines");
    qa_image result = {
        .width = w, .height = h, .index_bytes = 1, .bit_depth = 8, .srgb_intent = -1};
    if (!qa_img_alloc(&result.indices, count, e))
        return false;
    size_t at = 128;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t x = 0;
        while (x < stride) {
            if (at >= end)
                goto truncated;
            unsigned value = b.data[at++], run = 1;
            if ((value & 192) == 192) {
                run = value & 63;
                if (at >= end)
                    goto truncated;
                value = b.data[at++];
            }
            if (!run) {
                qa_img_fail(e, QA_ERROR_FORMAT, at, "Zero-length PCX packet");
                goto fail;
            }
            if (policy == QA_IMAGE_Q3) {
                size_t dst = (size_t)y * w + x;
                if (run > count - dst) {
                    qa_img_fail(e, QA_ERROR_FORMAT, at, "PCX packet exceeds image");
                    goto fail;
                }
                memset(result.indices.data + dst, (int)value, run);
            } else {
                if (run > stride - x) {
                    qa_img_fail(e, QA_ERROR_FORMAT, at, "PCX packet exceeds scanline");
                    goto fail;
                }
                if (x < w)
                    memset(result.indices.data + (size_t)y * w + x, (int)value,
                           run < w - x ? run : w - x);
            }
            x += run;
        }
    }
    if (palette) {
        if (!qa_img_palette((qa_bytes){b.data + palette_at, 768}, &result, e))
            goto fail;
        size_t rgba_size;
        if (!qa_img_size(w, h, 4, &rgba_size, e) || !qa_img_alloc(&result.rgba, rgba_size, e))
            goto fail;
        for (size_t i = 0; i < count; i++)
            memcpy(result.rgba.data + i * 4, result.palette.data + result.indices.data[i] * 4, 4);
    }
    *out = result;
    return true;
truncated:
    qa_img_fail(e, QA_ERROR_FORMAT, at, "Truncated PCX packet");
fail:
    qa_image_free(&result);
    return false;
}
void qa_mip_texture_free(qa_mip_texture *t) {
    if (!t)
        return;
    for (unsigned i = 0; i < 4; i++)
        qa_buffer_free(&t->levels[i].indices);
    *t = (qa_mip_texture){0};
}
static bool decode_mip(qa_bytes b, bool wal, qa_mip_texture *out, qa_error *e) {
    size_t header = wal ? 100 : 40, n = wal ? 32 : 16, count;
    if (!qa_img_input(b, header, out, e))
        return false;
    qa_mip_texture t = {0};
    memcpy(t.name, b.data, n);
    t.width = qa_load_u32le(b.data + n);
    t.height = qa_load_u32le(b.data + n + 4);
    if (!qa_img_size(t.width, t.height, 1, &count, e))
        return false;
    if (!wal && (t.width % 16 || t.height % 16))
        return qa_img_fail(e, QA_ERROR_FORMAT, 16, "Q1 mip dimensions must be multiples of 16");
    uint32_t offsets[4];
    bool external = true;
    for (unsigned i = 0; i < 4; i++) {
        offsets[i] = qa_load_u32le(b.data + n + 8 + i * 4);
        external = external && offsets[i] == 0;
    }
    if (!wal && external) {
        t.external = true;
        *out = t;
        return true;
    }
    if (wal) {
        memcpy(t.animation, b.data + 56, 32);
        t.flags = qa_load_i32le(b.data + 88);
        t.contents = qa_load_i32le(b.data + 92);
        t.value = qa_load_i32le(b.data + 96);
    }
    for (unsigned i = 0; i < 4; i++) {
        uint32_t w = t.width >> i, h = t.height >> i;
        w = w ? w : 1;
        h = h ? h : 1;
        if (!qa_img_size(w, h, 1, &count, e) || offsets[i] < header ||
            !qa_img_range(b, offsets[i], count)) {
            qa_img_fail(e, QA_ERROR_FORMAT, offsets[i], "Invalid mip level extent");
            qa_mip_texture_free(&t);
            return false;
        }
        t.levels[i].width = w;
        t.levels[i].height = h;
        if (!qa_img_copy((qa_bytes){b.data + offsets[i], count}, &t.levels[i].indices, e)) {
            qa_mip_texture_free(&t);
            return false;
        }
    }
    *out = t;
    return true;
}
bool qa_image_decode_mip(qa_bytes b, qa_mip_texture *out, qa_error *e) {
    return decode_mip(b, false, out, e);
}
bool qa_image_decode_wal(qa_bytes b, qa_mip_texture *out, qa_error *e) {
    return decode_mip(b, true, out, e);
}
bool qa_image_decode_lit(qa_bytes b, size_t expected, qa_buffer *out, qa_error *e) {
    if (!qa_img_input(b, 8, out, e))
        return false;
    if (memcmp(b.data, "QLIT", 4) || qa_load_u32le(b.data + 4) != 1 || (b.size - 8) % 3 ||
        (expected != SIZE_MAX && expected != (b.size - 8) / 3))
        return qa_img_fail(e, QA_ERROR_FORMAT, 0, "Invalid QLIT v1 header or sample count");
    return qa_img_copy((qa_bytes){b.data + 8, b.size - 8}, out, e);
}
bool qa_image_decode_colormap(qa_bytes b, qa_buffer *out, unsigned *first, qa_error *e) {
    if (!first)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "Missing fullbright output");
    if (!qa_img_input(b, 16385, out, e))
        return false;
    if (!qa_img_copy((qa_bytes){b.data, 16384}, out, e))
        return false;
    *first = 256U - b.data[16384];
    return true;
}
bool qa_image_player_translation(unsigned top, unsigned bottom, uint8_t table[256], qa_error *e) {
    if (!table || top > 13 || bottom > 13)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "Player colors require indices 0..13");
    for (unsigned i = 0; i < 256; i++)
        table[i] = (uint8_t)i;
    for (unsigned i = 0; i < 16; i++) {
        table[16 + i] = (uint8_t)(top * 16 + (top < 8 ? i : 15 - i));
        table[96 + i] = (uint8_t)(bottom * 16 + (bottom < 8 ? i : 15 - i));
    }
    return true;
}
bool qa_image_encode_pcx(const qa_image *in, qa_bytes palette, qa_buffer *out, qa_error *e) {
    size_t count;
    if (!in || !out || !palette.data || palette.size != 768 || in->index_bytes != 1 ||
        in->width > 65534 || in->height > 65536)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "Invalid PCX encoder arguments");
    if (!qa_img_size(in->width, in->height, 1, &count, e))
        return false;
    if (!in->indices.data || in->indices.size != count)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "Incomplete PCX input indices");
    size_t stride = ((size_t)in->width + 1) & ~(size_t)1;
    if (stride > (SIZE_MAX - 897) / 2 / in->height)
        return qa_img_fail(e, QA_ERROR_MEMORY, 0, "PCX output overflows");
    qa_buffer b;
    if (!qa_img_alloc(&b, 897 + stride * in->height * 2, e))
        return false;
    memset(b.data, 0, 128);
    b.data[0] = 10;
    b.data[1] = 5;
    b.data[2] = 1;
    b.data[3] = 8;
    qa_store_u16le(b.data + 8, (uint16_t)(in->width - 1));
    qa_store_u16le(b.data + 10, (uint16_t)(in->height - 1));
    qa_store_u16le(b.data + 12, (uint16_t)in->width);
    qa_store_u16le(b.data + 14, (uint16_t)(in->height > 65535 ? 65535 : in->height));
    b.data[65] = 1;
    qa_store_u16le(b.data + 66, (uint16_t)stride);
    qa_store_u16le(b.data + 68, 1);
    size_t at = 128;
    for (uint32_t y = 0; y < in->height; y++)
        for (size_t x = 0; x < stride; x++) {
            uint8_t v = x < in->width ? in->indices.data[(size_t)y * in->width + x] : 0;
            if ((v & 192) == 192)
                b.data[at++] = 193;
            b.data[at++] = v;
        }
    b.data[at++] = 12;
    memcpy(b.data + at, palette.data, 768);
    b.size = at + 768;
    *out = b;
    return true;
}
