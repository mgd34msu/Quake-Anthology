/* Port of src/content/q2/multiplayer/lmctf/admin.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/lmctf/admin.h"
#include "../multiplayer_internal.h"
#include <ctype.h>
#include "qa/foundation/js_case.h"
#include "qa/foundation/js.h"

static char *join_args(const char *const *args, size_t count) {
  size_t len = 1;
  for (size_t i = 0; i < count; i++) len += strlen(args[i]) + 1;
  char *out = qa_q2mp_xalloc(malloc(len));
  out[0] = '\0';
  for (size_t i = 0; i < count; i++) {
    if (i > 0) strcat(out, " ");
    strcat(out, args[i]);
  }
  return out;
}

static bool map_listed(qa_lmctf_rules *rules, const char *map) {
  for (size_t i = 0; i < rules->map_count; i++)
    if (strcmp(rules->map_list[i], map) == 0) return true;
  return false;
}

bool qa_lmctf_admin_command(qa_lmctf_context *context, qa_lmctf_match *match, qa_q2_entity *entity,
                            qa_q2_game_services *g, const char *name, const char *const *args, size_t arg_count) {
  qa_lmctf_player_state *state = qa_lmctf_player(context, entity->actor.id);
  qa_actor_id self = entity->actor.id;
  if (strcmp(name, "pausematch") == 0 || strcmp(name, "unpausematch") == 0 || strcmp(name, "pause_match") == 0) {
    if ((qa_js_to_int32(state->extra_flags) & 2) != 0) qa_lmctf_match_toggle_pause(match, g);
    else qa_lmctf_print(g, &self, "You are not a Referee\n");
    return true;
  }
  if (strcmp(name, "referee") == 0) {
    char *password = join_args(args, arg_count);
    if (strcmp(password, context->rules->rcon_password) == 0) {
      if (password[0] == '\0') qa_lmctf_print(g, &self, "Rcon Mode is off\n");
      else {
        state->extra_flags = (double)(qa_js_to_int32(state->extra_flags) | 6);
        qa_lmctf_print(g, &self, "You are now an Rcon\n");
      }
    } else if (strcmp(password, context->rules->ref_password) == 0) {
      if (password[0] == '\0') qa_lmctf_print(g, &self, "Referee Mode is off\n");
      else {
        state->extra_flags = (double)((qa_js_to_int32(state->extra_flags) | 2) & ~4);
        qa_lmctf_print(g, &self, "You are now a Referee\n");
      }
    } else {
      state->extra_flags = (double)(qa_js_to_int32(state->extra_flags) & ~6);
      qa_lmctf_print(g, &self, "Incorrect Referee Password\n");
    }
    free(password);
    return true;
  }
  if (strcmp(name, "match") == 0) {
    if ((qa_js_to_int32(state->extra_flags) & 2) == 0) {
      qa_lmctf_print(g, &self, "You are not a Referee\n");
      return true;
    }
    char *map = join_args(args, arg_count);
    if (map[0] == '\0') qa_lmctf_print(g, &self, "USAGE: match <mapname>\n");
    else if (!map_listed(context->rules, map)) qa_lmctf_print(g, &self, "%s is not a map from the maplist.\n", map);
    else {
      qa_lmctf_print(g, &self, "Match countdown beginning.\n");
      qa_lmctf_match_change_map(match, map, true);
    }
    free(map);
    return true;
  }
  static const char *const referee_only[5] = {"lock", "unlock", "startmatch", "stopmatch", "gotomap"};
  bool known = false;
  for (int i = 0; i < 5; i++)
    if (strcmp(name, referee_only[i]) == 0) known = true;
  if (!known) return false;
  if ((qa_js_to_int32(state->extra_flags) & 2) == 0) {
    qa_lmctf_print(g, &self, "Referee-only command denied.\n");
    return true;
  }
  if (strcmp(name, "lock") == 0 || strcmp(name, "unlock") == 0) {
    qa_lmctf_match_set_teams_locked(match, !qa_lmctf_match_teams_locked(match));
    qa_lmctf_print(g, NULL, "Teams are now %slocked\n", qa_lmctf_match_teams_locked(match) ? "" : "un");
  } else if (strcmp(name, "startmatch") == 0) {
    if (strcmp(qa_lmctf_match_phase(match), "none") != 0) qa_lmctf_print(g, &self, "Match already running, stop it first\n");
    else qa_lmctf_match_start(match, g);
  } else if (strcmp(name, "stopmatch") == 0) {
    if (strcmp(qa_lmctf_match_phase(match), "none") == 0) qa_lmctf_print(g, &self, "No match running\n");
    else {
      qa_lmctf_match_stop(match);
      qa_lmctf_print(g, NULL, "Match stopped by %s\n", qa_lmctf_name(context, self));
    }
  } else if (strcmp(name, "gotomap") == 0) {
    char *map = join_args(args, arg_count);
    { char *lowered = qa_js_to_lower_case(map); if (!lowered) qa_range_error("Out of memory"); free(map); map = lowered; }
    if (map[0] != '\0') {
      if (map_listed(context->rules, map)) qa_lmctf_match_change_map(match, map, false);
      else qa_lmctf_print(g, &self, "%s is not a map from the maplist.\n", map);
    }
    free(map);
  }
  return true;
}
