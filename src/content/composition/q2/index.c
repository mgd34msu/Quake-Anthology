/* Port of src/content/composition/q2/index.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q2/index.h"

#include <stdio.h>
#include <string.h>

#include "qa/core/cvars/info.h"
#include "qa/foundation/support.h"

#define R(rt) (rt)
#define H(rt) ((rt)->hooks)
#define HC(rt) ((rt)->hooks->ctx)
#define CFG(rt) (&(rt)->configuration)
#define SVC(rt) (&(rt)->configuration.services)

static void emit_event(qa_comp_q2_product_runtime *rt, qa_comp_q2_event *event) {
  SVC(rt)->emit(SVC(rt)->ctx, event);
}

/* --- packs() -------------------------------------------------------------- */
static size_t packs_of(const qa_comp_q2_product_runtime *rt, const char **out) {
  const qa_comp_q2_options *cfg = CFG(rt);
  if (strcmp(cfg->edition, "rerelease") == 0) { out[0] = "xatrix"; out[1] = "rogue"; return 2; }
  if (cfg->program == QA_COMP_Q2_BASEQ2) {
    /* Tag/DeathBall pull the Rogue pack in unifiedly on baseq2. */
    if (cfg->has_match && (cfg->match.kind == QA_COMP_Q2_MATCH_TAG || cfg->match.kind == QA_COMP_Q2_MATCH_DEATHBALL)) {
      out[0] = "rogue";
      return 1;
    }
    return 0;
  }
  out[0] = qa_comp_q2_program_name(cfg->program);
  return 1;
}

/* --- constructor bindings ------------------------------------------------- */
static void monster_drop_item(void *ctx, qa_actor_id actor, qa_comp_q2_game_services *game, const char *classname) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  H(rt)->items_drop_monster(HC(rt), rt->items, actor, game, classname);
}
static const char *monster_platform_state(void *ctx, qa_actor_id actor) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_entity *entity = rt->game->entity(rt->game->ctx, actor);
  if (entity == NULL) return NULL;
  for (size_t i = 0; i < rt->expansion_count; i++) {
    const char *phase = H(rt)->pack_movers_platform_state(HC(rt), rt->expansions[i].entities, entity);
    if (phase != NULL) return phase;
  }
  return H(rt)->base_platform_state(HC(rt), rt->base_entities, entity);
}
static void mover_path_corner(void *ctx, qa_comp_q2_entity *e, qa_comp_q2_game_services *g, qa_comp_q2_entity *o) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  H(rt)->touch_path_corner(HC(rt), rt->monsters, e, g, o);
}
static void mover_combat_point(void *ctx, qa_comp_q2_entity *e, qa_comp_q2_game_services *g, qa_comp_q2_entity *o) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  H(rt)->touch_combat_point(HC(rt), rt->monsters, e, g, o);
}
static void base_teleport_player(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_entity *entity = rt->game->entity(rt->game->ctx, actor);
  if (entity != NULL) H(rt)->players_teleport(HC(rt), rt->players, entity, rt->game, origin, angles);
}
static void base_turret_driver(void *ctx, qa_comp_q2_entity *e, qa_comp_q2_game_services *g) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  H(rt)->spawn_infantry_driver(HC(rt), rt->monsters, e, g);
}
static const void *base_monster_context(void *ctx, qa_actor_id actor) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  return H(rt)->monster_context(HC(rt), rt->monsters, actor);
}
static void base_resume_monster(void *ctx, qa_comp_q2_entity *e, qa_comp_q2_game_services *g) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  H(rt)->resume_monster(HC(rt), rt->monsters, e, g);
}
static qa_comp_q2_entity *item_random_respawn(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  const void *settings = SVC(rt)->random_items != NULL ? SVC(rt)->random_items(SVC(rt)->ctx) : NULL;
  if (settings == NULL || rt->armory == NULL) return NULL;
  return H(rt)->armory_random_respawn(HC(rt), rt->armory, entity, game, settings);
}
static bool item_weapon_picked(void *ctx, qa_actor_id actor, qa_item_id item, bool first) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  if (rt->has_rerelease) {
    qa_comp_q2_entity *entity = rt->game->entity(rt->game->ctx, actor);
    if (entity != NULL) return H(rt)->rerelease_weapon_picked(HC(rt), rt->players, entity, rt->game, item, first);
  }
  return CFG(rt)->item_hooks.weapon_picked(CFG(rt)->item_hooks.ctx, actor, item, first);
}
static void item_ammo_pack(void *ctx, qa_actor_id actor, qa_comp_q2_game_services *game, bool full) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  if (CFG(rt)->item_hooks.ammo_pack != NULL) CFG(rt)->item_hooks.ammo_pack(CFG(rt)->item_hooks.ctx, actor, game, full);
  if (rt->armory != NULL) {
    const char *packs[2];
    size_t count = packs_of(rt, packs);
    for (size_t i = 0; i < count; i++) H(rt)->armory_ammo_pack(HC(rt), rt->armory, actor, game, full, packs[i]);
  }
}
static double binding_gravity(void *ctx) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  return SVC(rt)->gravity(SVC(rt)->ctx);
}
static bool armory_intermission(void *ctx) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  return strcmp(H(rt)->players_intermission_kind(HC(rt), rt->players), "intermission") == 0;
}
static void armory_player_effect(void *ctx, const qa_comp_q2_missionpack_player_effect *effect) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_event event = {0};
  event.kind = QA_COMP_Q2_EVENT_MISSIONPACK_PLAYER;
  event.player_effect = effect;
  emit_event(rt, &event);
}
static void pack_emit(void *ctx, const qa_comp_q2_missionpack_entity_event *entity_event) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_event event = {0};
  event.kind = QA_COMP_Q2_EVENT_MISSIONPACK_ENTITY;
  event.entity_event = entity_event;
  emit_event(rt, &event);
}
static bool pack_bad_area(void *ctx, qa_actor_id actor, qa_comp_q2_game_services *game) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  return H(rt)->projectiles_bad_area(HC(rt), rt->armory, actor, game);
}
static bool pack_bad_area_entity(void *ctx, qa_actor_id actor, qa_comp_q2_game_services *game, qa_vec3 origin) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  return H(rt)->projectiles_bad_area_entity(HC(rt), rt->armory, actor, game, origin);
}
static void pack_mark_tesla(void *ctx, qa_actor_id self, qa_comp_q2_game_services *game, qa_actor_id tesla) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  H(rt)->projectiles_mark_tesla_area(HC(rt), rt->armory, self, game, tesla);
}
static qa_comp_q2_powerups pack_powerups(void *ctx, qa_actor_id actor) {
  return qa_comp_q2_product_runtime_powerups((qa_comp_q2_product_runtime *)ctx, actor);
}

