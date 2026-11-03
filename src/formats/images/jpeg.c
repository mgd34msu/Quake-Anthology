/* Port of src/formats/images/jpeg.ts. GPL-2.0-or-later. */
#include "qa/formats/images/jpeg.h"
#include "qa/formats/images/limits.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

/* TS JpegSourceError extends BinaryError and sets name "JpegSourceError",
 * which String(error), worker error transport and diagnostics print. No TS
 * `instanceof BinaryError` catch wraps decodeJpeg, so the raise carries the
 * subclass name; the code also marks it. */
noreturn void qa_jpeg_source_error(const char *source, int64_t offset,
                                   const char *format, ...) {
  char text[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("JpegSourceError", QA_JPEG_SOURCE_ERROR_CODE, offset, "%s:%lld: JPEG: %s", source,
           (long long)offset, text);
}

/* TS plain `throw new Error(msg)`. */
static noreturn void jpeg_error(const char *message) {
  qa_raise("Error", 0, 0, "%s", message);
}

static const uint8_t natural_order[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static int64_t at_i16(const int16_t *values, size_t count, int64_t index) {
  if (index < 0 || (uint64_t)index >= (uint64_t)count) {
    qa_range_error("JPEG internal index %lld", (long long)index);
  }
  return values[index];
}
static int64_t at_u8(const uint8_t *values, size_t count, int64_t index) {
  if (index < 0 || (uint64_t)index >= (uint64_t)count) {
    qa_range_error("JPEG internal index %lld", (long long)index);
  }
  return values[index];
}

struct warnings {
  qa_jpeg_print print;
  void *ctx;
  int64_t count;
};

static void warnings_emit(struct warnings *warnings, const char *format, ...) {
  if (warnings->count == 0 && warnings->print != NULL) {
    va_list args;
    va_start(args, format);
    va_list sizing;
    va_copy(sizing, args);
    int needed = vsnprintf(NULL, 0, format, sizing);
    va_end(sizing);
    if (needed >= 0) {
      char *text = (char *)malloc((size_t)needed + 2);
      if (text != NULL) {
        vsnprintf(text, (size_t)needed + 1, format, args);
        text[needed] = '\n';
        text[needed + 1] = '\0';
        warnings->print(warnings->ctx, text);
        free(text);
      }
    }
    va_end(args);
  }
  warnings->count++;
}

struct input {
  const uint8_t *bytes;
  size_t length;
  const char *source;
  struct warnings *warnings;
  size_t offset;
  int unread_marker;
  int64_t discarded_bytes;
  double fabricated_bytes;
  int64_t scans;
};

static noreturn void input_fail(const struct input *input, const char *message) {
  qa_binary_error(input->source, (int64_t)input->offset, "JPEG: %s", message);
}

static int input_byte(struct input *input) {
  if (input->offset >= input->length) input_fail(input, "truncated input");
  return input->bytes[input->offset++];
}

static int input_word(struct input *input) {
  int high = input_byte(input); /* big-endian: TS reads the high byte first */
  return high * 256 + input_byte(input);
}

static void input_next_marker(struct input *input) {
  for (;;) {
    int marker = input_byte(input);
    while (marker != 255) {
      input->discarded_bytes++;
      marker = input_byte(input);
    }
    do { marker = input_byte(input); } while (marker == 255);
    if (marker == 0) {
      input->discarded_bytes += 2;
      continue;
    }
    if (input->discarded_bytes != 0) {
      warnings_emit(input->warnings,
                    "Corrupt JPEG data: %lld extraneous bytes before marker 0x%02x",
                    (long long)input->discarded_bytes, marker);
      input->discarded_bytes = 0;
    }
    input->unread_marker = marker;
    return;
  }
}

static int input_marker(struct input *input) {
  if (input->unread_marker == 0) input_next_marker(input);
  int marker = input->unread_marker;
  input->unread_marker = 0;
  return marker;
}

static void input_restart(struct input *input, int desired) {
  if (input->unread_marker == 0) input_next_marker(input);
  if (input->unread_marker == 208 + desired) {
    input->unread_marker = 0;
    return;
  }
  warnings_emit(input->warnings,
                "Corrupt JPEG data: found marker 0x%02x instead of RST%d",
                input->unread_marker, desired);
  for (;;) {
    int marker = input->unread_marker;
    if (marker < 192 || marker == 208 + ((desired - 1) & 7) ||
        marker == 208 + ((desired - 2) & 7)) {
      input_next_marker(input);
    } else if (marker < 208 || marker > 215 ||
               marker == 208 + ((desired + 1) & 7) ||
               marker == 208 + ((desired + 2) & 7)) {
      return;
    } else {
      input->unread_marker = 0;
      return;
    }
  }
}

static void input_skip(struct input *input, int64_t size) {
  if (size <= 0) return;
  if ((uint64_t)size > (uint64_t)(input->length - input->offset)) {
    input->offset = input->length;
    input_fail(input, "truncated input");
  }
  input->offset += (size_t)size;
}

struct entropy {
  struct input *input;
  uint32_t value;
  int remaining;
  int printed_end;
};

static void entropy_fill(struct entropy *entropy, int required) {
  while (entropy->remaining < 25) {
    int next = 0;
    if (entropy->input->unread_marker != 0) {
      if (entropy->remaining >= required) break;
      /* Past the end of entropy data the decoder synthesizes zero bits
       * without consuming input; bound that. */
      double limit = (double)entropy->input->length * 16;
      if (++entropy->input->fabricated_bytes > (limit > 65536 ? limit : 65536)) {
        input_fail(entropy->input, "entropy data ends far short of the declared image");
      }
      if (!entropy->printed_end) {
        warnings_emit(entropy->input->warnings,
                      "Corrupt JPEG data: premature end of data segment");
        entropy->printed_end = 1;
      }
    } else {
      next = input_byte(entropy->input);
      if (next == 255) {
        int following = input_byte(entropy->input);
        while (following == 255) following = input_byte(entropy->input);
        if (following != 0) {
          entropy->input->unread_marker = following;
          continue;
        }
      }
    }
    entropy->value = (entropy->value << 8) | (uint32_t)next;
    entropy->remaining += 8;
  }
}

static int entropy_bits(struct entropy *entropy, int count) {
  if (entropy->remaining < count) entropy_fill(entropy, count);
  if (count == 0) return 0;
  entropy->remaining -= count;
  return (int)((entropy->value >> entropy->remaining) & ((1u << count) - 1));
}

static int entropy_signed(struct entropy *entropy, int count) {
  if (count > 15) {
    input_fail(entropy->input, "DC category exceeds source sign-extension table");
  }
  int value = entropy_bits(entropy, count);
  return count > 0 && value < (1 << (count - 1)) ? value + 1 - (1 << count) : value;
}

static void entropy_restart(struct entropy *entropy, int desired) {
  entropy->input->discarded_bytes += entropy->remaining / 8;
  entropy->remaining = 0;
  input_restart(entropy->input, desired);
  entropy->printed_end = 0;
}

/* Derived Huffman map keyed by (1<<length)+code, -1 absent. */
#define HUFF_MAP_SIZE 131072
static int entropy_symbol(struct entropy *entropy, const int16_t *table) {
  if (entropy->remaining < 8) entropy_fill(entropy, 0);
  int minimum = 1;
  if (entropy->remaining >= 8) {
    int look = (int)((entropy->value >> (entropy->remaining - 8)) & 255);
    for (int length = 1; length <= 8; length++) {
      int value = table[(1 << length) + (look >> (8 - length))];
      if (value >= 0) {
        entropy->remaining -= length;
        return value;
      }
    }
    minimum = 9;
  }
  int code = (1 << minimum) + entropy_bits(entropy, minimum);
  for (int length = minimum; length <= 16; length++) {
    int value = table[code];
    if (value >= 0) return value;
    code = code * 2 + entropy_bits(entropy, 1);
  }
  warnings_emit(entropy->input->warnings, "Corrupt JPEG data: bad Huffman code");
  return 0;
}

struct progression {
  int16_t *coefficients;
  size_t coefficient_count;
  int8_t bits[64];
  int has_table;
  uint16_t quantizer[64];
  float multipliers[64];
};

struct component {
  int id, h, v, quantizer;
  int64_t width, height, stride;
  uint8_t *pixels;
  size_t pixels_length;
  uint8_t *output_samples; /* NULL when buffered */
  size_t output_samples_length;
  struct progression *progression; /* NULL when not buffered */
};

struct frame {
  int progressive;
  int64_t width, height, max_h, max_v, columns, rows;
  struct component *components;
  size_t component_count;
};

struct descriptor { int id, h, v, quantizer; };
struct frame_header {
  int progressive, arithmetic, precision;
  int64_t width, height;
  struct descriptor *components;
  size_t component_count;
};

struct scan_header {
  struct { int id, selector; } members[4];
  size_t member_count;
  int start, end, approximation;
};

typedef enum color_space {
  CS_GRAYSCALE, CS_RGB, CS_YCBCR, CS_CMYK, CS_YCCK, CS_UNKNOWN
} color_space;

struct huff_table {
  uint8_t counts[16];
  uint8_t *values;
  int total;
  int16_t *derived; /* HUFF_MAP_SIZE, NULL until derived */
};

static void free_frame(struct frame *frame) {
  if (frame == NULL) return;
  for (size_t i = 0; i < frame->component_count; i++) {
    free(frame->components[i].pixels);
    free(frame->components[i].output_samples);
    if (frame->components[i].progression != NULL) {
      free(frame->components[i].progression->coefficients);
      free(frame->components[i].progression);
    }
  }
  free(frame->components);
  free(frame);
}

static int read_frame(struct input *input, int progressive, int arithmetic,
                      int duplicate, struct frame_header *out) {
  int length = input_word(input);
  int precision = input_byte(input);
  int64_t height = input_word(input);
  int64_t width = input_word(input);
  int count = input_byte(input);
  if (duplicate) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "Invalid JPEG file structure: two SOF markers");
  }
  if (width == 0 || height == 0 || count == 0) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "Empty JPEG image (DNL not supported)");
  }
  if (length != 8 + count * 3) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "Bogus marker length");
  }
  struct descriptor *descriptors =
      (struct descriptor *)malloc((size_t)count * sizeof *descriptors);
  if (descriptors == NULL) return 0;
  qa_cleanup node;
  qa_cleanup_push(&node, free, descriptors);
  for (int i = 0; i < count; i++) {
    descriptors[i].id = input_byte(input);
    int sampling = input_byte(input);
    descriptors[i].h = sampling >> 4;
    descriptors[i].v = sampling & 15;
    descriptors[i].quantizer = input_byte(input);
  }
  qa_cleanup_pop(&node);
  out->progressive = progressive;
  out->arithmetic = arithmetic;
  out->precision = precision;
  out->width = width;
  out->height = height;
  out->components = descriptors;
  out->component_count = (size_t)count;
  return 1;
}

