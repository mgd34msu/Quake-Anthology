/* Port of src/content/q2/multiplayer/lmctf/match.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/lmctf/match.h"
#include "../multiplayer_internal.h"

struct qa_lmctf_match {
  qa_lmctf_context *context;
  const char *phase;
  bool has_pending;
  qa_lmctf_map_change pending;
  double remaining, next_think;
  bool paused, teams_locked;
};

qa_lmctf_match *qa_lmctf_match_create(qa_lmctf_context *context) {
  qa_lmctf_match *m = calloc(1, sizeof *m);
  if (m == NULL) return NULL;
  m->context = context;
  m->phase = "none";
  return m;
}
void qa_lmctf_match_destroy(qa_lmctf_match *m) {
  if (m == NULL) return;
  free(m->pending.map);
  free(m);
}
const char *qa_lmctf_match_phase(qa_lmctf_match *m) { return m->phase; }
void qa_lmctf_match_set_phase(qa_lmctf_match *m, const char *phase) { m->phase = phase; }
const qa_lmctf_map_change *qa_lmctf_match_pending_map(qa_lmctf_match *m) { return m->has_pending ? &m->pending : NULL; }
double qa_lmctf_match_remaining(qa_lmctf_match *m) { return m->remaining; }
double qa_lmctf_match_next_think(qa_lmctf_match *m) { return m->next_think; }
bool qa_lmctf_match_paused(qa_lmctf_match *m) { return m->paused; }
void qa_lmctf_match_set_paused(qa_lmctf_match *m, bool paused) { m->paused = paused; }
bool qa_lmctf_match_teams_locked(qa_lmctf_match *m) { return m->teams_locked; }
void qa_lmctf_match_set_teams_locked(qa_lmctf_match *m, bool locked) { m->teams_locked = locked; }

void qa_lmctf_match_toggle_pause(qa_lmctf_match *m, qa_q2_game_services *g) {
  m->paused = !m->paused;
  if (m->context->rules->auto_lock) m->teams_locked = !m->paused;
  const char *text = m->paused ? "Game Paused\n" : "Game Unpaused\n";
  for (size_t i = 0; i < m->context->state_count; i++) {
    qa_actor_id actor = m->context->states[i].actor;
    if (qa_q2_game_entity_of(g, &actor) != NULL) {
      qa_q2_presentation_event event = {0};
      event.kind = "centerprint";
      event.has_actor = true;
      event.actor = actor;
      event.text = text;
      g->host->emit(g->host->ctx, event);
    }
  }
  g->host->diagnostic(g->host->ctx, text);
}

void qa_lmctf_match_change_map(qa_lmctf_match *m, const char *map, bool countdown) {
  qa_lmctf_match_stop(m);
  free(m->pending.map);
  m->pending.map = qa_q2mp_dup(map);
  m->pending.countdown = countdown;
  m->has_pending = true;
  if (countdown) m->phase = "countdown";
  for (size_t i = 0; i < m->context->state_count; i++) {
    qa_lmctf_player_state *state = &m->context->states[i].state;
    qa_lmctf_player_state_clear_stats(state);
    qa_q2_base_player_state *player = m->context->hooks.player(m->context->hooks.ctx, m->context->states[i].actor);
    if (player != NULL) player->score = 0;
  }
}

bool qa_lmctf_match_can_score(qa_lmctf_match *m) {
  return strcmp(m->phase, "countdown") != 0 && strcmp(m->phase, "over") != 0;
}

void qa_lmctf_match_start(qa_lmctf_match *m, qa_q2_game_services *g) {
  m->phase = "countdown";
  m->remaining = trunc(m->context->rules->countdown_seconds);
  m->next_think = qa_q2mp_now(g) + 1;
  if (m->context->rules->auto_lock) m->teams_locked = true;
}

void qa_lmctf_match_stop(qa_lmctf_match *m) {
  m->phase = "none";
  if (m->context->rules->auto_lock) m->teams_locked = false;
}

void qa_lmctf_match_frame(qa_lmctf_match *m, qa_q2_game_services *g) {
  if (m->has_pending || strcmp(m->phase, "none") == 0 || qa_q2mp_now(g) < m->next_think) return;
  m->next_think = qa_q2mp_now(g) + 1;
  if (m->paused) return;
  if (strcmp(m->phase, "countdown") == 0) {
    if (m->remaining == 60 || m->remaining == 30 || m->remaining == 15 || m->remaining == 10) {
      char remaining[32];
      qa_q2mp_format_number(remaining, sizeof remaining, m->remaining);
      qa_lmctf_print(g, NULL, "%s seconds until match begins.\n", remaining);
    }
    if (m->remaining <= 0) {
      for (size_t i = 0; i < m->context->state_count; i++) {
        qa_actor_id actor = m->context->states[i].actor;
        qa_lmctf_player_state *state = &m->context->states[i].state;
        qa_q2_base_player_state *player = m->context->hooks.player(m->context->hooks.ctx, actor);
        qa_q2_entity *entity = qa_q2_game_entity_of(g, &actor);
        if (player == NULL || player->spectator || entity == NULL) continue;
        g->damage(g->ctx, actor, entity, actor, &actor, 100000, 0, QA_Q2_ZERO, qa_q2_game_body(g, entity).origin, QA_Q2_ZERO, 23, 32, NULL);
        qa_lmctf_player_state_clear_stats(state);
        player->score = 0;
        state->spawn_state = 0;
      }
      m->phase = "inplay";
      m->remaining = trunc(m->context->rules->time_limit_minutes) * 60;
      char minutes[32];
      qa_q2mp_format_number(minutes, sizeof minutes, m->remaining / 60);
      qa_lmctf_print(g, NULL, "%s minutes until match ends.\n", minutes);
    }
  } else if (strcmp(m->phase, "inplay") == 0) {
    if (m->remaining > 10 && m->context->rules->frag_limit != 0) {
      for (size_t i = 0; i < m->context->state_count; i++)
        if (qa_lmctf_player_state_stat(&m->context->states[i].state, "score", 0) >= m->context->rules->frag_limit) m->remaining = 10;
    }
    if (m->remaining <= 0) {
      double red = 0, blue = 0;
      for (size_t i = 0; i < m->context->state_count; i++) {
        qa_lmctf_player_state *state = &m->context->states[i].state;
        if (state->team == 1) red += qa_lmctf_player_state_stat(state, "score", 0);
        else if (state->team == 2) blue += qa_lmctf_player_state_stat(state, "score", 0);
      }
      char red_text[32], blue_text[32];
      qa_q2mp_format_number(red_text, sizeof red_text, red);
      qa_q2mp_format_number(blue_text, sizeof blue_text, blue);
      if (red > blue) qa_lmctf_print(g, NULL, "Red: %s beats blue: %s!\n", red_text, blue_text);
      else if (blue > red) qa_lmctf_print(g, NULL, "Blue: %s beats red: %s!\n", blue_text, red_text);
      else qa_lmctf_print(g, NULL, "Tie game at %s!\n", red_text);
      m->phase = "over";
      m->remaining = 300;
      m->teams_locked = false;
      return;
    }
  } else if (m->remaining <= 0) {
    m->phase = "none";
  }
  m->remaining--;
}

qa_lmctf_match_checkpoint qa_lmctf_match_capture(qa_lmctf_match *m) {
  qa_lmctf_match_checkpoint out = {0};
  out.has_pending_map = m->has_pending;
  out.pending_map = m->pending;
  out.phase = m->phase;
  out.remaining = m->remaining;
  out.next_think = m->next_think;
  out.paused = m->paused;
  out.teams_locked = m->teams_locked;
  return out;
}

void qa_lmctf_match_restore(qa_lmctf_match *m, const qa_save_reader *reader) {
  qa_save_reader pending;
  if (!qa_save_reader_field(reader, "pendingMap", &pending)) qa_raise("Error", 0, 0, "Out of memory");
  free(m->pending.map);
  m->pending.map = NULL;
  if (qa_save_reader_is_undefined(&pending) || qa_save_reader_is_null(&pending)) {
    m->has_pending = false;
  } else {
    qa_save_reader field;
    m->has_pending = true;
    if (!qa_save_reader_field(&pending, "map", &field)) qa_raise("Error", 0, 0, "Out of memory");
    m->pending.map = qa_q2mp_dup(qa_save_reader_string(&field));
    qa_save_reader_destroy(&field);
    if (!qa_save_reader_field(&pending, "countdown", &field)) qa_raise("Error", 0, 0, "Out of memory");
    m->pending.countdown = qa_save_reader_boolean(&field);
    qa_save_reader_destroy(&field);
  }
  qa_save_reader_destroy(&pending);
  qa_save_reader field;
  static const char *const phases[4] = {"none", "countdown", "inplay", "over"};
  if (!qa_save_reader_field(reader, "phase", &field)) qa_raise("Error", 0, 0, "Out of memory");
  const char *phase = qa_save_reader_choice_string(&field, phases, 4);
  for (int i = 0; i < 4; i++)
    if (strcmp(phase, phases[i]) == 0) m->phase = phases[i];
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "remaining", &field)) qa_raise("Error", 0, 0, "Out of memory");
  m->remaining = qa_save_reader_integer(&field);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "nextThink", &field)) qa_raise("Error", 0, 0, "Out of memory");
  m->next_think = qa_save_reader_finite(&field);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "paused", &field)) qa_raise("Error", 0, 0, "Out of memory");
  m->paused = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "teamsLocked", &field)) qa_raise("Error", 0, 0, "Out of memory");
  m->teams_locked = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
}
