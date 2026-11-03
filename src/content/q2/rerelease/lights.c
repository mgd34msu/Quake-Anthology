/* Port of src/content/q2/rerelease/lights.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/lights.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/core/numeric.h"
#include "rr_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

double qa_q2_rerelease_color(const char *value) {
  if (!value || !strchr(value, ' ')) return (double)qa_native_atoi(value ? value : "");
  double comp[4] = {0, 0, 0, 1};
  /* tokens = trim().split(/\s+/) */
  char buf[512]; snprintf(buf, sizeof buf, "%s", value);
  size_t idx = 0;
  char *save = NULL;
  for (char *tok = strtok_r(buf, " \t\r\n\f\v", &save); tok && idx < 4; tok = strtok_r(NULL, " \t\r\n\f\v", &save))
    comp[idx++] = qa_native_atof(tok);
  bool any = false; for (int i = 0; i < 4; i++) if (comp[i] > 1) any = true;
  double mult = any ? 1 : 255;
  /* channel(3) | channel(2) << 8 | channel(1) << 16 | channel(0) << 24: each channel
   * is any number (map text), so ToInt32 and the 32-bit shifts apply. */
  double ch0 = trunc(comp[0] * mult), ch1 = trunc(comp[1] * mult);
  double ch2 = trunc(comp[2] * mult), ch3 = trunc(comp[3] * mult);
  return qa_js_bit_or(qa_js_bit_or(qa_js_bit_or(ch3, qa_js_shl(ch2, 8)), qa_js_shl(ch1, 16)), qa_js_shl(ch0, 24));
}

bool qa_q2_rerelease_lights_get_active(qa_q2_rerelease_lights *l, qa_actor_id a, bool *out) {
  for (size_t i = 0; i < l->count; i++) if (qa_same_actor(l->active[i].actor, a)) { *out = l->active[i].active; return true; }
  return false;
}
bool qa_q2_rerelease_lights_set_active(qa_q2_rerelease_lights *l, qa_actor_id a, bool v) {
  for (size_t i = 0; i < l->count; i++) if (qa_same_actor(l->active[i].actor, a)) { l->active[i].active = v; return true; }
  if (l->count == l->cap) { size_t nc = l->cap ? l->cap * 2 : 8; void *q = realloc(l->active, nc * sizeof *l->active); if (!q) return false; l->active = q; l->cap = nc; }
  l->active[l->count].actor = a; l->active[l->count].active = v; l->count++;
  return true;
}
void qa_q2_rerelease_lights_delete(qa_q2_rerelease_lights *l, qa_actor_id a) {
  for (size_t i = 0; i < l->count; i++) if (qa_same_actor(l->active[i].actor, a)) {
    memmove(&l->active[i], &l->active[i + 1], (l->count - i - 1) * sizeof *l->active); l->count--; return;
  }
}
void qa_q2_rerelease_lights_clear(qa_q2_rerelease_lights *l) { l->count = 0; }

