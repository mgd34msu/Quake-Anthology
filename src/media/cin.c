/* Port of src/media/cin.ts. GPL-2.0-or-later. */
#include "qa/media/cin.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

/* TS plain `throw new Error(msg)`. */
static noreturn void media_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

void qa_cin_audio_dispose(qa_cin_audio *audio) {
  free(audio->samples_s16);
  free(audio->samples_u8);
  memset(audio, 0, sizeof *audio);
}

void qa_cin_frame_dispose(qa_cin_frame *frame) {
  free(frame->pixels);
  qa_cin_audio_dispose(&frame->audio);
  memset(frame, 0, sizeof *frame);
}

void qa_cin_sample_range(int64_t frame, int64_t sample_rate, int64_t *start,
                         int64_t *end) {
  if (frame < 0 || frame >= QA_MAX_SAFE_INTEGER || sample_rate < 0 ||
      sample_rate > QA_MAX_SAFE_INTEGER ||
      (sample_rate != 0 && frame + 1 > QA_MAX_SAFE_INTEGER / sample_rate)) {
    qa_range_error("Invalid CIN sample position");
  }
  *start = frame * sample_rate / 14;
  *end = (frame + 1) * sample_rate / 14;
}

bool qa_cin_huffman_init(qa_cin_huffman *huffman, const uint8_t *counts,
                         size_t counts_length, const char *source) {
  memset(huffman, 0, sizeof *huffman);
  const char *name = source == NULL ? "<CIN Huffman>" : source;
  if (counts_length != 65536) {
    qa_binary_error(name, 0, "CIN requires 65536 Huffman counts");
  }
  huffman->nodes = (int32_t *)calloc(256 * 256 * 2, sizeof(int32_t));
  huffman->source = dup_text(name);
  if (huffman->nodes == NULL || huffman->source == NULL) {
    qa_cin_huffman_destroy(huffman);
    return false;
  }
  int32_t weights[512];
  uint8_t used[512];
  for (int context = 0; context < 256; context++) {
    memset(weights, 0, sizeof weights);
    memset(used, 0, sizeof used);
    for (int i = 0; i < 256; i++) {
      weights[i] = counts[context * 256 + i];
    }
    int count = 256;
    while (count != 511) {
      int selected[2] = {-1, -1};
      for (int pick = 0; pick < 2; pick++) {
        int best = 99999999;
        int choice = -1;
        for (int index = 0; index < count; index++) {
          int32_t weight = weights[index];
          if (used[index] == 0 && weight != 0 && weight < best) {
            best = weight;
            choice = index;
          }
        }
        if (choice != -1) used[choice] = 1;
        selected[pick] = choice;
      }
      if (selected[0] == -1 || selected[1] == -1) break;
      int offset = context * 512 + (count - 256) * 2;
      huffman->nodes[offset] = selected[0];
      huffman->nodes[offset + 1] = selected[1];
      weights[count] = weights[selected[0]] + weights[selected[1]];
      count++;
    }
    huffman->roots[context] = count - 1;
  }
  return true;
}

void qa_cin_huffman_destroy(qa_cin_huffman *huffman) {
  free(huffman->nodes);
  free(huffman->source);
  memset(huffman, 0, sizeof *huffman);
}

uint8_t *qa_cin_huffman_decode(qa_cin_huffman *huffman,
                               const uint8_t *compressed, size_t length,
                               int64_t expected_pixels) {
  uint8_t *stored = (uint8_t *)malloc(length == 0 ? 1 : length);
  if (stored == NULL) return NULL;
  memcpy(stored, compressed, length);
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, stored, length, huffman->source)) {
    free(stored);
    return NULL;
  }
  int32_t count = qa_binary_reader_i32(&reader);
  if (count != expected_pixels) {
    qa_binary_error(huffman->source, 0,
                    "CIN frame has %d pixels, expected %lld", count,
                    (long long)expected_pixels);
  }
  uint8_t *output = (uint8_t *)malloc(count == 0 ? 1 : (size_t)count);
  if (output == NULL) {
    qa_binary_reader_destroy(&reader);
    free(stored);
    return NULL;
  }
  int context = 0;
  int value = 0;
  int bits = 0;
  for (int32_t index = 0; index < count; index++) {
    int32_t node = huffman->roots[context];
    while (node >= 256) {
      if (bits == 0) {
        value = qa_binary_reader_u8(&reader);
        bits = 8;
      }
      int32_t child =
          huffman->nodes[context * 512 + (node - 256) * 2 + (value & 1)];
      if (child < 0 || child >= node) {
        qa_binary_error(huffman->source, (int64_t)reader.cursor,
                        "invalid CIN Huffman branch");
      }
      node = child;
      value = (int)((uint32_t)value >> 1);
      bits--;
    }
    output[index] = (uint8_t)node;
    context = node;
  }
  qa_binary_reader_destroy(&reader);
  free(stored);
  return output;
}

