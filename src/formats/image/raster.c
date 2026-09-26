/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"

typedef struct image_reader {
    qa_bytes bytes;
    size_t at;
    qa_error *error;
} image_reader;
static const uint8_t *take(image_reader *r, size_t size) {
    if (!qa_img_range(r->bytes, r->at, size)) {
        qa_img_fail(r->error, QA_ERROR_FORMAT, r->at, "Truncated image pixels");
        return NULL;
    }
    const uint8_t *p = r->bytes.data + r->at;
    r->at += size;
    return p;
}
static bool tga_color(image_reader *r, unsigned bits, unsigned descriptor, uint8_t rgba[4]) {
    const uint8_t *p = take(r, (bits + 7) / 8);
    if (!p)
        return false;
    if (bits == 15 || bits == 16) {
        unsigned word = qa_load_u16le(p), red = (word >> 10) & 31, green = (word >> 5) & 31,
                 blue = word & 31;
        rgba[0] = (uint8_t)((red << 3) | (red >> 2));
        rgba[1] = (uint8_t)((green << 3) | (green >> 2));
        rgba[2] = (uint8_t)((blue << 3) | (blue >> 2));
        rgba[3] = (bits == 16 && (descriptor & 15) && !(word & 32768)) ? 0 : 255;
    } else {
        rgba[0] = p[2];
        rgba[1] = p[1];
        rgba[2] = p[0];
        rgba[3] = bits == 32 ? p[3] : 255;
    }
    return true;
}
static bool tga_pixel(image_reader *r, bool indexed, bool gray, unsigned depth, qa_image *im,
                      unsigned *index, uint8_t rgba[4]) {
    if (indexed) {
        const uint8_t *p = take(r, depth / 8);
        if (!p)
            return false;
        *index = depth == 8 ? p[0] : qa_load_u16le(p);
        if (*index < im->palette_first || *index - im->palette_first >= im->palette_count)
            return qa_img_fail(r->error, QA_ERROR_FORMAT, r->at, "TGA palette index out of range");
        memcpy(rgba, im->palette.data + (*index - im->palette_first) * 4, 4);
        return true;
    }
    if (gray) {
        const uint8_t *p = take(r, depth / 8);
        if (!p)
            return false;
        rgba[0] = rgba[1] = rgba[2] = p[0];
        rgba[3] = depth == 16 ? p[1] : 255;
        return true;
    }
    return tga_color(r, depth, im->descriptor, rgba);
}
bool qa_image_decode_tga(qa_bytes b, qa_image_policy policy, qa_image *out, qa_error *e) {
    if (!qa_img_input(b, 18, out, e))
        return false;
    if (policy != QA_IMAGE_FORMAT && policy != QA_IMAGE_Q3)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "Invalid TGA policy");
    unsigned type = b.data[2], depth = b.data[16], descriptor = b.data[17], map_depth = b.data[7];
    uint32_t w = qa_load_u16le(b.data + 12), h = qa_load_u16le(b.data + 14);
    bool indexed = type == 1 || type == 9, gray = type == 3 || type == 11, rle = type >= 9,
         q3 = policy == QA_IMAGE_Q3;
    if (q3) {
        if ((type != 2 && type != 3 && type != 10) || b.data[1] != 0 ||
            (depth != 24 && depth != 32 && !(type == 3 && depth == 8)))
            return qa_img_fail(e, QA_ERROR_UNSUPPORTED, 0, "Unsupported Q3 TGA header");
        gray = depth == 8;
    } else if ((type != 1 && type != 2 && type != 3 && type != 9 && type != 10 && type != 11) ||
               (descriptor & 192) || b.data[1] != (indexed ? 1 : 0) ||
               (indexed ? (depth != 8 && depth != 16)
                : gray  ? (depth != 8 && depth != 16)
                        : (depth != 16 && depth != 24 && depth != 32)))
        return qa_img_fail(e, QA_ERROR_UNSUPPORTED, 0, "Unsupported TGA header");
    if (indexed && (map_depth != 15 && map_depth != 16 && map_depth != 24 && map_depth != 32))
        return qa_img_fail(e, QA_ERROR_UNSUPPORTED, 7, "Unsupported TGA palette depth");
    size_t count;
    if (!qa_img_size(w, h, 1, &count, e))
        return false;
    size_t at = 18U + b.data[0], pal_count = indexed ? qa_load_u16le(b.data + 5) : 0,
           pal_bytes = pal_count * ((map_depth + 7) / 8);
    if (!qa_img_range(b, at, pal_bytes))
        return qa_img_fail(e, QA_ERROR_FORMAT, at, "Truncated TGA palette");
    size_t pixel_at = at + pal_bytes,
           min_bytes = rle ? ((count + 127) / 128) * (depth / 8 + 1) : count * (depth / 8);
    if (!qa_img_range(b, pixel_at, min_bytes))
        return qa_img_fail(e, QA_ERROR_FORMAT, pixel_at, "Truncated TGA pixel data");
    qa_image im;
    if (!qa_img_new(w, h, &im, e))
        return false;
    im.descriptor = (uint8_t)descriptor;
    im.bit_depth = (uint8_t)depth;
    image_reader r = {b, at, e};
    if (indexed) {
        im.index_bytes = 2;
        im.palette_count = (uint32_t)pal_count;
        im.palette_first = qa_load_u16le(b.data + 3);
        if (!qa_img_alloc(&im.indices, count * 2, e) ||
            !qa_img_alloc(&im.palette, pal_count * 4, e))
            goto fail;
        for (size_t i = 0; i < pal_count; i++)
            if (!tga_color(&r, map_depth, descriptor, im.palette.data + i * 4))
                goto fail;
    }
    size_t pixel = 0;
    while (pixel < count) {
        unsigned packet = 0, run = 1;
        if (rle) {
            const uint8_t *p = take(&r, 1);
            if (!p)
                goto fail;
            packet = *p;
            run = (packet & 127) + 1;
        }
        if (run > count - pixel) {
            if (q3)
                run = (unsigned)(count - pixel);
            else {
                qa_img_fail(e, QA_ERROR_FORMAT, r.at, "TGA packet exceeds pixel count");
                goto fail;
            }
        }
        uint8_t rgba[4];
        unsigned index = 0;
        for (unsigned i = 0; i < run; i++, pixel++) {
            if ((i == 0 || !(packet & 128)) &&
                !tga_pixel(&r, indexed, gray, depth, &im, &index, rgba))
                goto fail;
            uint32_t x = (uint32_t)(pixel % w), y = (uint32_t)(pixel / w);
            if (!q3 && (descriptor & 16))
                x = w - x - 1;
            if (q3 || !(descriptor & 32))
                y = h - y - 1;
            size_t dst = (size_t)y * w + x;
            memcpy(im.rgba.data + dst * 4, rgba, 4);
            if (indexed)
                qa_store_u16le(im.indices.data + dst * 2, (uint16_t)index);
        }
    }
    *out = im;
    return true;
