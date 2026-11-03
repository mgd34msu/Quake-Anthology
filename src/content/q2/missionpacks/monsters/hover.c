/* Port of src/content/q2/missionpacks/monsters/hover.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/hover.h"

#include <stdio.h>
#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/hover.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/missionpacks/monsters/power_armor.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_hover.h"
#include "qa/content/q2/rerelease/monsters/common.h"

typedef struct hover_bundle {
  qa_q2mpm_weapons weapons;
  qa_q2mpm_monster_state *source;
} hover_bundle;
static void dead_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)ctx;
  qa_body_state body = game->body(game->ctx, entity->actor);
  if (body.ground == NULL && q2mpm_now(game) < entity->timestamp) {
    qa_q2_think think = {NULL, dead_think_fn};
    game->schedule(game->ctx, entity, 0.1, think);
    return;
  }
  q2mpm_emit_effect(game, "q2:explosion1", body.origin, q2mpm_zero(), 1, 0);
  game->remove(game->ctx, entity);
}
/* hover_dying (rerelease DLL frame callback of hover_move_death1): a grounded corpse explodes now, else a coin
 * flip throws a plain explosion and an organic or metallic gib. */
static void dying_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  if (game->body(game->ctx, entity->actor).ground != NULL) { dead_think_fn(NULL, entity, game); return; }
  if (qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(c), 2) != 0) return;
  q2mpm_emit_effect(game, "q2:plain-explosion", game->body(game->ctx, entity->actor).origin, q2mpm_zero(), 1, 0);
  bool organic = qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(c), 2) != 0;
  qa_q2m_gib_options options = {false, !organic, false, false};
  (void)qa_q2m_throw_gib(entity, game, organic ? "models/objects/gibs/sm_meat/tris.md2" : "models/objects/gibs/sm_metal/tris.md2", 120, options);
}
static void hover_path(qa_q2m_context *c, const char *suffix, char *buffer, size_t size) {
  size_t __maxlen = size;
  char * value = qa_q2mpm_monster_mass(c) < 225 ? "hover" : "daedalus";
  char * value2 = qa_q2mpm_monster_mass(c) < 225 ? "hov" : "daed";
  snprintf(buffer, __maxlen, "%s/%s%s.wav", value,
           value2, suffix);
}
static void sight_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  char buffer[64];
  hover_path(c, "sght1", buffer, sizeof buffer);
  q2mpm_snd(c, buffer, 2);
}
static void search_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  char buffer[64];
  hover_path(c, q2mpm_rnd(c->game) < 0.5 ? "srch1" : "srch2", buffer, sizeof buffer);
  q2mpm_snd(c, buffer, 2);
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  if (qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2) entity->skin = q2mpm_or(entity->skin, 1);
  if (q2mpm_now(game) < c->state->pain_time) return;
  c->state->pain_time = q2mpm_now(game) + 3;
  if (game->options.skill == 3) return;
  bool first = q2mpm_rnd(game) < (reaction.damage <= 25 ? 0.5 : 0.45 - 0.1 * game->options.skill);
  char buffer[64];
  hover_path(c, first ? "pain1" : "pain2", buffer, sizeof buffer);
  q2mpm_snd(c, buffer, 2);
  q2mpm_set_move(c, first ? (reaction.damage <= 25 ? "hover_move_pain3" : "hover_move_pain1") : "hover_move_pain2");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  c->entity->effects = 0;
  qa_armor_state armor;
  memset(&armor, 0, sizeof armor);
  armor.powered.kind = "none";
  qa_gameplay_authority_set_armor(c->game->host->combat, c->entity->actor, armor);
  if (qa_q2bm_standard_gib(c, reaction, 2, 2, "models/objects/gibs/sm_meat/tris.md2", 1) || c->state->dead) return;
  char buffer[64];
  hover_path(c, q2mpm_rnd(c->game) < 0.5 ? "deth1" : "deth2", buffer, sizeof buffer);
  qa_q2bm_begin_death_default(c, reaction, buffer, "hover_move_death1");
}
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  hover_bundle *bundle = ctx;
  (void)distance;
  return qa_q2mpm_rogue_blocked_check_shot(c, 0.25 + 0.05 * c->game->options.skill, bundle->source);
}
static void attack_cb_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double skill = c->game->options.skill;
  double chance = (skill == 0 ? 0 : 1 - 0.5 / skill) + (qa_q2mpm_monster_mass(c) > 150 ? 0.1 : 0);
  if (q2mpm_rnd(c->game) > chance) {
    c->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
    q2mpm_set_move(c, "hover_move_attack1");
    return;
  }
  if (q2mpm_rnd(c->game) <= 0.5) c->state->lefty = !c->state->lefty;
  c->state->attack_state = QA_Q2M_ATTACK_SLIDING;
  q2mpm_set_move(c, "hover_move_attack2");
}
static void reattack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2bm_alive_enemy(c) && qa_q2m_visible_enemy(c) && q2mpm_rnd(c->game) <= 0.6) {
    if (c->state->attack_state == QA_Q2M_ATTACK_STRAIGHT) { q2mpm_set_move(c, "hover_move_attack1"); return; }
    if (c->state->attack_state == QA_Q2M_ATTACK_SLIDING) { q2mpm_set_move(c, "hover_move_attack2"); return; }
    char message[96];
    snprintf(message, sizeof message, "hover_reattack: unexpected state %s",
             qa_q2m_attack_state_name(c->state->attack_state));
    c->game->host->diagnostic(c->game->host->ctx, message);
  }
  q2mpm_set_move(c, "hover_move_end_attack");
}
static void fire_blaster_fn(void *ctx, qa_q2m_context *c) {
  hover_bundle *bundle = ctx;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 62, &aim)) return;
  bool daedalus = qa_q2mpm_monster_mass(c) >= 200;
  if (daedalus)
    (void)bundle->weapons.fire_blaster2(bundle->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 1, 1000, 8);
  else
    (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 1, 1000,
                                  c->entity->frame == qa_q2mpm_rogue_hover_frames_value.attak104 ? 64 : 0,
                                  false, false, 0);
  qa_q2rm_monster_flash(c, daedalus ? 145 : 62, aim.start, aim.direction);
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->corpse = true;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2mpm_v3(-16, -16, -24);
  change.bounds.max = q2mpm_v3(16, 16, -8);
  q2mpm_move_body(c->game, c->entity, change, false, false);
  c->game->motion(c->game->ctx, c->entity, "toss");
  c->entity->timestamp = q2mpm_now(c->game) + 15;
  qa_q2_think think = {NULL, dead_think_fn};
  c->game->schedule(c->game->ctx, c->entity, 0.1, think);
}
static void daedalus_init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2mpm_monster_power_armor(c, "screen", 100);
  qa_q2bm_loop_sound(c, "daedalus/daedidle1.wav");
}
static void daedalus_after_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->skin = 2;
  c->game->show(c->game->ctx, c->entity);
}
static const qa_q2_named_think hover_thinks[] = {{"q2:rogue/hover_deadthink", {NULL, dead_think_fn}}};
size_t qa_q2mpm_create_rogue_hover_definitions(qa_q2mpm_arena *arena, qa_q2m_definition *out, qa_q2mpm_weapons weapons,
                                               qa_q2mpm_monster_state *source) {
  hover_bundle *bundle = qa_q2mpm_arena_alloc(arena, sizeof *bundle);
  if (!bundle) qa_raise("Error", 0, 0, "Out of memory creating rogue hover definitions");
  bundle->weapons = weapons;
  bundle->source = source;
  qa_q2m_definition definition = qa_q2bm_hover_definition;
  definition.moves = qa_q2mpm_rogue_hover_moves;
  definition.move_count = qa_q2mpm_rogue_hover_move_count;
  memset(&definition.source_callbacks, 0, sizeof definition.source_callbacks);
  definition.has_source_callbacks = true;
  definition.source_callbacks.think = hover_thinks;
  definition.source_callbacks.think_count = 1;
  definition.sight = (qa_q2m_handler){NULL, sight_fn};
  definition.search = (qa_q2m_handler){NULL, search_fn};
  definition.reactions.ctx = NULL;
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.blocked_ctx = bundle;
  definition.blocked = blocked_fn;
  const qa_q2m_named_handler extra[] = {
    {"hover_attack", {NULL, attack_cb_fn}},
    {"hover_reattack", {NULL, reattack_fn}},
    {"hover_fire_blaster", {bundle, fire_blaster_fn}},
    {"hover_dying", {NULL, dying_fn}},
    {"hover_dead", {NULL, dead_fn}},
  };
  definition.callbacks = qa_q2mpm_merge_callbacks(arena, definition.callbacks, definition.callback_count, extra, 5,
                                                  &definition.callback_count);
  out[0] = definition;
  out[1] = definition;
  out[1].classname = "monster_daedalus";
  out[1].kind = "daedalus";
  out[1].health = 450;
  out[1].mass = 225;
  out[1].has_yaw_speed = true;
  out[1].yaw_speed = 25;
  out[1].initialize = (qa_q2m_handler){NULL, daedalus_init_fn};
  out[1].after_spawn = (qa_q2m_handler){NULL, daedalus_after_fn};
  out[1].restore = (qa_q2m_handler){NULL, qa_q2mpm_restore_monster_power_armor_handler};
  return 2;
}
