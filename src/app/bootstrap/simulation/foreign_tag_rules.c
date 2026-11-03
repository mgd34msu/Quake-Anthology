/* Port of src/app/bootstrap/simulation/foreign-tag-rules.ts (TagRules): moved out of
 * shared_simulation_worlds.c as TS moved it out of runtime.ts (no behavior change). The rules run over the
 * simulation, which is the TS ForeignMatchHost; the shared helpers are in foreign_match_host.c
 * (foreign_match_host.h). GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/simulation/provider_frames.h"
#include "qa/app/bootstrap/simulation/runtime.h"
#include "qa/app/bootstrap/simulation/monster_sources.h"
#include "qa/app/bootstrap/simulation/monster_placement.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include "qa/content/catalog/monsters.h"
#include "qa/content/q1/base/monsters.h"
#include "qa/content/q2/rerelease/players.h"
#include "qa/content/q1/foundation/runtime.h"
#include "qa/content/composition/q1_hipnotic_supply.h"
#include "qa/content/composition/q1_q2_supply.h"
#include "qa/content/composition/q1_q3_supply.h"
#include "qa/content/monsters/definitions.h"
#include "qa/content/q1/base/creatures.h"
#include "qa/content/q1/base/species.h"
#include "qa/content/q1/foundation/pickups.h"
#include "qa/app/bootstrap/simulation/native_mod_host.h"
#include "qa/bots/navigation/rerelease_path.h"
#include "qa/content/catalog/timing.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/network/common/js_number.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/records.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/formats/q3_map/entities.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/world/actors/body.h"
#include "qa/content/q3/base/combat_bridge.h"
#include "qa/content/composition/q2_q3_items.h"
#include "foreign_match_host.h"

/* ---- Runtime Tag (5924-6057) ---- */
bool qa_app_shared_sim_foreign_tag(qa_app_shared_simulation *sim) {
  return qa_ss_is(sim->recipe.match.provider, "q2:tag") && qa_fm_deathmatch(sim) && qa_fm_family_is_not_q2_map(sim);
}
static void wl_tag_token_touched(void *ctx, qa_touch_contact contact);
void qa_app_shared_sim_spawn_tag_token(qa_app_shared_simulation *sim, const qa_actor_id *from) {
  qa_vec3 origin;
  if (from) { qa_body_state body; if (!qa_app_shared_sim_body(sim, *from, &body)) return; origin = body.origin; }
  else if (!qa_app_shared_sim_farthest_deathmatch_spawn(sim, &origin)) return;
  qa_bounds bounds = {{-15, -15, -15}, {15, 15, 15}};
  qa_owned_actor actor = qa_fm_spawn_trigger(sim, "q2:dm_tag_token", (qa_vec3){origin.x, origin.y, origin.z + 16}, bounds, wl_tag_token_touched);
  qa_fm_detached_model(sim, actor, "models/items/tagtoken/tris.md2");
  qa_shared_body_table_link(qa_fm_bodies(sim), actor, NULL);
  sim->has_tag_token = true; sim->tag_token = actor;
  sim->has_tag_dropped_at = true; sim->tag_dropped_at = qa_fm_time(sim);
}
void qa_app_shared_sim_add_tag_score(qa_app_shared_simulation *sim, qa_actor_id actor, double amount) {
  qa_source_match_player_result match = qa_app_shared_sim_primary_match_player(sim, actor);
  if (match.has_value) match.value.set_score(match.value.ctx, match.value.score(match.value.ctx) + amount);
}
void qa_app_shared_sim_give_tag_inventory(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_owned_actor owned;
  if (qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned)) qa_shared_inventory_table_give(sim->inventory, owned, "q2:dm_tag_token", 1);
}
void qa_app_shared_sim_take_tag_inventory(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_owned_actor owned;
  if (qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned)) qa_shared_inventory_table_consume(sim->inventory, owned, "q2:dm_tag_token", 1);
}
/* Native Tag heals the new owner and touches a body-armor pickup. */
void qa_app_shared_sim_tag_bonus(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_owned_actor owned; qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  if (!qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned) || !combat.has_value) return;
  if (combat.value.health < 100) qa_gameplay_authority_set_health(sim->combat, owned, qa_js_min(100, combat.value.health + 200));
  qa_regular_armor_state regular = combat.value.armor.regular;
  qa_armor_state armor = combat.value.armor;
  if (regular.kind == QA_REGULAR_ARMOR_NONE) {
    qa_regular_armor_state next; memset(&next, 0, sizeof next);
    if (qa_app_sim_provider_family(sim->recipe.map.entities.provider) == QA_GAME_FAMILY_Q1) {
      next.kind = QA_REGULAR_ARMOR_Q1; next.points = 100; next.absorption = 0.3; next.item = "q1:item_armor1";
    } else { next.kind = QA_REGULAR_ARMOR_Q3; next.points = 100; next.protection = (float)0.66; }
    armor.regular = next;
    qa_gameplay_authority_set_armor(sim->combat, owned, armor);
  } else if (regular.points < 100) {
    armor.regular.points = 100;
    qa_gameplay_authority_set_armor(sim->combat, owned, armor);
  }
}
/* foreignMatchHost grantQuad: actor => this.strategy.grantQuad(actor) */
void qa_app_shared_sim_grant_tag_quad(qa_app_shared_simulation *sim, qa_actor_id actor) {
  sim->strategy->grant_quad(sim, actor);
}
/* Q3Strategy.grantQuad */
void qa_ss_q3_grant_quad(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q3_game_entity *native = qa_fm_q3_native(sim, actor);
  if (native && native->client)
    qa_q3_base_player_state_slots_set(&native->client->ps.powerups, QA_Q3_PW_QUAD, qa_app_q3_source_runtime_level_time(sim->source.q3) + 30000);
}
static void wl_tag_token_touched(void *ctx, qa_touch_contact contact) {
  qa_app_shared_simulation *sim = ctx;
  if (!sim->has_tag_token || !qa_same_actor(contact.self.id, sim->tag_token.id)) return;
  /* The native token is untouchable for a second after it drops. */
  if (qa_fm_time(sim) - (sim->has_tag_dropped_at ? sim->tag_dropped_at : 0) < 1) return;
  qa_actor_id player = contact.other;
  if (qa_app_shared_sim_player(sim, player) == NULL || qa_fm_health(sim, player) <= 0) return;
  qa_owned_actor token = sim->tag_token;
  sim->has_tag_token = false; sim->has_tag_dropped_at = false;
  qa_session_actor_registry_release(sim->actors, token);
  sim->has_tag_owner = true; sim->tag_owner = player; sim->tag_count = 0;
  qa_app_shared_sim_give_tag_inventory(sim, player); qa_app_shared_sim_tag_bonus(sim, player);
}
void qa_app_shared_sim_tag_token_touched(qa_app_shared_simulation *sim, qa_touch_contact contact) { wl_tag_token_touched(sim, contact); }
void qa_app_shared_sim_tick_tag(qa_app_shared_simulation *sim) {
  if (!qa_app_shared_sim_foreign_tag(sim)) return;
  size_t player_count; qa_actor_id *players = qa_fm_players(sim, &player_count);
  if (!sim->has_tag_token && !sim->has_tag_owner) qa_app_shared_sim_spawn_tag_token(sim, NULL);
  if (sim->has_tag_token && sim->has_tag_dropped_at && qa_fm_time(sim) - sim->tag_dropped_at >= 30) {
    /* An untouched token returns to the spawn farthest from every player. */
    qa_vec3 spot; bool has_spot = qa_app_shared_sim_farthest_deathmatch_spawn(sim, &spot);
    qa_body_state body; bool has_body = qa_app_shared_sim_body(sim, sim->tag_token.id, &body);
    if (has_spot && has_body) {
      body.origin = (qa_vec3){spot.x, spot.y, spot.z + 16}; body.velocity = qa_ss_zero;
      qa_shared_body_table_write(qa_fm_bodies(sim), sim->tag_token, body);
      qa_shared_body_table_link(qa_fm_bodies(sim), sim->tag_token, NULL);
    }
    sim->tag_dropped_at = qa_fm_time(sim);
  }
  if (sim->has_tag_owner && !qa_fm_includes(players, player_count, sim->tag_owner)) {
    sim->has_tag_owner = false; sim->tag_count = 0; qa_app_shared_sim_spawn_tag_token(sim, NULL);
  }
  for (size_t i = 0; i < player_count; i++) {
    qa_actor_id actor = players[i];
    double health = qa_fm_health(sim, actor);
    double *previous_value = qa_ss_map_get(&sim->tag_health, &actor);
    bool has_previous = previous_value != NULL; double previous = has_previous ? *previous_value : 0;
    qa_ss_map_set(&sim->tag_health, &actor, &health);
    if (!has_previous || previous <= 0 || health > 0) continue;
    qa_owned_actor owned; bool has_attacker = false; qa_actor_id attacker;
    if (qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned)) {
      qa_attack_provenance *attack = qa_ss_map_get(&sim->last_attack, &owned);
      if (attack && attack->attacker) { has_attacker = true; attacker = *attack->attacker; }
    }
    bool has_killer = has_attacker && !qa_same_actor(attacker, actor) && qa_fm_includes(players, player_count, attacker);
    qa_actor_id killer = has_killer ? attacker : actor;
    if (sim->has_tag_owner && qa_same_actor(actor, sim->tag_owner)) {
      qa_app_shared_sim_take_tag_inventory(sim, actor);
      if (has_killer) qa_app_shared_sim_add_tag_score(sim, killer, 4);
      if (has_killer && qa_fm_health(sim, killer) > 0) {
        sim->tag_owner = killer; sim->tag_count = 0;
        qa_app_shared_sim_give_tag_inventory(sim, killer); qa_app_shared_sim_tag_bonus(sim, killer);
      } else {
        sim->has_tag_owner = false; sim->tag_count = 0; qa_app_shared_sim_spawn_tag_token(sim, &actor);
      }
    } else if (has_killer && sim->has_tag_owner && qa_same_actor(killer, sim->tag_owner)) {
      qa_app_shared_sim_add_tag_score(sim, killer, 2);
      if (++sim->tag_count == 5) { sim->tag_count = 0; qa_app_shared_sim_grant_tag_quad(sim, killer); }
    }
  }
  free(players);
}
