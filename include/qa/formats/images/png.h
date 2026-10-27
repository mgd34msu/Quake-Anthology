/*
 * Port of src/formats/images/png.ts. The TS uses node:zlib
 * inflateSync; the C port links zlib (-lz). Inflate failure messages
 * come from zlib itself, approximating Node's wrapped message. C
 * accommodations: byte inputs are pointer+length; `source` NULL means
 * "<png>"; nullable gamma/srgbIntent use has_* flags; PngImage embeds
 * its qa_image_level, with owned indices/palette/alpha present only
 * for indexed input (has_indexed). The TS re-export of encodePng is
 * mirrored by including png_encoder.h. Returns 0 only on allocation
 * failure. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_PNG_H
#define QA_FORMATS_IMAGES_PNG_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/render.h"
#include "qa/formats/images/png_encoder.h"

typedef struct qa_png_image {
  qa_image_level level;
  int64_t bit_depth, color_type;
  int has_gamma; double gamma;
  int has_srgb_intent; int64_t srgb_intent;
  int has_indexed;
  uint8_t *indices; size_t indices_length;
  uint8_t *palette; size_t palette_length;
  uint8_t *alpha; size_t alpha_length;
} qa_png_image;
void qa_png_image_destroy(qa_png_image *image);

int qa_decode_png(const uint8_t *bytes, size_t length, const char *source,
                  qa_png_image *out);

#endif
