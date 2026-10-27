/*
 * Port of src/formats/images/q3-tga.ts (Q3 LoadTGA profile). The TS
 * decodeTga here becomes qa_decode_q3_tga (qa_decode_tga is the
 * format-oriented reader in tga.h). C accommodations: byte inputs are
 * pointer+length; `source` NULL means "<buffer>"; the warning closure
 * becomes fn ptr + ctx (NULL means the TS default no-op; the text is
 * valid only during the call). BinaryError -> qa_binary_error.
 * Returns 0 only on allocation failure. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_Q3_TGA_H
#define QA_FORMATS_IMAGES_Q3_TGA_H

#include <stddef.h>
#include <stdint.h>

/* TS ImageData, shared by the q3-* historical readers. */
typedef struct qa_image_data {
  int64_t width, height;
  uint8_t *pixels; size_t pixels_length;
} qa_image_data;
void qa_image_data_destroy(qa_image_data *image);

typedef void (*qa_q3_tga_warning)(void *ctx, const char *text);

int qa_decode_q3_tga(const uint8_t *bytes, size_t length, const char *source,
                     qa_q3_tga_warning warning, void *warning_ctx,
                     qa_image_data *out);

#endif
