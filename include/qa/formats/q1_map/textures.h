/*
 * Port of src/formats/q1-map/textures.ts. Texture slots are owned
 * pointers; a NULL slot is the TS null texture. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q1_MAP_TEXTURES_H
#define QA_FORMATS_Q1_MAP_TEXTURES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/scene.h"
#include "qa/core/binary/index.h"
#include "qa/formats/q1_map/types.h"

/* TS Q1TextureLump. */
typedef struct qa_q1_texture_lump {
  qa_q1_mip_texture **textures; /* owned slots, may contain NULL */
  size_t texture_count;
  qa_q1_optional_number *offsets;
  size_t offset_count;
  qa_q1_mip_offset_row *mip_offsets;
  size_t mip_offset_count;
} qa_q1_texture_lump;

void qa_q1_read_textures(qa_binary_reader *reader, bool quake64, qa_q1_texture_lump *out);
void qa_q1_texture_lump_destroy(qa_q1_texture_lump *lump);

/* Malloc'd RGBA copy; *out_length = pixels_length * 4. RangeError
 * aborts reproduce the TS palette/index checks. */
uint8_t *qa_q1_texture_rgba(const uint8_t *pixels, size_t pixels_length,
                            const uint8_t *palette, size_t palette_length,
                            bool has_transparent_index, double transparent_index,
                            size_t *out_length);

#endif
