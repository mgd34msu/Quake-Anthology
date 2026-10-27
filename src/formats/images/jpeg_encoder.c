/* Port of src/formats/images/jpeg-encoder.ts. GPL-2.0-or-later. */
#include "qa/formats/images/jpeg_encoder.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static const uint8_t natural_order[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
static const uint8_t luminance_quantizers[64] = {
    16, 11, 12, 14, 12, 10, 16, 14, 13, 14, 18, 17, 16, 19, 24, 40,
    26, 24, 22, 22, 24, 49, 35, 37, 29, 40, 58, 51, 61, 60, 57, 51,
    56, 55, 64, 72, 92, 78, 64, 68, 87, 69, 55, 56, 80, 109, 81, 87,
    95, 98, 103, 104, 103, 62, 77, 113, 121, 112, 100, 120, 92, 101, 103, 99};
static const uint8_t chrominance_quantizers[64] = {
    17, 18, 18, 24, 21, 24, 47, 26, 26, 47, 99, 66, 56, 66, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

struct huffman_table {
  const uint8_t *counts; /* 16 */
  const uint8_t *values;
  int value_count;
};
static const uint8_t dc_luminance_counts[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t dc_values[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const uint8_t dc_chrominance_counts[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const uint8_t ac_luminance_counts[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 125};
static const uint8_t ac_luminance_values[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13,
    0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42,
    0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
    0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84,
    0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
static const uint8_t ac_chrominance_counts[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 119};
static const uint8_t ac_chrominance_values[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51,
    0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1,
    0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24,
    0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a,
    0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66,
    0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82,
    0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
    0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9,
    0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

static const struct huffman_table dc_luminance = {dc_luminance_counts, dc_values, 12};
static const struct huffman_table dc_chrominance = {dc_chrominance_counts, dc_values, 12};
static const struct huffman_table ac_luminance = {ac_luminance_counts, ac_luminance_values, 162};
static const struct huffman_table ac_chrominance = {ac_chrominance_counts, ac_chrominance_values, 162};

struct huffman_codes {
  int value[256];
  int size[256];
  int valid[256];
};

static void derive_codes(const struct huffman_table *table,
                         struct huffman_codes *codes) {
  memset(codes, 0, sizeof *codes);
  int code = 0, index = 0;
  for (int length = 0; length < 16; length++) {
    for (int entry = 0; entry < table->counts[length]; entry++) {
      int symbol = table->values[index++];
      codes->value[symbol] = code++;
      codes->size[symbol] = length + 1;
      codes->valid[symbol] = 1;
    }
    code *= 2;
  }
}

struct encoder_output {
  uint8_t *bytes;
  size_t capacity, length;
  uint64_t pending;
  int pending_bits;
  int source_destination;
  int oom;
};

static void out_byte(struct encoder_output *output, int value) {
  if (output->oom) return;
  if (output->length == output->capacity) {
    if (output->source_destination) {
      qa_range_error("SaveJPG destination capacity exceeded; source buffer exhaustion is unsupported");
    }
    size_t capacity = output->capacity * 2;
    uint8_t *grown = (uint8_t *)realloc(output->bytes, capacity);
    if (grown == NULL) { output->oom = 1; return; }
    output->bytes = grown;
    output->capacity = capacity;
  }
  output->bytes[output->length++] = (uint8_t)value;
}

static void out_marker(struct encoder_output *output, int marker,
                       const uint8_t *payload, size_t payload_length) {
  out_byte(output, 255);
  out_byte(output, marker);
  out_byte(output, (int)((payload_length + 2) >> 8));
  out_byte(output, (int)((payload_length + 2) & 255));
  for (size_t i = 0; i < payload_length; i++) out_byte(output, payload[i]);
}

static void out_bits(struct encoder_output *output, int64_t value, int size) {
  uint64_t mask = size == 0 ? 0 : ((1ULL << size) - 1);
  output->pending = output->pending * (1ULL << size) + ((uint64_t)value & mask);
  output->pending_bits += size;
  while (output->pending_bits >= 8) {
    output->pending_bits -= 8;
    int byte = (int)((output->pending >> output->pending_bits) & 255);
    out_byte(output, byte);
    if (byte == 255) out_byte(output, 0);
  }
  output->pending &= output->pending_bits == 0
                           ? 0
                           : ((1ULL << output->pending_bits) - 1);
}

static void out_symbol(struct encoder_output *output,
                       const struct huffman_codes *codes, int symbol) {
  if (symbol < 0 || symbol > 255 || !codes->valid[symbol]) {
    qa_range_error("JPEG encoder Huffman symbol %d", symbol);
  }
  out_bits(output, codes->value[symbol], codes->size[symbol]);
}

static void make_quantizers(const uint8_t *base, int64_t quality, uint8_t out[64]) {
  int64_t bounded = quality < 1 ? 1 : quality > 100 ? 100 : quality;
  int64_t scale = bounded < 50 ? 5000 / bounded : 200 - bounded * 2;
  for (int i = 0; i < 64; i++) {
    int64_t value = ((int64_t)base[i] * scale + 50) / 100;
    out[i] = (uint8_t)(value < 1 ? 1 : value > 255 ? 255 : value);
  }
}

static void make_divisors(const uint8_t table[64], float out[64]) {
  static const double scales[8] = {1, 1.387039845, 1.306562965, 1.175875602,
                                   1, 0.785694958, 0.541196100, 0.275899379};
  for (int zigzag = 0; zigzag < 64; zigzag++) {
    int natural = natural_order[zigzag];
    out[natural] =
        (float)(1.0 / ((double)table[zigzag] * scales[natural / 8] *
                       scales[natural % 8] * 8.0));
  }
}

/* Math.fround points: float storage rounds each kept intermediate. */
static void forward_pass(float *data, int start, int step) {
  float tmp0 = data[start] + data[start + step * 7];
  float tmp7 = data[start] - data[start + step * 7];
  float tmp1 = data[start + step] + data[start + step * 6];
  float tmp6 = data[start + step] - data[start + step * 6];
  float tmp2 = data[start + step * 2] + data[start + step * 5];
  float tmp5 = data[start + step * 2] - data[start + step * 5];
  float tmp3 = data[start + step * 3] + data[start + step * 4];
  float tmp4 = data[start + step * 3] - data[start + step * 4];
  float even10 = tmp0 + tmp3;
  float even13 = tmp0 - tmp3;
  float even11 = tmp1 + tmp2;
  float even12 = tmp1 - tmp2;
  data[start] = even10 + even11;
  data[start + step * 4] = even10 - even11;
  float z1 = (even12 + even13) * (float)0.707106781;
  data[start + step * 2] = even13 + z1;
  data[start + step * 6] = even13 - z1;
  float odd10 = tmp4 + tmp5;
  float odd11 = tmp5 + tmp6;
  float odd12 = tmp6 + tmp7;
  float z5 = (odd10 - odd12) * (float)0.382683433;
  float z2 = (float)0.541196100 * odd10 + z5;
  float z4 = (float)1.306562965 * odd12 + z5;
  float z3 = odd11 * (float)0.707106781;
  float z11 = tmp7 + z3;
  float z13 = tmp7 - z3;
  data[start + step * 5] = z13 + z2;
  data[start + step * 3] = z13 - z2;
  data[start + step] = z11 + z4;
  data[start + step * 7] = z11 - z4;
}

struct encoder_component {
  int sampling;
  int64_t width, height, stride;
  uint8_t *samples;
  float divisors[64];
  const struct huffman_codes *dc_codes, *ac_codes;
  int dc;
};

static int pixel_at(const qa_jpeg_image *image, int64_t index) {
  if (index < 0 || (uint64_t)index >= (uint64_t)image->pixels.length) {
    qa_range_error("JPEG encoder index %lld", (long long)index);
  }
  return image->pixels.bytes[index];
}

static int color_at(const qa_jpeg_image *image, int bottom_up, int64_t image_height,
                    int64_t x, int64_t y, int channel) {
  int64_t row = bottom_up ? image_height - 1 - y : y;
  int64_t offset = (row * (int64_t)image->width + x) * 4;
  int r = pixel_at(image, offset), g = pixel_at(image, offset + 1);
  int b = pixel_at(image, offset + 2);
  if (channel == 0) return (19595 * r + 38470 * g + 7471 * b + 32768) >> 16;
  if (channel == 1) return (-11059 * r - 21709 * g + 32768 * b + 8421375) >> 16;
  return (32768 * r - 27439 * g - 5329 * b + 8421375) >> 16;
}

static void fill_strip(const qa_jpeg_image *image, int bottom_up,
                       struct encoder_component *component, int channel,
                       int64_t mcu_row) {
  int64_t image_width = (int64_t)image->width, image_height = (int64_t)image->height;
  for (int64_t row = 0; row < (int64_t)component->sampling * 8; row++) {
    int64_t y = mcu_row * component->sampling * 8 + row;
    if (y > component->height - 1) y = component->height - 1;
    for (int64_t column = 0; column < component->stride; column++) {
      int64_t at = row * component->stride + column;
      if (channel == 0) {
        int64_t x = column < component->width - 1 ? column : component->width - 1;
        component->samples[at] =
            (uint8_t)color_at(image, bottom_up, image_height, x, y, 0);
      } else {
        int64_t x0 = column * 2 < image_width - 1 ? column * 2 : image_width - 1;
        int64_t x1 = x0 + 1 < image_width - 1 ? x0 + 1 : image_width - 1;
        int64_t y0 = y * 2;
        int64_t y1 = y0 + 1 < image_height - 1 ? y0 + 1 : image_height - 1;
        int sum = color_at(image, bottom_up, image_height, x0, y0, channel) +
                  color_at(image, bottom_up, image_height, x1, y0, channel) +
                  color_at(image, bottom_up, image_height, x0, y1, channel) +
                  color_at(image, bottom_up, image_height, x1, y1, channel);
        component->samples[at] = (uint8_t)((sum + 1 + (int)(column % 2)) >> 2);
      }
    }
  }
}

static void transform(const struct encoder_component *component, int64_t column,
                      int64_t row, int16_t block[64], float workspace[64]) {
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      int64_t index = (row * 8 + y) * component->stride + column * 8 + x;
      workspace[y * 8 + x] = (float)((int)component->samples[index] - 128);
    }
  }
  for (int r = 0; r < 8; r++) forward_pass(workspace, r * 8, 1);
  for (int c = 0; c < 8; c++) forward_pass(workspace, c, 8);
  for (int i = 0; i < 64; i++) {
    float scaled = workspace[i] * component->divisors[i];
    float rounded = (float)((double)scaled + 16384.5);
    block[i] = (int16_t)((int32_t)trunc((double)rounded) - 16384);
  }
}

static int category(int value) {
  if (value == 0) return 0;
  int magnitude = value < 0 ? -value : value;
  int size = 0;
  while (magnitude != 0) { size++; magnitude >>= 1; }
  return size;
}

static void write_block(struct encoder_output *output,
                        struct encoder_component *component, const int16_t block[64]) {
  int dc = block[0];
  int difference = dc - component->dc;
  int size = category(difference);
  out_symbol(output, component->dc_codes, size);
  if (size > 0) out_bits(output, difference < 0 ? difference - 1 : difference, size);
  component->dc = dc;
  int zeroes = 0;
  for (int index = 1; index < 64; index++) {
    int value = block[natural_order[index]];
    if (value == 0) {
      zeroes++;
    } else {
      while (zeroes > 15) { out_symbol(output, component->ac_codes, 240); zeroes -= 16; }
      int value_size = category(value);
      out_symbol(output, component->ac_codes, zeroes * 16 + value_size);
      out_bits(output, value < 0 ? value - 1 : value, value_size);
      zeroes = 0;
    }
  }
  if (zeroes > 0) out_symbol(output, component->ac_codes, 0);
}

static int is_integer(double value) {
  return isfinite(value) && trunc(value) == value;
}

uint8_t *qa_encode_jpeg(const qa_jpeg_image *image, int64_t quality,
                        const qa_jpeg_encoding_profile *profile,
                        size_t *out_length) {
  qa_jpeg_encoding_profile default_profile;
  memset(&default_profile, 0, sizeof default_profile);
  default_profile.kind = QA_JPEG_PROFILE_DIAGNOSTIC_OWNED;
  if (profile == NULL) profile = &default_profile;
  if (!is_integer(image->width) || !is_integer(image->height) ||
      image->width < 1 || image->height < 1 || image->width > 65500 ||
      image->height > 65500) {
    qa_range_error("JPEG encoder dimensions must be integers in 1..65500");
  }
  if ((double)image->pixels.length != image->width * image->height * 4.0) {
    qa_range_error("JPEG encoder requires tightly packed RGBA pixels");
  }
  int64_t width = (int64_t)image->width, height = (int64_t)image->height;
  if (profile->kind == QA_JPEG_PROFILE_SOURCE_DESTINATION &&
      profile->destination_length != (size_t)(width * height * 4)) {
    qa_range_error("SaveJPG destination requires exactly width*height*4 bytes");
  }
  int bottom_up = profile->kind == QA_JPEG_PROFILE_SOURCE_DESTINATION &&
                  profile->row_order_bottom_up;
  static struct huffman_codes lum_dc, lum_ac, chr_dc, chr_ac;
  static int codes_ready;
  if (!codes_ready) {
    derive_codes(&dc_luminance, &lum_dc);
    derive_codes(&ac_luminance, &lum_ac);
    derive_codes(&dc_chrominance, &chr_dc);
    derive_codes(&ac_chrominance, &chr_ac);
    codes_ready = 1;
  }
  uint8_t y_quantizers[64], c_quantizers[64];
  make_quantizers(luminance_quantizers, quality, y_quantizers);
  make_quantizers(chrominance_quantizers, quality, c_quantizers);
  struct encoder_output output;
  memset(&output, 0, sizeof output);
  output.source_destination =
      profile->kind == QA_JPEG_PROFILE_SOURCE_DESTINATION;
  if (output.source_destination) {
    output.bytes = profile->destination;
    output.capacity = profile->destination_length;
  } else {
    output.bytes = (uint8_t *)malloc(4096);
    if (output.bytes == NULL) return NULL;
    output.capacity = 4096;
  }
  out_byte(&output, 255);
  out_byte(&output, 216);
  static const uint8_t app0[14] = {74, 70, 73, 70, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
  out_marker(&output, 224, app0, sizeof app0);
  uint8_t dqt[65];
  dqt[0] = 0;
  memcpy(dqt + 1, y_quantizers, 64);
  out_marker(&output, 219, dqt, sizeof dqt);
  dqt[0] = 1;
  memcpy(dqt + 1, c_quantizers, 64);
  out_marker(&output, 219, dqt, sizeof dqt);
  uint8_t sof[15] = {8, (uint8_t)(height >> 8), (uint8_t)(height & 255),
                     (uint8_t)(width >> 8), (uint8_t)(width & 255), 3,
                     1, 34, 0, 2, 17, 1, 3, 17, 1};
  out_marker(&output, 192, sof, sizeof sof);
  const struct { int selector; const struct huffman_table *table; } dht[4] = {
      {0, &dc_luminance}, {16, &ac_luminance}, {1, &dc_chrominance}, {17, &ac_chrominance}};
  for (int i = 0; i < 4; i++) {
    uint8_t payload[1 + 16 + 162];
    payload[0] = (uint8_t)dht[i].selector;
    memcpy(payload + 1, dht[i].table->counts, 16);
    memcpy(payload + 17, dht[i].table->values, (size_t)dht[i].table->value_count);
    out_marker(&output, 196, payload, 17 + (size_t)dht[i].table->value_count);
  }
  static const uint8_t sos[10] = {3, 1, 0, 2, 17, 3, 17, 0, 63, 0};
  out_marker(&output, 218, sos, sizeof sos);
  int64_t chroma_width = (width + 1) / 2, chroma_height = (height + 1) / 2;
  struct encoder_component components[3];
  const struct { int64_t w, h; int sampling; const uint8_t *table; } specs[3] = {
      {width, height, 2, y_quantizers},
      {chroma_width, chroma_height, 1, c_quantizers},
      {chroma_width, chroma_height, 1, c_quantizers}};
  for (int i = 0; i < 3; i++) {
    struct encoder_component *component = &components[i];
    memset(component, 0, sizeof *component);
    component->sampling = specs[i].sampling;
    component->width = specs[i].w;
    component->height = specs[i].h;
    component->stride = (specs[i].w + 7) / 8 * 8;
    size_t sample_count =
        (size_t)(component->stride * component->sampling * 8);
    component->samples = (uint8_t *)malloc(sample_count);
    if (component->samples == NULL) output.oom = 1;
    make_divisors(specs[i].table, component->divisors);
    component->dc_codes = specs[i].sampling == 2 ? &lum_dc : &chr_dc;
    component->ac_codes = specs[i].sampling == 2 ? &lum_ac : &chr_ac;
  }
  int16_t block[64];
  float workspace[64];
  if (!output.oom) {
    for (int64_t mcu_row = 0; mcu_row < (height + 15) / 16; mcu_row++) {
      for (int channel = 0; channel < 3; channel++) {
        fill_strip(image, bottom_up, &components[channel], channel, mcu_row);
      }
      for (int64_t mcu_column = 0; mcu_column < (width + 15) / 16; mcu_column++) {
        for (int ci = 0; ci < 3; ci++) {
          struct encoder_component *component = &components[ci];
          for (int row = 0; row < component->sampling; row++) {
            for (int column = 0; column < component->sampling; column++) {
              int64_t block_column = mcu_column * component->sampling + column;
              int64_t block_row = mcu_row * component->sampling + row;
              if (block_column >= (component->width + 7) / 8 ||
                  block_row >= (component->height + 7) / 8) {
                memset(block, 0, sizeof block);
                block[0] = (int16_t)component->dc;
              } else {
                transform(component, block_column, row, block, workspace);
              }
              write_block(&output, component, block);
            }
          }
        }
      }
    }
  }
  for (int i = 0; i < 3; i++) free(components[i].samples);
  out_bits(&output, 127, 7);
  out_byte(&output, 255);
  out_byte(&output, 217);
  if (output.oom) {
    if (!output.source_destination) free(output.bytes);
    return NULL;
  }
  *out_length = output.length;
  if (output.source_destination) return output.bytes;
  uint8_t *result = (uint8_t *)malloc(output.length);
  if (result == NULL) { free(output.bytes); return NULL; }
  memcpy(result, output.bytes, output.length);
  free(output.bytes);
  return result;
}
