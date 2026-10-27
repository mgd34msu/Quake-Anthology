/* Port of src/formats/q1-map/textures.ts. GPL-2.0-or-later. */
#include "qa/formats/q1_map/textures.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static void section_or_die(const qa_binary_reader *reader, int64_t offset, int64_t length,
                           qa_binary_reader *out) {
  if (!qa_binary_reader_section(reader, offset, length, out)) qa_range_error("Out of memory");
}

static qa_byte_view read_level(qa_binary_reader *reader, double offset, double mip_offset, double scale,
                               double width, double height, bool quake64, double header_size) {
  qa_byte_view view = {NULL, 0};
  if (quake64 && mip_offset == 0) return view;
  if (mip_offset < header_size) {
    qa_binary_error(reader->source, (int64_t)(offset + mip_offset), "mip pixels overlap texture header");
  }
  double length = floor(width / scale) * floor(height / scale);
  qa_binary_reader section;
  section_or_die(reader, (int64_t)(offset + mip_offset), (int64_t)length, &section);
  view.bytes = qa_binary_reader_bytes(&section, (int64_t)length);
  view.length = (size_t)length;
  qa_binary_reader_destroy(&section);
  return view;
}

void qa_q1_read_textures(qa_binary_reader *reader, bool quake64, qa_q1_texture_lump *out) {
  memset(out, 0, sizeof *out);
  if (reader->length == 0) return;
  int32_t count = qa_binary_reader_i32(reader);
  if (count < 0 || (double)count > (double)qa_binary_reader_remaining(reader) / 4) {
    qa_binary_error(reader->source, 0, "invalid miptex count %d", (int)count);
  }
  out->textures = xmalloc((size_t)count * sizeof *out->textures);
  out->offsets = xmalloc((size_t)count * sizeof *out->offsets);
  out->mip_offsets = xmalloc((size_t)count * sizeof *out->mip_offsets);
  out->texture_count = out->offset_count = out->mip_offset_count = (size_t)count;
  for (int32_t i = 0; i < count; i++) {
    int32_t offset = qa_binary_reader_i32(reader);
    if (offset != -1 && offset < 4 + count * 4) {
      qa_binary_error(reader->source, (int64_t)reader->cursor - 4, "miptex overlaps offset table");
    }
    out->offsets[i].has_value = offset != -1;
    out->offsets[i].value = offset;
    out->textures[i] = NULL;
    out->mip_offsets[i].has_value = false;
  }
  double header_size = quake64 ? 44 : 40;
  for (int32_t i = 0; i < count; i++) {
    if (!out->offsets[i].has_value) continue;
    double offset = out->offsets[i].value;
    qa_binary_reader header;
    section_or_die(reader, (int64_t)offset, (int64_t)header_size, &header);
    char *name = qa_binary_reader_fixed_byte_string(&header, 16);
    double width = qa_binary_reader_u32(&header);
    double height = qa_binary_reader_u32(&header);
    qa_q1_mip_texture *texture = xmalloc(sizeof *texture);
    memset(texture, 0, sizeof *texture);
    texture->name = name;
    texture->width = width;
    texture->height = height;
    if (quake64) {
      texture->has_quake64_shift = true;
      texture->quake64_shift = qa_binary_reader_u32(&header);
    }
    double mips[4];
    for (int j = 0; j < 4; j++) mips[j] = qa_binary_reader_u32(&header);
    qa_binary_reader_destroy(&header);
    out->mip_offsets[i].has_value = true;
    for (int j = 0; j < 4; j++) out->mip_offsets[i].values[j] = mips[j];
    if (width == 0 || height == 0) qa_binary_error(reader->source, (int64_t)offset, "zero-sized mip texture");
    bool all_zero = mips[0] == 0 && mips[1] == 0 && mips[2] == 0 && mips[3] == 0;
    if (!quake64 && all_zero) {
      texture->is_embedded = false;
      out->textures[i] = texture;
      continue;
    }
    texture->is_embedded = true;
    texture->levels[0] = read_level(reader, offset, quake64 ? header_size : mips[0], 1, width, height, quake64, header_size);
    texture->levels[1] = read_level(reader, offset, mips[1], 2, width, height, quake64, header_size);
    texture->levels[2] = read_level(reader, offset, mips[2], 4, width, height, quake64, header_size);
    texture->levels[3] = read_level(reader, offset, mips[3], 8, width, height, quake64, header_size);
    out->textures[i] = texture;
  }
}

void qa_q1_texture_lump_destroy(qa_q1_texture_lump *lump) {
  if (lump == NULL) return;
  for (size_t i = 0; i < lump->texture_count; i++) {
    qa_q1_mip_texture *texture = lump->textures[i];
    if (texture == NULL) continue;
    free((void *)texture->name);
    for (int j = 0; j < 4; j++) free((void *)texture->levels[j].bytes);
    free(texture);
  }
  free(lump->textures);
  free(lump->offsets);
  free(lump->mip_offsets);
  memset(lump, 0, sizeof *lump);
}

uint8_t *qa_q1_texture_rgba(const uint8_t *pixels, size_t pixels_length,
                            const uint8_t *palette, size_t palette_length,
                            bool has_transparent_index, double transparent_index,
                            size_t *out_length) {
  if (palette_length != 768) {
    qa_range_error("Quake palette must contain 768 bytes, got %zu", palette_length);
  }
  if (has_transparent_index &&
      (transparent_index != trunc(transparent_index) || transparent_index < 0 || transparent_index > 255)) {
    qa_range_error("Transparent palette index must be a byte");
  }
  uint8_t *output = xmalloc(pixels_length * 4);
  size_t offset = 0;
  for (size_t i = 0; i < pixels_length; i++) {
    uint8_t pixel = pixels[i];
    output[offset++] = palette[pixel * 3];
    output[offset++] = palette[pixel * 3 + 1];
    output[offset++] = palette[pixel * 3 + 2];
    output[offset++] = has_transparent_index && (double)pixel == transparent_index ? 0 : 255;
  }
  *out_length = pixels_length * 4;
  return output;
}
