/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/weapons.h"
#include <stdlib.h>
#include <string.h>
void qa_weapon_ai_bots_a_init(qa_weapon_ai_bots_a *w) { memset(w, 0, sizeof *w); }
void qa_weapon_ai_bots_a_destroy(qa_weapon_ai_bots_a *w) {
  free(w->config.weapons);
  free(w->config.projectiles);
}
int64_t qa_weapon_ai_allocate(qa_weapon_ai_bots_a *w) {
  for (int i = 1; i <= QA_MAX_WEAPON_STATES; i++)
    if (!w->states[i]) {
      w->states[i] = 1;
      return i;
    }
  return 0;
}
void qa_weapon_ai_free(qa_weapon_ai_bots_a *w, int64_t h) {
  if (h >= 1 && h <= QA_MAX_WEAPON_STATES)
    w->states[h] = 0;
}
bool qa_weapon_ai_set_config(qa_weapon_ai_bots_a *w, const qa_weapon_info_bots_a *weapons,
                             size_t n) {
  free(w->config.weapons);
  w->config.weapons = NULL;
  if (n) {
    w->config.weapons = malloc(n * sizeof *weapons);
    if (!w->config.weapons)
      return false;
    memcpy(w->config.weapons, weapons, n * sizeof *weapons);
  }
  w->config.weapon_count = n;
  w->has_config = true;
  return true;
}
const qa_weapon_info_bots_a *qa_weapon_ai_get_weapon_info(qa_weapon_ai_bots_a *w, int64_t h,
                                                          int64_t weapon) {
  (void)h;
  for (size_t i = 0; i < w->config.weapon_count; i++)
    if (w->config.weapons[i].number == weapon)
      return &w->config.weapons[i];
  return NULL;
}
