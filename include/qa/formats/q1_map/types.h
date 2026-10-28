/*
 * Port of src/formats/q1-map/types.ts (types only). TS `number | null`
 * becomes qa_q1_optional_number; Uint8Array becomes owned bytes + length.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q1_MAP_TYPES_H
#define QA_FORMATS_Q1_MAP_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"

/* TS Q1BspFormat: "bsp29" | "bsp2" | "2psb" | "quake64". */
typedef enum qa_q1_bsp_format {
  QA_Q1_FORMAT_BSP29,
  QA_Q1_FORMAT_BSP2,
  QA_Q1_FORMAT_2PSB,
  QA_Q1_FORMAT_QUAKE64
} qa_q1_bsp_format;

static inline const char *qa_q1_bsp_format_name(qa_q1_bsp_format format) {
  switch (format) {
    case QA_Q1_FORMAT_BSP29: return "bsp29";
    case QA_Q1_FORMAT_BSP2: return "bsp2";
    case QA_Q1_FORMAT_2PSB: return "2psb";
    case QA_Q1_FORMAT_QUAKE64: return "quake64";
  }
  return "bsp29";
}

typedef struct qa_q1_optional_number {
  bool has_value; /* false is TS null */
  double value;
} qa_q1_optional_number;

typedef struct qa_q1_mip_offset_row {
  bool has_value;
  double values[4];
} qa_q1_mip_offset_row;

/* TS Q1Lump. `name` is one of the 15 Q1LumpName literals, in TS
 * order: "entities", "planes", "textures", "vertices",
 * "visibility", "nodes", "textureInfo", "faces", "lighting",
 * "clipnodes", "leaves", "leafFaces", "edges", "surfaceEdges",
 * "models" (the string table lives in formats/q1_map/index.c);
 * data owned. */
typedef struct qa_q1_lump {
  const char *name;
  double offset;
  double length;
  uint8_t *data;
  size_t data_length;
} qa_q1_lump;

typedef struct qa_q1_bspx_lump {
  char *name; /* owned */
  double offset;
  double length;
  uint8_t *data; /* owned */
  size_t data_length;
} qa_q1_bspx_lump;

typedef struct qa_q1_entity_property {
  char *key;   /* owned */
  char *value; /* owned */
} qa_q1_entity_property;

typedef struct qa_q1_entity {
  qa_q1_entity_property *properties; /* owned */
  size_t property_count;
} qa_q1_entity;

typedef struct qa_q1_face_normals {
  qa_vec3 *vectors; /* owned */
  size_t vector_count;
  double (*indices)[3]; /* owned */
  size_t index_count;
} qa_q1_face_normals;

typedef struct qa_q1_style_row {
  double *values; /* owned */
  size_t count;
} qa_q1_style_row;

/* TS Q1BspxMetadata. A NULL pointer field is TS null. */
typedef struct qa_q1_bspx_metadata {
  uint8_t *lightmap_shifts;
  size_t lightmap_shift_count;
  qa_q1_optional_number *lightmap_offsets;
  size_t lightmap_offset_count;
  qa_q1_style_row *lightmap_styles;
  size_t lightmap_style_count;
  qa_q1_style_row *lightmap_styles16;
  size_t lightmap_style16_count;
  uint8_t *rgb_lighting;
  size_t rgb_lighting_length;
  double *hdr_lighting;
  size_t hdr_lighting_count;
  uint8_t *lighting_directions;
  size_t lighting_direction_length;
  qa_vec3 *vertex_normals;
  size_t vertex_normal_count;
  qa_q1_face_normals *face_normals; /* NULL is TS null */
} qa_q1_bspx_metadata;

/* TS Q1Map extends Q1WorldGeometry. All storage owned; free with
 * qa_q1_map_destroy (declared in the umbrella header). */
typedef struct qa_q1_map {
  qa_q1_world_geometry world;
  char *source; /* owned */
  double version;
  qa_q1_lump *lumps;
  size_t lump_count;
  qa_q1_bspx_lump *bspx;
  size_t bspx_count;
  qa_q1_bspx_metadata bspx_metadata;
  qa_q1_entity *entity_list;
  size_t entity_count;
  qa_byte_view monochrome_lighting;
  qa_q1_optional_number *texture_offsets;
  size_t texture_offset_count;
  qa_q1_mip_offset_row *mip_offsets;
  size_t mip_offset_count;
} qa_q1_map;

/* TS Q1MapOptions. has_entities/has_lit mark TS undefined. */
typedef struct qa_q1_map_options {
  const char *source; /* NULL means "<Quake BSP>" */
  bool has_entities;
  const uint8_t *entities;
  size_t entities_length;
  bool has_lit;
  const uint8_t *lit;
  size_t lit_length;
} qa_q1_map_options;

#endif
