/* Port of src/content/q2/rerelease/monsters/base-variants/flipper.ts.
 * Rerelease m_flipper.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/flipper.h"

#include "qa/content/q2/base/monsters/flipper.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/flipper.h"
#include "../rrm_internal.h"

static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_alternate_fly_state *alt = &c->state->alt;
  alt->alternate_fly = true;
  alt->fly_thrusters = false;
  alt->fly_acceleration = 30;
  alt->fly_speed = 110;
  alt->fly_min_distance = 10;
  alt->fly_max_distance = 10;
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2 ? 1 : 0;
  if (q2rm_now(game) < state->pain_time) return;
  state->pain_time = q2rm_now(game) + 3;
  bool first = qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(c), 2) != 0;
  q2rm_snd(c, first ? "flipper/flppain1.wav" : "flipper/flppain2.wav", 2);
  if (!qa_q2rm_reacts_to_pain(c)) return;
  q2rm_set_move(c, first ? "flipper_move_pain1" : "flipper_move_pain2");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  if (qa_q2rm_check_gib(c)) {
    q2rm_snd(c, "misc/udeath.wav", 2);
    qa_q2m_gib_options plain = {false, false, false, false};
    qa_q2m_gib_options head = {true, false, false, false};
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/head2/tris.md2", damage, head);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2rm_snd(c, "flipper/flpdeth1.wav", 2);
  state->dead = true; state->can_take_damage = true;
  entity->server_flags = q2rm_or(entity->server_flags, 2);
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, "flipper_move_death");
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_corpse(c);
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2rm_v3(-16, -16, -8);
  change.bounds.max = q2rm_v3(16, 16, 8);
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static const qa_q2m_named_handler extra_callbacks[] = {
  {"flipper_dead", {NULL, dead_fn}},
};
static qa_q2m_definition flipper_definition;
static bool flipper_ready;
const qa_q2m_definition *qa_q2rm_get_rerelease_flipper_definition(void) {
  if (flipper_ready) return &flipper_definition;
  flipper_definition = qa_q2bm_flipper_definition;
  flipper_definition.moves = qa_q2rm_flipper_moves;
  flipper_definition.move_count = qa_q2rm_flipper_move_count;
  flipper_definition.bounds.min = q2rm_v3(-16, -16, -8);
  flipper_definition.bounds.max = q2rm_v3(16, 16, 20);
  flipper_definition.initialize = (qa_q2m_handler){NULL, init_fn};
  flipper_definition.reactions.pain = pain_fn;
  flipper_definition.reactions.die = die_fn;
  flipper_definition.callbacks = qa_q2rm_merge_callbacks(NULL, 
      flipper_definition.callbacks, flipper_definition.callback_count,
      extra_callbacks, 1, &flipper_definition.callback_count);
  flipper_ready = true;
  return &flipper_definition;
}
