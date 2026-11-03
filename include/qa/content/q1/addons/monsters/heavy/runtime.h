/* Port of src/content/q1/addons/monsters/heavy/runtime.ts.
 * MG3 heavy monster source controllers. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_HEAVY_RUNTIME_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_HEAVY_RUNTIME_H

#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/monsters/ai/controller.h"

#define QA_Q1AM_HEAVY_PREFIX "mg3:heavy" /* TS heavyPrefix */

typedef struct qa_q1am_heavy_monster qa_q1am_heavy_monster;
typedef struct qa_q1am_mg3_heavy qa_q1am_mg3_heavy;
/* TS HeavyAction. */
typedef void (*qa_q1am_heavy_action)(qa_q1am_heavy_monster *monster);
/* TS Readonly<Record<string, HeavyAction>> entry (insertion order). */
typedef struct qa_q1am_heavy_action_entry { const char *name; qa_q1am_heavy_action fn; } qa_q1am_heavy_action_entry;
/* TS Readonly<Record<string, Q1CallbackHandlers>> entry. */
typedef struct qa_q1am_heavy_callback_entry { const char *name; qa_q1_callback_handlers handlers; } qa_q1am_heavy_callback_entry;
/* TS interface HeavyDefinition; NULL melee/attack/sight/callbacks = TS absent. */
typedef struct qa_q1am_heavy_definition {
  qa_q1_base_monster_species spec;
  const qa_q1_monster_frame_table *frames;
  const qa_q1am_heavy_action_entry *actions; size_t action_count;
  qa_q1am_heavy_action spawn;
  void (*pain)(qa_q1am_heavy_monster *monster, const qa_actor_id *attacker, double damage);
  qa_q1am_heavy_action die;
  qa_q1am_heavy_action melee;
  bool (*attack)(qa_q1am_heavy_monster *monster);
  qa_q1am_heavy_action sight;
  const qa_q1am_heavy_callback_entry *callbacks; size_t callback_count;
} qa_q1am_heavy_definition;

/* TS class Q1HeavyMonster extends Mg3Monster. */
struct qa_q1am_heavy_monster {
  qa_q1am_mg3_monster base;
  qa_q1am_mg3_heavy *runtime;
  const qa_q1am_heavy_definition *definition;
};

/* TS class Q1Mg3Heavy. */
struct qa_q1am_mg3_heavy {
  qa_q1ad_context *context;
  struct qa_q1am_heavy_entry { qa_owned_actor actor; qa_q1am_heavy_monster *monster; } *monsters; /* TS Map, insertion order */
  size_t monster_count, monster_capacity;
  struct qa_q1am_heavy_registered *definitions; /* TS definitions Map (classname -> definition) */
  double rune_knight_melee_cycle;
  /* C ownership: controllers dropped from the map (they may still be running). */
  qa_q1am_heavy_monster **retired; size_t retired_count, retired_capacity;
  qa_q1am_heavy_definition lava_man; /* storage for lavaManDefinition(runtime) */
  qa_q1am_heavy_callback_entry lava_man_callbacks[4]; /* its callbacks capture this runtime */
  int64_t release_subscription;
  void *monster_callbacks; /* qa_q1_register_monster_callbacks closure state (owned) */
  qa_q1_state_extension extension;
  qa_q1am_mg3_heavy *next;
};

/* TS new Q1HeavyMonster(runtime, entity, definition), in place. False only on OOM. */
bool qa_q1am_heavy_monster_init(qa_q1am_heavy_monster *monster, qa_q1am_mg3_heavy *runtime, qa_q1_actor *entity,
                                const qa_q1am_heavy_definition *definition);
/* instanceof Q1HeavyMonster. */
bool qa_q1am_is_heavy_monster(const qa_q1_base_monster *monster);
/* TS number(key, value): context.setNumber(entity, key, value). */
void qa_q1am_heavy_monster_number(qa_q1am_heavy_monster *monster, const char *key, double value);
void qa_q1am_heavy_monster_install_callbacks(qa_q1am_heavy_monster *monster);
void qa_q1am_heavy_monster_initialize(qa_q1am_heavy_monster *monster, double size);

/* TS new Q1Mg3Heavy(context). NULL only on OOM. */
qa_q1am_mg3_heavy *qa_q1am_mg3_heavy_create(qa_q1ad_context *context);
qa_q1am_heavy_monster *qa_q1am_mg3_heavy_require(qa_q1am_mg3_heavy *runtime, qa_q1_actor *entity);
/* TS monsters.get(actor): NULL = undefined. */
qa_q1am_heavy_monster *qa_q1am_mg3_heavy_get(const qa_q1am_mg3_heavy *runtime, qa_owned_actor actor);
/* The definition is borrowed and must outlive the runtime. */
void qa_q1am_mg3_heavy_register(qa_q1am_mg3_heavy *runtime, const qa_q1am_heavy_definition *definition);
/* C ownership (TS GC): frees the game's heavy runtime and its controllers.
 * Call after the last use of the game. */
void qa_q1am_release_mg3_heavy(qa_q1_services *game);

#endif