static int64_t ceil_div(int64_t a, int64_t b) { return (a + b - 1) / b; }

static struct frame *prepare_frame(struct input *input,
                                   const struct frame_header *header, int buffered) {
  int64_t width = header->width, height = header->height;
  size_t count = header->component_count;
  if (width > 65500 || height > 65500) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "Maximum supported image dimension is 65500 pixels");
  }
  if (!qa_image_size_allowed((double)width, (double)height)) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "JPEG dimensions %lldx%lld exceed the decoder limit", (long long)width,
                         (long long)height);
  }
  if (header->precision != 8) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "Unsupported JPEG data precision %d", header->precision);
  }
  if (count > 10) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "Too many color components: %zu, max 10", count);
  }
  int64_t max_h = 1, max_v = 1;
  for (size_t i = 0; i < count; i++) {
    int h = header->components[i].h, v = header->components[i].v;
    if (h < 1 || h > 4 || v < 1 || v > 4) {
      qa_jpeg_source_error(input->source, (int64_t)input->offset,
                           "Bogus sampling factors");
    }
    if (h > max_h) max_h = h;
    if (v > max_v) max_v = v;
  }
  struct frame *frame = (struct frame *)calloc(1, sizeof *frame);
  if (frame == NULL) return NULL;
  frame->progressive = header->progressive;
  frame->width = width;
  frame->height = height;
  frame->max_h = max_h;
  frame->max_v = max_v;
  frame->columns = ceil_div(width, 8 * max_h);
  frame->rows = ceil_div(height, 8 * max_v);
  frame->components =
      (struct component *)calloc(count, sizeof *frame->components);
  if (frame->components == NULL) { free(frame); return NULL; }
  frame->component_count = count;
  for (size_t i = 0; i < count; i++) {
    struct component *component = &frame->components[i];
    const struct descriptor *d = &header->components[i];
    component->id = d->id;
    component->h = d->h;
    component->v = d->v;
    component->quantizer = d->quantizer;
    component->width = ceil_div(width * d->h, max_h);
    component->height = ceil_div(height * d->v, max_v);
    component->stride = frame->columns * d->h * 8;
    size_t plane = buffered
                       ? (size_t)(component->stride * frame->rows * d->v * 8)
                       : 0;
    component->pixels_length = plane;
    component->pixels = plane == 0 ? NULL : (uint8_t *)calloc(plane, 1);
    if (plane != 0 && component->pixels == NULL) { free_frame(frame); return NULL; }
    if (buffered) {
      struct progression *progression =
          (struct progression *)calloc(1, sizeof *progression);
      if (progression == NULL) { free_frame(frame); return NULL; }
      progression->coefficient_count = plane;
      progression->coefficients =
          plane == 0 ? NULL : (int16_t *)calloc(plane, sizeof(int16_t));
      if (plane != 0 && progression->coefficients == NULL) {
        component->progression = progression;
        free_frame(frame);
        return NULL;
      }
      for (int b = 0; b < 64; b++) progression->bits[b] = -1;
      component->progression = progression;
    } else {
      size_t samples = (size_t)(width * height);
      component->output_samples_length = samples;
      component->output_samples =
          samples == 0 ? NULL : (uint8_t *)calloc(samples, 1);
      if (samples != 0 && component->output_samples == NULL) {
        free_frame(frame);
        return NULL;
      }
    }
  }
  return frame;
}

static void read_scan_header(struct input *input,
                             const struct descriptor *header_components,
                             const struct component *frame_components,
                             size_t component_count, struct scan_header *out) {
  int length = input_word(input);
  int count = input_byte(input);
  if (count < 1 || count > 4 || length != 6 + count * 2) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "Bogus marker length");
  }
  out->member_count = 0;
  for (int i = 0; i < count; i++) {
    int id = input_byte(input);
    int selector = input_byte(input);
    int found = 0;
    for (size_t c = 0; c < component_count; c++) {
      int candidate = header_components != NULL ? header_components[c].id
                                                : frame_components[c].id;
      if (candidate == id) found = 1;
    }
    if (!found) {
      qa_jpeg_source_error(input->source, (int64_t)input->offset,
                           "Invalid component ID %d in SOS", id);
    }
    for (size_t m = 0; m < out->member_count; m++) {
      if (out->members[m].id == id) out->members[m].selector = selector;
    }
    out->members[out->member_count].id = id;
    out->members[out->member_count].selector = selector;
    out->member_count++;
  }
  out->start = input_byte(input);
  out->end = input_byte(input);
  out->approximation = input_byte(input);
}

static void read_quantizers(struct input *input, uint16_t tables[4][64],
                            int present[4]) {
  int64_t remaining = input_word(input) - 2;
  while (remaining > 0) {
    int selector = input_byte(input);
    int precision = selector >> 4;
    int id = selector & 15;
    if (id > 3) {
      qa_jpeg_source_error(input->source, (int64_t)input->offset,
                           "Bogus DQT index %d", id);
    }
    uint16_t table[64];
    for (int i = 0; i < 64; i++) {
      table[natural_order[i]] =
          (uint16_t)(precision == 0 ? input_byte(input) : input_word(input));
    }
    memcpy(tables[id], table, sizeof table);
    present[id] = 1;
    remaining -= precision == 0 ? 65 : 129;
  }
}

static int read_huffman(struct input *input, struct huff_table *tables[256]) {
  int64_t remaining = input_word(input) - 2;
  while (remaining > 0) {
    int selector = input_byte(input);
    uint8_t counts[16];
    int total = 0;
    for (int length = 0; length < 16; length++) {
      counts[length] = (uint8_t)input_byte(input);
      total += counts[length];
    }
    remaining -= 17;
    if (total > 256 || total > remaining) {
      qa_jpeg_source_error(input->source, (int64_t)input->offset,
                           "Bogus DHT counts");
    }
    uint8_t *values = total == 0 ? NULL : (uint8_t *)malloc((size_t)total);
    if (total != 0 && values == NULL) return 0;
    qa_cleanup node;
    qa_cleanup_push(&node, free, values);
    for (int i = 0; i < total; i++) values[i] = (uint8_t)input_byte(input);
    qa_cleanup_pop(&node);
    remaining -= total;
    int table_index = (selector & 16) != 0 ? selector - 16 : selector;
    if (table_index >= 4) {
      free(values);
      qa_jpeg_source_error(input->source, (int64_t)input->offset,
                           "Bogus DHT index %d", table_index);
    }
    struct huff_table *table =
        (struct huff_table *)calloc(1, sizeof *table);
    if (table == NULL) { free(values); return 0; }
    memcpy(table->counts, counts, sizeof counts);
    table->values = values;
    table->total = total;
    if (tables[selector] != NULL) {
      free(tables[selector]->values);
      free(tables[selector]->derived);
      free(tables[selector]);
    }
    tables[selector] = table;
  }
  return 1;
}

