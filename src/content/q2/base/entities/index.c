/* Port of src/content/q2/base/entities/index.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/entities/index.h"

#include <stdlib.h>

#include "qa/foundation/support.h"

const char *const qa_q2_base_entity_classnames[] = {
    "func_plat", "func_door_secret", "trigger_elevator", "func_conveyor", "func_killbox", "func_object",
    "trigger_push", "trigger_hurt", "trigger_gravity", "trigger_monsterjump",
    "target_temp_entity", "target_spawner", "target_blaster", "target_crosslevel_trigger", "target_crosslevel_target",
    "target_laser", "target_lightramp", "target_earthquake", "target_character", "target_string", "func_clock",
    "viewthing", "misc_blackhole", "misc_eastertank", "misc_easterchick", "misc_easterchick2", "monster_commander_body",
    "misc_bigviper", "misc_viper_bomb", "light_mine1", "light_mine2", "misc_gib_arm", "misc_gib_leg",
    "misc_teleporter", "misc_teleporter_dest", "turret_base", "turret_breach", "turret_driver",
};
const size_t qa_q2_base_entity_classname_count = sizeof qa_q2_base_entity_classnames / sizeof qa_q2_base_entity_classnames[0];

struct qa_q2_base_entity_module {
  qa_q2_base_mover_entities *movers;
  qa_q2_turret_entities *turrets;
  qa_q2_base_targets *targets;
  qa_q2_environmental_triggers *triggers;
  qa_q2_base_scenery *scenery;
  qa_q2_named_think *thinks; size_t think_count;
  qa_q2_named_use *uses; size_t use_count;
  qa_q2_named_touch *touches; size_t touch_count;
  qa_q2_named_die *dies; size_t die_count;
  qa_q2_named_blocked *blockeds; size_t blocked_count;
  qa_q2_callback_definitions defs;
  qa_q2_spawn_module spawn;
};

static bool spawn_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2_base_entity_module_spawn(ctx, entity, game);
}

qa_q2_base_entity_module *qa_q2_base_entity_module_create(qa_q2_base_entity_hooks hooks) {
  qa_q2_base_entity_module *m = calloc(1, sizeof *m);
  if (!m) return NULL;
  m->movers = qa_q2_base_mover_entities_create(hooks);
  m->turrets = qa_q2_turret_entities_create(hooks);
  m->targets = qa_q2_base_targets_create(hooks);
  m->triggers = qa_q2_environmental_triggers_create(hooks);
  m->scenery = qa_q2_base_scenery_create(hooks);
  if (!m->movers || !m->turrets || !m->targets || !m->triggers || !m->scenery) {
    qa_q2_base_entity_module_destroy(m);
    return NULL;
  }
  qa_q2_callback_definitions sources[5] = {
      qa_q2_base_mover_entities_callbacks(m->movers), qa_q2_base_targets_callbacks(m->targets),
      qa_q2_environmental_triggers_callbacks(m->triggers), qa_q2_base_scenery_callbacks(m->scenery),
      qa_q2_turret_entities_callbacks(m->turrets)};
  size_t thinks = 0, uses = 0, touches = 0, dies = 0, blockeds = 0, trajectories = 0;
  for (size_t i = 0; i < 5; i++) {
    thinks += sources[i].think_count; uses += sources[i].use_count; touches += sources[i].touch_count;
    dies += sources[i].die_count; blockeds += sources[i].blocked_count; trajectories += sources[i].trajectory_count;
  }
  m->thinks = malloc((thinks ? thinks : 1) * sizeof *m->thinks);
  m->uses = malloc((uses ? uses : 1) * sizeof *m->uses);
  m->touches = malloc((touches ? touches : 1) * sizeof *m->touches);
  m->dies = malloc((dies ? dies : 1) * sizeof *m->dies);
  m->blockeds = malloc((blockeds ? blockeds : 1) * sizeof *m->blockeds);
  if (!m->thinks || !m->uses || !m->touches || !m->dies || !m->blockeds) {
    qa_q2_base_entity_module_destroy(m);
    return NULL;
  }
  for (size_t i = 0; i < 5; i++) {
    for (size_t j = 0; j < sources[i].think_count; j++) m->thinks[m->think_count++] = sources[i].think[j];
    for (size_t j = 0; j < sources[i].use_count; j++) m->uses[m->use_count++] = sources[i].use[j];
    for (size_t j = 0; j < sources[i].touch_count; j++) m->touches[m->touch_count++] = sources[i].touch[j];
    for (size_t j = 0; j < sources[i].die_count; j++) m->dies[m->die_count++] = sources[i].die[j];
    for (size_t j = 0; j < sources[i].blocked_count; j++) m->blockeds[m->blocked_count++] = sources[i].blocked[j];
  }
  /* TS merges only think/use/touch/die/blocked; the linear-motion
   * trajectory defs ride along inside the movers' own callbacks. */
  (void)trajectories;
  m->defs = (qa_q2_callback_definitions){.think = m->thinks, .think_count = m->think_count,
      .use = m->uses, .use_count = m->use_count, .touch = m->touches, .touch_count = m->touch_count,
      .die = m->dies, .die_count = m->die_count, .blocked = m->blockeds, .blocked_count = m->blocked_count};
  m->spawn = (qa_q2_spawn_module){m, &m->defs, NULL, spawn_fn};
  return m;
}
qa_q2_base_entity_module *qa_q2_create_base_entity_module(qa_q2_base_entity_hooks hooks) {
  return qa_q2_base_entity_module_create(hooks);
}
void qa_q2_base_entity_module_destroy(qa_q2_base_entity_module *m) {
  if (!m) return;
  qa_q2_base_mover_entities_destroy(m->movers);
  qa_q2_turret_entities_destroy(m->turrets);
  qa_q2_base_targets_destroy(m->targets);
  qa_q2_environmental_triggers_destroy(m->triggers);
  qa_q2_base_scenery_destroy(m->scenery);
  free(m->thinks); free(m->uses); free(m->touches); free(m->dies); free(m->blockeds);
  free(m);
}
qa_q2_spawn_module qa_q2_base_entity_module_as_spawn(qa_q2_base_entity_module *m) { return m->spawn; }
qa_q2_base_traversal qa_q2_base_entity_module_mover_traversal(qa_q2_base_entity_module *m, qa_q2_entity *entity) {
  return qa_q2_base_mover_entities_traversal(m->movers, entity);
}
qa_q2_callback_definitions qa_q2_base_entity_module_callbacks(qa_q2_base_entity_module *m) { return m->defs; }
qa_q2_base_entities_module_checkpoint qa_q2_base_entity_module_capture(qa_q2_base_entity_module *m, qa_q2_game_services *game) {
  qa_q2_base_entities_module_checkpoint out = {0};
  out.version = 1;
  out.movers = qa_q2_base_mover_entities_capture(m->movers, game);
  out.scenery = qa_q2_base_scenery_capture(m->scenery, game);
  out.turrets = qa_q2_turret_entities_capture(m->turrets, game);
  out.wind_times = qa_q2_environmental_triggers_capture(m->triggers, game, &out.wind_time_count);
  return out;
}
void qa_q2_base_entity_module_restore(qa_q2_base_entity_module *m, qa_q2_game_services *game, qa_q2_base_entities_module_checkpoint checkpoint) {
  qa_q2_base_mover_entities_restore(m->movers, game, checkpoint.movers);
  qa_q2_base_scenery_restore(m->scenery, game, checkpoint.scenery);
  qa_q2_environmental_triggers_restore(m->triggers, game, checkpoint.wind_times, checkpoint.wind_time_count);
  qa_q2_turret_entities_restore(m->turrets, game, checkpoint.turrets);
}
qa_q2_base_platform_state *qa_q2_base_entity_module_platform_state(qa_q2_base_entity_module *m, qa_q2_entity *entity) {
  return qa_q2_base_mover_entities_platform_state(m->movers, entity);
}
bool qa_q2_base_entity_module_spawn(qa_q2_base_entity_module *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2_base_mover_entities_spawn(m->movers, entity, game) ||
         qa_q2_environmental_triggers_spawn(m->triggers, entity, game) ||
         qa_q2_base_targets_spawn(m->targets, entity, game) ||
         qa_q2_base_scenery_spawn(m->scenery, entity, game) ||
         qa_q2_turret_entities_spawn(m->turrets, entity, game);
}
