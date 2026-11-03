/* Port of src/formats/images/gif.ts. GPL-2.0-or-later. */
#include "qa/formats/images/gif.h"
#include "qa/formats/images/limits.h"
#include "qa/foundation/support.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/formats/images/mip.h"

static int sub_blocks(qa_binary_reader *reader, uint8_t **out, size_t *out_length) {
  uint8_t *buffer = NULL;
  size_t total = 0;
  for (int count = qa_binary_reader_u8(reader); count != 0;
       count = qa_binary_reader_u8(reader)) {
    uint8_t *block = qa_binary_reader_bytes(reader, count);
    if (block == NULL) { free(buffer); return 0; }
    uint8_t *grown = (uint8_t *)realloc(buffer, total + (size_t)count);
    if (grown == NULL) { free(block); free(buffer); return 0; }
    buffer = grown;
    memcpy(buffer + total, block, (size_t)count);
    free(block);
    total += (size_t)count;
  }
  *out = buffer;
  *out_length = total;
  return 1;
}

/* The TS dictionary holds byte arrays; prefix chains decode identically,
 * including its length and invalid-code checks at the same bit offset. */
static int lzw(const uint8_t *data, size_t data_length, int64_t minimum,
               int64_t count, const char *source, uint8_t **out) {
  if (minimum < 2 || minimum > 8) {
    qa_binary_error(source, 0, "Invalid GIF LZW code size");
  }
  int clear = 1 << (int)minimum, end = clear + 1;
  uint8_t *output = count == 0 ? NULL : (uint8_t *)malloc((size_t)count);
  if (count != 0 && output == NULL) return 0;
  int16_t prefix[4096];
  uint8_t suffix[4096], first[4096];
  int lengths[4096];
  uint8_t stack[4097];
  int size = (int)minimum + 1, next = end + 1, previous = -1;
  int64_t bit = 0, written = 0;
  for (int i = 0; i < clear; i++) {
    prefix[i] = -1;
    suffix[i] = (uint8_t)i;
    first[i] = (uint8_t)i;
    lengths[i] = 1;
  }
  while (written < count) {
    if (bit + size > (int64_t)data_length * 8) {
      free(output);
      qa_binary_error(source, bit >> 3, "Truncated GIF LZW data");
    }
    int code = 0;
    for (int i = 0; i < size; i++, bit++) {
      code |= ((data[bit >> 3] >> (bit & 7)) & 1) << i;
    }
    if (code == clear) {
      for (int i = 0; i < clear; i++) {
        prefix[i] = -1;
        suffix[i] = (uint8_t)i;
        first[i] = (uint8_t)i;
        lengths[i] = 1;
      }
      size = (int)minimum + 1;
      next = end + 1;
      previous = -1;
      continue;
    }
    if (code == end) break;
    int entry_code, entry_length, entry_first, kwkwk = 0;
    if (code < next) {
      entry_code = code;
      entry_length = lengths[code];
      entry_first = first[code];
    } else if (code == next && previous >= 0) {
      kwkwk = 1;
      entry_code = previous;
      entry_length = lengths[previous] + 1;
      entry_first = first[previous];
    } else {
      free(output);
      qa_binary_error(source, bit >> 3, "Invalid GIF LZW code");
    }
    if (entry_length > count - written) {
      free(output);
      qa_binary_error(source, bit >> 3, "Invalid GIF LZW code");
    }
    int sp = 0;
    if (kwkwk) stack[sp++] = (uint8_t)entry_first;
    for (int c = entry_code; c >= 0; c = prefix[c]) stack[sp++] = suffix[c];
    for (int i = sp - 1; i >= 0; i--) output[written++] = stack[i];
    int added = -1;
    if (previous >= 0 && next < 4096) {
      added = next;
      prefix[next] = (int16_t)previous;
      suffix[next] = (uint8_t)entry_first;
      first[next] = first[previous];
      lengths[next] = lengths[previous] + 1;
      next++;
      if (next == (1 << size) && size < 12) size++;
    }
    previous = kwkwk ? added : code;
  }
  if (written != count) {
    free(output);
    qa_binary_error(source, bit >> 3, "Incomplete GIF image");
  }
  *out = output;
  return 1;
}