/* --- match (CTF) hooks built by the constructor --------------------------- */
static qa_comp_q2_player_state *player_state_of(void *ctx, qa_actor_id actor) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  return rt->game->player_state(rt->game->ctx, actor);
}
static qa_comp_q2_game_services *world_of(void *ctx) { return ((qa_comp_q2_product_runtime *)ctx)->game; }
static void diagnostic_print(void *ctx, const char *text) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  CFG(rt)->player_hooks.diagnostic(CFG(rt)->player_hooks.ctx, text);
}
static void ctf_set_skin(void *ctx, qa_actor_id actor, const char *skin) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_player_state *state = player_state_of(rt, actor);
  qa_comp_q2_entity *entity = rt->game->entity(rt->game->ctx, actor);
  if (state == NULL || entity == NULL) qa_range_error("Match skin requires an admitted player");
  qa_info_options options = {0};
  options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  options.target = QA_CVAR_INFO_CLIENT_USERINFO;
  options.maximum_length = 512;
  options.server_high_characters = false;
  options.print = diagnostic_print;
  options.print_ctx = rt;
  char *info = qa_set_info_value(state->userinfo, "skin", skin, &options);
  rt->game->userinfo_changed(rt->game->ctx, entity, rt->game, info);
}
static void ctf_spawn_player(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  H(rt)->players_put_in_server(HC(rt), rt->players, entity, game, false, NULL);
}
static void ctf_observer(void *ctx, qa_comp_q2_entity *entity) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_movement_command command = {0};
  command.kind = "noclip";
  command.enabled = true;
  CFG(rt)->player_hooks.set_movement(CFG(rt)->player_hooks.ctx, entity->actor.id, command);
}
static void ctf_teleport(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                         qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  H(rt)->players_teleport(HC(rt), rt->players, entity, game, origin, angles);
  game->move_velocity(game->ctx, entity, velocity);
  qa_comp_q2_movement_command command = {0};
  command.kind = "teleport";
  command.origin = origin;
  command.angles = angles;
  command.velocity = velocity;
  command.command_angles = CFG(rt)->player_hooks.movement_command_angles(CFG(rt)->player_hooks.ctx, entity->actor.id);
  command.hold_milliseconds = 160;
  command.spectator = false;
  CFG(rt)->player_hooks.set_movement(CFG(rt)->player_hooks.ctx, entity->actor.id, command);
}
static void ctf_chase(void *ctx, qa_actor_id actor) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_entity *entity = rt->game->entity(rt->game->ctx, actor);
  if (entity != NULL) H(rt)->players_chase(HC(rt), rt->players, entity, rt->game, 1, true);
}
static void ctf_grapple_prediction(void *ctx, qa_actor_id actor, bool suppressed) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_event event = {0};
  event.kind = QA_COMP_Q2_EVENT_GRAPPLE_PREDICTION;
  event.actor = actor;
  event.suppressed = suppressed;
  emit_event(rt, &event);
}
static void ctf_end_level(void *ctx, qa_comp_q2_game_services *game, const char *map) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  if (map == NULL) H(rt)->players_end_deathmatch_level(HC(rt), rt->players, game);
  else H(rt)->players_begin_intermission(HC(rt), rt->players, game, map);
}
static void ctf_kick(void *ctx, qa_actor_id actor) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_event event = {0};
  event.kind = QA_COMP_Q2_EVENT_KICK;
  event.actor = actor;
  emit_event(rt, &event);
}
static void ctf_set_flags(void *ctx, double flags) {
  qa_comp_q2_product_runtime_set_deathmatch_flags((qa_comp_q2_product_runtime *)ctx, flags);
}
static bool ctf_chat_allowed(void *ctx, qa_actor_id actor, qa_comp_q2_game_services *game) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  return H(rt)->players_chat_allowed(HC(rt), rt->players, actor, game);
}

