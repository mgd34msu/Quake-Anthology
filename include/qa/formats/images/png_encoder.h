/*
 * Port of src/formats/images/png-encoder.ts. The TS uses node:zlib
 * deflateSync; the C port links zlib (-lz) with matching defaults.
 * C accommodations: the RGBA input is pointer+length; width/height are
 * doubles so the TS safe-integer checks reproduce. Returns malloc'd
 * PNG bytes (caller frees), NULL on OOM, *out_length set.
 * RangeError -> qa_range_error. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_PNG_ENCODER_H
#define QA_FORMATS_IMAGES_PNG_ENCODER_H

#include <stddef.h>
#include <stdint.h>

uint8_t *qa_encode_png(double width, double height, const uint8_t *rgba,
                       size_t rgba_length, size_t *out_length);

#endif
