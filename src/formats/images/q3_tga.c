/* Port of src/formats/images/q3-tga.ts. GPL-2.0-or-later. */
#include "qa/formats/images/q3_tga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"

void qa_image_data_destroy(qa_image_data *image) {
  free(image->pixels);
  image->pixels = NULL;
  image->pixels_length = 0;
}

static void write_pixel(uint8_t *output, int64_t source_pixel, int64_t width,
                        int64_t height, uint8_t red, uint8_t green, uint8_t blue,
                        uint8_t alpha) {
  int64_t source_y = source_pixel / width;
  int64_t source_x = source_pixel - source_y * width;
  int64_t destination = ((height - source_y - 1) * width + source_x) * 4;
  output[destination] = red;
  output[destination + 1] = green;
  output[destination + 2] = blue;
  output[destination + 3] = alpha;
}

static void read_and_write_pixel(qa_binary_reader *reader, uint8_t *output,
                                 int64_t source_pixel, int64_t width,
                                 int64_t height, int64_t pixel_depth) {
  if (pixel_depth == 8) {
    uint8_t intensity = qa_binary_reader_u8(reader);
    write_pixel(output, source_pixel, width, height, intensity, intensity,
                intensity, 255);
    return;
  }
  uint8_t blue = qa_binary_reader_u8(reader);
  uint8_t green = qa_binary_reader_u8(reader);
  uint8_t red = qa_binary_reader_u8(reader);
  uint8_t alpha = pixel_depth == 32 ? qa_binary_reader_u8(reader) : 255;
  write_pixel(output, source_pixel, width, height, red, green, blue, alpha);
}

int qa_decode_q3_tga(const uint8_t *bytes, size_t length, const char *source,
                     qa_q3_tga_warning warning, void *warning_ctx,
                     qa_image_data *out) {
  const char *name = source != NULL ? source : "<buffer>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) return 0;
  if (qa_binary_reader_length(&reader) < 18) {
    qa_binary_error(name, 0, "truncated TGA header");
  }
  int64_t id_length = qa_binary_reader_u8(&reader);
  int64_t color_map_type = qa_binary_reader_u8(&reader);
  int64_t image_type = qa_binary_reader_u8(&reader);
  qa_binary_reader_skip(&reader, 5);
  qa_binary_reader_skip(&reader, 4);
  int64_t width = qa_binary_reader_u16(&reader);
  int64_t height = qa_binary_reader_u16(&reader);
  int64_t pixel_depth = qa_binary_reader_u8(&reader);
  int64_t descriptor = qa_binary_reader_u8(&reader);
  if (image_type != 2 && image_type != 3 && image_type != 10) {
    qa_binary_error(name, 2, "unsupported TGA image type %lld", (long long)image_type);
  }
  if (color_map_type != 0) {
    qa_binary_error(name, 1, "color-mapped TGA images are unsupported");
  }
  if (pixel_depth != 24 && pixel_depth != 32 &&
      !(image_type == 3 && pixel_depth == 8)) {
    qa_binary_error(name, 16, "unsupported %lld-bit depth for TGA image type %lld",
                    (long long)pixel_depth, (long long)image_type);
  }
  if (width == 0 || height == 0) {
    qa_binary_error(name, 12, "invalid TGA dimensions %lldx%lld",
                    (long long)width, (long long)height);
  }
  qa_binary_reader_skip(&reader, id_length);
  int64_t pixel_count = width * height;
  double output_length = (double)pixel_count * 4.0;
  int64_t bytes_per_pixel = pixel_depth / 8;
  if (image_type == 10) {
    int64_t minimum = (pixel_count + 127) / 128 * (bytes_per_pixel + 1);
    if ((int64_t)qa_binary_reader_remaining(&reader) < minimum) {
      qa_binary_error(name, (int64_t)qa_binary_reader_offset(&reader),
                      "truncated TGA RLE pixel data");
    }
  } else if ((int64_t)qa_binary_reader_remaining(&reader) <
             pixel_count * bytes_per_pixel) {
    qa_binary_error(name, (int64_t)qa_binary_reader_offset(&reader),
                    "truncated TGA pixel data");
  }
  if (output_length > 2147483647.0) {
    qa_binary_error(name, 12,
                    "decoded TGA size %.0f overflows the source signed-int allocation",
                    output_length);
  }
  size_t out_length = (size_t)(pixel_count * 4);
  uint8_t *output = out_length == 0 ? NULL : (uint8_t *)malloc(out_length);
  if (out_length != 0 && output == NULL) {
    qa_binary_reader_destroy(&reader);
    return 0;
  }
  if (image_type != 10) {
    for (int64_t pixel = 0; pixel < pixel_count; pixel++) {
      read_and_write_pixel(&reader, output, pixel, width, height, pixel_depth);
    }
  } else {
    int64_t pixel = 0;
    while (pixel < pixel_count) {
      int64_t packet_header = qa_binary_reader_u8(&reader);
      int64_t packet_length = (packet_header & 0x7f) + 1;
      int64_t packet_end = pixel + packet_length < pixel_count
                               ? pixel + packet_length
                               : pixel_count;
      if ((packet_header & 0x80) != 0) {
        uint8_t blue = qa_binary_reader_u8(&reader);
        uint8_t green = qa_binary_reader_u8(&reader);
        uint8_t red = qa_binary_reader_u8(&reader);
        uint8_t alpha = pixel_depth == 32 ? qa_binary_reader_u8(&reader) : 255;
        while (pixel < packet_end) {
          write_pixel(output, pixel, width, height, red, green, blue, alpha);
          pixel++;
        }
      } else {
        while (pixel < packet_end) {
          read_and_write_pixel(&reader, output, pixel, width, height, pixel_depth);
          pixel++;
        }
      }
    }
  }
  qa_binary_reader_destroy(&reader);
  if ((descriptor & 0x20) != 0 && warning != NULL) {
    size_t text_length = strlen(name) + 96;
    char *text = (char *)malloc(text_length);
    if (text != NULL) {
      snprintf(text, text_length,
               "WARNING: '%s' TGA file header declares top-down image, ignoring\n",
               name);
      warning(warning_ctx, text);
      free(text);
    }
  }
  out->width = width;
  out->height = height;
  out->pixels = output;
  out->pixels_length = out_length;
  return 1;
}
