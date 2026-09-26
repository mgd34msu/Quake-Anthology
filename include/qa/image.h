#ifndef QA_IMAGE_H
#define QA_IMAGE_H

#include "qa/common.h"

/* Decoded rows are top-to-bottom. Buffers are owned; free with qa_image_free.
 * Indexed sources retain their original indices (1 byte or little-endian 2 bytes)
 * and RGBA palette alongside upload pixels. Missing external palettes leave
 * rgba/palette empty. No gamma correction is applied by decoders. */
typedef struct qa_image {
    uint32_t width, height;
    qa_buffer rgba;
    qa_buffer indices;
    qa_buffer palette;
    uint32_t palette_first, palette_count;
    uint8_t index_bytes, bit_depth, color_type, descriptor;
    bool has_gamma;
    double gamma;
    int srgb_intent; /* -1 when unspecified. */
} qa_image;

typedef enum qa_image_policy { QA_IMAGE_FORMAT, QA_IMAGE_Q3 } qa_image_policy;
typedef enum qa_image_format {
    QA_IMAGE_PCX,
    QA_IMAGE_QPIC,
    QA_IMAGE_TGA,
    QA_IMAGE_BMP,
    QA_IMAGE_PNG,
    QA_IMAGE_JPEG
} qa_image_format;

/* All readers leave out unchanged on failure. Free any previous output first. */
void qa_image_free(qa_image *image);
bool qa_image_decode(qa_bytes bytes, qa_image_format format, qa_image_policy policy, qa_image *out,
                     qa_error *error);
bool qa_image_decode_pcx(qa_bytes bytes, qa_image_policy policy, qa_image *out, qa_error *error);
bool qa_image_decode_qpic(qa_bytes bytes, qa_image *out, qa_error *error);
bool qa_image_decode_tga(qa_bytes bytes, qa_image_policy policy, qa_image *out, qa_error *error);
bool qa_image_decode_bmp(qa_bytes bytes, qa_image_policy policy, qa_image *out, qa_error *error);
bool qa_image_decode_png(qa_bytes bytes, qa_image *out, qa_error *error);
/* JPEG CMYK/YCCK retain Quake's decoded C/M/Y values, discarding K, alpha=255. */
bool qa_image_decode_jpeg(qa_bytes bytes, qa_image *out, qa_error *error);
bool qa_image_encode_pcx(const qa_image *image, qa_bytes palette_rgb, qa_buffer *out,
                         qa_error *error);
bool qa_image_encode_tga(const qa_image *image, qa_buffer *out, qa_error *error);
bool qa_image_encode_png(const qa_image *image, qa_buffer *out, qa_error *error);
bool qa_image_encode_jpeg(const qa_image *image, int quality, bool bottom_up, qa_buffer *out,
                          qa_error *error);

typedef struct qa_indexed_level {
    uint32_t width, height;
    qa_buffer indices;
} qa_indexed_level;
typedef struct qa_mip_texture {
    char name[33], animation[33];
    uint32_t width, height;
    bool external;
    qa_indexed_level levels[4];
    int32_t flags, contents, value; /* WAL metadata; zero for MIP. */
} qa_mip_texture;
void qa_mip_texture_free(qa_mip_texture *texture);
bool qa_image_decode_mip(qa_bytes bytes, qa_mip_texture *out, qa_error *error);
bool qa_image_decode_wal(qa_bytes bytes, qa_mip_texture *out, qa_error *error);
/* expected_samples=SIZE_MAX accepts any complete RGB sample sequence. */
bool qa_image_decode_lit(qa_bytes bytes, size_t expected_samples, qa_buffer *out, qa_error *error);
bool qa_image_decode_colormap(qa_bytes bytes, qa_buffer *levels, unsigned *first_fullbright,
                              qa_error *error);
bool qa_image_player_translation(unsigned top, unsigned bottom, uint8_t table[256],
                                 qa_error *error);

