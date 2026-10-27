/* Port of src/content/q1/addons/ctf/teams.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/teams.h"

#include <stdio.h>
#include <string.h>

void qa_q1ad_ctf_set_team(qa_q1ad_ctf_state *state, qa_actor_id actor, const char *team) {
  qa_q1ad_context *context = state->context;
  if (context->game.combat_set_team != NULL)
    context->game.combat_set_team(context->game.ctx, qa_q1ad_ctf_owner(state, actor), team);
  qa_q1ad_ctf_set(state, actor, "lastteam", qa_q1ad_ctf_team_number(team));
  double color = team == NULL ? 0 : qa_q1ad_ctf_team_number(team) - 1;
  if (state->services.colors != NULL) state->services.colors(state->services.ctx, actor, color, color);
}
void qa_q1ad_ctf_check_team(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  const char *current = qa_q1ad_ctf_team(state, actor);
  if (qa_q1ad_ctf_number(state, actor, "lastteam") >= 0 && current != NULL) {
    qa_q1ad_ctf_set(state, actor, "lastteam", qa_q1ad_ctf_team_number(current));
    return;
  }
  int red = 0, blue = 0;
  qa_actor_id players[64];
  size_t count = qa_q1ad_players(state->context, players, 64);
  for (size_t i = 0; i < count; i++) {
    if (qa_same_actor(actor, players[i])) continue;
    const char *team = qa_q1ad_ctf_team(state, players[i]);
    if (team != NULL && strcmp(team, "red") == 0) red++;
    else if (team != NULL && strcmp(team, "blue") == 0) blue++;
  }
  qa_q1ad_ctf_set_team(state, actor,
      blue < red || (blue == red && qa_q1ad_random(state->context) < 0.5) ? "blue" : "red");
}
void qa_q1ad_ctf_check_team_lock(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_context *context = state->context;
  double teamplay = qa_q1ad_ctf_teamplay(state);
  if (teamplay < 0) return;
  bool observer = state->services.observer != NULL && state->services.observer(state->services.ctx, actor);
  if (observer || qa_q1ad_ctf_start_map(state)) {
    if (qa_q1ad_ctf_number(state, actor, "lastteam") != 1 && state->services.colors != NULL)
      state->services.colors(state->services.ctx, actor, 0, 0);
    qa_q1ad_ctf_set(state, actor, "lastteam", 1);
    return;
  }
  if (qa_q1ad_ctf_number(state, actor, "stuffColor") != 0) {
    qa_q1ad_ctf_set(state, actor, "stuffColor", 0);
    double color = qa_q1ad_ctf_number(state, actor, "lastteam") - 1;
    if (state->services.colors != NULL) state->services.colors(state->services.ctx, actor, color, color);
    return;
  }
  const char *current = qa_q1ad_ctf_team(state, actor);
  const char *previous = qa_q1ad_ctf_last_team(state, actor);
  if (current == NULL && qa_q1ad_ctf_number(state, actor, "lastteam") == 0)
    qa_q1ad_ctf_set(state, actor, "lastteam", -1);
  if (qa_q1ad_ctf_team_number(current) == qa_q1ad_ctf_number(state, actor, "lastteam")) return;
  if (((int32_t)teamplay & QA_Q1AD_CTF_STATIC_TEAMS) != 0 &&
      qa_q1ad_ctf_number(state, actor, "lastteam") >= 0) {
    if (previous != NULL) {
      if (qa_q1ad_ctf_number(state, actor, "suicideCount") > 3 && state->services.disconnect != NULL)
        state->services.disconnect(state->services.ctx, actor);
      qa_q1ad_ctf_set(state, actor, "killed", qa_q1ad_ctf_number(state, actor, "killed") == 1 ? 1 : 2);
      if (context->game.combat_set_invulnerable != NULL)
        context->game.combat_set_invulnerable(context->game.ctx, qa_q1ad_ctf_owner(state, actor), false);
      qa_q1ad_damage(context, actor, &actor, &actor, 1000, NULL, "direct", "ctf:teamchange");
      qa_q1ad_ctf_set(state, actor, "suicideCount", qa_q1ad_ctf_number(state, actor, "suicideCount") + 1);
      qa_q1ad_ctf_set_team(state, actor, previous);
      return;
    }
    qa_q1ad_ctf_set(state, actor, "lastteam", -50);
  }
  if (qa_q1ad_ctf_number(state, actor, "lastteam") > 0) {
    qa_q1ad_ctf_set(state, actor, "killed", qa_q1ad_ctf_number(state, actor, "killed") == 1 ? 1 : 2);
    qa_q1ad_damage(context, actor, &actor, &actor, 1000, NULL, "direct", "ctf:teamchange");
  }
  double score = state->services.score != NULL ? state->services.score(state->services.ctx, actor) : 0;
  if (state->services.add_score != NULL) state->services.add_score(state->services.ctx, actor, -score);
  qa_q1ad_ctf_check_team(state, actor);
}
qa_q1ad_actor *qa_q1ad_ctf_spawn_point(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_context *context = state->context;
  const char *team = qa_q1ad_ctf_team(state, actor);
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(context, entities, 512);
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "testplayerstart") == 0)
      return entities[i];
  if (context->game.coop || context->game.deathmatch == 0)
    return context->game.spawn_select != NULL ? context->game.spawn_select(context->game.ctx, true) : NULL;
  const char *classname = qa_q1ad_ctf_start_map(state) && qa_q1ad_ctf_number(state, actor, "killed") != 0
      ? "info_vote_destination"
      : qa_q1ad_ctf_number(state, actor, "killed") == 0 && team != NULL
          ? strcmp(team, "red") == 0 ? "info_player_team1" : "info_player_team2"
          : "info_player_deathmatch";
  qa_q1ad_actor *spots[128];
  size_t spot_count = 0;
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, classname) == 0 && spot_count < 128)
      spots[spot_count++] = entities[i];
  qa_q1ad_actor *world = qa_q1ad_ctf_world(state);
  char field[96];
  (void)snprintf(field, sizeof field, "ctf.spawn.%s", classname);
  qa_actor_id last;
  bool has_last = qa_q1ad_actor_ref(world, field, &last);
  int index = -1;
  if (has_last)
    for (size_t i = 0; i < spot_count; i++)
      if (qa_same_actor(spots[i]->actor.id, last)) index = (int)i;
  qa_q1ad_actor *spot = spot_count > 0 ? spots[(size_t)(index + 1) % spot_count] : NULL;
  if (spot == NULL) {
    for (size_t i = 0; i < count; i++)
      if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "info_player_deathmatch") == 0)
        return entities[i];
    for (size_t i = 0; i < count; i++)
      if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "info_player_start") == 0)
        return entities[i];
    return NULL;
  }
  qa_q1ad_actor_set_ref(world, field, &spot->actor.id);
  return spot;
}
void qa_q1ad_ctf_show_team_prompt(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  if (state->services.prompt_supported != NULL &&
      state->services.prompt_supported(state->services.ctx, actor)) {
    static const qa_q1ad_ctf_prompt_choice CHOICES[] = {
      {"$qc_ctf_intro_auto", 103}, {"$qc_ctf_intro_red", 101},
      {"$qc_ctf_intro_blue", 102}, {"$qc_ctf_intro_observer", 104}};
    if (state->services.prompt != NULL)
      state->services.prompt(state->services.ctx, actor, "$qc_ctf_intro", CHOICES, 4);
    return;
  }
  qa_q1ad_message(state->context, actor,
      "Welcome!\nRunning ThreeWave CTF 5.0\n\nCapture the Flag!\n\nPress 1 for RED team\nPress 2 for BLUE team\nOr press JUMP for automatic team\n",
      false, NULL, 0);
}
bool qa_q1ad_ctf_observer_impulse(qa_q1ad_ctf_state *state, qa_actor_id actor, const double *forced_impulse) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_ctf_input input = state->services.input != NULL
      ? state->services.input(state->services.ctx, actor) : (qa_q1ad_ctf_input){0};
  if (forced_impulse != NULL) input.impulse = *forced_impulse;
  bool observer = state->services.observer != NULL && state->services.observer(state->services.ctx, actor);
  bool prompt = state->services.prompt_supported != NULL &&
      state->services.prompt_supported(state->services.ctx, actor);
  bool numbered = input.impulse >= 100 && input.impulse <= 104;
  bool fallback = !prompt && observer &&
      ((input.impulse >= 1 && input.impulse <= 3) || input.jump);
  if (!numbered && !fallback) return false;
  double teamplay = qa_q1ad_ctf_teamplay(state);
  if (input.impulse == 100 && ((int32_t)teamplay & QA_Q1AD_CTF_STATIC_TEAMS) != 0) {
    qa_q1ad_message(context, actor, "$qc_ctf_teams_locked", false, NULL, 0);
    if (state->services.consume_impulse != NULL) state->services.consume_impulse(state->services.ctx, actor);
    return true;
  }
  if (!observer) qa_q1ad_damage(context, actor, &actor, &actor, 1000, NULL, "direct", "ctf:teamchange");
  if (state->services.set_observer != NULL) state->services.set_observer(state->services.ctx, actor, false);
  qa_q1ad_ctf_set(state, actor, "killed", 0);
  if (input.impulse == 100 || input.impulse == 104) {
    qa_q1ad_ctf_set_team(state, actor, NULL);
    if (state->services.set_observer != NULL) state->services.set_observer(state->services.ctx, actor, true);
  } else if (input.impulse == 1 || input.impulse == 101) qa_q1ad_ctf_set_team(state, actor, "red");
  else if (input.impulse == 2 || input.impulse == 102) qa_q1ad_ctf_set_team(state, actor, "blue");
  else if (input.impulse == 103) {
    qa_q1ad_ctf_set(state, actor, "lastteam", -50);
    qa_q1ad_ctf_check_team(state, actor);
  }
  if (state->services.clear_prompt != NULL) state->services.clear_prompt(state->services.ctx, actor);
  const char *joined = qa_q1ad_ctf_last_team(state, actor);
  if (joined != NULL)
    qa_q1ad_ctf_announce(state, strcmp(joined, "red") == 0 ? "$qc_ks_joined_red" : "$qc_ks_joined_blue", &actor, "");
  if (state->services.consume_impulse != NULL) state->services.consume_impulse(state->services.ctx, actor);
  qa_q1ad_ctf_set(state, actor, "stuffColor", 1);
  if (state->services.respawn != NULL)
    state->services.respawn(state->services.ctx, actor, qa_q1ad_ctf_spawn_point(state, actor));
  if (input.impulse == 100) qa_q1ad_ctf_show_team_prompt(state, actor);
  return true;
}
