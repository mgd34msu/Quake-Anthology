/*
 * Port of src/content/q1/missionpacks/monsters/runtime.ts.
 * MissionMonster extends BaseMonster: it embeds qa_q1_base_monster as its
 * first member and overrides through qa_q1_base_monster_ops, so the base
 * dispatchers (qa_q1_base_monster_<m>) reach the overrides below and
 * qa_q1_base_monster_<m>_super is TS super.m(). GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_RUNTIME_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_RUNTIME_H

#include "qa/content/q1/base/monsters.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/missionpacks/monsters/types.h"

/* A registered PackMonsterDefinition with its TS BaseMonsterSource
 * actions map (`(monster) => action(runtime.require(monster.entity))`). */
typedef struct qa_q1mpm_registered_definition {
  qa_q1mpm_runtime *runtime;
  const qa_q1mpm_pack_monster_definition *definition;
  qa_q1_monster_action_entry *source_actions; size_t source_action_count; /* owned */
} qa_q1mpm_registered_definition;

struct qa_q1mpm_mission_monster {
  qa_q1_base_monster base; /* TS extends BaseMonster; must stay first */
  const qa_q1mpm_pack_monster_definition *definition;
  qa_q1mpm_runtime *runtime;
};

typedef struct qa_q1mpm_definition_entry {
  char *classname;
  qa_q1mpm_registered_definition *registered;
} qa_q1mpm_definition_entry;
typedef struct qa_q1mpm_monster_entry {
  qa_owned_actor actor;
  qa_q1mpm_mission_monster *monster;
} qa_q1mpm_monster_entry;

/* TS Q1MissionPackMonsters. Must not move after init: the game keeps
 * pointers to it (spawn handlers, callbacks, state extension). */
struct qa_q1mpm_runtime {
  qa_q1_services *game;
  qa_q1_base *base;
  const char *pack; /* TS Q1MissionPack: "hipnotic" | "rogue" */
  qa_q1mpm_mission_monster_hooks hooks;
  double authored_gremlins;
  double spawned_gremlins;
  /* TS monsters Map (insertion order). Entries own their controllers. */
  qa_q1mpm_monster_entry *monsters; size_t monster_count; size_t monster_capacity;
  /* Controllers dropped from the map stay allocated until destroy (TS GC
   * keeps one alive while its callback is still on the stack). */
  qa_q1mpm_mission_monster **retired; size_t retired_count; size_t retired_capacity;
  /* TS definitions Map: classname -> registered definition. */
  qa_q1mpm_definition_entry *definitions; size_t definition_count; size_t definition_capacity;
  qa_q1mpm_registered_definition **registered; size_t registered_count; size_t registered_capacity;
  /* Species definitions (TS speciesDefinition): Hipnotic one per base
   * classname with its hipnoticSpeciesVariants entry, Rogue the
   * rogueSpeciesVariants list. */
  qa_q1mpm_pack_monster_definition *synthetic; size_t synthetic_count;
  /* Factory-built definitions owned by registration (index.c). */
  qa_q1mpm_pack_monster_definition *owned_defs; size_t owned_def_count;
  char extension_id[64];
  qa_q1_state_extension extension; /* the foundation keeps this pointer */
  int64_t release_subscription;    /* host.actors.onRelease id */
  void *monster_callbacks;         /* registerMonsterCallbacks closure state (owned) */
  /* Storage of the factory-built definitions' callbacks records (owned). */
  void **allocations; size_t allocation_count; size_t allocation_capacity;
};

/* TS `new MissionMonster(game, entity, definition, runtime)` (heap).
 * Aborts on OOM. Free with qa_q1_base_monster_destroy(&monster->base). */
qa_q1mpm_mission_monster *qa_q1mpm_mission_monster_create(qa_q1_services *game, qa_q1_actor *entity,
                                                          const qa_q1mpm_pack_monster_definition *definition,
                                                          qa_q1mpm_runtime *runtime);
/* TS instanceof MissionMonster: NULL when monster is another BaseMonster. */
qa_q1mpm_mission_monster *qa_q1mpm_mission_monster_of(qa_q1_base_monster *monster);

/* The MissionMonster methods TS exposes besides the overrides (which the
 * qa_q1_base_monster_<m> dispatchers reach). attacker/activator NULL = TS null. */
void qa_q1mpm_monster_spawn_default(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_start_default(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_pain_default(qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage);
void qa_q1mpm_monster_die_default(qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker);
bool qa_q1mpm_monster_check_attack_default(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_monster_found_default(qa_q1mpm_mission_monster *monster, qa_actor_id target);
void qa_q1mpm_monster_ai_default(qa_q1mpm_mission_monster *monster, qa_q1_monster_ai mode, double distance);
/* TS useDefault / gibDeathDefault (super.use, super.gibDeath). */
void qa_q1mpm_monster_use_default(qa_q1mpm_mission_monster *monster, const qa_actor_id *activator);
bool qa_q1mpm_monster_gib_death_default(qa_q1mpm_mission_monster *monster);
/* TS MissionMonster's protected sfx and gibDeath overrides of BaseMonster. */
const char *qa_q1mpm_monster_sfx(qa_q1mpm_mission_monster *monster, const char *path);
bool qa_q1mpm_monster_gib_death(qa_q1mpm_mission_monster *monster);

/* TS `new Q1MissionPackMonsters(game, base, pack, hooks)`; hooks NULL = TS {}. */
void qa_q1mpm_runtime_init(qa_q1mpm_runtime *runtime, qa_q1_services *game, qa_q1_base *base,
                           const char *pack, const qa_q1mpm_mission_monster_hooks *hooks);
void qa_q1mpm_runtime_destroy(qa_q1mpm_runtime *runtime);
qa_q1mpm_mission_monster *qa_q1mpm_runtime_require(qa_q1mpm_runtime *runtime, qa_q1_actor *entity);
/* NULL = TS null. */
qa_q1mpm_mission_monster *qa_q1mpm_runtime_context(qa_q1mpm_runtime *runtime, qa_actor_id actor);
/* TS monsters.get / monsters.set. set takes ownership of monster. */
qa_q1mpm_mission_monster *qa_q1mpm_runtime_get(qa_q1mpm_runtime *runtime, qa_owned_actor actor);
void qa_q1mpm_runtime_set_monster(qa_q1mpm_runtime *runtime, qa_owned_actor actor,
                                  qa_q1mpm_mission_monster *monster);
void qa_q1mpm_runtime_register(qa_q1mpm_runtime *runtime, const qa_q1mpm_pack_monster_definition *definition);
void qa_q1mpm_runtime_spawn(qa_q1mpm_runtime *runtime, qa_q1_actor *entity);
void qa_q1mpm_runtime_charm(qa_q1mpm_runtime *runtime, qa_q1_actor *entity, qa_actor_id owner);

#endif
