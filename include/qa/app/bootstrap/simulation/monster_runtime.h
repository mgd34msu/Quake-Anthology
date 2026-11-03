/* Port of src/app/bootstrap/simulation/monster-runtime.ts: selected-monster
 * admission over an authored map program. Authored links and counters stay
 * in the map program (Q1 or Q2 foundation) while behavior attaches to the
 * selected actor through SelectedMonsterBehavior. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_MONSTER_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_MONSTER_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/monster_checkpoint.h"
#include "qa/app/bootstrap/simulation/q2_monster_sources.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/content/monsters/authored.h"
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/foundation/runtime.h"
#include "qa/contracts/content.h"
#include "qa/contracts/time.h"
#include "qa/formats/q1_map/types.h"

/* TS SelectedMonsterSource: reference, random and clock plus the q1
 * (game, addon?) or q2 (SelectedQ2MonsterModules) source modules. */
typedef struct qa_app_sim_selected_monster_source {
  qa_provider_reference reference;
  qa_app_sim_source_random *random;
  struct { qa_frame_context frame; bool advanced; } clock;
  const char *kind; /* "q1" | "q2" */
  qa_q1_services *game;          /* q1 */
  qa_q1ad_context *addon;        /* q1; NULL = TS undefined */
  qa_app_sim_selected_q2_monster_modules *q2; /* q2 (owned; game destroyed first) */
} qa_app_sim_selected_monster_source;

/* TS SelectedMonsterBehavior. relocated NULL = TS undefined; activator and
 * goal NULL = TS null; enemy/old_enemy false = TS null. The mission is
 * owned by the selected-monster runtime and outlives the actor. */
typedef struct qa_app_sim_selected_monster_behavior {
  void *ctx;
  void (*attach)(void *ctx, qa_owned_actor actor, qa_monster_definition_reference definition, const qa_monster_mission *mission);
  bool (*validate_placement)(void *ctx, qa_authored_monster *entry, qa_monster_definition_reference definition, const qa_vec3 *relocated);
  bool (*placement_ready)(void *ctx, qa_authored_monster *entry);
  bool (*enemy)(void *ctx, qa_actor_id actor, qa_actor_id *out);
  bool (*old_enemy)(void *ctx, qa_actor_id actor, qa_actor_id *out);
  void (*set_route)(void *ctx, qa_actor_id actor, const qa_actor_id *goal, double pause_until);
  void (*resume)(void *ctx, qa_actor_id actor, const qa_actor_id *activator);
} qa_app_sim_selected_monster_behavior;

/* TS MonsterMap: { kind: "q1", game: Q1Foundation } | { kind: "q2", game:
 * Q2Foundation, items: Q2ItemModule }. */
typedef struct qa_app_sim_monster_map {
  const char *kind; /* "q1" | "q2" */
  qa_q1_services *q1;
  qa_q2_foundation *q2;
  qa_q2_item_module *items;
} qa_app_sim_monster_map;

/* TS q1OrdinaryMonsters / q2OrdinaryMonsters sets. */
extern const char *const QA_APP_SIM_Q1_ORDINARY_MONSTERS[];
extern const size_t QA_APP_SIM_Q1_ORDINARY_MONSTER_COUNT;
extern const char *const QA_APP_SIM_Q2_ORDINARY_MONSTERS[];
extern const size_t QA_APP_SIM_Q2_ORDINARY_MONSTER_COUNT;
bool qa_app_sim_q1_ordinary_monster(const char *classname);
bool qa_app_sim_q2_ordinary_monster(const char *classname);

/* TS ReadonlyMap<string, string> of authored spawn fields: the Q1 entity
 * properties (later keys replace earlier ones) or the Q2 spawn values. */
typedef struct qa_app_sim_monster_fields {
  const qa_q1_entity *q1;
  const qa_q2_spawn_fields *q2;
} qa_app_sim_monster_fields;

/* TS q1PathFollower result. */
typedef struct qa_app_sim_q1_path_follower {
  const char *targetname;
  bool has_enemy; qa_actor_id enemy;
  void *ctx;
  void (*advance)(void *ctx, const char *name, const qa_actor_id *goal, double pause_until);
} qa_app_sim_q1_path_follower;

