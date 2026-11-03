/* Port of src/content/composition/q2/match.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q2/match.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/base/player/spawns.h"
#include "qa/core/cvars/info.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static bool starts_with(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}
static qa_q2_players *players_of(qa_q2_product_match *match) { return match->players(match->accessor_ctx); }

/* ---- constructor closures ---- */

/* TS addScore. */
static void add_score(void *ctx, qa_actor_id actor, double amount) {
  qa_q2_product_match *match = ctx;
  qa_q2_base_player_state *state = qa_q2_players_get_state(players_of(match), actor);
  if (state == NULL) qa_raise("Error", 0, 0, "Q2 match score requires an admitted player");
  state->score += amount;
}

/* TS selectSpawn: selectQ2Spawn with the admitted state, or a fresh
 * Q2PlayerState(0, now) when the actor has none. */
static qa_q2mp_spawn_point select_spawn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_product_match *match = ctx;
  qa_q2_players *players = players_of(match);
  qa_q2_base_player_state *state = qa_q2_players_get_state(players, entity->actor.id);
  qa_q2_base_player_state fresh;
  bool made = state == NULL;
  if (made) {
    qa_q2_base_player_state_init(&fresh, 0, game->host->now(game->host->ctx));
    state = &fresh;
  }
  qa_q2_entity *spot = qa_q2_select_spawn(game, state, qa_q2_players_rules(players)->spawn_point);
  if (made) qa_q2_base_player_state_destroy(&fresh);
  qa_q2mp_spawn_point point;
  point.origin = qa_q2_spawn_origin(spot, game);
  point.angles = qa_q2_game_body(game, spot).angles;
  return point;
}

/* TS Tag farthestSpawn. */
static qa_q2_entity *farthest_spawn(void *ctx, qa_q2_game_services *game) {
  (void)ctx;
  qa_q2_entity *selected = NULL;
  double distance = -1;
  size_t count = 0;
  qa_q2_entity **spots = qa_q2_entities_named(game, "info_player_deathmatch", &count);
  for (size_t i = 0; i < count; i++) {
    double current = qa_q2_players_range(game, spots[i]);
    if (current > distance) { selected = spots[i]; distance = current; }
  }
  free(spots);
  return selected;
}

/* TS DeathBall settings: () => selection (its getters read live). The
 * strings stay valid until the next settings() call. */
static qa_q2mp_deathball_settings deathball_settings(void *ctx) {
  qa_q2_product_match *match = ctx;
  free(match->settings_skins[0]);
  free(match->settings_skins[1]);
  match->settings_skins[0] = qa_q2_match_selection_team1_skin(&match->selection);
  match->settings_skins[1] = qa_q2_match_selection_team2_skin(&match->selection);
  qa_q2mp_deathball_settings settings;
  settings.team1_skin = match->settings_skins[0];
  settings.team2_skin = match->settings_skins[1];
  settings.goal_limit = qa_q2_match_selection_goal_limit(&match->selection);
  return settings;
}
static const char *deathball_skin(void *ctx, qa_actor_id actor) {
  qa_q2_product_match *match = ctx;
  qa_q2_base_player_state *state = qa_q2_players_get_state(players_of(match), actor);
  return state == NULL ? "" : state->skin;
}
static void print_diagnostic(void *ctx, const char *text) {
  qa_q2_game_services *game = ctx;
  game->host->diagnostic(game->host->ctx, text);
}
static void deathball_set_skin(void *ctx, qa_actor_id actor, const char *skin) {
  qa_q2_product_match *match = ctx;
  qa_q2_players *players = players_of(match);
  qa_q2_game_services *game = match->world(match->accessor_ctx);
  qa_q2_base_player_state *state = qa_q2_players_get_state(players, actor);
  qa_q2_entity *entity = game->entity(game->ctx, &actor);
  if (state == NULL || entity == NULL) qa_raise("Error", 0, 0, "Q2 DeathBall skin requires an admitted source player");
  qa_info_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  options.maximum_length = game->options.edition == QA_Q2_EDITION_RERELEASE ? 2048 : 512;
  options.target = QA_CVAR_INFO_CLIENT_USERINFO;
  options.server_high_characters = false;
  options.print = print_diagnostic;
  options.print_ctx = game;
  char *info = qa_set_info_value(state->userinfo, "skin", skin, &options);
  if (info == NULL) qa_range_error("out of memory");
  qa_q2_players_userinfo_changed(players, entity, game, info);
  free(info);
}
static void deathball_end_level(void *ctx) {
  qa_q2_product_match *match = ctx;
  qa_q2_players_end_deathmatch_level(players_of(match), match->world(match->accessor_ctx));
}
static double deathball_spawn_distance(void *ctx, qa_q2_entity *spot, qa_q2_game_services *game) {
  (void)ctx;
  return qa_q2_players_range(game, spot);
}

