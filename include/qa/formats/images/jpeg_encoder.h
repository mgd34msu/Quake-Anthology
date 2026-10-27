/*
 * Port of src/formats/images/jpeg-encoder.ts (pure-TS IJG compressor,
 * ported faithfully). C accommodations: profile NULL means the TS
 * default {kind:"diagnostic-owned"}; a diagnostic result is malloc'd
 * (caller frees), a source-destination result is the destination
 * buffer itself (borrowed, *out_length set) like the TS subarray
 * view. RangeError -> qa_range_error. Returns NULL only on allocation
 * failure. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_JPEG_ENCODER_H
#define QA_FORMATS_IMAGES_JPEG_ENCODER_H

#include <stddef.h>
#include <stdint.h>

#include "qa/formats/images/jpeg.h"

/* TS JpegEncodingProfile union on kind. */
typedef enum qa_jpeg_encoding_profile_kind {
  QA_JPEG_PROFILE_DIAGNOSTIC_OWNED,
  QA_JPEG_PROFILE_SOURCE_DESTINATION
} qa_jpeg_encoding_profile_kind;
typedef struct qa_jpeg_encoding_profile {
  qa_jpeg_encoding_profile_kind kind;
  uint8_t *destination; size_t destination_length; /* source-destination */
  int row_order_bottom_up; /* source-destination: 0 top-down, 1 bottom-up */
} qa_jpeg_encoding_profile;

uint8_t *qa_encode_jpeg(const qa_jpeg_image *image, int64_t quality,
                        const qa_jpeg_encoding_profile *profile,
                        size_t *out_length);

#endif
