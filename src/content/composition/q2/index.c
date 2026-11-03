/* Port of src/content/composition/q2/index.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q2/index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/scenery.h"
#include "qa/content/q2/foundation/targets.h"
#include "qa/content/q2/missionpacks/modes/deathball.h"
#include "qa/core/cvars/info.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/content/composition/q2_q3_items.h"

typedef qa_q2_product_runtime runtime_t;
#define CFG(r) (&(r)->configuration)

static qa_q2_game_services *game_of(runtime_t *r) { return r->services; }
static qa_q2_entity *entity_of(runtime_t *r, qa_actor_id actor) {
  qa_q2_game_services *game = game_of(r);
  return game->entity(game->ctx, &actor);
}
static void *xcalloc(size_t size) {
  void *value = calloc(1, size);
  if (value == NULL) qa_range_error("out of memory");
  return value;
}

/* TS packs(). */
static size_t packs_of(const runtime_t *r, qa_q2mp_pack out[2]) {
  const qa_q2_composition_options *c = &r->configuration;
  if (c->edition == QA_Q2_EDITION_RERELEASE) { out[0] = QA_Q2MP_PACK_XATRIX; out[1] = QA_Q2MP_PACK_ROGUE; return 2; }
  if (c->program == QA_Q2_PROGRAM_BASEQ2) {
    if (c->match != NULL && (c->match->kind == QA_Q2_MATCH_TAG || c->match->kind == QA_Q2_MATCH_DEATHBALL)) {
      out[0] = QA_Q2MP_PACK_ROGUE;
      return 1;
    }
    return 0;
  }
  out[0] = c->program == QA_Q2_PROGRAM_XATRIX ? QA_Q2MP_PACK_XATRIX : QA_Q2MP_PACK_ROGUE;
  return 1;
}
static qa_q2_match_kind match_kind(const runtime_t *r) {
  return r->configuration.match == NULL ? QA_Q2_MATCH_STANDARD : r->configuration.match->kind;
}

/* ---- Q2Monsters hooks ---- */
static void monsters_coop_kill(void *ctx, qa_actor_id attacker) {
  runtime_t *r = ctx;
  qa_q2_base_player_state *state = qa_q2_players_get_state(r->players, attacker);
  if (state != NULL) state->score++;
}
static void monsters_drop_item(void *ctx, qa_owned_actor actor, qa_q2_game_services *game, const char *classname) {
  runtime_t *r = ctx;
  (void)qa_q2_item_module_drop_monster(r->items, actor, game, classname);
}
static const char *monsters_platform_state(void *ctx, qa_actor_id actor) {
  runtime_t *r = ctx;
  qa_q2_entity *entity = entity_of(r, actor);
  if (entity == NULL) return NULL;
  for (size_t i = 0; i < r->expansion_count; i++) {
    qa_q2mp_entities *entities = &r->expansions[i].entities;
    qa_q2mp_platform_state phase;
    if (entities->has_movers && qa_q2mp_rogue_movers_platform_state(&entities->movers, entity, &phase))
      return qa_q2mp_platform_phase_name(phase.phase);
  }
  qa_q2_base_platform_state *base = qa_q2_base_entity_module_platform_state(r->base_entities, entity);
  return base == NULL ? NULL : base->phase;
}

/* ---- mover hooks ---- */
static void movers_path_corner(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id other) {
  runtime_t *r = ctx;
  qa_q2m_monsters_touch_path_corner(&r->monsters, entity, game, other);
}
static void movers_combat_point(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id other) {
  runtime_t *r = ctx;
  qa_q2m_monsters_touch_combat_point(&r->monsters, entity, game, other);
}

/* ---- base entity hooks: { ...configuration.entityHooks, ... } ---- */
static void teleport_player(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles) {
  runtime_t *r = ctx;
  qa_q2_entity *entity = entity_of(r, actor);
  if (entity != NULL) qa_q2_players_teleport_player(r->players, entity, game_of(r), origin, angles);
}
static void entity_player_push(void *ctx, qa_actor_id player, qa_vec3 velocity) {
  runtime_t *r = ctx;
  CFG(r)->entity_hooks.player_push(CFG(r)->entity_hooks.ctx, player, velocity);
}
static void entity_set_actor_gravity(void *ctx, qa_actor_id actor, double gravity) {
  runtime_t *r = ctx;
  CFG(r)->entity_hooks.set_actor_gravity(CFG(r)->entity_hooks.ctx, actor, gravity);
}
static qa_q2_base_local_time entity_local_time(void *ctx) {
  runtime_t *r = ctx;
  return CFG(r)->entity_hooks.local_time(CFG(r)->entity_hooks.ctx);
}
static qa_q2m_context *entity_turret_driver(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  runtime_t *r = ctx;
  return qa_q2m_monsters_spawn_infantry_driver(&r->monsters, entity, game);
}
static qa_q2m_context *entity_monster_context(void *ctx, qa_actor_id actor) {
  runtime_t *r = ctx;
  return qa_q2m_monsters_context(&r->monsters, actor);
}
static void entity_resume_monster(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  runtime_t *r = ctx;
  qa_q2m_monsters_resume_monster(&r->monsters, entity, game);
}

/* ---- item hooks: { ...configuration.itemHooks, edition, pickedUp,
 * randomRespawn?, weaponPicked, ammoPack } ---- */
static void items_silencer(void *ctx, qa_actor_id player, double charges) {
  runtime_t *r = ctx;
  CFG(r)->item_hooks.silencer(CFG(r)->item_hooks.ctx, player, charges);
}
static void items_power_armor(void *ctx, qa_actor_id player, const char *kind) {
  runtime_t *r = ctx;
  CFG(r)->item_hooks.power_armor(CFG(r)->item_hooks.ctx, player, kind);
}
static double items_weapon_respawn_seconds(void *ctx) {
  runtime_t *r = ctx;
  return CFG(r)->item_hooks.weapon_respawn_seconds(CFG(r)->item_hooks.ctx);
}
static void items_picked_up(void *ctx, qa_actor_id actor, qa_item_id item, bool usable) {
  runtime_t *r = ctx;
  qa_q2_base_player_state *state = qa_q2_players_get_state(r->players, actor);
  if (state == NULL) return;
  state->bonus_alpha = 0.25;
  if (usable) state->selected_item = item;
  if (CFG(r)->item_hooks.picked_up != NULL) CFG(r)->item_hooks.picked_up(CFG(r)->item_hooks.ctx, actor, item, usable);
}
static qa_q2_entity *items_random_respawn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  runtime_t *r = ctx;
  qa_q2mp_random_item_settings settings = CFG(r)->services.random_items(CFG(r)->services.ctx);
  return r->armory == NULL ? NULL : qa_q2mp_items_random_respawn(&r->armory->items, entity, game, settings);
}
static void items_weapon_picked(void *ctx, qa_actor_id actor, qa_item_id item, bool first) {
  runtime_t *r = ctx;
  if (r->rerelease != NULL) {
    qa_q2_entity *entity = entity_of(r, actor);
    if (entity != NULL) {
      qa_q2_rerelease_players_weapon_picked(&r->rerelease->players, entity, game_of(r), item, first);
      return;
    }
  }
  CFG(r)->item_hooks.weapon_picked(CFG(r)->item_hooks.ctx, actor, item, first);
}
static void items_ammo_pack(void *ctx, qa_owned_actor actor, qa_q2_game_services *game, bool full) {
  runtime_t *r = ctx;
  if (CFG(r)->item_hooks.ammo_pack != NULL) CFG(r)->item_hooks.ammo_pack(CFG(r)->item_hooks.ctx, actor, game, full);
  if (r->armory != NULL) {
    qa_q2mp_pack packs[2];
    size_t count = packs_of(r, packs);
    for (size_t i = 0; i < count; i++) qa_q2mp_items_ammo_pack(&r->armory->items, actor, game, full, packs[i]);
  }
}

