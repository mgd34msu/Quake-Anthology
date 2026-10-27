/* Port of src/content/q2/missionpacks/monsters/combat.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/combat.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/core/random/q2_rerelease.h"

void qa_q2mpm_rogue_heal_effects(qa_q2m_context *context) {
  qa_q2f_entity *entity = context->entity;
  entity->effects = q2mpm_and(entity->effects, ~256);
  entity->render_flags = q2mpm_and(entity->render_flags, ~(1024 | 2048 | 4096));
  if (context->state->resurrecting) {
    entity->effects = q2mpm_or(entity->effects, 256);
    entity->render_flags = q2mpm_or(entity->render_flags, 1024);
  }
  context->game->show(context->game->ctx, entity);
}
void qa_q2mpm_cleanup_rogue_heal_target(qa_q2m_monsters *monsters, qa_q2mpm_monster_state *source,
                                        qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mpm_state_get(source, entity)->has_healer = false;
  q2mpm_set_damageable(game, entity, true);
  qa_q2m_context *patient = qa_q2m_monsters_context(monsters, entity->actor.id);
  if (patient) {
    patient->state->can_take_damage = true;
    patient->state->resurrecting = false;
    qa_q2mpm_rogue_heal_effects(patient);
  }
}

typedef struct hooks_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_monster_state *source;
  qa_q2mpm_services services;
} hooks_bundle;
static const char *tesla_class(qa_q2f_game *game) { return q2mpm_rr(game) ? "tesla_mine" : "tesla"; }
static bool is_good_guy_hook(void *ctx, qa_q2f_entity *entity) {
  hooks_bundle *bundle = ctx;
  qa_q2m_context *context = qa_q2m_monsters_context(bundle->monsters, entity->actor.id);
  return context ? context->state->good_guy : qa_q2mpm_state_get(bundle->source, entity)->good_guy;
}
static void heal_cleanup(hooks_bundle *bundle, qa_q2m_context *context) {
  qa_q2f_entity *target = context->entity->has_enemy
      ? context->game->entity(context->game->ctx, &context->entity->enemy) : NULL;
  if (target) qa_q2mpm_cleanup_rogue_heal_target(bundle->monsters, bundle->source, target, context->game);
  context->state->medic = false;
}
static void target_tesla(hooks_bundle *bundle, qa_q2m_context *context, qa_q2f_entity *tesla) {
  qa_q2f_entity *entity = context->entity;
  qa_q2f_game *game = context->game;
  qa_q2m_state *state = context->state;
  if (state->medic) heal_cleanup(bundle, context);
  if (entity->has_enemy && game->host->is_player(game->host->ctx, entity->enemy)) {
    qa_q2mpm_rogue_state *own = qa_q2mpm_state_get(bundle->source, entity);
    own->has_last_player_enemy = true;
    own->last_player_enemy = entity->enemy;
  }
  if (entity->has_enemy && qa_same_actor(entity->enemy, tesla->actor.id)) return;
  state->has_old_enemy = entity->has_enemy;
  state->old_enemy = entity->enemy;
  entity->has_enemy = true;
  entity->enemy = tesla->actor.id;
  if (!state->has_ranged_attack) {
    qa_q2m_monsters_found_target(bundle->monsters, context);
    return;
  }
  if (qa_q2m_health(game, &entity->actor.id) > 0) context->attack(context->impl_ctx, context);
}
static bool consume_blocked_hook(void *ctx, qa_q2m_context *context) {
  hooks_bundle *bundle = ctx;
  qa_q2mpm_rogue_state *state = qa_q2mpm_state_get(bundle->source, context->entity);
  bool blocked = state->blocked;
  state->blocked = false;
  return blocked;
}
static bool recover_enemy_hook(void *ctx, qa_q2m_context *context, qa_actor_id *out) {
  hooks_bundle *bundle = ctx;
  qa_q2mpm_rogue_state *state = qa_q2mpm_state_get(bundle->source, context->entity);
  if (!state->has_last_player_enemy || qa_q2m_health(context->game, &state->last_player_enemy) <= 0) return false;
  *out = state->last_player_enemy;
  state->has_last_player_enemy = false;
  return true;
}
static qa_q2m_before_move_result before_move_hook(void *ctx, qa_q2m_context *context, qa_vec3 displacement) {
  hooks_bundle *bundle = ctx;
  qa_q2f_game *game = context->game;
  qa_q2f_entity *entity = context->entity;
  qa_q2m_state *state = context->state;
  qa_q2m_before_move_result result = {false, displacement};
  qa_q2mpm_rogue_state *metadata = qa_q2mpm_state_get(bundle->source, entity);
  if (qa_q2m_health(game, &entity->actor.id) <= 0) return result;
  qa_q2f_entity *current = bundle->services.bad_area_entity(bundle->services.ctx, entity->actor.id, NULL);
  if (current) {
    metadata->has_bad_area = true;
    metadata->bad_area = current->actor.id;
    qa_q2f_entity *enemy = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
    if (enemy && strcmp(enemy->classname, tesla_class(game)) == 0) {
      qa_body_state body = q2mpm_body(game, entity);
      qa_vec3 forward = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward;
      double bad_dot = qa_dot3_double(forward, qa_normalize3_double_hypot(
          qa_sub3_double(q2mpm_body(game, current).origin, body.origin)));
      double move_dot = qa_dot3_double(forward, qa_normalize3_double_hypot(displacement));
      if ((bad_dot < 0 && move_dot < 0) || (bad_dot > 0 && move_dot > 0)) {
        result.displacement = qa_scale3_double(displacement, -1);
        return result;
      }
    }
  } else if (metadata->has_bad_area) {
    metadata->has_bad_area = false;
    if (state->has_old_enemy) {
      entity->has_enemy = true;
      entity->enemy = state->old_enemy;
      entity->has_goal = true;
      entity->goal = state->old_enemy;
      qa_q2m_monsters_found_target(bundle->monsters, context);
      result.handled = true;
      return result;
    }
  }
  return result;
}
static bool accepts_ground_hook(void *ctx, qa_q2m_context *context, qa_vec3 origin) {
  hooks_bundle *bundle = ctx;
  qa_q2f_entity *entity = context->entity;
  qa_q2f_game *game = context->game;
  if (qa_q2m_health(game, &entity->actor.id) <= 0 || qa_q2mpm_state_get(bundle->source, entity)->has_bad_area)
    return true;
  qa_q2f_entity *area = bundle->services.bad_area_entity(bundle->services.ctx, entity->actor.id, &origin);
  if (!area) return true;
  qa_q2f_entity *owner = area->has_owner ? game->entity(game->ctx, &area->owner) : NULL;
  qa_q2f_entity *enemy = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
  const char *classic_tesla = game->options.edition == QA_Q2F_CLASSIC ? "telsa" : "tesla_mine";
  if (owner && strcmp(owner->classname, tesla_class(game)) == 0
      && (enemy == NULL || (strcmp(enemy->classname, classic_tesla) != 0
          && (!game->host->is_player(game->host->ctx, enemy->actor.id) || !qa_q2m_visible_enemy(context))))) {
    target_tesla(bundle, context, owner);
    qa_q2mpm_state_get(bundle->source, entity)->blocked = true;
  }
  return false;
}
static void before_killed_hook(void *ctx, qa_q2m_context *context) {
  hooks_bundle *bundle = ctx;
  if (context->state->medic) heal_cleanup(bundle, context);
}
static bool before_react_hook(void *ctx, qa_q2m_context *context, qa_actor_id attacker) {
  hooks_bundle *bundle = ctx;
  qa_q2f_entity *entity = context->entity;
  qa_q2f_game *game = context->game;
  qa_q2m_state *state = context->state;
  const qa_actor_id *inflictor_id = entity->last_attack ? entity->last_attack->inflictor : NULL;
  qa_q2f_entity *inflictor = inflictor_id ? game->entity(game->ctx, inflictor_id) : NULL;
  if (inflictor && strcmp(inflictor->classname, tesla_class(game)) == 0) {
    bool marked = bundle->services.mark_tesla_area(bundle->services.ctx, entity, inflictor);
    if (!q2mpm_rr(game)) {
      if (marked) target_tesla(bundle, context, inflictor);
    } else {
      qa_q2_rerelease_random *random = game->host->rerelease_random;
      if (!random) qa_q2f_error("Rerelease Tesla reaction requires the shared source RNG");
      qa_q2f_entity *enemy = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
      bool enemy_tesla = enemy && strcmp(enemy->classname, "tesla_mine") == 0;
      if ((marked || (qa_q2_rerelease_random_next_uint32(random) & 1u) != 0) && !enemy_tesla)
        target_tesla(bundle, context, inflictor);
    }
    return true;
  }
  if (qa_same_actor(attacker, entity->actor.id) || (entity->has_enemy && qa_same_actor(attacker, entity->enemy)))
    return false;
  qa_q2f_entity *other = game->entity(game->ctx, &attacker);
  if (state->good_guy && (game->host->is_player(game->host->ctx, attacker) || (other && is_good_guy_hook(bundle, other))))
    return false;
  double percent = qa_q2m_health(game, &entity->actor.id) / entity->max_health;
  if (entity->has_enemy && state->target_anger) {
    if (q2mpm_live(game, entity->enemy) && percent > 0.33) return true;
    state->target_anger = false;
  }
  if (q2mpm_rr(game) && qa_q2mpm_state_get(bundle->source, entity)->react_to_damage_time > q2mpm_now(game)) return true;
  if (entity->has_enemy && state->medic) {
    if (q2mpm_live(game, entity->enemy) && percent > 0.25) return true;
    heal_cleanup(bundle, context);
  }
  if (q2mpm_rr(game)) {
    qa_q2_rerelease_random *random = game->host->rerelease_random;
    if (!random) qa_q2f_error("Rerelease damage reaction requires the shared source RNG");
    qa_q2mpm_state_get(bundle->source, entity)->react_to_damage_time =
        q2mpm_now(game) + (double)qa_q2_rerelease_random_time_milliseconds(random, 3000, 5000) / 1000;
  }
  return false;
}
qa_q2m_source_combat_hooks qa_q2mpm_create_rogue_combat_hooks(qa_q2m_monsters *monsters,
                                                              qa_q2mpm_monster_state *source,
                                                              qa_q2mpm_services services) {
  hooks_bundle *bundle = malloc(sizeof *bundle);
  if (!bundle) qa_q2f_error("Out of memory creating rogue combat hooks");
  bundle->monsters = monsters;
  bundle->source = source;
  bundle->services = services;
  qa_q2m_source_combat_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = bundle;
  hooks.is_good_guy = is_good_guy_hook;
  hooks.before_react = before_react_hook;
  hooks.before_killed = before_killed_hook;
  hooks.recover_enemy = recover_enemy_hook;
  hooks.before_move = before_move_hook;
  hooks.accepts_ground_move = accepts_ground_hook;
  hooks.consume_blocked = consume_blocked_hook;
  return hooks;
}
void qa_q2mpm_rogue_target_anger(qa_q2m_monsters *monsters, qa_q2mpm_monster_state *source, qa_q2f_entity *entity,
                                 qa_q2f_entity *target, qa_q2f_game *game) {
  if (!q2mpm_live(game, entity->actor.id) || !q2mpm_live(game, target->actor.id)) return;
  qa_q2m_context *target_context = qa_q2m_monsters_context(monsters, target->actor.id);
  if (target_context) target_context->state->good_guy = true;
  else qa_q2mpm_state_get(source, target)->good_guy = true;
  qa_q2m_context *context = qa_q2m_monsters_context(monsters, entity->actor.id);
  if (!context) return;
  entity->has_enemy = true;
  entity->enemy = target->actor.id;
  context->state->target_anger = true;
  qa_q2m_monsters_found_target(monsters, context);
}
