/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_orders.h"
#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include <stdio.h>
#include <string.h>
void qa_bot_clean_string_bots_a(const char *t, char *out, size_t cap) {
  size_t w = 0;
  for (size_t i = 0; t[i] && w + 1 < cap; i++) {
    int code = (uint8_t)t[i];
    if (code == 94 && t[i + 1] && (uint8_t)t[i + 1] != 94) {
      i++;
      continue;
    }
    if (code >= 32 && code <= 126)
      out[w++] = t[i];
  }
  if (cap)
    out[w] = '\0';
}
static int up(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
const char *qa_stristr_bots_a(const char *text, const char *part) {
  size_t tl = strlen(text), pl = strlen(part);
  if (pl == 0)
    return text;
  for (size_t i = 0; i + pl <= tl; i++) {
    size_t j = 0;
    for (; j < pl; j++)
      if (up((uint8_t)text[i + j]) != up((uint8_t)part[j]))
        break;
    if (j == pl)
      return text + i;
  }
  return NULL;
}
/* GameType GT_TEAM = 3 (owner definitions.ts). Team: FREE=0 RED=1 BLUE=2 SPEC=3. */
bool qa_team_play_is_on_bots_a(qa_game_ai_context_bots_a *c) { return c->gametype >= 3; }
int64_t qa_bot_team_bots_a(qa_bot_state_bots_a *s, int64_t v) {
  (void)s;
  return v == 1 ? 1 : v == 2 ? 2 : 0;
}
int64_t qa_bot_opposite_team_bots_a(int64_t t) { return t == 1 ? 2 : t == 2 ? 1 : 0; }
bool qa_bot_same_team_bots_a(qa_game_ai_context_bots_a *c, int64_t a, int64_t b, int64_t entity) {
  if (entity < 0 || entity >= 64 || !qa_team_play_is_on_bots_a(c))
    return false;
  return a == b;
}
int64_t qa_bot_ctf_carrying_flag_bots_a(qa_bot_state_bots_a *s) {
  if (qa_bot_inventory_value_bots_a(s, QA_INV_REDFLAG) > 0)
    return QA_CTFFLAG_RED;
  if (qa_bot_inventory_value_bots_a(s, QA_INV_BLUEFLAG) > 0)
    return QA_CTFFLAG_BLUE;
  return QA_CTFFLAG_NONE;
}
bool qa_bot_1fctf_carrying_flag_bots_a(qa_bot_state_bots_a *s) {
  return qa_bot_inventory_value_bots_a(s, QA_INV_NEUTRALFLAG) > 0;
}
bool qa_bot_harvester_carrying_cubes_bots_a(qa_bot_state_bots_a *s, int64_t team) {
  return qa_bot_inventory_value_bots_a(s, team == 1 ? QA_INV_REDCUBE : QA_INV_BLUECUBE) > 0;
}
void qa_bot_team_goals_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s, bool retreat) {
  (void)c;
  (void)s;
  (void)retreat;
}
void qa_bot_remember_last_ordered_task_bots_a(qa_bot_state_bots_a *s, const char *task) {
  snprintf(s->last_ordered_task, sizeof s->last_ordered_task, "%s", task ? task : "");
}
