/* Port of src/content/q2/foundation/host.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static char *dupstr(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}

const char *qa_q2_spawn_get(const qa_q2_spawn_fields *f, const char *key) {
  for (size_t i = 0; i < f->value_count; i++)
    if (strcmp(f->values[i].key, key) == 0) return f->values[i].value;
  return NULL;
}
bool qa_q2_spawn_has(const qa_q2_spawn_fields *f, const char *key) { return qa_q2_spawn_get(f, key) != NULL; }

bool qa_q2_spawn_set(qa_q2_spawn_fields *f, const char *key, const char *value) {
  for (size_t i = 0; i < f->value_count; i++) {
    if (strcmp(f->values[i].key, key) == 0) {
      char *v = dupstr(value);
      if (!v) return false;
      free(f->values[i].value);
      f->values[i].value = v;
      if (strcmp(key, "classname") == 0) f->classname = v;
      return true;
    }
  }
  if (f->value_count == f->value_capacity) {
    size_t cap = f->value_capacity ? f->value_capacity * 2 : 8;
    qa_q2_field_pair *next = realloc(f->values, cap * sizeof *next);
    if (!next) return false;
    f->values = next;
    f->value_capacity = cap;
  }
  char *k = dupstr(key), *v = dupstr(value);
  if (!k || !v) { free(k); free(v); return false; }
  f->values[f->value_count].key = k;
  f->values[f->value_count].value = v;
  f->value_count++;
  if (strcmp(key, "classname") == 0) f->classname = v;
  return true;
}

void qa_q2_spawn_fields_free(qa_q2_spawn_fields *f) {
  for (size_t i = 0; i < f->value_count; i++) { free(f->values[i].key); free(f->values[i].value); }
  free(f->values);
  *f = (qa_q2_spawn_fields){0};
}

bool qa_q2_spawn_fields_clone(qa_q2_spawn_fields *out, const qa_q2_spawn_fields *in) {
  *out = (qa_q2_spawn_fields){.ordinal = in->ordinal, .classname = ""};
  for (size_t i = 0; i < in->value_count; i++)
    if (!qa_q2_spawn_set(out, in->values[i].key, in->values[i].value)) { qa_q2_spawn_fields_free(out); return false; }
  const char *cn = qa_q2_spawn_get(out, "classname");
  /* A created entity has no classname value; its name is interned. */
  out->classname = cn ? cn : (in->classname ? qa_q2_intern(in->classname) : "");
  return true;
}

bool qa_q2_think_eq(qa_q2_think a, qa_q2_think b) { return a.fn == b.fn && a.ctx == b.ctx; }
bool qa_q2_use_eq(qa_q2_use a, qa_q2_use b) { return a.fn == b.fn && a.ctx == b.ctx; }
bool qa_q2_touch_eq(qa_q2_touch a, qa_q2_touch b) { return a.fn == b.fn && a.ctx == b.ctx; }
bool qa_q2_pain_eq(qa_q2_pain a, qa_q2_pain b) { return a.fn == b.fn && a.ctx == b.ctx; }
bool qa_q2_die_eq(qa_q2_die a, qa_q2_die b) { return a.fn == b.fn && a.ctx == b.ctx; }
bool qa_q2_blocked_eq(qa_q2_blocked a, qa_q2_blocked b) { return a.fn == b.fn && a.ctx == b.ctx; }

static const char *get_or(const qa_q2_spawn_fields *f, const char *k) {
  const char *v = qa_q2_spawn_get(f, k);
  return v ? v : "";
}

