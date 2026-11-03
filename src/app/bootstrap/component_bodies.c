/* Port of src/app/bootstrap/component-bodies.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/component_bodies.h"
#include "qa/network/common/js_number.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* TS materialKey: JSON of the dedup fields (skin only without shader). */
static void material_key(const qa_app_body_material *pass, char *buf, size_t cap) {
  snprintf(buf, cap, "[%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s]",
      pass->has_custom_shader && pass->custom_shader_name ? pass->custom_shader_name : "null",
      QA_JS_NUMBER((double)pass->shader_rgba.x), QA_JS_NUMBER((double)pass->shader_rgba.y), QA_JS_NUMBER((double)pass->shader_rgba.z), QA_JS_NUMBER((double)pass->shader_rgba.w),
      QA_JS_NUMBER((double)pass->shader_tex_coord.x), QA_JS_NUMBER((double)pass->shader_tex_coord.y), QA_JS_NUMBER(pass->shader_time), QA_JS_NUMBER(pass->render_flags),
      QA_JS_NUMBER((double)pass->lighting_origin.x), QA_JS_NUMBER((double)pass->lighting_origin.y), QA_JS_NUMBER((double)pass->lighting_origin.z), QA_JS_NUMBER(pass->shadow_plane),
      pass->non_normalized_axes ? "true" : "false",
      !pass->has_custom_shader && pass->has_custom_skin && pass->custom_skin_surfaces ? pass->custom_skin_surfaces : "null");
}
size_t qa_app_body_materials(const qa_app_component_body *body, const char *part,
                             const qa_app_body_material **out, size_t capacity) {
  char (*seen)[560] = NULL;
  size_t seen_count = 0, result = 0;
  bool want_body = strcmp(part, "body") == 0;
  for (size_t s = 0; s < body->part_count; s++) {
    const qa_app_component_body_part *source = &body->parts[s];
    if (!want_body && strcmp(source->part, "body") != 0 && strcmp(source->part, part) != 0) continue;
    /* Per-source occurrence ordinals, as in TS. */
    char (*keys)[512] = calloc(source->pass_count ? source->pass_count : 1, sizeof *keys);
    size_t *ordinals = calloc(source->pass_count ? source->pass_count : 1, sizeof *ordinals);
    size_t distinct = 0;
    for (size_t i = 0; i < source->pass_count; i++) {
      char key[512];
      material_key(&source->passes[i], key, sizeof key);
      size_t ordinal = 0;
      for (size_t j = 0; j < distinct; j++) if (strcmp(keys[j], key) == 0) { ordinal = ++ordinals[j]; break; }
      bool fresh_key = true;
      for (size_t j = 0; j < distinct; j++) if (strcmp(keys[j], key) == 0) fresh_key = false;
      if (fresh_key) { snprintf(keys[distinct], sizeof keys[distinct], "%s", key); ordinals[distinct] = 0; distinct++; ordinal = 0; }
      char occurrence[560];
      snprintf(occurrence, sizeof occurrence, "%s/%zu", key, ordinal);
      bool known = false;
      for (size_t j = 0; j < seen_count; j++) if (strcmp(seen[j], occurrence) == 0) known = true;
      if (!known) {
        seen = realloc(seen, (seen_count + 1) * sizeof *seen);
        snprintf(seen[seen_count++], sizeof seen[0], "%s", occurrence);
        if (result < capacity) out[result] = &source->passes[i];
        result++;
      }
    }
    free(keys);
    free(ordinals);
  }
  free(seen);
  return result;
}
bool qa_app_body_base_visible(const qa_app_component_body *bodies, size_t body_count, qa_actor_id actor, const char *part) {
  for (size_t i = 0; i < body_count; i++) {
    if (!qa_same_actor(bodies[i].actor, actor)) continue;
    if (bodies[i].part_count == 0) return false;
    for (size_t j = 0; j < bodies[i].part_count; j++) {
      const qa_app_component_body_part *source = &bodies[i].parts[j];
      if (strcmp(part, "body") != 0 && strcmp(source->part, "body") != 0 && strcmp(source->part, part) != 0) continue;
      if (!source->base) return false;
    }
  }
  return true;
}
