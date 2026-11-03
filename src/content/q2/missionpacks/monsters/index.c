/* Port of src/content/q2/missionpacks/monsters/index.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/index.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/base/monsters/boss_common.h"

const char *const qa_q2mpm_original_mission_pack_fallbacks[QA_Q2MPM_ORIGINAL_FALLBACK_COUNT] = {
  "monster_gekk", "monster_fixbot", "monster_gladb", "monster_boss5", "monster_chick_heat", "monster_stalker",
  "monster_kamikaze", "monster_daedalus", "monster_turret", "monster_carrier", "monster_medic_commander",
  "monster_widow", "monster_widow2"};
static bool original_fallback(const char *classname) {
  for (size_t i = 0; i < QA_Q2MPM_ORIGINAL_FALLBACK_COUNT; i++)
    if (strcmp(qa_q2mpm_original_mission_pack_fallbacks[i], classname) == 0) return true;
  return false;
}

static double gravity_value(void *ctx) {
  qa_q2mpm_services *services = ctx;
  return services->gravity(services->ctx);
}

#define DEFINITION_ROOM 96
/* TS q2XatrixMonsterDefinitions. */
static size_t xatrix_definitions(qa_q2mpm_monsters_module *module, qa_q2mpm_weapons weapons) {
  qa_q2m_definition *out = module->definitions;
  size_t n = 0;
  out[n++] = qa_q2mpm_create_gekk_definition(&module->arena, module->monsters);
  out[n++] = qa_q2mpm_create_fixbot_definition(&module->arena, module->monsters);
  out[n++] = qa_q2mpm_create_gladb_definition(&module->arena, weapons);
  qa_q2bm_with_boss_explosion_callbacks(&out[n++], &qa_q2mpm_boss5_definition, module->monsters, module->boss5_think);
  out[n++] = qa_q2mpm_create_chick_heat_definition(&module->arena, weapons);
  n += qa_q2mpm_create_soldier_heavy_definitions(&module->arena, out + n, weapons);
  n += qa_q2mpm_create_xatrix_base_variants(&module->arena, out + n, module->monsters);
  return n;
}
/* TS q2RogueMonsterDefinitions. */
static size_t rogue_definitions(qa_q2mpm_monsters_module *module, qa_q2mpm_weapons weapons, qa_q2mpm_services services) {
  qa_q2m_monsters *monsters = module->monsters;
  qa_q2mpm_monster_state *source = &module->source;
  qa_q2m_definition *out = module->definitions;
  size_t n = 0;
  module->gravity_box = malloc(sizeof *module->gravity_box);
  if (!module->gravity_box) qa_raise("Error", 0, 0, "Out of memory building rogue monster definitions");
  *module->gravity_box = services;
  out[n++] = qa_q2mpm_create_stalker_definition(&module->arena, monsters, weapons, source, (qa_q2mpm_gravity_fn){module->gravity_box, gravity_value});
  n += qa_q2mpm_create_rogue_flyer_definitions(&module->arena, out + n, monsters, source);
  n += qa_q2mpm_create_rogue_hover_definitions(&module->arena, out + n, weapons, source);
  out[n++] = qa_q2mpm_create_rogue_turret_definition(&module->arena, monsters, services.movers, source);
  out[n++] = qa_q2mpm_create_carrier_definition(&module->arena, monsters, services);
  n += qa_q2mpm_create_rogue_medic_definitions(&module->arena, out + n, monsters, weapons, source);
  n += qa_q2mpm_create_rogue_base_variants(&module->arena, out + n, monsters, source);
  n += qa_q2mpm_create_rogue_arsenal_monsters(&module->arena, out + n, monsters, source);
  n += qa_q2mpm_create_rogue_jumping_monsters(&module->arena, out + n, monsters, source);
  out[n++] = qa_q2mpm_create_widow_definition(&module->arena, monsters, weapons, services, source);
  out[n++] = qa_q2mpm_create_widow2_definition(&module->arena, monsters, weapons, services, source);
  out[n++] = qa_q2mpm_create_rogue_gunner_definition(&module->arena, monsters, source);
  return n;
}