/* ---- CTF / LMCTF hooks: TS { ...hooks, emit }. Each leaf forwards to the
 * composition's hook with the composition's own ctx; emit wraps the event
 * as a Q2CompositionEvent for services.emit. ---- */
#define HOOKS(match) (&((qa_q2_product_match *)(match))->hooks)
static qa_q2_base_player_state *fw_player(void *ctx, qa_actor_id actor) {
  return HOOKS(ctx)->player(HOOKS(ctx)->ctx, actor);
}
static void fw_set_skin(void *ctx, qa_actor_id actor, const char *skin) {
  HOOKS(ctx)->set_skin(HOOKS(ctx)->ctx, actor, skin);
}
static void fw_spawn_player(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  HOOKS(ctx)->spawn_player(HOOKS(ctx)->ctx, entity, game);
}
static void fw_observer(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  HOOKS(ctx)->observer(HOOKS(ctx)->ctx, entity, game);
}
static void fw_teleport(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 origin, qa_vec3 angles,
                        qa_vec3 velocity) {
  HOOKS(ctx)->teleport(HOOKS(ctx)->ctx, entity, game, origin, angles, velocity);
}
static void fw_chase(void *ctx, qa_actor_id actor) { HOOKS(ctx)->chase(HOOKS(ctx)->ctx, actor); }
static void fw_set_grapple_prediction(void *ctx, qa_actor_id actor, bool suppressed) {
  HOOKS(ctx)->set_grapple_prediction(HOOKS(ctx)->ctx, actor, suppressed);
}
static double fw_gravity(void *ctx) { return HOOKS(ctx)->gravity(HOOKS(ctx)->ctx); }
static void fw_end_level(void *ctx, qa_q2_game_services *game, const char *map) {
  HOOKS(ctx)->end_level(HOOKS(ctx)->ctx, game, map);
}
static void fw_kick(void *ctx, qa_actor_id actor) { HOOKS(ctx)->kick(HOOKS(ctx)->ctx, actor); }
static void fw_set_deathmatch_flags(void *ctx, double flags) {
  HOOKS(ctx)->set_deathmatch_flags(HOOKS(ctx)->ctx, flags);
}
static bool fw_chat_allowed(void *ctx, qa_actor_id actor, qa_q2_game_services *game) {
  return HOOKS(ctx)->chat_allowed(HOOKS(ctx)->ctx, actor, game);
}
static void ctf_emit(void *ctx, qa_q2_ctf_event event) {
  qa_q2_product_match *match = ctx;
  qa_q2_composition_event wrapped;
  memset(&wrapped, 0, sizeof wrapped);
  wrapped.kind = QA_Q2_COMPOSITION_EVENT_CTF;
  wrapped.ctf = &event;
  match->services->emit(match->services->ctx, wrapped);
}
static void lmctf_emit(void *ctx, qa_lmctf_event event) {
  qa_q2_product_match *match = ctx;
  qa_q2_composition_event wrapped;
  memset(&wrapped, 0, sizeof wrapped);
  wrapped.kind = QA_Q2_COMPOSITION_EVENT_LMCTF;
  wrapped.lmctf = &event;
  match->services->emit(match->services->ctx, wrapped);
}
static qa_q2_ctf_hooks ctf_hooks(qa_q2_product_match *match) {
  qa_q2_ctf_hooks hooks = match->hooks;
  hooks.ctx = match;
  hooks.player = fw_player; hooks.set_skin = fw_set_skin; hooks.spawn_player = fw_spawn_player;
  hooks.observer = fw_observer; hooks.teleport = fw_teleport; hooks.chase = fw_chase;
  hooks.set_grapple_prediction = fw_set_grapple_prediction; hooks.gravity = fw_gravity; hooks.emit = ctf_emit;
  hooks.end_level = fw_end_level; hooks.kick = fw_kick; hooks.set_deathmatch_flags = fw_set_deathmatch_flags;
  hooks.chat_allowed = fw_chat_allowed;
  return hooks;
}
static qa_lmctf_hooks lmctf_hooks(qa_q2_product_match *match) {
  qa_lmctf_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = match;
  hooks.items = match->hooks.items; hooks.weapons = match->hooks.weapons;
  hooks.player = fw_player; hooks.set_skin = fw_set_skin; hooks.spawn_player = fw_spawn_player;
  hooks.observer = fw_observer; hooks.teleport = fw_teleport; hooks.chase = fw_chase;
  hooks.set_grapple_prediction = fw_set_grapple_prediction; hooks.gravity = fw_gravity; hooks.emit = lmctf_emit;
  hooks.end_level = fw_end_level; hooks.kick = fw_kick; hooks.set_deathmatch_flags = fw_set_deathmatch_flags;
  hooks.chat_allowed = fw_chat_allowed;
  return hooks;
}

