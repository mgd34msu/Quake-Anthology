/* Port of src/materials/geometry.ts. GPL-2.0-or-later. */
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/materials/geometry.h"

void qa_material_deform_state_init(qa_material_deform_state *state,
    qa_material_geometry geometry, double refdef_time,
    const char *const *render_text, size_t render_text_count,
    const char *material_name) {
  state->geometry = geometry;
  state->owned_vertices = NULL;
  state->owned_indices = NULL;
  state->refdef_time = refdef_time;
  state->render_text = render_text;
  state->render_text_count = render_text_count;
  state->material_name = material_name;
}

void qa_material_deform_state_destroy(qa_material_deform_state *state) {
  free(state->owned_vertices);
  free(state->owned_indices);
  state->owned_vertices = NULL;
  state->owned_indices = NULL;
}

qa_material_geometry qa_material_deform_state_snapshot(
    const qa_material_deform_state *state) {
  return state->geometry;
}

void qa_material_deform_state_replace(qa_material_deform_state *state,
                                      qa_material_geometry geometry) {
  qa_material_vertex *vertices = NULL;
  double *indices = NULL;
  if (geometry.vertex_count > 0) {
    vertices = malloc(geometry.vertex_count * sizeof *vertices);
    if (vertices == NULL) qa_range_error("material geometry allocation failed");
    memcpy(vertices, geometry.vertices,
           geometry.vertex_count * sizeof *vertices);
  }
  if (geometry.index_count > 0) {
    indices = malloc(geometry.index_count * sizeof *indices);
    if (indices == NULL) qa_range_error("material geometry allocation failed");
    memcpy(indices, geometry.indices, geometry.index_count * sizeof *indices);
  }
  free(state->owned_vertices);
  free(state->owned_indices);
  state->owned_vertices = vertices;
  state->owned_indices = indices;
  state->geometry.vertices = vertices;
  state->geometry.vertex_count = geometry.vertex_count;
  state->geometry.indices = indices;
  state->geometry.index_count = geometry.index_count;
}

void qa_material_deform_state_reset(qa_material_deform_state *state) {
  qa_material_geometry empty = {NULL, 0, NULL, 0};
  qa_material_deform_state_replace(state, empty);
}

void qa_material_deform_state_text_quad(const qa_material_deform_state *state,
                                        qa_material_vertex out[4]) {
  if (state->geometry.vertex_count < 4)
    qa_range_error("Text deformation requires a four-vertex source quad");
  for (int i = 0; i < 4; i++) out[i] = state->geometry.vertices[i];
}

void qa_material_deform_state_append_stamp(qa_material_deform_state *state,
                                           qa_material_geometry geometry) {
  size_t offset = state->geometry.vertex_count;
  size_t vertices = offset + geometry.vertex_count;
  size_t indices = state->geometry.index_count + geometry.index_count;
  qa_material_vertex *merged_v = NULL;
  double *merged_i = NULL;
  if (vertices > 0) {
    merged_v = malloc(vertices * sizeof *merged_v);
    if (merged_v == NULL) qa_range_error("material geometry allocation failed");
    if (offset > 0)
      memcpy(merged_v, state->geometry.vertices, offset * sizeof *merged_v);
    if (geometry.vertex_count > 0)
      memcpy(merged_v + offset, geometry.vertices,
             geometry.vertex_count * sizeof *merged_v);
  }
  if (indices > 0) {
    merged_i = malloc(indices * sizeof *merged_i);
    if (merged_i == NULL) qa_range_error("material geometry allocation failed");
    if (state->geometry.index_count > 0)
      memcpy(merged_i, state->geometry.indices,
             state->geometry.index_count * sizeof *merged_i);
    for (size_t i = 0; i < geometry.index_count; i++)
      merged_i[state->geometry.index_count + i] =
          (double)offset + geometry.indices[i];
  }
  free(state->owned_vertices);
  free(state->owned_indices);
  state->owned_vertices = merged_v;
  state->owned_indices = merged_i;
  state->geometry.vertices = merged_v;
  state->geometry.vertex_count = vertices;
  state->geometry.indices = merged_i;
  state->geometry.index_count = indices;
}
