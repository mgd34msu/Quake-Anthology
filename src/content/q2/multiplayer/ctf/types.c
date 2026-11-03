/* Port of src/content/q2/multiplayer/ctf/types.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/ctf/types.h"
#include "../multiplayer_internal.h"
#include <stdarg.h>

static const char *const DEFAULT_WARP[] = {"q2ctf1", "q2ctf2", "q2ctf3", "q2ctf4", "q2ctf5"};

void qa_create_q2_ctf_rules(qa_q2_ctf_rules *out) {
  memset(out, 0, sizeof *out);
  out->force_join = "";
  out->competition = 0;
  out->match_lock = true;
  out->election_percentage = 66;
  out->match_minutes = 20;
  out->setup_minutes = 10;
  out->start_seconds = 20;
  out->admin_password = qa_q2mp_dup("");
  out->warp_count = 5;
  out->warp_list = qa_q2mp_xalloc(malloc(5 * sizeof *out->warp_list));
  for (size_t i = 0; i < 5; i++) out->warp_list[i] = DEFAULT_WARP[i];
  out->capture_limit = 0;
  out->instant_weapons = false;
}

void qa_create_q2_ctf_rules_with(qa_q2_ctf_rules *out, const qa_q2_ctf_rules *changes, unsigned mask) {
  qa_create_q2_ctf_rules(out);
  if (mask & QA_Q2_CTF_RULES_FORCE_JOIN) out->force_join = changes->force_join;
  if (mask & QA_Q2_CTF_RULES_COMPETITION) out->competition = changes->competition;
  if (mask & QA_Q2_CTF_RULES_MATCH_LOCK) out->match_lock = changes->match_lock;
  if (mask & QA_Q2_CTF_RULES_ELECTION_PERCENTAGE) out->election_percentage = changes->election_percentage;
  if (mask & QA_Q2_CTF_RULES_MATCH_MINUTES) out->match_minutes = changes->match_minutes;
  if (mask & QA_Q2_CTF_RULES_SETUP_MINUTES) out->setup_minutes = changes->setup_minutes;
  if (mask & QA_Q2_CTF_RULES_START_SECONDS) out->start_seconds = changes->start_seconds;
  if (mask & QA_Q2_CTF_RULES_ADMIN_PASSWORD) {
    free(out->admin_password);
    out->admin_password = qa_q2mp_dup(changes->admin_password);
  }
  if (mask & QA_Q2_CTF_RULES_WARP_LIST) {
    free(out->warp_list);
    out->warp_count = changes->warp_count;
    out->warp_list = qa_q2mp_xalloc(malloc((changes->warp_count == 0 ? 1 : changes->warp_count) * sizeof *out->warp_list));
    for (size_t i = 0; i < changes->warp_count; i++) out->warp_list[i] = changes->warp_list[i];
  }
  if (mask & QA_Q2_CTF_RULES_CAPTURE_LIMIT) out->capture_limit = changes->capture_limit;
  if (mask & QA_Q2_CTF_RULES_INSTANT_WEAPONS) out->instant_weapons = changes->instant_weapons;
}

void qa_q2_ctf_rules_destroy(qa_q2_ctf_rules *rules) {
  free(rules->admin_password);
  free(rules->warp_list);
  rules->admin_password = NULL;
  rules->warp_list = NULL;
  rules->warp_count = 0;
}

void qa_q2_ctf_player_state_init(qa_q2_ctf_player_state *state) {
  memset(state, 0, sizeof *state);
  state->team = 0;
  state->id_view = true;
}

void qa_q2_ctf_match_state_init(qa_q2_ctf_match_state *state) {
  memset(state, 0, sizeof *state);
  state->phase = "none";
  state->last_time = -1;
}

static void free_election(qa_q2_ctf_election *e) {
  if (e == NULL) return;
  free(e->map);
  free(e->message);
  free(e);
}

void qa_q2_ctf_match_state_destroy(qa_q2_ctf_match_state *state) {
  qa_q2_ctf_match_state_clear_ghosts(state);
  free(state->ghosts);
  state->ghosts = NULL;
  state->ghost_cap = 0;
  free_election(state->election);
  state->election = NULL;
}

qa_q2_ctf_ghost *qa_q2_ctf_match_state_ghost(qa_q2_ctf_match_state *state, double code) {
  for (size_t i = 0; i < state->ghost_count; i++)
    if (state->ghosts[i].code == code) return &state->ghosts[i];
  return NULL;
}

void qa_q2_ctf_match_state_set_ghost(qa_q2_ctf_match_state *state, qa_q2_ctf_ghost ghost) {
  qa_q2_ctf_ghost *existing = qa_q2_ctf_match_state_ghost(state, ghost.code);
  if (existing != NULL) {
    free(existing->name);
    *existing = ghost;
    return;
  }
  if (state->ghost_count == state->ghost_cap) {
    state->ghost_cap = state->ghost_cap == 0 ? 8 : state->ghost_cap * 2;
    state->ghosts = qa_q2mp_xalloc(realloc(state->ghosts, state->ghost_cap * sizeof *state->ghosts));
  }
  state->ghosts[state->ghost_count++] = ghost;
}

void qa_q2_ctf_match_state_delete_ghost(qa_q2_ctf_match_state *state, double code) {
  for (size_t i = 0; i < state->ghost_count; i++)
    if (state->ghosts[i].code == code) {
      free(state->ghosts[i].name);
      memmove(&state->ghosts[i], &state->ghosts[i + 1], (state->ghost_count - i - 1) * sizeof *state->ghosts);
      state->ghost_count--;
      return;
    }
}

void qa_q2_ctf_match_state_clear_ghosts(qa_q2_ctf_match_state *state) {
  for (size_t i = 0; i < state->ghost_count; i++) free(state->ghosts[i].name);
  state->ghost_count = 0;
}

void qa_q2_ctf_match_state_set_election(qa_q2_ctf_match_state *state, qa_q2_ctf_election *election) {
  free_election(state->election);
  state->election = election;
}

qa_q2_ctf_player_state *qa_q2_ctf_context_get(qa_q2_ctf_context *context, qa_actor_id actor) {
  for (size_t i = 0; i < context->state_count; i++)
    if (qa_same_actor(context->states[i].actor, actor)) return &context->states[i].state;
  return NULL;
}

qa_q2_ctf_player_state *qa_q2_ctf_context_set(qa_q2_ctf_context *context, qa_actor_id actor, qa_q2_ctf_player_state state) {
  qa_q2_ctf_player_state *existing = qa_q2_ctf_context_get(context, actor);
  if (existing != NULL) {
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

void qa_q2_ctf_context_delete(qa_q2_ctf_context *context, qa_actor_id actor) {
  for (size_t i = 0; i < context->state_count; i++)
    if (qa_same_actor(context->states[i].actor, actor)) {
      memmove(&context->states[i], &context->states[i + 1], (context->state_count - i - 1) * sizeof *context->states);
      context->state_count--;
      return;
    }
}

void qa_q2_ctf_context_clear(qa_q2_ctf_context *context) { context->state_count = 0; }

const qa_q2_ctf_flag_def QA_CTF_FLAGS[3] = {
    {NULL, NULL, NULL, NULL, 0},
    {"item_flag_team1", "q2:item_flag_team1", "players/male/flag1.md2", "i_ctf1", 0x40000},
    {"item_flag_team2", "q2:item_flag_team2", "players/male/flag2.md2", "i_ctf2", 0x80000},
};

qa_q2_ctf_playing_team qa_other_ctf_team(qa_q2_ctf_playing_team team) { return team == 1 ? 2 : 1; }
const char *qa_ctf_team_name(qa_q2_ctf_team team) { return team == 1 ? "RED" : team == 2 ? "BLUE" : "UNKNOWN"; }

qa_q2_ctf_player_state *qa_ctf_player(qa_q2_ctf_context *context, qa_actor_id actor) {
  qa_q2_ctf_player_state *state = qa_q2_ctf_context_get(context, actor);
  if (state == NULL) qa_raise("Error", 0, 0, "CTF player has not been admitted to the shared match");
  return state;
}

void qa_ctf_print(qa_q2_game_services *game, const qa_actor_id *actor, const char *level, const char *format, ...) {
  char text[2048];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_q2_presentation_event event = {0};
  event.kind = "print";
  event.has_actor = actor != NULL;
  if (actor != NULL) event.actor = *actor;
  event.level = level == NULL ? "high" : level;
  event.text = text;
  game->host->emit(game->host->ctx, event);
}

const char *qa_ctf_name(qa_q2_ctf_context *context, qa_actor_id actor) {
  qa_q2_base_player_state *player = context->hooks.player(context->hooks.ctx, actor);
  return player == NULL ? "player" : player->name;
}

void qa_ctf_score(qa_q2_ctf_context *context, qa_actor_id actor, double amount) {
  qa_q2_base_player_state *player = context->hooks.player(context->hooks.ctx, actor);
  if (player == NULL) qa_raise("Error", 0, 0, "CTF score requires the shared player state");
  player->score += amount;
}

qa_saved_actor_id qa_save_ctf_actor(qa_actor_id actor) {
  qa_saved_actor_id saved = {(double)actor.slot, (double)actor.generation};
  return saved;
}

bool qa_ctf_carried_flag(qa_q2_game_services *game, qa_actor_id actor, qa_q2_ctf_playing_team *out) {
  if (qa_q2mp_inv_count(game, actor, QA_CTF_FLAGS[1].item) > 0) { *out = 1; return true; }
  if (qa_q2mp_inv_count(game, actor, QA_CTF_FLAGS[2].item) > 0) { *out = 2; return true; }
  return false;
}
