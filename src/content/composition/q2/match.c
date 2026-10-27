/* Port of src/content/composition/q2/match.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q2/match.h"

#include <string.h>

#include "qa/core/cvars/info.h"
#include "qa/foundation/support.h"

static bool is_ctf(const qa_comp_q2_product_match *m) { return m->source_kind == QA_COMP_Q2_MATCH_CTF; }
static bool is_lmctf(const qa_comp_q2_product_match *m) { return m->source_kind == QA_COMP_Q2_MATCH_LMCTF; }
static bool is_tag(const qa_comp_q2_product_match *m) { return m->source_kind == QA_COMP_Q2_MATCH_TAG; }
static bool is_ball(const qa_comp_q2_product_match *m) { return m->source_kind == QA_COMP_Q2_MATCH_DEATHBALL; }
static bool is_horde(const qa_comp_q2_product_match *m) { return m->source_kind == QA_COMP_Q2_MATCH_HORDE; }

/* --- constructor bindings ------------------------------------------------- */
static void add_score(void *ctx, qa_actor_id actor, double amount) {
  qa_comp_q2_product_match *match = (qa_comp_q2_product_match *)ctx;
  qa_comp_q2_player_state *state = match->player_state(match->players_ctx, actor);
  if (state == NULL) qa_range_error("Q2 match score requires an admitted player");
  state->score += amount;
}
static void select_spawn(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                         qa_vec3 *out_origin, qa_vec3 *out_angles) {
  (void)ctx;
  game->select_spawn_point(game->ctx, game, entity, out_origin, out_angles);
}
static qa_comp_q2_entity *farthest_spawn(void *ctx, qa_comp_q2_game_services *game) {
  (void)ctx;
  size_t count = 0;
  const qa_comp_q2_entity *const *spots = game->entities_named(game->ctx, game, "info_player_deathmatch", &count);
  const qa_comp_q2_entity *selected = NULL;
  double distance = -1;
  for (size_t i = 0; i < count; i++) {
    double current = game->players_range(game->ctx, game, spots[i]);
    if (current > distance) { selected = spots[i]; distance = current; }
  }
  return (qa_comp_q2_entity *)selected;
}
static const qa_comp_q2_match_selection *ball_settings(void *ctx) {
  return &((qa_comp_q2_product_match *)ctx)->selection;
}
static const char *ball_skin(void *ctx, qa_actor_id actor) {
  qa_comp_q2_product_match *match = (qa_comp_q2_product_match *)ctx;
  qa_comp_q2_player_state *state = match->player_state(match->players_ctx, actor);
  return state != NULL && state->skin != NULL ? state->skin : "";
}
static void diagnostic_print(void *ctx, const char *text) {
  qa_comp_q2_game_services *game = (qa_comp_q2_game_services *)ctx;
  game->diagnostic(game->ctx, text);
}
static void ball_set_skin(void *ctx, qa_actor_id actor, const char *skin) {
  qa_comp_q2_product_match *match = (qa_comp_q2_product_match *)ctx;
  qa_comp_q2_game_services *game = match->world(match->world_ctx);
  qa_comp_q2_player_state *state = match->player_state(match->players_ctx, actor);
  qa_comp_q2_entity *entity = game->entity(game->ctx, actor);
  if (state == NULL || entity == NULL) qa_range_error("Q2 DeathBall skin requires an admitted source player");
  qa_info_options options = {0};
  options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  options.target = QA_CVAR_INFO_CLIENT_USERINFO;
  options.maximum_length = strcmp(game->edition, "rerelease") == 0 ? 2048 : 512;
  options.server_high_characters = false;
  options.print = diagnostic_print;
  options.print_ctx = game;
  char *info = qa_set_info_value(state->userinfo, "skin", skin, &options);
  game->userinfo_changed(game->ctx, entity, game, info);
}
static void ball_end_level(void *ctx) {
  qa_comp_q2_product_match *match = (qa_comp_q2_product_match *)ctx;
  qa_comp_q2_game_services *game = match->world(match->world_ctx);
  game->end_deathmatch_level(game->ctx, game);
}
static double ball_spawn_distance(void *ctx, qa_comp_q2_entity *spot, qa_comp_q2_game_services *game) {
  (void)ctx;
  return game->players_range(game->ctx, game, spot);
}
static void emit_ctf(void *ctx, const qa_comp_q2_ctf_event *ctf_event) {
  qa_comp_q2_product_match *match = (qa_comp_q2_product_match *)ctx;
  qa_comp_q2_event event = {0};
  event.kind = QA_COMP_Q2_EVENT_CTF;
  event.ctf_event = ctf_event;
  match->services->emit(match->services->ctx, &event);
}
static void emit_lmctf(void *ctx, const qa_comp_lmctf_event *lmctf_event) {
  qa_comp_q2_product_match *match = (qa_comp_q2_product_match *)ctx;
  qa_comp_q2_event event = {0};
  event.kind = QA_COMP_Q2_EVENT_LMCTF;
  event.lmctf_event = lmctf_event;
  match->services->emit(match->services->ctx, &event);
}

