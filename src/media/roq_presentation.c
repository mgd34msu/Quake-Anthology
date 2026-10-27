/* Port of src/media/roq-presentation.ts. GPL-2.0-or-later. */
#include "qa/media/roq_presentation.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static bool copy_level(qa_image_level *out, int64_t width, int64_t height,
                       const uint8_t *pixels, size_t length) {
  uint8_t *copy = (uint8_t *)malloc(length == 0 ? 1 : length);
  if (copy == NULL) return false;
  memcpy(copy, pixels, length);
  out->width = (double)width;
  out->height = (double)height;
  out->pixels.bytes = copy;
  out->pixels.length = length;
  return true;
}

bool qa_source_roq_shader_pixels(qa_roq_scratch *scratch,
                                 qa_roq_frame_pointer pointer,
                                 qa_image_level *out) {
  uint8_t *view =
      qa_roq_scratch_view(scratch, pointer.offset, 256 * 256 * 4);
  return copy_level(out, 256, 256, view, 256 * 256 * 4);
}

static int read_byte(const uint8_t *source, size_t length, int64_t index) {
  if (index < 0 || index >= (int64_t)length) {
    qa_range_error("Source cinematic resample exceeded linbuf");
  }
  return source[index];
}

bool qa_source_roq_ui_pixels(qa_roq_scratch *scratch,
                             qa_roq_frame_pointer pointer, int64_t width,
                             int64_t height, int64_t draw_width,
                             int64_t draw_height, bool dirty,
                             qa_image_level *out) {
  if (!dirty || (width == draw_width && height == draw_height)) {
    uint8_t *view = qa_roq_scratch_view(scratch, pointer.offset,
                                        draw_width * draw_height * 4);
    return copy_level(out, draw_width, draw_height, view,
                      (size_t)(draw_width * draw_height * 4));
  }
  uint8_t *source =
      qa_roq_scratch_view(scratch, pointer.offset, 512 * 512 * 4);
  size_t source_length = 512 * 512 * 4;
  uint8_t *result = (uint8_t *)malloc(256 * 256 * 4);
  if (result == NULL) return false;
  int64_t xm = width / 256;
  int64_t ym = height / 256;
  int64_t shift = width == 512 ? 9 : 8;
  for (int64_t y = 0; y < 256; y++) {
    for (int64_t x = 0; x < 256; x++) {
      for (int channel = 0; channel < 4; channel++) {
        int value;
        if (xm == 2 && ym == 2) {
          int64_t index = (y << 12) + x * 8 + channel;
          value = (read_byte(source, source_length, index) +
                   read_byte(source, source_length, index + 4) +
                   read_byte(source, source_length, index + 2048) +
                   read_byte(source, source_length, index + 2052)) >> 2;
        } else if (xm == 2 && ym == 1) {
          int64_t index = (y << 11) + x * 8 + channel;
          value = (read_byte(source, source_length, index) +
                   read_byte(source, source_length, index + 4)) >> 1;
        } else {
          value = read_byte(source, source_length,
                            (((y * ym) << shift) + x * xm) * 4 + channel);
        }
        result[(y * 256 + x) * 4 + channel] = (uint8_t)value;
      }
    }
  }
  out->width = 256;
  out->height = 256;
  out->pixels.bytes = result;
  out->pixels.length = 256 * 256 * 4;
  return true;
}