/* --- effective player hooks ------------------------------------------------ */
static double player_quad_fire_until(void *ctx, qa_actor_id actor) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  bool drop = SVC(rt)->drop_quad_fire != NULL ? SVC(rt)->drop_quad_fire(SVC(rt)->ctx) : true;
  return strcmp(CFG(rt)->edition, "rerelease") == 0 && drop && rt->armory != NULL
             ? H(rt)->armory_quad_fire_until(HC(rt), rt->armory, actor)
             : 0;
}
static qa_comp_q2_weapon_input player_weapon_input(void *ctx, qa_actor_id actor) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_weapon_input input = CFG(rt)->player_hooks.weapon_input(CFG(rt)->player_hooks.ctx, actor);
  return qa_comp_q2_product_runtime_weapon_input(rt, actor, input);
}
static void player_score(void *ctx, qa_comp_q2_entity *victim, qa_comp_q2_entity *attacker,
                         qa_comp_q2_game_services *game, double change, double means,
                         qa_comp_q2_entity *recipient) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  bool standard = !CFG(rt)->has_match || CFG(rt)->match.kind == QA_COMP_Q2_MATCH_STANDARD;
  if (standard && CFG(rt)->player_hooks.score != NULL) {
    CFG(rt)->player_hooks.score(CFG(rt)->player_hooks.ctx, victim, attacker, game, change, means, recipient);
    return;
  }
  qa_comp_q2_product_match_score(&rt->match, victim, game, change, means, recipient, attacker);
}
static void player_spawned(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_product_match_player_spawned(&rt->match, entity, game);
  if (CFG(rt)->player_hooks.player_spawned != NULL)
    CFG(rt)->player_hooks.player_spawned(CFG(rt)->player_hooks.ctx, entity, game);
}
static qa_comp_q2_entity *player_select_spawn(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  const void *spot = qa_comp_q2_product_match_select_spawn(&rt->match, entity, game);
  if (spot != NULL) return (qa_comp_q2_entity *)spot;
  if (rt->rogue_spawns != NULL) {
    qa_comp_q2_entity *rogue = H(rt)->rogue_select_spawn(HC(rt), rt->rogue_spawns, game);
    if (rogue != NULL) return rogue;
  }
  return CFG(rt)->player_hooks.select_spawn != NULL
             ? CFG(rt)->player_hooks.select_spawn(CFG(rt)->player_hooks.ctx, entity, game)
             : NULL;
}
static bool player_command(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                           const char *name, const char *const *args, size_t arg_count) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  return qa_comp_q2_product_match_command(&rt->match, entity, game, name, args, arg_count) ||
         (CFG(rt)->player_hooks.command != NULL &&
          CFG(rt)->player_hooks.command(CFG(rt)->player_hooks.ctx, entity, game, name, args, arg_count));
}
static void player_before_death(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                                const qa_attack_provenance *attack) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_product_match_drop_inventory(&rt->match, entity, game);
  if (CFG(rt)->player_hooks.before_death_inventory != NULL)
    CFG(rt)->player_hooks.before_death_inventory(CFG(rt)->player_hooks.ctx, entity, game, attack);
}
static void player_death(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                         const qa_attack_provenance *attack) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_product_match_death(&rt->match, entity, game);
  if (rt->armory != NULL) {
    H(rt)->spheres_owner_died(HC(rt), rt->armory, entity->actor.id, game, attack);
    H(rt)->armory_items_reset(HC(rt), rt->armory, entity->actor.id);
  }
  if (CFG(rt)->player_hooks.death != NULL) CFG(rt)->player_hooks.death(CFG(rt)->player_hooks.ctx, entity, game, attack);
}
static void player_disconnect(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_product_match_disconnect(&rt->match, entity, game);
  if (rt->armory != NULL) {
    H(rt)->spheres_disconnect(HC(rt), rt->armory, entity->actor.id, game);
    H(rt)->armory_items_reset(HC(rt), rt->armory, entity->actor.id);
  }
  if (CFG(rt)->player_hooks.disconnect != NULL)
    CFG(rt)->player_hooks.disconnect(CFG(rt)->player_hooks.ctx, entity, game);
}
static void player_set_movement(void *ctx, qa_actor_id actor, qa_comp_q2_movement_command command) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  CFG(rt)->player_hooks.set_movement(CFG(rt)->player_hooks.ctx, actor, command);
}
static qa_vec3 player_movement_angles(void *ctx, qa_actor_id actor) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  return CFG(rt)->player_hooks.movement_command_angles(CFG(rt)->player_hooks.ctx, actor);
}
static void player_diagnostic(void *ctx, const char *text) { diagnostic_print(ctx, text); }