static void show_light(qa_q2_rerelease_lights *l, qa_q2_entity *e, qa_q2_game_services *g) {
  e->visible = rr_band(e->server_flags, 1) == 0;
  uint32_t skin = qa_js_to_uint32(e->skin); /* TS skin >>> n */
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "dynamic-light"; ev.has_actor = true; ev.actor = e->actor.id;
  ev.origin = rr_body_of(g, e).origin; ev.radius = e->frame;
  ev.light_color = rr_v3((double)(qa_js_to_int32((skin >> 24) & 255)) / 255, (double)(qa_js_to_int32((skin >> 16) & 255)) / 255, (double)(qa_js_to_int32((skin >> 8) & 255)) / 255);
  ev.visible = e->visible;
  rr_emit_rr(&l->hooks, ev);
}
static void light_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a);
static void light_flicker(void *c, qa_q2_entity *e, qa_q2_game_services *g);
static void light_think(void *c, qa_q2_entity *e, qa_q2_game_services *g);
static double style_code_at(const char *style, size_t length, double index);
static void light_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a; qa_q2_rerelease_lights *l = c;
  bool cur = false; qa_q2_rerelease_lights_get_active(l, e->actor.id, &cur);
  bool active = !cur; qa_q2_rerelease_lights_set_active(l, e->actor.id, active);
  if (active) e->server_flags = rr_bnot_and(e->server_flags, 1); else e->server_flags = rr_bor(e->server_flags, 1);
  show_light(l, e, g);
  if (!active) { g->cancel(g->ctx, e); return; }
  if (e->has_chain) { g->schedule(g->ctx, e, 0.1, l->think); return; }
  if (rr_band(e->spawnflags, 4) != 0) g->schedule(g->ctx, e, 0.1, l->flicker);
}
static void light_flicker(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_lights *l = c;
  if (g->host->random(g->host->ctx) < 0.5) e->server_flags = rr_bxor(e->server_flags, 1);
  show_light(l, e, g); g->schedule(g->ctx, e, 0.1, l->flicker);
}
static void light_think(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_lights *l = c;
  if (rr_band(e->spawnflags, 4) != 0 && g->host->random(g->host->ctx) < 0.5) e->server_flags = rr_bxor(e->server_flags, 1);
  const char *style = l->hooks.light_style(l->hooks.ctx, e->style);
  if (!style || !style[0]) style = "m";
  qa_q2_entity *target = e->has_chain ? rr_ent(g, &e->chain) : NULL;
  if (!target) qa_q2rr_error("target_light lost its source color target");
  e->delay += e->speed;
  size_t slen = strlen(style);
  /* Math.trunc(delay) % length and charCodeAt: a negative index (negative speed) reads NaN
   * and a NaN index reads character 0, where a C long cast would be undefined. */
  double index = fmod(trunc(e->delay), (double)slen);
  double current = (style_code_at(style, slen, index) - 97) / 25.0;
  double fraction = fmod(e->delay, 1);
  double next = (style_code_at(style, slen, fmod(index + 1, (double)slen)) - 97) / 25.0;
  double lerp = rr_band(e->spawnflags, 2) != 0 ? current : next * fraction + current * (1 - fraction);
  uint32_t tskin = qa_js_to_uint32(target->skin), cskin = qa_js_to_uint32(e->count); /* TS >>> */
  double ch8 = trunc((double)(qa_js_to_int32((tskin >> 8) & 255)) * lerp + (double)(qa_js_to_int32((cskin >> 8) & 255)) * (1 - lerp));
  double ch16 = trunc((double)(qa_js_to_int32((tskin >> 16) & 255)) * lerp + (double)(qa_js_to_int32((cskin >> 16) & 255)) * (1 - lerp));
  double ch24 = trunc((double)(qa_js_to_int32((tskin >> 24) & 255)) * lerp + (double)(qa_js_to_int32((cskin >> 24) & 255)) * (1 - lerp));
  e->skin = qa_js_bit_or(qa_js_bit_or(qa_js_shl(ch8, 8), qa_js_shl(ch16, 16)), qa_js_shl(ch24, 24)); /* JS << and | on int32 */
  show_light(l, e, g); g->schedule(g->ctx, e, 0.1, l->think);
}
/* String.prototype.charCodeAt over an ASCII style string. */
static double style_code_at(const char *style, size_t length, double index) {
  double at = isnan(index) ? 0 : trunc(index);
  return at >= 0 && at < (double)length ? (double)(unsigned char)style[(size_t)at] : NAN;
}
void qa_q2_rerelease_lights_init(qa_q2_rerelease_lights *l, qa_q2_rerelease_hooks hooks) {
  memset(l, 0, sizeof *l); l->hooks = hooks;
  l->use = (qa_q2_use){l, light_use}; l->think = (qa_q2_think){l, light_think}; l->flicker = (qa_q2_think){l, light_flicker};
}
void qa_q2_rerelease_lights_destroy(qa_q2_rerelease_lights *l) { free(l->active); memset(l, 0, sizeof *l); }
bool qa_q2_rerelease_lights_spawn(qa_q2_rerelease_lights *l, qa_q2_entity *e, qa_q2_game_services *g) {
  if (!e->classname || strcmp(e->classname, "target_light") != 0) return false;
  e->visible = false; e->server_flags = rr_bor(e->server_flags, 1);
  e->frame = qa_q2_number_field(&e->spawn, "radius", 0) != 0 ? qa_q2_number_field(&e->spawn, "radius", 0) : 150;
  e->count = e->skin;
  qa_q2_rerelease_lights_set_active(l, e->actor.id, false);
  if (e->target && e->target[0]) {
    qa_q2_entity *t = g->pick_target(g->ctx, e->target);
    e->has_chain = t != NULL; if (t) e->chain = t->actor.id;
  }
  e->use = l->use;
  if (rr_band(e->spawnflags, 1) != 0) light_use(l, e, g, &e->actor.id, &e->actor.id);
  e->speed = e->speed == 0 ? 1 : 0.1 / e->speed;
  if (qa_q2_is_n64(&g->options)) e->style += 10;
  g->link(g->ctx, e); show_light(l, e, g);
  return true;
}
static qa_q2_named_think lt_thinks[2]; static qa_q2_named_use lt_uses[1];
qa_q2_callback_definitions qa_q2_rerelease_lights_callbacks(qa_q2_rerelease_lights *l) {
  lt_thinks[0] = (qa_q2_named_think){"rr.target_light_think", l->think};
  lt_thinks[1] = (qa_q2_named_think){"rr.target_light_flicker_think", l->flicker};
  lt_uses[0] = (qa_q2_named_use){"rr.target_light_use", l->use};
  qa_q2_callback_definitions d = {0}; d.think = lt_thinks; d.think_count = 2; d.use = lt_uses; d.use_count = 1;
  return d;
}
static bool lights_spawn_cb(void *c, qa_q2_entity *e, qa_q2_game_services *g) { return qa_q2_rerelease_lights_spawn(c, e, g); }
qa_q2_spawn_module qa_q2_rerelease_lights_as_spawn(qa_q2_rerelease_lights *l) {
  qa_q2_spawn_module m = {0}; m.ctx = l; m.spawn = lights_spawn_cb; return m;
}
