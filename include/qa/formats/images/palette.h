/*
 * Port of src/formats/images/palette.ts. C accommodations:
 * IndexedRenderImage is qa_render_image with kind QA_RENDER_IMAGE_INDEXED8
 * (see qa/contracts/render.h); byte inputs are pointer+length; returned
 * qa_image_level pixels are malloc'd (free with qa_image_level_destroy
 * from qa/formats/images/mip.h). RangeError -> qa_range_error. Functions
 * returning int return 0 only on allocation failure. The TS WeakMap of
 * composed gamma tables becomes a cache keyed by profile address, so a
 * mutated profile keeps its first composed table, as in the TS.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_PALETTE_H
#define QA_FORMATS_IMAGES_PALETTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/render.h"

typedef qa_render_image qa_indexed_render_image;

int qa_decode_palette(const uint8_t *bytes, size_t length,
                      qa_resolved_resource_reference source, qa_palette *out);
void qa_palette_destroy(qa_palette *palette);

typedef struct qa_q1_colormap {
  uint8_t *levels; /* 16384 bytes, owned */
  int64_t fullbright_first, fullbright_last;
} qa_q1_colormap;
int qa_decode_q1_colormap(const uint8_t *bytes, size_t length,
                          qa_q1_colormap *out);
void qa_q1_colormap_destroy(qa_q1_colormap *colormap);

/* Borrows levels/palette/translation; validates like the TS. */
qa_indexed_render_image qa_indexed_render_image_make(
    const qa_image_level *levels, size_t level_count, qa_palette palette,
    qa_palette_transparency transparency,
    const qa_fullbright_range *fullbright, const qa_byte_view *translation);

/* Malloc'd 256-entry translation; caller frees. NULL on OOM. */
uint8_t *qa_q1_player_translation(int64_t top_color, int64_t bottom_color);

/* TS PaletteLayer: "combined"|"ordinary"|"fullbright". */
typedef enum qa_palette_layer {
  QA_PALETTE_LAYER_COMBINED, QA_PALETTE_LAYER_ORDINARY, QA_PALETTE_LAYER_FULLBRIGHT
} qa_palette_layer;
int qa_expand_indexed_image(const qa_indexed_render_image *image, int64_t level,
                            qa_palette_layer layer, qa_image_level *out);

/* TS GammaProfile union on kind. intensity/only_gamma unused for q1-gl;
 * overbright_bits only for q3 (validated integer in 0..2). */
typedef enum qa_gamma_kind {
  QA_GAMMA_Q1_GL, QA_GAMMA_Q2_GL, QA_GAMMA_Q3
} qa_gamma_kind;
typedef struct qa_gamma_profile {
  qa_gamma_kind kind;
  double gamma;
  double intensity;
  double overbright_bits;
  bool only_gamma;
} qa_gamma_profile;

void qa_build_gamma_table(const qa_gamma_profile *profile, uint8_t table[256]);
int qa_apply_image_gamma(const qa_image_level *level,
                         const qa_gamma_profile *profile, qa_image_level *out);

#endif
