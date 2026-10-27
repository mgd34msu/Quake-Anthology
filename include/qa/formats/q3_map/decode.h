/*
 * Port of src/formats/q3-map/decode.ts. Record shapes that match
 * contracts/scene.h reuse those structs; raw node/leaf/model/brush/
 * surface records keep their stored field layout. All storage owned.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q3_MAP_DECODE_H
#define QA_FORMATS_Q3_MAP_DECODE_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/formats/q3_map/entities.h"

#define QA_Q3_LIGHTMAP_WIDTH 128
#define QA_Q3_LIGHTMAP_HEIGHT 128
#define QA_Q3_LIGHTMAP_BYTES (128 * 128 * 3)

typedef struct qa_q3_raw_node {
  double plane;
  double children[2];
  qa_bounds bounds;
} qa_q3_raw_node;

typedef struct qa_q3_raw_leaf {
  double cluster;
  double area;
  qa_bounds bounds;
  double first_surface;
  double surface_count;
  double first_brush;
  double brush_count;
} qa_q3_raw_leaf;

typedef struct qa_q3_raw_model {
  qa_bounds bounds;
  double first_surface;
  double surface_count;
  double first_brush;
  double brush_count;
} qa_q3_raw_model;

typedef struct qa_q3_raw_brush {
  double first_side;
  double side_count;
  double shader;
} qa_q3_raw_brush;

/* TS BspSurface: type is "planar" | "patch" | "triangles" | "flare". */
typedef struct qa_q3_raw_surface {
  const char *type;
  double shader;
  double fog;
  double first_vertex;
  double vertex_count;
  double first_index;
  double index_count;
  double lightmap;
  double lightmap_x;
  double lightmap_y;
  double lightmap_width;
  double lightmap_height;
  qa_vec3 lightmap_origin;
  qa_vec3 lightmap_vectors[3];
  double patch_width;
  double patch_height;
} qa_q3_raw_surface;

/* TS BspMap. */
typedef struct qa_q3_bsp_map {
  char *entities; /* owned */
  qa_q3_entity *entity_records;
  size_t entity_record_count;
  qa_q3_bsp_shader *shaders;
  size_t shader_count;
  qa_plane *planes;
  size_t plane_count;
  qa_q3_raw_node *nodes;
  size_t node_count;
  qa_q3_raw_leaf *leaves;
  size_t leaf_count;
  double *leaf_surfaces;
  size_t leaf_surface_count;
  double *leaf_brushes;
  size_t leaf_brush_count;
  qa_q3_raw_model *models;
  size_t model_count;
  qa_q3_raw_brush *brushes;
  size_t brush_count;
  qa_q3_bsp_brush_side *brush_sides;
  size_t brush_side_count;
  qa_q3_bsp_vertex *vertices;
  size_t vertex_count;
  double *indices;
  size_t index_count;
  qa_q3_bsp_fog *fogs;
  size_t fog_count;
  qa_q3_raw_surface *surfaces;
  size_t surface_count;
  qa_byte_view *lightmaps; /* owned bytes per entry */
  size_t lightmap_count;
  qa_q3_light_grid_point *light_grid;
  size_t light_grid_count;
  qa_q3_visibility *visibility; /* NULL is TS null */
} qa_q3_bsp_map;

/* `source` NULL means "<bsp>". */
void qa_q3_parse_bsp(const uint8_t *data, size_t data_length, const char *source, qa_q3_bsp_map *out);
void qa_q3_bsp_map_destroy(qa_q3_bsp_map *map);

#endif
