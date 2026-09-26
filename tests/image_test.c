#include "qa/binary.h"
#include "qa/image.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    } while (0)
#define BYTES(array) ((qa_bytes){(array), sizeof(array)})
static qa_error error;
static void pixel(const qa_image *im, size_t i, unsigned r, unsigned g, unsigned b, unsigned a) {
    CHECK(im->rgba.size > i * 4 + 3);
    CHECK(im->rgba.data[i * 4] == r);
    CHECK(im->rgba.data[i * 4 + 1] == g);
    CHECK(im->rgba.data[i * 4 + 2] == b);
    CHECK(im->rgba.data[i * 4 + 3] == a);
}
static void pcx_and_palette(void) {
    uint8_t indices[] = {1, 192, 255, 2, 3, 4}, palette[768];
    for (unsigned i = 0; i < 256; i++) {
        palette[i * 3] = (uint8_t)i;
        palette[i * 3 + 1] = (uint8_t)(255 - i);
        palette[i * 3 + 2] = 64;
    }
    qa_image source = {.width = 3,
                       .height = 2,
                       .index_bytes = 1,
                       .indices = {indices, sizeof(indices)}},
             decoded = {0};
    qa_buffer encoded = {0};
    CHECK(qa_image_encode_pcx(&source, BYTES(palette), &encoded, &error));
    CHECK(qa_image_decode_pcx((qa_bytes){encoded.data, encoded.size}, QA_IMAGE_FORMAT, &decoded,
                              &error));
    CHECK(decoded.width == 3 && decoded.height == 2 && decoded.palette_count == 256);
    CHECK(!memcmp(indices, decoded.indices.data, 6));
    pixel(&decoded, 2, 255, 0, 64, 255);
    qa_image_free(&decoded);
    /* The historical Q3 reader consumes row padding as the next row's pixels. */
    CHECK(
        qa_image_decode_pcx((qa_bytes){encoded.data, encoded.size}, QA_IMAGE_Q3, &decoded, &error));
    CHECK(decoded.indices.data[3] == 0 && decoded.indices.data[4] == 2);
    qa_image_free(&decoded);
    uint8_t original = encoded.data[128];
    encoded.data[128] = 0xc0;
    decoded.width = 777;
    CHECK(!qa_image_decode_pcx((qa_bytes){encoded.data, encoded.size}, QA_IMAGE_FORMAT, &decoded,
                               &error));
    CHECK(decoded.width == 777);
    encoded.data[128] = original;
    qa_buffer_free(&encoded);
    uint8_t translation[256];
    CHECK(qa_image_player_translation(8, 3, translation, &error));
    CHECK(translation[16] == 143 && translation[31] == 128 && translation[96] == 48);
    CHECK(!qa_image_player_translation(14, 3, translation, &error));
    qa_indexed_level level = {3, 2, {indices, sizeof(indices)}};
    qa_palette_options options = {255, 192, 255, NULL, QA_PALETTE_ORDINARY};
    CHECK(qa_image_expand_indexed(&level, BYTES(palette), &options, &decoded, &error));
    pixel(&decoded, 0, 1, 254, 64, 255);
    pixel(&decoded, 1, 192, 63, 64, 0);
    pixel(&decoded, 2, 255, 0, 64, 0);
    qa_image_free(&decoded);
    options.layer = QA_PALETTE_FULLBRIGHT;
    CHECK(qa_image_expand_indexed(&level, BYTES(palette), &options, &decoded, &error));
    pixel(&decoded, 0, 1, 254, 64, 0);
    pixel(&decoded, 1, 192, 63, 64, 255);
    qa_image_free(&decoded);
    uint8_t colormap[16385] = {0};
    colormap[16384] = 32;
    qa_buffer levels = {0};
    unsigned first = 0;
    CHECK(qa_image_decode_colormap(BYTES(colormap), &levels, &first, &error));
    CHECK(first == 224 && levels.size == 16384);
    qa_buffer_free(&levels);
}
static void qpic_mips_wad(void) {
    uint8_t qpic[12] = {0};
    qa_store_u32le(qpic, 2);
    qa_store_u32le(qpic + 4, 2);
    qpic[8] = 17;
    qa_image im = {0};
    CHECK(qa_image_decode_qpic(BYTES(qpic), &im, &error));
    CHECK(im.width == 2 && im.indices.data[0] == 17 && !im.rgba.data);
    qa_image_free(&im);
    CHECK(!qa_image_decode_qpic((qa_bytes){qpic, 11}, &im, &error));
    qa_store_u32le(qpic, UINT32_MAX);
    CHECK(!qa_image_decode_qpic(BYTES(qpic), &im, &error));
    uint8_t mip[40 + 256 + 64 + 16 + 4 + 770] = {0};
    memcpy(mip, "brick", 5);
    qa_store_u32le(mip + 16, 16);
    qa_store_u32le(mip + 20, 16);
    qa_mip_texture texture = {0};
    CHECK(qa_image_decode_mip(BYTES(mip), &texture, &error));
    CHECK(texture.external && !strcmp(texture.name, "brick"));
    qa_mip_texture_free(&texture);
    size_t at = 40;
    for (unsigned i = 0; i < 4; i++) {
        qa_store_u32le(mip + 24 + i * 4, (uint32_t)at);
        size_t n = (size_t)(16U >> i) * (16U >> i);
        memset(mip + at, (int)(i + 1), n);
        at += n;
    }
    qa_store_u16le(mip + at, 256);
    mip[at + 2] = 200;
    CHECK(qa_image_decode_mip(BYTES(mip), &texture, &error));
    CHECK(!texture.external && texture.levels[3].indices.size == 4 &&
          texture.levels[2].indices.data[0] == 3);
    qa_mip_texture_free(&texture);
    uint8_t wal[104] = {0};
    memcpy(wal, "test", 4);
    qa_store_u32le(wal + 32, 1);
    qa_store_u32le(wal + 36, 1);
    for (unsigned i = 0; i < 4; i++) {
        qa_store_u32le(wal + 40 + i * 4, 100 + i);
        wal[100 + i] = (uint8_t)(7 + i);
    }
    memcpy(wal + 56, "next", 4);
    qa_store_u32le(wal + 88, 0x80000001);
    qa_store_u32le(wal + 92, 4);
    qa_store_u32le(wal + 96, 6);
    CHECK(qa_image_decode_wal(BYTES(wal), &texture, &error));
    CHECK(texture.flags == INT32_MIN + 1 && texture.contents == 4 && texture.value == 6 &&
          !strcmp(texture.animation, "next"));
    CHECK(texture.levels[3].indices.data[0] == 10);
    qa_mip_texture_free(&texture);
    qa_store_u32le(wal + 40, 99);
    CHECK(!qa_image_decode_wal(BYTES(wal), &texture, &error));
    uint8_t wad[12 + sizeof(mip) + 32];
    memset(wad, 0, sizeof(wad));
    memcpy(wad, "WAD3", 4);
    qa_store_u32le(wad + 4, 1);
    qa_store_u32le(wad + 8, 12 + sizeof(mip));
    memcpy(wad + 12, mip, sizeof(mip));
    uint8_t *d = wad + 12 + sizeof(mip);
    qa_store_u32le(d, 12);
    qa_store_u32le(d + 4, sizeof(mip));
    qa_store_u32le(d + 8, sizeof(mip));
    d[12] = 67;
    memcpy(d + 16, "BRICK", 5);
    qa_wad archive = {0};
    qa_wad_image image = {0};
    CHECK(qa_wad_decode(BYTES(wad), &archive, &error));
    CHECK(archive.wad3 && archive.count == 1 && !strcmp(archive.lumps[0].name, "brick"));
    CHECK(qa_wad_decode_image(&archive, 0, &image, &error));
    CHECK(image.kind == QA_WAD_MIP && image.palette_rgb.size == 768 &&
          image.palette_rgb.data[0] == 200);
    qa_wad_image_free(&image);
    archive.lumps[0].compression = 1;
    CHECK(!qa_wad_decode_image(&archive, 0, &image, &error));
    qa_wad_free(&archive);
    uint8_t lit[14] = {'Q', 'L', 'I', 'T', 1, 0, 0, 0, 1, 2, 3, 4, 5, 6};
    qa_buffer samples = {0};
    CHECK(qa_image_decode_lit(BYTES(lit), 2, &samples, &error));
    CHECK(samples.size == 6 && samples.data[5] == 6);
    qa_buffer_free(&samples);
    CHECK(!qa_image_decode_lit(BYTES(lit), 3, &samples, &error));
}
static void tga_bmp(void) {
    uint8_t tga[24] = {0};
    tga[2] = 2;
    qa_store_u16le(tga + 12, 1);
    qa_store_u16le(tga + 14, 2);
    tga[16] = 24;
    tga[17] = 32;
    tga[20] = 255;
    tga[22] = 255;
    qa_image im = {0};
    CHECK(qa_image_decode_tga(BYTES(tga), QA_IMAGE_FORMAT, &im, &error));
    pixel(&im, 0, 255, 0, 0, 255);
    pixel(&im, 1, 0, 255, 0, 255);
    qa_image_free(&im);
    CHECK(qa_image_decode_tga(BYTES(tga), QA_IMAGE_Q3, &im, &error));
    pixel(&im, 0, 0, 255, 0, 255);
    qa_image_free(&im);
    uint8_t pal[24] = {0};
    pal[1] = 1;
    pal[2] = 1;
    qa_store_u16le(pal + 3, 256);
    qa_store_u16le(pal + 5, 1);
    pal[7] = 32;
    qa_store_u16le(pal + 12, 1);
    qa_store_u16le(pal + 14, 1);
    pal[16] = 16;
    pal[17] = 32;
    pal[18] = 30;
    pal[19] = 20;
    pal[20] = 10;
    pal[21] = 40;
    qa_store_u16le(pal + 22, 256);
    CHECK(qa_image_decode_tga(BYTES(pal), QA_IMAGE_FORMAT, &im, &error));
    pixel(&im, 0, 10, 20, 30, 40);
    CHECK(im.palette_first == 256 && im.index_bytes == 2 && qa_load_u16le(im.indices.data) == 256);
    qa_image_free(&im);
    qa_store_u16le(pal + 22, 255);
    CHECK(!qa_image_decode_tga(BYTES(pal), QA_IMAGE_FORMAT, &im, &error));
    uint8_t rle[22] = {0};
    rle[2] = 10;
    qa_store_u16le(rle + 12, 1);
    qa_store_u16le(rle + 14, 1);
    rle[16] = 24;
    rle[18] = 0x83;
    rle[21] = 200;
    CHECK(!qa_image_decode_tga(BYTES(rle), QA_IMAGE_FORMAT, &im, &error));
    CHECK(qa_image_decode_tga(BYTES(rle), QA_IMAGE_Q3, &im, &error));
    pixel(&im, 0, 200, 0, 0, 255);
    qa_image_free(&im);
    uint8_t bmp[62] = {0};
    memcpy(bmp, "BM", 2);
    qa_store_u32le(bmp + 2, sizeof(bmp));
    qa_store_u32le(bmp + 10, 54);
    qa_store_u32le(bmp + 14, 40);
    qa_store_u32le(bmp + 18, 1);
    qa_store_u32le(bmp + 22, 2);
    qa_store_u16le(bmp + 26, 1);
    qa_store_u16le(bmp + 28, 24);
    bmp[56] = 255;
    bmp[59] = 255;
    CHECK(qa_image_decode_bmp(BYTES(bmp), QA_IMAGE_FORMAT, &im, &error));
    pixel(&im, 0, 0, 255, 0, 255);
    pixel(&im, 1, 255, 0, 0, 255);
    qa_image_free(&im);
    CHECK(qa_image_decode_bmp(BYTES(bmp), QA_IMAGE_Q3, &im, &error));
    pixel(&im, 0, 255, 0, 0, 255);
    qa_image_free(&im);
    qa_store_u32le(bmp + 22, (uint32_t)-2);
    CHECK(qa_image_decode_bmp(BYTES(bmp), QA_IMAGE_FORMAT, &im, &error));
    pixel(&im, 0, 255, 0, 0, 255);
    qa_image_free(&im);
    qa_store_u32le(bmp + 10, 53);
    CHECK(!qa_image_decode_bmp(BYTES(bmp), QA_IMAGE_FORMAT, &im, &error));
}
static void transform_and_encode(void) {
    uint8_t pixels[] = {255, 0, 0, 128, 0, 255, 0, 255, 0, 0, 255, 0, 255, 255, 255, 255};
    qa_image in = {.width = 2, .height = 2, .rgba = {pixels, sizeof(pixels)}}, im = {0};
    qa_buffer b = {0};
    CHECK(qa_image_mip(&in, QA_MIP_BOX, &im, &error));
    pixel(&im, 0, 127, 127, 127, 159);
    qa_image_free(&im);
    CHECK(qa_image_resample(&in, 1, 1, QA_RESAMPLE_Q1, &im, &error));
    pixel(&im, 0, 0, 255, 0, 255);
    qa_image_free(&im);
    CHECK(qa_image_resample(&in, 1, 1, QA_RESAMPLE_Q2_Q3, &im, &error));
    pixel(&im, 0, 127, 127, 127, 159);
    qa_image_free(&im);
    CHECK(qa_image_mip(&in, QA_MIP_Q3_WEIGHTED, &im, &error));
    pixel(&im, 0, 127, 127, 127, 159);
    qa_image_free(&im);
    qa_gamma_options gamma = {QA_GAMMA_Q2, 1, 2, 0, false};
    CHECK(qa_image_apply_gamma(&in, &gamma, &im, &error));
    pixel(&im, 0, 255, 0, 0, 128);
    qa_image_free(&im);
    CHECK(qa_image_encode_tga(&in, &b, &error));
    CHECK(qa_image_decode_tga((qa_bytes){b.data, b.size}, QA_IMAGE_FORMAT, &im, &error));
    CHECK(!memcmp(im.rgba.data, pixels, sizeof(pixels)));
    qa_image_free(&im);
    qa_buffer_free(&b);
    CHECK(qa_image_encode_png(&in, &b, &error));
    CHECK(qa_image_decode_png((qa_bytes){b.data, b.size}, &im, &error));
    CHECK(!memcmp(im.rgba.data, pixels, sizeof(pixels)));
    CHECK(im.bit_depth == 8 && im.color_type == 6 && !im.has_gamma);
    qa_image_free(&im);
    b.data[b.size - 1] ^= 1;
    im.width = 456;
    CHECK(!qa_image_decode_png((qa_bytes){b.data, b.size}, &im, &error));
    CHECK(im.width == 456);
    qa_buffer_free(&b);
    uint8_t solid[8 * 8 * 4];
    for (size_t i = 0; i < sizeof(solid); i += 4) {
        solid[i] = 60;
        solid[i + 1] = 120;
        solid[i + 2] = 180;
        solid[i + 3] = 0;
    }
    in = (qa_image){.width = 8, .height = 8, .rgba = {solid, sizeof(solid)}};
    CHECK(qa_image_encode_jpeg(&in, 100, false, &b, &error));
    CHECK(qa_image_decode_jpeg((qa_bytes){b.data, b.size}, &im, &error));
    CHECK(im.width == 8 && im.height == 8);
    for (size_t i = 0; i < im.rgba.size; i += 4) {
        CHECK(abs(im.rgba.data[i] - 60) <= 2);
        CHECK(abs(im.rgba.data[i + 1] - 120) <= 2);
        CHECK(abs(im.rgba.data[i + 2] - 180) <= 2);
        CHECK(im.rgba.data[i + 3] == 255);
    }
    qa_image_free(&im);
    qa_buffer_free(&b);
}
/* Native library fixture generation exercises retained PNG metadata and JPEG
 * profiles independently of this module's screenshot encoders. */
