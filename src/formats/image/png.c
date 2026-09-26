#include "internal.h"
#include <png.h>

typedef struct png_context {
    qa_bytes input;
    size_t offset;
    qa_error *error;
    qa_image image;
    png_bytep *rows;
    qa_buffer encoded;
    size_t capacity;
} png_context;
static void png_error_handler(png_structp png, png_const_charp message) {
    png_context *c = png_get_error_ptr(png);
    qa_error_set(c->error, QA_ERROR_FORMAT, c->offset, "PNG: %s", message);
    png_longjmp(png, 1);
}
static void png_warning_handler(png_structp png, png_const_charp message) {
    (void)png;
    (void)message;
}
static void png_read_memory(png_structp png, png_bytep data, png_size_t count) {
    png_context *c = png_get_io_ptr(png);
    if (!qa_img_range(c->input, c->offset, count))
        png_error(png, "Truncated input");
    memcpy(data, c->input.data + c->offset, count);
    c->offset += count;
}
static void png_write_memory(png_structp png, png_bytep data, png_size_t count) {
    png_context *c = png_get_io_ptr(png);
    if (count > SIZE_MAX - c->encoded.size)
        png_error(png, "Encoded size overflows");
    size_t need = c->encoded.size + count;
    if (need > c->capacity) {
        size_t capacity = c->capacity ? c->capacity : 4096;
        while (capacity < need) {
            if (capacity > SIZE_MAX / 2) {
                capacity = need;
                break;
            }
            capacity *= 2;
        }
        uint8_t *p = realloc(c->encoded.data, capacity);
        if (!p)
            png_error(png, "Output allocation failed");
        c->encoded.data = p;
        c->capacity = capacity;
    }
    memcpy(c->encoded.data + c->encoded.size, data, count);
    c->encoded.size = need;
}
static void png_flush_memory(png_structp png) { (void)png; }
bool qa_image_decode_png(qa_bytes b, qa_image *out, qa_error *e) {
    if (!qa_img_input(b, 33, out, e))
        return false;
    if (png_sig_cmp(b.data, 0, 8))
        return qa_img_fail(e, QA_ERROR_FORMAT, 0, "Invalid PNG signature");
    png_context *c = calloc(1, sizeof(*c));
    if (!c)
        return qa_img_fail(e, QA_ERROR_MEMORY, 0, "PNG context allocation failed");
    c->input = b;
    c->error = e;
    c->image.srgb_intent = -1;
    png_structp png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, c, png_error_handler, png_warning_handler);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!info) {
        if (png)
            png_destroy_read_struct(&png, NULL, NULL);
        free(c);
        return qa_img_fail(e, QA_ERROR_MEMORY, 0, "PNG decoder allocation failed");
    }
    if (setjmp(png_jmpbuf(png)))
        goto fail;
    png_set_read_fn(png, c, png_read_memory);
    png_set_user_limits(png, PNG_UINT_31_MAX, PNG_UINT_31_MAX);
    png_set_crc_action(png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
    png_read_info(png, info);
    png_uint_32 w, h;
    int depth, type, interlace, compression, filter;
    png_get_IHDR(png, info, &w, &h, &depth, &type, &interlace, &compression, &filter);
    if (!qa_img_new(w, h, &c->image, e))
        goto fail;
    c->image.bit_depth = (uint8_t)depth;
    c->image.color_type = (uint8_t)type;
    c->image.has_gamma = png_get_gAMA(png, info, &c->image.gamma) != 0;
    if (!png_get_sRGB(png, info, &c->image.srgb_intent))
        c->image.srgb_intent = -1;
    if (type == PNG_COLOR_TYPE_PALETTE) {
        png_colorp palette;
        int colors;
        if (!png_get_PLTE(png, info, &palette, &colors) || colors <= 0 || colors > 256)
            png_error(png, "Invalid palette");
        c->image.palette_count = (uint32_t)colors;
        c->image.index_bytes = 1;
        if (!qa_img_alloc(&c->image.palette, (size_t)colors * 4, e) ||
            !qa_img_alloc(&c->image.indices, (size_t)w * h, e))
            goto fail;
        png_bytep alpha = NULL;
        int alpha_count = 0;
        png_color_16p transparent = NULL;
        png_get_tRNS(png, info, &alpha, &alpha_count, &transparent);
        for (int i = 0; i < colors; i++) {
            uint8_t *d = c->image.palette.data + (size_t)i * 4;
            d[0] = palette[i].red;
            d[1] = palette[i].green;
            d[2] = palette[i].blue;
            d[3] = i < alpha_count ? alpha[i] : 255;
        }
        if (depth < 8)
            png_set_packing(png);
    } else {
        if (depth == 16)
            png_set_strip_16(png);
        if (type == PNG_COLOR_TYPE_GRAY && depth < 8)
            png_set_expand_gray_1_2_4_to_8(png);
        bool transparency = png_get_valid(png, info, PNG_INFO_tRNS) != 0;
        if (transparency)
            png_set_tRNS_to_alpha(png);
        if (type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA)
            png_set_gray_to_rgb(png);
        if (!(type & PNG_COLOR_MASK_ALPHA) && !transparency)
            png_set_add_alpha(png, 255, PNG_FILLER_AFTER);
    }
    png_set_interlace_handling(png);
    png_read_update_info(png, info);
    size_t row_bytes = png_get_rowbytes(png, info),
           expected = (size_t)w * (type == PNG_COLOR_TYPE_PALETTE ? 1 : 4);
    if (row_bytes != expected || (uint64_t)h * sizeof(*c->rows) > SIZE_MAX)
        png_error(png, "Unexpected PNG row layout");
    c->rows = malloc((size_t)h * sizeof(*c->rows));
    if (!c->rows)
        png_error(png, "Row allocation failed");
    for (uint32_t y = 0; y < h; y++)
        c->rows[y] = (type == PNG_COLOR_TYPE_PALETTE ? c->image.indices.data : c->image.rgba.data) +
                     (size_t)y * row_bytes;
    png_read_image(png, c->rows);
    png_read_end(png, info);
    if (type == PNG_COLOR_TYPE_PALETTE)
        for (size_t i = 0; i < c->image.indices.size; i++) {
            unsigned index = c->image.indices.data[i];
            if (index >= c->image.palette_count)
                png_error(png, "Palette index out of range");
            memcpy(c->image.rgba.data + i * 4, c->image.palette.data + index * 4, 4);
        }
    png_destroy_read_struct(&png, &info, NULL);
    free(c->rows);
    *out = c->image;
    free(c);
    return true;
fail:
    png_destroy_read_struct(&png, &info, NULL);
    free(c->rows);
    qa_image_free(&c->image);
    free(c);
    return false;
}
bool qa_image_encode_png(const qa_image *in, qa_buffer *out, qa_error *e) {
    if (!out || !qa_img_rgba(in, e))
        return false;
    if (in->width > PNG_UINT_31_MAX || in->height > PNG_UINT_31_MAX)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "PNG dimensions exceed format");
    png_context *c = calloc(1, sizeof(*c));
    if (!c)
        return qa_img_fail(e, QA_ERROR_MEMORY, 0, "PNG context allocation failed");
    c->error = e;
    png_structp png =
        png_create_write_struct(PNG_LIBPNG_VER_STRING, c, png_error_handler, png_warning_handler);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!info) {
        if (png)
            png_destroy_write_struct(&png, NULL);
        free(c);
        return qa_img_fail(e, QA_ERROR_MEMORY, 0, "PNG encoder allocation failed");
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        qa_buffer_free(&c->encoded);
        free(c);
        return false;
    }
    png_set_write_fn(png, c, png_write_memory, png_flush_memory);
    png_set_IHDR(png, info, in->width, in->height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (uint32_t y = 0; y < in->height; y++)
        png_write_row(png, in->rgba.data + (size_t)y * in->width * 4);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    *out = c->encoded;
    free(c);
    return true;
}
