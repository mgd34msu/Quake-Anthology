/* Port of src/content/q2/rerelease/q64/index.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/q64/index.h"
#include "qa/network/common/js_number.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/rerelease/players.h"
#include "qa/content/q2/rerelease/view.h"
#include "qa/core/math.h"
#include "../rr_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

static double anglemod_q(double angle) {
  long q = qa_js_to_int32(trunc(angle * 65536 / 360)) & 65535;
  return (double)q * 360 / 65536;
}
static double turn_q(double current, double ideal, double speed) {
  current = anglemod_q(current);
  double move = ideal - current;
  if (ideal > current && move >= 180) move -= 360;
  else if (ideal <= current && move <= -180) move += 360;
  double clamped = move < -speed ? -speed : move > speed ? speed : move;
  return anglemod_q(current + clamped);
}
static qa_q2rr_eye_state *eye_get(qa_q2_rerelease_q64 *q, qa_actor_id a) {
  for (size_t i = 0; i < q->eye_count; i++) if (qa_same_actor(q->eyes[i].actor, a)) return &q->eyes[i].state;
  return NULL;
}
static bool eye_set(qa_q2_rerelease_q64 *q, qa_actor_id a, qa_q2rr_eye_state s) {
  qa_q2rr_eye_state *p = eye_get(q, a); if (p) { *p = s; return true; }
  if (q->eye_count == q->eye_cap) { size_t nc = q->eye_cap ? q->eye_cap * 2 : 8; void *r = realloc(q->eyes, nc * sizeof *q->eyes); if (!r) return false; q->eyes = r; q->eye_cap = nc; }
  q->eyes[q->eye_count].actor = a; q->eyes[q->eye_count].state = s; q->eye_count++; return true;
}
static qa_q2rr_camera_state *cam_get(qa_q2_rerelease_q64 *q, qa_actor_id a) {
  for (size_t i = 0; i < q->cam_count; i++) if (qa_same_actor(q->cameras[i].actor, a)) return &q->cameras[i].state;
  return NULL;
}
static bool cam_set(qa_q2_rerelease_q64 *q, qa_actor_id a, qa_q2rr_camera_state s) {
  qa_q2rr_camera_state *p = cam_get(q, a); if (p) { *p = s; return true; }
  if (q->cam_count == q->cam_cap) { size_t nc = q->cam_cap ? q->cam_cap * 2 : 8; void *r = realloc(q->cameras, nc * sizeof *q->cameras); if (!r) return false; q->cameras = r; q->cam_cap = nc; }
  q->cameras[q->cam_count].actor = a; q->cameras[q->cam_count].state = s; q->cam_count++; return true;
}
static qa_q2rr_dummy_state *dum_get(qa_q2_rerelease_q64 *q, qa_actor_id a) {
  for (size_t i = 0; i < q->dum_count; i++) if (qa_same_actor(q->dummies[i].actor, a)) return &q->dummies[i].state;
  return NULL;
}
static bool dum_set(qa_q2_rerelease_q64 *q, qa_actor_id a, qa_q2rr_dummy_state s) {
  qa_q2rr_dummy_state *p = dum_get(q, a); if (p) { *p = s; return true; }
  if (q->dum_count == q->dum_cap) { size_t nc = q->dum_cap ? q->dum_cap * 2 : 8; void *r = realloc(q->dummies, nc * sizeof *q->dummies); if (!r) return false; q->dummies = r; q->dum_cap = nc; }
  q->dummies[q->dum_count].actor = a; q->dummies[q->dum_count].state = s; q->dum_count++; return true;
}
static void map_del_eye(qa_q2_rerelease_q64 *q, qa_actor_id a) {
  for (size_t i = 0; i < q->eye_count; i++) if (qa_same_actor(q->eyes[i].actor, a)) { memmove(&q->eyes[i], &q->eyes[i+1], (q->eye_count-i-1)*sizeof *q->eyes); q->eye_count--; return; }
}
static void map_del_cam(qa_q2_rerelease_q64 *q, qa_actor_id a) {
  for (size_t i = 0; i < q->cam_count; i++) if (qa_same_actor(q->cameras[i].actor, a)) { memmove(&q->cameras[i], &q->cameras[i+1], (q->cam_count-i-1)*sizeof *q->cameras); q->cam_count--; return; }
}
static void map_del_dum(qa_q2_rerelease_q64 *q, qa_actor_id a) {
  for (size_t i = 0; i < q->dum_count; i++) if (qa_same_actor(q->dummies[i].actor, a)) { memmove(&q->dummies[i], &q->dummies[i+1], (q->dum_count-i-1)*sizeof *q->dummies); q->dum_count--; return; }
}

void qa_q2_rerelease_q64_release(qa_q2_rerelease_q64 *q, qa_actor_id a) { map_del_eye(q, a); map_del_cam(q, a); map_del_dum(q, a); }
qa_q2_rerelease_q64_checkpoint qa_q2_rerelease_q64_capture(qa_q2_rerelease_q64 *q) {
  free(q->cp_eyes); free(q->cp_cams); free(q->cp_dums);
  q->cp_eyes = q->eye_count ? malloc(q->eye_count * sizeof *q->cp_eyes) : NULL;
  q->cp_cams = q->cam_count ? malloc(q->cam_count * sizeof *q->cp_cams) : NULL;
  q->cp_dums = q->dum_count ? malloc(q->dum_count * sizeof *q->cp_dums) : NULL;
  for (size_t i = 0; i < q->eye_count; i++) { q->cp_eyes[i].actor = (qa_saved_actor_id){(double)q->eyes[i].actor.slot, (double)q->eyes[i].actor.generation}; q->cp_eyes[i].state = q->eyes[i].state; }
  for (size_t i = 0; i < q->cam_count; i++) { q->cp_cams[i].actor = (qa_saved_actor_id){(double)q->cameras[i].actor.slot, (double)q->cameras[i].actor.generation}; q->cp_cams[i].state = q->cameras[i].state; }
  for (size_t i = 0; i < q->dum_count; i++) { q->cp_dums[i].actor = (qa_saved_actor_id){(double)q->dummies[i].actor.slot, (double)q->dummies[i].actor.generation}; q->cp_dums[i].state = q->dummies[i].state; }
  qa_q2_rerelease_q64_checkpoint cp = {q->cp_eyes, q->eye_count, q->cp_cams, q->cam_count, q->cp_dums, q->dum_count};
  return cp;
}
void qa_q2_rerelease_q64_restore(qa_q2_rerelease_q64 *q, qa_q2_game_services *g, qa_q2_rerelease_q64_checkpoint cp) {
  q->eye_count = 0; q->cam_count = 0; q->dum_count = 0;
  for (size_t i = 0; i < cp.eye_count; i++) eye_set(q, qa_q2_restore_actor(g, cp.eyes[i].actor).id, cp.eyes[i].state);
  for (size_t i = 0; i < cp.camera_count; i++) cam_set(q, qa_q2_restore_actor(g, cp.cameras[i].actor).id, cp.cameras[i].state);
  for (size_t i = 0; i < cp.dummy_count; i++) dum_set(q, qa_q2_restore_actor(g, cp.dummies[i].actor).id, cp.dummies[i].state);
}

static void spinning_think(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_q64 *q = c;
  if (e->timestamp <= rr_now(g)) {
    double left = rr_now(g) + 1;
    double right = g->host->random(g->host->ctx) * 5;
    e->timestamp = left + right;
    double x = e->decel + g->host->random(g->host->ctx) * (e->speed - e->decel);
    double y = e->decel + g->host->random(g->host->ctx) * (e->speed - e->decel);
    double z = e->decel + g->host->random(g->host->ctx) * (e->speed - e->decel);
    double x2 = g->host->random(g->host->ctx) < 0.5 ? -x : x;
    double y2 = g->host->random(g->host->ctx) < 0.5 ? -y : y;
    double z2 = g->host->random(g->host->ctx) < 0.5 ? -z : z;
    e->movedir = rr_v3(x2, y2, z2);
  }
  qa_vec3 av = e->angular_velocity, md = e->movedir;
  double sx = av.x < md.x ? (md.x < av.x + e->accel ? md.x : av.x + e->accel) : (md.x > av.x - e->accel ? md.x : av.x - e->accel);
  double sy = av.y < md.y ? (md.y < av.y + e->accel ? md.y : av.y + e->accel) : (md.y > av.y - e->accel ? md.y : av.y - e->accel);
  double sz = av.z < md.z ? (md.z < av.z + e->accel ? md.z : av.z + e->accel) : (md.z > av.z - e->accel ? md.z : av.z - e->accel);
  e->angular_velocity = rr_v3(sx, sy, sz);
  g->motion(g->ctx, e, "push");
  g->schedule(g->ctx, e, rr_frame(g), q->spinning_think);
}
static void eye_think(void *c, qa_q2_entity *e, qa_q2_game_services *g);
static void eye_setup(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_vec3 a;
  qa_vec3 b;
  qa_q2_rerelease_q64 *q = c;
  qa_q2rr_eye_state *st = eye_get(q, e->actor.id);
  if (!st) qa_q2rr_error("func_eye source state missing");
  const char *pt = qa_q2_spawn_get(&e->spawn, "pathtarget");
  qa_q2_entity *target = g->pick_target(g->ctx, pt ? pt : "");
  if (!target) rr_diag(g, "func_eye: bad target");
  else st->eye_position = (a = rr_body_of(g, target).origin, b = rr_body_of(g, e).origin, qa_q2_subtract(a, b));
  e->movedir = qa_q2_normalize(st->eye_position);
  g->schedule(g->ctx, e, 0.1, q->eye_think);
}
static void eye_think(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_q64 *q = c;
  qa_q2rr_eye_state *st = eye_get(q, e->actor.id);
  if (!st) qa_q2rr_error("func_eye source state missing");
  qa_body_state body = rr_body_of(g, e);
  bool has_closest = false; qa_actor_id closest = {0}; double closest_dist = INFINITY;
  size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
  for (size_t i = 0; i < pn; i++) {
    qa_q2_base_player_state *pl = qa_q2_players_get_state(q->players->base, pls[i]);
    qa_body_state_result br = qa_shared_body_table_read(g->host->bodies, pls[i]);
    if (!pl || !pl->connected || !br.has_value) continue;
    qa_vec3 dir = qa_q2_subtract(br.value.origin, body.origin);
    double dist = qa_q2_length(dir);
    if (qa_q2_dot(qa_q2_normalize(dir), e->movedir) < st->vision_cone || dist >= e->damage_radius || dist >= closest_dist) continue;
    has_closest = true; closest = pls[i]; closest_dist = dist;
  }
  e->has_enemy = has_closest; if (has_closest) e->enemy = closest;
  qa_vec3 wanted = body.angles;
  qa_body_state_result tbr = has_closest ? qa_shared_body_table_read(g->host->bodies, closest) : (qa_body_state_result){0};
  if (has_closest && tbr.has_value) {
    if (rr_band(e->spawnflags, 0x20000) == 0) { qa_q2_game_use_targets_entity(g, e, &closest, false); e->spawnflags = rr_bor(e->spawnflags, 0x20000); }
    if (!qa_session_actor_registry_is_live(g->host->actors, e->actor.id)) return;
    qa_angle_vectors v = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
    qa_vec3 off = st->eye_position;
    qa_vec3 eye = qa_q2_add(body.origin, qa_q2_add(qa_q2_add(qa_q2_scale(v.forward, off.x), qa_q2_scale(v.right, off.y)), qa_q2_scale(v.up, off.z)));
    wanted = qa_vector_to_angles_with_rule(qa_q2_normalize(qa_q2_subtract(tbr.value.origin, eye)), QA_VECTOR_TO_ANGLES_NATIVE);
    e->frame = 2; e->timestamp = rr_now(g) + e->wait;
  } else if (e->timestamp <= rr_now(g)) { wanted = st->neutral_angles; e->frame = 0; }
  qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_angles = true;
  ch.angles = rr_v3(turn_q(body.angles.x, wanted.x, e->speed), turn_q(body.angles.y, wanted.y, e->speed), body.angles.z);
  g->move(g->ctx, e, ch, true);
  g->show(g->ctx, e);
  g->schedule(g->ctx, e, rr_frame(g), q->eye_think);
}
static qa_vec3 look_at(qa_q2_entity *e, qa_q2_game_services *g, qa_vec3 origin, qa_vec3 previous) {
  const char *pt = qa_q2_spawn_get(&e->spawn, "pathtarget");
  size_t n = 0; qa_q2_entity *const *targets = g->targets(g->ctx, pt ? pt : "", &n);
  if (n == 0) return previous;
  qa_vec3 delta = qa_q2_subtract(rr_body_of(g, targets[0]).origin, origin);
  double planar = hypot(delta.x, delta.y);
  return rr_v3(planar == 0 ? (delta.z > 0 ? -90 : 90) : -atan2(delta.z, planar) * 180 / 3.141592653589793,
               planar == 0 ? 0 : atan2(delta.y, delta.x) * 180 / 3.141592653589793, 0);
}
static void dummy_think(void *c, qa_q2_entity *e, qa_q2_game_services *g);
static void camera_think(void *c, qa_q2_entity *e, qa_q2_game_services *g);
static void camera_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *other, const qa_actor_id *act) {
  (void)other; qa_q2_rerelease_q64 *q = c;
  double music = qa_q2_number_field(&e->spawn, "sounds", 0);
  if (music != 0) {
    char tb[64]; snprintf(tb, sizeof tb, "%s", QA_JS_NUMBER(music)); /* String(music) */
    qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev); ev.kind = "music"; ev.track = tb; rr_emit_host(g, ev);
  }
  if (!e->target || !e->target[0]) return;
  qa_q2_entity *target = g->pick_target(g->ctx, e->target);
  if (!target) return;
  qa_vec3 origin = rr_body_of(g, e).origin;
  qa_q2_entity *source = rr_ent(g, act);
  e->has_goal = true; e->goal = target->actor.id;
  e->has_activator = act != NULL; if (act) e->activator = *act;
  if (source && qa_q2_players_get_state(q->players->base, source->actor.id)) {
    qa_body_state b = rr_body_of(g, source);
    qa_q2_entity *dummy = g->create(g->ctx, "target_camera_dummy", NULL, 0);
    e->has_enemy = true; e->enemy = dummy->actor.id;
    dummy->has_owner = true; dummy->owner = source->actor.id; dummy->clip_mask = source->clip_mask;
    dummy->model = source->model; dummy->model2 = source->model2; dummy->skin = source->skin;
    dummy->frame = source->frame; dummy->render_flags = 1;
    qa_q2_body_changes ch; memset(&ch, 0, sizeof ch);
    ch.has_origin = true; ch.origin = b.origin; ch.has_angles = true; ch.angles = b.angles;
    ch.has_velocity = true; ch.velocity = b.velocity; ch.has_bounds = true; ch.bounds = b.bounds;
    ch.has_ground = true; ch.ground_null = b.ground == NULL; if (b.ground) ch.ground = *b.ground;
    g->move(g->ctx, dummy, ch, false);
    g->solid(g->ctx, dummy, "box"); g->motion(g->ctx, dummy, "step"); g->link(g->ctx, dummy); g->show(g->ctx, dummy);
    dum_set(q, dummy->actor.id, (qa_q2rr_dummy_state){0, 0, false, false});
    g->schedule(g->ctx, dummy, 0.1, q->dummy_think);
  }
  double distance = qa_q2_length(qa_q2_subtract(rr_body_of(g, target).origin, origin));
  qa_q2rr_camera_state st = {distance, distance, e->speed, look_at(e, g, origin, QA_Q2_ZERO), false};
  cam_set(q, e->actor.id, st);
  qa_q2_players_set_intermission(q->players->base,
      (qa_q2_intermission){.kind = "intermission", .map = "", .started = rr_now(g), .exit = false, .has_landmark = false});
  qa_q2_rerelease_players_move_to_camera(q->players, g, origin, st.angles, true);
  if (rr_band(qa_q2_number_field(&e->spawn, "hackflags", 0), 128) != 0) q->end_of_unit(q->end_ctx, g);
  g->schedule(g->ctx, e, e->wait, q->camera_think);
}
static void camera_think(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_q64 *q = c;
  qa_q2rr_camera_state *st = cam_get(q, e->actor.id);
  if (!st) qa_q2rr_error("target_camera source state missing");
  bool skip = false;
  if (rr_band(qa_q2_number_field(&e->spawn, "hackflags", 0), 64) != 0 && rr_now(g) > 2) {
    for (size_t i = 0; i < qa_q2_players_state_count(q->players->base); i++) {
      qa_actor_id actor; qa_q2_base_player_state *state;
      if (qa_q2_players_state_at(q->players->base, i, &actor, &state) && state->connected && state->buttons != 0) skip = true;
    }
  }
  qa_q2_entity *target = e->has_goal ? rr_ent(g, &e->goal) : NULL;
  if (skip || !target) {
    if (e->killtarget && e->killtarget[0]) {
      qa_q2_entity *dummy = e->has_enemy ? rr_ent(g, &e->enemy) : NULL;
      if (dummy) g->remove(g->ctx, dummy);
      qa_q2_players_set_intermission(q->players->base, (qa_q2_intermission){.kind = "playing"});
      q->players->intermission_camera_set = true;
      size_t n = 0; qa_q2_entity *const *dests = g->targets(g->ctx, e->killtarget, &n);
      for (size_t i = 0; i < n; i++) if (dests[i]->use.fn)
        dests[i]->use.fn(dests[i]->use.ctx, dests[i], g, &e->actor.id, e->has_activator ? &e->activator : NULL);
      qa_q2_rerelease_players_finish_camera(q->players, g);
    }
    g->cancel(g->ctx, e); return;
  }
  st->remaining -= st->speed * rr_frame(g) * 0.8;
  if (st->remaining <= 0) {
    if (rr_band(qa_q2_number_field(&target->spawn, "hackflags", 0), 2) != 0) {
      qa_q2_entity *dummy = e->has_enemy ? rr_ent(g, &e->enemy) : NULL;
      qa_q2rr_dummy_state *fade = dummy ? dum_get(q, dummy->actor.id) : NULL;
      if (dummy && fade) {
        qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
        ev.kind = "entity-event"; ev.has_actor = true; ev.actor = dummy->actor.id; ev.event = 6; rr_emit_host(g, ev);
        fade->fading = true; fade->fade_remaining = target->wait; fade->fade_duration = target->wait;
      }
    }
    qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_origin = true; ch.origin = rr_body_of(g, target).origin;
    g->move(g->ctx, e, ch, true);
    qa_q2_entity *next = (target->target && target->target[0]) ? g->pick_target(g->ctx, target->target) : NULL;
    e->has_goal = next != NULL; if (next) e->goal = next->actor.id;
    if (next) {
      st->speed = next->speed != 0 ? next->speed : 55;
      qa_vec3 a2 = rr_body_of(g, next).origin;
      qa_vec3 b2 = rr_body_of(g, e).origin;
      st->distance = st->remaining = qa_q2_length(qa_q2_subtract(a2, b2));
    }
    g->schedule(g->ctx, e, target->wait, q->camera_think); return;
  }
  double fraction = 1 - st->remaining / st->distance;
  qa_vec3 a = rr_body_of(g, e).origin;
  qa_vec3 a3 = rr_body_of(g, target).origin;
  qa_vec3 b3 = rr_body_of(g, e).origin;
  qa_vec3 b = qa_q2_scale(qa_q2_subtract(a3, b3), fraction);
  qa_vec3 origin = qa_q2_add(a, b);
  qa_q2_entity *dummy = e->has_enemy ? rr_ent(g, &e->enemy) : NULL;
  if (dummy) {
    qa_q2rr_dummy_state *ds = dum_get(q, dummy->actor.id);
    if (ds && ds->fading) {
      qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "alpha"; ev.has_actor = true; ev.actor = dummy->actor.id;
      ev.alpha = fraction > 1.0 / 255 ? fraction : 1.0 / 255;
      rr_emit_rr(&q->hooks, ev);
    }
  }
  st->angles = look_at(e, g, origin, st->angles);
  qa_q2_rerelease_players_move_to_camera(q->players, g, origin, st->angles, false);
  g->schedule(g->ctx, e, rr_frame(g), q->camera_think);
}
static void dummy_think(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_q64 *q = c;
  qa_q2_entity *source = e->has_owner ? rr_ent(g, &e->owner) : NULL;
  qa_q2rr_dummy_state *st = dum_get(q, e->actor.id);
  if (!source || !st) { g->remove(g->ctx, e); return; }
  qa_q2_player_context ctx = qa_q2_players_context(q->players->base, source, g);
  ctx.entity = e;
  ctx.movement.grounded = rr_body_of(g, e).ground != NULL;
  qa_q2_rerelease_client_animation(&ctx, qa_q2_rerelease_players_get_extra(q->players, source->actor.id));
  g->show(g->ctx, e);
  if (st->fading) {
    st->fade_remaining = st->fade_remaining - 0.1 > 0 ? st->fade_remaining - 0.1 : 0;
    double a = st->fade_duration == 0 ? 0 : st->fade_remaining / st->fade_duration;
    qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "alpha"; ev.has_actor = true; ev.actor = e->actor.id;
    ev.alpha = a > 1.0 / 255 ? a : 1.0 / 255;
    rr_emit_rr(&q->hooks, ev);
  }
  g->schedule(g->ctx, e, 0.1, q->dummy_think);
}

