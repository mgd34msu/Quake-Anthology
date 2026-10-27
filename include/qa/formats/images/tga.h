/*
 * Port of src/formats/images/tga.ts. C accommodations: byte inputs are
 * pointer+length; `source` NULL means "<tga>"; TgaImage embeds its
 * qa_image_level, with owned indices/palette_rgba present only for
 * color-mapped input (has_indexed, mirroring the TS nullable
 * `indexed`). BinaryError -> qa_binary_error, RangeError ->
 * qa_range_error. Returns 0 only on allocation failure. The TS
 * re-export of decodeQ3Tga is mirrored by including q3_tga.h.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_TGA_H
#define QA_FORMATS_IMAGES_TGA_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/render.h"
#include "qa/formats/images/q3_tga.h"

typedef struct qa_tga_image {
  qa_image_level level;
  int64_t descriptor;
  int has_indexed;
  uint16_t *indices; size_t indices_length;
  int64_t first;
  uint8_t *palette_rgba; size_t palette_rgba_length;
} qa_tga_image;
void qa_tga_image_destroy(qa_tga_image *image);

int qa_decode_tga(const uint8_t *bytes, size_t length, const char *source,
                  qa_tga_image *out);
/* Malloc'd TGA bytes; caller frees. NULL on OOM, *out_length set. */
uint8_t *qa_encode_tga(const qa_image_level *image, size_t *out_length);

#endif
