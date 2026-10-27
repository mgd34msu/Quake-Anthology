/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_team.h"
#include <string.h>
bool qa_bot_valid_team_leader_bots_a(qa_bot_state_bots_a *s) { return s->team_leader[0] != '\0'; }
int64_t qa_bot_num_teammates_bots_a(const bool *same, size_t n, int64_t self) {
  int64_t k = 0;
  for (size_t i = 0; i < n; i++)
    if ((int64_t)i != self && same[i])
      k++;
  return k;
}
void qa_bot_set_teammate_task_preference_bots_a(qa_game_ai_team_state_bots_a *t, int64_t tm,
                                                int64_t pref) {
  if (tm >= 0 && tm < 64)
    t->task_preferences[tm] = pref;
}
int64_t qa_bot_get_teammate_task_preference_bots_a(qa_game_ai_team_state_bots_a *t, int64_t tm) {
  return (tm >= 0 && tm < 64) ? t->task_preferences[tm] : 0;
}
void qa_bot_voice_chat_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s, int64_t client,
                              const char *voice) {
  (void)client;
  if (c->say_team)
    c->say_team(c->host_ctx, s->client, voice ? voice : "");
}
void qa_bot_voice_chat_only_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                   int64_t client, const char *voice) {
  qa_bot_voice_chat_bots_a(c, s, client, voice);
}
void qa_bot_team_ai_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s) {
  (void)c;
  (void)s;
}
