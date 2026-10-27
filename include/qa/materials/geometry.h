/*
 * Port of src/materials/geometry.ts: source Q3 byte-color vertices and
 * the retained deformation state. The state borrows its initial
 * geometry; replace/reset/append copy into state-owned storage freed by
 * qa_material_deform_state_destroy. GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_GEOMETRY_H
#define QA_MATERIALS_GEOMETRY_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/scene.h"

/* TS MaterialVertex = Q3BspVertex (byte units in color). */
typedef qa_q3_bsp_vertex qa_material_vertex;

typedef struct qa_material_geometry {
  const qa_material_vertex *vertices; size_t vertex_count;
  const double *indices; size_t index_count;
} qa_material_geometry;

/* TS MaterialDeformState. material_name NULL = TS material null. */
typedef struct qa_material_deform_state {
  qa_material_geometry geometry;
  qa_material_vertex *owned_vertices;
  double *owned_indices;
  double refdef_time;
  const char *const *render_text; size_t render_text_count;
  const char *material_name;
} qa_material_deform_state;

void qa_material_deform_state_init(qa_material_deform_state *state,
    qa_material_geometry geometry, double refdef_time,
    const char *const *render_text, size_t render_text_count,
    const char *material_name);
void qa_material_deform_state_destroy(qa_material_deform_state *state);
qa_material_geometry qa_material_deform_state_snapshot(
    const qa_material_deform_state *state);
void qa_material_deform_state_replace(qa_material_deform_state *state,
                                      qa_material_geometry geometry);
void qa_material_deform_state_reset(qa_material_deform_state *state);
/* Aborts with the TS Error message when fewer than four vertices. */
void qa_material_deform_state_text_quad(const qa_material_deform_state *state,
                                        qa_material_vertex out[4]);
void qa_material_deform_state_append_stamp(qa_material_deform_state *state,
                                           qa_material_geometry geometry);

#endif