void qa_q2_rerelease_q64_init(qa_q2_rerelease_q64 *q, qa_q2_rerelease_players *p,
                              qa_q2_rerelease_hooks h, void *end_ctx,
                              void (*end_of_unit)(void *ctx, qa_q2_game_services *g)) {
  memset(q, 0, sizeof *q); q->players = p; q->hooks = h; q->end_ctx = end_ctx; q->end_of_unit = end_of_unit;
  q->spinning_think = (qa_q2_think){q, spinning_think}; q->eye_setup = (qa_q2_think){q, eye_setup};
  q->eye_think = (qa_q2_think){q, eye_think}; q->camera_think = (qa_q2_think){q, camera_think};
  q->dummy_think = (qa_q2_think){q, dummy_think}; q->camera_use = (qa_q2_use){q, camera_use};
}
void qa_q2_rerelease_q64_destroy(qa_q2_rerelease_q64 *q) {
  free(q->eyes); free(q->cameras); free(q->dummies); free(q->cp_eyes); free(q->cp_cams); free(q->cp_dums);
  memset(q, 0, sizeof *q);
}
bool qa_q2_rerelease_q64_spawn(qa_q2_rerelease_q64 *q, qa_q2_entity *e, qa_q2_game_services *g) {
  const char *cn = e->classname; if (!cn) return false;
  if (strcmp(cn, "target_camera") == 0) {
    if (g->options.mode == QA_Q2_MODE_DEATHMATCH) g->remove(g->ctx, e);
    else { e->visible = false; e->server_flags = rr_bor(e->server_flags, 1); e->use = q->camera_use; }
    return true;
  }
  if (strcmp(cn, "func_eye") != 0 && strcmp(cn, "func_spinning") != 0) return false;
  g->solid(g->ctx, e, "brush"); g->motion(g->ctx, e, "push");
  if (strcmp(cn, "func_spinning") == 0) {
    if (e->speed == 0) e->speed = 100;
    if (e->damage == 0) e->damage = 2;
    e->timestamp = 0;
    g->schedule(g->ctx, e, rr_frame(g), q->spinning_think);
  } else {
    double rad = qa_q2_number_field(&e->spawn, "radius", 0);
    e->damage_radius = rad != 0 ? rad : 512;
    e->speed = (e->speed != 0 ? e->speed : 45) * rr_frame(g);
    e->wait = 1;
    double vc = qa_q2_number_field(&e->spawn, "vision_cone", 0);
    qa_vec3 neutral_angles2 = rr_body_of(g, e).angles;
    qa_vec3 eye_position2 = qa_q2_vector_field(&e->spawn, "eye_position");
    qa_q2rr_eye_state st = {neutral_angles2, eye_position2, vc != 0 ? vc : 0.5};
    eye_set(q, e->actor.id, st);
    const char *pt = qa_q2_spawn_get(&e->spawn, "pathtarget");
    if (pt && pt[0]) g->schedule(g->ctx, e, 0.1, q->eye_setup);
    else {
      qa_angle_vectors v = qa_math_angle_vectors_rule(st.neutral_angles, QA_ANGLE_RULE_NATIVE);
      qa_vec3 point = st.eye_position;
      e->movedir = v.forward;
      qa_q2rr_eye_state *stored = eye_get(q, e->actor.id);
      stored->eye_position = qa_q2_add(qa_q2_add(qa_q2_scale(v.forward, point.x), qa_q2_scale(v.right, point.y)), qa_q2_scale(v.up, point.z));
      g->schedule(g->ctx, e, 0.1, q->eye_think);
    }
  }
  g->link(g->ctx, e); return true;
}
static qa_q2_named_think q64_thinks[5]; static qa_q2_named_use q64_uses[1];
qa_q2_callback_definitions qa_q2_rerelease_q64_callbacks(qa_q2_rerelease_q64 *q) {
  q64_thinks[0] = (qa_q2_named_think){"rr.func_eye_setup", q->eye_setup};
  q64_thinks[1] = (qa_q2_named_think){"rr.func_eye_think", q->eye_think};
  q64_thinks[2] = (qa_q2_named_think){"rr.func_spinning_think", q->spinning_think};
  q64_thinks[3] = (qa_q2_named_think){"rr.update_target_camera", q->camera_think};
  q64_thinks[4] = (qa_q2_named_think){"rr.target_camera_dummy_think", q->dummy_think};
  q64_uses[0] = (qa_q2_named_use){"rr.use_target_camera", q->camera_use};
  qa_q2_callback_definitions d = {0}; d.think = q64_thinks; d.think_count = 5; d.use = q64_uses; d.use_count = 1; return d;
}
static bool q64_spawn_cb(void *c, qa_q2_entity *e, qa_q2_game_services *g) { return qa_q2_rerelease_q64_spawn(c, e, g); }
qa_q2_spawn_module qa_q2_rerelease_q64_as_spawn(qa_q2_rerelease_q64 *q) {
  qa_q2_spawn_module m = {0}; m.ctx = q; m.spawn = q64_spawn_cb; return m;
}