fail:
    qa_image_free(&im);
    return false;
}
bool qa_image_decode_bmp(qa_bytes b, qa_image_policy policy, qa_image *out, qa_error *e) {
    if (!qa_img_input(b, 54, out, e))
        return false;
    if (policy != QA_IMAGE_FORMAT && policy != QA_IMAGE_Q3)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "Invalid BMP policy");
    bool q3 = policy == QA_IMAGE_Q3;
    if (q3 ? (b.data[0] != 'B' && b.data[1] != 'M') : (b.data[0] != 'B' || b.data[1] != 'M'))
        return qa_img_fail(e, QA_ERROR_FORMAT, 0, "Expected Windows BMP");
    int32_t sw = qa_load_i32le(b.data + 18), sh = qa_load_i32le(b.data + 22);
    unsigned depth = qa_load_u16le(b.data + 28), colors = qa_load_u32le(b.data + 46);
    if (qa_load_u32le(b.data + 30) != 0 ||
        (!q3 && (qa_load_u32le(b.data + 14) != 40 || qa_load_u16le(b.data + 26) != 1)))
        return qa_img_fail(e, QA_ERROR_UNSUPPORTED, 14,
                           "BMP requires BITMAPINFOHEADER and uncompressed pixels");
    if (q3 && qa_load_u32le(b.data + 2) != b.size)
        return qa_img_fail(e, QA_ERROR_FORMAT, 2, "Q3 BMP file size mismatch");
    if (sw < 0 || sh == INT32_MIN || (!q3 && (!sw || !sh)))
        return qa_img_fail(e, QA_ERROR_FORMAT, 18, "Invalid BMP dimensions");
    uint32_t w = (uint32_t)sw, h = (uint32_t)(sh < 0 ? -sh : sh);
    size_t palette_count = depth == 8 ? (q3 || !colors ? 256 : colors) : 0;
    if (palette_count > 256 || !qa_img_range(b, 54, palette_count * 4))
        return qa_img_fail(e, QA_ERROR_FORMAT, 54, "Invalid BMP palette");
    if (q3 && (!w || !h)) {
        *out = (qa_image){.width = w, .height = h, .srgb_intent = -1};
        return true;
    }
    if (depth != 8 && depth != 24 && depth != 32)
        return qa_img_fail(e, QA_ERROR_UNSUPPORTED, 28, "BMP requires 8/24/32-bit pixels");
    size_t count;
    if (!qa_img_size(w, h, 4, &count, e))
        return false;
    size_t row = (size_t)w * (depth / 8), stride = q3 ? row : (row + 3) & ~(size_t)3;
    size_t offset = q3 ? 54 + palette_count * 4 : qa_load_u32le(b.data + 10);
    if (offset < 54 + palette_count * 4 || stride > SIZE_MAX / h ||
        !qa_img_range(b, offset, stride * h))
        return qa_img_fail(e, QA_ERROR_FORMAT, offset, "Invalid BMP pixel extent");
    qa_image im;
    if (!qa_img_new(w, h, &im, e))
        return false;
    im.bit_depth = (uint8_t)depth;
    if (depth == 8) {
        im.index_bytes = 1;
        im.palette_count = (uint32_t)palette_count;
        if (!qa_img_alloc(&im.palette, palette_count * 4, e) ||
            !qa_img_alloc(&im.indices, count / 4, e))
            goto fail;
        for (size_t i = 0; i < palette_count; i++) {
            const uint8_t *p = b.data + 54 + i * 4;
            uint8_t *d = im.palette.data + i * 4;
            d[0] = p[2];
            d[1] = p[1];
            d[2] = p[0];
            d[3] = 255;
        }
    }
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            size_t src = offset + (size_t)y * stride + (size_t)x * (depth / 8),
                   dst = ((size_t)(!q3 && sh < 0 ? y : h - y - 1) * w + x);
            const uint8_t *p = b.data + src;
            uint8_t *d = im.rgba.data + dst * 4;
            if (depth == 8) {
                if (*p >= palette_count) {
                    qa_img_fail(e, QA_ERROR_FORMAT, src, "BMP palette index out of range");
                    goto fail;
                }
                im.indices.data[dst] = *p;
                memcpy(d, im.palette.data + *p * 4, 4);
            } else {
                d[0] = p[2];
                d[1] = p[1];
                d[2] = p[0];
                d[3] = depth == 32 ? p[3] : 255;
            }
        }
    *out = im;
    return true;
fail:
    qa_image_free(&im);
    return false;
}
bool qa_image_encode_tga(const qa_image *in, qa_buffer *out, qa_error *e) {
    if (!out || !qa_img_rgba(in, e))
        return false;
    if (in->width > 65535 || in->height > 65535 || in->rgba.size > SIZE_MAX - 18)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "TGA output dimensions exceed format");
    qa_buffer b;
    if (!qa_img_alloc(&b, 18 + in->rgba.size, e))
        return false;
    memset(b.data, 0, 18);
    b.data[2] = 2;
    qa_store_u16le(b.data + 12, (uint16_t)in->width);
    qa_store_u16le(b.data + 14, (uint16_t)in->height);
    b.data[16] = 32;
    b.data[17] = 40;
    for (size_t i = 0; i < in->rgba.size; i += 4) {
        const uint8_t *p = in->rgba.data + i;
        uint8_t *d = b.data + 18 + i;
        d[0] = p[2];
        d[1] = p[1];
        d[2] = p[0];
        d[3] = p[3];
    }
    *out = b;
    return true;
}