static bool set_value(qa_save_value *object, const char *key,
                      qa_save_value *child) {
  return child != NULL && qa_save_value_set(object, key, child);
}
static bool set_number(qa_save_value *object, const char *key, double value) {
  return set_value(object, key, qa_save_value_number(value));
}
static bool set_bool(qa_save_value *object, const char *key, bool value) {
  return set_value(object, key, qa_save_value_boolean(value));
}
static void read_field(const qa_save_reader *reader, const char *name,
                       qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) {
    media_error("out of memory");
  }
}
static void reader_init(qa_save_reader *reader, const qa_save_value *value,
                        const char *path) {
  if (!qa_save_reader_init(reader, value, path)) {
    media_error("out of memory");
  }
}

bool qa_cin_decoder_init(qa_cin_decoder *decoder, qa_media_input *input,
                         const char *source) {
  (void)source; /* TS passes it to mediaBytes only for byte inputs. */
  memset(decoder, 0, sizeof *decoder);
  decoder->input = input;
  decoder->input_length = input->byte_length;
  decoder->frame_rate = 14;
  decoder->offset = 20 + 65536;
  uint8_t *header_bytes = qa_read_media(input, 0, 20);
  if (header_bytes == NULL) {
    qa_cin_decoder_close(decoder);
    return false;
  }
  qa_binary_reader header;
  if (!qa_binary_reader_init(&header, header_bytes, 20, input->source)) {
    free(header_bytes);
    qa_cin_decoder_close(decoder);
    return false;
  }
  decoder->width = qa_binary_reader_i32(&header);
  decoder->height = qa_binary_reader_i32(&header);
  int64_t sample_rate = qa_binary_reader_i32(&header);
  int64_t sample_bytes = qa_binary_reader_i32(&header);
  int64_t channels = qa_binary_reader_i32(&header);
  qa_binary_reader_destroy(&header);
  free(header_bytes);
  if (decoder->width <= 0 || decoder->height <= 0 ||
      decoder->width * decoder->height > 0x1000000) {
    qa_binary_error(input->source, 0, "invalid CIN dimensions");
  }
  if (sample_rate == 0 && sample_bytes == 0 && channels == 0) {
    decoder->has_audio_format = false;
  } else if (sample_rate > 0 && (sample_bytes == 1 || sample_bytes == 2) &&
             (channels == 1 || channels == 2)) {
    decoder->has_audio_format = true;
    decoder->audio_format.sample_rate = sample_rate;
    decoder->audio_format.sample_bytes = (int)sample_bytes;
    decoder->audio_format.channels = (int)channels;
  } else {
    qa_binary_error(input->source, 8, "invalid CIN audio format");
  }
  uint8_t *counts = qa_read_media(input, 20, 65536);
  if (counts == NULL) {
    qa_cin_decoder_close(decoder);
    return false;
  }
  bool ok = qa_cin_huffman_init(&decoder->huffman, counts, 65536,
                                input->source);
  free(counts);
  if (!ok) {
    qa_cin_decoder_close(decoder);
    return false;
  }
  return true;
}

bool qa_cin_decoder_init_bytes(qa_cin_decoder *decoder, const uint8_t *data,
                               size_t length, const char *source) {
  qa_media_input *input = qa_media_bytes(
      data, length, source == NULL ? "<CIN>" : source);
  if (input == NULL) {
    memset(decoder, 0, sizeof *decoder);
    return false;
  }
  return qa_cin_decoder_init(decoder, input, source);
}

