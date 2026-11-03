/* Port of src/app/bootstrap/simulation/foreign-q2-world.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/foreign_q2_world.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/scenery.h"
#include "qa/content/q2/foundation/targets.h"
#include "qa/content/q2/missionpacks/projectiles/mines.h"
#include "qa/foundation/support.h"
#include "qa/content/composition/q2_q3_items.h"

typedef qa_app_sim_foreign_q2_world_modules world_t;
static qa_q2_game_services *game_of(world_t *w) { return qa_q2_entity_services_game(w->game); }

/* ---- { ...options.weapons, noise }: the caller's leaves keep weapons.ctx. ---- */
static double weapons_quad(void *ctx, qa_actor_id actor) {
  world_t *w = ctx;
  return w->options.weapons.quad_multiplier(w->options.weapons.ctx, actor);
}
static double weapons_source_damage(void *ctx, qa_actor_id actor) {
  world_t *w = ctx;
  return w->options.weapons.source_damage_multiplier(w->options.weapons.ctx, actor);
}
static double weapons_firing_interval(void *ctx, qa_actor_id actor, double seconds) {
  world_t *w = ctx;
  return w->options.weapons.firing_interval(w->options.weapons.ctx, actor, seconds);
}
static void weapons_emit(void *ctx, qa_q2w_event event) {
  world_t *w = ctx;
  w->options.weapons.emit(w->options.weapons.ctx, event);
}
static void weapons_noise(void *ctx, qa_actor_id actor, qa_vec3 origin, bool secondary) {
  world_t *w = ctx;
  qa_q2m_monsters_report_noise(&w->monsters, actor, origin, secondary);
}
static void weapons_dodge(void *ctx, qa_actor_id monster, qa_actor_id attacker, double eta, qa_trace_result trace) {
  world_t *w = ctx;
  w->options.weapons.dodge(w->options.weapons.ctx, monster, attacker, eta, trace);
}
static void *weapons_lag_begin(void *ctx, qa_actor_id actor, qa_vec3 start, qa_vec3 direction) {
  world_t *w = ctx;
  return w->options.weapons.lag_begin(w->options.weapons.ctx, actor, start, direction);
}
static void weapons_lag_end(void *ctx, void *token) {
  world_t *w = ctx;
  w->options.weapons.lag_end(w->options.weapons.ctx, token);
}
static void weapons_ammo_changed(void *ctx, qa_actor_id actor, qa_item_id ammo) {
  world_t *w = ctx;
  w->options.weapons.ammo_changed(w->options.weapons.ctx, actor, ammo);
}
static bool weapons_can_target(void *ctx, const qa_actor_id *attacker, qa_actor_id target) {
  world_t *w = ctx;
  return w->options.weapons.can_target(w->options.weapons.ctx, attacker, target);
}

/* ---- mover hooks ---- */
static void movers_path_corner(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id actor) {
  world_t *w = ctx;
  qa_q2m_monsters_touch_path_corner(&w->monsters, entity, game, actor);
}
static void movers_combat_point(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id actor) {
  world_t *w = ctx;
  qa_q2m_monsters_touch_combat_point(&w->monsters, entity, game, actor);
}

/* ---- Q2MissionPackProjectiles hooks ---- */
static qa_q2m_context *projectiles_monster(void *ctx, qa_actor_id actor) {
  world_t *w = ctx;
  return qa_q2m_monsters_context(&w->monsters, actor);
}
static void projectiles_player_effect(void *ctx, qa_q2mp_player_effect effect) {
  world_t *w = ctx;
  w->options.player_effect(w->options.ctx, effect);
}