void qa_q2_product_match_init(qa_q2_product_match *match, const qa_q2_match_selection *selection,
                              void *accessor_ctx, qa_q2_players *(*players)(void *ctx),
                              qa_q2_item_module *items, qa_q2_game_services *(*world)(void *ctx),
                              qa_q2_ctf_hooks hooks, const qa_q2_composition_services *services) {
  memset(match, 0, sizeof *match);
  match->selection = *selection;
  match->source_kind = selection->kind;
  match->accessor_ctx = accessor_ctx;
  match->players = players;
  match->world = world;
  match->hooks = hooks;
  match->services = services;
  qa_shared_grapple_control *grapple = services->shared_grapple;
  switch (selection->kind) {
    case QA_Q2_MATCH_CTF:
      match->ctf = qa_q2_ctf_create(ctf_hooks(match), NULL, 0, grapple);
      if (match->ctf == NULL) qa_range_error("out of memory");
      break;
    case QA_Q2_MATCH_LMCTF:
      match->lmctf = qa_q2_lmctf_create(lmctf_hooks(match), selection->travel == NULL ? NULL : &selection->travel->rules,
                                        selection->travel, grapple);
      if (match->lmctf == NULL) qa_range_error("out of memory");
      break;
    case QA_Q2_MATCH_STANDARD:
      break;
    case QA_Q2_MATCH_HORDE:
      match->horde = qa_q2_horde_create();
      if (match->horde == NULL) qa_range_error("out of memory");
      break;
    case QA_Q2_MATCH_TAG: {
      qa_q2mp_tag_hooks tag;
      memset(&tag, 0, sizeof tag);
      tag.items = items; tag.ctx = match;
      tag.select_spawn = select_spawn; tag.farthest_spawn = farthest_spawn; tag.add_score = add_score;
      qa_q2mp_tag_init(&match->tag, tag);
      break;
    }
    case QA_Q2_MATCH_DEATHBALL: {
      qa_q2mp_deathball_hooks ball;
      memset(&ball, 0, sizeof ball);
      ball.ctx = match;
      ball.settings = deathball_settings; ball.add_score = add_score; ball.select_spawn = select_spawn;
      ball.skin = deathball_skin; ball.set_skin = deathball_set_skin; ball.end_level = deathball_end_level;
      ball.spawn_distance = deathball_spawn_distance;
      qa_q2mp_deathball_init(&match->ball, ball);
      break;
    }
  }
}