static void png_palette_write(png_structp p, png_bytep data, png_size_t n) {
    qa_buffer *b = png_get_io_ptr(p);
    uint8_t *next = realloc(b->data, b->size + n);
    CHECK(next != NULL);
    b->data = next;
    memcpy(b->data + b->size, data, n);
    b->size += n;
}
static void native_codec_profiles(void) {
    qa_buffer b = {0};
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    CHECK(p != NULL);
    png_infop info = png_create_info_struct(p);
    CHECK(info != NULL);
    png_set_write_fn(p, &b, png_palette_write, NULL);
    png_set_IHDR(p, info, 2, 2, 1, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_ADAM7, 0, 0);
    png_color palette[2] = {{20, 30, 40}, {50, 60, 70}};
    png_byte alpha[2] = {255, 0};
    png_set_PLTE(p, info, palette, 2);
    png_set_tRNS(p, info, alpha, 2, NULL);
    png_set_gAMA(p, info, 0.5);
    png_write_info(p, info);
    uint8_t rows[2] = {0x40, 0x80};
    png_bytep pointers[2] = {rows, rows + 1};
    png_write_image(p, pointers);
    png_write_end(p, info);
    png_destroy_write_struct(&p, &info);
    qa_image im = {0};
    CHECK(qa_image_decode_png((qa_bytes){b.data, b.size}, &im, &error));
    CHECK(im.index_bytes == 1 && im.palette_count == 2 && im.bit_depth == 1 && im.has_gamma &&
          im.gamma == 0.5);
    CHECK(im.indices.data[0] == 0 && im.indices.data[1] == 1 && im.indices.data[2] == 1 &&
          im.indices.data[3] == 0);
    pixel(&im, 1, 50, 60, 70, 0);
    qa_image_free(&im);
    qa_buffer_free(&b);
    for (unsigned cmyk = 0; cmyk < 2; cmyk++) {
        struct jpeg_compress_struct j = {0};
        struct jpeg_error_mgr err;
        j.err = jpeg_std_error(&err);
        jpeg_create_compress(&j);
        unsigned char *bytes = NULL;
        unsigned long size = 0;
        jpeg_mem_dest(&j, &bytes, &size);
        j.image_width = 2;
        j.image_height = 2;
        j.input_components = cmyk ? 4 : 1;
        j.in_color_space = cmyk ? JCS_CMYK : JCS_GRAYSCALE;
        jpeg_set_defaults(&j);
        jpeg_set_quality(&j, 100, TRUE);
        jpeg_simple_progression(&j);
        jpeg_start_compress(&j, TRUE);
        uint8_t row[] = {90, 90, 90, 90, 90, 90, 90, 90};
        while (j.next_scanline < j.image_height) {
            JSAMPROW ptr = row;
            jpeg_write_scanlines(&j, &ptr, 1);
        }
        jpeg_finish_compress(&j);
        jpeg_destroy_compress(&j);
        CHECK(qa_image_decode_jpeg((qa_bytes){bytes, (size_t)size}, &im, &error));
        pixel(&im, 0, 90, 90, 90, 255);
        qa_image_free(&im);
        free(bytes);
    }
}
static void gif_animation(void) {
    uint8_t gif[] = {'G',  'I', 'F', '8',  '9',  'a',  2,    0,    1,   0,    0x80, 0,   0,
                     255,  0,   0,   0,    255,  0,    0x21, 0xff, 11,  'N',  'E',  'T', 'S',
                     'C',  'A', 'P', 'E',  '2',  '.',  '0',  3,    1,   5,    0,    0,   0x21,
                     0xf9, 4,   8,   7,    0,    0,    0,    0x2c, 0,   0,    0,    0,   1,
                     0,    1,   0,   0,    2,    2,    0x44, 0x01, 0,   0x21, 0xf9, 4,   12,
                     9,    0,   0,   0,    0x2c, 1,    0,    0,    0,   1,    0,    1,   0,
                     0,    2,   2,   0x4c, 0x01, 0,    0x2c, 0,    0,   0,    0,    1,   0,
                     1,    0,   0,   2,    2,    0x4c, 0x01, 0,    0x3b};
    qa_gif animation = {0};
    CHECK(qa_image_decode_gif(BYTES(gif), &animation, &error));
    CHECK(animation.frame_count == 3 && animation.width == 2 && animation.loop_count == 5);
    CHECK(animation.frames[0].delay_centiseconds == 7 &&
          animation.frames[1].delay_centiseconds == 9);
    pixel(&animation.frames[0].image, 0, 255, 0, 0, 255);
    pixel(&animation.frames[0].image, 1, 0, 0, 0, 0);
    pixel(&animation.frames[1].image, 0, 0, 0, 0, 0);
    pixel(&animation.frames[1].image, 1, 0, 255, 0, 255);
    pixel(&animation.frames[2].image, 0, 0, 255, 0, 255);
    pixel(&animation.frames[2].image, 1, 0, 0, 0, 0);
    qa_gif_free(&animation);
    CHECK(!qa_image_decode_gif((qa_bytes){gif, sizeof(gif) - 3}, &animation, &error));
}
static void malformed_inputs(void) {
    uint8_t zeros[128] = {0};
    qa_image im = {.width = 991};
    for (unsigned format = QA_IMAGE_PCX; format <= QA_IMAGE_JPEG; format++)
        for (size_t n = 0; n < 33; n++) {
            CHECK(!qa_image_decode((qa_bytes){zeros, n}, (qa_image_format)format, QA_IMAGE_FORMAT,
                                   &im, &error));
            CHECK(im.width == 991);
        }
    CHECK(!qa_image_decode((qa_bytes){NULL, 128}, QA_IMAGE_PNG, QA_IMAGE_FORMAT, &im, &error));
    CHECK(!qa_image_decode(BYTES(zeros), QA_IMAGE_PNG, (qa_image_policy)99, &im, &error));
}
int main(void) {
    pcx_and_palette();
    qpic_mips_wad();
    tga_bmp();
    transform_and_encode();
    native_codec_profiles();
    gif_animation();
    malformed_inputs();
    puts("image tests passed");
    return EXIT_SUCCESS;
}