void qa_comp_q2_product_match_init(qa_comp_q2_product_match *match, const qa_comp_q2_match_selection *selection,
                                   qa_comp_q2_player_state *(*player_state)(void *ctx, qa_actor_id actor),
                                   void *players_ctx, void *items,
                                   qa_comp_q2_game_services *(*world)(void *ctx), void *world_ctx,
                                   const qa_comp_q2_ctf_hooks *ctf_hooks,
                                   const qa_comp_q2_services *services, const qa_comp_q2_mode_hooks *hooks) {
  memset(match, 0, sizeof *match);
  match->selection = *selection;
  match->source_kind = selection->kind;
  match->hooks = hooks;
  match->services = services;
  match->player_state = player_state;
  match->players_ctx = players_ctx;
  match->world = world;
  match->world_ctx = world_ctx;
  match->tag_bindings = (qa_comp_q2_tag_bindings){match, items, add_score, select_spawn, farthest_spawn};
  match->deathball_bindings = (qa_comp_q2_deathball_bindings){match, ball_settings, add_score, select_spawn,
                                                              ball_skin, ball_set_skin, ball_end_level,
                                                              ball_spawn_distance};
  void *ctx = hooks->ctx;
  const qa_shared_grapple_control *grapple = services->shared_grapple;
  switch (selection->kind) {
    case QA_COMP_Q2_MATCH_CTF:
      match->source = hooks->create_ctf(ctx, ctf_hooks, emit_ctf, match, grapple);
      break;
    case QA_COMP_Q2_MATCH_LMCTF:
      match->source = hooks->create_lmctf(ctx, ctf_hooks, emit_lmctf, match,
                                          selection->has_travel ? selection->travel->rules : NULL,
                                          selection->has_travel ? selection->travel : NULL, grapple);
      break;
    case QA_COMP_Q2_MATCH_STANDARD:
      match->source = NULL;
      break;
    case QA_COMP_Q2_MATCH_HORDE:
      match->source = hooks->create_horde(ctx);
      break;
    case QA_COMP_Q2_MATCH_TAG:
      match->source = hooks->create_tag(ctx, &match->tag_bindings);
      break;
    case QA_COMP_Q2_MATCH_DEATHBALL:
      match->source = hooks->create_deathball(ctx, &match->deathball_bindings);
      break;
  }
}

const void *qa_comp_q2_product_match_callbacks(const qa_comp_q2_product_match *match) {
  return match->source != NULL ? match->hooks->callbacks(match->hooks->ctx, match->source) : NULL;
}

bool qa_comp_q2_product_match_spawn(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                    qa_comp_q2_game_services *game) {
  if ((strcmp(entity->classname, "dm_tag_token") == 0 && !is_tag(match)) ||
      (strncmp(entity->classname, "dm_dball_", 9) == 0 && !is_ball(match))) {
    game->remove(game->ctx, entity);
    return true;
  }
  return match->source != NULL && match->hooks->spawn(match->hooks->ctx, match->source, entity, game);
}

void qa_comp_q2_product_match_after_spawn(qa_comp_q2_product_match *match, qa_comp_q2_game_services *game) {
  if (match->source == NULL || is_horde(match)) return;
  if (is_ctf(match)) match->hooks->ctf_after_spawn(match->hooks->ctx, match->source, game);
  else match->hooks->post_spawn(match->hooks->ctx, match->source, game);
}

void qa_comp_q2_product_match_admitted(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                       qa_comp_q2_game_services *game) {
  if (is_ctf(match) || is_lmctf(match)) match->hooks->admitted(match->hooks->ctx, match->source, entity, game);
  else if (is_ball(match)) match->hooks->client_begin(match->hooks->ctx, match->source, entity, game);
}

const void *qa_comp_q2_product_match_select_spawn(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                                  qa_comp_q2_game_services *game) {
  return match->source != NULL && !is_tag(match) && !is_horde(match)
             ? match->hooks->select_spawn(match->hooks->ctx, match->source, entity, game)
             : NULL;
}

