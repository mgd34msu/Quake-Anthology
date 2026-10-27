/* Port of src/content/q2/base/monsters/parasite.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/parasite.h"

#include <math.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/parasite.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "parasite_move_stand" : "parasite_move_run");
}
static void start_run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "parasite_move_stand" : "parasite_move_start_run");
}
bool qa_q2bm_parasite_drain_reachable(qa_vec3 start, qa_vec3 end) {
  qa_vec3 direction = qa_sub3_double(start, end);
  if (qa_hypot3(direction) > 256) return false;
  double pitch = qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE_AI).x;
  if (pitch < -180) pitch += 360;
  return fabs(pitch) <= 30;
}
static const qa_q2bm_sound_spec sight_spec = {"parasite/parsght1.wav", 1, 1};
static const qa_q2bm_sound_spec launch_spec = {"parasite/paratck1.wav", 1, 1};
static const qa_q2bm_sound_spec reel_spec = {"parasite/paratck4.wav", 1, 1};
static const qa_q2bm_sound_spec tap_spec = {"parasite/paridle1.wav", 1, 2};
static const qa_q2bm_sound_spec scratch_spec = {"parasite/paridle2.wav", 1, 2};
static const qa_q2bm_sound_spec search_spec = {"parasite/parsrch1.wav", 1, 2};

static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  q2bm_sound(c->game, c->entity, q2bm_rnd(c->game) < 0.5 ? "parasite/parpain1.wav" : "parasite/parpain2.wav",
             true, 2, false, 0, false, 0);
  q2bm_set_move(c, "parasite_move_pain1");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2bm_begin_death_default(c, reaction, "parasite/pardeth1.wav", "parasite_move_death");
}
static void refidget_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, q2bm_rnd(c->game) <= 0.8 ? "parasite_move_fidget" : "parasite_move_end_fidget");
}
static void drain_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !entity->has_enemy) return;
  qa_vec3 start = qa_q2m_project_flash_default(c, q2bm_v3(24, 0, 6));
  qa_vec3 top = enemy.origin; top.z = enemy.origin.z + enemy.bounds.max.z - 8;
  qa_vec3 bottom = enemy.origin; bottom.z = enemy.origin.z + enemy.bounds.min.z + 8;
  if (!qa_q2bm_parasite_drain_reachable(start, enemy.origin)
      && !qa_q2bm_parasite_drain_reachable(start, top)
      && !qa_q2bm_parasite_drain_reachable(start, bottom))
    return;
  qa_vec3 end = enemy.origin;
  qa_trace_result trace = q2bm_trace(game, start, end, NULL, &entity->actor.id, QA_Q2M_MASK_SHOT);
  if (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, entity->enemy)) return;
  bool first = entity->frame == qa_q2bm_parasite_frames_value.drain03;
  if (first) {
    qa_q2f_presentation_event e;
    memset(&e, 0, sizeof e);
    e.kind = QA_Q2F_EVENT_SOUND; e.actor = &entity->enemy; e.origin = end;
    e.path = "parasite/paratck2.wav"; e.channel = 0; e.volume = 1; e.attenuation = 1;
    e.reliable = false; e.loop = "once";
    game->host->emit(game->host->ctx, e);
  } else if (entity->frame == qa_q2bm_parasite_frames_value.drain04) {
    q2bm_sound(game, entity, "parasite/paratck3.wav", true, 1, false, 0, false, 0);
  }
  qa_q2bm_extended_event beam;
  memset(&beam, 0, sizeof beam);
  beam.kind = "monster-beam"; beam.effect = "parasite"; beam.actor = entity->actor.id;
  beam.start = start; beam.end = end;
  qa_q2bm_emit_extended(beam);
  qa_q2f_damage_args args;
  memset(&args, 0, sizeof args);
  args.target = entity->enemy; args.inflictor_entity = entity; args.inflictor_actor = entity->actor.id;
  args.attacker = &entity->actor.id;
  args.amount = first ? 5 : 2; args.knockback = 0;
  args.direction = qa_sub3_double(start, end); args.point = end; args.normal = q2bm_zero();
  args.means_of_death = 0; args.has_flags = true; args.flags = 8; args.weapon = NULL;
  (void)game->damage(game->ctx, args);
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler parasite_callbacks[] = {
  {"parasite_stand", {(void *)"parasite_move_stand", qa_q2bm_move_handler_fn}},
  {"parasite_walk", {(void *)"parasite_move_walk", qa_q2bm_move_handler_fn}},
  CB("parasite_run", run_fn), CB("parasite_start_run", start_run_fn),
  CB("parasite_dead", qa_q2bm_finish_corpse_handler),
  {"parasite_launch", {(void *)&launch_spec, qa_q2bm_sound_handler_fn}},
  {"parasite_reel_in", {(void *)&reel_spec, qa_q2bm_sound_handler_fn}},
  {"parasite_tap", {(void *)&tap_spec, qa_q2bm_sound_handler_fn}},
  {"parasite_scratch", {(void *)&scratch_spec, qa_q2bm_sound_handler_fn}},
  {"parasite_search", {(void *)&search_spec, qa_q2bm_sound_handler_fn}},
  {"parasite_do_fidget", {(void *)"parasite_move_fidget", qa_q2bm_move_handler_fn}},
  CB("parasite_refidget", refidget_fn), CB("parasite_drain_attack", drain_attack_fn),
};
const qa_q2m_definition qa_q2bm_parasite_definition = {
  .classname = "monster_parasite", .kind = "parasite", .model = "models/monsters/parasite/tris.md2",
  .health = 175, .gib_health = -50, .mass = 250, .bounds = {{-16, -16, -24}, {16, 16, 24}}, .scale = 1,
  .initial_move = "parasite_move_stand",
  .moves = qa_q2bm_parasite_moves, .move_count = 14,
  .callbacks = parasite_callbacks, .callback_count = sizeof parasite_callbacks / sizeof parasite_callbacks[0],
  .stand = {(void *)"parasite_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"parasite_move_start_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, start_run_fn},
  .attack = {(void *)"parasite_move_drain", qa_q2bm_move_handler_fn},
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .idle = {(void *)"parasite_move_start_fidget", qa_q2bm_move_handler_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
