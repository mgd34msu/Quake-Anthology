/* Port of src/content/q2/base/monsters/medic.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/medic.h"
#include "qa/foundation/js.h"

#include <math.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/medic.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/perception.h"

static const qa_vec3 cable_offsets[10] = {
  {45, -9.2, 15.5}, {48.4, -9.7, 15.2}, {47.8, -9.8, 15.8}, {47.3, -9.3, 14.3}, {45.4, -10.1, 13.1},
  {41.9, -12.7, 12}, {37.8, -15.8, 11.2}, {34.3, -18.4, 10.7}, {32.7, -19.7, 10.4}, {32.7, -19.7, 10.4}};

static qa_q2_entity *patient(qa_q2m_monsters *monsters, qa_q2m_context *c) {
  qa_q2_entity *best = NULL;
  size_t count = 0;
  const qa_actor_id *actors = c->game->host->nearby(c->game->host->ctx, q2bm_body(c->game, c->entity).origin, 1024, &count);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = actors[i];
    qa_q2_entity *entity = c->game->entity(c->game->ctx, &actor);
    qa_q2m_context *candidate = qa_q2m_monsters_context(monsters, actor);
    if (entity == NULL || entity == c->entity || (qa_js_to_uint32(entity->server_flags) & 4u) == 0
        || (candidate && candidate->state->good_guy) || entity->has_owner
        || qa_q2m_health(c->game, &actor) > 0 || entity->has_next_think || !qa_q2m_visible(c, &actor))
      continue;
    if (best == NULL || entity->max_health > best->max_health) best = entity;
  }
  return best;
}
static bool acquire(qa_q2m_monsters *monsters, qa_q2m_context *c, bool preserve_enemy) {
  qa_q2_entity *target = patient(monsters, c);
  if (target == NULL) return false;
  if (preserve_enemy) {
    c->state->has_old_enemy = c->entity->has_enemy;
    if (c->entity->has_enemy) c->state->old_enemy = c->entity->enemy;
  }
  c->entity->has_enemy = true; c->entity->enemy = target->actor.id;
  target->has_owner = true; target->owner = c->entity->actor.id;
  c->state->medic = true;
  qa_q2m_monsters_found_target(monsters, c);
  return true;
}
static void run_fn(void *ctx, qa_q2m_context *c) {
  if (!c->state->medic && acquire(ctx, c, true)) return;
  q2bm_set_move(c, c->state->stand_ground ? "medic_move_stand" : "medic_move_run");
}
static void idle_fn(void *ctx, qa_q2m_context *c) {
  q2bm_sound(c->game, c->entity, "medic/idle.wav", true, 2, true, 1, true, 2);
  (void)acquire(ctx, c, false);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->medic ? "medic_move_attackCable" : "medic_move_attackBlaster");
}
static const qa_q2bm_sound_spec sight_spec = {"medic/medsght1.wav", 2, 1};
static const qa_q2bm_sound_spec hook_launch_spec = {"medic/medatck2.wav", 1, 1};
static void search_fn(void *ctx, qa_q2m_context *c) {
  q2bm_sound(c->game, c->entity, "medic/medsrch1.wav", true, 2, true, 1, true, 2);
  if (!c->state->has_old_enemy) (void)acquire(ctx, c, true);
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->medic) { attack_fn(NULL, c); return true; }
  return qa_q2m_default_check_attack(c, NULL);
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  bool first = q2bm_rnd(c->game) < 0.5;
  q2bm_set_move(c, first ? "medic_move_pain1" : "medic_move_pain2");
  q2bm_sound(c->game, c->entity, first ? "medic/medpain1.wav" : "medic/medpain2.wav",
             true, 2, false, 0, false, 0);
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2_entity *target = c->game->entity(c->game->ctx, c->entity->has_enemy ? &c->entity->enemy : NULL);
  if (target && target->has_owner && qa_same_actor(target->owner, c->entity->actor.id))
    target->has_owner = false;
  qa_q2bm_begin_death_default(c, reaction, "medic/meddeth1.wav", "medic_move_death");
}
static void dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta,
                     const qa_trace_result *trace, bool gravity) {
  (void)ctx; (void)eta; (void)trace; (void)gravity;
  if (q2bm_rnd(c->game) > 0.25) return;
  if (!c->entity->has_enemy) { c->entity->has_enemy = true; c->entity->enemy = attacker; }
  q2bm_set_move(c, "medic_move_duck");
}
static void duck_down_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->ducked) return;
  qa_q2m_set_duck(c, true);
  c->state->pause_time = q2bm_now(c->game) + 1;
}
static void duck_hold_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->hold_frame = q2bm_now(c->game) < c->state->pause_time;
}
static void duck_up_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_set_duck(c, false); }
static void hook_retract_fn(void *ctx, qa_q2m_context *c) {
  q2bm_sound(c->game, c->entity, "medic/medatck5.wav", true, 1, false, 0, false, 0);
  if (!c->entity->has_enemy) return;
  qa_q2m_context *target = qa_q2m_monsters_context(ctx, c->entity->enemy);
  if (target) target->state->resurrecting = false;
}
static void continue_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2m_visible_enemy(c) && q2bm_rnd(c->game) <= 0.95) q2bm_set_move(c, "medic_move_attackHyperBlaster");
}
static void fire_blaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 60, &aim)) return;
  double frame = c->entity->frame;
  const qa_q2bm_medic_frames *f = &qa_q2bm_medic_frames_value;
  double effects = frame == f->attack9 || frame == f->attack12 ? 8
                 : frame == f->attack19 || frame == f->attack22 || frame == f->attack25 || frame == f->attack28 ? 64 : 0;
  (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 2, 1000, effects,
                                false, false, 0);
  qa_q2bm_muzzle(c, 60, aim.direction, aim.start);
}
static void cable_attack_fn(void *ctx, qa_q2m_context *c) {
  qa_q2m_monsters *monsters = ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *target = game->entity(game->ctx, entity->has_enemy ? &entity->enemy : NULL);
  if (target == NULL) return;
  qa_vec3 start = qa_q2m_project_flash_default(
      c, cable_offsets[qa_q2m_record_at(10, entity->frame - qa_q2bm_medic_frames_value.attack42)]);
  qa_body_state body = q2bm_body(game, target);
  qa_vec3 direction = qa_sub3_double(start, body.origin);
  if (qa_hypot3(direction) > 256) return;
  double pitch = qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE_AI).x;
  if (pitch < -180) pitch += 360;
  if (fabs(pitch) > 45) return;
  qa_trace_result trace = q2bm_trace(game, start, body.origin, NULL, &entity->actor.id, QA_Q2M_MASK_SHOT);
  if (trace.fraction != 1 && (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, target->actor.id)))
    return;
  if (entity->frame == qa_q2bm_medic_frames_value.attack43) {
    qa_q2_presentation_event e;
    memset(&e, 0, sizeof e);
    e.kind = "sound"; e.has_actor = true; e.actor = target->actor.id; e.origin = body.origin;
    e.path = "medic/medatck3.wav"; e.channel = 0; e.volume = 1; e.attenuation = 1;
    e.reliable = false; e.loop = "once";
    game->host->emit(game->host->ctx, e);
    qa_q2m_context *patient_context = qa_q2m_monsters_context(monsters, target->actor.id);
    if (patient_context) patient_context->state->resurrecting = true;
  } else if (entity->frame == qa_q2bm_medic_frames_value.attack50) {
    target->spawnflags = 0; target->target = ""; target->targetname = "";
    target->combat_target = ""; target->death_target = "";
    target->has_owner = true; target->owner = entity->actor.id;
    qa_q2m_context *revived = qa_q2m_monsters_respawn(monsters, target, game);
    target->has_owner = false;
    if (target->think.fn != NULL) {
      target->has_next_think = true; target->next_think = q2bm_now(game);
      qa_q2_think think = target->think;
      think.fn(think.ctx, target, game);
    }
    revived->state->resurrecting = true;
    if (c->state->has_old_enemy && game->host->is_player(game->host->ctx, c->state->old_enemy)) {
      target->has_enemy = true; target->enemy = c->state->old_enemy;
      qa_q2m_monsters_found_target(monsters, revived);
    }
  } else if (entity->frame == qa_q2bm_medic_frames_value.attack44) {
    q2bm_sound(game, entity, "medic/medatck4.wav", true, 1, false, 0, false, 0);
  }
  qa_body_state current = q2bm_body(game, target);
  qa_vec3 end = current.origin;
  end.z = current.origin.z + (current.bounds.min.z + current.bounds.max.z) / 2;
  qa_vec3 forward = qa_math_angle_vectors_rule(q2bm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).forward;
  qa_q2_presentation_event beam;
  memset(&beam, 0, sizeof beam);
  beam.kind = "monster-beam"; beam.effect = "medic"; beam.has_actor = true; beam.actor = entity->actor.id;
  beam.start = qa_add3_double(start, qa_scale3_double(forward, 8));
  beam.end = end;
  game->host->emit(game->host->ctx, beam);
}
void qa_q2bm_create_medic_definition(qa_q2bm_medic_definition *out, qa_q2m_monsters *monsters) {
  qa_q2m_named_handler callbacks[11] = {
    {"medic_idle", {monsters, idle_fn}},
    {"medic_run", {monsters, run_fn}},
    {"medic_dead", {NULL, qa_q2bm_finish_corpse_handler}},
    {"medic_duck_down", {NULL, duck_down_fn}},
    {"medic_duck_hold", {NULL, duck_hold_fn}},
    {"medic_duck_up", {NULL, duck_up_fn}},
    {"medic_hook_launch", {(void *)&hook_launch_spec, qa_q2bm_sound_handler_fn}},
    {"medic_hook_retract", {monsters, hook_retract_fn}},
    {"medic_continue", {NULL, continue_fn}},
    {"medic_fire_blaster", {NULL, fire_blaster_fn}},
    {"medic_cable_attack", {monsters, cable_attack_fn}},
  };
  memcpy(out->callbacks, callbacks, sizeof callbacks);
  qa_q2m_definition d = {
    .classname = "monster_medic", .kind = "medic", .model = "models/monsters/medic/tris.md2",
    .health = 300, .gib_health = -130, .mass = 400, .bounds = {{-24, -24, -24}, {24, 24, 32}}, .scale = 1,
    .initial_move = "medic_move_stand",
    .moves = qa_q2bm_medic_moves, .move_count = qa_q2bm_medic_move_count,
    .callbacks = out->callbacks, .callback_count = 11,
    .stand = {(void *)"medic_move_stand", qa_q2bm_move_handler_fn},
    .walk = {(void *)"medic_move_walk", qa_q2bm_move_handler_fn},
    .run = {monsters, run_fn}, .attack = {NULL, attack_fn}, .idle = {monsters, idle_fn},
    .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
    .search = {monsters, search_fn},
    .check_attack = check_attack_fn,
    .reactions = {NULL, pain_fn, die_fn},
    .dodge = dodge_fn,
  };
  out->definition = d;
}