void qa_q2_product_match_destroy(qa_q2_product_match *match) {
  if (match->ctf != NULL) qa_q2_ctf_destroy(match->ctf);
  if (match->lmctf != NULL) qa_q2_lmctf_destroy(match->lmctf);
  if (match->horde != NULL) qa_q2_horde_destroy(match->horde);
  free(match->settings_skins[0]);
  free(match->settings_skins[1]);
  match->ctf = NULL; match->lmctf = NULL; match->horde = NULL;
  match->settings_skins[0] = match->settings_skins[1] = NULL;
}

/* TS get callbacks(): this.source?.callbacks ?? {}. */
qa_q2_callback_definitions qa_q2_product_match_callbacks(qa_q2_product_match *match) {
  switch (match->source_kind) {
    case QA_Q2_MATCH_CTF: return qa_q2_ctf_callbacks(match->ctf);
    case QA_Q2_MATCH_LMCTF: return qa_q2_lmctf_callbacks(match->lmctf);
    case QA_Q2_MATCH_HORDE: return qa_q2_horde_callbacks(match->horde);
    case QA_Q2_MATCH_TAG: return qa_q2mp_tag_callbacks(&match->tag);
    case QA_Q2_MATCH_DEATHBALL: return qa_q2mp_deathball_callbacks(&match->ball);
    case QA_Q2_MATCH_STANDARD: break;
  }
  qa_q2_callback_definitions none;
  memset(&none, 0, sizeof none);
  return none;
}

bool qa_q2_product_match_spawn(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game) {
  if ((str_eq(entity->classname, "dm_tag_token") && match->source_kind != QA_Q2_MATCH_TAG) ||
      (starts_with(entity->classname, "dm_dball_") && match->source_kind != QA_Q2_MATCH_DEATHBALL)) {
    game->remove(game->ctx, entity);
    return true;
  }
  switch (match->source_kind) {
    case QA_Q2_MATCH_CTF: return qa_q2_ctf_spawn(match->ctf, entity, game);
    case QA_Q2_MATCH_LMCTF: return qa_q2_lmctf_spawn(match->lmctf, entity, game);
    case QA_Q2_MATCH_HORDE: return qa_q2_horde_spawn(match->horde, entity, game);
    case QA_Q2_MATCH_TAG: return qa_q2mp_tag_spawn(&match->tag, entity, game);
    case QA_Q2_MATCH_DEATHBALL: return qa_q2mp_deathball_spawn(&match->ball, entity, game);
    case QA_Q2_MATCH_STANDARD: break;
  }
  return false;
}

static bool spawn_module_spawn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2_product_match_spawn(ctx, entity, game);
}
qa_q2_spawn_module qa_q2_product_match_as_spawn(qa_q2_product_match *match) {
  qa_q2_spawn_module module;
  memset(&module, 0, sizeof module);
  module.ctx = match;
  module.spawn = spawn_module_spawn;
  return module;
}

void qa_q2_product_match_after_spawn(qa_q2_product_match *match, qa_q2_game_services *game) {
  switch (match->source_kind) {
    case QA_Q2_MATCH_CTF: qa_q2_ctf_after_spawn(match->ctf, game); break;
    case QA_Q2_MATCH_HORDE: break;
    case QA_Q2_MATCH_LMCTF: qa_q2_lmctf_post_spawn(match->lmctf, game); break;
    case QA_Q2_MATCH_TAG: qa_q2mp_tag_post_spawn(&match->tag, game); break;
    case QA_Q2_MATCH_DEATHBALL: qa_q2mp_deathball_post_spawn(&match->ball, game); break;
    case QA_Q2_MATCH_STANDARD: break;
  }
}

