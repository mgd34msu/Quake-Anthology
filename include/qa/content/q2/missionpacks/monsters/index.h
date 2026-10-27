/* Port of src/content/q2/missionpacks/monsters/index.ts (barrel +
 * mission-pack monster module). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_INDEX_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_INDEX_H

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/missionpacks/monsters/boss5.h"
#include "qa/content/q2/missionpacks/monsters/carrier.h"
#include "qa/content/q2/missionpacks/monsters/chick_heat.h"
#include "qa/content/q2/missionpacks/monsters/combat.h"
#include "qa/content/q2/missionpacks/monsters/dabeam.h"
#include "qa/content/q2/missionpacks/monsters/fixbot.h"
#include "qa/content/q2/missionpacks/monsters/flyer.h"
#include "qa/content/q2/missionpacks/monsters/gekk.h"
#include "qa/content/q2/missionpacks/monsters/gladb.h"
#include "qa/content/q2/missionpacks/monsters/hints.h"
#include "qa/content/q2/missionpacks/monsters/hover.h"
#include "qa/content/q2/missionpacks/monsters/medic.h"
#include "qa/content/q2/missionpacks/monsters/power_armor.h"
#include "qa/content/q2/missionpacks/monsters/rogue_arsenal.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/rogue_gunner.h"
#include "qa/content/q2/missionpacks/monsters/rogue_infantry.h"
#include "qa/content/q2/missionpacks/monsters/rogue_jumpers.h"
#include "qa/content/q2/missionpacks/monsters/rogue_soldier.h"
#include "qa/content/q2/missionpacks/monsters/rogue_variants.h"
#include "qa/content/q2/missionpacks/monsters/soldierh.h"
#include "qa/content/q2/missionpacks/monsters/spawn.h"
#include "qa/content/q2/missionpacks/monsters/stalker.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_berserk.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_brain.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_carrier.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_chick.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_float.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_flyer.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_gunner.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_hover.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_infantry.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_medic.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_mutant.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_parasite.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_soldier.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_stalker.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_turret.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_widow.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_widow2.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_boss5.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_brain.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_fixbot.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_gekk.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_gladb.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_infantry.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_soldierh.h"
#include "qa/content/q2/missionpacks/monsters/turret.h"
#include "qa/content/q2/missionpacks/monsters/types.h"
#include "qa/content/q2/missionpacks/monsters/widow.h"
#include "qa/content/q2/missionpacks/monsters/widow/common.h"
#include "qa/content/q2/missionpacks/monsters/widow/death.h"
#include "qa/content/q2/missionpacks/monsters/widow2.h"
#include "qa/content/q2/missionpacks/monsters/xatrix_variants.h"

/* TS MissionPackMonsters class. Definitions are owned by the module
 * (merged callback tables inside them are process-lifetime). */
typedef struct qa_q2mpm_monsters_module {
  qa_q2mpm_pack pack;
  qa_q2m_monsters *monsters;
  qa_q2mpm_hints hints;
  qa_q2mpm_monster_state source;
  qa_q2m_source_combat_hooks rogue_hooks;
  bool registered;
  bool found_hint;
  qa_q2m_definition *definitions; size_t definition_count;
  qa_q2f_callback_definitions merged_callbacks;
  bool callbacks_built;
} qa_q2mpm_monsters_module;

/* TS createQ2MissionPackMonsters. weapons NULL mirrors TS undefined
 * (the heat chick, gladb and boss5 are then absent). False only on
 * OOM. */
bool qa_q2mpm_monsters_init(qa_q2mpm_monsters_module *module, qa_q2mpm_pack pack, qa_q2m_monsters *monsters,
                            qa_q2mpm_services services, const qa_q2mpm_weapons *weapons);
void qa_q2mpm_monsters_destroy(qa_q2mpm_monsters_module *module);
qa_q2f_callback_definitions qa_q2mpm_monsters_callbacks(qa_q2mpm_monsters_module *module);
qa_q2bm_spawn_module qa_q2mpm_monsters_as_spawn(qa_q2mpm_monsters_module *module);
bool qa_q2mpm_monsters_spawn(qa_q2mpm_monsters_module *module, qa_q2f_entity *entity, qa_q2f_game *game);
/* False only on OOM; out receives the hint initialization report. */
bool qa_q2mpm_monsters_finalize(qa_q2mpm_monsters_module *module, qa_q2f_game *game,
                                qa_q2mpm_hint_initialization *out);
bool qa_q2mpm_monsters_capture(qa_q2mpm_monsters_module *module, qa_q2f_game *game,
                               qa_q2mpm_monsters_checkpoint *out);
void qa_q2mpm_monsters_restore(qa_q2mpm_monsters_module *module, qa_q2f_game *game,
                               const qa_q2mpm_monsters_checkpoint *checkpoint);
void qa_q2mpm_monsters_target_anger(qa_q2mpm_monsters_module *module, qa_q2f_entity *entity,
                                    qa_q2f_entity *target, qa_q2f_game *game);

/* TS createQ2XatrixAdditions: registers xatrix additions into
 * monsters and returns the number of definitions registered (9), or
 * 0 on OOM. The definitions are written to caller storage (room 9). */
size_t qa_q2mpm_create_xatrix_additions(qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                        qa_q2mpm_weapons weapons);

#endif