/* ---- armory hooks ---- */
static struct qa_q2m_context *armory_monster(void *ctx, qa_actor_id actor) {
  runtime_t *r = ctx;
  return qa_q2m_monsters_context(&r->monsters, actor);
}
static double services_gravity(void *ctx) {
  runtime_t *r = ctx;
  return CFG(r)->services.gravity(CFG(r)->services.ctx);
}
static bool armory_intermission(void *ctx) {
  runtime_t *r = ctx;
  return strcmp(qa_q2_players_intermission(r->players).kind, "intermission") == 0;
}
static void armory_player_effect(void *ctx, qa_q2mp_player_effect effect) {
  runtime_t *r = ctx;
  qa_q2_composition_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_COMPOSITION_EVENT_MISSIONPACK_PLAYER;
  event.player_effect = &effect;
  CFG(r)->services.emit(CFG(r)->services.ctx, event);
}

/* ---- per-expansion entity and monster hooks ---- */
typedef struct expansion_ctx { runtime_t *runtime; size_t index; } expansion_ctx;
static runtime_t *expansion_runtime(void *ctx) { return ((expansion_ctx *)ctx)->runtime; }
static void expansion_teleport(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles) {
  teleport_player(expansion_runtime(ctx), actor, origin, angles);
}
static void expansion_target_anger(void *ctx, qa_q2_entity *entity, qa_q2_entity *target, qa_q2_game_services *game) {
  expansion_ctx *e = ctx;
  runtime_t *r = e->runtime;
  qa_q2mp_pack pack = r->expansions[e->index].pack;
  for (size_t i = 0; i < r->expansion_count; i++)
    if (r->expansions[i].pack == pack) {
      qa_q2mpm_monsters_target_anger(&r->expansions[i].monsters, entity, target, game);
      return;
    }
  qa_raise("Error", 0, 0, "Missing Q2 %s source monster registration", pack == QA_Q2MP_PACK_XATRIX ? "xatrix" : "rogue");
}
static void expansion_emit(void *ctx, qa_q2mp_entity_event event) {
  runtime_t *r = expansion_runtime(ctx);
  qa_q2_composition_event wrapped;
  memset(&wrapped, 0, sizeof wrapped);
  wrapped.kind = QA_Q2_COMPOSITION_EVENT_MISSIONPACK_ENTITY;
  wrapped.entity_event = &event;
  CFG(r)->services.emit(CFG(r)->services.ctx, wrapped);
}
static double expansion_gravity(void *ctx) { return services_gravity(expansion_runtime(ctx)); }
static qa_q2mp_mines *mines_of(runtime_t *r) { return &r->armory->projectiles.base.base; }
static bool expansion_bad_area(void *ctx, qa_actor_id actor) {
  runtime_t *r = expansion_runtime(ctx);
  return qa_q2mp_mines_bad_area(mines_of(r), actor, game_of(r));
}
static qa_q2_entity *expansion_bad_area_entity(void *ctx, qa_actor_id actor, const qa_vec3 *origin) {
  runtime_t *r = expansion_runtime(ctx);
  return qa_q2mp_mines_bad_area_entity(mines_of(r), actor, game_of(r), origin);
}
static bool expansion_mark_tesla_area(void *ctx, qa_q2_entity *self, qa_q2_entity *tesla) {
  runtime_t *r = expansion_runtime(ctx);
  return qa_q2mp_mines_mark_tesla_area(mines_of(r), self->actor, game_of(r), tesla);
}
static qa_q2mpm_powerups expansion_powerups(void *ctx, qa_actor_id actor) {
  qa_q2_composition_powerups p = qa_q2_product_runtime_powerups(expansion_runtime(ctx), actor);
  qa_q2mpm_powerups out = {p.quad_until, p.double_until, p.invulnerability_until};
  return out;
}

/* ---- match hooks (TS Omit<Q2CtfHooks, "emit">) ---- */
static qa_q2_players *match_players(void *ctx) { return ((runtime_t *)ctx)->players; }
static qa_q2_game_services *match_world(void *ctx) { return game_of(ctx); }
static qa_q2_base_player_state *ctf_player(void *ctx, qa_actor_id actor) {
  return qa_q2_players_get_state(((runtime_t *)ctx)->players, actor);
}
static void print_diagnostic(void *ctx, const char *text) {
  runtime_t *r = ctx;
  CFG(r)->host->diagnostic(CFG(r)->host->ctx, text);
}
static void ctf_set_skin(void *ctx, qa_actor_id actor, const char *skin) {
  runtime_t *r = ctx;
  qa_q2_base_player_state *state = qa_q2_players_get_state(r->players, actor);
  qa_q2_entity *entity = entity_of(r, actor);
  if (state == NULL || entity == NULL) qa_raise("Error", 0, 0, "Match skin requires an admitted player");
  qa_info_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  options.maximum_length = 512;
  options.target = QA_CVAR_INFO_CLIENT_USERINFO;
  options.server_high_characters = false;
  options.print = print_diagnostic;
  options.print_ctx = r;
  char *info = qa_set_info_value(state->userinfo, "skin", skin, &options);
  if (info == NULL) qa_range_error("out of memory");
  qa_q2_players_userinfo_changed(r->players, entity, game_of(r), info);
  free(info);
}
static void ctf_spawn_player(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_players_put_in_server(((runtime_t *)ctx)->players, entity, game, true, NULL);
}
static void ctf_observer(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  runtime_t *r = ctx;
  (void)game;
  qa_q2_player_movement_change change;
  memset(&change, 0, sizeof change);
  change.kind = "noclip";
  change.enabled = true;
  CFG(r)->player_hooks.set_movement(CFG(r)->player_hooks.ctx, entity->actor.id, change);
}
static void ctf_teleport(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 origin, qa_vec3 angles,
                         qa_vec3 velocity) {
  runtime_t *r = ctx;
  qa_q2_players_teleport_player(r->players, entity, game, origin, angles);
  qa_q2_body_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_velocity = true;
  changes.velocity = velocity;
  qa_q2_game_move(game, entity, changes, true);
  qa_q2_player_movement movement = CFG(r)->player_hooks.movement(CFG(r)->player_hooks.ctx, entity->actor.id);
  qa_q2_player_movement_change change;
  memset(&change, 0, sizeof change);
  change.kind = "teleport";
  change.origin = origin; change.angles = angles; change.velocity = velocity;
  change.command_angles = movement.command_angles;
  change.hold_milliseconds = 160;
  change.spectator = false;
  CFG(r)->player_hooks.set_movement(CFG(r)->player_hooks.ctx, entity->actor.id, change);
}
static void ctf_chase(void *ctx, qa_actor_id actor) {
  runtime_t *r = ctx;
  qa_q2_entity *entity = entity_of(r, actor);
  if (entity != NULL) qa_q2_players_chase(r->players, entity, game_of(r), 1, true);
}
static void ctf_set_grapple_prediction(void *ctx, qa_actor_id actor, bool suppressed) {
  runtime_t *r = ctx;
  qa_q2_composition_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_COMPOSITION_EVENT_GRAPPLE_PREDICTION;
  event.actor = actor;
  event.suppressed = suppressed;
  CFG(r)->services.emit(CFG(r)->services.ctx, event);
}
static void ctf_end_level(void *ctx, qa_q2_game_services *game, const char *map) {
  runtime_t *r = ctx;
  if (map == NULL) qa_q2_players_end_deathmatch_level(r->players, game);
  else qa_q2_players_begin_intermission(r->players, game, map, NULL);
}
static void ctf_kick(void *ctx, qa_actor_id actor) {
  runtime_t *r = ctx;
  qa_q2_composition_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_COMPOSITION_EVENT_KICK;
  event.actor = actor;
  CFG(r)->services.emit(CFG(r)->services.ctx, event);
}
static void ctf_set_deathmatch_flags(void *ctx, double flags) { qa_q2_product_runtime_set_deathmatch_flags(ctx, flags); }
static bool ctf_chat_allowed(void *ctx, qa_actor_id actor, qa_q2_game_services *game) {
  return qa_q2_players_chat_allowed(((runtime_t *)ctx)->players, actor, game);
}