void qa_q2_product_match_admitted(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (match->source_kind == QA_Q2_MATCH_CTF) qa_q2_ctf_admitted(match->ctf, entity, game);
  else if (match->source_kind == QA_Q2_MATCH_LMCTF) qa_q2_lmctf_admitted(match->lmctf, entity, game);
  else if (match->source_kind == QA_Q2_MATCH_DEATHBALL) qa_q2mp_deathball_client_begin(&match->ball, entity, game);
}

bool qa_q2_product_match_select_spawn(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game,
                                      qa_vec3 *out_origin, qa_vec3 *out_angles) {
  switch (match->source_kind) {
    case QA_Q2_MATCH_CTF: return qa_q2_ctf_select_spawn(match->ctf, entity, game, out_origin, out_angles);
    case QA_Q2_MATCH_LMCTF: qa_q2_lmctf_select_spawn(match->lmctf, entity, game, out_origin, out_angles); return true;
    case QA_Q2_MATCH_DEATHBALL: {
      qa_q2mp_spawn_point point = qa_q2mp_deathball_select_spawn(&match->ball, entity, game);
      *out_origin = point.origin;
      *out_angles = point.angles;
      return true;
    }
    case QA_Q2_MATCH_TAG: case QA_Q2_MATCH_HORDE: case QA_Q2_MATCH_STANDARD: break;
  }
  return false;
}

void qa_q2_product_match_score(qa_q2_product_match *match, qa_q2_entity *victim, qa_q2_game_services *game,
                               double change, double means, qa_q2_entity *recipient, bool has_attacker,
                               qa_q2_entity *attacker) {
  if (!has_attacker) attacker = recipient;
  if (match->source_kind == QA_Q2_MATCH_CTF) {
    qa_q2_ctf_score_event(match->ctf, victim, attacker, game, change, means, recipient);
    return;
  }
  if (match->source_kind == QA_Q2_MATCH_LMCTF) {
    qa_q2_lmctf_score_event(match->lmctf, victim, attacker, game, change, means, recipient);
    return;
  }
  if (match->source_kind == QA_Q2_MATCH_TAG) {
    qa_q2mp_tag_score(&match->tag, recipient, victim, game, change, means);
    return;
  }
  qa_q2_base_player_state *state = qa_q2_players_get_state(players_of(match), recipient->actor.id);
  if (state == NULL) qa_raise("Error", 0, 0, "Q2 match score has no admitted source recipient");
  state->score += change;
}

void qa_q2_product_match_death(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (match->source_kind == QA_Q2_MATCH_CTF) qa_q2_ctf_death(match->ctf, entity, game);
  else if (match->source_kind == QA_Q2_MATCH_LMCTF) qa_q2_lmctf_player_death(match->lmctf, entity, game);
  else if (match->source_kind == QA_Q2_MATCH_TAG) qa_q2mp_tag_player_death(&match->tag, entity, game);
}

void qa_q2_product_match_disconnect(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (match->source_kind == QA_Q2_MATCH_TAG) qa_q2mp_tag_disconnect(&match->tag, entity, game);
  else if (match->source_kind == QA_Q2_MATCH_CTF) qa_q2_ctf_disconnect(match->ctf, entity, game);
  else if (match->source_kind == QA_Q2_MATCH_LMCTF) qa_q2_lmctf_disconnect(match->lmctf, entity, game);
}

double qa_q2_product_match_damage(qa_q2_product_match *match, qa_actor_id target, const qa_actor_id *attacker,
                                  double amount) {
  qa_actor_id none;
  memset(&none, 0, sizeof none);
  if (match->source_kind == QA_Q2_MATCH_TAG)
    return qa_q2mp_tag_change_damage(&match->tag, target, attacker != NULL, attacker != NULL ? *attacker : none, amount);
  if (match->source_kind == QA_Q2_MATCH_DEATHBALL)
    return qa_q2mp_deathball_change_damage(&match->ball, target, attacker != NULL, attacker != NULL ? *attacker : none,
                                           amount);
  return amount;
}

