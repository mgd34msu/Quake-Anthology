/* Port of src/content/q2/rerelease/monsters/index.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/monsters/index.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/rerelease/monsters/arachnid.h"
#include "qa/content/q2/rerelease/monsters/base_variants/boss2.h"
#include "qa/content/q2/rerelease/monsters/base_variants/brain.h"
#include "qa/content/q2/rerelease/monsters/base_variants/chick.h"
#include "qa/content/q2/rerelease/monsters/base_variants/flipper.h"
#include "qa/content/q2/rerelease/monsters/base_variants/floater.h"
#include "qa/content/q2/rerelease/monsters/base_variants/flyer.h"
#include "qa/content/q2/rerelease/monsters/base_variants/hover.h"
#include "qa/content/q2/rerelease/monsters/base_variants/insane.h"
#include "qa/content/q2/rerelease/monsters/base_variants/makron.h"
#include "qa/content/q2/rerelease/monsters/base_variants/medic.h"
#include "qa/content/q2/rerelease/monsters/base_variants/parasite.h"
#include "qa/content/q2/rerelease/monsters/berserk.h"
#include "qa/content/q2/rerelease/monsters/gladiator.h"
#include "qa/content/q2/rerelease/monsters/guardian.h"
#include "qa/content/q2/rerelease/monsters/guncmdr.h"
#include "qa/content/q2/rerelease/monsters/gunner.h"
#include "qa/content/q2/rerelease/monsters/shambler.h"
#include "qa/content/q2/rerelease/monsters/supertank.h"

#define RERELEASE_EDITION 1 /* TS "rerelease" (QA_Q2F_RERELEASE) */

typedef struct ordinary_set {
  qa_q2m_definition berserk;
  const qa_q2m_definition *gunner;
  const qa_q2m_definition *flipper;
  const qa_q2m_definition *floater;
  qa_q2m_definition hover;
  qa_q2m_definition flyer;
  qa_q2m_definition brain;
  qa_q2m_definition parasite;
} ordinary_set;
static void ordinary_definitions(qa_q2m_monsters *monsters, qa_q2rm_mutant_definition *mutant, ordinary_set *out) {
  out->berserk = qa_q2rm_create_rerelease_berserk_definition(monsters);
  out->gunner = qa_q2rm_get_rerelease_gunner_definition();
  out->flipper = qa_q2rm_get_rerelease_flipper_definition();
  out->floater = qa_q2rm_get_rerelease_floater_definition();
  out->hover = qa_q2rm_create_rerelease_hover_definition(monsters);
  out->flyer = qa_q2rm_create_rerelease_flyer_definition(monsters);
  qa_q2rm_create_rerelease_mutant_definition(mutant, monsters);
  out->brain = qa_q2rm_create_rerelease_brain_definition(monsters);
  out->parasite = qa_q2rm_create_rerelease_parasite_definition(monsters);
}

void qa_q2rm_register_rerelease_ordinary_monsters(qa_q2rm_ordinary_module *out, qa_q2m_monsters *monsters,
                                                  qa_q2mpm_weapons weapons) {
  memset(out, 0, sizeof *out);
  ordinary_set ordinary;
  ordinary_definitions(monsters, &out->mutant, &ordinary);
  size_t at = 0;
  out->definitions[at++] = ordinary.berserk;
  out->definitions[at++] = *ordinary.gunner;
  out->definitions[at++] = *ordinary.flipper;
  out->definitions[at++] = *ordinary.floater;
  out->definitions[at++] = ordinary.hover;
  out->definitions[at++] = ordinary.flyer;
  out->definitions[at++] = out->mutant.definition;
  out->definitions[at++] = ordinary.parasite;
  out->definitions[at++] = ordinary.brain;
  for (size_t i = 0; i < at; i++) qa_q2m_monsters_register(monsters, &out->definitions[i], RERELEASE_EDITION);
  qa_q2m_definition batch[6];
  size_t batch_count = 0;
  batch_count += qa_q2rm_create_rerelease_chick_definitions(weapons, &batch[batch_count]);
  batch_count += qa_q2rm_create_rerelease_tank_definitions(weapons, &batch[batch_count]);
  batch_count += qa_q2rm_create_rerelease_gladiator_definitions(weapons, &batch[batch_count]);
  for (size_t i = 0; i < batch_count; i++) {
    if (strcmp(batch[i].classname, "monster_chick_heat") == 0 || strcmp(batch[i].classname, "monster_gladb") == 0)
      continue;
    out->definitions[at] = batch[i];
    qa_q2m_monsters_register(monsters, &out->definitions[at], RERELEASE_EDITION);
    at++;
  }
}

