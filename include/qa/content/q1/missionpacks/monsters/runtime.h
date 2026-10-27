/*
 * Port of src/content/q1/missionpacks/monsters/runtime.ts.
 * MissionMonster extends the mirrored BaseMonster (qa_q1mpm_base_ops);
 * base-owned scalar state (currentFrame/nextFrame/inPain/counter/
 * idleUntil/lefty/sliding/lightningCount/countedDeath) is mirrored on
 * the struct. TS `undefined` returns are void. next_frame writes copy
 * into the fixed buffer. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_RUNTIME_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_RUNTIME_H

#include "qa/content/q1/missionpacks/monsters/types.h"

struct qa_q1mpm_mission_monster {
  qa_q1mpm_game *game;
  qa_q1mpm_actor *entity;
  const qa_q1mpm_pack_monster_definition *definition;
  qa_q1mpm_runtime *runtime;
  qa_q1mpm_base_ops base; /* mirrored BaseMonster super operations */
  qa_q1mpm_monster_state state;
  /* Mirrored BaseMonster scalar state. */
  char current_frame[128];
  char next_frame[128];
  double in_pain; double counter; double idle_until;
  bool lefty; bool sliding; double lightning_count; bool counted_death;
  const char *callback_prefix; /* TS source?.callbackPrefix ?? "base" */
};

typedef struct qa_q1mpm_definition_entry {
  char *classname;
  const qa_q1mpm_pack_monster_definition *definition;
} qa_q1mpm_definition_entry;
typedef struct qa_q1mpm_monster_entry {
  qa_owned_actor actor;
  qa_q1mpm_mission_monster *monster; /* owned */
} qa_q1mpm_monster_entry;

struct qa_q1mpm_runtime {
  qa_q1mpm_game *game;
  qa_q1mpm_q1base *base;
  const char *pack; /* TS Q1MissionPack: "hipnotic" | "rogue" | ... */
  qa_q1mpm_mission_monster_hooks hooks;
  double authored_gremlins;
  double spawned_gremlins;
  qa_q1mpm_monster_entry *monsters; size_t monster_count; size_t monster_capacity;
  qa_q1mpm_definition_entry *definitions; size_t definition_count; size_t definition_capacity;
  /* Synthetic baseBehavior definitions for Hipnotic base species. */
  qa_q1mpm_pack_monster_definition *synthetic; size_t synthetic_count;
  /* Factory-built definitions owned by registration (index.c). */
  qa_q1mpm_pack_monster_definition *owned_defs; size_t owned_def_count;
};

/* false only on OOM (TS constructor cannot fail that way). */
bool qa_q1mpm_mission_monster_init(qa_q1mpm_mission_monster *monster, qa_q1mpm_game *game,
                                   qa_q1mpm_actor *entity,
                                   const qa_q1mpm_pack_monster_definition *definition,
                                   qa_q1mpm_runtime *runtime);
