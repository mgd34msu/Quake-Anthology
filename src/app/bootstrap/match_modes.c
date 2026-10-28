/* Port of src/app/bootstrap/match-modes.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/match_modes.h"
#include <stdio.h>
#include <string.h>
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static bool has(const char *const *names, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++) if (names[i] != NULL && strcmp(names[i], name) == 0) return true;
  return false;
}
const char *qa_app_match_mode_unavailable(qa_app_match_mode_selection selection) {
  if (is(selection.rules, "standard")) return NULL;
  if (is(selection.rules, "horde"))
    return selection.family == QA_GAME_FAMILY_Q3 ? NULL
        : is(selection.mode, "deathmatch") ? "Horde requires single player or cooperative mode" : NULL;
  if (is(selection.rules, "tag") || is(selection.rules, "deathball")) return NULL;
  if ((is(selection.rules, "ctf") || is(selection.rules, "lmctf"))
      && (selection.family == QA_GAME_FAMILY_Q3 || selection.family == QA_GAME_FAMILY_Q1)) return NULL;
  if (selection.family != QA_GAME_FAMILY_Q2) return "These match rules require a Quake II source game";
  return NULL;
}
static const char *missing_join(char *scratch, size_t size, const char *prefix,
    const char *const *required, size_t req_count, const char *const *names, size_t count) {
  size_t at = 0; bool any = false;
  int written = snprintf(scratch, size, "%s", prefix);
  at = written < 0 ? 0 : (size_t)written;
  for (size_t i = 0; i < req_count; i++) {
    if (has(names, count, required[i])) continue;
    written = snprintf(scratch + at, at < size ? size - at : 0, "%s%s", any ? ", " : "", required[i]);
    if (written > 0) at += (size_t)written;
    any = true;
  }
  return any ? scratch : NULL;
}
const char *qa_app_match_map_unavailable(qa_app_match_mode_selection selection,
    const char *const *classnames, size_t classname_count, char *scratch, size_t scratch_size) {
  const char *unavailable = qa_app_match_mode_unavailable(selection);
  if (unavailable != NULL) return unavailable;
  if (is(selection.rules, "deathball") && selection.family != QA_GAME_FAMILY_Q2) {
    if (!has(classnames, classname_count, "info_player_deathmatch"))
      return "DeathBall map is missing: info_player_deathmatch";
  } else if (is(selection.rules, "deathball")) {
    static const char *const required[] = {"dm_dball_ball", "dm_dball_ball_start", "dm_dball_goal",
        "dm_dball_team1_start", "dm_dball_team2_start"};
    const char *r = missing_join(scratch, scratch_size, "DeathBall map is missing: ", required, 5, classnames, classname_count);
    if (r != NULL) return r;
  }
  if (is(selection.rules, "horde") && selection.family != QA_GAME_FAMILY_Q3) {
    bool monster = false;
    for (size_t i = 0; i < classname_count; i++)
      if (classnames[i] != NULL && strncmp(classnames[i], "info_monster_start", 18) == 0) monster = true;
    if (!has(classnames, classname_count, "horde_manager") || !monster)
      return "Horde requires an authored horde_manager and monster spawn points";
  }
  if (is(selection.rules, "lmctf") && selection.family != QA_GAME_FAMILY_Q2) {
    if (!has(classnames, classname_count, "info_player_deathmatch"))
      return "LMCTF map is missing: info_player_deathmatch";
  } else if (is(selection.rules, "ctf") || is(selection.rules, "lmctf")) {
    static const char *const q3flags[] = {"team_CTF_redflag", "team_CTF_blueflag"};
    static const char *const flags[] = {"item_flag_team1", "item_flag_team2"};
    const char *const *use = selection.family == QA_GAME_FAMILY_Q3 ? q3flags : flags;
    char tmp[256];
    const char *r = missing_join(tmp, sizeof tmp, "CTF map is missing: ", use, 2, classnames, classname_count);
    if (r != NULL && !has(classnames, classname_count, "info_player_deathmatch")) {
      snprintf(scratch, scratch_size, "%s", tmp);
      return scratch;
    }
  }
  if (is(selection.mode, "deathmatch") && !is(selection.rules, "deathball")
      && !has(classnames, classname_count, "info_player_deathmatch")
      && !(selection.family == QA_GAME_FAMILY_Q3 && has(classnames, classname_count, "team_CTF_redplayer")
           && has(classnames, classname_count, "team_CTF_blueplayer")))
    return "Deathmatch requires an authored player spawn";
  return NULL;
}