void qa_q2_entity_init(qa_q2_entity *e, qa_owned_actor actor, qa_q2_spawn_fields spawn) {
  *e = (qa_q2_entity){0};
  e->actor = actor;
  e->spawn = spawn;
  e->model2 = ""; e->model3 = ""; e->model4 = "";
  e->old_frame = -1; e->scale = 1; e->alpha = 1; e->light_level = 128;
  e->noise = ""; e->sound = ""; e->map = "";
  e->clip_mask = 0x6000003;
  e->visible = true;
  e->solid = "none"; e->motion = "stationary";
  e->gravity = 1;
  e->gravity_vector = (qa_vec3){0, 0, -1};
  e->classname = e->spawn.classname ? e->spawn.classname : "";
  e->target = get_or(&e->spawn, "target");
  e->targetname = get_or(&e->spawn, "targetname");
  e->killtarget = get_or(&e->spawn, "killtarget");
  e->combat_target = get_or(&e->spawn, "combattarget");
  e->death_target = get_or(&e->spawn, "deathtarget");
  e->health_target = get_or(&e->spawn, "healthtarget");
  e->item_target = get_or(&e->spawn, "itemtarget");
  e->message = get_or(&e->spawn, "message");
  e->model = get_or(&e->spawn, "model");
  e->spawnflags = 0;
}

void qa_q2_entity_destroy(qa_q2_entity *e) { qa_q2_spawn_fields_free(&e->spawn); }

bool qa_q2_entity_flare(const qa_q2_entity *e, qa_scene_flare *out, qa_vec3 *rim) {
  int32_t rf = qa_js_to_int32(e->render_flags); /* TS renderFlags & x: ToInt32 */
  if ((rf & 0x200000) == 0) return false;
  int32_t shell = rf & 0x1c00;
  const char *image = "misc/flare.tga";
  if ((rf & 256) != 0) {
    const char *v = qa_q2_spawn_get(&e->spawn, "image");
    if (v && v[0]) image = v;
  }
  uint32_t skin = qa_js_to_uint32(e->skin); /* TS skin >>> n: a negative skin is UB as a C cast */
  out->image = image;
  out->fade_start = trunc(qa_q2_number_field(&e->spawn, "fade_start_dist", 96));
  out->fade_end = trunc(qa_q2_number_field(&e->spawn, "fade_end_dist", 384));
  out->scale = e->scale != 0 ? e->scale : 1;
  out->lock_angle = (rf & 1) != 0;
  out->color = e->skin == 0 ? (qa_vec3){255, 255, 255}
      : (qa_vec3){(double)(skin >> 24), (double)(qa_js_to_int32((skin >> 16) & 255)), (double)(qa_js_to_int32((skin >> 8) & 255))};
  if (shell == 0) out->rim_color = NULL;
  else {
    *rim = (qa_vec3){shell & 0x400 ? 255 : 0, shell & 0x800 ? 255 : 0, shell & 0x1000 ? 255 : 0};
    out->rim_color = rim;
  }
  return true;
}

qa_authored_target qa_q2_entity_authored_target(qa_q2_entity *e) {
  /* Borrowed view: char* fields point at the entity's borrowed strings. */
  return (qa_authored_target){.actor = e->actor, .classname = e->classname,
      .targetname = (char *)e->targetname, .target = (char *)e->target,
      .killtarget = (char *)e->killtarget, .message = (char *)e->message, .delay = e->delay};
}

qa_body_state qa_q2_game_body(qa_q2_game_services *g, qa_q2_entity *e) { return g->body(g->ctx, e->actor); }
void qa_q2_game_move(qa_q2_game_services *g, qa_q2_entity *e, qa_q2_body_changes c, bool link) { g->move(g->ctx, e, c, link); }
qa_q2_entity *qa_q2_game_entity_of(qa_q2_game_services *g, const qa_actor_id *a) { return g->entity(g->ctx, a); }
void qa_q2_game_use_targets_entity(qa_q2_game_services *g, qa_q2_entity *e, const qa_actor_id *a, bool ignore) {
  g->use_targets(g->ctx, qa_q2_entity_authored_target(e), a, ignore);
}
void qa_q2_game_mark_dodgeable(qa_q2_game_services *g, qa_q2_entity *e) {
  if (g->mark_dodgeable != NULL) g->mark_dodgeable(g->ctx, e);
}