void qa_cin_decoder_destroy(qa_cin_decoder *decoder) {
  qa_cin_decoder_close(decoder);
  qa_cin_huffman_destroy(&decoder->huffman);
  memset(decoder, 0, sizeof *decoder);
}

qa_save_value *qa_cin_decoder_capture_checkpoint(qa_cin_decoder *decoder) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  bool ok = set_number(object, "width", (double)decoder->width) &&
            set_number(object, "height", (double)decoder->height) &&
            set_number(object, "inputLength",
                       (double)decoder->input_length) &&
            set_number(object, "offset", (double)decoder->offset) &&
            set_number(object, "frameIndex", (double)decoder->frame_index) &&
            set_bool(object, "ended", decoder->ended) &&
            set_bool(object, "closed", decoder->closed) &&
            set_value(object, "palette",
                      qa_save_value_bytes(decoder->colors, 768));
  if (!ok) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

void qa_cin_decoder_restore_checkpoint(qa_cin_decoder *decoder,
                                       const qa_save_value *value) {
  qa_save_reader r;
  reader_init(&r, value, "cin-decoder");
  qa_save_reader f;
  read_field(&r, "width", &f);
  qa_save_reader_literal_number(&f, (double)decoder->width);
  qa_save_reader_destroy(&f);
  read_field(&r, "height", &f);
  qa_save_reader_literal_number(&f, (double)decoder->height);
  qa_save_reader_destroy(&f);
  read_field(&r, "inputLength", &f);
  qa_save_reader_literal_number(&f, (double)decoder->input_length);
  qa_save_reader_destroy(&f);
  read_field(&r, "offset", &f);
  int64_t offset = (int64_t)qa_save_reader_integer_min(&f, 20 + 65536);
  qa_save_reader_destroy(&f);
  read_field(&r, "palette", &f);
  size_t colors_length = 0;
  const uint8_t *colors = qa_save_reader_bytes(&f, &colors_length);
  uint8_t palette[768];
  if (colors_length == 768) memcpy(palette, colors, 768);
  qa_save_reader_destroy(&f);
  if (offset > decoder->input_length || colors_length != 768) {
    qa_save_reader_fail(&r, "invalid CIN cursor or palette");
  }
  read_field(&r, "frameIndex", &f);
  decoder->frame_index = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(&r, "ended", &f);
  decoder->ended = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  decoder->offset = offset;
  memcpy(decoder->colors, palette, 768);
  read_field(&r, "closed", &f);
  bool closed = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&r);
  if (closed) qa_cin_decoder_close(decoder);
}

void qa_cin_decoder_palette(const qa_cin_decoder *decoder, uint8_t out[768]) {
  memcpy(out, decoder->colors, 768);
}

int64_t qa_cin_decoder_next_frame_index(const qa_cin_decoder *decoder) {
  return decoder->frame_index;
}

static uint8_t *decoder_read(qa_cin_decoder *decoder, int64_t length) {
  uint8_t *bytes = qa_read_media(decoder->input, decoder->offset, length);
  decoder->offset += length; /* TS advances before any Huffman error */
  return bytes;
}

static int32_t read_i32(qa_cin_decoder *decoder, uint8_t *bytes) {
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, bytes, 4, decoder->input->source)) {
    media_error("out of memory");
  }
  int32_t value = qa_binary_reader_i32(&reader);
  qa_binary_reader_destroy(&reader);
  return value;
}

