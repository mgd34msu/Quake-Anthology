/* Port of src/world/collision/q3/map-resource.ts. Map data is owned
 * record arrays (source hunk byte layout/pointer identity not
 * reproduced; record values identical). GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_MAP_RESOURCE_H
#define QA_WORLD_COLLISION_Q3_MAP_RESOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/world/collision/q3/allocation.h"
#include "qa/world/collision/q3/patch.h"
typedef struct qa_q3_collision_plane { qa_plane plane; double type; double signbits; } qa_q3_collision_plane;
typedef struct qa_q3_collision_node { double plane; double children[2]; } qa_q3_collision_node;
typedef struct qa_q3_collision_leaf { double cluster, area, first_brush, brush_count, first_surface, surface_count; } qa_q3_collision_leaf;
typedef struct qa_collision_indexes { const double *values; size_t length; } qa_collision_indexes;
double qa_collision_indexes_at(qa_collision_indexes indexes, double index);
typedef struct qa_q3_collision_model_rec { qa_bounds bounds; qa_collision_indexes brushes; qa_collision_indexes surfaces; } qa_q3_collision_model_rec;
typedef struct qa_q3_collision_brush { double shader, contents; qa_bounds bounds; double first_side, side_count; int32_t check_count; } qa_q3_collision_brush;
typedef struct qa_q3_collision_brush_side { double plane, shader, surface_flags; } qa_q3_collision_brush_side;
typedef struct qa_q3_collision_patch_rec { double contents, surface_flags; qa_patch_collide *collide; int32_t check_count; } qa_q3_collision_patch_rec;
typedef struct qa_q3_collision_area { int32_t flood, flood_valid; } qa_q3_collision_area;
typedef struct qa_q3_collision_box_model { qa_bounds bounds; } qa_q3_collision_box_model;
void qa_q3_collision_box_model_init(qa_q3_collision_box_model *m);
typedef struct qa_collision_box_storage qa_collision_box_storage;
typedef struct qa_collision_map_data {
  const char *kind; int32_t check_count;
  char *entities; /* owned, NULL until loaded */
  qa_q3_bsp_shader *shaders; size_t shader_count; char **shader_names;
  qa_q3_collision_plane *planes; size_t plane_count;
  qa_q3_collision_node *nodes; size_t node_count;
  qa_q3_collision_leaf *leaves; size_t leaf_count;
  double *leaf_brush_values; qa_collision_indexes leaf_brushes;
  double *leaf_surface_values; qa_collision_indexes leaf_surfaces;
  qa_q3_collision_brush *brushes; size_t brush_count;
  qa_q3_collision_brush_side *brush_sides; size_t brush_side_count;
  qa_q3_collision_model_rec *models; size_t model_count;
  double **model_index_values; /* owned per-model index storage */
  qa_q3_collision_patch_rec **patches; size_t patch_count; /* per surface, NULL-absent */
  bool surfaces_loaded;
  qa_q3_collision_area *areas; size_t area_count;
  int32_t *portals; size_t portal_count; bool portals_loaded;
  double cluster_count;
  uint8_t *visibility; size_t visibility_length; bool visibility_loaded;
  bool has_visibility_row_bytes; double visibility_row_bytes;
  qa_collision_box_storage *box; /* NULL = TS null */
} qa_collision_map_data;
double qa_collision_portals_at(const qa_collision_map_data *m, double index);
void qa_collision_portals_set(qa_collision_map_data *m, double index, double value);
qa_q3_collision_patch_rec *qa_collision_map_patch(qa_collision_map_data *m, double index);
void qa_collision_map_data_destroy(qa_collision_map_data *m);
struct qa_collision_box_storage {
  qa_collision_map_data *map; qa_q3_collision_box_model *model;
  qa_q3_collision_brush *brush; /* borrowed map brush (box brush) */
};
qa_bounds qa_collision_box_storage_bounds(const qa_collision_box_storage *s);
int32_t qa_collision_box_storage_check_count(const qa_collision_box_storage *s);
typedef struct qa_box_side_read { qa_q3_collision_plane plane; double surface_flags; } qa_box_side_read;
qa_box_side_read qa_collision_box_read_side(qa_collision_box_storage *s, double index);
void qa_collision_box_set_bounds(qa_collision_box_storage *s, qa_vec3 mins, qa_vec3 maxs, bool capsule);
/* Heap decoded map; destroy with qa_collision_map_data_destroy+free via helper. */
qa_collision_map_data *qa_decoded_collision_map(const qa_q3_world_geometry *map, qa_collision_debug_surface *debug);
void qa_decoded_collision_map_destroy(qa_collision_map_data *m);
#endif