static const int16_t *derive_huffman(struct input *input,
                                     struct huff_table *tables[256],
                                     int selector, int *oom) {
  struct huff_table *raw = selector >= 0 && selector < 256 ? tables[selector] : NULL;
  if (raw == NULL) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "Huffman table 0x%02x was not defined", selector & 15);
  }
  if (raw->derived != NULL) return raw->derived;
  int16_t *table = (int16_t *)malloc(HUFF_MAP_SIZE * sizeof *table);
  if (table == NULL) { *oom = 1; return NULL; }
  for (int i = 0; i < HUFF_MAP_SIZE; i++) table[i] = -1;
  int code = 0, value_index = 0;
  for (int length = 1; length <= 16; length++) {
    int count = raw->counts[length - 1];
    if (length <= 8 && code + count > (1 << length) && count != 0) {
      free(table);
      input_fail(input, "oversubscribed Huffman table exceeds source lookahead allocation");
    }
    for (int symbol = 0; symbol < count; symbol++, code++) {
      int value = raw->values[value_index++];
      if (code < (1 << length)) table[(1 << length) + code] = (int16_t)value;
    }
    code *= 2;
  }
  raw->derived = table;
  return table;
}

static void read_arithmetic_tables(struct input *input) {
  int64_t remaining = input_word(input) - 2;
  while (remaining > 0) {
    int index = input_byte(input);
    int value = input_byte(input);
    remaining -= 2;
    if (index >= 32) {
      qa_jpeg_source_error(input->source, (int64_t)input->offset,
                           "Bogus DAC index %d", index);
    }
    if (index < 16 && (value & 15) > (value >> 4)) {
      qa_jpeg_source_error(input->source, (int64_t)input->offset,
                           "Bogus DAC value 0x%x", value);
    }
  }
}

static int read_jfif(struct input *input) {
  int64_t remaining = input_word(input) - 2;
  int recognized = 0;
  if (remaining >= 14) {
    uint8_t prefix[14];
    for (int i = 0; i < 14; i++) prefix[i] = (uint8_t)input_byte(input);
    remaining -= 14;
    recognized = memcmp(prefix, "JFIF\0", 5) == 0;
    if (recognized && prefix[5] != 1) {
      warnings_emit(input->warnings, "Warning: unknown JFIF revision number %d.%02d",
                    prefix[5], prefix[6]);
    }
  }
  input_skip(input, remaining);
  return recognized;
}

static int read_adobe(struct input *input, int *has_transform) {
  int64_t remaining = input_word(input) - 2;
  int transform = 0;
  *has_transform = 0;
  if (remaining >= 12) {
    uint8_t prefix[12];
    for (int i = 0; i < 12; i++) prefix[i] = (uint8_t)input_byte(input);
    remaining -= 12;
    if (memcmp(prefix, "Adobe", 5) == 0) {
      transform = prefix[11];
      *has_transform = 1;
    }
  }
  input_skip(input, remaining);
  return transform;
}

static int64_t descale_int(int64_t value, int bits) {
  int64_t divisor = 1LL << bits;
  int64_t shifted = value + (1LL << (bits - 1));
  int64_t result = shifted / divisor;
  if (shifted % divisor != 0 && shifted < 0) result--;
  return result;
}

static int64_t clamp255(int64_t value) {
  return value < 0 ? 0 : value > 255 ? 255 : value;
}

static void float_multipliers(const uint16_t quantizer[64], float out[64]) {
  static const double scales[8] = {1, 1.387039845, 1.306562965, 1.175875602,
                                   1, 0.785694958, 0.541196100, 0.275899379};
  for (int i = 0; i < 64; i++) {
    out[i] = (float)((double)quantizer[i] * scales[i / 8] * scales[i % 8]);
  }
}

static void inverse_pass(const float *values, int start, int step, float *output,
                         int output_start, int output_step) {
  float a0 = values[start], a1 = values[start + step];
  float a2 = values[start + step * 2], a3 = values[start + step * 3];
  float a4 = values[start + step * 4], a5 = values[start + step * 5];
  float a6 = values[start + step * 6], a7 = values[start + step * 7];
  float even10 = a0 + a4;
  float even11 = a0 - a4;
  float even13 = a2 + a6;
  float even12 = (a2 - a6) * (float)1.414213562 - even13;
  float tmp0 = even10 + even13;
  float tmp3 = even10 - even13;
  float tmp1 = even11 + even12;
  float tmp2 = even11 - even12;
  float z13 = a5 + a3;
  float z10 = a5 - a3;
  float z11 = a1 + a7;
  float z12 = a1 - a7;
  float tmp7 = z11 + z13;
  float odd11 = (z11 - z13) * (float)1.414213562;
  float z5 = (z10 + z12) * (float)1.847759065;
  float odd10 = (float)1.082392200 * z12 - z5;
  float odd12 = (float)-2.613125930 * z10 + z5;
  float tmp6 = odd12 - tmp7;
  float tmp5 = odd11 - tmp6;
  float tmp4 = odd10 + tmp5;
  output[output_start] = tmp0 + tmp7;
  output[output_start + output_step * 7] = tmp0 - tmp7;
  output[output_start + output_step] = tmp1 + tmp6;
  output[output_start + output_step * 6] = tmp1 - tmp6;
  output[output_start + output_step * 2] = tmp2 + tmp5;
  output[output_start + output_step * 5] = tmp2 - tmp5;
  output[output_start + output_step * 4] = tmp3 + tmp4;
  output[output_start + output_step * 3] = tmp3 - tmp4;
}

static int idct_sample(float value) {
  int64_t index = descale_int((int64_t)trunc((double)value), 3) & 1023;
  if (index < 128) return (int)index + 128;
  if (index < 512) return 255;
  if (index < 896) return 0;
  return (int)index - 896;
}

struct scan_component {
  struct component *component;
  int32_t prediction_dc;
  const int16_t *dc, *ac;
  const float *multipliers;
  float *owned_multipliers;
};

static void decode_block(struct entropy *entropy, struct scan_component *scan,
                         int64_t column, int64_t row, float block[64]) {
  struct component *component = scan->component;
  struct progression *storage = component->progression;
  int64_t offset = (row * (component->stride / 8) + column) * 64;
  if (storage == NULL) {
    for (int i = 0; i < 64; i++) block[i] = 0;
  } else {
    for (int i = 0; i < 64; i++) {
      block[i] = (float)at_i16(storage->coefficients,
                               storage->coefficient_count, offset + i);
    }
  }
  int dc_size = entropy_symbol(entropy, scan->dc);
  scan->prediction_dc =
      (int32_t)((uint32_t)scan->prediction_dc + (uint32_t)entropy_signed(entropy, dc_size));
  block[0] = (float)(int16_t)(scan->prediction_dc & 0xffff);
  int index = 1;
  while (index < 64) {
    int symbol = entropy_symbol(entropy, scan->ac);
    int run = symbol >> 4;
    int size = symbol & 15;
    if (size == 0) {
      if (run != 15) break;
      index += 16;
    } else {
      index += run;
      int natural = natural_order[index < 63 ? index : 63];
      index++;
      block[natural] = (float)entropy_signed(entropy, size);
    }
  }
  if (storage != NULL) {
    for (int i = 0; i < 64; i++) {
      if (offset + i < 0 || (uint64_t)(offset + i) >= storage->coefficient_count) {
        qa_range_error("JPEG internal index %lld", (long long)(offset + i));
      }
      storage->coefficients[offset + i] = (int16_t)(int32_t)block[i];
    }
  } else {
    for (int i = 0; i < 64; i++) block[i] = block[i] * scan->multipliers[i];
  }
}

static void inverse_block(float block[64], float workspace[64]) {
  for (int x = 0; x < 8; x++) {
    int dc_only = 1;
    for (int y = 1; y < 8; y++) dc_only = dc_only && block[y * 8 + x] == 0;
    if (dc_only) {
      for (int y = 0; y < 8; y++) workspace[y * 8 + x] = block[x];
    } else {
      inverse_pass(block, x, 8, workspace, x, 8);
    }
  }
  for (int y = 0; y < 8; y++) inverse_pass(workspace, y * 8, 1, block, y * 8, 1);
}

static void write_block(struct input *input, struct component *component,
                        int64_t column, int64_t row, float block[64],
                        float workspace[64]) {
  inverse_block(block, workspace);
  for (int y = 0; y < 8; y++) {
    int64_t destination = (row * 8 + y) * component->stride + column * 8;
    if (destination + 8 > (int64_t)component->pixels_length) {
      input_fail(input, "block outside component plane");
    }
    for (int x = 0; x < 8; x++) {
      component->pixels[destination + x] = (uint8_t)idct_sample(block[y * 8 + x]);
    }
  }
}