/* ---- player hooks: { ...configuration.playerHooks, overrides } ---- */
#define PH(r) (&CFG(r)->player_hooks)
static double ph_quad_fire_drop_until(void *ctx, qa_actor_id actor) {
  runtime_t *r = ctx;
  bool drops = CFG(r)->edition == QA_Q2_EDITION_RERELEASE
      ? (CFG(r)->services.drop_quad_fire != NULL ? CFG(r)->services.drop_quad_fire(CFG(r)->services.ctx) : true)
      : (qa_js_to_int32(qa_q2_options_deathmatch_flags(&game_of(r)->options)) & 0x10000) != 0;
  return drops && r->armory != NULL ? qa_q2mp_items_powerups(&r->armory->items, actor).quad_fire_until : 0;
}
static bool ph_grant_selected_arsenal(void *ctx, qa_actor_id actor, const char *category) {
  runtime_t *r = ctx;
  return PH(r)->grant_selected_arsenal(PH(r)->ctx, actor, category);
}
static bool ph_give_selected_item(void *ctx, qa_actor_id actor, const char *const *args, size_t count) {
  runtime_t *r = ctx;
  return PH(r)->give_selected_item(PH(r)->ctx, actor, args, count);
}
static bool ph_weapon_state(void *ctx, qa_actor_id actor, qa_q2_character_weapon *out) {
  runtime_t *r = ctx;
  return PH(r)->weapon_state(PH(r)->ctx, actor, out);
}
static qa_q2_player_movement ph_movement(void *ctx, qa_actor_id actor) {
  runtime_t *r = ctx;
  return PH(r)->movement(PH(r)->ctx, actor);
}
static void ph_set_movement(void *ctx, qa_actor_id actor, qa_q2_player_movement_change change) {
  runtime_t *r = ctx;
  PH(r)->set_movement(PH(r)->ctx, actor, change);
}
static void ph_emit(void *ctx, qa_q2_player_event event) {
  runtime_t *r = ctx;
  PH(r)->emit(PH(r)->ctx, event);
}
static void ph_noise(void *ctx, qa_actor_id actor, qa_vec3 origin) {
  runtime_t *r = ctx;
  PH(r)->noise(PH(r)->ctx, actor, origin);
}
static qa_q2w_input ph_weapon_input(void *ctx, qa_actor_id actor) {
  runtime_t *r = ctx;
  return qa_q2_product_runtime_weapon_input(r, actor, PH(r)->weapon_input(PH(r)->ctx, actor));
}
static bool ph_banned(void *ctx, const char *address) {
  runtime_t *r = ctx;
  return PH(r)->banned(PH(r)->ctx, address);
}
static void ph_score(void *ctx, qa_q2_entity *victim, qa_q2_entity *attacker, qa_q2_game_services *game, double change,
                     double means, qa_q2_entity *recipient) {
  runtime_t *r = ctx;
  if (match_kind(r) == QA_Q2_MATCH_STANDARD && PH(r)->score != NULL) {
    PH(r)->score(PH(r)->ctx, victim, attacker, game, change, means, recipient);
    return;
  }
  qa_q2_product_match_score(&r->match, victim, game, change, means, recipient, true, attacker);
}
static void ph_player_spawned(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  runtime_t *r = ctx;
  qa_q2_product_match_player_spawned(&r->match, entity, game);
  if (PH(r)->player_spawned != NULL) PH(r)->player_spawned(PH(r)->ctx, entity, game);
}
static void ph_persistent_inventory_initialized(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  runtime_t *r = ctx;
  PH(r)->persistent_inventory_initialized(PH(r)->ctx, entity, game);
}
/* TS match.selectSpawn ?? rogueSpawns?.selectSpawn ?? configured ?? null. */
static bool ph_select_spawn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 *out_origin,
                            qa_vec3 *out_angles) {
  runtime_t *r = ctx;
  if (qa_q2_product_match_select_spawn(&r->match, entity, game, out_origin, out_angles)) return true;
  if (r->rogue_spawns != NULL) {
    qa_q2mp_spawn_point point;
    if (qa_q2mp_rogue_player_spawns_select_spawn(r->rogue_spawns, game, &point)) {
      *out_origin = point.origin;
      *out_angles = point.angles;
      return true;
    }
  }
  return PH(r)->select_spawn != NULL && PH(r)->select_spawn(PH(r)->ctx, entity, game, out_origin, out_angles);
}
static bool ph_command(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, const char *name,
                       const char *const *args, size_t count) {
  runtime_t *r = ctx;
  return qa_q2_product_match_command(&r->match, entity, game, name, args, count) ||
         (PH(r)->command != NULL && PH(r)->command(PH(r)->ctx, entity, game, name, args, count));
}
static void ph_drop_inventory(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game,
                              const qa_attack_provenance *attack) {
  runtime_t *r = ctx;
  PH(r)->drop_inventory(PH(r)->ctx, entity, game, attack);
}
static void ph_before_death_inventory(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game,
                                      const qa_attack_provenance *attack) {
  runtime_t *r = ctx;
  qa_q2_product_match_drop_inventory(&r->match, entity, game);
  if (PH(r)->before_death_inventory != NULL) PH(r)->before_death_inventory(PH(r)->ctx, entity, game, attack);
}
static void ph_death(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, const qa_attack_provenance *attack) {
  runtime_t *r = ctx;
  qa_q2_product_match_death(&r->match, entity, game);
  if (r->armory != NULL) {
    qa_q2mp_spheres_owner_died(&r->armory->spheres, entity->actor.id, game, attack);
    qa_q2mp_items_reset(&r->armory->items, entity->actor.id);
  }
  if (PH(r)->death != NULL) PH(r)->death(PH(r)->ctx, entity, game, attack);
}
static void ph_disconnect(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  runtime_t *r = ctx;
  qa_q2_product_match_disconnect(&r->match, entity, game);
  if (r->armory != NULL) {
    qa_q2mp_spheres_disconnect(&r->armory->spheres, entity->actor.id, game);
    qa_q2mp_items_reset(&r->armory->items, entity->actor.id);
  }
  if (PH(r)->disconnect != NULL) PH(r)->disconnect(PH(r)->ctx, entity, game);
}
static qa_q2_player_hooks player_hooks(runtime_t *r) {
  const qa_q2_player_hooks *c = PH(r);
  qa_q2_player_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = r;
  hooks.quad_fire_drop_until = ph_quad_fire_drop_until;
  hooks.grant_selected_arsenal = c->grant_selected_arsenal != NULL ? ph_grant_selected_arsenal : NULL;
  hooks.give_selected_item = c->give_selected_item != NULL ? ph_give_selected_item : NULL;
  hooks.weapon_state = c->weapon_state != NULL ? ph_weapon_state : NULL;
  hooks.movement = c->movement != NULL ? ph_movement : NULL;
  hooks.set_movement = c->set_movement != NULL ? ph_set_movement : NULL;
  hooks.emit = c->emit != NULL ? ph_emit : NULL;
  hooks.noise = c->noise != NULL ? ph_noise : NULL;
  hooks.weapon_input = ph_weapon_input;
  hooks.banned = c->banned != NULL ? ph_banned : NULL;
  hooks.score = ph_score;
  hooks.player_spawned = ph_player_spawned;
  hooks.persistent_inventory_initialized =
      c->persistent_inventory_initialized != NULL ? ph_persistent_inventory_initialized : NULL;
  hooks.select_spawn = ph_select_spawn;
  hooks.death = ph_death;
  hooks.drop_inventory = c->drop_inventory != NULL ? ph_drop_inventory : NULL;
  hooks.before_death_inventory = ph_before_death_inventory;
  hooks.disconnect = ph_disconnect;
  hooks.command = ph_command;
  return hooks;
}