bool qa_cin_decoder_next(qa_cin_decoder *decoder, qa_cin_frame *out) {
  memset(out, 0, sizeof *out);
  if (decoder->closed) media_error("CIN decoder is closed");
  if (decoder->ended) {
    out->is_end = true;
    return true;
  }
  uint8_t *command_bytes = decoder_read(decoder, 4);
  if (command_bytes == NULL) return false;
  int32_t command = read_i32(decoder, command_bytes);
  free(command_bytes);
  if (command == 2) {
    decoder->ended = true;
    out->is_end = true;
    return true;
  }
  if (command == 1) {
    uint8_t *palette = decoder_read(decoder, 768);
    if (palette == NULL) return false;
    memcpy(decoder->colors, palette, 768);
    free(palette);
  }
  uint8_t *size_bytes = decoder_read(decoder, 4);
  if (size_bytes == NULL) return false;
  int32_t size = read_i32(decoder, size_bytes);
  free(size_bytes);
  if (size < 4 || size > 0x20000) {
    qa_binary_error(decoder->input->source, decoder->offset - 4,
                    "bad CIN compressed frame size");
  }
  uint8_t *compressed = decoder_read(decoder, size);
  if (compressed == NULL) return false;
  qa_cin_frame frame;
  memset(&frame, 0, sizeof frame);
  if (decoder->has_audio_format) {
    int64_t sample_rate = decoder->audio_format.sample_rate;
    int sample_bytes = decoder->audio_format.sample_bytes;
    int channels = decoder->audio_format.channels;
    int64_t start = 0, end = 0;
    qa_cin_sample_range(decoder->frame_index, sample_rate, &start, &end);
    int64_t byte_count = (end - start) * sample_bytes * channels;
    uint8_t *bytes = decoder_read(decoder, byte_count);
    if (bytes == NULL && byte_count != 0) {
      free(compressed);
      return false;
    }
    frame.has_audio = true;
    frame.audio.channels = channels;
    frame.audio.sample_rate = sample_rate;
    frame.audio.source_sample = start;
    if (sample_bytes == 2) {
      size_t count = (size_t)(byte_count / 2);
      int16_t *signed_samples =
          (int16_t *)malloc((count == 0 ? 1 : count) * sizeof(int16_t));
      if (signed_samples == NULL) {
        free(bytes);
        free(compressed);
        return false;
      }
      qa_binary_reader reader;
      if (!qa_binary_reader_init(&reader, bytes, (size_t)byte_count,
                                 decoder->input->source)) {
        free(signed_samples);
        free(bytes);
        free(compressed);
        return false;
      }
      for (size_t i = 0; i < count; i++) {
        signed_samples[i] = qa_binary_reader_i16(&reader);
      }
      qa_binary_reader_destroy(&reader);
      free(bytes);
      frame.audio.is_s16 = true;
      frame.audio.samples_s16 = signed_samples;
      frame.audio.sample_count = count;
    } else {
      frame.audio.is_s16 = false;
      frame.audio.samples_u8 = bytes;
      frame.audio.sample_count = (size_t)byte_count;
    }
  }
  int64_t pixel_count = decoder->width * decoder->height;
  frame.pixels = qa_cin_huffman_decode(&decoder->huffman, compressed,
                                       (size_t)size, pixel_count);
  free(compressed);
  if (frame.pixels == NULL) {
    qa_cin_frame_dispose(&frame);
    return false;
  }
  frame.pixels_length = (size_t)pixel_count;
  frame.index = decoder->frame_index++;
  frame.time = (double)frame.index * 1000.0 / 14.0;
  memcpy(frame.palette, decoder->colors, 768);
  *out = frame;
  return true;
}

void qa_cin_decoder_rewind(qa_cin_decoder *decoder) {
  if (decoder->closed) media_error("CIN decoder is closed");
  decoder->offset = 20 + 65536;
  decoder->frame_index = 0;
  decoder->ended = false;
  memset(decoder->colors, 0, sizeof decoder->colors);
}

void qa_cin_decoder_close(qa_cin_decoder *decoder) {
  if (!decoder->closed) {
    decoder->closed = true;
    qa_media_input_close(decoder->input);
    decoder->input = NULL;
  }
}

uint8_t *qa_cin_rgba(const uint8_t *pixels, size_t pixels_length,
                     const uint8_t *palette, size_t palette_length) {
  if (palette_length != 768) {
    qa_range_error("CIN palette requires 256 RGB colors");
  }
  uint8_t *rgba =
      (uint8_t *)malloc(pixels_length == 0 ? 1 : pixels_length * 4);
  if (rgba == NULL) return NULL;
  for (size_t index = 0; index < pixels_length; index++) {
    uint8_t pixel = pixels[index];
    rgba[index * 4] = palette[pixel * 3];
    rgba[index * 4 + 1] = palette[pixel * 3 + 1];
    rgba[index * 4 + 2] = palette[pixel * 3 + 2];
    rgba[index * 4 + 3] = 255;
  }
  return rgba;
}