struct strip_plane {
  uint8_t *pixels;
  uint8_t *initialized;
  size_t pixels_length;
  int64_t *pointers; /* -1 = TS Array hole */
  size_t pointer_count;
  int64_t starts[2];
};

struct one_pass {
  struct input *input;
  struct frame *frame;
  color_space colorspace;
  int context;
  struct strip_plane *planes;
  int which;
  int64_t output_row;
};

static void free_one_pass(struct one_pass *strip) {
  if (strip == NULL) return;
  if (strip->planes != NULL) {
    for (size_t i = 0; i < strip->frame->component_count; i++) {
      free(strip->planes[i].pixels);
      free(strip->planes[i].initialized);
      free(strip->planes[i].pointers);
    }
    free(strip->planes);
  }
  free(strip);
}

static struct strip_plane *strip_plane_at(struct one_pass *strip, int64_t index) {
  if (index < 0 || (uint64_t)index >= (uint64_t)strip->frame->component_count) {
    input_fail(strip->input, "scan output uses an uninitialized source component pointer");
  }
  return &strip->planes[index];
}

static int64_t strip_row_pointer(struct one_pass *strip, struct strip_plane *plane,
                                 int64_t row, int which) {
  int64_t index = plane->starts[which] + row;
  if (index < 0 || (uint64_t)index >= (uint64_t)plane->pointer_count) {
    input_fail(strip->input, "row pointer outside source pointer allocation");
  }
  int64_t pointer = plane->pointers[index];
  if (pointer < 0) input_fail(strip->input, "uninitialized source row pointer");
  return pointer;
}

static int64_t strip_pointer_at(struct strip_plane *plane, int64_t index) {
  if (index < 0 || (uint64_t)index >= (uint64_t)plane->pointer_count ||
      plane->pointers[index] < 0) {
    qa_range_error("JPEG internal index %lld", (long long)index);
  }
  return plane->pointers[index];
}

static struct one_pass *make_one_pass(struct input *input, struct frame *frame,
                                      color_space colorspace) {
  struct one_pass *strip = (struct one_pass *)calloc(1, sizeof *strip);
  if (strip == NULL) return NULL;
  strip->input = input;
  strip->frame = frame;
  strip->colorspace = colorspace;
  strip->context = 0;
  for (size_t i = 0; i < frame->component_count; i++) {
    struct component *c = &frame->components[i];
    if (frame->max_h == (int64_t)c->h * 2 && frame->max_v == (int64_t)c->v * 2 &&
        c->width > 2) {
      strip->context = 1;
    }
  }
  strip->planes =
      (struct strip_plane *)calloc(frame->component_count, sizeof *strip->planes);
  if (strip->planes == NULL) { free(strip); return NULL; }
  for (size_t i = 0; i < frame->component_count; i++) {
    struct component *component = &frame->components[i];
    struct strip_plane *plane = &strip->planes[i];
    int64_t v = component->v;
    int64_t stride = ceil_div(component->width, 8) * 8;
    int64_t rows = v * (strip->context ? 10 : 8);
    plane->starts[0] = strip->context ? v : 0;
    plane->starts[1] = strip->context ? v * 13 : 0;
    plane->pointer_count = (size_t)(strip->context ? v * 24 : rows);
    plane->pixels_length = (size_t)(stride * rows);
    plane->pointers =
        (int64_t *)malloc(plane->pointer_count * sizeof(int64_t));
    plane->pixels = (uint8_t *)calloc(plane->pixels_length, 1);
    plane->initialized = (uint8_t *)calloc(plane->pixels_length, 1);
    if (plane->pointers == NULL || plane->pixels == NULL ||
        plane->initialized == NULL) {
      free_one_pass(strip);
      return NULL;
    }
    for (size_t p = 0; p < plane->pointer_count; p++) plane->pointers[p] = -1;
    for (int s = 0; s < 2; s++) {
      for (int64_t row = 0; row < rows; row++) {
        plane->pointers[plane->starts[s] + row] = row * stride;
      }
    }
    if (strip->context) {
      for (int64_t row = 0; row < v * 2; row++) {
        plane->pointers[plane->starts[1] + v * 6 + row] = (v * 8 + row) * stride;
        plane->pointers[plane->starts[1] + v * 8 + row] = (v * 6 + row) * stride;
      }
      for (int64_t row = 0; row < v; row++) {
        plane->pointers[plane->starts[0] - v + row] = 0;
      }
    }
  }
  return strip;
}

static void strip_write(struct one_pass *strip, int64_t index, int64_t column,
                        int64_t row, float block[64], float workspace[64]) {
  int64_t count = (int64_t)strip->frame->component_count;
  int64_t physical = (int64_t)strip->which * count + index;
  if (strip->context && physical >= count * 2) {
    input_fail(strip->input, "component pointer outside source xbuffer allocation");
  }
  struct strip_plane *plane =
      strip_plane_at(strip, strip->context ? physical % count : index);
  int which = strip->context && physical >= count ? 1 : 0;
  inverse_block(block, workspace);
  for (int y = 0; y < 8; y++) {
    int64_t destination =
        strip_row_pointer(strip, plane, row * 8 + y, which) + column * 8;
    if (destination < 0 || destination + 8 > (int64_t)plane->pixels_length) {
      input_fail(strip->input, "IDCT write outside source sample allocation");
    }
    for (int x = 0; x < 8; x++) {
      plane->pixels[destination + x] = (uint8_t)idct_sample(block[y * 8 + x]);
      plane->initialized[destination + x] = 1;
    }
  }
}

static int strip_read(struct one_pass *strip, struct strip_plane *plane,
                      int64_t row, int64_t column) {
  int64_t offset = strip_row_pointer(strip, plane, row, strip->which) + column;
  if (offset < 0 || offset >= (int64_t)plane->pixels_length) {
    input_fail(strip->input, "upsampling read outside source sample allocation");
  }
  if (plane->initialized[offset] != 1) {
    input_fail(strip->input, "upsampling reads an uninitialized source sample");
  }
  return plane->pixels[offset];
}

static void strip_emit_group(struct one_pass *strip, int64_t group) {
  struct frame *frame = strip->frame;
  if (strip->output_row >= frame->height) return;
  for (size_t i = 0; i < frame->component_count; i++) {
    struct component *component = &frame->components[i];
    struct strip_plane *plane = strip_plane_at(strip, (int64_t)i);
    if (component->output_samples == NULL) {
      jpeg_error("JPEG one-pass component lacks output samples");
    }
    int64_t h = frame->max_h / component->h, v = frame->max_v / component->v;
    if (h == 1 && v == 1) continue;
    for (int64_t y = 0; y < frame->max_v; y++) {
      int64_t row = group * component->v + y / v;
      for (int64_t x = 0; x < frame->width; x++) {
        int64_t column = x / h;
        int center = strip_read(strip, plane, row, column);
        int value = center;
        if (h == 2 && component->width > 2 && (v == 1 || v == 2)) {
          int64_t neighbor = column + (x % 2 == 0 ? -1 : 1);
          if (neighbor < 0) neighbor = 0;
          if (neighbor > component->width - 1) neighbor = component->width - 1;
          int horizontal = strip_read(strip, plane, row, neighbor);
          if (v == 1) {
            value = (center * 3 + horizontal + (x % 2 == 0 ? 1 : 2)) >> 2;
          } else {
            int64_t adjacent = row + (y % 2 == 0 ? -1 : 1);
            int vertical = strip_read(strip, plane, adjacent, column);
            int diagonal = strip_read(strip, plane, adjacent, neighbor);
            value = (center * 9 + horizontal * 3 + vertical * 3 + diagonal +
                     (x % 2 == 0 ? 8 : 7)) >>
                    4;
          }
        }
        if (strip->output_row + y < frame->height) {
          component->output_samples[(strip->output_row + y) * frame->width + x] =
              (uint8_t)value;
        }
      }
    }
  }
  int64_t emit_rows = frame->max_v < frame->height - strip->output_row
                          ? frame->max_v
                          : frame->height - strip->output_row;
  for (int64_t y = 0; y < emit_rows; y++) {
    if (strip->colorspace == CS_YCBCR || strip->colorspace == CS_YCCK) {
      for (int64_t x = 0; x < frame->width; x++) {
        for (size_t i = 0; i < frame->component_count; i++) {
          struct component *component = &frame->components[i];
          if (component->h != frame->max_h || component->v != frame->max_v) continue;
          if (component->output_samples == NULL) {
            jpeg_error("JPEG one-pass component lacks output samples");
          }
          component->output_samples[(strip->output_row + y) * frame->width + x] =
              (uint8_t)strip_read(strip, strip_plane_at(strip, (int64_t)i),
                                  group * component->v + y, x);
        }
      }
    } else {
      for (size_t i = 0; i < frame->component_count; i++) {
        struct component *component = &frame->components[i];
        if (component->h != frame->max_h || component->v != frame->max_v) continue;
        if (component->output_samples == NULL) {
          jpeg_error("JPEG one-pass component lacks output samples");
        }
        for (int64_t x = 0; x < frame->width; x++) {
          component->output_samples[(strip->output_row + y) * frame->width + x] =
              (uint8_t)strip_read(strip, strip_plane_at(strip, (int64_t)i),
                                  group * component->v + y, x);
        }
      }
    }
  }
  strip->output_row += frame->max_v;
}

