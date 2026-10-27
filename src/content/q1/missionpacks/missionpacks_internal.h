/* Slice-internal helpers for q1/missionpacks (+world): vector ops on
 * landed qa/core/math.h (TS owners: foundation/types.ts vadd/vscale/
 * vsub/normalize/length, foundation/entity.ts moveDirection via
 * qa_q1b_move_direction) and mirrored declarations for unlanded
 * cross-module functions (qa_q1mp_ext_*; link owners when landed):
 *  - aim/fire helpers: src/content/q1/foundation/weapons.ts
 *  - travel: src/content/q1/base/travel.ts (see travel.h)
 *  - obituary/rules: src/content/q1/base/rules.ts (see obituaries.h)
 *  - player/character: src/content/q1/base/player.ts (see presentation.h)
 *  - monsters runtime: src/content/q1/missionpacks/monsters/index.ts
 *    (landed with qa_q1mpm_* game mirror; adapted in runtime.h)
 * GPL-2.0-or-later. */
#ifndef QA_Q1MP_MISSIONPACKS_INTERNAL_H
#define QA_Q1MP_MISSIONPACKS_INTERNAL_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/types.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

#define QA_Q1MP_ZERO ((qa_vec3){0, 0, 0})
#define QA_Q1MP_V3(x, y, z) ((qa_vec3){(x), (y), (z)})
#define QA_Q1MP_NONE ((qa_q1mp_opt_actor){false, {{NULL, NULL}, 0, 0, NULL}})
#define QA_Q1MP_SOME(id) ((qa_q1mp_opt_actor){true, (id)})
#define QA_Q1MP_CH(name) ((qa_q1mp_channel){(name), 0})
#define QA_Q1MP_POINT ((qa_bounds){{0, 0, 0}, {0, 0, 0}})

static inline qa_vec3 qa_q1mp_vadd(qa_vec3 a, qa_vec3 b) { return qa_add3_double(a, b); }
static inline qa_vec3 qa_q1mp_vsub(qa_vec3 a, qa_vec3 b) { return qa_sub3_double(a, b); }
static inline qa_vec3 qa_q1mp_vscale(qa_vec3 a, double s) { return qa_scale3_double(a, s); }
static inline double qa_q1mp_dot(qa_vec3 a, qa_vec3 b) { return qa_dot3_double(a, b); }
static inline double qa_q1mp_length(qa_vec3 a) { return qa_length3_double(a); }
static inline qa_vec3 qa_q1mp_normalize(qa_vec3 a) {
  double m = qa_length3_double(a);
  return m == 0 ? QA_Q1MP_ZERO : qa_scale3_double(a, 1 / m);
}
static inline bool qa_q1mp_streq(const char *a, const char *b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}
static inline bool qa_q1mp_starts(const char *s, const char *prefix) {
  return s != NULL && prefix != NULL && strncmp(s, prefix, strlen(prefix)) == 0;
}
static inline double qa_q1mp_fround(double v) { return (double)(float)v; }
static inline double qa_q1mp_teamplay(qa_q1mp_game *game) {
  return game->base->options.has_teamplay ? game->base->options.teamplay : 0;
}
static inline double qa_q1mp_random(qa_q1mp_game *game) {
  return game->base->host->random(game->base->host->ctx);
}
static inline qa_q1b_basis qa_q1mp_vectors(qa_q1mp_game *game, qa_vec3 angles) {
  return game->base->make_vectors(game->base, angles);
}
/* TS entity.text/number/vector via landed q1b actor helpers. */
static inline const char *qa_q1mp_text(const qa_q1mp_actor *e, const char *k) { return qa_q1b_actor_text(e, k); }
static inline double qa_q1mp_number(const qa_q1mp_actor *e, const char *k) { return qa_q1b_actor_number(e, k); }
static inline qa_vec3 qa_q1mp_vector_of(const qa_q1mp_actor *e, const char *k) { return qa_q1b_actor_vector(e, k); }
static inline void qa_q1mp_set_text(qa_q1mp_actor *e, const char *k, const char *v) { qa_q1b_fields_set(e, k, v); }
/* TS game.sound defaults: channel from name, attenuation 1, volume 1. */
static inline void qa_q1mp_sound(qa_q1mp_game *g, qa_q1mp_actor *e, const char *p) { qa_q1b_game_sound(g->base, e, p); }
static inline void qa_q1mp_sound_owned_ch(qa_q1mp_game *g, qa_owned_actor a, const char *p, const char *ch) {
  qa_q1b_game_sound_owned_ch(g->base, a, p, QA_Q1MP_CH(ch));
}
static inline void qa_q1mp_sound_ch(qa_q1mp_game *g, qa_q1mp_actor *e, const char *p, const char *ch) {
  qa_q1b_game_sound_ch(g->base, e, p, QA_Q1MP_CH(ch));
}
/* TS String(number) for field writes. */
static inline void qa_q1mp_format(char *buf, size_t n, double v) { qa_q1b_format_number(buf, n, v); }