double qa_q2_product_match_knockback(qa_q2_product_match *match, qa_actor_id target, double amount, double means,
                                     qa_q2_game_services *game) {
  return match->source_kind == QA_Q2_MATCH_DEATHBALL
             ? qa_q2mp_deathball_change_knockback(&match->ball, target, amount, means, game)
             : amount;
}

bool qa_q2_product_match_check_rules(qa_q2_product_match *match, qa_q2_game_services *game) {
  if (match->source_kind == QA_Q2_MATCH_DEATHBALL) return qa_q2mp_deathball_check_rules(&match->ball, game);
  if (match->source_kind == QA_Q2_MATCH_CTF) return qa_q2_ctf_check_rules(match->ctf, game);
  return false;
}

void qa_q2_product_match_drop_inventory(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (match->source_kind == QA_Q2_MATCH_CTF) qa_q2_ctf_drop_inventory(match->ctf, entity, game);
  else if (match->source_kind == QA_Q2_MATCH_LMCTF) qa_q2_lmctf_drop_inventory(match->lmctf, entity, game);
}

bool qa_q2_product_match_command(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game,
                                 const char *name, const char *const *args, size_t arg_count) {
  if (match->source_kind == QA_Q2_MATCH_CTF) return qa_q2_ctf_command(match->ctf, entity, game, name, args, arg_count);
  if (match->source_kind == QA_Q2_MATCH_LMCTF)
    return qa_q2_lmctf_command(match->lmctf, entity, game, name, args, arg_count);
  return false;
}

void qa_q2_product_match_before_player(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (match->source_kind == QA_Q2_MATCH_CTF) qa_q2_ctf_before_player(match->ctf, entity, game);
}

void qa_q2_product_match_player_spawned(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (match->source_kind == QA_Q2_MATCH_LMCTF) {
    qa_q2_lmctf_player_spawned(match->lmctf, entity, game);
    return;
  }
  if (match->source_kind != QA_Q2_MATCH_CTF) return;
  qa_q2_ctf_player_state *state = qa_q2_ctf_context_get(qa_q2_ctf_get_context(match->ctf), entity->actor.id);
  if (state == NULL) return;
  qa_combat_trait_changes traits;
  memset(&traits, 0, sizeof traits);
  traits.has_team = true;
  traits.team = state->team == 0 ? NULL : state->team == 1 ? "RED" : "BLUE";
  qa_gameplay_authority_set_traits(game->host->combat, entity->actor, traits);
  qa_q2_base_player_state *player = qa_q2_players_get_state(players_of(match), entity->actor.id);
  if (qa_q2_ctf_grapple_module_native_enabled(qa_q2_ctf_get_grapple(match->ctf)) && state->team != 0 &&
      player != NULL && player->use_q2_weapons) {
    qa_inventory_entry grapple;
    memset(&grapple, 0, sizeof grapple);
    grapple.item = "q2:weapon_grapple"; grapple.count = 1; grapple.capacity = 1;
    qa_shared_inventory_table_configure(game->host->inventory, entity->actor, grapple, NULL, NULL);
  }
  qa_q2_ctf_assign_skin(match->ctf, entity);
}

double qa_q2_product_match_gravity_scale(qa_q2_product_match *match, qa_actor_id actor) {
  return match->source_kind == QA_Q2_MATCH_LMCTF ? qa_q2_lmctf_gravity_scale(match->lmctf, actor) : 1;
}