static void strip_finish_row(struct one_pass *strip, int64_t imcu) {
  if (!strip->context) {
    for (int group = 0; group < 8; group++) strip_emit_group(strip, group);
    return;
  }
  if (imcu > 0) strip_emit_group(strip, 9);
  int64_t available = 7;
  if (imcu == strip->frame->rows - 1) {
    for (size_t i = 0; i < strip->frame->component_count; i++) {
      struct component *component = &strip->frame->components[i];
      struct strip_plane *plane = strip_plane_at(strip, (int64_t)i);
      int64_t remainder = component->height % ((int64_t)component->v * 8);
      int64_t rows_left = remainder == 0 ? (int64_t)component->v * 8 : remainder;
      if (i == 0) available = ceil_div(rows_left, component->v);
      int64_t last = strip_row_pointer(strip, plane, rows_left - 1, strip->which);
      for (int64_t row = 0; row < (int64_t)component->v * 2; row++) {
        plane->pointers[plane->starts[strip->which] + rows_left + row] = last;
      }
    }
  }
  for (int64_t group = 0; group < available; group++) {
    strip_emit_group(strip, group);
  }
  if (imcu == 0) {
    for (size_t i = 0; i < strip->frame->component_count; i++) {
      struct component *component = &strip->frame->components[i];
      struct strip_plane *plane = strip_plane_at(strip, (int64_t)i);
      for (int s = 0; s < 2; s++) {
        int64_t start = plane->starts[s];
        for (int64_t row = 0; row < component->v; row++) {
          plane->pointers[start - component->v + row] =
              strip_pointer_at(plane, start + (int64_t)component->v * 9 + row);
          plane->pointers[start + (int64_t)component->v * 10 + row] =
              strip_pointer_at(plane, start + row);
        }
      }
    }
  }
  strip->which = strip->which == 0 ? 1 : 0;
}

static struct component *find_component(struct input *input, struct frame *frame,
                                        int id) {
  for (size_t i = 0; i < frame->component_count; i++) {
    if (frame->components[i].id == id) return &frame->components[i];
  }
  qa_jpeg_source_error(input->source, (int64_t)input->offset,
                       "Invalid component ID %d in SOS", id);
}

static const uint16_t *require_quantizer(struct input *input,
                                         uint16_t tables[4][64],
                                         const int present[4], int id) {
  if (id < 0 || id > 3 || !present[id]) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "Quantization table 0x%02x was not defined", id);
  }
  return tables[id];
}

struct scan_scratch {
  struct one_pass *strip;
  struct scan_component *scans;
  size_t count;
};

static void scan_scratch_release(void *raw) {
  struct scan_scratch *scratch = raw;
  free_one_pass(scratch->strip);
  for (size_t j = 0; j < scratch->count; j++) free(scratch->scans[j].owned_multipliers);
}

static int read_scan(struct input *input, const struct scan_header *header,
                     struct frame *frame, uint16_t quantizers[4][64],
                     const int quantizers_present[4],
                     struct huff_table *huffman[256], int restart_interval,
                     int buffered, color_space colorspace) {
  size_t count = header->member_count;
  struct scan_component scans[4];
  memset(scans, 0, sizeof scans);
  struct scan_scratch scratch = {NULL, scans, count};
  qa_cleanup scratch_node;
  qa_cleanup_push(&scratch_node, scan_scratch_release, &scratch);
  int64_t block_sum = 0;
  for (size_t i = 0; i < count; i++) {
    scans[i].component = find_component(input, frame, header->members[i].id);
    block_sum += (int64_t)scans[i].component->h * scans[i].component->v;
  }
  if (count > 1 && block_sum > 10) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "Sampling factors too large for interleaved scan");
  }
  for (size_t i = 0; i < count; i++) {
    struct component *component = scans[i].component;
    struct progression *storage = component->progression;
    if (storage != NULL && storage->has_table) {
      scans[i].multipliers = storage->multipliers;
      continue;
    }
    const uint16_t *quantizer = require_quantizer(
        input, quantizers, quantizers_present, component->quantizer);
    if (storage != NULL) {
      memcpy(storage->quantizer, quantizer, sizeof storage->quantizer);
      float_multipliers(quantizer, storage->multipliers);
      storage->has_table = 1;
      scans[i].multipliers = storage->multipliers;
    } else {
      float *multipliers = (float *)malloc(64 * sizeof(float));
      if (multipliers == NULL) {
        qa_cleanup_pop(&scratch_node);
        scan_scratch_release(&scratch);
        return 0;
      }
      float_multipliers(quantizer, multipliers);
      scans[i].owned_multipliers = multipliers;
      scans[i].multipliers = multipliers;
    }
  }
  if (header->start != 0 || header->end != 63 || header->approximation != 0) {
    warnings_emit(input->warnings, "Invalid SOS parameters for sequential JPEG");
  }
  int oom = 0;
  for (size_t i = 0; i < count; i++) {
    int selector = header->members[i].selector;
    const int selectors[2] = {selector >> 4, 16 + (selector & 15)};
    for (int t = 0; t < 2; t++) {
      if (huffman[selectors[t]] == NULL) {
        qa_jpeg_source_error(input->source, (int64_t)input->offset,
                             "Huffman table 0x%02x was not defined",
                             selectors[t] & 15);
      }
    }
    scans[i].dc = derive_huffman(input, huffman, selectors[0], &oom);
    scans[i].ac = oom ? NULL : derive_huffman(input, huffman, selectors[1], &oom);
    if (oom) break;
  }
  if (oom) {
    qa_cleanup_pop(&scratch_node);
    scan_scratch_release(&scratch);
    return 0;
  }
  int single = count == 1;
  int64_t columns = single ? ceil_div(scans[0].component->width, 8) : frame->columns;
  int64_t rows = single ? ceil_div(scans[0].component->height, 8) : frame->rows;
  struct entropy entropy;
  memset(&entropy, 0, sizeof entropy);
  entropy.input = input;
  float blocks[10][64];
  float workspace[64];
  struct one_pass *strip = NULL;
  if (!buffered) {
    strip = make_one_pass(input, frame, colorspace);
    if (strip == NULL) {
      qa_cleanup_pop(&scratch_node);
      scan_scratch_release(&scratch);
      return 0;
    }
  }
  scratch.strip = strip;
  int restart = 0;
  for (int64_t mcu = 0; mcu < columns * rows; mcu++) {
    if (restart_interval != 0 && mcu > 0 && mcu % restart_interval == 0) {
      entropy_restart(&entropy, restart);
      restart = (restart + 1) & 7;
      for (size_t i = 0; i < count; i++) scans[i].prediction_dc = 0;
    }
    int64_t column = mcu % columns, row = mcu / columns;
    int block_index = 0;
    struct { int64_t index, column, row, block; } pending[10];
    int pending_count = 0;
    for (size_t i = 0; i < count; i++) {
      struct scan_component *scan = &scans[i];
      int64_t h = single ? 1 : scan->component->h;
      int64_t v = single ? 1 : scan->component->v;
      for (int64_t y = 0; y < v; y++) {
        for (int64_t x = 0; x < h; x++) {
          if (block_index >= 10) jpeg_error("JPEG MCU exceeds block storage");
          float *block = blocks[block_index];
          int this_block = block_index++;
          int64_t block_column = column * h + x, block_row = row * v + y;
          decode_block(&entropy, scan, block_column, block_row, block);
          if (strip != NULL && block_column * 8 < scan->component->width &&
              block_row * 8 < scan->component->height) {
            pending[pending_count].index = (int64_t)i;
            pending[pending_count].column = block_column;
            pending[pending_count].row =
                single ? row % scan->component->v : y;
            pending[pending_count].block = this_block;
            pending_count++;
          }
        }
      }
    }
    if (strip != NULL) {
      for (int p = 0; p < pending_count; p++) {
        strip_write(strip, pending[p].index, pending[p].column, pending[p].row,
                    blocks[pending[p].block], workspace);
      }
      if (column == columns - 1 &&
          (!single || row % scans[0].component->v == scans[0].component->v - 1 ||
           row == rows - 1)) {
        strip_finish_row(strip, single ? row / scans[0].component->v : row);
      }
    }
  }
  entropy.remaining = 0;
  qa_cleanup_pop(&scratch_node);
  scan_scratch_release(&scratch);
  return 1;
}