/* --- rerelease hooks / pickup policy / pack target anger ------------------ */
static bool rerelease_holds_health_bar(void *ctx, qa_actor_id actor) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  qa_comp_q2_entity *entity = rt->game->entity(rt->game->ctx, actor);
  return entity != NULL && strcmp(entity->classname, "monster_jorg") == 0;
}
static void rerelease_clear_powerups(void *ctx, qa_actor_id actor) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  H(rt)->armory_items_reset(HC(rt), rt->armory, actor);
  if (CFG(rt)->rerelease_hooks.clear_expansion_powerups != NULL)
    CFG(rt)->rerelease_hooks.clear_expansion_powerups(CFG(rt)->rerelease_hooks.ctx, actor);
}
static void rerelease_transfer(void *ctx, qa_actor_id old_actor, qa_actor_id new_actor, qa_comp_q2_game_services *game) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  H(rt)->rerelease_transfer_healthbar(HC(rt), rt->rerelease_entities, old_actor, new_actor, game);
}
static bool policy_instanced_coop(void *ctx, qa_comp_q2_game_services *game) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  return (rt->has_rerelease) && H(rt)->rerelease_instanced_coop(HC(rt), rt->rerelease_entities, game);
}
static bool policy_can_pickup(void *ctx, qa_comp_q2_entity *e, qa_comp_q2_game_services *g, qa_actor_id a) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  bool allowed = rt->mode_hooks->ctf_pickups_allowed(rt->mode_hooks->ctx, rt->match.source);
  return allowed && (!rt->has_rerelease || H(rt)->rerelease_can_pickup(HC(rt), rt->rerelease_entities, e, g, a));
}
static bool policy_before_pickup(void *ctx, qa_comp_q2_entity *e, qa_comp_q2_game_services *g, qa_actor_id a) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  bool allowed = rt->mode_hooks->ctf_pickups_allowed(rt->mode_hooks->ctx, rt->match.source);
  return allowed && (!rt->has_rerelease || H(rt)->rerelease_before_pickup(HC(rt), rt->rerelease_entities, e, g, a));
}
static void policy_before_targets(void *ctx, qa_comp_q2_entity *e, qa_comp_q2_game_services *g, qa_actor_id a, bool t) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  if (rt->has_rerelease) H(rt)->rerelease_before_targets(HC(rt), rt->rerelease_entities, e, g, a, t);
}
static void policy_after_pickup(void *ctx, qa_comp_q2_entity *e, qa_comp_q2_game_services *g, qa_actor_id a, bool t) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  if (rt->has_rerelease) H(rt)->rerelease_after_pickup(HC(rt), rt->rerelease_entities, e, g, a, t);
}
static bool policy_keep_after(void *ctx, qa_comp_q2_entity *e, qa_comp_q2_game_services *g, qa_actor_id a) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  return rt->has_rerelease && H(rt)->rerelease_keep_after_pickup(HC(rt), rt->rerelease_entities, e, g, a);
}
static void pack_target_anger_at(qa_comp_q2_product_runtime *rt, size_t index, qa_comp_q2_entity *entity,
                                 qa_comp_q2_entity *target, qa_comp_q2_game_services *game) {
  if (index >= rt->expansion_count)
    qa_range_error("Missing Q2 %s source monster registration", rt->pack_entity_bindings[index].pack);
  H(rt)->pack_target_anger(HC(rt), rt->expansions[index].monsters, entity, target, game);
}
static void pack_target_anger_0(void *ctx, qa_comp_q2_entity *e, qa_comp_q2_entity *t, qa_comp_q2_game_services *g) {
  pack_target_anger_at((qa_comp_q2_product_runtime *)ctx, 0, e, t, g);
}
static void pack_target_anger_1(void *ctx, qa_comp_q2_entity *e, qa_comp_q2_entity *t, qa_comp_q2_game_services *g) {
  pack_target_anger_at((qa_comp_q2_product_runtime *)ctx, 1, e, t, g);
}
static double flags_getter(void *ctx) {
  qa_comp_q2_product_runtime *rt = (qa_comp_q2_product_runtime *)ctx;
  return SVC(rt)->has_deathmatch_flags ? SVC(rt)->flags_read(SVC(rt)->ctx) : (double)rt->active_deathmatch_flags;
}

