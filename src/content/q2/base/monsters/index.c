/* Port of src/content/q2/base/monsters/index.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/index.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/base/monsters/berserk.h"
#include "qa/content/q2/base/monsters/boss2.h"
#include "qa/content/q2/base/monsters/boss3_stand.h"
#include "qa/content/q2/base/monsters/boss_common.h"
#include "qa/content/q2/base/monsters/brain.h"
#include "qa/content/q2/base/monsters/chick.h"
#include "qa/content/q2/base/monsters/flipper.h"
#include "qa/content/q2/base/monsters/floater.h"
#include "qa/content/q2/base/monsters/flyer.h"
#include "qa/content/q2/base/monsters/gladiator.h"
#include "qa/content/q2/base/monsters/gunner.h"
#include "qa/content/q2/base/monsters/hover.h"
#include "qa/content/q2/base/monsters/insane.h"
#include "qa/content/q2/base/monsters/makron.h"
#include "qa/content/q2/base/monsters/parasite.h"
#include "qa/content/q2/base/monsters/supertank.h"
#include "qa/content/q2/base/monsters/tank.h"

void qa_q2bm_classic_base_monster_definitions(qa_q2bm_classic_base_definitions *out, qa_q2m_monsters *monsters) {
  qa_q2bm_create_medic_definition(&out->medic, monsters);
  qa_q2bm_create_mutant_definition(&out->mutant, monsters);
  qa_q2bm_create_jorg_definition(&out->jorg, monsters);
  qa_q2bm_with_makron_spawn_callbacks(&out->makron_spawn_wrapped, &qa_q2bm_makron_definition, monsters,
                                      out->makron_think);
  const qa_q2m_definition base[QA_Q2BM_CLASSIC_BASE_DEFINITION_COUNT] = {
    qa_q2bm_berserk_definition, qa_q2bm_brain_definition, qa_q2bm_chick_definition,
    qa_q2bm_flipper_definition, *qa_q2bm_get_floater_definition(), qa_q2bm_flyer_definition,
    qa_q2bm_gladiator_definition, qa_q2bm_gunner_definition, qa_q2bm_hover_definition,
    out->medic.definition, out->mutant.definition, qa_q2bm_parasite_definition,
    qa_q2bm_supertank_definition, qa_q2bm_tank_definition, qa_q2bm_tank_commander_definition,
    qa_q2bm_boss2_definition, out->jorg.definition, out->makron_spawn_wrapped,
    qa_q2bm_actor_definition, qa_q2bm_insane_definition,
  };
  for (size_t i = 0; i < QA_Q2BM_CLASSIC_BASE_DEFINITION_COUNT; i++)
    qa_q2bm_with_boss_explosion_callbacks(&out->definitions[i], &base[i], monsters, out->boss_think[i]);
}

static qa_q2_callback_definitions classic_callbacks_fn(void *ctx) {
  qa_q2bm_classic_base_module *module = ctx;
  qa_q2_callback_definitions core = qa_q2m_monsters_callbacks(module->monsters);
  qa_q2_spawn_module boss = qa_q2bm_boss3_stand_module();
  qa_q2_callback_definitions boss_defs = *boss.callbacks;
  qa_q2_spawn_module actor = qa_q2bm_actor_target_as_spawn(&module->targets);
  qa_q2_callback_definitions actor_defs = *actor.callbacks;
  free(module->merged_think); free(module->merged_use); free(module->merged_touch);
  module->merged_think = NULL; module->merged_use = NULL; module->merged_touch = NULL;
  module->merged_think_count = core.think_count + boss_defs.think_count;
  module->merged_use_count = core.use_count + boss_defs.use_count + actor_defs.use_count;
  module->merged_touch_count = core.touch_count + actor_defs.touch_count;
  module->merged_think = malloc((module->merged_think_count ? module->merged_think_count : 1) * sizeof *module->merged_think);
  module->merged_use = malloc((module->merged_use_count ? module->merged_use_count : 1) * sizeof *module->merged_use);
  module->merged_touch = malloc((module->merged_touch_count ? module->merged_touch_count : 1) * sizeof *module->merged_touch);
  if (!module->merged_think || !module->merged_use || !module->merged_touch)
    qa_raise("Error", 0, 0, "Classic base monster callback merge allocation failed");
  size_t at = 0;
  for (size_t i = 0; i < core.think_count; i++) module->merged_think[at++] = core.think[i];
  for (size_t i = 0; i < boss_defs.think_count; i++) module->merged_think[at++] = boss_defs.think[i];
  at = 0;
  for (size_t i = 0; i < core.use_count; i++) module->merged_use[at++] = core.use[i];
  for (size_t i = 0; i < boss_defs.use_count; i++) module->merged_use[at++] = boss_defs.use[i];
  for (size_t i = 0; i < actor_defs.use_count; i++) module->merged_use[at++] = actor_defs.use[i];
  at = 0;
  for (size_t i = 0; i < core.touch_count; i++) module->merged_touch[at++] = core.touch[i];
  for (size_t i = 0; i < actor_defs.touch_count; i++) module->merged_touch[at++] = actor_defs.touch[i];
  qa_q2_callback_definitions merged = core;
  merged.think = module->merged_think; merged.think_count = module->merged_think_count;
  merged.use = module->merged_use; merged.use_count = module->merged_use_count;
  merged.touch = module->merged_touch; merged.touch_count = module->merged_touch_count;
  return merged;
}
static bool classic_spawn_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2bm_classic_base_module *module = ctx;
  qa_q2_spawn_module boss = qa_q2bm_boss3_stand_module();
  if (boss.spawn(boss.ctx, entity, game)) return true;
  qa_q2_spawn_module actor = qa_q2bm_actor_target_as_spawn(&module->targets);
  if (actor.spawn(actor.ctx, entity, game)) return true;
  return qa_q2m_monsters_spawn(module->monsters, entity, game);
}
bool qa_q2bm_register_classic_base_monsters(qa_q2bm_classic_base_module *out, qa_q2m_monsters *monsters) {
  memset(out, 0, sizeof *out);
  out->monsters = monsters;
  qa_q2bm_classic_base_monster_definitions(&out->defs, monsters);
  for (size_t i = 0; i < QA_Q2BM_CLASSIC_BASE_DEFINITION_COUNT; i++)
    qa_q2m_monsters_register(monsters, &out->defs.definitions[i], -1);
  qa_q2bm_create_actor_target_module(&out->targets, monsters);
  return true;
}
void qa_q2bm_classic_base_module_destroy(qa_q2bm_classic_base_module *module) {
  free(module->merged_think); free(module->merged_use); free(module->merged_touch);
  module->merged_think = NULL; module->merged_use = NULL; module->merged_touch = NULL;
  module->merged_think_count = module->merged_use_count = module->merged_touch_count = 0;
}
qa_q2_spawn_module qa_q2bm_classic_base_as_spawn(qa_q2bm_classic_base_module *module) {
  module->spawn_callbacks = classic_callbacks_fn(module);
  qa_q2_spawn_module spawn = {module, &module->spawn_callbacks, NULL, classic_spawn_fn};
  return spawn;
}