static void progressive_ac_first(struct entropy *entropy, const int16_t *table,
                                 int16_t *coefficients, size_t coefficient_count,
                                 int64_t offset, int start, int end, int low,
                                 int64_t *eob_remaining) {
  if (*eob_remaining > 0) {
    (*eob_remaining)--;
    return;
  }
  int index = start;
  while (index <= end) {
    int symbol = entropy_symbol(entropy, table);
    int run = symbol >> 4;
    int size = symbol & 15;
    if (size == 0) {
      if (run < 15) {
        *eob_remaining = (1 << run) + entropy_bits(entropy, run) - 1;
        return;
      }
      index += 16;
    } else {
      index += run;
      int natural = natural_order[index < 63 ? index : 63];
      index++;
      int64_t at = offset + natural;
      if (at < 0 || (uint64_t)at >= (uint64_t)coefficient_count) {
        qa_range_error("JPEG internal index %lld", (long long)at);
      }
      coefficients[at] = (int16_t)(entropy_signed(entropy, size) * (1 << low));
    }
  }
}

static void refine_coefficient(struct entropy *entropy, int16_t *coefficients,
                               size_t coefficient_count, int64_t index, int bit) {
  int value = (int)at_i16(coefficients, coefficient_count, index);
  if (entropy_bits(entropy, 1) != 0 && (value & bit) == 0) {
    coefficients[index] = (int16_t)(value + (value >= 0 ? bit : -bit));
  }
}

static void progressive_ac_refine(struct entropy *entropy, const int16_t *table,
                                  int16_t *coefficients, size_t coefficient_count,
                                  int64_t offset, int start, int end, int low,
                                  int64_t *eob_remaining) {
  int bit = 1 << low;
  int index = start;
  if (*eob_remaining == 0) {
    while (index <= end) {
      int symbol = entropy_symbol(entropy, table);
      int run = symbol >> 4;
      int size = symbol & 15;
      int value = 0;
      if (size != 0) {
        if (size != 1) {
          warnings_emit(entropy->input->warnings,
                        "Corrupt JPEG data: bad Huffman code");
        }
        value = entropy_bits(entropy, 1) == 0 ? -bit : bit;
      } else if (run < 15) {
        *eob_remaining = (1 << run) + entropy_bits(entropy, run);
        break;
      }
      while (index <= end) {
        int64_t position = offset + natural_order[index];
        if (at_i16(coefficients, coefficient_count, position) != 0) {
          refine_coefficient(entropy, coefficients, coefficient_count, position, bit);
        } else if (--run < 0) {
          break;
        }
        index++;
      }
      if (value != 0) {
        int64_t at = offset + natural_order[index < 63 ? index : 63];
        if (at < 0 || (uint64_t)at >= (uint64_t)coefficient_count) {
          qa_range_error("JPEG internal index %lld", (long long)at);
        }
        coefficients[at] = (int16_t)value;
      }
      index++;
    }
  }
  if (*eob_remaining > 0) {
    for (; index <= end; index++) {
      int64_t position = offset + natural_order[index];
      if (at_i16(coefficients, coefficient_count, position) != 0) {
        refine_coefficient(entropy, coefficients, coefficient_count, position, bit);
      }
    }
    (*eob_remaining)--;
  }
}

enum progressive_mode { PM_DC_REFINE, PM_DC_FIRST, PM_AC_FIRST, PM_AC_REFINE };

static int read_progressive_scan(struct input *input,
                                 const struct scan_header *header,
                                 struct frame *frame,
                                 uint16_t quantizers[4][64],
                                 const int quantizers_present[4],
                                 struct huff_table *huffman[256],
                                 int restart_interval) {
  size_t count = header->member_count;
  struct {
    struct component *component;
    struct progression *state;
    int selector;
    const int16_t *table;
    int32_t prediction_dc;
    enum progressive_mode mode;
  } scans[4];
  memset(scans, 0, sizeof scans);
  int64_t block_sum = 0;
  for (size_t i = 0; i < count; i++) {
    scans[i].component = find_component(input, frame, header->members[i].id);
    scans[i].selector = header->members[i].selector;
    scans[i].state = scans[i].component->progression;
    if (scans[i].state == NULL) {
      input_fail(input, "progressive scan without coefficient storage");
    }
    block_sum += (int64_t)scans[i].component->h * scans[i].component->v;
  }
  int high = header->approximation >> 4, low = header->approximation & 15;
  if (count > 1 && block_sum > 10) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "Sampling factors too large for interleaved scan");
  }
  for (size_t i = 0; i < count; i++) {
    struct progression *state = scans[i].state;
    if (state->has_table) continue;
    const uint16_t *quantizer = require_quantizer(
        input, quantizers, quantizers_present, scans[i].component->quantizer);
    memcpy(state->quantizer, quantizer, sizeof state->quantizer);
    float_multipliers(quantizer, state->multipliers);
    state->has_table = 1;
  }
  int start = header->start, end = header->end;
  if ((start == 0 ? end != 0 : start > end || end > 63 || count != 1) ||
      (high != 0 && low != high - 1) || low > 13) {
    qa_jpeg_source_error(input->source, (int64_t)input->offset,
                         "Invalid progressive parameters Ss=%d Se=%d Ah=%d Al=%d",
                         start, end, high, low);
  }
  struct entropy entropy;
  memset(&entropy, 0, sizeof entropy);
  entropy.input = input;
  int64_t eob_remaining = 0;
  for (size_t i = 0; i < count; i++) {
    struct progression *state = scans[i].state;
    size_t component_index = 0;
    for (size_t c = 0; c < frame->component_count; c++) {
      if (&frame->components[c] == scans[i].component) component_index = c;
    }
    if (start > 0 && state->bits[0] < 0) {
      warnings_emit(input->warnings,
                    "Inconsistent progression sequence for component %zu coefficient 0",
                    component_index);
    }
    for (int index = start; index <= end; index++) {
      int previous = state->bits[index];
      if (high != (previous < 0 ? 0 : previous)) {
        warnings_emit(input->warnings,
                      "Inconsistent progression sequence for component %zu coefficient %d",
                      component_index, index);
      }
      state->bits[index] = (int8_t)low;
    }
  }
  int oom = 0;
  for (size_t i = 0; i < count; i++) {
    if (start == 0 && high != 0) {
      scans[i].mode = PM_DC_REFINE;
    } else {
      int selector = start == 0 ? scans[i].selector >> 4
                                : 16 + (scans[i].selector & 15);
      scans[i].table = derive_huffman(input, huffman, selector, &oom);
      if (oom) return 0;
      scans[i].mode = start == 0 ? PM_DC_FIRST : high == 0 ? PM_AC_FIRST : PM_AC_REFINE;
    }
  }
  int single = count == 1;
  int64_t columns = single ? ceil_div(scans[0].component->width, 8) : frame->columns;
  int64_t rows = single ? ceil_div(scans[0].component->height, 8) : frame->rows;
  int restart = 0;
  for (int64_t mcu = 0; mcu < columns * rows; mcu++) {
    if (restart_interval != 0 && mcu > 0 && mcu % restart_interval == 0) {
      entropy_restart(&entropy, restart);
      restart = (restart + 1) & 7;
      eob_remaining = 0;
      for (size_t i = 0; i < count; i++) scans[i].prediction_dc = 0;
    }
    int64_t column = mcu % columns, row = mcu / columns;
    for (size_t i = 0; i < count; i++) {
      int64_t h = single ? 1 : scans[i].component->h;
      int64_t v = single ? 1 : scans[i].component->v;
      for (int64_t y = 0; y < v; y++) {
        for (int64_t x = 0; x < h; x++) {
          struct progression *state = scans[i].state;
          int64_t offset = ((row * v + y) * (scans[i].component->stride / 8) +
                            column * h + x) *
                           64;
          switch (scans[i].mode) {
            case PM_DC_REFINE:
              if (entropy_bits(&entropy, 1) != 0) {
                int value = (int)at_i16(state->coefficients,
                                        state->coefficient_count, offset);
                state->coefficients[offset] = (int16_t)(value | (1 << low));
              }
              break;
            case PM_DC_FIRST: {
              int size = entropy_symbol(&entropy, scans[i].table);
              scans[i].prediction_dc =
                  (int32_t)((uint32_t)scans[i].prediction_dc +
                            (uint32_t)entropy_signed(&entropy, size));
              if (offset < 0 || (uint64_t)offset >= state->coefficient_count) {
                qa_range_error("JPEG internal index %lld", (long long)offset);
              }
              state->coefficients[offset] =
                  (int16_t)((uint32_t)scans[i].prediction_dc << low);
              break;
            }
            case PM_AC_FIRST:
              progressive_ac_first(&entropy, scans[i].table, state->coefficients,
                                   state->coefficient_count, offset, start, end,
                                   low, &eob_remaining);
              break;
            case PM_AC_REFINE:
              progressive_ac_refine(&entropy, scans[i].table, state->coefficients,
                                    state->coefficient_count, offset, start, end,
                                    low, &eob_remaining);
              break;
          }
        }
      }
    }
  }
  entropy.remaining = 0;
  return 1;
}