/* ---- rerelease hooks: { ...configuration.rereleaseHooks,
 * monsterHoldsHealthBar, clearExpansionPowerups } ---- */
#define RH(r) (&CFG(r)->rerelease_hooks)
static void rh_emit(void *ctx, qa_q2_rerelease_event event) { runtime_t *r = ctx; RH(r)->emit(RH(r)->ctx, event); }
static const char *rh_light_style(void *ctx, double style) {
  runtime_t *r = ctx;
  return RH(r)->light_style(RH(r)->ctx, style);
}
static qa_q2rr_player_identity rh_player_identity(void *ctx, qa_actor_id actor) {
  runtime_t *r = ctx;
  return RH(r)->player_identity(RH(r)->ctx, actor);
}
static bool rh_clip_trigger(void *ctx, qa_q2_entity *trigger, qa_actor_id player, qa_q2_game_services *game) {
  runtime_t *r = ctx;
  return RH(r)->clip_trigger(RH(r)->ctx, trigger, player, game);
}
static qa_q2rr_navigation rh_navigation(void *ctx, qa_vec3 start, qa_vec3 goal, const qa_actor_id *actor) {
  runtime_t *r = ctx;
  return RH(r)->navigation(RH(r)->ctx, start, goal, actor);
}
static bool rh_monsters_searching(void *ctx, const qa_actor_id *player) {
  runtime_t *r = ctx;
  return RH(r)->monsters_searching(RH(r)->ctx, player);
}
static bool rh_monster_holds_health_bar(void *ctx, qa_actor_id monster) {
  runtime_t *r = ctx;
  if (RH(r)->monster_holds_health_bar != NULL) return RH(r)->monster_holds_health_bar(RH(r)->ctx, monster);
  qa_q2_entity *entity = entity_of(r, monster);
  return entity != NULL && strcmp(entity->classname, "monster_jorg") == 0;
}
static qa_q2rr_powerups_result rh_expansion_powerups(void *ctx, qa_actor_id player) {
  runtime_t *r = ctx;
  return RH(r)->expansion_powerups(RH(r)->ctx, player);
}
static void rh_clear_expansion_powerups(void *ctx, qa_actor_id player) {
  runtime_t *r = ctx;
  qa_q2mp_items_reset(&r->armory->items, player);
  if (RH(r)->clear_expansion_powerups != NULL) RH(r)->clear_expansion_powerups(RH(r)->ctx, player);
}
static void rh_player_collision(void *ctx, qa_actor_id player, bool collide) {
  runtime_t *r = ctx;
  RH(r)->player_collision(RH(r)->ctx, player, collide);
}
static bool rh_grounded_on_world(void *ctx, qa_actor_id player) {
  runtime_t *r = ctx;
  return RH(r)->grounded_on_world(RH(r)->ctx, player);
}
static void rh_push_player(void *ctx, qa_actor_id player, qa_vec3 velocity) {
  runtime_t *r = ctx;
  RH(r)->push_player(RH(r)->ctx, player, velocity);
}
static void rh_set_actor_gravity(void *ctx, qa_actor_id actor, double gravity) {
  runtime_t *r = ctx;
  RH(r)->set_actor_gravity(RH(r)->ctx, actor, gravity);
}
static void rh_set_world_gravity(void *ctx, double gravity) {
  runtime_t *r = ctx;
  RH(r)->set_world_gravity(RH(r)->ctx, gravity);
}
static qa_q2_rerelease_hooks rerelease_hooks(runtime_t *r) {
  const qa_q2_rerelease_hooks *c = RH(r);
  qa_q2_rerelease_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = r;
  hooks.emit = c->emit != NULL ? rh_emit : NULL;
  hooks.light_style = c->light_style != NULL ? rh_light_style : NULL;
  hooks.player_identity = c->player_identity != NULL ? rh_player_identity : NULL;
  hooks.clip_trigger = c->clip_trigger != NULL ? rh_clip_trigger : NULL;
  hooks.navigation = c->navigation != NULL ? rh_navigation : NULL;
  hooks.monsters_searching = c->monsters_searching != NULL ? rh_monsters_searching : NULL;
  hooks.monster_holds_health_bar = rh_monster_holds_health_bar;
  hooks.expansion_powerups = c->expansion_powerups != NULL ? rh_expansion_powerups : NULL;
  hooks.clear_expansion_powerups = rh_clear_expansion_powerups;
  hooks.player_collision = c->player_collision != NULL ? rh_player_collision : NULL;
  hooks.grounded_on_world = c->grounded_on_world != NULL ? rh_grounded_on_world : NULL;
  hooks.push_player = c->push_player != NULL ? rh_push_player : NULL;
  hooks.set_actor_gravity = c->set_actor_gravity != NULL ? rh_set_actor_gravity : NULL;
  hooks.set_world_gravity = c->set_world_gravity != NULL ? rh_set_world_gravity : NULL;
  return hooks;
}
static void rerelease_transfer_healthbar(void *ctx, qa_actor_id old_actor, qa_actor_id new_actor,
                                         qa_q2_game_services *game) {
  runtime_t *r = ctx;
  qa_q2_rerelease_entities_transfer_healthbar_target(&r->rerelease->entities.base, old_actor, new_actor, game);
}

