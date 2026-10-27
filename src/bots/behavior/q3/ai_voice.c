/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_voice.h"
#include <string.h>
static bool is(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
bool qa_bot_voice_chat_command_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                      int64_t mode, const char *v, qa_bot_voice_order_fn on_order,
                                      void *hc) {
  (void)mode;
  static const char *known[] = {"getflag",       "offense",       "defend",     "defendflag",
                                "patrol",        "camp",          "followme",   "followflagcarrier",
                                "returnflag",    "startleader",   "stopleader", "whoisleader",
                                "wantondefense", "wantonoffense", NULL};
  for (int i = 0; known[i]; i++)
    if (is(v, known[i])) {
      if (on_order)
        on_order(hc, c, s, s->client, v);
      return true;
    }
  return false;
}
void qa_bot_voice_chat_defend_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                     int64_t client, int64_t mode) {
  (void)mode;
  if (c->say_team)
    c->say_team(c->host_ctx, client, "defend");
  (void)s;
}
void qa_bot_voice_chat_get_flag_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                       int64_t client, int64_t mode) {
  (void)mode;
  if (c->say_team)
    c->say_team(c->host_ctx, client, "getflag");
  (void)s;
}