bool qa_q2mpm_monsters_init(qa_q2mpm_monsters_module *module, qa_q2mpm_pack pack, qa_q2m_monsters *monsters,
                            qa_q2mpm_services services, const qa_q2mpm_weapons *weapons, qa_q2_edition edition) {
  if (!weapons) qa_raise("Error", 0, 0, "Q2 mission-pack monsters require mission-pack weapons");
  memset(module, 0, sizeof *module);
  module->pack = pack;
  module->monsters = monsters;
  qa_q2mpm_state_init(&module->source);
  module->has_hints = pack == QA_Q2MPM_PACK_ROGUE;
  if (module->has_hints) {
    qa_q2mpm_hints_init(&module->hints, monsters);
    qa_q2m_monsters_set_hint_paths(monsters, &module->hints.hooks);
  }
  module->definitions = calloc(DEFINITION_ROOM, sizeof *module->definitions);
  module->original_source_fallbacks = calloc(QA_Q2MPM_ORIGINAL_FALLBACK_COUNT, sizeof *module->original_source_fallbacks);
  module->rerelease_definitions = calloc(DEFINITION_ROOM, sizeof *module->rerelease_definitions);
  module->rerelease_moves = calloc(DEFINITION_ROOM, sizeof *module->rerelease_moves);
  if (!module->definitions || !module->original_source_fallbacks || !module->rerelease_definitions || !module->rerelease_moves) {
    qa_q2mpm_monsters_destroy(module);
    return false;
  }
  module->definition_count = pack == QA_Q2MPM_PACK_XATRIX ? xatrix_definitions(module, *weapons)
                                                          : rogue_definitions(module, *weapons, services);
  if (module->definition_count > DEFINITION_ROOM) qa_raise("Error", 0, 0, "Q2 mission-pack monster table overflow");
  for (size_t i = 0; i < module->definition_count; i++) {
    const char *classname = module->definitions[i].classname;
    if (original_fallback(classname) && module->original_source_fallback_count < QA_Q2MPM_ORIGINAL_FALLBACK_COUNT)
      module->original_source_fallbacks[module->original_source_fallback_count++] = classname;
  }
  for (size_t i = 0; i < module->definition_count; i++) {
    if (edition == QA_Q2_EDITION_CLASSIC) qa_q2m_monsters_register(monsters, &module->definitions[i], 0);
    if (!original_fallback(module->definitions[i].classname)) continue;
    if (edition != QA_Q2_EDITION_RERELEASE) {
      qa_q2m_monsters_register(monsters, &module->definitions[i], -1);
      continue;
    }
    /* { ...definition, moves: rereleaseMissionPackMoves(definition.classname, definition.moves) } */
    size_t r = module->rerelease_definition_count++;
    module->rerelease_definitions[r] = module->definitions[i];
    module->rerelease_moves[r] = qa_q2rm_rerelease_mission_pack_moves(module->definitions[i].classname, module->definitions[i].moves,
                                                                      module->definitions[i].move_count);
    if (module->rerelease_moves[r].moves != NULL) {
      module->rerelease_definitions[r].moves = module->rerelease_moves[r].moves;
      module->rerelease_definitions[r].move_count = module->rerelease_moves[r].count;
    }
    qa_q2m_monsters_register(monsters, &module->rerelease_definitions[r], -1);
  }
  module->rogue_hooks = qa_q2mpm_create_rogue_combat_hooks(&module->arena, monsters, &module->source, services);
  if (pack == QA_Q2MPM_PACK_ROGUE) qa_q2m_monsters_set_source_combat_rules(monsters, QA_Q2M_RULES_ROGUE, &module->rogue_hooks);
  else qa_q2m_monsters_set_source_combat_rules(monsters, QA_Q2M_RULES_BASE, NULL);
  return true;
}
void qa_q2mpm_monsters_destroy(qa_q2mpm_monsters_module *module) {
  free(module->definitions);
  free(module->original_source_fallbacks);
  if (module->rerelease_moves)
    for (size_t i = 0; i < module->rerelease_definition_count; i++) qa_q2rm_mission_pack_moves_free(&module->rerelease_moves[i]);
  free(module->rerelease_moves);
  free(module->rerelease_definitions);
  free(module->gravity_box);
  free(module->merged_think);
  free(module->merged_touch);
  qa_q2mpm_arena_destroy(&module->arena);
  qa_q2mpm_state_destroy(&module->source);
  if (module->has_hints) qa_q2mpm_hints_destroy(&module->hints);
  memset(module, 0, sizeof *module);
}
/* TS: { ...callbacks, think: { ...callbacks.think, ...dabeam.think }, touch: { ...callbacks.touch, ...hints?.touch } }. */
qa_q2_callback_definitions qa_q2mpm_monsters_callbacks(qa_q2mpm_monsters_module *module) {
  qa_q2_callback_definitions core = qa_q2m_monsters_callbacks(module->monsters);
  qa_q2_callback_definitions dabeam = qa_q2mpm_monster_dabeam_callbacks();
  qa_q2_callback_definitions hints;
  memset(&hints, 0, sizeof hints);
  if (module->has_hints) hints = qa_q2mpm_hints_callbacks(&module->hints);
  free(module->merged_think); free(module->merged_touch);
  size_t thinks = core.think_count + dabeam.think_count, touches = core.touch_count + hints.touch_count;
  module->merged_think = malloc((thinks ? thinks : 1) * sizeof *module->merged_think);
  module->merged_touch = malloc((touches ? touches : 1) * sizeof *module->merged_touch);
  if (!module->merged_think || !module->merged_touch) qa_raise("Error", 0, 0, "Out of memory merging mission-pack callbacks");
  size_t t = 0;
  for (size_t i = 0; i < core.think_count; i++) module->merged_think[t++] = core.think[i];
  for (size_t i = 0; i < dabeam.think_count; i++) module->merged_think[t++] = dabeam.think[i];
  size_t to = 0;
  for (size_t i = 0; i < core.touch_count; i++) module->merged_touch[to++] = core.touch[i];
  for (size_t i = 0; i < hints.touch_count; i++) module->merged_touch[to++] = hints.touch[i];
  qa_q2_callback_definitions merged = core;
  merged.think = module->merged_think; merged.think_count = t;
  merged.touch = module->merged_touch; merged.touch_count = to;
  return merged;
}
static qa_q2_callback_definitions spawn_callbacks_fn(void *ctx) { return qa_q2mpm_monsters_callbacks(ctx); }
static bool spawn_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2mpm_monsters_spawn(ctx, entity, game);
}
qa_q2_spawn_module qa_q2mpm_monsters_as_spawn(qa_q2mpm_monsters_module *module) {
  module->spawn_callbacks = spawn_callbacks_fn(module);
  qa_q2_spawn_module spawn = {module, &module->spawn_callbacks, NULL, spawn_fn};
  return spawn;
}
bool qa_q2mpm_monsters_spawn(qa_q2mpm_monsters_module *module, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mpm_state_bind(&module->source, game);
  if (module->has_hints && qa_q2mpm_hints_spawn(&module->hints, entity, game)) return true;
  return qa_q2m_monsters_spawn(module->monsters, entity, game);
}
bool qa_q2mpm_monsters_finalize(qa_q2mpm_monsters_module *module, qa_q2_game_services *game,
                                qa_q2mpm_hint_initialization *out) {
  if (module->has_hints) return qa_q2mpm_hints_finalize(&module->hints, game, out);
  memset(out, 0, sizeof *out);
  return true;
}
bool qa_q2mpm_monsters_capture(qa_q2mpm_monsters_module *module, qa_q2_game_services *game,
                               qa_q2mpm_monsters_checkpoint *out) {
  if (!qa_q2mpm_state_capture(&module->source, game, out)) return false;
  if (module->has_hints) {
    qa_q2mpm_hints_checkpoint hints;
    if (!qa_q2mpm_hints_capture(&module->hints, game, &hints)) {
      qa_q2mpm_monsters_checkpoint_free(out);
      return false;
    }
    out->has_hints = true;
    out->hints = hints;
  }
  return true;
}
void qa_q2mpm_monsters_restore(qa_q2mpm_monsters_module *module, qa_q2_game_services *game,
                               const qa_q2mpm_monsters_checkpoint *checkpoint) {
  qa_q2mpm_state_bind(&module->source, game);
  qa_q2mpm_state_restore(&module->source, game, checkpoint);
  if (checkpoint->has_hints) {
    if (!module->has_hints) qa_raise("Error", 0, 0, "Rogue hint paths restored without their selected source module");
    qa_q2mpm_hints_restore(&module->hints, game, &checkpoint->hints);
  }
}
void qa_q2mpm_monsters_target_anger(qa_q2mpm_monsters_module *module, qa_q2_entity *entity,
                                    qa_q2_entity *target, qa_q2_game_services *game) {
  qa_q2mpm_rogue_target_anger(module->monsters, &module->source, entity, target, game);
}
