/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <math.h>

bool qa_img_fail(qa_error *error, qa_status status, size_t offset, const char *message) {
    qa_error_set(error, status, offset, "%s", message);
    return false;
}
bool qa_img_size(uint32_t w, uint32_t h, size_t channels, size_t *size, qa_error *error) {
    if (!w || !h || !channels || (size_t)w > SIZE_MAX / h || (size_t)w * h > SIZE_MAX / channels)
        return qa_img_fail(error, QA_ERROR_FORMAT, 0, "Invalid or overflowing image dimensions");
    *size = (size_t)w * h * channels;
    return true;
}
bool qa_img_alloc(qa_buffer *out, size_t size, qa_error *error) {
    uint8_t *p = size ? malloc(size) : NULL;
    if (size && !p)
        return qa_img_fail(error, QA_ERROR_MEMORY, 0, "Image allocation failed");
    *out = (qa_buffer){p, size};
    return true;
}
bool qa_img_copy(qa_bytes bytes, qa_buffer *out, qa_error *error) {
    if (!qa_img_alloc(out, bytes.size, error))
        return false;
    if (bytes.size)
        memcpy(out->data, bytes.data, bytes.size);
    return true;
}
bool qa_img_input(qa_bytes bytes, size_t minimum, const void *out, qa_error *error) {
    if (!out || (!bytes.data && bytes.size))
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid image argument");
    if (bytes.size < minimum)
        return qa_img_fail(error, QA_ERROR_FORMAT, bytes.size, "Truncated image header");
    return true;
}
bool qa_img_rgba(const qa_image *image, qa_error *error) {
    size_t size;
    if (!image)
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Missing image");
    if (!qa_img_size(image->width, image->height, 4, &size, error))
        return false;
    if (!image->rgba.data || image->rgba.size != size)
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Expected complete RGBA8 image");
    return true;
}
bool qa_img_new(uint32_t w, uint32_t h, qa_image *out, qa_error *error) {
    size_t size;
    *out = (qa_image){.width = w, .height = h, .srgb_intent = -1};
    return qa_img_size(w, h, 4, &size, error) && qa_img_alloc(&out->rgba, size, error);
}
void qa_image_free(qa_image *image) {
    if (!image)
        return;
    qa_buffer_free(&image->rgba);
    qa_buffer_free(&image->indices);
    qa_buffer_free(&image->palette);
    *image = (qa_image){0};
}
bool qa_img_palette(qa_bytes rgb, qa_image *image, qa_error *error) {
    if (!rgb.size || rgb.size % 3 || rgb.size > 768)
        return qa_img_fail(error, QA_ERROR_FORMAT, 0, "Invalid RGB palette");
    image->palette_count = (uint32_t)(rgb.size / 3);
    if (!qa_img_alloc(&image->palette, image->palette_count * 4, error))
        return false;
    for (size_t i = 0; i < image->palette_count; ++i) {
        memcpy(image->palette.data + i * 4, rgb.data + i * 3, 3);
        image->palette.data[i * 4 + 3] = 255;
    }
    return true;
}
bool qa_image_decode(qa_bytes b, qa_image_format f, qa_image_policy p, qa_image *out, qa_error *e) {
    if (p != QA_IMAGE_FORMAT && p != QA_IMAGE_Q3)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "Unknown image policy");
    switch (f) {
    case QA_IMAGE_PCX:
        return qa_image_decode_pcx(b, p, out, e);
    case QA_IMAGE_QPIC:
        return qa_image_decode_qpic(b, out, e);
    case QA_IMAGE_TGA:
        return qa_image_decode_tga(b, p, out, e);
    case QA_IMAGE_BMP:
        return qa_image_decode_bmp(b, p, out, e);
    case QA_IMAGE_PNG:
        return qa_image_decode_png(b, out, e);
    case QA_IMAGE_JPEG:
        return qa_image_decode_jpeg(b, out, e);
    }
    return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "Unknown image format");
}
bool qa_image_expand_indexed(const qa_indexed_level *image, qa_bytes palette,
                             const qa_palette_options *options, qa_image *out, qa_error *error) {
    qa_palette_options defaults = {-1, -1, -1, NULL, QA_PALETTE_COMBINED};
    const qa_palette_options *p = options ? options : &defaults;
    size_t count;
    if (!image || !out || !palette.data || palette.size != 768 || p->transparent_index < -1 ||
        p->transparent_index > 255 || p->fullbright_first < -1 || p->fullbright_first > 256 ||
        p->fullbright_last < -1 || p->fullbright_last > 255 || p->layer < QA_PALETTE_COMBINED ||
        p->layer > QA_PALETTE_FULLBRIGHT)
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid palette expansion arguments");
    if (!qa_img_size(image->width, image->height, 1, &count, error))
        return false;
    if (!image->indices.data || image->indices.size != count)
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Incomplete indexed image");
    qa_image result;
    if (!qa_img_new(image->width, image->height, &result, error))
        return false;
    for (size_t i = 0; i < count; i++) {
        unsigned original = image->indices.data[i],
                 index = p->translation ? p->translation[original] : original;
        bool full = p->fullbright_first >= 0 && (int)index >= p->fullbright_first &&
                    (int)index <= p->fullbright_last;
        bool visible =
            (int)original != p->transparent_index &&
            (p->layer == QA_PALETTE_COMBINED || (p->layer == QA_PALETTE_FULLBRIGHT ? full : !full));
        memcpy(result.rgba.data + i * 4, palette.data + index * 3, 3);
        result.rgba.data[i * 4 + 3] = visible ? 255 : 0;
    }
    *out = result;
    return true;
}
bool qa_image_mip(const qa_image *in, qa_mip_filter filter, qa_image *out, qa_error *error) {
    if (!out || (filter != QA_MIP_BOX && filter != QA_MIP_Q3_WEIGHTED))
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid mip arguments");
    if (!qa_img_rgba(in, error))
        return false;
    uint32_t w = in->width > 1 ? in->width / 2 : 1, h = in->height > 1 ? in->height / 2 : 1;
    if (filter == QA_MIP_Q3_WEIGHTED &&
        ((in->width & (in->width - 1)) || (in->height & (in->height - 1))))
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0,
                           "Q3 weighted mip requires power-of-two dimensions");
    qa_image result;
    if (!qa_img_new(w, h, &result, error))
        return false;
    if (filter == QA_MIP_Q3_WEIGHTED && (in->width == 1 || in->height == 1)) {
        memcpy(result.rgba.data, in->rgba.data, result.rgba.size);
        *out = result;
        return true;
    }
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            for (unsigned c = 0; c < 4; c++) {
                unsigned sum = 0;
                if (filter == QA_MIP_Q3_WEIGHTED) {
                    for (int dy = -1; dy <= 2; dy++)
                        for (int dx = -1; dx <= 2; dx++) {
                            uint32_t sx = (x * 2 + (uint32_t)dx) & (in->width - 1),
                                     sy = (y * 2 + (uint32_t)dy) & (in->height - 1);
                            unsigned weight =
                                (dx == -1 || dx == 2 ? 1U : 2U) * (dy == -1 || dy == 2 ? 1U : 2U);
                            sum += weight * in->rgba.data[((size_t)sy * in->width + sx) * 4 + c];
                        }
                    sum /= 36;
                } else {
                    uint32_t x2 = x * 2 + 1 < in->width ? x * 2 + 1 : x * 2,
                             y2 = y * 2 + 1 < in->height ? y * 2 + 1 : y * 2;
                    sum = (unsigned)in->rgba.data[((size_t)y * 2 * in->width + x * 2) * 4 + c] +
                          in->rgba.data[((size_t)y * 2 * in->width + x2) * 4 + c] +
                          in->rgba.data[((size_t)y2 * in->width + x * 2) * 4 + c] +
                          in->rgba.data[((size_t)y2 * in->width + x2) * 4 + c];
                    sum /= 4;
                }
                result.rgba.data[((size_t)y * w + x) * 4 + c] = (uint8_t)sum;
            }
    *out = result;
    return true;
}
void qa_mip_chain_free(qa_mip_chain *chain) {
    if (!chain)
        return;
    for (size_t i = 0; i < chain->count; i++)
        qa_image_free(&chain->levels[i]);
    free(chain->levels);
    *chain = (qa_mip_chain){0};
}
bool qa_image_mip_chain(const qa_image *image, qa_mip_filter filter, qa_mip_chain *out,
                        qa_error *error) {
    if (!out || (filter != QA_MIP_BOX && filter != QA_MIP_Q3_WEIGHTED))
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid mip chain arguments");
    if (!qa_img_rgba(image, error))
        return false;
    qa_mip_chain chain = {0};
    uint32_t width = image->width, height = image->height;
    size_t count = 0;
    while (width > 1 || height > 1) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        count++;
    }
    if (count) {
        chain.levels = calloc(count, sizeof(*chain.levels));
        if (!chain.levels)
            return qa_img_fail(error, QA_ERROR_MEMORY, 0, "Mip chain allocation failed");
    }
    const qa_image *previous = image;
    for (size_t i = 0; i < count; i++) {
        if (!qa_image_mip(previous, filter, &chain.levels[i], error)) {
            qa_mip_chain_free(&chain);
            return false;
        }
        chain.count++;
        previous = &chain.levels[i];
    }
    *out = chain;
    return true;
}
static uint32_t resample_row(uint32_t row, uint32_t input_height, uint32_t output_height,
                             unsigned quarter) {
    uint64_t numerator = (uint64_t)row * input_height;
    uint64_t whole = numerator / output_height;
    uint64_t remainder = numerator % output_height;
    return (uint32_t)(whole + (remainder * 4 + (uint64_t)quarter * input_height) /
                                  ((uint64_t)output_height * 4));
}
bool qa_image_resample(const qa_image *in, uint32_t w, uint32_t h, qa_resample_filter filter,
                       qa_image *out, qa_error *error) {
    if (!out || (filter != QA_RESAMPLE_Q1 && filter != QA_RESAMPLE_Q2_Q3))
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid resample arguments");
    if (!qa_img_rgba(in, error))
        return false;
    qa_image result;
    if (!qa_img_new(w, h, &result, error))
        return false;
    uint64_t step = ((uint64_t)in->width << 16) / w;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t x1 =
                (uint32_t)((step / (filter == QA_RESAMPLE_Q1 ? 2 : 4) + (uint64_t)x * step) >> 16);
            uint32_t y1 = resample_row(y, in->height, h, filter == QA_RESAMPLE_Q1 ? 0 : 1);
            uint32_t x2 = (uint32_t)((3 * (step / 4) + (uint64_t)x * step) >> 16),
                     y2 = resample_row(y, in->height, h, 3);
            for (unsigned c = 0; c < 4; c++) {
                unsigned value = in->rgba.data[((size_t)y1 * in->width + x1) * 4 + c];
                if (filter == QA_RESAMPLE_Q2_Q3)
                    value = (value + in->rgba.data[((size_t)y1 * in->width + x2) * 4 + c] +
                             in->rgba.data[((size_t)y2 * in->width + x1) * 4 + c] +
                             in->rgba.data[((size_t)y2 * in->width + x2) * 4 + c]) /
                            4;
                result.rgba.data[((size_t)y * w + x) * 4 + c] = (uint8_t)value;
            }
        }
    *out = result;
    return true;
}
bool qa_image_gamma_table(const qa_gamma_options *p, uint8_t table[256], qa_error *error) {
    if (!p || !table || p->profile < QA_GAMMA_Q1 || p->profile > QA_GAMMA_Q3 ||
        !isfinite(p->gamma) || p->gamma <= 0 ||
        (p->profile == QA_GAMMA_Q3 && p->overbright_bits > 2))
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid gamma profile");
    for (unsigned i = 0; i < 256; i++) {
        double v;
        if (p->profile == QA_GAMMA_Q1)
            v = (float)((float)pow((i + 1) / 256.0, p->gamma) * 255.0f + 0.5f);
        else if (p->gamma == 1)
            v = i;
        else if (p->profile == QA_GAMMA_Q3)
            v = 255 * pow((float)i / 255, 1.0f / p->gamma) + 0.5;
        else
            v = (float)(255 * pow((i + 0.5) / 255.5, p->gamma) + 0.5);
        v = floor(v);
        if (p->profile == QA_GAMMA_Q3)
            v *= 1U << p->overbright_bits;
        table[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    return true;
}
bool qa_image_apply_gamma(const qa_image *in, const qa_gamma_options *p, qa_image *out,
                          qa_error *error) {
    uint8_t table[256];
    if (!out || !qa_img_rgba(in, error) || !qa_image_gamma_table(p, table, error))
        return false;
    bool uses_intensity = p->profile != QA_GAMMA_Q1 && !p->only_gamma;
    if (uses_intensity && !isfinite(p->intensity))
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid intensity");
    float intensity = uses_intensity ? fmaxf(1, p->intensity) : 1;
    qa_image result;
    if (!qa_img_new(in->width, in->height, &result, error))
        return false;
    for (size_t i = 0; i < in->rgba.size; i++) {
        float v = in->rgba.data[i] * intensity;
        result.rgba.data[i] = i % 4 == 3 ? in->rgba.data[i] : table[(unsigned)(v > 255 ? 255 : v)];
    }
    *out = result;
    return true;
}
