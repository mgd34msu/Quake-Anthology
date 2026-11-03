/*
 * Port of src/content/composition/q2/index.ts: Q2ProductRuntime, one Q2
 * source runtime built from the real Q2 modules (Q2Foundation, Q2Weapons,
 * Q2Monsters, mover/item/target/scenery modules, Q2Players or
 * Q2RereleasePlayers, base entities and monsters, the mission-pack armory,
 * entities and monsters, the rerelease module and monsters, and the
 * selected match mode). The session calls its player and entity phases
 * from its own scheduler. The TS file re-exports types.ts and save.ts; see
 * those headers. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q2_INDEX_H
#define QA_CONTENT_COMPOSITION_Q2_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/composition/q2/match.h"
#include "qa/content/composition/q2/types.h"
#include "qa/content/q2/base/entities/index.h"
#include "qa/content/q2/base/monsters/index.h"
#include "qa/content/q2/base/player/index.h"
#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/foundation/movers.h"
#include "qa/content/q2/foundation/runtime.h"
#include "qa/content/q2/missionpacks.h"
#include "qa/content/q2/missionpacks/monsters/index.h"
#include "qa/content/q2/rerelease/index.h"
#include "qa/content/q2/rerelease/monsters/index.h"
#include "qa/content/q2/rerelease/players.h"

#define QA_Q2_PRODUCT_MAX_EXPANSIONS 2
#define QA_Q2_PRODUCT_MAX_MODULES 16

struct qa_q2_product_runtime;
/** TS Q2ProductExpansion. hooks_ctx backs the per-pack closures. */
typedef struct qa_q2_product_expansion {
  qa_q2mp_pack pack;
  qa_q2mp_entities entities;
  qa_q2mpm_monsters_module monsters;
  struct { struct qa_q2_product_runtime *runtime; size_t index; } hooks_ctx;
} qa_q2_product_expansion;

/** TS Q2ProductRerelease. */
typedef struct qa_q2_product_rerelease {
  qa_q2_rerelease_players players;
  qa_q2_rerelease_module entities;
  qa_q2rm_monsters_module monsters;
} qa_q2_product_rerelease;

/** TS Q2ProductRuntime. Created on the heap; every closure the modules
 * hold points back at it. armory, rerelease and rogue_spawns are NULL for
 * TS null; has_movement_stop_speed false is TS null. */
typedef struct qa_q2_product_runtime {
  qa_q2_composition_options configuration;
  qa_q2_entity_services *game;      /* TS Q2Foundation */
  qa_q2_game_services *services;    /* game's Q2GameServices view */
  qa_q2w_weapons *weapons;
  qa_q2m_monsters monsters;
  qa_q2_mover_module *movers;
  qa_q2_item_module *items;
  qa_q2_players *players;
  qa_q2_base_entity_module *base_entities;
  qa_q2bm_classic_base_module base_monsters;
  qa_q2mp_armory *armory;
  qa_q2_product_expansion expansions[QA_Q2_PRODUCT_MAX_EXPANSIONS];
  size_t expansion_count;
  qa_q2_product_rerelease *rerelease;
  qa_q2_spawn_module modules[QA_Q2_PRODUCT_MAX_MODULES];
  size_t module_count;
  qa_q2_product_match match;
  bool has_movement_stop_speed;
  double movement_stop_speed;
  qa_q2mp_rogue_player_spawns *rogue_spawns;
  uint32_t active_deathmatch_flags; /* TS activeRules.deathmatchFlags */
  /* Storage behind module views whose TS callbacks are getters: the
   * snapshot is refreshed from the getter before every registration. */
  qa_q2_callback_definitions module_callbacks[QA_Q2_PRODUCT_MAX_MODULES];
  qa_q2_callback_definitions (*module_callbacks_getter[QA_Q2_PRODUCT_MAX_MODULES])(void *module);
  qa_q2_rerelease_options rerelease_options;
} qa_q2_product_runtime;

/** TS new Q2ProductRuntime(configuration) / createQ2ProductRuntime.
 * Throws the TS Errors (qa_raise "Error") for a non-deathmatch Tag or
 * DeathBall admission and missing rerelease arsenal state. NULL on OOM. */
qa_q2_product_runtime *qa_q2_product_runtime_create(const qa_q2_composition_options *configuration);
qa_q2_product_runtime *qa_create_q2_product_runtime(const qa_q2_composition_options *options);
void qa_q2_product_runtime_destroy(qa_q2_product_runtime *runtime);

void qa_q2_product_runtime_set_deathmatch_flags(qa_q2_product_runtime *runtime, double flags);
/* TS get originalSourceFallbacks: writes up to capacity names (static
 * strings) and returns the total count. */
size_t qa_q2_product_runtime_original_source_fallbacks(const qa_q2_product_runtime *runtime, const char **out,
                                                       size_t capacity);
void qa_q2_product_runtime_register_callbacks(qa_q2_product_runtime *runtime);
/* landmark NULL = TS null. */
qa_q2_entity *qa_q2_product_runtime_admit(qa_q2_product_runtime *runtime, qa_owned_actor actor,
                                          qa_q2_player_admission admission, const qa_q2_landmark_carry *landmark);
qa_q2w_input qa_q2_product_runtime_weapon_input(qa_q2_product_runtime *runtime, qa_actor_id actor, qa_q2w_input input);
qa_q2_composition_powerups qa_q2_product_runtime_powerups(qa_q2_product_runtime *runtime, qa_actor_id actor);
void qa_q2_product_runtime_record_damage(qa_q2_product_runtime *runtime, qa_q2_entity *entity,
                                         qa_damage_decision decision);
void qa_q2_product_runtime_before_reaction(qa_q2_product_runtime *runtime, qa_actor_id actor,
                                           const qa_attack_provenance *attack);
void qa_q2_product_runtime_movement_impact(qa_q2_product_runtime *runtime, qa_actor_id actor, double impact_delta,
                                           bool on_ladder);
void qa_q2_product_runtime_record_foreign_weapon_fire(qa_q2_product_runtime *runtime, qa_actor_id actor);
void qa_q2_product_runtime_after_player_frames(qa_q2_product_runtime *runtime);
void qa_q2_product_runtime_after_spawn(qa_q2_product_runtime *runtime);
void qa_q2_product_runtime_check_rules(qa_q2_product_runtime *runtime);

#endif