void qa_comp_q2_product_match_score(qa_comp_q2_product_match *match, qa_comp_q2_entity *victim,
                                    qa_comp_q2_game_services *game, double change, double means,
                                    qa_comp_q2_entity *recipient, qa_comp_q2_entity *attacker) {
  if (attacker == NULL) attacker = recipient;
  if (is_ctf(match) || is_lmctf(match)) {
    match->hooks->ctf_score(match->hooks->ctx, match->source, victim, attacker, game, change, means, recipient);
    return;
  }
  if (is_tag(match)) {
    match->hooks->tag_score(match->hooks->ctx, match->source, recipient, victim, game, change, means);
    return;
  }
  qa_comp_q2_player_state *state = match->player_state(match->players_ctx, recipient->actor.id);
  if (state == NULL) qa_range_error("Q2 match score has no admitted source recipient");
  state->score += change;
}

void qa_comp_q2_product_match_death(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                    qa_comp_q2_game_services *game) {
  if (is_ctf(match)) match->hooks->death(match->hooks->ctx, match->source, entity, game);
  else if (is_lmctf(match) || is_tag(match)) match->hooks->player_death(match->hooks->ctx, match->source, entity, game);
}

void qa_comp_q2_product_match_disconnect(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                         qa_comp_q2_game_services *game) {
  if (is_tag(match) || is_ctf(match) || is_lmctf(match))
    match->hooks->disconnect(match->hooks->ctx, match->source, entity, game);
}

double qa_comp_q2_product_match_damage(qa_comp_q2_product_match *match, qa_actor_id target,
                                       const qa_actor_id *attacker, double amount) {
  return is_tag(match) || is_ball(match)
             ? match->hooks->change_damage(match->hooks->ctx, match->source, target, attacker, amount)
             : amount;
}

double qa_comp_q2_product_match_knockback(qa_comp_q2_product_match *match, qa_actor_id target, double amount,
                                          double means, qa_comp_q2_game_services *game) {
  return is_ball(match) ? match->hooks->change_knockback(match->hooks->ctx, match->source, target, amount, means, game)
                        : amount;
}

bool qa_comp_q2_product_match_check_rules(qa_comp_q2_product_match *match, qa_comp_q2_game_services *game) {
  return (is_ball(match) || is_ctf(match)) && match->hooks->check_rules(match->hooks->ctx, match->source, game);
}

void qa_comp_q2_product_match_drop_inventory(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                             qa_comp_q2_game_services *game) {
  if (is_ctf(match) || is_lmctf(match)) match->hooks->drop_inventory(match->hooks->ctx, match->source, entity, game);
}

bool qa_comp_q2_product_match_command(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                      qa_comp_q2_game_services *game, const char *name,
                                      const char *const *args, size_t arg_count) {
  return (is_ctf(match) || is_lmctf(match)) &&
         match->hooks->command(match->hooks->ctx, match->source, entity, game, name, args, arg_count);
}

void qa_comp_q2_product_match_before_player(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                            qa_comp_q2_game_services *game) {
  if (is_ctf(match)) match->hooks->before_player(match->hooks->ctx, match->source, entity, game);
}

void qa_comp_q2_product_match_player_spawned(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                             qa_comp_q2_game_services *game) {
  if (is_lmctf(match)) {
    match->hooks->lmctf_player_spawned(match->hooks->ctx, match->source, entity, game);
    return;
  }
  if (is_ctf(match)) {
    int team = match->hooks->ctf_state_team(match->hooks->ctx, match->source, entity->actor.id);
    if (team < 0) return;
    game->set_traits_team(game->ctx, entity->actor, team == 0 ? NULL : team == 1 ? "RED" : "BLUE");
    qa_comp_q2_player_state *state = match->player_state(match->players_ctx, entity->actor.id);
    if (match->hooks->ctf_grapple_native_enabled(match->hooks->ctx, match->source) && team != 0 &&
        state != NULL && state->use_q2_weapons)
      game->inventory_configure(game->ctx, entity->actor,
                                (qa_inventory_entry){"q2:weapon_grapple", 1, 1, false, {0}});
    match->hooks->ctf_assign_skin(match->hooks->ctx, match->source, entity);
  }
}

double qa_comp_q2_product_match_gravity_scale(qa_comp_q2_product_match *match, qa_actor_id actor) {
  return is_lmctf(match) ? match->hooks->lmctf_gravity_scale(match->hooks->ctx, match->source, actor) : 1;
}

