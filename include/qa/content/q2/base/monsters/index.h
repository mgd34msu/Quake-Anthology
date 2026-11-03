/* Port of src/content/q2/base/monsters/index.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_MONSTERS_INDEX_H
#define QA_CONTENT_Q2_BASE_MONSTERS_INDEX_H
#include "qa/content/q2/base/monsters/actor.h"
#include "qa/content/q2/base/monsters/jorg.h"
#include "qa/content/q2/base/monsters/medic.h"
#include "qa/content/q2/base/monsters/mutant.h"

#define QA_Q2BM_CLASSIC_BASE_DEFINITION_COUNT 20
/* TS q2ClassicBaseMonsterDefinitions result plus the factory/wrap
 * storage its definitions borrow. */
typedef struct qa_q2bm_classic_base_definitions {
  qa_q2m_definition definitions[QA_Q2BM_CLASSIC_BASE_DEFINITION_COUNT];
  qa_q2bm_medic_definition medic;
  qa_q2bm_mutant_definition mutant;
  qa_q2bm_jorg_definition jorg;
  qa_q2m_definition makron_spawn_wrapped;
  qa_q2_named_think makron_think[2];
  qa_q2_named_think boss_think[QA_Q2BM_CLASSIC_BASE_DEFINITION_COUNT][3];
} qa_q2bm_classic_base_definitions;
void qa_q2bm_classic_base_monster_definitions(qa_q2bm_classic_base_definitions *out, qa_q2m_monsters *monsters);

/* TS registerQ2ClassicBaseMonsters result. Merged callback tables are
 * rebuilt per callbacks call (TS getter) into owned storage. */
typedef struct qa_q2bm_classic_base_module {
  qa_q2m_monsters *monsters;
  qa_q2bm_classic_base_definitions defs;
  qa_q2bm_actor_target_module targets;
  qa_q2_named_think *merged_think; size_t merged_think_count;
  qa_q2_named_use *merged_use; size_t merged_use_count;
  qa_q2_named_touch *merged_touch; size_t merged_touch_count;
  qa_q2_callback_definitions spawn_callbacks; /* TS callbacks getter, read by Q2EntityServices */
} qa_q2bm_classic_base_module;
/* False only on OOM. */
bool qa_q2bm_register_classic_base_monsters(qa_q2bm_classic_base_module *out, qa_q2m_monsters *monsters);
void qa_q2bm_classic_base_module_destroy(qa_q2bm_classic_base_module *module);
qa_q2_spawn_module qa_q2bm_classic_base_as_spawn(qa_q2bm_classic_base_module *module);
#endif
