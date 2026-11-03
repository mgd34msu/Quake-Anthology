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
#include "qa/content/q2/rerelease/monsters/tables/missionpack.h"

/* TS MissionPackMonsters class. Definitions, and every bundle and merged
 * callback table their builders allocate (arena), are owned by the module
 * and freed by qa_q2mpm_monsters_destroy. */
typedef struct qa_q2mpm_monsters_module {
  qa_q2mpm_pack pack;
  qa_q2m_monsters *monsters;
  bool has_hints; qa_q2mpm_hints hints;  /* TS hints: Q2RogueHints | null (rogue only) */
  qa_q2mpm_monster_state source;
  qa_q2m_source_combat_hooks rogue_hooks;
  qa_q2mpm_services *gravity_box;        /* owned; backs the stalker gravity closure */
  qa_q2m_definition *definitions; size_t definition_count;
  qa_q2_named_think boss5_think[3];
  const char **original_source_fallbacks; size_t original_source_fallback_count;
  qa_q2_named_think *merged_think; qa_q2_named_touch *merged_touch;
  qa_q2mpm_arena arena;
  qa_q2_callback_definitions spawn_callbacks; /* TS callbacks getter, read by Q2EntityServices */
  /* Rerelease fallback registrations: { ...definition, moves: rereleaseMissionPackMoves(...) } (owned). */
  qa_q2m_definition *rerelease_definitions; size_t rerelease_definition_count;
  qa_q2rm_mission_pack_moves *rerelease_moves;
} qa_q2mpm_monsters_module;

/* TS q2OriginalMissionPackFallbacks. */
#define QA_Q2MPM_ORIGINAL_FALLBACK_COUNT 13
extern const char *const qa_q2mpm_original_mission_pack_fallbacks[QA_Q2MPM_ORIGINAL_FALLBACK_COUNT];
/* TS Q2MissionPackMonsters constructor / registerQ2MissionPackMonsters:
 * builds the pack's definitions, registers them (classic edition: all of
 * them under "classic"; the original-source fallbacks also edition-free),
 * installs rogue hint paths and the source combat rules. The module must
 * not move after init (registered definitions and hooks point into it).
 * False only on OOM. */
bool qa_q2mpm_monsters_init(qa_q2mpm_monsters_module *module, qa_q2mpm_pack pack, qa_q2m_monsters *monsters,
                            qa_q2mpm_services services, const qa_q2mpm_weapons *weapons, qa_q2_edition edition);
void qa_q2mpm_monsters_destroy(qa_q2mpm_monsters_module *module);
/* TS get callbacks(): monsters.callbacks + dabeam think + hint touch. The
 * returned arrays stay valid until the next call or destroy. */
qa_q2_callback_definitions qa_q2mpm_monsters_callbacks(qa_q2mpm_monsters_module *module);
qa_q2_spawn_module qa_q2mpm_monsters_as_spawn(qa_q2mpm_monsters_module *module);
bool qa_q2mpm_monsters_spawn(qa_q2mpm_monsters_module *module, qa_q2_entity *entity, qa_q2_game_services *game);
/* TS hints?.finalize(game): a xatrix module reports no hint paths. */
bool qa_q2mpm_monsters_finalize(qa_q2mpm_monsters_module *module, qa_q2_game_services *game,
                                qa_q2mpm_hint_initialization *out);
bool qa_q2mpm_monsters_capture(qa_q2mpm_monsters_module *module, qa_q2_game_services *game,
                               qa_q2mpm_monsters_checkpoint *out);
/* TS restore: a checkpoint with hints for a module without them is a TS Error. */
void qa_q2mpm_monsters_restore(qa_q2mpm_monsters_module *module, qa_q2_game_services *game,
                               const qa_q2mpm_monsters_checkpoint *checkpoint);
void qa_q2mpm_monsters_target_anger(qa_q2mpm_monsters_module *module, qa_q2_entity *entity,
                                    qa_q2_entity *target, qa_q2_game_services *game);

#endif