/* ---- CTF pickup policy ---- */
static bool policy_instanced_coop(void *ctx, qa_q2_game_services *game) {
  runtime_t *r = ctx;
  return r->rerelease != NULL && qa_q2_rerelease_module_instanced_coop(&r->rerelease->entities, game);
}
static bool policy_can_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id actor) {
  runtime_t *r = ctx;
  return qa_q2_ctf_pickups_allowed(r->match.ctf) &&
         (r->rerelease == NULL || qa_q2_rerelease_module_can_pickup(&r->rerelease->entities, entity, game, actor));
}
static bool policy_before_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id actor) {
  runtime_t *r = ctx;
  return qa_q2_ctf_pickups_allowed(r->match.ctf) &&
         (r->rerelease == NULL || qa_q2_rerelease_module_before_pickup(&r->rerelease->entities, entity, game, actor));
}
static void policy_before_targets(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id actor,
                                  bool taken) {
  runtime_t *r = ctx;
  if (r->rerelease != NULL) qa_q2_rerelease_module_before_targets(&r->rerelease->entities, entity, game, actor, taken);
}
static void policy_after_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id actor,
                                bool taken) {
  runtime_t *r = ctx;
  if (r->rerelease != NULL) qa_q2_rerelease_module_after_pickup(&r->rerelease->entities, entity, game, actor, taken);
}
static bool policy_keep_after_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id actor) {
  runtime_t *r = ctx;
  return r->rerelease != NULL && qa_q2_rerelease_module_keep_after_pickup(&r->rerelease->entities, entity, game, actor);
}

/* ---- game options closures ---- */
static const char *options_player_name(void *ctx, qa_actor_id actor) {
  runtime_t *r = ctx;
  qa_q2_base_player_state *state = qa_q2_players_get_state(r->players, actor);
  return state == NULL ? NULL : state->name;
}
static double options_deathmatch_flags(void *ctx) {
  runtime_t *r = ctx;
  return CFG(r)->services.deathmatch_flags_read != NULL ? CFG(r)->services.deathmatch_flags_read(CFG(r)->services.ctx)
                                                        : (double)r->active_deathmatch_flags;
}

/* Spawn-module views of modules TS passes as Q2SpawnModule directly. */
static bool rogue_spawns_spawn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2mp_rogue_player_spawns_spawn(ctx, entity, game);
}
static bool pack_entities_spawn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2mp_entities_spawn(ctx, entity, game);
}
static bool pack_items_spawn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2mp_items_spawn(ctx, entity, game);
}
static bool monsters_spawn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2m_monsters_spawn(ctx, entity, game);
}

static void add_module(runtime_t *r, qa_q2_spawn_module module) {
  if (r->module_count == QA_Q2_PRODUCT_MAX_MODULES) qa_range_error("Q2 product module table is full");
  r->modules[r->module_count++] = module;
}
/* A module whose TS callbacks are a getter: the runtime holds the latest
 * result and reads the getter again before each registration. */
static void add_module_with(runtime_t *r, qa_q2_spawn_module module,
                            qa_q2_callback_definitions (*getter)(void *module)) {
  if (r->module_count == QA_Q2_PRODUCT_MAX_MODULES) qa_range_error("Q2 product module table is full");
  r->module_callbacks_getter[r->module_count] = getter;
  r->module_callbacks[r->module_count] = getter(module.ctx);
  module.callbacks = &r->module_callbacks[r->module_count];
  add_module(r, module);
}
static qa_q2_callback_definitions rerelease_module_callbacks(void *module) { return qa_q2_rerelease_module_callbacks(module); }
static qa_q2_callback_definitions match_callbacks(void *module) { return qa_q2_product_match_callbacks(module); }
static qa_q2_callback_definitions pack_entities_callbacks(void *module) { return qa_q2mp_entities_callbacks(module); }
static qa_q2_callback_definitions monsters_callbacks(void *module) { return qa_q2m_monsters_callbacks(module); }