/* --- constructor ------------------------------------------------------------ */
void qa_comp_q2_product_runtime_init(qa_comp_q2_product_runtime *rt, const qa_comp_q2_options *configuration,
                                     const qa_comp_q2_runtime_hooks *hooks, const qa_comp_q2_mode_hooks *mode_hooks) {
  memset(rt, 0, sizeof *rt);
  rt->configuration = *configuration;
  rt->hooks = hooks;
  rt->mode_hooks = mode_hooks;
  const qa_comp_q2_options *cfg = CFG(rt);
  void *ctx = hooks->ctx;
  if (cfg->has_match && cfg->match.kind != QA_COMP_Q2_MATCH_STANDARD && cfg->match.kind != QA_COMP_Q2_MATCH_HORDE &&
      strcmp(cfg->options.mode, "deathmatch") != 0)
    qa_range_error("Q2 Tag and DeathBall require deathmatch admission");
  rt->weapons = cfg->weapons;
  rt->monster_bindings = (qa_comp_q2_monster_bindings){rt, monster_drop_item, monster_platform_state};
  rt->mover_bindings = (qa_comp_q2_mover_bindings){rt, mover_path_corner, mover_combat_point};
  rt->base_entity_bindings = (qa_comp_q2_base_entity_bindings){rt, cfg->entity_hooks, NULL, rt->weapons,
                                                               base_teleport_player, base_turret_driver,
                                                               base_monster_context, base_resume_monster};
  rt->item_bindings = (qa_comp_q2_item_bindings){rt, cfg->item_hooks.rest, cfg->services.random_items != NULL,
                                                 item_random_respawn, item_weapon_picked, item_ammo_pack};
  rt->armory_bindings = (qa_comp_q2_armory_bindings){rt, rt->weapons, NULL, base_monster_context,
                                                     cfg->services.hunter_camera, cfg->services.strong_mines,
                                                     binding_gravity, armory_intermission, armory_player_effect};
  rt->pack_monster_bindings = (qa_comp_q2_pack_monster_bindings){rt, NULL, binding_gravity, pack_bad_area,
                                                                 pack_bad_area_entity, pack_mark_tesla, pack_powerups};
  rt->ctf_hooks = (qa_comp_q2_ctf_hooks){rt, NULL, rt->weapons, player_state_of, ctf_set_skin, ctf_spawn_player,
                                         ctf_observer, ctf_teleport, ctf_chase, ctf_grapple_prediction, binding_gravity,
                                         ctf_end_level, ctf_kick, ctf_set_flags, ctf_chat_allowed};
  rt->effective_player_hooks = (qa_comp_q2_player_hooks){rt, player_set_movement, player_movement_angles,
                                                         player_weapon_input, player_diagnostic, player_score,
                                                         player_spawned, player_select_spawn, player_command,
                                                         player_before_death, player_death, player_disconnect,
                                                         player_quad_fire_until};
  rt->monsters = hooks->create_monsters(ctx, rt->weapons, &rt->monster_bindings);
  void *base_monsters = hooks->register_classic_base_monsters(ctx, rt->monsters);
  rt->movers = hooks->create_movers(ctx, &rt->mover_bindings);
  rt->base_entity_bindings.movers = rt->movers;
  rt->base_entities = hooks->create_base_entities(ctx, &rt->base_entity_bindings);
  rt->items = hooks->create_items(ctx, &rt->item_bindings);
  rt->armory_bindings.items = rt->items;
  rt->ctf_hooks.items = rt->items;
  bool no_armory = strcmp(cfg->edition, "classic") == 0 && cfg->program == QA_COMP_Q2_BASEQ2 &&
                   !(cfg->has_match && (cfg->match.kind == QA_COMP_Q2_MATCH_TAG ||
                                        cfg->match.kind == QA_COMP_Q2_MATCH_DEATHBALL));
  rt->armory = no_armory ? NULL
                         : hooks->register_armory(ctx, cfg->program == QA_COMP_Q2_XATRIX ? "xatrix" : "rogue",
                                                  &rt->armory_bindings, cfg->edition);
  const char *packs[2];
  size_t pack_count = packs_of(rt, packs);
  if (rt->armory != NULL) {
    void *projectiles = hooks->armory_projectiles(ctx, rt->armory);
    rt->pack_monster_bindings.movers = rt->movers;
    rt->expansion_count = pack_count;
    for (size_t i = 0; i < pack_count; i++) {
      rt->expansions[i].pack = packs[i];
      rt->pack_entity_bindings[i] = (qa_comp_q2_pack_entity_bindings){
          rt, rt->movers, rt->weapons, projectiles, base_teleport_player,
          i == 0 ? pack_target_anger_0 : pack_target_anger_1, pack_emit, packs[i]};
      rt->expansions[i].entities = hooks->create_pack_entities(ctx, packs[i], &rt->pack_entity_bindings[i]);
      rt->expansions[i].monsters = hooks->register_pack_monsters(ctx, rt->monsters, packs[i], projectiles,
                                                                 &rt->pack_monster_bindings, cfg->edition);
    }
  }
  qa_comp_q2_match_selection standard = {0};
  standard.kind = QA_COMP_Q2_MATCH_STANDARD;
  qa_comp_q2_product_match_init(&rt->match, cfg->has_match ? &cfg->match : &standard, player_state_of, rt, rt->items,
                                world_of, rt, &rt->ctf_hooks, &rt->configuration.services, mode_hooks);
  rt->rogue_spawns = strcmp(cfg->edition, "classic") == 0 && cfg->program == QA_COMP_Q2_ROGUE
                         ? hooks->create_rogue_spawns(ctx)
                         : NULL;
  rt->has_movement_stop_speed = cfg->has_match && cfg->match.kind == QA_COMP_Q2_MATCH_DEATHBALL;
  rt->movement_stop_speed = rt->has_movement_stop_speed ? 0 : 0;
  if (strcmp(cfg->edition, "rerelease") == 0) {
    if (rt->armory == NULL) qa_range_error("Rerelease source composition requires the combined arsenal");
    rt->effective_rerelease_hooks = cfg->rerelease_hooks;
    rt->effective_rerelease_hooks.ctx = rt;
    if (rt->effective_rerelease_hooks.monster_holds_health_bar == NULL)
      rt->effective_rerelease_hooks.monster_holds_health_bar = rerelease_holds_health_bar;
    rt->effective_rerelease_hooks.clear_expansion_powerups = rerelease_clear_powerups;
    rt->players = hooks->create_rerelease_players(ctx, rt->items, rt->weapons, &rt->effective_player_hooks,
                                                  &rt->effective_rerelease_hooks, cfg->rerelease_options,
                                                  cfg->player_rules);
    rt->rerelease_players = rt->players;
    rt->rerelease_entities = hooks->create_rerelease_module(ctx, rt->players, &rt->effective_rerelease_hooks,
                                                             cfg->campaign != NULL, cfg->campaign);
    void *source = NULL;
    for (size_t i = 0; i < rt->expansion_count; i++)
      if (strcmp(rt->expansions[i].pack, "rogue") == 0)
        source = hooks->pack_monsters_source(ctx, rt->expansions[i].monsters);
    if (source == NULL) qa_range_error("Rerelease source composition requires Rogue monster state");
    rt->rerelease_monster_bindings = (qa_comp_q2_rerelease_monster_bindings){
        rt, source, hooks->armory_projectiles(ctx, rt->armory), rerelease_transfer, cfg->program == QA_COMP_Q2_N64,
        cfg->program == QA_COMP_Q2_MG2 ? "mg1"
        : cfg->program == QA_COMP_Q2_XATRIX || cfg->program == QA_COMP_Q2_ROGUE ? qa_comp_q2_program_name(cfg->program)
                                                                                 : "base"};
    rt->rerelease_monsters = hooks->register_rerelease_monsters(ctx, rt->monsters, &rt->rerelease_monster_bindings);
    rt->has_rerelease = true;
  } else {
    rt->players = hooks->create_players(ctx, rt->items, rt->weapons, &rt->effective_player_hooks, cfg->player_rules);
  }
  if (rt->match.source_kind == QA_COMP_Q2_MATCH_CTF) {
    rt->pickup_policy = (qa_comp_q2_pickup_policy){rt, policy_instanced_coop, policy_can_pickup, policy_before_pickup,
                                                   policy_before_targets, policy_after_pickup, policy_keep_after};
    hooks->items_set_pickup_policy(ctx, rt->items, &rt->pickup_policy);
  }
  void *target_module = hooks->create_target_module(ctx);
  void *scenery_module = hooks->create_scenery_module(ctx);
  size_t count = 0;
  if (rt->has_rerelease) rt->modules[count++] = rt->rerelease_entities;
  rt->modules[count++] = rt->players;
  rt->modules[count++] = &rt->match; /* match module sentinel (see register_callbacks) */
  if (rt->rogue_spawns != NULL) rt->modules[count++] = rt->rogue_spawns;
  for (size_t i = 0; i < rt->expansion_count; i++) rt->modules[count++] = rt->expansions[i].entities;
  rt->modules[count++] = rt->base_entities;
  rt->modules[count++] = target_module;
  rt->modules[count++] = rt->movers;
  rt->modules[count++] = scenery_module;
  if (rt->armory != NULL) rt->modules[count++] = hooks->armory_items(ctx, rt->armory);
  rt->modules[count++] = rt->items;
  if (rt->has_rerelease) rt->modules[count++] = rt->rerelease_monsters;
  for (size_t i = 0; i < rt->expansion_count; i++) rt->modules[count++] = rt->expansions[i].monsters;
  rt->modules[count++] = base_monsters;
  rt->modules[count++] = rt->monsters;
  rt->module_count = count;
  double configured = SVC(rt)->has_deathmatch_flags ? SVC(rt)->flags_read(SVC(rt)->ctx) : cfg->options.deathmatch_flags;
  double flags = cfg->has_match && cfg->match.kind == QA_COMP_Q2_MATCH_DEATHBALL
                     ? hooks->deathball_rules_flags(ctx, configured)
                     : configured;
  rt->active_deathmatch_flags = (uint32_t)flags;
  if (flags != configured) qa_comp_q2_product_runtime_set_deathmatch_flags(rt, flags);
  rt->game = hooks->create_foundation(ctx, cfg->host, &cfg->options, cfg->edition, flags_getter, rt, rt->modules,
                                       rt->module_count);
  qa_comp_q2_product_runtime_register_callbacks(rt);
}