/* ---- sourceEffects(game) ---- */
static bool armor_allowed(void *ctx, qa_damage_request request, qa_combat_state target,
                          const qa_combat_state *attacker) {
  qa_q2_product_match_effects_binding *effects = ctx;
  qa_q2_product_match *match = effects->match;
  bool unrestricted = match->source_kind == QA_Q2_MATCH_LMCTF
      ? (qa_js_to_int32(qa_q2_lmctf_get_rules(match->lmctf)->ctf_flags) & 1024) == 0
      : (qa_js_to_int32(qa_q2_options_deathmatch_flags(&effects->game->options)) & 262144) == 0;
  return unrestricted || request.attack.attacker == NULL || qa_same_actor(*request.attack.attacker, request.target) ||
         target.team == NULL || attacker == NULL || !str_eq(target.team, attacker->team);
}
static double ctf_before_momentum(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                                  const qa_combat_state *attacker) {
  (void)target; (void)attacker;
  qa_q2_product_match_effects_binding *effects = ctx;
  return qa_q2_ctf_techs_strength(qa_q2_ctf_get_techs(effects->match->ctf), request.attack.attacker, effects->game,
                                  damage);
}
static double ctf_after_armor(void *ctx, qa_damage_request request, double take, qa_combat_state target,
                              const qa_combat_state *attacker) {
  (void)target; (void)attacker;
  qa_q2_product_match_effects_binding *effects = ctx;
  return qa_q2_ctf_techs_resistance(qa_q2_ctf_get_techs(effects->match->ctf), request.target, effects->game, take);
}
static void ctf_after_health(void *ctx, qa_damage_decision decision, qa_current_combat_state current) {
  (void)current;
  qa_q2_product_match_effects_binding *effects = ctx;
  qa_q2_ctf_flags_hurt_carrier(qa_q2_ctf_get_flags(effects->match->ctf), decision.request.target,
                               decision.request.attack.attacker, effects->game);
}
static double lmctf_before_momentum(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                                    const qa_combat_state *attacker) {
  (void)target; (void)attacker;
  qa_q2_product_match_effects_binding *effects = ctx;
  return qa_lmctf_runes_damage(qa_q2_lmctf_get_runes(effects->match->lmctf), request.attack.attacker, damage,
                               effects->game);
}
static double lmctf_after_power_armor(void *ctx, qa_damage_request request, double take, qa_combat_state target,
                                      const qa_combat_state *attacker) {
  (void)target; (void)attacker;
  qa_q2_product_match_effects_binding *effects = ctx;
  return qa_lmctf_runes_after_power_armor(qa_q2_lmctf_get_runes(effects->match->lmctf), request.target, take,
                                          effects->game);
}
static void lmctf_after_health(void *ctx, qa_damage_decision decision, qa_current_combat_state current) {
  (void)current;
  qa_q2_product_match_effects_binding *effects = ctx;
  qa_lmctf_runes_after_health(qa_q2_lmctf_get_runes(effects->match->lmctf), decision.request.target,
                              decision.request.attack.attacker, decision.applied_damage, effects->game);
}

qa_q2_damage_source_effects qa_q2_product_match_source_effects(qa_q2_product_match_effects_binding *storage,
                                                               qa_q2_product_match *match,
                                                               qa_q2_game_services *game) {
  storage->match = match;
  storage->game = game;
  qa_q2_damage_source_effects effects;
  memset(&effects, 0, sizeof effects);
  effects.ctx = storage;
  if (match->source_kind == QA_Q2_MATCH_CTF) {
    effects.before_momentum = ctf_before_momentum;
    effects.after_armor = ctf_after_armor;
    effects.power_armor_allowed = armor_allowed;
    effects.armor_allowed = armor_allowed;
    effects.after_health = ctf_after_health;
  } else if (match->source_kind == QA_Q2_MATCH_LMCTF) {
    effects.before_momentum = lmctf_before_momentum;
    effects.after_power_armor = lmctf_after_power_armor;
    effects.armor_allowed = armor_allowed;
    effects.after_health = lmctf_after_health;
  }
  return effects;
}

void qa_q2_product_match_effects(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (match->source_kind == QA_Q2_MATCH_CTF) qa_q2_ctf_after_player(match->ctf, entity, game);
  if (match->source_kind == QA_Q2_MATCH_LMCTF) qa_q2_lmctf_player_frame(match->lmctf, entity, game);
  if (match->source_kind == QA_Q2_MATCH_TAG) {
    entity->effects = (double)((qa_js_to_int32(entity->effects) & ~0x20000000) |
                               qa_js_to_int32(qa_q2mp_tag_effects(&match->tag, entity->actor.id)));
    game->show(game->ctx, entity);
  }
}