static void smooth_progressive_block(struct component *component,
                                     struct progression *state,
                                     const uint16_t *quantizer, int64_t column,
                                     int64_t row, int16_t coefficients[64]) {
  int64_t columns = ceil_div(component->width, 8);
  int64_t rows = ceil_div(component->height, 8);
  int64_t dc_values[3][3];
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      int64_t cx = column + dx, cy = row + dy;
      if (cx < 0) cx = 0;
      if (cx > columns - 1) cx = columns - 1;
      if (cy < 0) cy = 0;
      if (cy > rows - 1) cy = rows - 1;
      dc_values[dy + 1][dx + 1] =
          at_i16(state->coefficients, state->coefficient_count,
                 (cy * (component->stride / 8) + cx) * 64);
    }
  }
  int64_t q00 = quantizer[0];
  int64_t center = dc_values[1][1], above = dc_values[0][1], below = dc_values[2][1];
  int64_t left = dc_values[1][0], right = dc_values[1][2];
  int64_t numerators[5] = {
      36 * q00 * (left - right), 36 * q00 * (above - below),
      9 * q00 * (above + below - 2 * center),
      5 * q00 * (dc_values[0][0] - dc_values[0][2] - dc_values[2][0] + dc_values[2][2]),
      9 * q00 * (left + right - 2 * center)};
  for (int index = 1; index <= 5; index++) {
    int low = state->bits[index];
    int natural = natural_order[index];
    if (low == 0 || coefficients[natural] != 0) continue;
    int64_t quant = quantizer[natural];
    int64_t numerator = numerators[index - 1];
    int64_t magnitude = numerator < 0 ? -numerator : numerator;
    int64_t prediction = (quant * 128 + magnitude) / (quant * 256);
    if (low > 0 && prediction > (1LL << low) - 1) prediction = (1LL << low) - 1;
    coefficients[natural] = (int16_t)(numerator < 0 ? -prediction : prediction);
  }
}

static void finish_buffered(struct input *input, struct frame *frame) {
  float block[64], workspace[64];
  int16_t coefficients[64];
  int smoothing_safe = frame->progressive;
  if (smoothing_safe) {
    for (size_t i = 0; i < frame->component_count && smoothing_safe; i++) {
      struct progression *state = frame->components[i].progression;
      if (state == NULL || !state->has_table || state->bits[0] < 0) {
        smoothing_safe = 0;
        break;
      }
      for (int k = 0; k < 6; k++) {
        if (state->quantizer[natural_order[k]] == 0) smoothing_safe = 0;
      }
    }
  }
  for (size_t i = 0; i < frame->component_count; i++) {
    struct component *component = &frame->components[i];
    struct progression *state = component->progression;
    if (state == NULL) input_fail(input, "buffered frame without coefficient storage");
    static const float zeroes[64] = {0};
    const float *multipliers = state->has_table ? state->multipliers : zeroes;
    int smoothing = 0;
    if (smoothing_safe) {
      for (int b = 1; b < 6; b++) {
        if (state->bits[b] != 0) smoothing = 1;
      }
    }
    for (int64_t row = 0; row < ceil_div(component->height, 8); row++) {
      for (int64_t column = 0; column < ceil_div(component->width, 8); column++) {
        int64_t offset = (row * (component->stride / 8) + column) * 64;
        for (int k = 0; k < 64; k++) {
          coefficients[k] = (int16_t)at_i16(state->coefficients,
                                            state->coefficient_count, offset + k);
        }
        if (smoothing && state->has_table) {
          smooth_progressive_block(component, state, state->quantizer, column, row,
                                   coefficients);
        }
        for (int k = 0; k < 64; k++) {
          block[k] = (float)coefficients[k] * multipliers[k];
        }
        write_block(input, component, column, row, block, workspace);
      }
    }
  }
}

static int64_t sample_component(struct component *component, int64_t x, int64_t y,
                                const struct frame *frame) {
  if (component->output_samples != NULL) {
    return at_u8(component->output_samples, component->output_samples_length,
                 y * frame->width + x);
  }
  int64_t h = frame->max_h / component->h, v = frame->max_v / component->v;
  int64_t cx = x / h, cy = y / v;
  int64_t center = at_u8(component->pixels, component->pixels_length,
                         cy * component->stride + cx);
  if (h != 2 || component->width <= 2 || (v != 1 && v != 2)) return center;
  int64_t neighbor_x = cx + (x % 2 == 0 ? -1 : 1);
  if (neighbor_x < 0) neighbor_x = 0;
  if (neighbor_x > component->width - 1) neighbor_x = component->width - 1;
  int64_t horizontal = at_u8(component->pixels, component->pixels_length,
                             cy * component->stride + neighbor_x);
  if (v == 1) return (center * 3 + horizontal + (x % 2 == 0 ? 1 : 2)) >> 2;
  int64_t neighbor_y = cy + (y % 2 == 0 ? -1 : 1);
  if (neighbor_y < 0) neighbor_y = 0;
  if (neighbor_y > component->height - 1) neighbor_y = component->height - 1;
  int64_t vertical = at_u8(component->pixels, component->pixels_length,
                           neighbor_y * component->stride + cx);
  int64_t diagonal = at_u8(component->pixels, component->pixels_length,
                           neighbor_y * component->stride + neighbor_x);
  return (center * 9 + horizontal * 3 + vertical * 3 + diagonal +
          (x % 2 == 0 ? 8 : 7)) >>
         4;
}

static color_space select_color_space(const struct frame *frame,
                                      int has_adobe, int adobe_transform, int jfif,
                                      struct warnings *warnings) {
  size_t count = frame->component_count;
  if (count == 1) return CS_GRAYSCALE;
  if (count != 3 && count != 4) return CS_UNKNOWN;
  if (count == 4) {
    if (has_adobe && adobe_transform != 0 && adobe_transform != 2) {
      warnings_emit(warnings, "Unknown Adobe color transform code %d",
                    adobe_transform);
    }
    return !has_adobe || adobe_transform == 0 ? CS_CMYK : CS_YCCK;
  }
  if (jfif) return CS_YCBCR;
  if (has_adobe) {
    if (adobe_transform != 0 && adobe_transform != 1) {
      warnings_emit(warnings, "Unknown Adobe color transform code %d",
                    adobe_transform);
    }
    return adobe_transform == 0 ? CS_RGB : CS_YCBCR;
  }
  return frame->components[0].id == 82 && frame->components[1].id == 71 &&
                 frame->components[2].id == 66
             ? CS_RGB
             : CS_YCBCR;
}

static int render_image(struct input *input, struct frame *frame,
                        color_space colorspace, qa_jpeg_image *out) {
  if (frame->component_count == 2) {
    input_fail(input, "two-component LoadJPG alpha writes exceed the source output allocation");
  }
  if (frame->component_count == 0) jpeg_error("JPEG missing image component");
  size_t length = (size_t)(frame->width * frame->height * 4);
  uint8_t *pixels = length == 0 ? NULL : (uint8_t *)malloc(length);
  if (length != 0 && pixels == NULL) return 0;
  if (colorspace == CS_UNKNOWN) {
    int64_t count = (int64_t)frame->component_count;
    for (size_t index = 0; index < length; index++) {
      int64_t pixel = (int64_t)index / count;
      struct component *component = &frame->components[index % (size_t)count];
      pixels[index] =
          index % 4 == 3
              ? 255
              : (uint8_t)sample_component(component, pixel % frame->width,
                                          pixel / frame->width, frame);
    }
    out->width = (double)frame->width;
    out->height = (double)frame->height;
    out->pixels.bytes = pixels;
    out->pixels.length = length;
    return 1;
  }
  struct component *first = &frame->components[0];
  struct component *second =
      frame->component_count > 1 ? &frame->components[1] : NULL;
  struct component *third =
      frame->component_count > 2 ? &frame->components[2] : NULL;
  for (int64_t y = 0; y < frame->height; y++) {
    for (int64_t x = 0; x < frame->width; x++) {
      int64_t offset = (y * frame->width + x) * 4;
      int64_t a = sample_component(first, x, y, frame);
      if (second == NULL || third == NULL) {
        pixels[offset] = (uint8_t)a;
        pixels[offset + 1] = (uint8_t)a;
        pixels[offset + 2] = (uint8_t)a;
      } else {
        int64_t b = sample_component(second, x, y, frame);
        int64_t c = sample_component(third, x, y, frame);
        if (colorspace == CS_RGB || colorspace == CS_CMYK) {
          pixels[offset] = (uint8_t)a;
          pixels[offset + 1] = (uint8_t)b;
          pixels[offset + 2] = (uint8_t)c;
        } else {
          int64_t red = a + descale_int(91881 * (c - 128), 16);
          int64_t green = a + descale_int(-22554 * (b - 128) - 46802 * (c - 128), 16);
          int64_t blue = a + descale_int(116130 * (b - 128), 16);
          int ycck = colorspace == CS_YCCK;
          pixels[offset] = (uint8_t)clamp255(ycck ? 255 - red : red);
          pixels[offset + 1] = (uint8_t)clamp255(ycck ? 255 - green : green);
          pixels[offset + 2] = (uint8_t)clamp255(ycck ? 255 - blue : blue);
        }
      }
      pixels[offset + 3] = 255;
    }
  }
  out->width = (double)frame->width;
  out->height = (double)frame->height;
  out->pixels.bytes = pixels;
  out->pixels.length = length;
  return 1;
}