void qa_comp_create_q2_product_runtime(qa_comp_q2_product_runtime *runtime, const qa_comp_q2_options *options,
                                       const qa_comp_q2_runtime_hooks *hooks, const qa_comp_q2_mode_hooks *mode_hooks) {
  qa_comp_q2_product_runtime_init(runtime, options, hooks, mode_hooks);
}

void qa_comp_q2_product_runtime_set_deathmatch_flags(qa_comp_q2_product_runtime *rt, double flags) {
  if (SVC(rt)->has_deathmatch_flags) {
    SVC(rt)->flags_write(SVC(rt)->ctx, (double)(uint32_t)flags);
    return;
  }
  rt->active_deathmatch_flags = (uint32_t)flags;
}

bool qa_comp_q2_product_runtime_original_source_fallbacks(const qa_comp_q2_product_runtime *rt, const char **out,
                                                          size_t capacity, size_t *out_count) {
  *out_count = 0;
  if (!rt->has_rerelease) return true;
  size_t have = 0;
  for (size_t i = 0; i < rt->expansion_count; i++) {
    size_t count = 0;
    const char *const *names = H(rt)->pack_original_source_fallbacks(HC(rt), rt->expansions[i].monsters, &count);
    for (size_t j = 0; j < count; j++) {
      if (strcmp(names[j], "monster_gladb") == 0 || strcmp(names[j], "monster_boss5") == 0) continue;
      if (have >= capacity) return false;
      out[have++] = names[j];
    }
  }
  *out_count = have;
  return true;
}