void qa_gif_image_destroy(qa_gif_image *image) {
  for (size_t i = 0; i < image->frame_count; i++) {
    qa_image_level_destroy(&image->frames[i].image);
    free(image->frames[i].indices);
    free(image->frames[i].palette);
  }
  free(image->frames);
  free(image->global_palette);
  image->frames = NULL;
  image->global_palette = NULL;
  image->frame_count = 0;
  image->global_palette_length = 0;
}

static int gif_frame(qa_binary_reader *reader, const char *name, int64_t width,
                     int64_t height, const uint8_t *global_palette,
                     size_t global_palette_length, uint8_t *canvas,
                     size_t canvas_length, int64_t transparent_index,
                     int64_t delay, int64_t disposal, qa_gif_frame **frames,
                     size_t *frame_count, size_t *frame_capacity, double *retained_bytes) {
  int64_t x = qa_binary_reader_u16(reader), y = qa_binary_reader_u16(reader);
  int64_t frame_width = qa_binary_reader_u16(reader);
  int64_t frame_height = qa_binary_reader_u16(reader);
  int64_t flags = qa_binary_reader_u8(reader);
  if (frame_width == 0 || frame_height == 0 || x + frame_width > width ||
      y + frame_height > height) {
    qa_binary_error(name, (int64_t)qa_binary_reader_offset(reader),
                    "GIF frame exceeds its logical screen");
  }
  const uint8_t *palette = global_palette;
  size_t palette_length = global_palette_length;
  uint8_t *local_palette = NULL;
  if ((flags & 128) != 0) {
    palette_length = (size_t)(2 << (flags & 7)) * 3;
    local_palette = qa_binary_reader_bytes(reader, (int64_t)palette_length);
    if (local_palette == NULL) return 0;
    palette = local_palette;
  }
  if (palette == NULL) {
    qa_binary_error(name, (int64_t)qa_binary_reader_offset(reader),
                    "GIF frame has no palette");
  }
  int64_t minimum = qa_binary_reader_u8(reader);
  uint8_t *compressed = NULL;
  size_t compressed_length = 0;
  if (!sub_blocks(reader, &compressed, &compressed_length)) {
    free(local_palette);
    return 0;
  }
  uint8_t *decoded = NULL;
  int ok = lzw(compressed, compressed_length, minimum, frame_width * frame_height,
               name, &decoded);
  free(compressed);
  if (!ok) { free(local_palette); return 0; }
  int64_t *rows = (int64_t *)malloc((size_t)frame_height * sizeof(int64_t));
  uint8_t *indices =
      (uint8_t *)malloc((size_t)(frame_width * frame_height));
  uint8_t *previous = NULL;
  if (disposal == 3) {
    previous = (uint8_t *)malloc(canvas_length);
    if (previous != NULL) memcpy(previous, canvas, canvas_length);
  }
  if (rows == NULL || indices == NULL || (disposal == 3 && previous == NULL)) {
    free(rows);
    free(indices);
    free(previous);
    free(decoded);
    free(local_palette);
    return 0;
  }
  int64_t row_count = 0;
  if ((flags & 64) == 0) {
    for (int64_t row = 0; row < frame_height; row++) rows[row_count++] = row;
  } else {
    static const int passes[4][2] = {{0, 8}, {4, 8}, {2, 4}, {1, 2}};
    for (int p = 0; p < 4; p++) {
      for (int64_t row = passes[p][0]; row < frame_height; row += passes[p][1]) {
        rows[row_count++] = row;
      }
    }
  }
  for (int64_t row = 0; row < frame_height; row++) {
    int64_t destination_y = rows[row];
    for (int64_t column = 0; column < frame_width; column++) {
      uint8_t index = decoded[row * frame_width + column];
      if ((size_t)index * 3 + 3 > palette_length) {
        qa_binary_error(name, (int64_t)qa_binary_reader_offset(reader),
                        "GIF palette index out of range");
      }
      indices[destination_y * frame_width + column] = index;
      if ((int64_t)index != transparent_index) {
        uint8_t *at = canvas + ((y + destination_y) * width + x + column) * 4;
        at[0] = palette[index * 3];
        at[1] = palette[index * 3 + 1];
        at[2] = palette[index * 3 + 2];
        at[3] = 255;
      }
    }
  }
  free(rows);
  free(decoded);
  *retained_bytes += (double)canvas_length;
  if (*frame_count >= QA_MAX_ANIMATION_FRAMES || *retained_bytes > QA_MAX_ANIMATION_BYTES) {
    free(indices);
    free(previous);
    free(local_palette);
    qa_binary_error(name, (int64_t)qa_binary_reader_offset(reader),
                    "GIF exceeds the frame or decoded size limit");
  }
  if (*frame_count == *frame_capacity) {
    size_t capacity = *frame_capacity == 0 ? 4 : *frame_capacity * 2;
    qa_gif_frame *grown =
        (qa_gif_frame *)realloc(*frames, capacity * sizeof *grown);
    if (grown == NULL) {
      free(indices);
      free(previous);
      free(local_palette);
      return 0;
    }
    *frames = grown;
    *frame_capacity = capacity;
  }
  qa_gif_frame *frame = &(*frames)[*frame_count];
  memset(frame, 0, sizeof *frame);
  frame->image.width = (double)width;
  frame->image.height = (double)height;
  uint8_t *pixels = (uint8_t *)malloc(canvas_length);
  uint8_t *palette_copy = (uint8_t *)malloc(palette_length);
  if (pixels == NULL || palette_copy == NULL) {
    free(pixels);
    free(palette_copy);
    free(indices);
    free(previous);
    free(local_palette);
    return 0;
  }
  memcpy(pixels, canvas, canvas_length);
  memcpy(palette_copy, palette, palette_length);
  frame->image.pixels.bytes = pixels;
  frame->image.pixels.length = canvas_length;
  frame->rect_x = x;
  frame->rect_y = y;
  frame->rect_width = frame_width;
  frame->rect_height = frame_height;
  frame->indices = indices;
  frame->indices_length = (size_t)(frame_width * frame_height);
  frame->palette = palette_copy;
  frame->palette_length = palette_length;
  frame->transparent_index = transparent_index;
  frame->delay_centiseconds = delay;
  frame->disposal = disposal;
  (*frame_count)++;
  free(local_palette);
  if (disposal == 2) {
    for (int64_t row = y; row < y + frame_height; row++) {
      memset(canvas + (row * width + x) * 4, 0, (size_t)frame_width * 4);
    }
  } else if (previous != NULL) {
    memcpy(canvas, previous, canvas_length);
  }
  free(previous);
  return 1;
}

