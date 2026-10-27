/*
 * Port of src/formats/images/gif.ts. C accommodations: byte inputs are
 * pointer+length; `source` NULL means "<gif>"; nullable TS numbers
 * (transparentIndex, loopCount) use -1 for null; the global palette is
 * NULL (length 0) when absent. All buffers are owned; destroy frees
 * them. BinaryError -> qa_binary_error. Returns 0 only on allocation
 * failure. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_GIF_H
#define QA_FORMATS_IMAGES_GIF_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/render.h"

typedef struct qa_gif_frame {
  qa_image_level image;
  int64_t rect_x, rect_y, rect_width, rect_height;
  uint8_t *indices; size_t indices_length;
  uint8_t *palette; size_t palette_length;
  int64_t transparent_index; /* -1 = null */
  int64_t delay_centiseconds;
  int64_t disposal;
} qa_gif_frame;

typedef struct qa_gif_image {
  int64_t width, height;
  qa_gif_frame *frames; size_t frame_count;
  int64_t loop_count; /* -1 = null */
  int64_t background_index;
  uint8_t *global_palette; size_t global_palette_length; /* NULL if none */
} qa_gif_image;
void qa_gif_image_destroy(qa_gif_image *image);

int qa_decode_gif(const uint8_t *bytes, size_t length, const char *source,
                  qa_gif_image *out);

#endif