/* ---- Q2MissionPackMonsterServices ---- */
static qa_q2mp_mines *mines_of(world_t *w) { return &w->projectiles.base.base; }
static double services_gravity(void *ctx) {
  world_t *w = ctx;
  return w->options.gravity(w->options.ctx);
}
static qa_q2mpm_powerups services_powerups(void *ctx, qa_actor_id actor) {
  world_t *w = ctx;
  return w->options.powerups(w->options.ctx, actor);
}
static bool services_bad_area(void *ctx, qa_actor_id actor) {
  world_t *w = ctx;
  return qa_q2mp_mines_bad_area(mines_of(w), actor, game_of(w));
}
static qa_q2_entity *services_bad_area_entity(void *ctx, qa_actor_id actor, const qa_vec3 *origin) {
  world_t *w = ctx;
  return qa_q2mp_mines_bad_area_entity(mines_of(w), actor, game_of(w), origin);
}
static bool services_mark_tesla_area(void *ctx, qa_q2_entity *self, qa_q2_entity *tesla) {
  world_t *w = ctx;
  return qa_q2mp_mines_mark_tesla_area(mines_of(w), self->actor, game_of(w), tesla);
}

/* ---- Q2MissionPackEntities hooks (per pack) ---- */
static void pack_teleport_player(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles) {
  qa_app_sim_foreign_q2_world_pack *pack = (qa_app_sim_foreign_q2_world_pack *)((char *)ctx - offsetof(qa_app_sim_foreign_q2_world_pack, hooks));
  world_t *w = pack->hooks.world;
  w->options.teleport_player(w->options.ctx, actor, origin, angles);
}
static void pack_target_anger(void *ctx, qa_q2_entity *entity, qa_q2_entity *target, qa_q2_game_services *game) {
  qa_app_sim_foreign_q2_world_pack *pack = (qa_app_sim_foreign_q2_world_pack *)((char *)ctx - offsetof(qa_app_sim_foreign_q2_world_pack, hooks));
  qa_q2mpm_monsters_target_anger(&pack->monsters, entity, target, game);
}
static void pack_emit(void *ctx, qa_q2mp_entity_event event) {
  (void)ctx; (void)event; /* TS emit: () => undefined. */
}
static bool pack_entities_spawn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2mp_entities_spawn(ctx, entity, game);
}

/* ---- Q2BaseEntityHooks ---- */
static void base_teleport_player(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles) {
  world_t *w = ctx;
  w->options.teleport_player(w->options.ctx, actor, origin, angles);
}
static void base_player_push(void *ctx, qa_actor_id actor, qa_vec3 velocity) {
  world_t *w = ctx;
  w->options.player_push(w->options.ctx, actor, velocity);
}
static void base_set_actor_gravity(void *ctx, qa_actor_id actor, double gravity) {
  world_t *w = ctx;
  w->options.set_actor_gravity(w->options.ctx, actor, gravity);
}
/* TS new Date() getHours/getMinutes/getSeconds: local wall-clock time. */
static qa_q2_base_local_time base_local_time(void *ctx) {
  (void)ctx;
  time_t now = time(NULL);
  struct tm local;
  qa_q2_base_local_time out = {0, 0, 0};
  if (localtime_r(&now, &local) != NULL) { out.hour = local.tm_hour; out.minute = local.tm_min; out.second = local.tm_sec; }
  return out;
}
static qa_q2m_context *base_turret_driver(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  world_t *w = ctx;
  return qa_q2m_monsters_spawn_infantry_driver(&w->monsters, entity, game);
}
static qa_q2m_context *base_monster_context(void *ctx, qa_actor_id actor) {
  world_t *w = ctx;
  return qa_q2m_monsters_context(&w->monsters, actor);
}
static void base_resume_monster(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  world_t *w = ctx;
  qa_q2m_monsters_resume_monster(&w->monsters, entity, game);
}

static void add_module(world_t *w, qa_q2_spawn_module module) {
  if (w->module_count == sizeof w->modules / sizeof *w->modules) qa_range_error("Foreign Q2 world module table is full");
  w->modules[w->module_count++] = module;
}