typedef struct gif_guard {
  qa_binary_reader *reader;
  uint8_t **global_palette;
  uint8_t **canvas;
  qa_gif_frame **frames;
  size_t *frame_count;
} gif_guard;

static void gif_guard_release(void *raw) {
  gif_guard *guard = (gif_guard *)raw;
  qa_gif_image partial;
  memset(&partial, 0, sizeof partial);
  partial.frames = *guard->frames;
  partial.frame_count = *guard->frame_count;
  partial.global_palette = *guard->global_palette;
  qa_gif_image_destroy(&partial);
  free(*guard->canvas);
  qa_binary_reader_destroy(guard->reader);
}

int qa_decode_gif(const uint8_t *bytes, size_t length, const char *source,
                  qa_gif_image *out) {
  const char *name = source != NULL ? source : "<gif>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) return 0;
  memset(out, 0, sizeof *out);
  /* A BinaryError caught by the caller must not leak the decode state. */
  uint8_t *global_palette = NULL;
  uint8_t *canvas = NULL;
  qa_gif_frame *frames = NULL;
  size_t frame_count = 0, frame_capacity = 0;
  gif_guard guard_state = {&reader, &global_palette, &canvas, &frames, &frame_count};
  qa_cleanup guard;
  qa_cleanup_push(&guard, gif_guard_release, &guard_state);
  char *signature = qa_binary_reader_fixed_byte_string(&reader, 6);
  if (signature == NULL) { qa_binary_reader_destroy(&reader); qa_cleanup_pop(&guard); return 0; }
  int signature_ok = strcmp(signature, "GIF87a") == 0 || strcmp(signature, "GIF89a") == 0;
  free(signature);
  if (!signature_ok) qa_binary_error(name, 0, "Invalid GIF signature");
  int64_t width = qa_binary_reader_u16(&reader);
  int64_t height = qa_binary_reader_u16(&reader);
  int64_t packed = qa_binary_reader_u8(&reader);
  int64_t background_index = qa_binary_reader_u8(&reader);
  qa_binary_reader_skip(&reader, 1);
  if (!qa_image_size_allowed((double)width, (double)height)) {
    qa_binary_error(name, 6, "Invalid GIF dimensions");
  }
  size_t global_palette_length = 0;
  if ((packed & 128) != 0) {
    global_palette_length = (size_t)(2 << (packed & 7)) * 3;
    global_palette = qa_binary_reader_bytes(&reader, (int64_t)global_palette_length);
    if (global_palette == NULL) { qa_binary_reader_destroy(&reader); qa_cleanup_pop(&guard); return 0; }
  }
  size_t canvas_length = (size_t)(width * height * 4);
  canvas = (uint8_t *)calloc(canvas_length, 1);
  if (canvas == NULL) {
    free(global_palette);
    qa_binary_reader_destroy(&reader);
    qa_cleanup_pop(&guard); return 0;
  }
  double retained_bytes = 0;
  int64_t transparent_index = -1, delay = 0, disposal = 0, loop_count = -1;
  int oom = 0;
  for (int block = qa_binary_reader_u8(&reader); block != 59 && !oom;
       block = qa_binary_reader_u8(&reader)) {
    if (block == 33) {
      int label = qa_binary_reader_u8(&reader);
      if (label == 249) {
        if (qa_binary_reader_u8(&reader) != 4) {
          qa_binary_error(name, (int64_t)qa_binary_reader_offset(&reader),
                          "Invalid GIF graphic control extension");
        }
        int flags = qa_binary_reader_u8(&reader);
        delay = qa_binary_reader_u16(&reader);
        int index = qa_binary_reader_u8(&reader);
        transparent_index = (flags & 1) == 0 ? -1 : index;
        disposal = (flags >> 2) & 7;
        if (qa_binary_reader_u8(&reader) != 0) {
          qa_binary_error(name, (int64_t)qa_binary_reader_offset(&reader),
                          "Invalid GIF extension terminator");
        }
      } else if (label == 255) {
        int application_length = qa_binary_reader_u8(&reader);
        char *application =
            qa_binary_reader_fixed_byte_string(&reader, application_length);
        if (application == NULL) { oom = 1; break; }
        uint8_t *payload = NULL;
        size_t payload_length = 0;
        if (!sub_blocks(&reader, &payload, &payload_length)) {
          free(application);
          oom = 1;
          break;
        }
        if ((strcmp(application, "NETSCAPE2.0") == 0 ||
             strcmp(application, "ANIMEXTS1.0") == 0) &&
            payload_length >= 3 && payload[0] == 1) {
          loop_count = payload[1] | (payload[2] << 8);
        }
        free(application);
        free(payload);
      } else {
        uint8_t *discard = NULL;
        size_t discard_length = 0;
        if (!sub_blocks(&reader, &discard, &discard_length)) { oom = 1; break; }
        free(discard);
      }
      continue;
    }
    if (block != 44) {
      qa_binary_error(name, (int64_t)qa_binary_reader_offset(&reader) - 1,
                      "Unknown GIF block %d", block);
    }
    oom = !gif_frame(&reader, name, width, height, global_palette,
                     global_palette_length, canvas, canvas_length,
                     transparent_index, delay, disposal, &frames, &frame_count,
                     &frame_capacity, &retained_bytes);
    transparent_index = -1;
    delay = 0;
    disposal = 0;
    if (oom) break;
  }
  free(canvas);
  canvas = NULL;
  if (oom) {
    out->frames = frames;
    out->frame_count = frame_count;
    out->global_palette = global_palette;
    out->global_palette_length = global_palette_length;
    qa_gif_image_destroy(out);
    qa_binary_reader_destroy(&reader);
    qa_cleanup_pop(&guard); return 0;
  }
  if (frame_count == 0) {
    qa_binary_error(name, (int64_t)qa_binary_reader_offset(&reader),
                    "GIF has no image frames");
  }
  qa_cleanup_pop(&guard);
  qa_binary_reader_destroy(&reader);
  out->width = width;
  out->height = height;
  out->frames = frames;
  out->frame_count = frame_count;
  out->loop_count = loop_count;
  out->background_index = background_index;
  out->global_palette = global_palette;
  out->global_palette_length = global_palette_length;
  return 1;
}
