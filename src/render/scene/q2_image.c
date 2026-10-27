/* Port of src/render/scene/q2-image.ts. GPL-2.0-or-later. */
#include "qa/render/scene/q2_image.h"
#include <stdlib.h>
#include <string.h>
#include "qa/formats/images/mip.h"
#include "qa/formats/images/palette.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
void qa_q2_mipmapped_image_destroy(qa_q2_mipmapped_image *image) {
  if (!image) return;
  if (image->_chain) qa_mip_chain_destroy((qa_mip_chain *)image->_chain);
  free(image->_chain);
  if (image->_scaled) qa_image_level_destroy((qa_image_level *)image->_scaled);
  free(image->_scaled);
  if (image->_expanded) qa_image_level_destroy((qa_image_level *)image->_expanded);
  free(image->_expanded);
  memset(image, 0, sizeof *image);
}
qa_render_image qa_q2_mipmapped_image_new(qa_render_image content, qa_q2_mipmapped_image *out) {
  memset(out, 0, sizeof *out);
  if (content.kind == QA_RENDER_IMAGE_DEPTH32F) { out->image = content; return content; }
  const qa_image_level *first;
  qa_image_level borrowed;
  if (content.kind == QA_RENDER_IMAGE_INDEXED8) {
    qa_image_level *expanded = malloc(sizeof *expanded);
    if (!expanded) qa_range_error("Allocation failed");
    memset(expanded, 0, sizeof *expanded);
    /* TS expandIndexedImage(content): default level 0 / combined layer. */
    if (!qa_expand_indexed_image(&content, 0, QA_PALETTE_LAYER_COMBINED, expanded)) qa_range_error("Allocation failed");
    out->_expanded = expanded; first = expanded;
  } else {
    if (!content.level_count) qa_range_error("Cannot read properties of undefined (reading 'width')");
    borrowed = content.levels[0]; first = &borrowed;
  }
  qa_image_level *scaled = malloc(sizeof *scaled);
  if (!scaled) qa_range_error("Allocation failed");
  memset(scaled, 0, sizeof *scaled); out->_scaled = scaled;
  qa_gamma_profile profile = { QA_GAMMA_Q2_GL, 1, 2, 0, false };
  if (!qa_apply_image_gamma(first, &profile, scaled)) qa_range_error("Allocation failed");
  qa_mip_chain *chain = malloc(sizeof *chain);
  if (!chain) qa_range_error("Allocation failed");
  memset(chain, 0, sizeof *chain); out->_chain = chain;
  /* TS generateMipChain default profile is "box" in formats/images. */
  if (!qa_generate_mip_chain(scaled, QA_MIP_BOX, chain)) qa_range_error("Allocation failed");
  out->levels = chain->levels; out->level_count = chain->count;
  out->image.kind = QA_RENDER_IMAGE_RGBA8;
  out->image.levels = chain->levels; out->image.level_count = chain->count;
  out->image.border_color = qa_math_vec4(0, 0, 0, 0);
  return out->image;
}