qa_q2_product_runtime *qa_q2_product_runtime_create(const qa_q2_composition_options *configuration) {
  qa_q2_match_kind kind = configuration->match == NULL ? QA_Q2_MATCH_STANDARD : configuration->match->kind;
  if (kind != QA_Q2_MATCH_STANDARD && kind != QA_Q2_MATCH_HORDE && configuration->options.mode != QA_Q2_MODE_DEATHMATCH)
    qa_raise("Error", 0, 0, "Q2 Tag and DeathBall require deathmatch admission");
  runtime_t *r = xcalloc(sizeof *r);
  r->configuration = *configuration;
  r->weapons = configuration->weapons;

  qa_q2m_monster_hooks monster_hooks;
  memset(&monster_hooks, 0, sizeof monster_hooks);
  monster_hooks.ctx = r;
  monster_hooks.coop_kill = monsters_coop_kill;
  monster_hooks.drop_item = monsters_drop_item;
  monster_hooks.platform_state = monsters_platform_state;
  if (!qa_q2m_monsters_init(&r->monsters, qa_q2m_weapons_from_ballistics(&r->weapons->base), monster_hooks))
    qa_range_error("out of memory");
  if (!qa_q2bm_register_classic_base_monsters(&r->base_monsters, &r->monsters)) qa_range_error("out of memory");

  qa_q2_mover_hooks mover_hooks = {r, movers_path_corner, movers_combat_point};
  r->movers = qa_q2_create_mover_module(mover_hooks);
  if (r->movers == NULL) qa_range_error("out of memory");

  qa_q2_base_entity_hooks entity_hooks;
  memset(&entity_hooks, 0, sizeof entity_hooks);
  entity_hooks.movers = r->movers;
  entity_hooks.weapons = &r->weapons->base;
  entity_hooks.ctx = r;
  entity_hooks.teleport_player = teleport_player;
  entity_hooks.player_push = configuration->entity_hooks.player_push != NULL ? entity_player_push : NULL;
  entity_hooks.set_actor_gravity = configuration->entity_hooks.set_actor_gravity != NULL ? entity_set_actor_gravity : NULL;
  entity_hooks.local_time = configuration->entity_hooks.local_time != NULL ? entity_local_time : NULL;
  entity_hooks.turret_driver = entity_turret_driver;
  entity_hooks.monster_context = entity_monster_context;
  entity_hooks.resume_monster = entity_resume_monster;
  r->base_entities = qa_q2_create_base_entity_module(entity_hooks);
  if (r->base_entities == NULL) qa_range_error("out of memory");

  qa_q2_item_hooks item_hooks;
  memset(&item_hooks, 0, sizeof item_hooks);
  item_hooks.ctx = r;
  item_hooks.weapon_picked = items_weapon_picked;
  item_hooks.silencer = configuration->item_hooks.silencer != NULL ? items_silencer : NULL;
  item_hooks.power_armor = configuration->item_hooks.power_armor != NULL ? items_power_armor : NULL;
  item_hooks.picked_up = items_picked_up;
  item_hooks.ammo_pack = items_ammo_pack;
  item_hooks.random_respawn = configuration->services.random_items != NULL ? items_random_respawn : NULL;
  if (configuration->item_hooks.weapon_respawn_seconds != NULL) {
    item_hooks.weapon_respawn_seconds = items_weapon_respawn_seconds;
    item_hooks.has_weapon_respawn_seconds = true;
  }
  item_hooks.edition = configuration->edition;
  item_hooks.foreign_items = qa_composition_q3_foreign_item_definitions;
  r->items = qa_q2_create_item_module(item_hooks);
  if (r->items == NULL) qa_range_error("out of memory");

  if (!(configuration->edition == QA_Q2_EDITION_CLASSIC && configuration->program == QA_Q2_PROGRAM_BASEQ2 &&
        kind != QA_Q2_MATCH_TAG && kind != QA_Q2_MATCH_DEATHBALL)) {
    qa_q2mp_armory_hooks armory_hooks;
    memset(&armory_hooks, 0, sizeof armory_hooks);
    armory_hooks.weapons = r->weapons;
    armory_hooks.items = r->items;
    armory_hooks.hunter_camera = configuration->services.hunter_camera;
    armory_hooks.strong_mines = configuration->services.strong_mines;
    armory_hooks.gravity_ctx = r;
    armory_hooks.gravity = services_gravity;
    armory_hooks.ctx = r;
    armory_hooks.monster = armory_monster;
    armory_hooks.player_effect = armory_player_effect;
    armory_hooks.intermission = armory_intermission;
    r->armory = xcalloc(sizeof *r->armory);
    qa_q2mp_register_armory(configuration->program == QA_Q2_PROGRAM_XATRIX ? QA_Q2MP_PACK_XATRIX : QA_Q2MP_PACK_ROGUE,
                            armory_hooks, configuration->edition, r->armory);
  }
  if (r->armory != NULL) {
    qa_q2mp_pack packs[2];
    size_t count = packs_of(r, packs);
    for (size_t i = 0; i < count; i++) {
      qa_q2_product_expansion *expansion = &r->expansions[i];
      expansion->pack = packs[i];
      expansion->hooks_ctx.runtime = r;
      expansion->hooks_ctx.index = i;
      r->expansion_count = i + 1;
      qa_q2mp_entity_hooks hooks;
      memset(&hooks, 0, sizeof hooks);
      hooks.movers = r->movers;
      hooks.weapons = &r->weapons->base;
      hooks.projectiles = mines_of(r);
      hooks.ctx = &expansion->hooks_ctx;
      hooks.teleport_player = expansion_teleport;
      hooks.target_anger = expansion_target_anger;
      hooks.emit = expansion_emit;
      qa_q2mp_entities_init(&expansion->entities, packs[i], hooks);
      qa_q2mpm_services services;
      memset(&services, 0, sizeof services);
      services.ctx = &expansion->hooks_ctx;
      services.movers = r->movers;
      services.gravity = expansion_gravity;
      services.bad_area = expansion_bad_area;
      services.bad_area_entity = expansion_bad_area_entity;
      services.mark_tesla_area = expansion_mark_tesla_area;
      services.powerups = expansion_powerups;
      qa_q2mpm_weapons weapons = qa_q2mp_projectiles_monster_weapons(&r->armory->projectiles);
      if (!qa_q2mpm_monsters_init(&expansion->monsters, packs[i] == QA_Q2MP_PACK_XATRIX ? QA_Q2MPM_PACK_XATRIX
                                                                                          : QA_Q2MPM_PACK_ROGUE,
                                  &r->monsters, services, &weapons, configuration->edition))
        qa_range_error("out of memory");
    }
  }

  qa_q2_ctf_hooks ctf;
  memset(&ctf, 0, sizeof ctf);
  ctf.ctx = r;
  ctf.items = r->items;
  ctf.weapons = r->weapons;
  ctf.player = ctf_player;
  ctf.set_skin = ctf_set_skin;
  ctf.spawn_player = ctf_spawn_player;
  ctf.observer = ctf_observer;
  ctf.teleport = ctf_teleport;
  ctf.chase = ctf_chase;
  ctf.set_grapple_prediction = ctf_set_grapple_prediction;
  ctf.gravity = services_gravity;
  ctf.end_level = ctf_end_level;
  ctf.kick = ctf_kick;
  ctf.set_deathmatch_flags = ctf_set_deathmatch_flags;
  ctf.chat_allowed = ctf_chat_allowed;
  qa_q2_match_selection standard;
  memset(&standard, 0, sizeof standard);
  standard.kind = QA_Q2_MATCH_STANDARD;
  qa_q2_product_match_init(&r->match, configuration->match != NULL ? configuration->match : &standard, r,
                           match_players, r->items, match_world, ctf, &r->configuration.services);

  if (configuration->edition == QA_Q2_EDITION_CLASSIC && configuration->program == QA_Q2_PROGRAM_ROGUE) {
    r->rogue_spawns = xcalloc(sizeof *r->rogue_spawns);
    qa_q2mp_rogue_player_spawns_init(r->rogue_spawns);
  }
  r->has_movement_stop_speed = kind == QA_Q2_MATCH_DEATHBALL;
  r->movement_stop_speed = 0;

  qa_q2_player_hooks hooks = player_hooks(r);
  const qa_q2_base_player_rules *rules = configuration->player_rules_mask != 0 ? &configuration->player_rules : NULL;
  if (configuration->edition == QA_Q2_EDITION_RERELEASE) {
    if (r->armory == NULL) qa_raise("Error", 0, 0, "Rerelease source composition requires the combined arsenal");
    r->rerelease = xcalloc(sizeof *r->rerelease);
    if (!qa_q2_rerelease_players_init(&r->rerelease->players, r->items, r->weapons, hooks, rerelease_hooks(r),
                                      configuration->rerelease_options, rules, configuration->player_rules_mask))
      qa_range_error("out of memory");
    qa_q2_rerelease_module_options module;
    memset(&module, 0, sizeof module);
    module.players = &r->rerelease->players;
    module.hooks = rerelease_hooks(r);
    module.campaign = configuration->campaign;
    if (!qa_q2_create_rerelease_module(&r->rerelease->entities, module)) qa_range_error("out of memory");
    qa_q2mpm_monster_state *source = NULL;
    for (size_t i = 0; i < r->expansion_count; i++)
      if (r->expansions[i].pack == QA_Q2MP_PACK_ROGUE) { source = &r->expansions[i].monsters.source; break; }
    if (source == NULL) qa_raise("Error", 0, 0, "Rerelease source composition requires Rogue monster state");
    qa_q2rm_monster_options monsters;
    memset(&monsters, 0, sizeof monsters);
    monsters.source = source;
    monsters.weapons = qa_q2mp_projectiles_monster_weapons(&r->armory->projectiles);
    monsters.transfer_healthbar_target.ctx = r;
    monsters.transfer_healthbar_target.fn = rerelease_transfer_healthbar;
    monsters.is_n64 = configuration->program == QA_Q2_PROGRAM_N64;
    monsters.expansion = configuration->program == QA_Q2_PROGRAM_MG2 ? "mg1"
                       : configuration->program == QA_Q2_PROGRAM_XATRIX ? "xatrix"
                       : configuration->program == QA_Q2_PROGRAM_ROGUE ? "rogue" : "base";
    qa_q2rm_register_rerelease_monsters(&r->rerelease->monsters, &r->monsters, monsters);
    r->players = r->rerelease->players.base;
  } else {
    r->players = qa_q2_players_create(r->items, r->weapons, hooks, rules, configuration->player_rules_mask);
    if (r->players == NULL) qa_range_error("out of memory");
  }
  if (r->match.source_kind == QA_Q2_MATCH_CTF) {
    qa_q2_pickup_policy policy;
    memset(&policy, 0, sizeof policy);
    policy.ctx = r;
    policy.instanced_coop = policy_instanced_coop;
    policy.can_pickup = policy_can_pickup;
    policy.before_pickup = policy_before_pickup;
    policy.before_targets = policy_before_targets;
    policy.after_pickup = policy_after_pickup;
    policy.keep_after_pickup = policy_keep_after_pickup;
    qa_q2_item_module_set_pickup_policy(r->items, policy);
  }

  /* TS modules, in this order. */
  if (r->rerelease != NULL)
    add_module_with(r, qa_q2_rerelease_module_as_spawn(&r->rerelease->entities), rerelease_module_callbacks);
  add_module(r, qa_q2_players_as_spawn(r->players));
  add_module_with(r, qa_q2_product_match_as_spawn(&r->match), match_callbacks);
  if (r->rogue_spawns != NULL) {
    qa_q2_spawn_module rogue;
    memset(&rogue, 0, sizeof rogue);
    rogue.ctx = r->rogue_spawns;
    rogue.spawn = rogue_spawns_spawn;
    add_module(r, rogue);
  }
  for (size_t i = 0; i < r->expansion_count; i++) {
    qa_q2_spawn_module entities;
    memset(&entities, 0, sizeof entities);
    entities.ctx = &r->expansions[i].entities;
    entities.spawn = pack_entities_spawn;
    add_module_with(r, entities, pack_entities_callbacks);
  }
  add_module(r, qa_q2_base_entity_module_as_spawn(r->base_entities));
  add_module(r, qa_q2_create_target_module());
  add_module(r, qa_q2_mover_module_as_spawn(r->movers));
  add_module(r, qa_q2_create_scenery_module());
  if (r->armory != NULL) {
    qa_q2_spawn_module items;
    memset(&items, 0, sizeof items);
    items.ctx = &r->armory->items;
    items.spawn = pack_items_spawn;
    add_module(r, items);
  }
  add_module(r, qa_q2_item_module_as_spawn(r->items));
  if (r->rerelease != NULL) add_module(r, qa_q2rm_monsters_as_spawn(&r->rerelease->monsters));
  for (size_t i = 0; i < r->expansion_count; i++) add_module(r, qa_q2mpm_monsters_as_spawn(&r->expansions[i].monsters));
  add_module(r, qa_q2bm_classic_base_as_spawn(&r->base_monsters));
  qa_q2_spawn_module monsters;
  memset(&monsters, 0, sizeof monsters);
  monsters.ctx = &r->monsters;
  monsters.spawn = monsters_spawn;
  add_module_with(r, monsters, monsters_callbacks);

  double configured = configuration->services.deathmatch_flags_read != NULL
      ? configuration->services.deathmatch_flags_read(configuration->services.ctx)
      : configuration->options.deathmatch_flags;
  double flags = kind == QA_Q2_MATCH_DEATHBALL ? qa_q2mp_deathball_get_rules(configured).deathmatch_flags : configured;
  r->active_deathmatch_flags = qa_js_to_uint32(flags);
  if (flags != configured) qa_q2_product_runtime_set_deathmatch_flags(r, flags);

  qa_q2_game_options options = configuration->options;
  options.edition = configuration->edition;
  options.player_name_ctx = r;
  options.player_name = options_player_name;
  options.deathmatch_flags_ctx = r;
  options.deathmatch_flags_read = options_deathmatch_flags;
  r->game = qa_q2_entity_services_create(configuration->host, options, r->modules, r->module_count);
  if (r->game == NULL) qa_range_error("out of memory");
  r->services = qa_q2_entity_services_game(r->game);
  qa_q2_product_runtime_register_callbacks(r);
  return r;
}

