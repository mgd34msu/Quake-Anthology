/*
 * Port of src/formats/images/bmp.ts. C accommodations: byte inputs are
 * pointer+length; `source` NULL means "<bmp>"; BmpImage embeds its
 * qa_image_level, with owned indices/palette present only for 8-bit
 * input (has_indexed, mirroring the TS nullable `indexed`).
 * BinaryError -> qa_binary_error. Returns 0 only on allocation
 * failure. The TS re-exports of decodeQ3Bmp/BmpDropError are mirrored
 * by including q3_bmp.h. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_BMP_H
#define QA_FORMATS_IMAGES_BMP_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/render.h"
#include "qa/formats/images/q3_bmp.h"

typedef struct qa_bmp_image {
  qa_image_level level;
  int has_indexed;
  uint8_t *indices; size_t indices_length;
  uint8_t *palette; size_t palette_length; /* RGB triples */
} qa_bmp_image;
void qa_bmp_image_destroy(qa_bmp_image *image);

int qa_decode_bmp(const uint8_t *bytes, size_t length, const char *source,
                  qa_bmp_image *out);

#endif
