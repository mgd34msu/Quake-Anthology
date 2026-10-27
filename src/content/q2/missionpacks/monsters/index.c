/* Port of src/content/q2/missionpacks/monsters/index.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/index.h"

#include <stdlib.h>
#include <string.h>

static void merge_all(qa_q2mpm_monsters_module *module) {
  if (module->callbacks_built) return;
  qa_q2f_callback_definitions sources[5];
  sources[0] = qa_q2mpm_hints_callbacks(&module->hints);
  sources[1] = qa_q2mpm_rogue_spawn_callbacks();
  sources[2] = qa_q2mpm_monster_dabeam_callbacks();
  sources[3] = qa_q2mpm_widow_debris_callbacks();
  size_t source_count = 4;
  size_t think = 0, touch = 0, die = 0, use = 0;
  for (size_t i = 0; i < source_count; i++) {
    think += sources[i].think_count;
    touch += sources[i].touch_count;
    die += sources[i].die_count;
    use += sources[i].use_count;
  }
  qa_q2f_named_think *thinks = malloc((think ? think : 1) * sizeof *thinks);
  qa_q2f_named_touch *touches = malloc((touch ? touch : 1) * sizeof *touches);
  qa_q2f_named_die *dies = malloc((die ? die : 1) * sizeof *dies);
  qa_q2f_named_use *uses = malloc((use ? use : 1) * sizeof *uses);
  if (!thinks || !touches || !dies || !uses) qa_q2f_error("Out of memory merging mission-pack callbacks");
  size_t t = 0, to = 0, d = 0, u = 0;
  for (size_t i = 0; i < source_count; i++) {
    for (size_t j = 0; j < sources[i].think_count; j++) thinks[t++] = sources[i].think[j];
    for (size_t j = 0; j < sources[i].touch_count; j++) touches[to++] = sources[i].touch[j];
    for (size_t j = 0; j < sources[i].die_count; j++) dies[d++] = sources[i].die[j];
    for (size_t j = 0; j < sources[i].use_count; j++) uses[u++] = sources[i].use[j];
  }
  module->merged_callbacks.think = thinks;
  module->merged_callbacks.think_count = t;
  module->merged_callbacks.touch = touches;
  module->merged_callbacks.touch_count = to;
  module->merged_callbacks.die = dies;
  module->merged_callbacks.die_count = d;
  module->merged_callbacks.use = uses;
  module->merged_callbacks.use_count = u;
  module->callbacks_built = true;
}
qa_q2f_callback_definitions qa_q2mpm_monsters_callbacks(qa_q2mpm_monsters_module *module) {
  merge_all(module);
  return module->merged_callbacks;
}
static double gravity_value(void *ctx) {
  qa_q2mpm_services *services = ctx;
  return services->gravity(services->ctx);
}
static size_t build_definitions(qa_q2mpm_monsters_module *module, qa_q2mpm_services services,
                                const qa_q2mpm_weapons *weapons) {
  qa_q2m_monsters *monsters = module->monsters;
  qa_q2mpm_monster_state *source = &module->source;
  module->definitions = malloc(48 * sizeof *module->definitions);
  if (!module->definitions) return 0;
  qa_q2m_definition *out = module->definitions;
  size_t n = 0;
  if (module->pack == QA_Q2MPM_PACK_XATRIX) {
    if (!weapons) { free(module->definitions); module->definitions = NULL; return 0; }
    n += qa_q2mpm_create_xatrix_base_variants(out + n, monsters);
    n += qa_q2mpm_create_soldier_heavy_definitions(out + n, *weapons);
    out[n++] = qa_q2mpm_create_chick_heat_definition(*weapons);
    out[n++] = qa_q2mpm_create_gekk_definition(monsters);
    out[n++] = qa_q2mpm_create_fixbot_definition(monsters);
    out[n++] = qa_q2mpm_create_gladb_definition(*weapons);
    out[n++] = qa_q2mpm_boss5_definition;
    return n;
  }
  n += qa_q2mpm_create_rogue_base_variants(out + n, monsters, source);
  n += qa_q2mpm_create_rogue_arsenal_monsters(out + n, monsters, source);
  n += qa_q2mpm_create_rogue_jumping_monsters(out + n, monsters, source);
  if (weapons) {
    n += qa_q2mpm_create_rogue_flyer_definitions(out + n, monsters, source);
    n += qa_q2mpm_create_rogue_hover_definitions(out + n, *weapons, source);
    n += qa_q2mpm_create_rogue_medic_definitions(out + n, monsters, *weapons, source);
    out[n++] = qa_q2mpm_create_carrier_definition(monsters, services);
    out[n++] = qa_q2mpm_create_widow_definition(monsters, *weapons, services, source);
    out[n++] = qa_q2mpm_create_widow2_definition(monsters, *weapons, services, source);
    out[n++] = qa_q2mpm_create_rogue_turret_definition(monsters, services.movers, source);
    qa_q2mpm_services *gravity_box = malloc(sizeof *gravity_box);
    if (!gravity_box) { free(module->definitions); module->definitions = NULL; return 0; }
    *gravity_box = services;
    qa_q2mpm_gravity_fn gravity = {gravity_box, gravity_value};
    out[n++] = qa_q2mpm_create_stalker_definition(monsters, *weapons, source, gravity);
    out[n++] = qa_q2mpm_create_chick_heat_definition(*weapons);
    out[n++] = qa_q2mpm_create_gladb_definition(*weapons);
    out[n++] = qa_q2mpm_boss5_definition;
  } else {
    free(module->definitions);
    module->definitions = NULL;
    qa_q2f_error("Rogue mission-pack monsters require mission-pack weapons");
  }
  return n;
}
bool qa_q2mpm_monsters_init(qa_q2mpm_monsters_module *module, qa_q2mpm_pack pack, qa_q2m_monsters *monsters,
                            qa_q2mpm_services services, const qa_q2mpm_weapons *weapons) {
  memset(module, 0, sizeof *module);
  module->pack = pack;
  module->monsters = monsters;
  qa_q2mpm_state_init(&module->source);
  qa_q2mpm_hints_init(&module->hints, monsters);
  module->rogue_hooks = qa_q2mpm_create_rogue_combat_hooks(monsters, &module->source, services);
  module->definition_count = build_definitions(module, services, weapons);
  return module->definitions != NULL;
}
void qa_q2mpm_monsters_destroy(qa_q2mpm_monsters_module *module) {
  free(module->definitions);
  free((void *)module->merged_callbacks.think);
  free((void *)module->merged_callbacks.touch);
  free((void *)module->merged_callbacks.die);
  free((void *)module->merged_callbacks.use);
  qa_q2mpm_state_destroy(&module->source);
  qa_q2mpm_hints_destroy(&module->hints);
  memset(module, 0, sizeof *module);
}
static qa_q2f_callback_definitions spawn_callbacks_fn(void *ctx) { return qa_q2mpm_monsters_callbacks(ctx); }
static bool spawn_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  return qa_q2mpm_monsters_spawn(ctx, entity, game);
}
qa_q2bm_spawn_module qa_q2mpm_monsters_as_spawn(qa_q2mpm_monsters_module *module) {
  qa_q2bm_spawn_module spawn = {module, spawn_callbacks_fn, spawn_fn};
  return spawn;
}
bool qa_q2mpm_monsters_spawn(qa_q2mpm_monsters_module *module, qa_q2f_entity *entity, qa_q2f_game *game) {
  if (module->pack != QA_Q2MPM_PACK_ROGUE) return false;
  return qa_q2mpm_hints_spawn(&module->hints, entity, game);
}
bool qa_q2mpm_monsters_finalize(qa_q2mpm_monsters_module *module, qa_q2f_game *game,
                                qa_q2mpm_hint_initialization *out) {
  if (!qa_q2mpm_hints_finalize(&module->hints, game, out)) return false;
  bool classic = game->options.edition == QA_Q2F_CLASSIC;
  module->found_hint = module->pack == QA_Q2MPM_PACK_ROGUE && classic && module->hints.present;
  if (module->pack == QA_Q2MPM_PACK_ROGUE) {
    qa_q2m_monsters_set_hint_paths(module->monsters, classic ? NULL : &module->hints.hooks);
    if (classic) qa_q2m_monsters_set_source_combat_rules(module->monsters, QA_Q2M_RULES_BASE, NULL);
    else qa_q2m_monsters_set_source_combat_rules(module->monsters, QA_Q2M_RULES_ROGUE, &module->rogue_hooks);
  }
  if (!module->registered) {
    int edition = module->pack == QA_Q2MPM_PACK_ROGUE && classic ? 0 : -1;
    for (size_t i = 0; i < module->definition_count; i++)
      qa_q2m_monsters_register(module->monsters, &module->definitions[i], edition);
    module->registered = true;
  }
  if (module->found_hint) {
    qa_q2m_monsters_set_hint_paths(module->monsters, &module->hints.hooks);
    for (size_t i = 0; i < module->definition_count; i++) {
      const char *classname = module->definitions[i].classname;
      if (strcmp(classname, "monster_infantry") == 0 || strcmp(classname, "monster_gunner") == 0)
        qa_q2m_monsters_register(module->monsters, &module->definitions[i], 0);
    }
  }
  return true;
}
bool qa_q2mpm_monsters_capture(qa_q2mpm_monsters_module *module, qa_q2f_game *game,
                               qa_q2mpm_monsters_checkpoint *out) {
  if (!qa_q2mpm_state_capture(&module->source, game, out)) return false;
  if (module->hints.present) {
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
void qa_q2mpm_monsters_restore(qa_q2mpm_monsters_module *module, qa_q2f_game *game,
                               const qa_q2mpm_monsters_checkpoint *checkpoint) {
  qa_q2mpm_state_restore(&module->source, game, checkpoint);
  qa_q2mpm_hints_checkpoint empty;
  memset(&empty, 0, sizeof empty);
  empty.version = 1;
  qa_q2mpm_hints_restore(&module->hints, game, checkpoint->has_hints ? &checkpoint->hints : &empty);
}
void qa_q2mpm_monsters_target_anger(qa_q2mpm_monsters_module *module, qa_q2f_entity *entity,
                                    qa_q2f_entity *target, qa_q2f_game *game) {
  qa_q2mpm_rogue_target_anger(module->monsters, &module->source, entity, target, game);
}
size_t qa_q2mpm_create_xatrix_additions(qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                        qa_q2mpm_weapons weapons) {
  size_t n = 0;
  n += qa_q2mpm_create_xatrix_base_variants(out + n, monsters);
  n += qa_q2mpm_create_soldier_heavy_definitions(out + n, weapons);
  out[n++] = qa_q2mpm_create_chick_heat_definition(weapons);
  out[n++] = qa_q2mpm_create_gekk_definition(monsters);
  out[n++] = qa_q2mpm_create_fixbot_definition(monsters);
  out[n++] = qa_q2mpm_create_gladb_definition(weapons);
  out[n++] = qa_q2mpm_boss5_definition;
  for (size_t i = 0; i < n; i++) qa_q2m_monsters_register(monsters, &out[i], -1);
  return n;
}
