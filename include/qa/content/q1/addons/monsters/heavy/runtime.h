/* Port of src/content/q1/addons/monsters/heavy/runtime.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_HEAVY_RUNTIME_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_HEAVY_RUNTIME_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

#define QA_Q1AM_HEAVY_PREFIX "mg3:heavy"

typedef struct qa_q1am_heavy_monster qa_q1am_heavy_monster;
typedef struct qa_q1am_heavy_definition qa_q1am_heavy_definition;
typedef void (*qa_q1am_heavy_action)(qa_q1am_heavy_monster *monster);
typedef struct qa_q1am_heavy_action_entry {
  const char *name;
  qa_q1am_heavy_action fn;
} qa_q1am_heavy_action_entry;
typedef struct qa_q1am_heavy_callback_entry {
  const char *name;
  qa_q1am_handlers handlers;
} qa_q1am_heavy_callback_entry;
struct qa_q1am_heavy_definition {
  qa_q1am_species spec;
  const qa_q1am_frame_table *frames;
  const qa_q1am_heavy_action_entry *actions; size_t action_count;
  qa_q1am_heavy_action spawn;
  void (*pain)(qa_q1am_heavy_monster *monster, bool has_attacker, qa_actor_id attacker, double damage);
  qa_q1am_heavy_action die;
  qa_q1am_heavy_action melee;
  bool (*attack)(qa_q1am_heavy_monster *monster);
  qa_q1am_heavy_action sight;
  const qa_q1am_heavy_callback_entry *callbacks; size_t callback_count;
};

typedef struct qa_q1am_mg3_heavy qa_q1am_mg3_heavy;
struct qa_q1am_heavy_monster {
  qa_q1am_monster base;
  qa_q1am_monster_ops super_ops;
  qa_q1am_mg3_heavy *runtime;
  const qa_q1am_heavy_definition *definition;
  qa_q1am_source source;
  qa_q1am_action_entry *wrapped_actions;
  struct qa_q1am_heavy_monster *next;
};
struct qa_q1am_mg3_heavy {
  qa_q1am_context *context;
  qa_q1am_heavy_monster *head, *tail;
  const qa_q1am_heavy_definition *definitions[16];
  size_t definition_count;
  double rune_knight_melee_cycle;
};

void qa_q1am_heavy_monster_init(qa_q1am_heavy_monster *monster, qa_q1am_mg3_heavy *runtime,
                                qa_q1am_actor *entity, const qa_q1am_heavy_definition *definition);
void qa_q1am_heavy_monster_set_number(qa_q1am_heavy_monster *monster, const char *key, double value);
void qa_q1am_heavy_monster_install_callbacks(qa_q1am_heavy_monster *monster);
void qa_q1am_heavy_monster_initialize(qa_q1am_heavy_monster *monster, double size);
void qa_q1am_mg3_heavy_init(qa_q1am_mg3_heavy *runtime, qa_q1am_context *context);
qa_q1am_heavy_monster *qa_q1am_mg3_heavy_require(qa_q1am_mg3_heavy *runtime, qa_q1am_actor *entity);
void qa_q1am_mg3_heavy_register(qa_q1am_mg3_heavy *runtime, const qa_q1am_heavy_definition *definition);

#endif
