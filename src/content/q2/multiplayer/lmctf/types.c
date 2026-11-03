/* Port of src/content/q2/multiplayer/lmctf/types.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "../multiplayer_internal.h"

const qa_lmctf_rune_definition QA_LMCTF_RUNES[5] = {
    {"damage", 1, "damage_rune", "q2:damage_rune", "models/ctf/damage/tris.md2", "a_strength", "Damage Artifact"},
    {"haste", 4, "haste_rune", "q2:haste_rune", "models/ctf/haste/tris.md2", "a_haste", "Haste Artifact"},
    {"resist", 2, "resist_rune", "q2:resist_rune", "models/ctf/resist/tris.md2", "a_resist", "Resist Artifact"},
    {"regen", 8, "regen_rune", "q2:regen_rune", "models/ctf/regen/tris.md2", "a_regen", "Regen Artifact"},
    {"vampire", 16, "vampire_rune", "q2:vampire_rune", "models/ctf/resist/tris.md2", "k_redkey", "Vampire Artifact"},
};

void qa_create_lmctf_rules(qa_lmctf_rules *out) {
  memset(out, 0, sizeof *out);
  out->runes = 15;
  out->rcon_password = qa_q2mp_dup("");
  out->ref_password = qa_q2mp_dup("");
  out->countdown_seconds = 15;
  out->quad_seconds = 30;
}

void qa_lmctf_rules_copy(qa_lmctf_rules *out, const qa_lmctf_rules *in) {
  *out = *in;
  out->rcon_password = qa_q2mp_dup(in->rcon_password);
  out->ref_password = qa_q2mp_dup(in->ref_password);
  out->map_list = qa_q2mp_xalloc(malloc((in->map_count == 0 ? 1 : in->map_count) * sizeof *out->map_list));
  for (size_t i = 0; i < in->map_count; i++) out->map_list[i] = in->map_list[i];
}

void qa_create_lmctf_rules_with(qa_lmctf_rules *out, const qa_lmctf_rules *changes, unsigned mask) {
  qa_create_lmctf_rules(out);
  if (mask & QA_LMCTF_RULES_CTF_FLAGS) out->ctf_flags = changes->ctf_flags;
  if (mask & QA_LMCTF_RULES_REF_FLAGS) out->ref_flags = changes->ref_flags;
  if (mask & QA_LMCTF_RULES_RUNES) out->runes = changes->runes;
  if (mask & QA_LMCTF_RULES_SKIN_SET) out->skin_set = changes->skin_set;
  if (mask & QA_LMCTF_RULES_FLAG_INIT) out->flag_init = changes->flag_init;
  if (mask & QA_LMCTF_RULES_DISABLED_WEAPONS) out->disabled_weapons = changes->disabled_weapons;
  if (mask & QA_LMCTF_RULES_TIME_LIMIT) out->time_limit_minutes = changes->time_limit_minutes;
  if (mask & QA_LMCTF_RULES_FRAG_LIMIT) out->frag_limit = changes->frag_limit;
  if (mask & QA_LMCTF_RULES_MAP_LIST) {
    free(out->map_list);
    out->map_count = changes->map_count;
    out->map_list = qa_q2mp_xalloc(malloc((changes->map_count == 0 ? 1 : changes->map_count) * sizeof *out->map_list));
    for (size_t i = 0; i < changes->map_count; i++) out->map_list[i] = changes->map_list[i];
  }
  if (mask & QA_LMCTF_RULES_RCON_PASSWORD) {
    free(out->rcon_password);
    out->rcon_password = qa_q2mp_dup(changes->rcon_password);
  }
  if (mask & QA_LMCTF_RULES_FAST_SWITCH) out->fast_switch = changes->fast_switch;
  if (mask & QA_LMCTF_RULES_REF_PASSWORD) {
    free(out->ref_password);
    out->ref_password = qa_q2mp_dup(changes->ref_password);
  }
  if (mask & QA_LMCTF_RULES_AUTO_LOCK) out->auto_lock = changes->auto_lock;
  if (mask & QA_LMCTF_RULES_COUNTDOWN) out->countdown_seconds = changes->countdown_seconds;
  if (mask & QA_LMCTF_RULES_QUAD_SECONDS) out->quad_seconds = changes->quad_seconds;
}

void qa_lmctf_rules_destroy(qa_lmctf_rules *rules) {
  free(rules->rcon_password);
  free(rules->ref_password);
  free(rules->map_list);
  rules->rcon_password = NULL;
  rules->ref_password = NULL;
  rules->map_list = NULL;
  rules->map_count = 0;
}

void qa_lmctf_player_state_init(qa_lmctf_player_state *state) {
  memset(state, 0, sizeof *state);
  state->extra_flags = 16 | 32;
}

void qa_lmctf_player_state_destroy(qa_lmctf_player_state *state) {
  qa_lmctf_player_state_clear_stats(state);
  free(state->statistics);
  state->statistics = NULL;
  state->statistic_cap = 0;
}

double qa_lmctf_player_state_stat(const qa_lmctf_player_state *state, const char *name, double fallback) {
  for (size_t i = 0; i < state->statistic_count; i++)
    if (strcmp(state->statistics[i].key, name) == 0) return state->statistics[i].count;
  return fallback;
}

void qa_lmctf_player_state_set_stat(qa_lmctf_player_state *state, const char *name, double count) {
  for (size_t i = 0; i < state->statistic_count; i++)
    if (strcmp(state->statistics[i].key, name) == 0) {
      state->statistics[i].count = count;
      return;
    }
  if (state->statistic_count == state->statistic_cap) {
    state->statistic_cap = state->statistic_cap == 0 ? 8 : state->statistic_cap * 2;
    state->statistics = qa_q2mp_xalloc(realloc(state->statistics, state->statistic_cap * sizeof *state->statistics));
  }
  state->statistics[state->statistic_count].key = qa_q2mp_dup(name);
  state->statistics[state->statistic_count].count = count;
  state->statistic_count++;
}

void qa_lmctf_player_state_clear_stats(qa_lmctf_player_state *state) {
  for (size_t i = 0; i < state->statistic_count; i++) free(state->statistics[i].key);
  state->statistic_count = 0;
}

qa_lmctf_player_state *qa_lmctf_context_get(qa_lmctf_context *context, qa_actor_id actor) {
  for (size_t i = 0; i < context->state_count; i++)
    if (qa_same_actor(context->states[i].actor, actor)) return &context->states[i].state;
  return NULL;
}

qa_lmctf_player_state *qa_lmctf_context_set(qa_lmctf_context *context, qa_actor_id actor, qa_lmctf_player_state state) {
  qa_lmctf_player_state *existing = qa_lmctf_context_get(context, actor);
  if (existing != NULL) {
    qa_lmctf_player_state_destroy(existing);
    *existing = state;
    return existing;
  }
  if (context->state_count == context->state_cap) {
    context->state_cap = context->state_cap == 0 ? 8 : context->state_cap * 2;
    context->states = qa_q2mp_xalloc(realloc(context->states, context->state_cap * sizeof *context->states));
  }
  context->states[context->state_count].actor = actor;
  context->states[context->state_count].state = state;
  return &context->states[context->state_count++].state;
}

void qa_lmctf_context_delete(qa_lmctf_context *context, qa_actor_id actor) {
  for (size_t i = 0; i < context->state_count; i++)
    if (qa_same_actor(context->states[i].actor, actor)) {
      qa_lmctf_player_state_destroy(&context->states[i].state);
      memmove(&context->states[i], &context->states[i + 1], (context->state_count - i - 1) * sizeof *context->states);
      context->state_count--;
      return;
    }
}

void qa_lmctf_context_clear(qa_lmctf_context *context) {
  for (size_t i = 0; i < context->state_count; i++) qa_lmctf_player_state_destroy(&context->states[i].state);
  context->state_count = 0;
}

bool qa_lmctf_context_can_score(qa_lmctf_context *context) { return context->can_score(context->rule_ctx); }
bool qa_lmctf_context_flags_touchable(qa_lmctf_context *context) { return context->flags_touchable(context->rule_ctx); }

qa_lmctf_player_state *qa_lmctf_player(qa_lmctf_context *context, qa_actor_id actor) {
  qa_lmctf_player_state *state = qa_lmctf_context_get(context, actor);
  if (state == NULL) qa_raise("Error", 0, 0, "LMCTF player has not been admitted");
  return state;
}

const char *qa_lmctf_name(qa_lmctf_context *context, qa_actor_id actor) {
  qa_q2_base_player_state *player = context->hooks.player(context->hooks.ctx, actor);
  return player == NULL ? "player" : player->name;
}

void qa_lmctf_print(qa_q2_game_services *game, const qa_actor_id *actor, const char *format, ...) {
  char text[2048];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_q2_presentation_event event = {0};
  event.kind = "print";
  event.has_actor = actor != NULL;
  if (actor != NULL) event.actor = *actor;
  event.level = "high";
  event.text = text;
  game->host->emit(game->host->ctx, event);
}

void qa_lmctf_stat(qa_lmctf_context *context, qa_actor_id actor, const char *name, double amount) {
  qa_lmctf_player_state *state = qa_lmctf_player(context, actor);
  if (qa_lmctf_context_can_score(context))
    qa_lmctf_player_state_set_stat(state, name, qa_lmctf_player_state_stat(state, name, 0) + amount);
}

void qa_lmctf_score(qa_lmctf_context *context, qa_q2_game_services *game, qa_actor_id actor, double amount, const char *name, const qa_actor_id *victim) {
  qa_q2_base_player_state *player = context->hooks.player(context->hooks.ctx, actor);
  if (player == NULL) qa_raise("Error", 0, 0, "LMCTF score requires the shared player state");
  player->score += amount;
  qa_lmctf_stat(context, actor, "score", amount);
  qa_lmctf_event event = {0};
  event.kind = "score-log";
  event.actor = actor;
  event.has_victim = victim != NULL;
  if (victim != NULL) event.victim = *victim;
  event.name = name;
  event.amount = amount;
  event.seconds = qa_q2mp_now(game);
  context->hooks.emit(context->hooks.ctx, event);
}

bool qa_lmctf_active(qa_lmctf_context *context, qa_q2_game_services *game, qa_actor_id actor) {
  qa_lmctf_player_state *state = qa_lmctf_context_get(context, actor);
  qa_q2_base_player_state *player = context->hooks.player(context->hooks.ctx, actor);
  return state != NULL && player != NULL && player->connected && !player->spectator &&
         (state->team != 0 || game->options.mode != QA_Q2_MODE_DEATHMATCH) &&
         qa_session_actor_registry_is_live(game->host->actors, actor);
}

void qa_lmctf_toss(qa_q2_entity *entity, qa_q2_entity *player, qa_q2_game_services *game, qa_vec3 forward) {
  qa_body_state body = qa_q2_game_body(game, player);
  qa_vec3 end = {body.origin.x + forward.x * 24, body.origin.y + forward.y * 24, body.origin.z + forward.z * 24 - 16};
  qa_bounds bounds = qa_q2_game_body(game, entity).bounds;
  qa_trace_result trace = qa_q2mp_trace(game, body.origin, end, &bounds, &player->actor.id, 1);
  qa_q2_body_changes move = {0};
  move.has_origin = true;
  move.origin = trace.end;
  move.has_velocity = true;
  move.velocity = (qa_vec3){forward.x * 200, forward.y * 200, 300};
  qa_q2_game_move(game, entity, move, true);
}

void qa_lmctf_travel_destroy(qa_lmctf_travel *travel) {
  qa_lmctf_rules_destroy(&travel->rules);
  free(travel->players);
  travel->players = NULL;
  travel->player_count = 0;
}