void qa_comp_q2_product_runtime_register_callbacks(qa_comp_q2_product_runtime *rt) {
  for (size_t i = 0; i < rt->module_count; i++) {
    const void *callbacks = rt->modules[i] == (void *)&rt->match
                                ? qa_comp_q2_product_match_callbacks(&rt->match)
                                : H(rt)->module_callbacks(HC(rt), rt->modules[i]);
    if (callbacks != NULL) H(rt)->register_source_callbacks(HC(rt), rt->game, callbacks);
  }
  H(rt)->register_source_callbacks(HC(rt), rt->game, H(rt)->weapons_callbacks(HC(rt), rt->weapons));
  if (rt->armory != NULL) {
    H(rt)->register_source_callbacks(HC(rt), rt->game, H(rt)->projectiles_callbacks(HC(rt), rt->armory));
    H(rt)->register_source_callbacks(HC(rt), rt->game, H(rt)->spheres_callbacks(HC(rt), rt->armory));
    H(rt)->register_source_callbacks(HC(rt), rt->game, H(rt)->doppleganger_callbacks(HC(rt), rt->armory));
  }
}

qa_comp_q2_entity *qa_comp_q2_product_runtime_admit(qa_comp_q2_product_runtime *rt, qa_owned_actor actor,
                                                    const qa_comp_q2_player_admission *admission, const void *landmark) {
  qa_comp_q2_entity *entity = H(rt)->attach_player(HC(rt), rt->game, actor);
  if (!admission->has_use_q2_weapons || admission->use_q2_weapons)
    H(rt)->weapons_bind(HC(rt), rt->weapons, entity, rt->game);
  H(rt)->players_attach(HC(rt), rt->players, entity, rt->game, admission);
  qa_comp_q2_product_match_admitted(&rt->match, entity, rt->game);
  if (rt->match.source_kind != QA_COMP_Q2_MATCH_CTF)
    H(rt)->players_put_in_server(HC(rt), rt->players, entity, rt->game, landmark != NULL, landmark);
  return entity;
}

