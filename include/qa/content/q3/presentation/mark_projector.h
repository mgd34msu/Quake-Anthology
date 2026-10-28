/* Port of src/content/q3/presentation/mark-projector.ts. GPL-2.0-or-later.
 * World decal projection from id Software's renderer/tr_marks.c.
 * Geometry spans are borrowed. Face vertices carry only positions and
 * grid vertices position+normal (the only fields the TS reads). */
#ifndef QA_CONTENT_Q3_PRESENTATION_MARK_PROJECTOR_H
#define QA_CONTENT_Q3_PRESENTATION_MARK_PROJECTOR_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
typedef enum qa_q3_pres_mark_surface_kind {
  QA_Q3_PRES_MARK_SKIP, QA_Q3_PRES_MARK_FACE, QA_Q3_PRES_MARK_GRID
} qa_q3_pres_mark_surface_kind;
typedef struct qa_q3_pres_mark_grid_vertex { qa_vec3 position, normal; } qa_q3_pres_mark_grid_vertex;
typedef struct qa_q3_pres_mark_surface {
  qa_q3_pres_mark_surface_kind kind;
  double surface_flags, content_flags;
  qa_plane plane; /* face */
  const qa_vec3 *vertices; size_t vertex_count; /* face positions */
  const double *indices; size_t index_count; /* face */
  const qa_q3_pres_mark_grid_vertex *grid_vertices; size_t grid_vertex_count;
  double grid_width, grid_height;
} qa_q3_pres_mark_surface;
typedef struct qa_q3_pres_mark_node {
  double plane;
  bool child_is_node[2]; double child_index[2];
} qa_q3_pres_mark_node;
typedef struct qa_q3_pres_mark_leaf { double first_surface, surface_count; } qa_q3_pres_mark_leaf;
typedef struct qa_q3_pres_mark_geometry {
  const qa_q3_pres_mark_node *nodes; size_t node_count;
  const qa_plane *planes; size_t plane_count;
  const qa_q3_pres_mark_leaf *leaves; size_t leaf_count;
  const double *leaf_surfaces; size_t leaf_surface_count;
  double surface_count;
  const qa_q3_pres_mark_surface *surfaces; size_t surfaces_count;
} qa_q3_pres_mark_geometry;
typedef struct qa_q3_pres_mark_fragment { double first_point, point_count; } qa_q3_pres_mark_fragment;
typedef struct qa_q3_pres_mark_fragments {
  qa_vec3 *points; size_t point_count; /* owned */
  qa_q3_pres_mark_fragment *fragments; size_t fragment_count; /* owned */
} qa_q3_pres_mark_fragments;
void qa_q3_pres_mark_fragments_destroy(qa_q3_pres_mark_fragments *fragments);
typedef struct qa_q3_pres_mark_projection {
  const qa_vec3 *points; size_t point_count;
  qa_vec3 projection;
  double max_points, max_fragments;
} qa_q3_pres_mark_projection;
/* TS SourceMarkProjection: borrowed inputs and reached output stores. */
typedef struct qa_q3_pres_source_mark_projection {
  double point_count, max_points, max_fragments;
  void *ctx;
  qa_vec3 (*read_point)(void *ctx, double index);
  qa_vec3 (*read_projection)(void *ctx);
  void (*write_fragment)(void *ctx, double index, qa_q3_pres_mark_fragment fragment);
  void (*write_points)(void *ctx, double first_point, const qa_vec3 *points, size_t count);
} qa_q3_pres_source_mark_projection;
typedef struct qa_q3_pres_bsp_mark_projector { qa_q3_pres_mark_geometry geometry; } qa_q3_pres_bsp_mark_projector;
/* Aborts with the TS RangeError unless every BSP surface index is kept. */
void qa_q3_pres_bsp_mark_projector_init(qa_q3_pres_bsp_mark_projector *projector, qa_q3_pres_mark_geometry geometry);
/* False only on OOM. */
bool qa_q3_pres_bsp_mark_projector_mark_fragments(qa_q3_pres_bsp_mark_projector *projector,
    qa_q3_pres_mark_projection query, qa_q3_pres_mark_fragments *out);
double qa_q3_pres_bsp_mark_projector_mark_fragments_record(qa_q3_pres_bsp_mark_projector *projector,
    qa_q3_pres_source_mark_projection query);
#endif