world_t *qa_app_sim_create_foreign_q2_world_modules(qa_app_sim_foreign_q2_world_options options) {
  world_t *w = calloc(1, sizeof *w);
  if (w == NULL) qa_range_error("Out of memory");
  w->options = options;
  qa_q2w_hooks weapons = options.weapons;
  weapons.ctx = w;
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
  if (!qa_q2w_ballistics_init(&w->ballistics, weapons)) qa_range_error("Out of memory");
  if (!qa_q2m_monsters_init(&w->monsters, qa_q2m_weapons_from_ballistics(&w->ballistics), options.behavior))
    qa_range_error("Out of memory");
  if (!qa_q2bm_register_classic_base_monsters(&w->base, &w->monsters)) qa_range_error("Out of memory");
  qa_q2_mover_hooks mover_hooks = {w, movers_path_corner, movers_combat_point};
  w->movers = qa_q2_create_mover_module(mover_hooks);
  if (w->movers == NULL) qa_range_error("Out of memory");
  qa_q2mp_projectile_hooks projectile_hooks;
  memset(&projectile_hooks, 0, sizeof projectile_hooks);
  projectile_hooks.base = &w->ballistics;
  projectile_hooks.gravity_ctx = options.ctx;
  projectile_hooks.gravity = options.gravity;
  projectile_hooks.ctx = w;
  projectile_hooks.monster = projectiles_monster;
  projectile_hooks.player_effect = projectiles_player_effect;
  qa_q2mp_projectiles_init(&w->projectiles, projectile_hooks);
  qa_q2mpm_services services;
  memset(&services, 0, sizeof services);
  services.ctx = w;
  services.movers = w->movers;
  services.gravity = services_gravity;
  services.powerups = services_powerups;
  services.bad_area = services_bad_area;
  services.bad_area_entity = services_bad_area_entity;
  services.mark_tesla_area = services_mark_tesla_area;
  const char *selected[2];
  size_t selected_count = 0;
  if (options.edition == QA_Q2_EDITION_RERELEASE) { selected[0] = "xatrix"; selected[1] = "rogue"; selected_count = 2; }
  else if (strcmp(options.program, "xatrix") == 0) { selected[0] = "xatrix"; selected_count = 1; }
  else if (strcmp(options.program, "rogue") == 0) { selected[0] = "rogue"; selected_count = 1; }
  qa_q2mpm_weapons pack_weapons = qa_q2mp_projectiles_monster_weapons(&w->projectiles);
  for (size_t i = 0; i < selected_count; i++) {
    qa_app_sim_foreign_q2_world_pack *pack = &w->packs[i];
    bool xatrix = strcmp(selected[i], "xatrix") == 0;
    pack->pack = selected[i];
    pack->hooks.world = w;
    pack->hooks.index = i;
    if (!qa_q2mpm_monsters_init(&pack->monsters, xatrix ? QA_Q2MPM_PACK_XATRIX : QA_Q2MPM_PACK_ROGUE, &w->monsters, services,
                                &pack_weapons, options.edition)) qa_range_error("Out of memory");
    w->pack_count = i + 1;
    qa_q2mp_entity_hooks entity_hooks;
    memset(&entity_hooks, 0, sizeof entity_hooks);
    entity_hooks.movers = w->movers;
    entity_hooks.weapons = &w->ballistics;
    entity_hooks.projectiles = mines_of(w);
    entity_hooks.ctx = &pack->hooks;
    entity_hooks.teleport_player = pack_teleport_player;
    entity_hooks.target_anger = pack_target_anger;
    entity_hooks.emit = pack_emit;
    qa_q2mp_entities_init(&pack->entities, xatrix ? QA_Q2MP_PACK_XATRIX : QA_Q2MP_PACK_ROGUE, entity_hooks);
  }
  qa_q2_base_entity_hooks base_hooks;
  memset(&base_hooks, 0, sizeof base_hooks);
  base_hooks.movers = w->movers;
  base_hooks.weapons = &w->ballistics;
  base_hooks.ctx = w;
  base_hooks.teleport_player = base_teleport_player;
  base_hooks.player_push = base_player_push;
  base_hooks.set_actor_gravity = base_set_actor_gravity;
  base_hooks.local_time = base_local_time;
  base_hooks.turret_driver = base_turret_driver;
  base_hooks.monster_context = base_monster_context;
  base_hooks.resume_monster = base_resume_monster;
  w->base_entities = qa_q2_create_base_entity_module(base_hooks);
  if (w->base_entities == NULL) qa_range_error("Out of memory");
  qa_q2_item_hooks item_hooks = options.item_hooks;
  item_hooks.edition = options.edition;
  item_hooks.foreign_items = qa_composition_q3_foreign_item_definitions;
  w->items = qa_q2_create_item_module(item_hooks);
  if (w->items == NULL) qa_range_error("Out of memory");
  /* TS pushes the rerelease module after the list is built; its registration
   * runs first here because the C module tables read the registered monster
   * callbacks when the list is built, where TS reads them at createGame. */
  if (options.edition == QA_Q2_EDITION_RERELEASE) {
    qa_q2mpm_monster_state *source = NULL;
    for (size_t i = 0; i < w->pack_count; i++)
      if (strcmp(w->packs[i].pack, "rogue") == 0) { source = &w->packs[i].monsters.source; break; }
    if (source == NULL) qa_raise("Error", 0, 0, "Rerelease monster registration requires its Rogue state");
    qa_q2rm_monster_options monsters;
    memset(&monsters, 0, sizeof monsters);
    monsters.source = source;
    monsters.weapons = pack_weapons;
    monsters.is_n64 = false;
    monsters.expansion = strcmp(options.program, "baseq2") == 0 ? "base" : strcmp(options.program, "mg2") == 0 ? "mg1" : options.program;
    qa_q2rm_register_rerelease_monsters(&w->rerelease, &w->monsters, monsters);
    w->has_rerelease = true;
  }
  for (size_t i = 0; i < w->pack_count; i++) {
    qa_app_sim_foreign_q2_world_pack *pack = &w->packs[i];
    pack->entity_callbacks = qa_q2mp_entities_callbacks(&pack->entities);
    qa_q2_spawn_module module;
    memset(&module, 0, sizeof module);
    module.ctx = &pack->entities;
    module.callbacks = &pack->entity_callbacks;
    module.spawn = pack_entities_spawn;
    add_module(w, module);
  }
  add_module(w, qa_q2_base_entity_module_as_spawn(w->base_entities));
  add_module(w, qa_q2_create_target_module());
  add_module(w, qa_q2_mover_module_as_spawn(w->movers));
  add_module(w, qa_q2_create_scenery_module());
  add_module(w, qa_q2_item_module_as_spawn(w->items));
  add_module(w, qa_q2bm_classic_base_as_spawn(&w->base));
  for (size_t i = 0; i < w->pack_count; i++) add_module(w, qa_q2mpm_monsters_as_spawn(&w->packs[i].monsters));
  if (w->has_rerelease) add_module(w, qa_q2rm_monsters_as_spawn(&w->rerelease));
  w->game = options.create_game(options.ctx, &w->monsters, w->modules, w->module_count);
  if (w->game == NULL) qa_range_error("Out of memory");
  qa_q2_game_services *game = game_of(w);
  qa_q2w_ballistics_register_callbacks(&w->ballistics, game);
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2mp_projectiles_callbacks(&w->projectiles));
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2_mover_module_callbacks(w->movers));
  return w;
}

void qa_app_sim_foreign_q2_world_modules_destroy(world_t *w) {
  if (w == NULL) return;
  if (w->has_rerelease) qa_q2rm_monsters_module_destroy(&w->rerelease);
  for (size_t i = 0; i < w->pack_count; i++) qa_q2mpm_monsters_destroy(&w->packs[i].monsters);
  qa_q2_item_module_destroy(w->items);
  qa_q2_base_entity_module_destroy(w->base_entities);
  qa_q2_mover_module_destroy(w->movers);
  qa_q2bm_classic_base_module_destroy(&w->base);
  qa_q2m_monsters_destroy(&w->monsters);
  qa_q2w_ballistics_destroy(&w->ballistics);
  free(w);
}