/* TS weaponItem(weapon) into caller buffer (landed q1b helper). */
static inline const char *qa_q1mp_weapon_item(const char *weapon, char *buf, size_t n) {
  return qa_q1b_weapon_item(weapon, buf, n);
}
qa_vec3 qa_q1mp_ext_aim(qa_q1mp_game *game, qa_owned_actor actor, qa_vec3 forward);
/* --- game wrapper helpers --- */
static inline double qa_q1mp_attack_delay(qa_q1mp_game *g, qa_q1mp_player_state *p, double d) {
  return g->weapon_attack_delay ? g->weapon_attack_delay(g->ctx, p, d) : d;
}
static inline double qa_q1mp_frame_delay(qa_q1mp_game *g, qa_q1mp_player_state *p, double d) {
  return g->weapon_frame_delay ? g->weapon_frame_delay(g->ctx, p, d) : d;
}
static inline bool qa_q1mp_consume(qa_q1mp_game *g, qa_owned_actor a, const char *item, double n) {
  if (g->inventory_consume_bool) return g->inventory_consume_bool(g->ctx, a, item, n);
  if (g->inventory_consume) { g->inventory_consume(g->ctx, a, item, n); return true; }
  return g->base->host->inventory_count(g->base->host->ctx, a.id, item) >= n;
}
static inline double qa_q1mp_give(qa_q1mp_game *g, qa_owned_actor a, const char *item, double n) {
  if (g->inventory_give) return g->inventory_give(g->ctx, a, item, n);
  g->base->host->inventory_give(g->base->host->ctx, a, item, n); return n;
}
static inline double qa_q1mp_count(qa_q1mp_game *g, qa_actor_id a, const char *item) {
  return g->base->host->inventory_count(g->base->host->ctx, a, item);
}
static inline void qa_q1mp_launch_behavior(qa_q1mp_game *g, qa_q1mp_actor *e, qa_actor_id o, const char *w, const char *k) {
  if (g->launch_projectile_behavior) g->launch_projectile_behavior(g->ctx, e, o, w, k);
}
static inline bool qa_q1mp_body_read(qa_q1mp_game *g, qa_actor_id a, qa_q1b_body *out) {
  return g->base->host->bodies_read(g->base->host->ctx, a, out);
}
static inline qa_vec3 qa_q1mp_aim(qa_q1mp_game *g, qa_owned_actor a, qa_vec3 f) {
  return g->aim ? g->aim(g->ctx, a, f) : qa_q1mp_ext_aim(g, a, f);
}
static inline bool qa_q1mp_can_damage_actor(qa_q1mp_game *g, qa_actor_id a) {
  qa_combat_state c; return g->base->host->combat_read(g->base->host->ctx, a, &c) && c.can_take_damage;
}
static inline void qa_q1mp_damage_w(qa_q1mp_game *g, qa_actor_id t, qa_actor_id inf, qa_q1mp_opt_actor atk, double n, const char *w) {
  (void)g->base->damage(g->base, t, inf, atk, n, w, NULL, NULL, NULL);
}
static inline void qa_q1mp_damage_full(qa_q1mp_game *g, qa_actor_id t, qa_actor_id inf, qa_q1mp_opt_actor atk, double n, const char *w, const char *del, const char *dt, const char *ae) {
  (void)g->base->damage(g->base, t, inf, atk, n, w, del, dt, ae);
}
static inline void qa_q1mp_effect_a(qa_q1mp_game *g, const char *e, qa_vec3 o, qa_q1mp_opt_actor a, double n) {
  g->base->effect(g->base, e, o, a, n);
}
static inline void qa_q1mp_set_model(qa_q1mp_actor *e, const char *m) { (void)snprintf(e->model, sizeof e->model, "%s", m ? m : ""); }
static inline void qa_q1mp_set_classname(qa_q1mp_actor *e, const char *m) { (void)snprintf(e->classname, sizeof e->classname, "%s", m ? m : ""); }
static inline void qa_q1mp_set_target(qa_q1mp_actor *e, const char *m) { (void)snprintf(e->target, sizeof e->target, "%s", m ? m : ""); }
/* TS game.find(target)[0] or NULL. */
static inline qa_q1mp_actor *qa_q1mp_find0(qa_q1mp_game *g, const char *t) {
  qa_q1mp_actor *out[1]; return g->base->find(g->base, t, out, 1) ? out[0] : NULL;
}
static inline qa_q1b_trace qa_q1mp_trace(qa_q1mp_game *g, qa_vec3 s, qa_vec3 e, qa_q1mp_opt_actor ig, bool mon, bool mis) {
  qa_q1b_trace_request r = {s, e, QA_Q1MP_POINT, ig, mon, mis};
  return g->base->host->trace(g->base->host->ctx, &r);
}
static inline void qa_q1mp_patch(qa_q1mp_game *g, qa_q1mp_actor *e, qa_q1b_body_patch *p) { g->base->set_body(g->base, e, p); }
static inline void qa_q1mp_set_velocity(qa_q1mp_game *g, qa_q1mp_actor *e, qa_vec3 v) {
  qa_q1b_body_patch p = {0}; p.has_velocity = true; p.velocity = v; qa_q1mp_patch(g, e, &p);
}
static inline void qa_q1mp_set_angles_b(qa_q1mp_game *g, qa_q1mp_actor *e, qa_vec3 v) {
  qa_q1b_body_patch p = {0}; p.has_angles = true; p.angles = v; qa_q1mp_patch(g, e, &p);
}
/* Wraps a qa_q1b_game* callback runtime back to the wrapper stored in ctx. */
#define QA_Q1MP_WRAP(ctx) ((qa_q1mp_game *)(ctx))
/* Mirrored aim() (TS owner: foundation/weapons.ts, unlanded). */
qa_vec3 qa_q1mp_ext_aim(qa_q1mp_game *game, qa_owned_actor actor, qa_vec3 forward);
/* Mirrored fireBullets() (same owner). */
void qa_q1mp_ext_fire_bullets(qa_q1mp_game *game, qa_owned_actor shooter, qa_vec3 direction,
                              qa_vec3 view_angles, double count, double spread_x, double spread_y, const char *weapon);

#endif