static void free_huffman_tables(struct huff_table *tables[256]) {
  for (int i = 0; i < 256; i++) {
    if (tables[i] != NULL) {
      free(tables[i]->values);
      free(tables[i]->derived);
      free(tables[i]);
      tables[i] = NULL;
    }
  }
}

struct decode_state {
  struct frame *frame;
  struct frame_header header;
  struct huff_table *huffman[256];
};

static void decode_state_release(void *raw) {
  struct decode_state *state = raw;
  free_frame(state->frame);
  free(state->header.components);
  free_huffman_tables(state->huffman);
  memset(state, 0, sizeof *state);
}

int qa_decode_jpeg(const uint8_t *bytes, size_t length, const char *source,
                   qa_jpeg_print print, void *print_ctx, qa_jpeg_image *out) {
  const char *name = source != NULL ? source : "<jpeg>";
  struct warnings warnings;
  memset(&warnings, 0, sizeof warnings);
  warnings.print = print;
  warnings.ctx = print_ctx;
  struct input input;
  memset(&input, 0, sizeof input);
  input.bytes = bytes;
  input.length = length;
  input.source = name;
  input.warnings = &warnings;
  int first = input_byte(&input);
  int second = input_byte(&input);
  if (first != 255 || second != 216) {
    qa_jpeg_source_error(name, (int64_t)input.offset,
                         "Not a JPEG file: starts with 0x%02x 0x%02x", first, second);
  }
  uint16_t quantizers[4][64];
  int quantizers_present[4] = {0, 0, 0, 0};
  /* A caught BinaryError releases the decoder state (TS GC). */
  struct decode_state owned;
  memset(&owned, 0, sizeof owned);
  qa_cleanup owned_node;
  qa_cleanup_push(&owned_node, decode_state_release, &owned);
  struct huff_table **huffman = owned.huffman;
  enum { STATE_NONE, STATE_HEADER, STATE_READY } state = STATE_NONE;
#define header owned.header
#define frame owned.frame
  color_space colorspace = CS_UNKNOWN;
  int multiple_scans = 0;
  int restart_interval = 0;
  int has_adobe = 0, adobe_transform = 0, jfif = 0;
  int oom = 0;
  for (;;) {
    int marker = input_marker(&input);
    if (marker == 217) {
      if (state == STATE_NONE) {
        qa_jpeg_source_error(name, (int64_t)input.offset,
                             "JPEG datastream contains no image");
      }
      if (state == STATE_HEADER) {
        qa_jpeg_source_error(name, (int64_t)input.offset,
                             "Invalid JPEG file structure: missing SOS marker");
      }
      if (multiple_scans) finish_buffered(&input, frame);
      int result = render_image(&input, frame, colorspace, out);
      qa_cleanup_pop(&owned_node);
      decode_state_release(&owned);
      return result;
    }
    if (marker == 216) {
      qa_jpeg_source_error(name, (int64_t)input.offset,
                           "Invalid JPEG file structure: two SOI markers");
    }
    if (marker == 1 || (marker >= 208 && marker <= 215)) continue;
    if (marker == 195 || marker == 197 || marker == 198 || marker == 199 ||
        marker == 200 || marker == 203 || marker == 205 || marker == 206 ||
        marker == 207) {
      qa_jpeg_source_error(name, (int64_t)input.offset,
                           "Unsupported JPEG process: SOF type 0x%02x", marker);
    }
    if (marker == 218 && state == STATE_NONE) {
      qa_jpeg_source_error(name, (int64_t)input.offset,
                           "Invalid JPEG file structure: SOS before SOF");
    }
    int known = marker == 192 || marker == 193 || marker == 194 || marker == 196 ||
                marker == 201 || marker == 202 || marker == 204 || marker == 218 ||
                marker == 219 || marker == 220 || marker == 221 || marker == 254 ||
                (marker >= 224 && marker <= 239);
    if (!known) {
      qa_jpeg_source_error(name, (int64_t)input.offset,
                           "Unsupported marker type 0x%02x", marker);
    }
    if (marker == 219) { read_quantizers(&input, quantizers, quantizers_present); continue; }
    if (marker == 196) {
      if (!read_huffman(&input, huffman)) { oom = 1; break; }
      continue;
    }
    if (marker == 204) { read_arithmetic_tables(&input); continue; }
    if (marker == 192 || marker == 193 || marker == 194 || marker == 201 ||
        marker == 202) {
      struct frame_header next;
      memset(&next, 0, sizeof next);
      if (!read_frame(&input, marker == 194 || marker == 202,
                      marker == 201 || marker == 202, state != STATE_NONE, &next)) {
        oom = 1;
        break;
      }
      free(header.components);
      header = next;
      state = STATE_HEADER;
    } else if (marker == 221) {
      if (input_word(&input) != 4) {
        qa_jpeg_source_error(name, (int64_t)input.offset, "Bogus marker length");
      }
      restart_interval = input_word(&input);
    } else if (marker == 218) {
      if (state == STATE_NONE) {
        qa_jpeg_source_error(name, (int64_t)input.offset,
                             "Invalid JPEG file structure: SOS before SOF");
      }
      if (++input.scans > 256) {
        qa_jpeg_source_error(name, (int64_t)input.offset, "Too many scans");
      }
      struct scan_header scan_header;
      memset(&scan_header, 0, sizeof scan_header);
      if (state == STATE_HEADER) {
        read_scan_header(&input, header.components, NULL, header.component_count,
                         &scan_header);
      } else {
        read_scan_header(&input, NULL, frame->components, frame->component_count,
                         &scan_header);
      }
      if (state == STATE_READY && !multiple_scans) {
        qa_jpeg_source_error(name, (int64_t)input.offset,
                             "Didn't expect more than one scan");
      }
      if (state == STATE_HEADER) {
        multiple_scans = header.progressive ||
                         scan_header.member_count < header.component_count;
        frame = prepare_frame(&input, &header, multiple_scans);
        if (frame == NULL) { oom = 1; break; }
        colorspace = select_color_space(frame, has_adobe, adobe_transform, jfif,
                                        &warnings);
        for (size_t i = 0; i < frame->component_count; i++) {
          if (frame->max_h % frame->components[i].h != 0 ||
              frame->max_v % frame->components[i].v != 0) {
            qa_jpeg_source_error(name, (int64_t)input.offset,
                                 "Fractional sampling not implemented yet");
          }
        }
        if (header.arithmetic) {
          qa_jpeg_source_error(name, (int64_t)input.offset,
                               "Sorry, there are legal restrictions on arithmetic coding");
        }
        state = STATE_READY;
      }
      int ok;
      if (frame->progressive) {
        ok = read_progressive_scan(&input, &scan_header, frame, quantizers,
                                   quantizers_present, huffman, restart_interval);
      } else {
        ok = read_scan(&input, &scan_header, frame, quantizers,
                       quantizers_present, huffman, restart_interval,
                       multiple_scans, colorspace);
      }
      if (!ok) { oom = 1; break; }
    } else if (marker == 224) {
      if (read_jfif(&input)) jfif = 1;
    } else if (marker == 238) {
      int has_transform = 0;
      int transform = read_adobe(&input, &has_transform);
      if (has_transform) {
        has_adobe = 1;
        adobe_transform = transform;
      }
    } else {
      input_skip(&input, (int64_t)input_word(&input) - 2);
    }
  }
  (void)oom;
  qa_cleanup_pop(&owned_node);
  decode_state_release(&owned);
  return 0;
#undef header
#undef frame
}
