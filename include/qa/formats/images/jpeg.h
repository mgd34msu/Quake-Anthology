/*
 * Port of src/formats/images/jpeg.ts (pure-TS IJG decoder, ported
 * faithfully; no library decoder). C accommodations: byte inputs are
 * pointer+length; `source` NULL means "<jpeg>"; the print closure
 * becomes fn ptr + ctx (NULL means the TS default no-op; warning text
 * is valid only during the call and includes its trailing newline).
 * JpegImage is qa_image_level (pixels owned, free with
 * qa_image_level_destroy). JpegSourceError -> qa_jpeg_source_error,
 * BinaryError -> qa_binary_error, RangeError -> qa_range_error, plain
 * Error aborts as "Error:". Returns 0 only on allocation failure.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_JPEG_H
#define QA_FORMATS_IMAGES_JPEG_H

#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "qa/contracts/render.h"

typedef qa_image_level qa_jpeg_image;

typedef void (*qa_jpeg_print)(void *ctx, const char *text);

/* `format` is the IJG source message (without the "JPEG: " prefix). */
noreturn void qa_jpeg_source_error(const char *source, int64_t offset,
                                   const char *format, ...);

int qa_decode_jpeg(const uint8_t *bytes, size_t length, const char *source,
                   qa_jpeg_print print, void *print_ctx, qa_jpeg_image *out);

#endif
