/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_chat.h"
int64_t qa_bot_num_active_players_bots_a(qa_game_ai_context_bots_a *c, const int64_t *scores,
                                         const bool *conn, size_t n) {
  (void)c;
  (void)scores;
  int64_t k = 0;
  for (size_t i = 0; i < n; i++)
    if (conn[i])
      k++;
  return k;
}
bool qa_bot_is_first_in_rankings_bots_a(qa_bot_state_bots_a *s, const int64_t *scores, size_t n) {
  if (s->client < 0 || (size_t)s->client >= n)
    return false;
  for (size_t i = 0; i < n; i++)
    if ((int64_t)i != s->client && scores[i] > scores[s->client])
      return false;
  return true;
}
bool qa_bot_is_last_in_rankings_bots_a(qa_bot_state_bots_a *s, const int64_t *scores, size_t n) {
  if (s->client < 0 || (size_t)s->client >= n)
    return false;
  for (size_t i = 0; i < n; i++)
    if ((int64_t)i != s->client && scores[i] < scores[s->client])
      return false;
  return true;
}
bool qa_bot_valid_chat_position_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                       bool vis) {
  (void)c;
  (void)s;
  return !vis;
}
double qa_bot_chat_time_bots_a(qa_bot_state_bots_a *s) { return s->last_chat_time; }
void qa_bot_initial_chat_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                const char *t) {
  (void)c;
  (void)s;
  (void)t;
}
bool qa_bot_chat_enter_game_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s) {
  (void)c;
  if (!s->enter_game_chat)
    return false;
  s->enter_game_chat = false;
  return true;
}
bool qa_bot_chat_exit_game_bots_a(qa_bot_state_bots_a *s) {
  (void)s;
  return false;
}
bool qa_bot_chat_start_level_bots_a(qa_bot_state_bots_a *s) {
  (void)s;
  return false;
}
bool qa_bot_chat_end_level_bots_a(qa_bot_state_bots_a *s) {
  (void)s;
  return false;
}
bool qa_bot_chat_death_bots_a(qa_bot_state_bots_a *s) {
  (void)s;
  return false;
}
bool qa_bot_chat_kill_bots_a(qa_bot_state_bots_a *s) {
  (void)s;
  return false;
}
bool qa_bot_chat_enemy_suicide_bots_a(qa_bot_state_bots_a *s) { return s->enemy_suicide; }
bool qa_bot_chat_hit_talking_bots_a(qa_bot_state_bots_a *s) {
  (void)s;
  return false;
}
bool qa_bot_chat_hit_no_death_bots_a(qa_bot_state_bots_a *s) {
  (void)s;
  return false;
}
bool qa_bot_chat_hit_no_kill_bots_a(qa_bot_state_bots_a *s) {
  (void)s;
  return false;
}
bool qa_bot_chat_random_bots_a(qa_bot_state_bots_a *s) {
  (void)s;
  return false;
}
void qa_bot_chat_test_bots_a(qa_bot_state_bots_a *s) { (void)s; }
