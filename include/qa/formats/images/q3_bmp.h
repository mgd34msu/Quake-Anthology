/*
 * Port of src/formats/images/q3-bmp.ts (Q3 LoadBMP profile). The TS
 * decodeBmp here becomes qa_decode_q3_bmp (qa_decode_bmp is the
 * format-oriented reader in bmp.h). BmpDropError is qa_raise with name
 * "BmpDropError" (catchable with qa_try, as the TS throw is). C
 * accommodations: byte inputs are pointer+length; `source` NULL means
 * "<buffer>". Returns 0 only on allocation failure.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_Q3_BMP_H
#define QA_FORMATS_IMAGES_Q3_BMP_H

#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "qa/formats/images/q3_tga.h"

noreturn void qa_bmp_drop_error(const char *source, const char *format, ...);

int qa_decode_q3_bmp(const uint8_t *bytes, size_t length, const char *source,
                     qa_image_data *out);

#endif
