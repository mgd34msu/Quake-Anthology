/* Port of src/content/q2/missionpacks/monsters/widow/common.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/widow/common.h"

#include <stdlib.h>

#include "../mp_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/missionpacks/monsters/power_armor.h"
#include "qa/content/q2/missionpacks/monsters/spawn.h"
#include "qa/foundation/js.h"

static const qa_bounds stalker_bounds = {{-28, -28, -18}, {28, 28, 18}};
qa_vec3 qa_q2mpm_widow_project(qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 offset) {
  qa_body_state body = q2mpm_body(game, entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  return qa_add3_double(body.origin, qa_add3_double(qa_scale3_double(axes.forward, offset.x),
      qa_add3_double(qa_scale3_double(axes.right, offset.y), qa_scale3_double(axes.up, offset.z))));
}
void qa_q2mpm_widow_slots(qa_q2m_context *context) {
  qa_q2_game_services *game = context->game;
  double skill = game->options.skill;
  context->state->monster_slots = skill < 2 ? 3 : skill == 2 ? 4 : 6;
  if (game->options.mode == QA_Q2_MODE_COOP) {
    size_t count = 0;
    const qa_actor_id *players = game->host->players(game->host->ctx, &count);
    double live = 0;
    for (size_t i = 0; i < count; i++)
      if (q2mpm_live(game, players[i]) && game->host->is_player(game->host->ctx, players[i])) live++;
    context->state->monster_slots = qa_js_min(6, context->state->monster_slots + skill * (live - 1));
  }
}
double qa_q2mpm_widow_slots_left(qa_q2m_context *context) {
  return context->state->monster_slots - context->state->monster_used;
}
static bool coop_target(qa_q2m_context *context, qa_actor_id *out) {
  qa_q2_game_services *game = context->game;
  size_t count = 0;
  const qa_actor_id *all = game->host->players(game->host->ctx, &count);
  qa_actor_id *candidates = malloc((count ? count : 1) * sizeof *candidates);
  if (!candidates) qa_raise("Error", 0, 0, "Out of memory in widow coop target");
  size_t found = 0;
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = all[i];
    if (q2mpm_live(game, actor) && game->host->is_player(game->host->ctx, actor) && qa_q2m_visible(context, &actor))
      candidates[found++] = actor;
  }
  if (found == 0) { free(candidates); return false; }
  double pick = qa_js_min((double)found - 1, floor(q2mpm_rnd(game) * (double)found));
  *out = q2mpm_at_a(candidates, found, pick);
  free(candidates);
  return true;
}
void qa_q2mpm_widow_summon(qa_q2m_context *context, qa_q2m_monsters *monsters, bool second, bool grow) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2m_state *state = context->state;
  for (int side_index = 0; side_index < 2; side_index++) {
    double side = side_index == 0 ? 1 : -1;
    qa_vec3 projected = qa_q2mpm_widow_project(entity, game,
                                               q2mpm_v3(30, side * (second ? 135 : 100), second ? 0 : 16));
    qa_vec3 point;
    if (!qa_q2mpm_find_rogue_spawn_point(game, projected, stalker_bounds, 64, &point)) continue;
    if (grow) {
      (void)qa_q2mpm_rogue_spawn_grow(game, point, 1);
      continue;
    }
    qa_q2_entity *child = qa_q2mpm_create_rogue_ground_monster(monsters, game, point,
                                                                q2mpm_body(game, entity).angles, stalker_bounds,
                                                                "monster_stalker", 256);
    if (!child) continue;
    qa_q2m_context *child_context = qa_q2m_monsters_context(monsters, child->actor.id);
    if (!child_context) qa_raise("Error", 0, 0, "Widow child lacks its shared monster controller");
    state->monster_used++;
    child_context->state->has_commander = true;
    child_context->state->commander = entity->actor.id;
    child->has_next_think = true;
    child->next_think = q2mpm_now(game);
    if (child->think.fn) child->think.fn(child->think.ctx, child, game);
    child_context->state->spawned_by = QA_Q2M_SPAWNED_WIDOW;
    child_context->state->do_not_count = true;
    child_context->state->ignore_shots = true;
    bool has_target = entity->has_enemy;
    qa_actor_id target = entity->enemy;
    if (game->options.mode == QA_Q2_MODE_COOP) {
      qa_actor_id picked;
      bool got = coop_target(child_context, &picked);
      if (got) { has_target = true; target = picked; } else has_target = false;
      if (has_target && entity->has_enemy && qa_same_actor(target, entity->enemy)) {
        got = coop_target(child_context, &picked);
        if (got) { has_target = true; target = picked; } else has_target = false;
      }
      if (!has_target) { has_target = entity->has_enemy; target = entity->enemy; }
    }
    if (has_target && q2mpm_live(game, target) && qa_q2m_health(game, &target) > 0) {
      child->has_enemy = true;
      child->enemy = target;
      qa_q2m_monsters_found_target(monsters, child_context);
      child_context->attack(child_context->impl_ctx, child_context);
    }
  }
}
static void set_invulnerable(qa_q2_game_services *game, qa_q2_entity *entity, bool invulnerable) {
  qa_combat_trait_changes traits;
  memset(&traits, 0, sizeof traits);
  traits.has_invulnerable = true;
  traits.invulnerable = invulnerable;
  qa_gameplay_authority_set_traits(game->host->combat, entity->actor, traits);
}
void qa_q2mpm_widow_clear_powerups(qa_q2m_context *context, qa_q2mpm_monster_state *source) {
  qa_q2mpm_rogue_state *power = qa_q2mpm_state_get(source, context->entity);
  power->widow_quad_until = 0;
  power->widow_double_until = 0;
  power->widow_invulnerable_until = 0;
  context->entity->effects = (double)(qa_js_to_int32(qa_js_to_uint32(context->entity->effects) & ~(32768u | 65536u | 134217728u)));
  set_invulnerable(context->game, context->entity, false);
}
static bool power_shown(double until, double now) {
  double remaining = qa_js_round((until - now) * 10);
  return remaining > 0 && (remaining > 30 || (qa_js_to_int32(remaining) & 4) != 0);
}
void qa_q2mpm_widow_powerups(qa_q2m_context *context, qa_q2mpm_services services, qa_q2mpm_monster_state *source) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  double now = q2mpm_now(game);
  double skill = game->options.skill;
  qa_q2mpm_rogue_state *own = qa_q2mpm_state_get(source, entity);
  /* TS armor() closure inlined at its call sites. */
  if (game->options.mode != QA_Q2_MODE_COOP) {
    if (entity->has_enemy) {
      qa_q2mpm_powerups other = services.powerups(services.ctx, entity->enemy);
      if (power_shown(other.quad_until, now)) {
        if (skill == 1) { own->widow_double_until = other.quad_until; source->widow_damage_multiplier = 2; }
        else if (skill >= 2) {
          own->widow_quad_until = other.quad_until;
          source->widow_damage_multiplier = 4;
          if (skill == 3 && qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, "q2:monster-power") <= 0)
            qa_q2mpm_monster_power_armor(context, "shield", 250 * skill);
        }
      } else if (power_shown(other.double_until, now)) {
        if (skill >= 2) {
          own->widow_double_until = other.double_until;
          source->widow_damage_multiplier = 2;
          if (skill == 3 && qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, "q2:monster-power") <= 0)
            qa_q2mpm_monster_power_armor(context, "shield", 250 * skill);
        }
      } else {
        source->widow_damage_multiplier = 1;
      }
      if (power_shown(other.invulnerability_until, now)) {
        if (skill == 1) {
          if (qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, "q2:monster-power") <= 0)
            qa_q2mpm_monster_power_armor(context, "shield", 250 * skill);
        } else if (skill >= 2) {
          own->widow_invulnerable_until = other.invulnerability_until;
          if (skill == 3 && qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, "q2:monster-power") <= 0)
            qa_q2mpm_monster_power_armor(context, "shield", 250 * skill);
        }
      }
    }
  } else {
    size_t count = 0;
    const qa_actor_id *all = game->host->players(game->host->ctx, &count);
    for (int field = 0; field < 3; field++) {
      bool responded = false;
      for (size_t i = 0; i < count; i++) {
        qa_actor_id actor = all[i];
        if (!q2mpm_live(game, actor) || !game->host->is_player(game->host->ctx, actor)) continue;
        qa_q2mpm_powerups other = services.powerups(services.ctx, actor);
        double until = field == 0 ? other.invulnerability_until : field == 1 ? other.quad_until : other.double_until;
        if (!power_shown(until, now)) continue;
        /* respond(actor) */
        if (power_shown(other.quad_until, now)) {
          if (skill == 1) { own->widow_double_until = other.quad_until; source->widow_damage_multiplier = 2; }
          else if (skill >= 2) {
            own->widow_quad_until = other.quad_until;
            source->widow_damage_multiplier = 4;
            if (skill == 3 && qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, "q2:monster-power") <= 0)
              qa_q2mpm_monster_power_armor(context, "shield", 250 * skill);
          }
        } else if (power_shown(other.double_until, now)) {
          if (skill >= 2) {
            own->widow_double_until = other.double_until;
            source->widow_damage_multiplier = 2;
            if (skill == 3 && qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, "q2:monster-power") <= 0)
              qa_q2mpm_monster_power_armor(context, "shield", 250 * skill);
          }
        } else {
          source->widow_damage_multiplier = 1;
        }
        if (power_shown(other.invulnerability_until, now)) {
          if (skill == 1) {
            if (qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, "q2:monster-power") <= 0)
              qa_q2mpm_monster_power_armor(context, "shield", 250 * skill);
          } else if (skill >= 2) {
            own->widow_invulnerable_until = other.invulnerability_until;
            if (skill == 3 && qa_shared_inventory_table_count(game->host->inventory, entity->actor.id, "q2:monster-power") <= 0)
              qa_q2mpm_monster_power_armor(context, "shield", 250 * skill);
          }
        }
        responded = true;
        break;
      }
      if (responded) break;
    }
  }
  set_invulnerable(game, entity, own->widow_invulnerable_until > now);
}
typedef struct power_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_monster_state *source;
} power_bundle;
static void power_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  power_bundle *bundle = ctx;
  qa_q2m_context *context = qa_q2m_monsters_context(bundle->monsters, entity->actor.id);
  if (!context) return;
  qa_q2mpm_rogue_state *power = qa_q2mpm_state_get(bundle->source, entity);
  double now = q2mpm_now(game);
  entity->effects = (double)(qa_js_to_int32(qa_js_to_uint32(entity->effects) & ~(32768u | 65536u | 134217728u)));
  if (qa_q2m_health(game, &entity->actor.id) > 0) {
    const double untils[3] = {power->widow_quad_until, power->widow_double_until, power->widow_invulnerable_until};
    const uint32_t flags[3] = {32768u, 134217728u, 65536u};
    for (int i = 0; i < 3; i++) {
      double remaining = qa_js_round((untils[i] - now) * 10);
      if (remaining > 0 && (remaining > 30 || (qa_js_to_int32(remaining) & 4) != 0))
        entity->effects = (double)(qa_js_to_int32(qa_js_to_uint32(entity->effects) | flags[i]));
    }
  }
  set_invulnerable(game, entity, power->widow_invulnerable_until > now);
}
qa_q2_think qa_q2mpm_widow_power_think(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters, qa_q2mpm_monster_state *source) {
  power_bundle *bundle = qa_q2mpm_arena_alloc(arena, sizeof *bundle);
  if (!bundle) qa_raise("Error", 0, 0, "Out of memory creating widow power think");
  bundle->monsters = monsters;
  bundle->source = source;
  qa_q2_think think = {bundle, power_think_fn};
  return think;
}
void qa_q2mpm_widow_restore_armor(qa_q2m_context *context) {
  size_t count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(context->game->host->inventory, context->entity->actor.id, &count);
  bool found = false;
  for (size_t i = 0; i < count; i++)
    if (strcmp(entries[i].item, "q2:monster-power") == 0) found = true;
  if (found) qa_q2mpm_restore_monster_power_armor(context);
}
void qa_q2mpm_widow_restore_armor_handler(void *ctx, qa_q2m_context *context) {
  (void)ctx;
  qa_q2mpm_widow_restore_armor(context);
}
