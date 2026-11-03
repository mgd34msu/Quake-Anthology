/* Port of src/render/scene/shadow-geometry.ts. GPL-2.0-or-later. */
#include "qa/render/scene/shadow_geometry.h"
#include <math.h>
#include <string.h>
#include "qa/materials/deform.h"
#include "qa/render/scene/view.h"
static int32_t i32d(double v) {
  if (!isfinite(v)) return 0;
  double m = fmod(trunc(v), 4294967296.0);
  if (m < 0) m += 4294967296.0;
  if (m >= 2147483648.0) m -= 4294967296.0;
  return (int32_t)m;
}
static int is(const char *a, const char *b) { return a && !strcmp(a, b); }
bool qa_scene_flags_cast_shadow(const char *kind, double bits, double alpha, bool view_model) {
  if (view_model) return false;
  if (is(kind, "q2")) return (i32d(bits) & (4 | 16 | 32 | 128 | 8192 | 0x00200000)) == 0;
  if (is(kind, "q3") && (i32d(bits) & (4 | 8 | 64)) != 0) return false;
  return alpha >= 1;
}
bool qa_scene_entity_casts_shadow(const qa_scene_entity *entity, bool view_model) {
  if (view_model || entity->model.kind == QA_DECODED_MODEL_Q1_SPRITE
      || entity->model.kind == QA_DECODED_MODEL_Q2_SPRITE) return false;
  return qa_scene_flags_cast_shadow(entity->flags_kind, entity->flags_bits, entity->color.w, view_model);
}
void qa_scene_shadow_geometry_destroy(qa_scene_shadow_geometry *g) {
  if (!g) return;
  if (g->has_geometry) qa_material_deform_state_destroy(&g->_state);
  memset(g, 0, sizeof *g);
}
void qa_shadow_material_geometry(const qa_compiled_material *shader, qa_material_geometry geometry,
                                 const qa_material_draw_context *context, qa_scene_shadow_geometry *out) {
  memset(out, 0, sizeof *out);
  const qa_mat_shader_definition *definition = &shader->registered.definition;
  int rejected = is(shader->finished.iterator.kind, "sky") || shader->finished.sort > 3;
  static const char *bad_parms[5] = { "nodraw", "trans", "water", "slime", "lava" };
  for (size_t i = 0; !rejected && i < definition->surface_parms.count; i++)
    for (int k = 0; k < 5; k++) if (is(definition->surface_parms.values[i], bad_parms[k])) rejected = 1;
  for (size_t i = 0; !rejected && i < definition->deform_count; i++)
    if (is(definition->deforms[i].kind, "autosprite") || is(definition->deforms[i].kind, "autosprite2")
        || is(definition->deforms[i].kind, "projectionshadow")) rejected = 1;
  if (rejected) { out->has_geometry = false; return; }
  double time = context->time - context->time_offset;
  if (definition->clamp_time != 0 && time >= definition->clamp_time) time = definition->clamp_time;
  /* TS MaterialDeformState takes the whole definition; C geometry.h keeps
   * only material_name (definition name), matching the landed port. */
  qa_material_deform_state_init(&out->_state, geometry, context->refdef_time,
                                context->render_text, context->render_text_count, definition->name);
  out->geometry = qa_deform_geometry_apply(&out->_state, definition->deforms, definition->deform_count,
                                            context->deform_view, (float)time, context->noise, context->projection_shadow);
  out->has_geometry = true;
}
void qa_shadow_finish_quad(const qa_scene_camera *camera, qa_vec4 out[4]) {
  static const double corners[4][2] = {{-100, 100}, {100, 100}, {100, -100}, {-100, -100}};
  for (int k = 0; k < 4; k++) out[k] = qa_scene_project_point(camera->projection, (qa_vec3){corners[k][0], corners[k][1], -10});
}