qa_q2_product_runtime *qa_create_q2_product_runtime(const qa_q2_composition_options *options) {
  return qa_q2_product_runtime_create(options);
}

void qa_q2_product_runtime_destroy(qa_q2_product_runtime *r) {
  if (r == NULL) return;
  qa_q2_entity_services_destroy(r->game);
  qa_q2_product_match_destroy(&r->match);
  if (r->rerelease != NULL) {
    qa_q2rm_monsters_module_destroy(&r->rerelease->monsters);
    qa_q2_rerelease_module_destroy(&r->rerelease->entities);
    qa_q2_rerelease_players_destroy(&r->rerelease->players);
    free(r->rerelease);
  } else {
    qa_q2_players_destroy(r->players);
  }
  for (size_t i = 0; i < r->expansion_count; i++) qa_q2mpm_monsters_destroy(&r->expansions[i].monsters);
  if (r->armory != NULL) { qa_q2mp_armory_destroy(r->armory); free(r->armory); }
  free(r->rogue_spawns);
  qa_q2_item_module_destroy(r->items);
  qa_q2_base_entity_module_destroy(r->base_entities);
  qa_q2_mover_module_destroy(r->movers);
  qa_q2bm_classic_base_module_destroy(&r->base_monsters);
  qa_q2m_monsters_destroy(&r->monsters);
  free(r);
}

void qa_q2_product_runtime_set_deathmatch_flags(qa_q2_product_runtime *r, double flags) {
  if (CFG(r)->services.deathmatch_flags_write != NULL) {
    CFG(r)->services.deathmatch_flags_write(CFG(r)->services.ctx, (double)qa_js_to_uint32(flags));
    return;
  }
  r->active_deathmatch_flags = qa_js_to_uint32(flags);
}

size_t qa_q2_product_runtime_original_source_fallbacks(const qa_q2_product_runtime *r, const char **out,
                                                       size_t capacity) {
  if (r->rerelease == NULL) return 0;
  size_t count = 0;
  for (size_t i = 0; i < r->expansion_count; i++) {
    const qa_q2mpm_monsters_module *monsters = &r->expansions[i].monsters;
    for (size_t j = 0; j < monsters->original_source_fallback_count; j++) {
      const char *classname = monsters->original_source_fallbacks[j];
      if (strcmp(classname, "monster_gladb") == 0 || strcmp(classname, "monster_boss5") == 0) continue;
      if (count < capacity) out[count] = classname;
      count++;
    }
  }
  return count;
}