/* --- sourceEffects over the landed qa_q2_damage_source_effects ----------- */
static bool armor_allowed(void *ctx, qa_damage_request request, qa_combat_state target,
                          const qa_combat_state *attacker) {
  qa_comp_q2_match_effects_state *state = (qa_comp_q2_match_effects_state *)ctx;
  qa_comp_q2_product_match *match = state->match;
  qa_comp_q2_game_services *game = state->game;
  bool flag_clear = is_lmctf(match) ? ((int64_t)match->hooks->lmctf_ctf_flags(match->hooks->ctx, match->source) & 1024) == 0
                                    : ((int64_t)game->deathmatch_flags(game->ctx) & 262144) == 0;
  return flag_clear || request.attack.attacker == NULL ||
         qa_same_actor(*request.attack.attacker, request.target) || target.team == NULL ||
         attacker == NULL || attacker->team == NULL || strcmp(target.team, attacker->team) != 0;
}
static double ctf_before_momentum(void *ctx, qa_damage_request request, double damage,
                                  qa_combat_state target, const qa_combat_state *attacker) {
  qa_comp_q2_match_effects_state *state = (qa_comp_q2_match_effects_state *)ctx;
  (void)target; (void)attacker;
  return state->match->hooks->tech_strength(state->match->hooks->ctx, state->match->source,
                                            request.attack.attacker, state->game, damage);
}
static double ctf_after_armor(void *ctx, qa_damage_request request, double take,
                              qa_combat_state target, const qa_combat_state *attacker) {
  qa_comp_q2_match_effects_state *state = (qa_comp_q2_match_effects_state *)ctx;
  (void)target; (void)attacker;
  return state->match->hooks->tech_resistance(state->match->hooks->ctx, state->match->source, request.target,
                                              state->game, take);
}
static void ctf_after_health(void *ctx, qa_damage_decision decision, qa_current_combat_state current) {
  qa_comp_q2_match_effects_state *state = (qa_comp_q2_match_effects_state *)ctx;
  (void)current;
  state->match->hooks->flags_hurt_carrier(state->match->hooks->ctx, state->match->source, decision.request.target,
                                          decision.request.attack.attacker, state->game);
}
static double lmctf_before_momentum(void *ctx, qa_damage_request request, double damage,
                                    qa_combat_state target, const qa_combat_state *attacker) {
  qa_comp_q2_match_effects_state *state = (qa_comp_q2_match_effects_state *)ctx;
  (void)target; (void)attacker;
  return state->match->hooks->rune_damage(state->match->hooks->ctx, state->match->source,
                                           request.attack.attacker, damage, state->game);
}
static double lmctf_after_power_armor(void *ctx, qa_damage_request request, double take,
                                      qa_combat_state target, const qa_combat_state *attacker) {
  qa_comp_q2_match_effects_state *state = (qa_comp_q2_match_effects_state *)ctx;
  (void)target; (void)attacker;
  return state->match->hooks->rune_after_power_armor(state->match->hooks->ctx, state->match->source, request.target,
                                                     take, state->game);
}
static void lmctf_after_health(void *ctx, qa_damage_decision decision, qa_current_combat_state current) {
  qa_comp_q2_match_effects_state *state = (qa_comp_q2_match_effects_state *)ctx;
  (void)current;
  state->match->hooks->rune_after_health(state->match->hooks->ctx, state->match->source, decision.request.target,
                                          decision.request.attack.attacker, decision.applied_damage, state->game);
}

qa_q2_damage_source_effects qa_comp_q2_product_match_source_effects(qa_comp_q2_match_effects_state *state) {
  qa_q2_damage_source_effects effects = {0};
  effects.ctx = state;
  if (is_ctf(state->match)) {
    effects.before_momentum = ctf_before_momentum;
    effects.after_armor = ctf_after_armor;
    effects.power_armor_allowed = armor_allowed;
    effects.armor_allowed = armor_allowed;
    effects.after_health = ctf_after_health;
  } else if (is_lmctf(state->match)) {
    effects.before_momentum = lmctf_before_momentum;
    effects.after_power_armor = lmctf_after_power_armor;
    effects.armor_allowed = armor_allowed;
    effects.after_health = lmctf_after_health;
  }
  return effects;
}

void qa_comp_q2_product_match_effects(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                      qa_comp_q2_game_services *game) {
  if (is_ctf(match)) match->hooks->ctf_after_player(match->hooks->ctx, match->source, entity, game);
  if (is_lmctf(match)) match->hooks->lmctf_player_frame(match->hooks->ctx, match->source, entity, game);
  if (is_tag(match)) {
    entity->effects = (entity->effects & ~0x20000000) | match->hooks->tag_effects(match->hooks->ctx, match->source,
                                                                                    entity->actor.id);
    game->show(game->ctx, entity);
  }
}
