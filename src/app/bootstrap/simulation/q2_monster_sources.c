/* Port of src/app/bootstrap/simulation/q2-monster-sources.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q2_monster_sources.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/missionpacks/projectiles/mines.h"
#include "qa/foundation/support.h"
typedef qa_app_sim_selected_q2_monster_modules modules_t;
static qa_q2_game_services *game_of(modules_t *m) { return qa_q2_entity_services_game(m->game); }
/* { ...options.weapons, noise: (actor, origin, secondary) => monsters.reportNoise(...) }: the
 * caller's leaves keep weapons.ctx. */
static double weapons_quad(void *ctx, qa_actor_id actor) { modules_t *m = ctx; return m->options.weapons.quad_multiplier(m->options.weapons.ctx, actor); }
static double weapons_source_damage(void *ctx, qa_actor_id actor) { modules_t *m = ctx; return m->options.weapons.source_damage_multiplier(m->options.weapons.ctx, actor); }
static double weapons_firing_interval(void *ctx, qa_actor_id actor, double seconds) { modules_t *m = ctx; return m->options.weapons.firing_interval(m->options.weapons.ctx, actor, seconds); }
static void weapons_emit(void *ctx, qa_q2w_event event) { modules_t *m = ctx; m->options.weapons.emit(m->options.weapons.ctx, event); }
static void weapons_noise(void *ctx, qa_actor_id actor, qa_vec3 origin, bool secondary) { modules_t *m = ctx; qa_q2m_monsters_report_noise(&m->monsters, actor, origin, secondary); }
static void weapons_dodge(void *ctx, qa_actor_id monster, qa_actor_id attacker, double eta, qa_trace_result trace) {
  modules_t *m = ctx; m->options.weapons.dodge(m->options.weapons.ctx, monster, attacker, eta, trace);
}
static void *weapons_lag_begin(void *ctx, qa_actor_id actor, qa_vec3 start, qa_vec3 direction) {
  modules_t *m = ctx; return m->options.weapons.lag_begin(m->options.weapons.ctx, actor, start, direction);
}
static void weapons_lag_end(void *ctx, void *token) { modules_t *m = ctx; m->options.weapons.lag_end(m->options.weapons.ctx, token); }
static void weapons_ammo_changed(void *ctx, qa_actor_id actor, qa_item_id ammo) { modules_t *m = ctx; m->options.weapons.ammo_changed(m->options.weapons.ctx, actor, ammo); }
static bool weapons_can_target(void *ctx, const qa_actor_id *attacker, qa_actor_id target) {
  modules_t *m = ctx; return m->options.weapons.can_target(m->options.weapons.ctx, attacker, target);
}
static void movers_path_corner(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id actor) {
  modules_t *m = ctx; qa_q2m_monsters_touch_path_corner(&m->monsters, entity, game, actor);
}
static void movers_combat_point(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id actor) {
  modules_t *m = ctx; qa_q2m_monsters_touch_combat_point(&m->monsters, entity, game, actor);
}
static qa_q2m_context *projectiles_monster(void *ctx, qa_actor_id actor) { modules_t *m = ctx; return qa_q2m_monsters_context(&m->monsters, actor); }
static void projectiles_player_effect(void *ctx, qa_q2mp_player_effect effect) { modules_t *m = ctx; m->options.player_effect(m->options.ctx, effect); }
static qa_q2mp_mines *mines_of(modules_t *m) { return &m->projectiles.base.base; }
static double services_gravity(void *ctx) { modules_t *m = ctx; return m->options.gravity(m->options.ctx); }
static qa_q2mpm_powerups services_powerups(void *ctx, qa_actor_id actor) { modules_t *m = ctx; return m->options.powerups(m->options.ctx, actor); }
static bool services_bad_area(void *ctx, qa_actor_id actor) { modules_t *m = ctx; return qa_q2mp_mines_bad_area(mines_of(m), actor, game_of(m)); }
static qa_q2_entity *services_bad_area_entity(void *ctx, qa_actor_id actor, const qa_vec3 *origin) {
  modules_t *m = ctx; return qa_q2mp_mines_bad_area_entity(mines_of(m), actor, game_of(m), origin);
}
static bool services_mark_tesla_area(void *ctx, qa_q2_entity *self, qa_q2_entity *tesla) {
  modules_t *m = ctx; return qa_q2mp_mines_mark_tesla_area(mines_of(m), self->actor, game_of(m), tesla);
}
static void add_module(modules_t *m, qa_q2_spawn_module module) {
  if (m->module_count == sizeof m->modules / sizeof *m->modules) qa_range_error("Selected Q2 monster module table is full");
  m->modules[m->module_count++] = module;
}
modules_t *qa_app_sim_create_selected_q2_monster_modules(qa_app_sim_q2_monster_options options) {
  modules_t *m = calloc(1, sizeof *m);
  if (m == NULL) qa_range_error("Out of memory");
  m->options = options;
  qa_q2w_hooks weapons = options.weapons;
  weapons.ctx = m;
  weapons.quad_multiplier = options.weapons.quad_multiplier != NULL ? weapons_quad : NULL;
  weapons.source_damage_multiplier = options.weapons.source_damage_multiplier != NULL ? weapons_source_damage : NULL;
  weapons.firing_interval = options.weapons.firing_interval != NULL ? weapons_firing_interval : NULL;
  weapons.emit = options.weapons.emit != NULL ? weapons_emit : NULL;
  weapons.noise = weapons_noise;
  weapons.dodge = options.weapons.dodge != NULL ? weapons_dodge : NULL;
  weapons.lag_begin = options.weapons.lag_begin != NULL ? weapons_lag_begin : NULL;
  weapons.lag_end = options.weapons.lag_end != NULL ? weapons_lag_end : NULL;
  weapons.ammo_changed = options.weapons.ammo_changed != NULL ? weapons_ammo_changed : NULL;
  weapons.can_target = options.weapons.can_target != NULL ? weapons_can_target : NULL;
  if (!qa_q2w_ballistics_init(&m->ballistics, weapons)) qa_range_error("Out of memory");
  if (!qa_q2m_monsters_init(&m->monsters, qa_q2m_weapons_from_ballistics(&m->ballistics), options.behavior)) qa_range_error("Out of memory");
  if (!qa_q2bm_register_classic_base_monsters(&m->base, &m->monsters)) qa_range_error("Out of memory");
  qa_q2_mover_hooks mover_hooks = {m, movers_path_corner, movers_combat_point};
  m->movers = qa_q2_create_mover_module(mover_hooks);
  if (m->movers == NULL) qa_range_error("Out of memory");
  qa_q2mp_projectile_hooks projectile_hooks; memset(&projectile_hooks, 0, sizeof projectile_hooks);
  projectile_hooks.base = &m->ballistics;
  projectile_hooks.gravity_ctx = options.ctx; projectile_hooks.gravity = options.gravity;
  projectile_hooks.ctx = m; projectile_hooks.monster = projectiles_monster; projectile_hooks.player_effect = projectiles_player_effect;
  qa_q2mp_projectiles_init(&m->projectiles, projectile_hooks);
  qa_q2mpm_services services; memset(&services, 0, sizeof services);
  services.ctx = m; services.movers = m->movers; services.gravity = services_gravity; services.powerups = services_powerups;
  services.bad_area = services_bad_area; services.bad_area_entity = services_bad_area_entity; services.mark_tesla_area = services_mark_tesla_area;
  const char *selected[2]; size_t selected_count = 0;
  if (options.edition == QA_Q2_EDITION_RERELEASE) { selected[0] = "xatrix"; selected[1] = "rogue"; selected_count = 2; }
  else if (strcmp(options.program, "xatrix") == 0) { selected[0] = "xatrix"; selected_count = 1; }
  else if (strcmp(options.program, "rogue") == 0) { selected[0] = "rogue"; selected_count = 1; }
  qa_q2mpm_weapons pack_weapons = qa_q2mp_projectiles_monster_weapons(&m->projectiles);
  for (size_t i = 0; i < selected_count; i++) {
    m->packs[i].pack = selected[i];
    if (!qa_q2mpm_monsters_init(&m->packs[i].monsters, strcmp(selected[i], "xatrix") == 0 ? QA_Q2MPM_PACK_XATRIX : QA_Q2MPM_PACK_ROGUE,
                                &m->monsters, services, &pack_weapons, options.edition)) qa_range_error("Out of memory");
    m->pack_count = i + 1;
  }
  /* The rerelease registration runs before the module list is built: the C
   * module tables read the registered monster callbacks when the list is
   * built, where TS reads them at createGame (as in foreign_q2_world.c). */
  if (options.edition == QA_Q2_EDITION_RERELEASE) {
    qa_q2mpm_monster_state *source = NULL;
    for (size_t i = 0; i < m->pack_count; i++)
      if (strcmp(m->packs[i].pack, "rogue") == 0) { source = &m->packs[i].monsters.source; break; }
    if (source == NULL) qa_raise("Error", 0, 0, "Rerelease monster registration requires its Rogue state");
    qa_q2rm_monster_options rerelease; memset(&rerelease, 0, sizeof rerelease);
    rerelease.source = source; rerelease.weapons = pack_weapons; rerelease.is_n64 = false;
    rerelease.expansion = strcmp(options.program, "baseq2") == 0 ? "base" : strcmp(options.program, "mg2") == 0 ? "mg1" : options.program;
    qa_q2rm_register_rerelease_monsters(&m->rerelease, &m->monsters, rerelease);
    m->has_rerelease = true;
  }
  add_module(m, qa_q2bm_classic_base_as_spawn(&m->base));
  for (size_t i = 0; i < m->pack_count; i++) add_module(m, qa_q2mpm_monsters_as_spawn(&m->packs[i].monsters));
  if (m->has_rerelease) add_module(m, qa_q2rm_monsters_as_spawn(&m->rerelease));
  m->game = options.create_game(options.ctx, &m->monsters, m->modules, m->module_count);
  if (m->game == NULL) qa_range_error("Out of memory");
  qa_q2_game_services *game = game_of(m);
  qa_q2w_ballistics_register_callbacks(&m->ballistics, game);
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2mp_projectiles_callbacks(&m->projectiles));
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2_mover_module_callbacks(m->movers));
  return m;
}
void qa_app_sim_selected_q2_monster_modules_destroy(modules_t *m) {
  if (m == NULL) return;
  if (m->has_rerelease) qa_q2rm_monsters_module_destroy(&m->rerelease);
  for (size_t i = 0; i < m->pack_count; i++) qa_q2mpm_monsters_destroy(&m->packs[i].monsters);
  qa_q2_mover_module_destroy(m->movers);
  qa_q2bm_classic_base_module_destroy(&m->base);
  qa_q2m_monsters_destroy(&m->monsters);
  qa_q2w_ballistics_destroy(&m->ballistics);
  free(m);
}
