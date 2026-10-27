/* Port of src/content/q2/rerelease/monsters/index.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_INDEX_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_INDEX_H
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/content/q2/missionpacks/monsters/types.h"
#include "qa/content/q2/rerelease/monsters/base_variants/actor.h"
#include "qa/content/q2/rerelease/monsters/base_variants/jorg.h"
#include "qa/content/q2/rerelease/monsters/base_variants/mutant.h"
#include "qa/content/q2/rerelease/monsters/tank.h"

/* TS Q2RereleaseMonsterOptions. expansion mirrors the TS string
 * union "base" | "xatrix" | "rogue" | "mg1" (carried, unused in TS
 * registration logic). transfer.fn NULL = TS undefined. */
typedef struct qa_q2rm_monster_options {
  qa_q2mpm_monster_state *source;
  bool is_n64;
  const char *expansion;
  qa_q2mpm_weapons weapons;
  qa_q2rm_healthbar_transfer transfer_healthbar_target;
} qa_q2rm_monster_options;

/* Storage for definitions registered by
 * qa_q2rm_register_rerelease_ordinary_monsters (TS keeps them alive
 * through the registry; C registrations borrow these). */
#define QA_Q2RM_ORDINARY_DEFINITION_COUNT 16
typedef struct qa_q2rm_ordinary_module {
  qa_q2m_definition definitions[QA_Q2RM_ORDINARY_DEFINITION_COUNT];
  qa_q2rm_mutant_definition mutant;
} qa_q2rm_ordinary_module;
void qa_q2rm_register_rerelease_ordinary_monsters(qa_q2rm_ordinary_module *out, qa_q2m_monsters *monsters,
                                                  qa_q2mpm_weapons weapons);

/* TS registerQ2RereleaseMonsters result. Storage for every
 * registered definition plus merged callback tables (rebuilt per
 * callbacks call, TS object literal rebuilt per access). */
#define QA_Q2RM_RERELEASE_DEFINITION_COUNT 32
typedef struct qa_q2rm_monsters_module {
  qa_q2m_monsters *monsters;
  qa_q2m_definition definitions[QA_Q2RM_RERELEASE_DEFINITION_COUNT];
  qa_q2rm_mutant_definition mutant;
  qa_q2rm_jorg_definition jorg;
  qa_q2rm_actor_module actor;
  qa_q2rm_tank_stand_module tank_stand;
  qa_q2f_named_think *merged_think; size_t merged_think_count;
  qa_q2f_named_use *merged_use; size_t merged_use_count;
  qa_q2f_named_touch *merged_touch; size_t merged_touch_count;
} qa_q2rm_monsters_module;
void qa_q2rm_register_rerelease_monsters(qa_q2rm_monsters_module *out, qa_q2m_monsters *monsters,
                                         qa_q2rm_monster_options options);
void qa_q2rm_monsters_module_destroy(qa_q2rm_monsters_module *module);
qa_q2bm_spawn_module qa_q2rm_monsters_as_spawn(qa_q2rm_monsters_module *module);
#endif