/* TS SelectedMonsters.capture() result (strings borrowed from the runtime
 * until its next mutation). */
typedef struct qa_app_sim_saved_authored_monsters {
  qa_app_sim_saved_authored_monster *entries; size_t count;
} qa_app_sim_saved_authored_monsters;
void qa_app_sim_saved_authored_monsters_free(qa_app_sim_saved_authored_monsters *saved);

typedef struct qa_app_sim_selected_monsters qa_app_sim_selected_monsters;
/* TS new SelectedMonsters(selection, map, behavior); selection kind must be
 * "replace" and is borrowed (its strings outlive the runtime). */
qa_app_sim_selected_monsters *qa_app_sim_selected_monsters_create(const qa_enemy_selection *selection,
    qa_app_sim_monster_map map, qa_app_sim_selected_monster_behavior behavior);
void qa_app_sim_selected_monsters_destroy(qa_app_sim_selected_monsters *monsters);
/* TS resolve: false = TS null. */
bool qa_app_sim_selected_monsters_resolve(qa_app_sim_selected_monsters *monsters, const char *classname,
    qa_app_sim_monster_fields fields, qa_monster_definition_reference *out);
void qa_app_sim_selected_monsters_admit_q1(qa_app_sim_selected_monsters *monsters, qa_owned_actor actor,
    const qa_q1_entity *source, double ordinal, qa_monster_definition_reference definition);
void qa_app_sim_selected_monsters_admit_q2(qa_app_sim_selected_monsters *monsters, qa_owned_actor actor,
    const qa_q2_spawn_fields *source, qa_monster_definition_reference definition);
bool qa_app_sim_selected_monsters_active(qa_app_sim_selected_monsters *monsters, qa_actor_id actor);
qa_app_sim_saved_authored_monsters qa_app_sim_selected_monsters_capture(qa_app_sim_selected_monsters *monsters);
void qa_app_sim_selected_monsters_restore(qa_app_sim_selected_monsters *monsters,
    const qa_app_sim_saved_authored_monster *entries, size_t count);
bool qa_app_sim_selected_monsters_before_turn(qa_app_sim_selected_monsters *monsters, qa_actor_id actor);
/* TS q1PathFollower: false = TS null. */
bool qa_app_sim_selected_monsters_q1_path_follower(qa_app_sim_selected_monsters *monsters, qa_actor_id actor,
    qa_app_sim_q1_path_follower *out);
/* TS q2PathFollower / q2CombatFollower: NULL = TS null. The returned
 * follower is stored with the entry and replaced by the next call for the
 * same actor. */
const qa_q2m_path_follower *qa_app_sim_selected_monsters_q2_path_follower(qa_app_sim_selected_monsters *monsters, qa_actor_id actor);
const qa_q2m_combat_follower *qa_app_sim_selected_monsters_q2_combat_follower(qa_app_sim_selected_monsters *monsters, qa_actor_id actor);
/* TS mission(entry) for an admitted entry (NULL when actor has none). */
const qa_monster_mission *qa_app_sim_selected_monsters_mission(qa_app_sim_selected_monsters *monsters, qa_actor_id actor);
/* TS authored.get / definitions.get: NULL / false when absent. */
qa_authored_monster *qa_app_sim_selected_monsters_authored(qa_app_sim_selected_monsters *monsters, qa_actor_id actor);
bool qa_app_sim_selected_monsters_definition(qa_app_sim_selected_monsters *monsters, qa_actor_id actor,
    qa_monster_definition_reference *out);
/* entry.placement = { kind: "waiting", barriers, activator: null } for an
 * entry this runtime owns: the barriers are copied into the entry's record. */
void qa_app_sim_authored_monster_set_waiting(qa_authored_monster *entry, const qa_authored_monster_barrier *barriers, size_t count);
/* TS authored.size and insertion-order iteration. */
size_t qa_app_sim_selected_monsters_count(const qa_app_sim_selected_monsters *monsters);
qa_authored_monster *qa_app_sim_selected_monsters_at(qa_app_sim_selected_monsters *monsters, size_t index);
#endif
