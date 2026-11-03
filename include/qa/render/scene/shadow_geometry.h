/* Port of src/render/scene/shadow-geometry.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_SHADOW_GEOMETRY_H
#define QA_RENDER_SCENE_SHADOW_GEOMETRY_H
#include <stdbool.h>
#include "qa/contracts/scene.h"
#include "qa/materials/compile.h"
#include "qa/materials/evaluate.h"
#include "qa/materials/geometry.h"
bool qa_scene_entity_casts_shadow(const qa_scene_entity *entity, bool view_model);
/* flags_kind/bits mirror TS SceneEntity.flags ("q1"|"q2"|"q3", bits). */
bool qa_scene_flags_cast_shadow(const char *flags_kind, double flags_bits, double alpha, bool view_model);
typedef struct qa_scene_shadow_geometry {
  qa_material_geometry geometry; bool has_geometry;
  qa_material_deform_state _state; /* owned deformation storage */
} qa_scene_shadow_geometry;
void qa_scene_shadow_geometry_destroy(qa_scene_shadow_geometry *g);
/* has_geometry false is TS null. */
void qa_shadow_material_geometry(const qa_compiled_material *shader, qa_material_geometry geometry, const qa_material_draw_context *context, qa_scene_shadow_geometry *out);
/* TS shadowFinishQuad: RB_ShadowFinish draws this quad at eye-space z = -10 under the view projection. */
void qa_shadow_finish_quad(const qa_scene_camera *camera, qa_vec4 out[4]);
#endif
