/*
 * Port of src/content/q1/missionpacks/monsters/types.ts. The mission-pack
 * monsters run on the one Q1 game (qa_q1_services, TS Q1EntityServices)
 * and the base monster types (MonsterSpecies, MonsterFrame, MonsterAi).
 * Closures (TS MissionAction and the definition hooks) are fn + ctx; a
 * NULL fn is a TS absent optional member. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_TYPES_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/base/animation.h"
#include "qa/content/q1/base/species.h"
#include "qa/content/q1/foundation/callbacks.h"

typedef struct qa_q1mpm_mission_monster qa_q1mpm_mission_monster; /* TS MissionMonster */
typedef struct qa_q1mpm_runtime qa_q1mpm_runtime;                 /* TS Q1MissionPackMonsters */

/* TS MissionMonsterHooks. charmer NULL = absent; it returns false for TS null. */
typedef struct qa_q1mpm_mission_monster_hooks {
  void *ctx;
  bool (*charmer)(void *ctx, qa_actor_id *out);
} qa_q1mpm_mission_monster_hooks;

/* TS MissionAction. */
typedef void (*qa_q1mpm_action_fn)(void *ctx, qa_q1mpm_mission_monster *monster);
typedef struct qa_q1mpm_action { qa_q1mpm_action_fn fn; void *ctx; } qa_q1mpm_action;
/* One entry of TS Record<string, MissionAction>. */
typedef struct qa_q1mpm_named_action { const char *name; qa_q1mpm_action action; } qa_q1mpm_named_action;
/* One entry of TS Record<string, Q1CallbackHandlers>. */
typedef struct qa_q1mpm_named_handlers { const char *name; qa_q1_callback_handlers handlers; } qa_q1mpm_named_handlers;

/* PackMonsterDefinition hook shapes; attacker/activator NULL = TS null. */
typedef void (*qa_q1mpm_pain_hook_fn)(void *ctx, qa_q1mpm_mission_monster *monster,
                                      const qa_actor_id *attacker, double damage);
typedef void (*qa_q1mpm_die_hook_fn)(void *ctx, qa_q1mpm_mission_monster *monster,
                                     const qa_actor_id *attacker);
typedef bool (*qa_q1mpm_check_attack_fn)(void *ctx, qa_q1mpm_mission_monster *monster);
typedef void (*qa_q1mpm_found_fn)(void *ctx, qa_q1mpm_mission_monster *monster, qa_actor_id target);
typedef void (*qa_q1mpm_ai_hook_fn)(void *ctx, qa_q1mpm_mission_monster *monster,
                                    qa_q1_monster_ai mode, double distance);
typedef void (*qa_q1mpm_use_hook_fn)(void *ctx, qa_q1mpm_mission_monster *monster,
                                     const qa_actor_id *activator);
/* TS redirect: returns the animation to play (name itself when unchanged). */
typedef const char *(*qa_q1mpm_redirect_fn)(void *ctx, qa_q1mpm_mission_monster *monster, const char *name);
/* TS prePain: true consumes the pain. */
typedef bool (*qa_q1mpm_pre_pain_fn)(void *ctx, qa_q1mpm_mission_monster *monster,
                                     const qa_actor_id *attacker, double damage);
/* TS gibDeath: true means the death was handled. */
typedef bool (*qa_q1mpm_gib_death_fn)(void *ctx, qa_q1mpm_mission_monster *monster);
/* TS sfx: the sound path to play for path. */
typedef const char *(*qa_q1mpm_sfx_fn)(void *ctx, qa_q1mpm_mission_monster *monster, const char *path);

/* TS PackMonsterDefinition. Optional members have a NULL fn. */
typedef struct qa_q1mpm_pack_monster_definition {
  qa_q1_base_monster_species spec;
  bool has_base_behavior; bool base_behavior;
  qa_q1_monster_frame_table frames;
  const qa_q1mpm_named_action *actions; size_t action_count;
  const qa_q1mpm_named_handlers *callbacks; size_t callback_count;
  qa_q1mpm_action spawn; qa_q1mpm_action start;
  qa_q1mpm_pain_hook_fn pain; void *pain_ctx;
  qa_q1mpm_die_hook_fn die; void *die_ctx;
  qa_q1mpm_action melee;
  qa_q1mpm_check_attack_fn check_attack; void *check_attack_ctx;
  qa_q1mpm_found_fn found; void *found_ctx;
  qa_q1mpm_ai_hook_fn ai; void *ai_ctx;
  qa_q1mpm_use_hook_fn use; void *use_ctx;
  /* Maps an animation the shared AI asks for onto the one this pack's state
   * calls for (th_stand and friends swapped at spawn). */
  qa_q1mpm_redirect_fn redirect; void *redirect_ctx;
  /* Runs after retaliation and before the shared pain callback. */
  qa_q1mpm_pre_pain_fn pre_pain; void *pre_pain_ctx;
  /* Replaces the species gib threshold. */
  qa_q1mpm_gib_death_fn gib_death; void *gib_death_ctx;
  qa_q1mpm_sfx_fn sfx; void *sfx_ctx;
} qa_q1mpm_pack_monster_definition;

/* TS PackMonsterFactory: fills the caller's array, returns the count written. */
typedef size_t (*qa_q1mpm_pack_monster_factory_fn)(
    void *ctx, qa_q1mpm_runtime *runtime,
    const qa_q1mpm_pack_monster_definition **out, size_t capacity);
/* TS MissionSpawn. */
typedef void (*qa_q1mpm_mission_spawn_fn)(void *ctx, qa_q1_actor *entity);

#endif