void qa_q1mpm_mission_monster_destroy(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_set_next_frame(qa_q1mpm_mission_monster *monster, const char *name);
void qa_q1mpm_monster_set_enemy(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor enemy);
qa_vec3 qa_q1mpm_monster_origin(qa_q1mpm_mission_monster *monster);
bool qa_q1mpm_monster_target(qa_q1mpm_mission_monster *monster, qa_vec3 *out);
double qa_q1mpm_monster_distance(qa_q1mpm_mission_monster *monster);
const qa_q1mpm_species_spec *qa_q1mpm_monster_spec(qa_q1mpm_mission_monster *monster);

void qa_q1mpm_monster_spawn(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_spawn_default(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_start(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_start_default(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_play(qa_q1mpm_mission_monster *monster, const char *name);
void qa_q1mpm_monster_pain(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage);
void qa_q1mpm_monster_pain_default(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage);
void qa_q1mpm_monster_die_default(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker);
void qa_q1mpm_monster_die(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker);
void qa_q1mpm_monster_melee_attack(qa_q1mpm_mission_monster *monster);
bool qa_q1mpm_monster_try_attack(qa_q1mpm_mission_monster *monster);
bool qa_q1mpm_monster_check_attack_default(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_found(qa_q1mpm_mission_monster *monster, qa_actor_id target);
void qa_q1mpm_monster_found_default(qa_q1mpm_mission_monster *monster, qa_actor_id target);
void qa_q1mpm_monster_ai(qa_q1mpm_mission_monster *monster, qa_q1mpm_ai mode, double distance);
void qa_q1mpm_monster_ai_default(qa_q1mpm_mission_monster *monster, qa_q1mpm_ai mode, double distance);
void qa_q1mpm_monster_run(qa_q1mpm_mission_monster *monster, double distance);
void qa_q1mpm_monster_move_to_enemy(qa_q1mpm_mission_monster *monster, double distance);
bool qa_q1mpm_monster_search_for_coop_target(qa_q1mpm_mission_monster *monster);
qa_q1mpm_basis qa_q1mpm_monster_make_vectors(qa_q1mpm_mission_monster *monster);
bool qa_q1mpm_monster_find_target(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_retaliate(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker);
void qa_q1mpm_monster_use(qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor activator);
/* BaseMonster surface used directly by mission files (delegates). */
void qa_q1mpm_monster_delay(qa_q1mpm_mission_monster *monster, double seconds);
void qa_q1mpm_monster_face(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_change_yaw(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_attack_finished(qa_q1mpm_mission_monster *monster, double seconds);
bool qa_q1mpm_monster_visible(qa_q1mpm_mission_monster *monster);
bool qa_q1mpm_monster_visible_actor(qa_q1mpm_mission_monster *monster, qa_actor_id target);
bool qa_q1mpm_monster_eye(qa_q1mpm_mission_monster *monster, qa_vec3 *out);
bool qa_q1mpm_monster_eye_actor(qa_q1mpm_mission_monster *monster, qa_actor_id target, qa_vec3 *out);
double qa_q1mpm_monster_range_distance(qa_q1mpm_mission_monster *monster);
double qa_q1mpm_monster_range_distance_actor(qa_q1mpm_mission_monster *monster, qa_actor_id target);
void qa_q1mpm_monster_count_kill(qa_q1mpm_mission_monster *monster);
qa_save_value *qa_q1mpm_monster_capture(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_restore(qa_q1mpm_mission_monster *monster, const qa_save_reader *reader);

bool qa_q1mpm_runtime_init(qa_q1mpm_runtime *runtime, qa_q1mpm_game *game, qa_q1mpm_q1base *base,
                           const char *pack, const qa_q1mpm_mission_monster_hooks *hooks);
void qa_q1mpm_runtime_destroy(qa_q1mpm_runtime *runtime);
qa_q1mpm_mission_monster *qa_q1mpm_runtime_require(qa_q1mpm_runtime *runtime, qa_q1mpm_actor *entity);
qa_q1mpm_mission_monster *qa_q1mpm_runtime_context(qa_q1mpm_runtime *runtime, qa_actor_id actor);
qa_q1mpm_mission_monster *qa_q1mpm_runtime_get(qa_q1mpm_runtime *runtime, qa_owned_actor actor);
bool qa_q1mpm_runtime_set_monster(qa_q1mpm_runtime *runtime, qa_owned_actor actor,
                                  qa_q1mpm_mission_monster *monster);
bool qa_q1mpm_runtime_register(qa_q1mpm_runtime *runtime,
                               const qa_q1mpm_pack_monster_definition *definition);
void qa_q1mpm_runtime_spawn(qa_q1mpm_runtime *runtime, qa_q1mpm_actor *entity);
void qa_q1mpm_runtime_charm(qa_q1mpm_runtime *runtime, qa_q1mpm_actor *entity, qa_actor_id owner);

#endif