qa_comp_q2_weapon_input qa_comp_q2_product_runtime_weapon_input(qa_comp_q2_product_runtime *rt, qa_actor_id actor,
                                                                qa_comp_q2_weapon_input input) {
  qa_comp_q2_weapon_input foreign = input;
  if (H(rt)->items_haste_until(HC(rt), rt->items, actor) > H(rt)->now(HC(rt), rt->game)) foreign.haste = true;
  qa_comp_q2_weapon_input adjusted = rt->armory != NULL ? H(rt)->armory_input(HC(rt), rt->armory, actor, foreign) : foreign;
  qa_comp_q2_entity *entity = rt->game->entity(rt->game->ctx, actor);
  return rt->match.source_kind == QA_COMP_Q2_MATCH_CTF && entity != NULL
             ? rt->mode_hooks->ctf_weapon_input(rt->mode_hooks->ctx, rt->match.source, entity, rt->game, adjusted)
             : adjusted;
}

qa_comp_q2_powerups qa_comp_q2_product_runtime_powerups(qa_comp_q2_product_runtime *rt, qa_actor_id actor) {
  qa_comp_q2_player_state *state = player_state_of(rt, actor);
  if (state != NULL && state->has_use_q2_inventory && !state->use_q2_inventory)
    return SVC(rt)->foreign_powerups(SVC(rt)->ctx, actor);
  qa_comp_q2_powerups base = H(rt)->items_player_powerups(HC(rt), rt->items, actor);
  return (qa_comp_q2_powerups){base.quad_until,
                               rt->armory != NULL ? H(rt)->armory_double_until(HC(rt), rt->armory, actor) : 0,
                               base.invulnerability_until};
}

void qa_comp_q2_product_runtime_record_damage(qa_comp_q2_product_runtime *rt, qa_comp_q2_entity *entity,
                                              const qa_damage_decision *decision) {
  H(rt)->players_record_damage(HC(rt), rt->players, entity, rt->game, decision);
}

void qa_comp_q2_product_runtime_before_reaction(qa_comp_q2_product_runtime *rt, qa_actor_id actor,
                                                const qa_attack_provenance *attack) {
  if (rt->armory != NULL) H(rt)->spheres_owner_damaged(HC(rt), rt->armory, actor, attack, rt->game);
}

void qa_comp_q2_product_runtime_movement_impact(qa_comp_q2_product_runtime *rt, qa_actor_id actor,
                                                double impact_delta, bool on_ladder) {
  if (rt->has_rerelease) H(rt)->rerelease_movement_impact(HC(rt), rt->players, actor, impact_delta, on_ladder);
}

void qa_comp_q2_product_runtime_record_foreign_weapon_fire(qa_comp_q2_product_runtime *rt, qa_actor_id actor) {
  if (rt->has_rerelease) H(rt)->rerelease_record_weapon_fire(HC(rt), rt->players, actor, H(rt)->now(HC(rt), rt->game));
}

void qa_comp_q2_product_runtime_after_player_frames(qa_comp_q2_product_runtime *rt) {
  size_t count = 0;
  const qa_actor_id *actors = H(rt)->players_state_actors(HC(rt), rt->players, &count);
  for (size_t i = 0; i < count; i++) {
    qa_comp_q2_entity *entity = rt->game->entity(rt->game->ctx, actors[i]);
    if (entity != NULL) qa_comp_q2_product_match_effects(&rt->match, entity, rt->game);
  }
  if (rt->has_rerelease) H(rt)->rerelease_after_player_frames(HC(rt), rt->rerelease_entities, rt->game);
}

void qa_comp_q2_product_runtime_after_spawn(qa_comp_q2_product_runtime *rt) {
  for (size_t i = 0; i < rt->expansion_count; i++)
    (void)H(rt)->pack_hints_finalize(HC(rt), rt->expansions[i].monsters, rt->game);
  qa_comp_q2_product_match_after_spawn(&rt->match, rt->game);
}

void qa_comp_q2_product_runtime_check_rules(qa_comp_q2_product_runtime *rt) {
  if (strcmp(H(rt)->players_intermission_kind(HC(rt), rt->players), "playing") == 0 &&
      qa_comp_q2_product_match_check_rules(&rt->match, rt->game))
    return;
  H(rt)->players_check_rules(HC(rt), rt->players, rt->game);
}
