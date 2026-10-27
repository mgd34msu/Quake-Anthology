/* Port of src/content/q2/multiplayer/lmctf/spawns.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/lmctf/spawns.h"
#include "../multiplayer_internal.h"

qa_q2_entity *qa_lmctf_team_spawn(qa_lmctf_context *context, qa_q2_entity *entity, qa_q2_game_services *g) {
  const char *classname = qa_lmctf_player(context, entity->actor.id)->team == 1 ? "info_player_red" : "info_player_blue";
  size_t n = 0;
  qa_q2_entity **spots = qa_q2mp_entities_named(g, classname, &n);
  qa_q2_entity *best = NULL;
  double distance = 0;
  for (size_t i = 0; i < n; i++) {
    double current = qa_q2mp_players_range(g, spots[i]);
    if (current > distance) {
      best = spots[i];
      distance = current;
    }
  }
  qa_q2_entity *out = best != NULL ? best : n > 0 ? spots[0] : NULL;
  free(spots);
  return out;
}

static qa_q2_entity *first_named(qa_q2_game_services *g, const char *classname) {
  size_t n = 0;
  qa_q2_entity **spots = qa_q2mp_entities_named(g, classname, &n);
  qa_q2_entity *out = n > 0 ? spots[0] : NULL;
  free(spots);
  return out;
}

void qa_select_lmctf_spawn(qa_lmctf_context *context, qa_q2_entity *entity, qa_q2_game_services *g, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  qa_lmctf_player_state *state = qa_lmctf_player(context, entity->actor.id);
  qa_q2mp_player_state *player = context->hooks.player(context->hooks.ctx, entity->actor.id);
  if (player == NULL) qa_q2f_error("LMCTF spawn needs the admitted source player");
  qa_q2_entity *spot = state->spawn_state == 0 ? qa_lmctf_team_spawn(context, entity, g) : NULL;
  state->spawn_state = 1;
  size_t dm_count = 0;
  qa_q2_entity **dm = qa_q2mp_entities_named(g, "info_player_deathmatch", &dm_count);
  free(dm);
  if (spot == NULL && dm_count > 0) {
    qa_q2_entity *deathmatch = qa_q2mp_select_spawn(g, player->slot, "");
    qa_q2_entity *team = qa_lmctf_team_spawn(context, entity, g);
    spot = team == NULL || qa_q2mp_players_range(g, deathmatch) > qa_q2mp_players_range(g, team) ? deathmatch : team;
  }
  if (spot == NULL) spot = first_named(g, "info_flag_red");
  if (spot == NULL) spot = first_named(g, "info_flag_blue");
  if (spot == NULL) spot = qa_q2mp_select_spawn(g, player->slot, "");
  *out_origin = qa_q2_add(qa_q2_game_body(g, spot).origin, (qa_vec3){0, 0, 9});
  *out_angles = qa_q2_game_body(g, spot).angles;
}
