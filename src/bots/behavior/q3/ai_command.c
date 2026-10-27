/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_command.h"
#include "qa/bots/behavior/library/chat.h"
#include <stdio.h>
#include <string.h>
qa_vec3 qa_bot_gps_to_position_bots_a(qa_game_ai_context_bots_a *ctx, const char *text) {
  size_t offset = 0;
  double coords[3];
  for (int k = 0; k < 3; k++) {
    int32_t number = 0;
    while (text[offset] == ' ')
      offset++;
    int sign = text[offset] == '-' ? -1 : 1;
    if (sign == -1)
      offset++;
    while (text[offset]) {
      char ch = text[offset++];
      if (ch < '0' || ch > '9')
        break;
      number = (int32_t)((uint32_t)number * 10u + (uint32_t)(ch - 48));
    }
    int32_t value = (int32_t)((uint32_t)sign * (uint32_t)number);
    if (ctx->print) {
      char b[32];
      snprintf(b, sizeof b, "%d\n", value);
      ctx->print(ctx->host_ctx, b);
    }
    coords[k] = (float)value;
  }
  return (qa_vec3){coords[0], coords[1], coords[2]};
}
static bool fold_eq(const char *a, const char *b) {
  while (*a && *b) {
    int x = (uint8_t)*a, y = (uint8_t)*b;
    if (x >= 'a' && x <= 'z')
      x -= 32;
    if (y >= 'a' && y <= 'z')
      y -= 32;
    if (x != y)
      return false;
    a++;
    b++;
  }
  return *a == *b;
}
int64_t qa_find_client_by_name_bots_a(const char *const *names, size_t n, const char *name) {
  for (size_t i = 0; i < n; i++)
    if (fold_eq(names[i], name))
      return (int64_t)i;
  for (size_t i = 0; i < n; i++)
    if (qa_string_contains_bots_a(names[i], name, false) >= 0)
      return (int64_t)i;
  return -1;
}
bool qa_bot_match_message_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                 const char *m,
                                 bool (*dispatch)(void *, qa_game_ai_context_bots_a *,
                                                  qa_bot_state_bots_a *, const char *),
                                 void *hc) {
  return dispatch ? dispatch(hc, c, s, m) : false;
}
void qa_bot_print_team_goal_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s) {
  (void)c;
  (void)s;
}