static qa_q2f_callback_definitions rerelease_callbacks_fn(void *ctx) {
  qa_q2rm_monsters_module *module = ctx;
  qa_q2f_callback_definitions core = qa_q2m_monsters_callbacks(module->monsters);
  qa_q2bm_spawn_module tank = qa_q2rm_tank_stand_as_spawn(&module->tank_stand);
  qa_q2f_callback_definitions tank_defs = tank.callbacks(tank.ctx);
  qa_q2bm_spawn_module actor = qa_q2rm_actor_targets_as_spawn(&module->actor);
  qa_q2f_callback_definitions actor_defs = actor.callbacks(actor.ctx);
  free(module->merged_think); free(module->merged_use); free(module->merged_touch);
  module->merged_think = NULL; module->merged_use = NULL; module->merged_touch = NULL;
  module->merged_think_count = core.think_count + tank_defs.think_count;
  module->merged_use_count = core.use_count + tank_defs.use_count;
  module->merged_touch_count = core.touch_count + actor_defs.touch_count;
  module->merged_think = malloc((module->merged_think_count ? module->merged_think_count : 1) * sizeof *module->merged_think);
  module->merged_use = malloc((module->merged_use_count ? module->merged_use_count : 1) * sizeof *module->merged_use);
  module->merged_touch = malloc((module->merged_touch_count ? module->merged_touch_count : 1) * sizeof *module->merged_touch);
  if (!module->merged_think || !module->merged_use || !module->merged_touch)
    qa_q2f_error("Rerelease monster callback merge allocation failed");
  size_t at = 0;
  for (size_t i = 0; i < core.think_count; i++) module->merged_think[at++] = core.think[i];
  for (size_t i = 0; i < tank_defs.think_count; i++) module->merged_think[at++] = tank_defs.think[i];
  at = 0;
  for (size_t i = 0; i < core.use_count; i++) module->merged_use[at++] = core.use[i];
  for (size_t i = 0; i < tank_defs.use_count; i++) module->merged_use[at++] = tank_defs.use[i];
  at = 0;
  for (size_t i = 0; i < core.touch_count; i++) module->merged_touch[at++] = core.touch[i];
  for (size_t i = 0; i < actor_defs.touch_count; i++) module->merged_touch[at++] = actor_defs.touch[i];
  qa_q2f_callback_definitions merged = core;
  merged.think = module->merged_think; merged.think_count = module->merged_think_count;
  merged.use = module->merged_use; merged.use_count = module->merged_use_count;
  merged.touch = module->merged_touch; merged.touch_count = module->merged_touch_count;
  return merged;
}
static bool rerelease_spawn_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2rm_monsters_module *module = ctx;
  qa_q2bm_spawn_module tank = qa_q2rm_tank_stand_as_spawn(&module->tank_stand);
  if (tank.spawn(tank.ctx, entity, game)) return true;
  qa_q2bm_spawn_module actor = qa_q2rm_actor_targets_as_spawn(&module->actor);
  if (actor.spawn(actor.ctx, entity, game)) return true;
  return qa_q2m_monsters_spawn(module->monsters, entity, game);
}

void qa_q2rm_register_rerelease_monsters(qa_q2rm_monsters_module *out, qa_q2m_monsters *monsters,
                                         qa_q2rm_monster_options options) {
  memset(out, 0, sizeof *out);
  out->monsters = monsters;
  ordinary_set ordinary;
  ordinary_definitions(monsters, &out->mutant, &ordinary);
  qa_q2rm_create_rerelease_jorg_definition(&out->jorg, monsters, options.transfer_healthbar_target);
  qa_q2rm_create_rerelease_actor_module(&out->actor, monsters);
  qa_q2rm_create_tank_stand_module(&out->tank_stand);
  size_t at = 0;
  out->definitions[at++] = qa_q2rm_arachnid_definition;
  out->definitions[at++] = ordinary.berserk;
  out->definitions[at++] = qa_q2rm_create_guardian_definition(monsters);
  out->definitions[at++] = qa_q2rm_shambler_definition;
  out->definitions[at++] = qa_q2rm_create_gun_commander_definition(options.weapons);
  out->definitions[at++] = *ordinary.gunner;
  out->definitions[at++] = *ordinary.flipper;
  out->definitions[at++] = *ordinary.floater;
  out->definitions[at++] = ordinary.hover;
  out->definitions[at++] = ordinary.flyer;
  at += qa_q2rm_create_rerelease_chick_definitions(options.weapons, &out->definitions[at]);
  out->definitions[at++] = out->mutant.definition;
  out->definitions[at++] = *qa_q2rm_get_rerelease_insane_definition();
  out->definitions[at++] = *qa_q2rm_get_rerelease_boss2_definition();
  out->definitions[at++] = *qa_q2rm_get_rerelease_makron_definition();
  out->definitions[at++] = out->jorg.definition;
  out->definitions[at++] = ordinary.brain;
  out->definitions[at++] = ordinary.parasite;
  at += qa_q2rm_create_rerelease_medic_definitions(monsters, options.weapons, options.source, &out->definitions[at]);
  out->definitions[at++] = out->actor.definition;
  at += qa_q2rm_create_rerelease_tank_definitions(options.weapons, &out->definitions[at]);
  at += qa_q2rm_create_rerelease_gladiator_definitions(options.weapons, &out->definitions[at]);
  at += qa_q2rm_create_rerelease_supertank_definitions(options.weapons, options.is_n64, &out->definitions[at]);
  for (size_t i = 0; i < at; i++) qa_q2m_monsters_register(monsters, &out->definitions[i], RERELEASE_EDITION);
}
void qa_q2rm_monsters_module_destroy(qa_q2rm_monsters_module *module) {
  free(module->merged_think); free(module->merged_use); free(module->merged_touch);
  module->merged_think = NULL; module->merged_use = NULL; module->merged_touch = NULL;
  module->merged_think_count = module->merged_use_count = module->merged_touch_count = 0;
}
qa_q2bm_spawn_module qa_q2rm_monsters_as_spawn(qa_q2rm_monsters_module *module) {
  qa_q2bm_spawn_module spawn = {module, rerelease_callbacks_fn, rerelease_spawn_fn};
  return spawn;
}