typedef enum qa_palette_layer {
    QA_PALETTE_COMBINED,
    QA_PALETTE_ORDINARY,
    QA_PALETTE_FULLBRIGHT
} qa_palette_layer;
typedef struct qa_palette_options {
    int transparent_index;                 /* -1 opaque; tests ORIGINAL index before translation. */
    int fullbright_first, fullbright_last; /* -1 disables fullbright range. */
    const uint8_t *translation;            /* NULL or 256 entries. */
    qa_palette_layer layer;
} qa_palette_options;
bool qa_image_expand_indexed(const qa_indexed_level *image, qa_bytes palette_rgb,
                             const qa_palette_options *options, qa_image *out, qa_error *error);
typedef enum qa_mip_filter { QA_MIP_BOX, QA_MIP_Q3_WEIGHTED } qa_mip_filter;
bool qa_image_mip(const qa_image *image, qa_mip_filter filter, qa_image *out, qa_error *error);
/* Only generated levels are owned. The unchanged source level is shared and
 * omitted from this array; a 1x1 source therefore produces an empty chain. */
typedef struct qa_mip_chain {
    size_t count;
    qa_image *levels;
} qa_mip_chain;
bool qa_image_mip_chain(const qa_image *image, qa_mip_filter filter, qa_mip_chain *out,
                        qa_error *error);
void qa_mip_chain_free(qa_mip_chain *chain);
typedef enum qa_resample_filter { QA_RESAMPLE_Q1, QA_RESAMPLE_Q2_Q3 } qa_resample_filter;
bool qa_image_resample(const qa_image *image, uint32_t width, uint32_t height,
                       qa_resample_filter filter, qa_image *out, qa_error *error);

typedef enum qa_gamma_profile { QA_GAMMA_Q1, QA_GAMMA_Q2, QA_GAMMA_Q3 } qa_gamma_profile;
typedef struct qa_gamma_options {
    qa_gamma_profile profile;
    float gamma, intensity;
    unsigned overbright_bits;
    bool only_gamma;
} qa_gamma_options;
bool qa_image_gamma_table(const qa_gamma_options *options, uint8_t table[256], qa_error *error);
bool qa_image_apply_gamma(const qa_image *image, const qa_gamma_options *options, qa_image *out,
                          qa_error *error);

/* WAD directory entries borrow bytes from the input; keep that input alive.
 * Decode a lump to obtain independent owned image/texture buffers. */
typedef struct qa_wad_lump {
    char name[17];
    uint32_t offset, disk_size, decoded_size;
    uint8_t type, compression;
    qa_bytes bytes;
} qa_wad_lump;
typedef struct qa_wad {
    bool wad3;
    size_t count;
    qa_wad_lump *lumps;
} qa_wad;
typedef enum qa_wad_image_kind {
    QA_WAD_RAW,
    QA_WAD_QPIC,
    QA_WAD_MIP,
    QA_WAD_PALETTE
} qa_wad_image_kind;
typedef struct qa_wad_image {
    qa_wad_image_kind kind;
    qa_image image;
    qa_mip_texture texture;
    qa_buffer palette_rgb;
    qa_bytes raw;
} qa_wad_image;
bool qa_wad_decode(qa_bytes bytes, qa_wad *out, qa_error *error);
bool qa_wad_decode_image(const qa_wad *wad, size_t index, qa_wad_image *out, qa_error *error);
void qa_wad_free(qa_wad *wad);
void qa_wad_image_free(qa_wad_image *image);

typedef struct qa_gif_frame {
    qa_image image; /* Complete composited logical screen, owned. */
    uint32_t x, y, width, height;
    qa_buffer indices, palette_rgb; /* Local deinterlaced frame, owned. */
    int transparent_index;
    unsigned delay_centiseconds, disposal;
} qa_gif_frame;
typedef struct qa_gif {
    uint32_t width, height;
    unsigned background_index;
    int loop_count; /* -1 absent, 0 forever. */
    qa_buffer global_palette_rgb;
    size_t frame_count;
    qa_gif_frame *frames;
} qa_gif;
bool qa_image_decode_gif(qa_bytes bytes, qa_gif *out, qa_error *error);
void qa_gif_free(qa_gif *gif);

#endif
