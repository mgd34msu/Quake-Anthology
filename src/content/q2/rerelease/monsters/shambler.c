/* Port of src/content/q2/rerelease/monsters/shambler.ts.
 * Rerelease m_shambler.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/shambler.h"

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/missionpacks/monsters/types.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/shambler.h"
#include "rrm_internal.h"

static const qa_vec3 left_hand[5] = {{44, 36, 25}, {10, 44, 57}, {-1, 40, 70}, {-10, 34, 75}, {7.4, 24, 89}};
static const qa_vec3 right_hand[5] = {{28, -38, 25}, {31, -7, 70}, {20, 0, 80}, {16, 1.2, 81}, {27, -11, 83}};

static void emit_shambler_beam(qa_q2f_entity *actor_entity, qa_vec3 start, qa_vec3 end, bool visible) {
  qa_q2mpm_beam_event event;
  memset(&event, 0, sizeof event);
  event.actor = actor_entity->actor.id;
  event.start = start; event.end = end;
  event.width = 2; event.color = (double)(uint32_t)0xffffffffu; event.visible = visible;
  qa_q2mpm_emit_beam(event);
}
static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->brutal = c->entity->has_enemy && c->game->host->is_player(c->game->host->ctx, c->entity->enemy);
  q2rm_set_move(c, c->state->stand_ground ? "shambler_move_stand" : "shambler_move_run");
}
static void clear_beam(qa_q2m_context *c) {
  qa_q2f_game *game = c->game;
  const qa_actor_id *ids[2] = {c->entity->has_beam ? &c->entity->beam : NULL,
                               c->entity->has_beam2 ? &c->entity->beam2 : NULL};
  for (int i = 0; i < 2; i++) {
    qa_q2f_entity *beam = game->entity(game->ctx, ids[i]);
    if (!beam) continue;
    emit_shambler_beam(beam, q2rm_body(game, beam).origin, beam->pos2, false);
    game->remove(game->ctx, beam);
  }
  c->entity->has_beam = false;
  c->entity->has_beam2 = false;
}
static void lightning_update(qa_q2m_context *c) {
  double frame = c->entity->frame - qa_q2rm_shambler_frames_value.magic01;
  if (frame >= 5) { clear_beam(c); return; }
  if (frame < 0) return;
  qa_q2f_entity *beam = c->game->entity(c->game->ctx, c->entity->has_beam ? &c->entity->beam : NULL);
  if (!beam) return;
  qa_vec3 start = qa_q2m_project_flash_default(c, left_hand[(size_t)frame]);
  qa_vec3 end = qa_q2m_project_flash_default(c, right_hand[(size_t)frame]);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = start;
  q2rm_move_body(c->game, beam, change, false, false);
  beam->pos2 = end;
  emit_shambler_beam(beam, start, end, true);
}
static void claw(qa_q2m_context *c, bool smash) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  if (!entity->has_enemy) return;
  qa_q2m_run_ai(c, QA_Q2M_AI_CHARGE, smash ? 0 : 10);
  if (!game->can_damage(game->ctx, entity->enemy, entity->actor)) return;
  qa_vec3 aim = {80, q2rm_body(game, entity).bounds.min.x, -4};
  bool hit = c->weapons.fire_hit(c->weapons.ctx, entity, game, aim,
                                 (smash ? 110 : 70) + floor(q2rm_rnd(game) * 10), smash ? 120 : 80);
  if (hit) q2rm_snd(c, "shambler/smack.wav", 1);
}
static void melee_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double chance = q2rm_rnd(c->game);
  q2rm_set_move(c, chance > 0.6 || qa_q2m_health(c->game, &c->entity->actor.id) == 600 ? "shambler_attack_smash"
      : chance > 0.3 ? "shambler_attack_swingl" : "shambler_attack_swingr");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  if (q2rm_now(game) < entity->timestamp) return;
  entity->timestamp = q2rm_now(game) + 0.001;
  q2rm_snd(c, "shambler/shurt2.wav", 0);
  if (!qa_q2rm_chainfist(c) && reaction.damage <= 30 && q2rm_rnd(game) > 0.2) return;
  const qa_q2rm_shambler_frames *f = &qa_q2rm_shambler_frames_value;
  double frame = entity->frame;
  if (game->options.skill >= 2
      && ((frame >= f->smash01 && frame <= f->smash12) || (frame >= f->swingl01 && frame <= f->swingl09)
          || (frame >= f->swingr01 && frame <= f->swingr09))) return;
  if (!qa_q2rm_reacts_to_pain(c) || q2rm_now(game) < state->pain_time) return;
  state->pain_time = q2rm_now(game) + 2;
  q2rm_set_move(c, "shambler_move_pain");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  clear_beam(c);
  if (qa_q2rm_check_gib(c)) {
    q2rm_snd(c, "misc/udeath.wav", 2);
    qa_q2m_gib_options plain = {false, false, false, false};
    qa_q2m_gib_options head = {true, false, false, false};
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", reaction.pain.damage, plain);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/chest/tris.md2", reaction.pain.damage, plain);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/head2/tris.md2", reaction.pain.damage, head);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2rm_snd(c, "shambler/sdeath.wav", 2);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, "shambler_move_death");
}
static void maybe_idle_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2rm_rnd(c->game) > 0.8) q2rm_snd4(c, "shambler/sidle.wav", 2, 1, 2);
}
static void windup_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_snd(c, "shambler/sattck1.wav", 1);
  qa_q2f_entity *beam = c->game->create(c->game->ctx, "shambler_lightning", NULL);
  c->entity->beam = beam->actor.id; c->entity->has_beam = true;
  beam->owner = c->entity->actor.id; beam->has_owner = true;
  beam->model = "models/proj/lightning/tris.md2";
  beam->render_flags = q2rm_or(beam->render_flags, 128);
  lightning_update(c);
}
static void lightning_fn(void *ctx, qa_q2m_context *c) { (void)ctx; lightning_update(c); }
static void save_loc_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 eye;
  if (!qa_q2m_enemy_eye(c, &eye)) return;
  c->entity->pos1 = eye;
  c->state->next_frame = qa_q2rm_shambler_frames_value.magic09;
  q2rm_snd(c, "shambler/sboom.wav", 1);
  lightning_update(c);
}
static void cast_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_vec3 offset = {0, 0, 48};
  for (int i = 0; i < 8; i++) {
    qa_vec3 candidate = {0, 0, 48.0 - i * 4};
    if (qa_q2m_clear_shot(c, candidate)) { offset = candidate; break; }
  }
  qa_vec3 start = qa_q2m_project_flash_default(c, offset);
  qa_vec3 direction;
  if (!qa_q2rm_predicted_direction(c, start, 0, false, q2rm_has(c->entity->spawnflags, 1) ? 0 : 0.1, &direction))
    return;
  qa_q2f_game *game = c->game;
  qa_vec3 end = qa_add3_double(start, qa_scale3_double(direction, 8192));
  qa_trace_result trace = q2rm_trace(game, start, end, NULL, &c->entity->actor.id,
                                     (double)((0x46004003u | 8u | 16u)));
  emit_shambler_beam(c->entity, start, trace.end, true);
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, game, start, direction,
                         8 + floor(q2rm_rnd(game) * 4), 15, 0, 0, 45);
}
static void smash_fn(void *ctx, qa_q2m_context *c) { (void)ctx; claw(c, true); }
static void claw_fn(void *ctx, qa_q2m_context *c) { (void)ctx; claw(c, false); }
static void swingl_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_run_ai(c, QA_Q2M_AI_CHARGE, 8);
  qa_body_state enemy;
  if (q2rm_rnd(c->game) < 0.5 && qa_q2m_enemy_body(c, &enemy) && qa_q2m_target_distance(c) < 80)
    q2rm_set_move(c, "shambler_attack_swingr");
}
static void swingr_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_run_ai(c, QA_Q2M_AI_CHARGE, 1);
  qa_q2m_run_ai(c, QA_Q2M_AI_CHARGE, 10);
  qa_body_state enemy;
  if (q2rm_rnd(c->game) < 0.5 && qa_q2m_enemy_body(c, &enemy) && qa_q2m_target_distance(c) < 80)
    q2rm_set_move(c, "shambler_attack_swingl");
}
static void shrink_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  bounds.max.z = 0;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_corpse(c);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2rm_v3(-16, -16, -24);
  change.bounds.max = q2rm_v3(16, 16, 0);
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static const qa_q2bm_sound_spec sight_spec = {"shambler/ssight.wav", 2, 1};
static const qa_q2bm_sound_spec idle_spec = {"shambler/sidle.wav", 2, 2};
static const qa_q2bm_sound_spec melee1_spec = {"shambler/melee1.wav", 1, 1};
static const qa_q2bm_sound_spec melee2_spec = {"shambler/melee2.wav", 1, 1};
static const qa_q2m_named_handler shambler_callbacks[] = {
  {"shambler_run", {NULL, run_fn}},
  {"shambler_maybe_idle", {NULL, maybe_idle_fn}},
  {"shambler_windup", {NULL, windup_fn}},
  {"shambler_lightning_update", {NULL, lightning_fn}},
  {"ShamblerSaveLoc", {NULL, save_loc_fn}},
  {"ShamblerCastLightning", {NULL, cast_fn}},
  {"shambler_melee1", {(void *)&melee1_spec, qa_q2bm_sound_handler_fn}},
  {"shambler_melee2", {(void *)&melee2_spec, qa_q2bm_sound_handler_fn}},
  {"sham_smash10", {NULL, smash_fn}},
  {"ShamClaw", {NULL, claw_fn}},
  {"sham_swingl9", {NULL, swingl_fn}},
  {"sham_swingr9", {NULL, swingr_fn}},
  {"shambler_shrink", {NULL, shrink_fn}},
  {"shambler_dead", {NULL, dead_fn}},
};
const qa_q2m_definition qa_q2rm_shambler_definition = {
  .classname = "monster_shambler", .kind = "shambler", .model = "models/monsters/shambler/tris.md2",
  .health = 600, .gib_health = -60, .mass = 500, .scale = 1,
  .bounds = {{-32, -32, -24}, {32, 32, 64}},
  .initial_move = "shambler_move_stand",
  .moves = qa_q2rm_shambler_moves, .move_count = 9,
  .callbacks = shambler_callbacks, .callback_count = sizeof shambler_callbacks / sizeof shambler_callbacks[0],
  .stand = {(void *)"shambler_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"shambler_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn},
  .attack = {(void *)"shambler_attack_magic", qa_q2bm_move_handler_fn},
  .melee = {NULL, melee_fn},
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .idle = {(void *)&idle_spec, qa_q2bm_sound_handler_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
