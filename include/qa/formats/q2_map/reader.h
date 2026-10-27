/*
 * Port of src/formats/q2-map/reader.ts. Raw records retain on-disk
 * sentinels as in the TS. All storage owned. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q2_MAP_READER_H
#define QA_FORMATS_Q2_MAP_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/formats/q2_map/bspx.h"

typedef struct qa_q2_lump {
  double offset;
  double length;
} qa_q2_lump;

typedef struct qa_q2_raw_face {
  double plane;
  double draw_flags;
  qa_index_range edges;
  double texture_info;
  double styles[4];
  double lighting_offset;
} qa_q2_raw_face;

typedef struct qa_q2_raw_texture_info {
  qa_texture_projection projection;
  double flags;
  double value;
  char *name; /* owned */
  double next;
} qa_q2_raw_texture_info;

typedef struct qa_q2_raw_leaf {
  double contents;
  double cluster;
  double area;
  qa_bounds bounds;
  qa_index_range faces;
  qa_index_range brushes;
} qa_q2_raw_leaf;

typedef struct qa_q2_bsp {
  char *source; /* owned */
  const char *format; /* "ibsp38" | "qbsp" */
  double version;     /* 38 */
  qa_q2_lump *lumps;
  size_t lump_count;
  char *entities; /* owned */
  uint8_t *entity_bytes;
  size_t entity_byte_length;
  qa_bsp_plane *planes;
  size_t plane_count;
  qa_vec3 *vertices;
  size_t vertex_count;
  qa_bsp_edge *edges;
  size_t edge_count;
  double *surface_edges;
  size_t surface_edge_count;
  qa_bsp_node *nodes;
  size_t node_count;
  qa_q2_raw_leaf *leaves;
  size_t leaf_count;
  double *leaf_faces;
  size_t leaf_face_count;
  double *leaf_brushes;
  size_t leaf_brush_count;
  qa_q2_raw_texture_info *texture_info;
  size_t texture_info_count;
  qa_q2_raw_face *faces;
  size_t face_count;
  qa_q2_brush *brushes;
  size_t brush_count;
  qa_q2_brush_side *brush_sides;
  size_t brush_side_count;
  qa_q2_world_model *models;
  size_t model_count;
  qa_q2_area *areas;
  size_t area_count;
  qa_q2_area_portal *area_portals;
  size_t area_portal_count;
  qa_q2_visibility *visibility; /* NULL is TS null */
  uint8_t *lighting;
  size_t lighting_length;
  uint8_t *pop;
  size_t pop_length;
  bool has_bspx;
  qa_q2_bspx_directory bspx;
  qa_q2_string_list diagnostics;
} qa_q2_bsp;

/* `source` NULL means "<q2-map>". */
void qa_read_q2_bsp(const uint8_t *bytes, size_t length, const char *source, qa_q2_bsp *out);
void qa_q2_bsp_destroy(qa_q2_bsp *map);
/* kind is "pvs" | "phs"; visibility NULL is the TS null. Malloc'd row. */
uint8_t *qa_decompress_q2_visibility(const qa_q2_visibility *visibility, double cluster, const char *kind,
                                     double cluster_count, size_t *out_length);

#endif
