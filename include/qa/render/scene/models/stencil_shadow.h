/* Port of src/render/scene/models/stencil-shadow.ts: stencil shadow volume silhouettes from
 * Quake III tr_shadows.c (RB_ShadowTessEnd, R_RenderShadowEdges). GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_MODELS_STENCIL_SHADOW_H
#define QA_RENDER_SCENE_MODELS_STENCIL_SHADOW_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
/* SHADER_MAX_VERTEXES / 2: id skips the surface when the tessellation is larger. */
#define QA_STENCIL_SHADOW_MAX_VERTEXES 500
typedef struct qa_stencil_shadow_volume {
  /* The surface vertices followed by the same vertices pushed 512 units away from the light. */
  qa_vec3 *positions; size_t position_count;
  /* Two triangles per silhouette edge, in the winding of id's triangle strip (v, s, v2, s2). */
  double *indices; size_t index_count;
} qa_stencil_shadow_volume;
/* lightDir points toward the light in the surface's own space. Returns false where id draws
 * nothing (TS null); on true the caller owns out (qa_stencil_shadow_volume_destroy). */
bool qa_make_stencil_shadow_volume(const qa_vec3 *positions, size_t position_count, const double *indices, size_t index_count,
                              qa_vec3 light_dir, qa_stencil_shadow_volume *out);
void qa_stencil_shadow_volume_destroy(qa_stencil_shadow_volume *volume);
#endif
