/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_main.h"
#include <string.h>
void qa_remove_color_escape_sequences_bots_a(const char *t, char *out, size_t cap) {
  size_t w = 0;
  for (size_t i = 0; t[i] && w + 1 < cap; i++) {
    int ch = (uint8_t)t[i];
    int next = t[i + 1] ? (uint8_t)t[i + 1] : 0;
    if (ch == 94 && next != 0 && next != 94) {
      i++;
      continue;
    }
    if (ch < 128 && ch > 0x7e)
      continue;
    out[w++] = t[i];
  }
  if (cap)
    out[w] = '\0';
}
static bool ascii_equal_ci(const char *a, const char *b) {
  while (*a && *b) {
    int x = (uint8_t)*a, y = (uint8_t)*b;
    if (x >= 'A' && x <= 'Z')
      x += 32;
    if (y >= 'A' && y <= 'Z')
      y += 32;
    if (x != y)
      return false;
    a++;
    b++;
  }
  return *a == *b;
}
bool qa_bot_team_leader_bots_a(qa_bot_state_bots_a *s, const char *name) {
  return name && ascii_equal_ci(name, s->team_leader);
}
void qa_game_ai_bots_a_init(qa_game_ai_bots_a *g, qa_game_ai_context_bots_a *c) {
  g->context = c;
  g->setup_done = false;
}
void qa_game_ai_frame_bots_a(qa_game_ai_bots_a *g, double time) {
  if (g->context)
    g->context->time = time;
}