void qa_q2_product_runtime_register_callbacks(qa_q2_product_runtime *r) {
  qa_q2_source_callbacks *callbacks = qa_q2_entity_services_callbacks(r->game);
  for (size_t i = 0; i < r->module_count; i++) {
    if (r->module_callbacks_getter[i] != NULL) r->module_callbacks[i] = r->module_callbacks_getter[i](r->modules[i].ctx);
    if (r->modules[i].callbacks != NULL) qa_q2_source_callbacks_register(callbacks, *r->modules[i].callbacks);
  }
  qa_q2_source_callbacks_register(callbacks, qa_q2w_ballistics_callbacks(&r->weapons->base));
  if (r->armory != NULL) {
    qa_q2_source_callbacks_register(callbacks, qa_q2mp_projectiles_callbacks(&r->armory->projectiles));
    qa_q2_source_callbacks_register(callbacks, qa_q2mp_spheres_callbacks(&r->armory->spheres));
    qa_q2_source_callbacks_register(callbacks, qa_q2mp_doppleganger_callbacks(&r->armory->doppleganger));
  }
}

qa_q2_entity *qa_q2_product_runtime_admit(qa_q2_product_runtime *r, qa_owned_actor actor,
                                          qa_q2_player_admission admission, const qa_q2_landmark_carry *landmark) {
  qa_q2_game_services *game = game_of(r);
  qa_q2_entity *entity = qa_q2_entity_services_attach_player(r->game, actor);
  if (!admission.has_use_q2_weapons || admission.use_q2_weapons) (void)qa_q2w_weapons_bind(r->weapons, entity->actor, game, NULL);
  (void)qa_q2_players_attach(r->players, entity, game, admission);
  qa_q2_product_match_admitted(&r->match, entity, game);
  if (r->match.source_kind != QA_Q2_MATCH_CTF) qa_q2_players_put_in_server(r->players, entity, game, false, landmark);
  qa_q2_base_player_state *joined = qa_q2_players_get_state(r->players, actor.id);
  if (game->options.mode != QA_Q2_MODE_SINGLEPLAYER && joined != NULL && !joined->spectator) {
    qa_q2_presentation_event effect;
    memset(&effect, 0, sizeof effect);
    effect.kind = "effect";
    effect.effect = "q2:login";
    effect.origin = qa_q2_game_body(game, entity).origin;
    effect.count = 1;
    effect.color = 0;
    game->host->emit(game->host->ctx, effect);
    char text[64];
    snprintf(text, sizeof text, "%s entered the game\n", joined->name);
    qa_q2_player_event print;
    memset(&print, 0, sizeof print);
    print.kind = "print";
    print.level = "high";
    print.text = text;
    qa_q2_player_hooks *hooks = qa_q2_players_hooks(r->players);
    hooks->emit(hooks->ctx, print);
  }
  return entity;
}

qa_q2w_input qa_q2_product_runtime_weapon_input(qa_q2_product_runtime *r, qa_actor_id actor, qa_q2w_input input) {
  qa_q2_game_services *game = game_of(r);
  qa_q2w_input foreign = input;
  if (qa_q2_item_module_player_powerups(r->items, actor).haste_until > game->host->now(game->host->ctx))
    foreign.haste = true;
  qa_q2w_input adjusted = r->armory != NULL ? qa_q2mp_items_input(&r->armory->items, actor, foreign) : foreign;
  qa_q2_entity *entity = entity_of(r, actor);
  return r->match.source_kind == QA_Q2_MATCH_CTF && entity != NULL
             ? qa_q2_ctf_weapon_input(r->match.ctf, entity, game, adjusted)
             : adjusted;
}

qa_q2_composition_powerups qa_q2_product_runtime_powerups(qa_q2_product_runtime *r, qa_actor_id actor) {
  qa_q2_base_player_state *state = qa_q2_players_get_state(r->players, actor);
  if (state != NULL && !state->use_q2_inventory)
    return CFG(r)->services.foreign_powerups(CFG(r)->services.ctx, actor);
  qa_q2_player_powerups base = qa_q2_item_module_player_powerups(r->items, actor);
  qa_q2_composition_powerups out;
  out.quad_until = base.quad_until;
  out.invulnerability_until = base.invulnerability_until;
  out.double_until = r->armory != NULL ? qa_q2mp_items_powerups(&r->armory->items, actor).double_until : 0;
  return out;
}

void qa_q2_product_runtime_record_damage(qa_q2_product_runtime *r, qa_q2_entity *entity, qa_damage_decision decision) {
  qa_q2_players_record_damage(r->players, entity, game_of(r), decision);
}

void qa_q2_product_runtime_before_reaction(qa_q2_product_runtime *r, qa_actor_id actor,
                                           const qa_attack_provenance *attack) {
  if (r->armory != NULL) qa_q2mp_spheres_owner_damaged(&r->armory->spheres, actor, attack, game_of(r));
}

void qa_q2_product_runtime_movement_impact(qa_q2_product_runtime *r, qa_actor_id actor, double impact_delta,
                                           bool on_ladder) {
  if (r->rerelease != NULL)
    qa_q2_rerelease_players_movement_impact(&r->rerelease->players, actor, impact_delta, on_ladder);
}

void qa_q2_product_runtime_record_foreign_weapon_fire(qa_q2_product_runtime *r, qa_actor_id actor) {
  if (r->rerelease == NULL) return;
  qa_q2_game_services *game = game_of(r);
  qa_q2_rerelease_players_record_weapon_fire(&r->rerelease->players, actor, game->host->now(game->host->ctx));
}

void qa_q2_product_runtime_after_player_frames(qa_q2_product_runtime *r) {
  qa_q2_game_services *game = game_of(r);
  /* TS iterates states.keys() live: snapshot the keys, and skip a key
   * whose state was deleted before its turn, as the Map iterator does. */
  size_t count = qa_q2_players_state_count(r->players);
  qa_actor_id *actors = count == 0 ? NULL : malloc(count * sizeof *actors);
  if (count != 0 && actors == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_q2_base_player_state *state;
    (void)qa_q2_players_state_at(r->players, i, &actors[i], &state);
  }
  for (size_t i = 0; i < count; i++) {
    if (qa_q2_players_get_state(r->players, actors[i]) == NULL) continue; /* deleted before visited */
    qa_q2_entity *entity = entity_of(r, actors[i]);
    if (entity != NULL) qa_q2_product_match_effects(&r->match, entity, game);
  }
  free(actors);
  if (r->rerelease != NULL) qa_q2_rerelease_module_after_player_frames(&r->rerelease->entities, game);
}

void qa_q2_product_runtime_after_spawn(qa_q2_product_runtime *r) {
  qa_q2_game_services *game = game_of(r);
  for (size_t i = 0; i < r->expansion_count; i++) {
    qa_q2mpm_hint_initialization report;
    memset(&report, 0, sizeof report);
    if (qa_q2mpm_monsters_finalize(&r->expansions[i].monsters, game, &report)) qa_q2mpm_hint_init_free(&report);
  }
  qa_q2_product_match_after_spawn(&r->match, game);
}

void qa_q2_product_runtime_check_rules(qa_q2_product_runtime *r) {
  qa_q2_game_services *game = game_of(r);
  if (strcmp(qa_q2_players_intermission(r->players).kind, "playing") == 0 &&
      qa_q2_product_match_check_rules(&r->match, game))
    return;
  qa_q2_players_check_rules(r->players, game);
}
