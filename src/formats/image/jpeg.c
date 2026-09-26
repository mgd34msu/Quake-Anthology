/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <setjmp.h>
#include <stdio.h>

#include <jpeglib.h>

typedef struct jpeg_error_context {
    struct jpeg_error_mgr base;
    jmp_buf jump;
    qa_error *error;
} jpeg_error_context;
typedef struct jpeg_decode_context {
    struct jpeg_decompress_struct decoder;
    jpeg_error_context error;
    qa_image image;
    uint8_t *row;
} jpeg_decode_context;
typedef struct jpeg_encode_context {
    struct jpeg_compress_struct encoder;
    jpeg_error_context error;
    unsigned char *bytes;
    unsigned long size;
    uint8_t *row;
} jpeg_encode_context;
static void jpeg_error_handler(j_common_ptr common) {
    jpeg_error_context *context = (jpeg_error_context *)common->err;
    char message[JMSG_LENGTH_MAX];
    common->err->format_message(common, message);
    qa_error_set(context->error, QA_ERROR_FORMAT, 0, "JPEG: %s", message);
    longjmp(context->jump, 1);
}
static void jpeg_warning_handler(j_common_ptr common) { (void)common; }
bool qa_image_decode_jpeg(qa_bytes b, qa_image *out, qa_error *e) {
    if (!qa_img_input(b, 2, out, e))
        return false;
    if (b.size > ULONG_MAX)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "JPEG input exceeds library size type");
    jpeg_decode_context *c = calloc(1, sizeof(*c));
    if (!c)
        return qa_img_fail(e, QA_ERROR_MEMORY, 0, "JPEG context allocation failed");
    c->decoder.err = jpeg_std_error(&c->error.base);
    c->error.base.error_exit = jpeg_error_handler;
    c->error.base.output_message = jpeg_warning_handler;
    c->error.error = e;
    if (setjmp(c->error.jump))
        goto fail;
    jpeg_create_decompress(&c->decoder);
    jpeg_mem_src(&c->decoder, b.data, (unsigned long)b.size);
    jpeg_read_header(&c->decoder, TRUE);
    bool cmyk = c->decoder.jpeg_color_space == JCS_CMYK || c->decoder.jpeg_color_space == JCS_YCCK;
    c->decoder.out_color_space = cmyk ? JCS_CMYK : JCS_RGB;
    c->decoder.dct_method = JDCT_FLOAT;
    jpeg_start_decompress(&c->decoder);
    if (!qa_img_new(c->decoder.output_width, c->decoder.output_height, &c->image, e))
        goto fail;
    size_t channels = cmyk ? 4 : 3;
    if (c->decoder.output_components != (int)channels) {
        qa_img_fail(e, QA_ERROR_FORMAT, 0, "Unexpected JPEG output channels");
        goto fail;
    }
    c->row = malloc((size_t)c->image.width * channels);
    if (!c->row) {
        qa_img_fail(e, QA_ERROR_MEMORY, 0, "JPEG row allocation failed");
        goto fail;
    }
    while (c->decoder.output_scanline < c->decoder.output_height) {
        uint32_t y = c->decoder.output_scanline;
        JSAMPROW row = c->row;
        if (jpeg_read_scanlines(&c->decoder, &row, 1) != 1) {
            qa_img_fail(e, QA_ERROR_FORMAT, 0, "Incomplete JPEG scanlines");
            goto fail;
        }
        for (uint32_t x = 0; x < c->image.width; x++) {
            uint8_t *d = c->image.rgba.data + ((size_t)y * c->image.width + x) * 4;
            memcpy(d, c->row + (size_t)x * channels, 3);
            d[3] = 255;
        }
    }
    jpeg_finish_decompress(&c->decoder);
    jpeg_destroy_decompress(&c->decoder);
    free(c->row);
    *out = c->image;
    free(c);
    return true;
fail:
    jpeg_destroy_decompress(&c->decoder);
    free(c->row);
    qa_image_free(&c->image);
    free(c);
    return false;
}
bool qa_image_encode_jpeg(const qa_image *in, int quality, bool bottom_up, qa_buffer *out,
                          qa_error *e) {
    if (!out || !qa_img_rgba(in, e))
        return false;
    if (in->width > JPEG_MAX_DIMENSION || in->height > JPEG_MAX_DIMENSION)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, 0, "JPEG dimensions exceed format");
    jpeg_encode_context *c = calloc(1, sizeof(*c));
    if (!c)
        return qa_img_fail(e, QA_ERROR_MEMORY, 0, "JPEG context allocation failed");
    c->encoder.err = jpeg_std_error(&c->error.base);
    c->error.base.error_exit = jpeg_error_handler;
    c->error.base.output_message = jpeg_warning_handler;
    c->error.error = e;
    if (setjmp(c->error.jump))
        goto fail;
    jpeg_create_compress(&c->encoder);
    jpeg_mem_dest(&c->encoder, &c->bytes, &c->size);
    c->encoder.image_width = in->width;
    c->encoder.image_height = in->height;
    c->encoder.input_components = 3;
    c->encoder.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c->encoder);
    jpeg_set_quality(&c->encoder, quality, TRUE);
    c->encoder.dct_method = JDCT_FLOAT;
    jpeg_start_compress(&c->encoder, TRUE);
    c->row = malloc((size_t)in->width * 3);
    if (!c->row) {
        qa_img_fail(e, QA_ERROR_MEMORY, 0, "JPEG row allocation failed");
        goto fail;
    }
    while (c->encoder.next_scanline < c->encoder.image_height) {
        uint32_t y =
            bottom_up ? in->height - c->encoder.next_scanline - 1 : c->encoder.next_scanline;
        for (uint32_t x = 0; x < in->width; x++)
            memcpy(c->row + (size_t)x * 3, in->rgba.data + ((size_t)y * in->width + x) * 4, 3);
        JSAMPROW row = c->row;
        jpeg_write_scanlines(&c->encoder, &row, 1);
    }
    jpeg_finish_compress(&c->encoder);
    jpeg_destroy_compress(&c->encoder);
    free(c->row);
    *out = (qa_buffer){c->bytes, (size_t)c->size};
    free(c);
    return true;
fail:
    jpeg_destroy_compress(&c->encoder);
    free(c->bytes);
    free(c->row);
    free(c);
    return false;
}
