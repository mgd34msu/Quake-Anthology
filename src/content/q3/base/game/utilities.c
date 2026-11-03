/* Port of src/content/q3/base/game/utilities.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/utilities.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
void qa_q3_temporary_vector_set(qa_q3_temporary_vector *v, double x, double y, double z) { v->x = (float)x; v->y = (float)y; v->z = (float)z; }
void qa_q3_game_utility_scratch_init(qa_q3_game_utility_scratch *s, void *ctx, void (*print)(void *, const char *)) {
  memset(s, 0, sizeof *s); s->print_ctx = ctx; s->print = print;
}
qa_q3_temporary_vector *qa_q3_game_utility_scratch_tv(qa_q3_game_utility_scratch *s, double x, double y, double z) {
  qa_q3_temporary_vector *v = &s->vectors[s->vector_index]; s->vector_index = (s->vector_index + 1) & 7;
  qa_q3_temporary_vector_set(v, x, y, z); return v;
}
uint8_t *qa_q3_game_utility_scratch_vtos(qa_q3_game_utility_scratch *s, qa_vec3 vector) {
  uint8_t *str = s->string_bytes + s->string_index * 32; s->string_index = (s->string_index + 1) & 7;
  qa_q3_game_format_arg a[3] = {qa_q3_game_format_number(qa_qvm_float_to_int(vector.x)), qa_q3_game_format_number(qa_qvm_float_to_int(vector.y)), qa_q3_game_format_number(qa_qvm_float_to_int(vector.z))};
  char *value = qa_q3_game_format("(%i %i %i)", a, 3, 32000); if (!value) qa_range_error("out of memory");
  size_t len = strlen(value);
  if (len >= 32 && s->print) { qa_q3_game_format_arg pa[2] = {qa_q3_game_format_number((double)len), qa_q3_game_format_number(32)}; char *m = qa_q3_game_format("Com_sprintf: overflow of %i in %i\n", pa, 2, 32000); if (m) { s->print(s->print_ctx, m); free(m); } }
  size_t keep = len < 31 ? len : 31; memcpy(str, value, keep); str[keep] = 0; free(value); return str;
}
double qa_q3_debug_line(qa_vec3 start, qa_vec3 end, double color, qa_q3_base_bot_debug_polygons polygons) {
  qa_vec3 direction = qa_normalize3(qa_sub3(end, start)), up = {0, 0, 1};
  double dot = qa_dot3(direction, up);
  qa_vec3 raw = (dot > (float)0.99 || dot < (float)-0.99) ? (qa_vec3){1, 0, 0} : qa_cross3(direction, up);
  qa_vec3 cross = qa_normalize3(raw);
  qa_vec3 points[4] = {qa_add3(start, qa_scale3(cross, 2)), qa_add3(start, qa_scale3(cross, -2)), qa_add3(end, qa_scale3(cross, -2)), qa_add3(end, qa_scale3(cross, 2))};
  return polygons.create(polygons.ctx, color, 4, points, 4);
}
static const char *byte_string(const char *v) { return v; }
void qa_q3_config_string_registry_init(qa_q3_config_string_registry *r, qa_q3_config_string_store store) { r->store = store; }
double qa_q3_config_string_registry_find(qa_q3_config_string_registry *r, const char *name, double start, double maximum, bool create) {
  if (!name) return 0;
  const char *text = byte_string(name); if (!text[0]) return 0;
  if (start != trunc(start) || maximum != trunc(maximum) || start < 0 || maximum < 1 || start + maximum > 1024)
    qa_range_error("Configstring range must fit MAX_CONFIGSTRINGS");
  int index = 1;
  for (; index < maximum; index++) {
    const char *value = r->store.get(r->store.ctx, start + index); if (!value || !value[0]) break;
    char buf[1024]; size_t n = strlen(value) < 1023 ? strlen(value) : 1023; memcpy(buf, value, n); buf[n] = 0;
    if (strcmp(buf, text) == 0) return index;
  }
  if (!create) return 0;
  if (index == maximum) qa_range_error("G_FindConfigstringIndex: overflow");
  r->store.set(r->store.ctx, start + index, text); return index;
}
double qa_q3_config_string_registry_model_index(qa_q3_config_string_registry *r, const char *n) { return qa_q3_config_string_registry_find(r, n, 32, 256, true); }
double qa_q3_config_string_registry_sound_index(qa_q3_config_string_registry *r, const char *n) { return qa_q3_config_string_registry_find(r, n, 288, 256, true); }
const char *qa_q3_entity_string_field_get(qa_q3_game_entity *e, qa_q3_entity_string_field f) {
  switch (f) { case QA_Q3_FIELD_CLASSNAME: return qa_q3_game_entity_classname(e); case QA_Q3_FIELD_MODEL: return e->model;
    case QA_Q3_FIELD_MODEL2: return e->model2; case QA_Q3_FIELD_MESSAGE: return e->message; case QA_Q3_FIELD_TARGET: return e->target;
    case QA_Q3_FIELD_TARGETNAME: return e->targetname; case QA_Q3_FIELD_TEAM: return e->team;
    case QA_Q3_FIELD_TARGET_SHADER_NAME: return e->target_shader_name; default: return e->target_shader_new_name; }
}
static int fold_equal(const char *a, const char *b) {
  while (*a && *b) { int ca = (unsigned char)*a, cb = (unsigned char)*b; if (ca >= 'A' && ca <= 'Z') ca += 32; if (cb >= 'A' && cb <= 'Z') cb += 32; if (ca != cb) return 0; a++; b++; }
  return *a == *b;
}
qa_q3_game_entity *qa_q3_find_entity(qa_q3_entity_pool *pool, qa_q3_game_entity *after, qa_q3_entity_string_field field, const char *match) {
  if (!match) return NULL;
  for (int i = after ? qa_js_to_int32(after->slot) + 1 : 0; i < pool->num_entities; i++) {
    qa_q3_game_entity *e = qa_q3_entity_pool_at(pool, i); const char *v = qa_q3_entity_string_field_get(e, field);
    if (qa_q3_game_entity_inuse(e) && v && fold_equal(v, match)) return e;
  }
  return NULL;
}
qa_q3_game_entity *qa_q3_pick_target(qa_q3_target_selection_context ctx, const char *target_name) {
  if (!target_name) { ctx.warn(ctx.ctx, "G_PickTarget called with NULL targetname\n"); return NULL; }
  qa_q3_game_entity *choices[32]; size_t count = 0; qa_q3_game_entity *found = NULL;
  while (count < 32) { found = qa_q3_find_entity(ctx.pool, found, QA_Q3_FIELD_TARGETNAME, target_name); if (!found) break; choices[count++] = found; }
  if (count == 0) { char buf[1024]; snprintf(buf, sizeof buf, "G_PickTarget: target %s not found\n", target_name); ctx.warn(ctx.ctx, buf); return NULL; }
  double random = ctx.random_int(ctx.ctx);
  if (random != trunc(random) || random < 0) qa_range_error("Game rand must return a nonnegative integer");
  return choices[(size_t)((int64_t)random % (int64_t)count)];
}
void qa_q3_use_targets(qa_q3_target_use_context ctx, qa_q3_game_entity *entity, const qa_q3_use_participant *activator) {
  if (!entity) return;
  if (entity->target_shader_name && entity->target_shader_new_name) {
    double secs = (float)((float)ctx.time * (float)0.001); ctx.remap_shader(ctx.ctx, entity->target_shader_name, entity->target_shader_new_name, secs);
  }
  if (!entity->target) return;
  qa_q3_game_entity *target = NULL;
  while ((target = qa_q3_find_entity(ctx.pool, target, QA_Q3_FIELD_TARGETNAME, entity->target)) != NULL) {
    if (target == entity) ctx.warn(ctx.ctx, "WARNING: Entity used itself.\n");
    else if (target->use.fn) { qa_q3_use_participant self_p = qa_q3_participant_entity(entity); qa_q3_entity_use_fn fn; memcpy(&fn, &target->use.fn, sizeof fn); fn(target->use.ctx, target, &self_p, activator); }
    if (!qa_q3_game_entity_inuse(entity)) { ctx.warn(ctx.ctx, "entity was removed while using targets\n"); return; }
  }
}
void qa_q3_team_command(qa_q3_entity_pool *pool, double team, const char *command, qa_q3_team_command_send_fn send, void *send_ctx) {
  for (int i = 0; i < pool->max_clients; i++) {
    qa_q3_game_client *c = qa_q3_entity_pool_client_at(pool, i);
    if (c->pers.connected == QA_Q3_CONN_CONNECTED && c->sess.session_team == team) send(send_ctx, i, command);
  }
}
qa_q3_move_direction_result qa_q3_move_direction(qa_vec3 angles) {
  int vertical = angles.x == 0 && angles.z == 0; qa_q3_move_direction_result r;
  if (vertical && angles.y == -1) r.direction = (qa_vec3){0, 0, 1};
  else if (vertical && angles.y == -2) r.direction = (qa_vec3){0, 0, -1};
  else r.direction = qa_qvm_angle_vectors(angles).forward;
  r.angles = (qa_vec3){0, 0, 0}; return r;
}
