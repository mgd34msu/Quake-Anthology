/* Port of src/content/q2/foundation/shadow-lights.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/shadow_lights.h"

#include <math.h>
#include <string.h>

#include "qa/content/q2/foundation/fields.h"
#include "qa/core/math.h"

void qa_q2_dynamic_light_use_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)o; (void)a;
  e->server_flags = (double)((int64_t)e->server_flags ^ 1);
  e->visible = ((int64_t)e->server_flags & 1) == 0;
  qa_q2_emit_shadow_light(e, g);
}
const qa_q2_use QA_Q2_DYNAMIC_LIGHT_USE = {NULL, qa_q2_dynamic_light_use_fn};

void qa_q2_spawn_shadow_light(qa_q2_entity *e, qa_q2_game_services *g) {
  if (qa_q2_number_field(&e->spawn, "shadowlightradius", 0) > 0) {
    e->render_flags = 1 << 14;
    qa_body_state body = qa_q2_game_body(g, e);
    body.bounds = (qa_bounds){QA_Q2_ZERO, QA_Q2_ZERO};
    qa_shared_body_table_write(g->host->bodies, e->actor, body);
    g->link(g->ctx, e);
  }
  if (e->targetname[0] != '\0') e->use = QA_Q2_DYNAMIC_LIGHT_USE;
  if (((int64_t)e->spawnflags & 1) != 0) e->server_flags = (double)((int64_t)e->server_flags ^ 1);
  e->visible = ((int64_t)e->server_flags & 1) == 0;
}

void qa_q2_emit_shadow_light(qa_q2_entity *e, qa_q2_game_services *g) {
  size_t n = 0;
  qa_q2_entity *const *entities = g->entities(g->ctx, &n);
  qa_q2_entity *target = NULL, *style_ent = NULL;
  if (e->target[0]) {
    for (size_t i = 0; i < n; i++) if (strcmp(entities[i]->targetname, e->target) == 0) { target = entities[i]; break; }
  }
  const char *style_target = qa_q2_spawn_get(&e->spawn, "shadowlightstyletarget");
  if (style_target) {
    for (size_t i = 0; i < n; i++) if (strcmp(entities[i]->targetname, style_target) == 0) { style_ent = entities[i]; break; }
  }
  qa_vec3 origin = qa_q2_game_body(g, e).origin;
  uint32_t packed = (uint32_t)e->skin;
  qa_q2_presentation_event ev = {0};
  ev.kind = "dynamic-light";
  ev.has_actor = true; ev.actor = e->actor.id; ev.origin = origin;
  ev.light_color = packed == 0 ? (qa_vec3){1, 1, 1}
      : (qa_vec3){((packed >> 24) & 255) / 255.0, ((packed >> 16) & 255) / 255.0, ((packed >> 8) & 255) / 255.0};
  ev.visible = ((int64_t)e->server_flags & 1) == 0;
  ev.radius = qa_q2_number_field(&e->spawn, "shadowlightradius", 0);
  ev.intensity = qa_q2_number_field(&e->spawn, "shadowlightintensity", 1);
  ev.resolution = qa_q2_integer_field(&e->spawn, "shadowlightresolution", 0);
  ev.fade_start = qa_q2_number_field(&e->spawn, "shadowlightstartfadedistance", 0);
  ev.fade_end = qa_q2_number_field(&e->spawn, "shadowlightendfadedistance", 0);
  ev.lightstyle = style_ent ? style_ent->style : qa_q2_integer_field(&e->spawn, "shadowlightstyle", -1);
  if (target) {
    ev.has_cone = true;
    ev.cone_direction = qa_normalize3_or_zero(qa_sub3(qa_q2_game_body(g, target).origin, origin));
    ev.cone_cos_half_angle = cos(qa_q2_number_field(&e->spawn, "shadowlightconeangle", 45) * 3.141592653589793 / 180);
  }
  g->host->emit(g->host->ctx, ev);
}

void qa_q2_emit_shadow_lights(qa_q2_game_services *g) {
  size_t n = 0;
  qa_q2_entity *const *entities = g->entities(g->ctx, &n);
  for (size_t i = 0; i < n; i++)
    if (strcmp(entities[i]->classname, "dynamic_light") == 0) qa_q2_emit_shadow_light(entities[i], g);
}
