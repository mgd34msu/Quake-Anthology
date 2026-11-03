/* Port of src/content/q1/addons/ctf/teams.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/teams.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/base/rules.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static int32_t teamplay_bits(qa_q1ad_ctf_state *state) { return qa_js_to_int32(qa_q1ad_ctf_teamplay(state)); }

void qa_q1ad_ctf_set_team(qa_q1ad_ctf_state *state, qa_actor_id actor, const char *team) {
  qa_gameplay_authority_set_traits(state->context->game->host.combat, qa_q1ad_ctf_owner(state, actor),
                                   (qa_combat_trait_changes){.has_team = true, .team = team});
  qa_q1ad_ctf_set(state, actor, "lastteam", qa_q1ad_ctf_team_number(team));
  double color = team == NULL ? 0 : qa_q1ad_ctf_team_number(team) - 1;
  state->services.colors(state->services.ctx, actor, color, color);
}
void qa_q1ad_ctf_check_team(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  const char *current = qa_q1ad_ctf_team(state, actor);
  if (qa_q1ad_ctf_number(state, actor, "lastteam") >= 0 && current != NULL) {
    qa_q1ad_ctf_set(state, actor, "lastteam", qa_q1ad_ctf_team_number(current));
    return;
  }
  int red = 0, blue = 0;
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++) {
    if (qa_same_actor(actor, players[i])) continue;
    const char *team = qa_q1ad_ctf_team(state, players[i]);
    if (team != NULL && strcmp(team, "red") == 0) red++;
    else if (team != NULL && strcmp(team, "blue") == 0) blue++;
  }
  qa_q1ad_ctf_set_team(state, actor, blue < red || (blue == red && qa_q1_host_random(game) < 0.5) ? "blue" : "red");
}
void qa_q1ad_ctf_check_team_lock(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  if (qa_q1ad_ctf_teamplay(state) < 0) return;
  if (state->services.observer(state->services.ctx, actor) || qa_q1ad_ctf_start_map(state)) {
    if (qa_q1ad_ctf_number(state, actor, "lastteam") != 1) state->services.colors(state->services.ctx, actor, 0, 0);
    qa_q1ad_ctf_set(state, actor, "lastteam", 1);
    return;
  }
  if (qa_q1ad_ctf_number(state, actor, "stuffColor") != 0) {
    qa_q1ad_ctf_set(state, actor, "stuffColor", 0);
    double shirt = qa_q1ad_ctf_number(state, actor, "lastteam") - 1;
    double pants = qa_q1ad_ctf_number(state, actor, "lastteam") - 1;
    state->services.colors(state->services.ctx, actor, shirt, pants);
    return;
  }
  const char *current = qa_q1ad_ctf_team(state, actor), *previous = qa_q1ad_ctf_last_team(state, actor);
  if (current == NULL && qa_q1ad_ctf_number(state, actor, "lastteam") == 0) qa_q1ad_ctf_set(state, actor, "lastteam", -1);
  if (qa_q1ad_ctf_team_number(current) == qa_q1ad_ctf_number(state, actor, "lastteam")) return;
  if ((teamplay_bits(state) & QA_Q1AD_CTF_STATIC_TEAMS) && qa_q1ad_ctf_number(state, actor, "lastteam") >= 0) {
    if (previous != NULL) {
      if (qa_q1ad_ctf_number(state, actor, "suicideCount") > 3) {
        state->services.disconnect(state->services.ctx, actor);
        return;
      }
      qa_q1ad_ctf_set(state, actor, "killed", qa_q1ad_ctf_number(state, actor, "killed") == 1 ? 1 : 2);
      qa_gameplay_authority_set_traits(game->host.combat, qa_q1ad_ctf_owner(state, actor),
                                       (qa_combat_trait_changes){.has_invulnerable = true, .invulnerable = false});
      (void)qa_q1_damage(game, actor, actor, &actor, 1000, NULL, "direct", "ctf:teamchange", NULL);
      qa_q1ad_ctf_set(state, actor, "suicideCount", qa_q1ad_ctf_number(state, actor, "suicideCount") + 1);
      qa_q1ad_ctf_set_team(state, actor, previous);
      return;
    }
    qa_q1ad_ctf_set(state, actor, "lastteam", -50);
  }
  if (qa_q1ad_ctf_number(state, actor, "lastteam") > 0) {
    qa_q1ad_ctf_set(state, actor, "killed", qa_q1ad_ctf_number(state, actor, "killed") == 1 ? 1 : 2);
    (void)qa_q1_damage(game, actor, actor, &actor, 1000, NULL, "direct", "ctf:teamchange", NULL);
  }
  state->services.add_score(state->services.ctx, actor, -state->services.score(state->services.ctx, actor));
  qa_q1ad_ctf_check_team(state, actor);
}

static qa_q1_actor *rotate(qa_q1ad_ctf_state *state, qa_q1_actor **entities, size_t count, const char *name) {
  qa_q1_actor *world = qa_q1ad_ctf_world(state);
  char key[96];
  (void)snprintf(key, sizeof key, "ctf.spawn.%s", name);
  qa_actor_id last;
  bool has_last = qa_q1_actor_reference_get(world, key, &last);
  size_t spot_count = 0, index = (size_t)-1;
  for (size_t i = 0; i < count; i++)
    if (strcmp(entities[i]->classname, name) == 0) {
      if (index == (size_t)-1 && has_last && qa_same_actor(entities[i]->actor.id, last)) index = spot_count;
      spot_count++;
    }
  if (spot_count == 0) return NULL;
  size_t wanted = (index + 1) % spot_count, seen = 0;
  qa_q1_actor *spot = NULL;
  for (size_t i = 0; i < count && spot == NULL; i++)
    if (strcmp(entities[i]->classname, name) == 0 && seen++ == wanted) spot = entities[i];
  (void)qa_q1_actor_reference_set(qa_q1ad_ctf_world(state), key, &spot->actor.id);
  return spot;
}
static qa_q1_actor *first_named(qa_q1_actor **entities, size_t count, const char *classname) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(entities[i]->classname, classname) == 0) return entities[i];
  return NULL;
}
qa_q1_actor *qa_q1ad_ctf_spawn_point(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  const char *team = qa_q1ad_ctf_team(state, actor);
  size_t count = game->entity_count;
  qa_q1_actor **entities = malloc((count != 0 ? count : 1) * sizeof *entities); /* [...entities.values()] */
  if (entities == NULL) qa_range_error("out of memory");
  memcpy(entities, game->entities, count * sizeof *entities);
  qa_q1_actor *result = first_named(entities, count, "testplayerstart");
  if (result == NULL) {
    if (game->options.coop || game->options.deathmatch == 0) {
      result = qa_q1_spawn_selector_select(qa_q1_base_spawn_selector(state->context->base), true);
    } else {
      const char *classname =
          qa_q1ad_ctf_start_map(state) && qa_q1ad_ctf_number(state, actor, "killed") != 0 ? "info_vote_destination"
          : qa_q1ad_ctf_number(state, actor, "killed") == 0 && team != NULL
              ? (strcmp(team, "red") == 0 ? "info_player_team1" : "info_player_team2")
              : "info_player_deathmatch";
      result = rotate(state, entities, count, classname);
      if (result == NULL && strcmp(classname, "info_player_deathmatch") != 0)
        result = rotate(state, entities, count, "info_player_deathmatch");
      if (result == NULL) result = first_named(entities, count, "info_player_start");
    }
  }
  free(entities);
  return result;
}
void qa_q1ad_ctf_show_team_prompt(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  if (state->services.prompt_supported(state->services.ctx, actor)) {
    static const qa_q1ad_ctf_prompt_choice CHOICES[4] = {
      {"$qc_ctf_intro_auto", 103}, {"$qc_ctf_intro_red", 101}, {"$qc_ctf_intro_blue", 102},
      {"$qc_ctf_intro_observer", 104}};
    state->services.prompt(state->services.ctx, actor, "$qc_ctf_intro", CHOICES, 4);
    return;
  }
  qa_q1_message(state->context->game, &actor,
                "Welcome!\nRunning ThreeWave CTF 5.0\n\nCapture the Flag!\n\nPress 1 for RED team\nPress 2 for BLUE team\n"
                "Or press JUMP for automatic team\n",
                true, NULL, 0);
}
bool qa_q1ad_ctf_observer_impulse(qa_q1ad_ctf_state *state, qa_actor_id actor, const double *forced_impulse) {
  qa_q1_services *game = state->context->game;
  qa_q1ad_ctf_input input = state->services.input(state->services.ctx, actor);
  if (forced_impulse != NULL) input.impulse = *forced_impulse;
  bool observer = state->services.observer(state->services.ctx, actor);
  if (!(input.impulse >= 100 && input.impulse <= 104) &&
      !(!state->services.prompt_supported(state->services.ctx, actor) && observer &&
        ((input.impulse >= 1 && input.impulse <= 3) || input.jump)))
    return false;
  if (input.impulse == 100 && (teamplay_bits(state) & QA_Q1AD_CTF_STATIC_TEAMS)) {
    qa_q1_message(game, &actor, "$qc_ctf_teams_locked", true, NULL, 0);
    state->services.consume_impulse(state->services.ctx, actor);
    return true;
  }
  if (!observer) (void)qa_q1_damage(game, actor, actor, &actor, 1000, NULL, "direct", "ctf:teamchange", NULL);
  state->services.set_observer(state->services.ctx, actor, false);
  qa_q1ad_ctf_set(state, actor, "killed", 0);
  if (input.impulse == 100 || input.impulse == 104) {
    qa_q1ad_ctf_set_team(state, actor, NULL);
    state->services.set_observer(state->services.ctx, actor, true);
  } else if (input.impulse == 1 || input.impulse == 101) qa_q1ad_ctf_set_team(state, actor, "red");
  else if (input.impulse == 2 || input.impulse == 102) qa_q1ad_ctf_set_team(state, actor, "blue");
  else if (input.impulse == 103) {
    qa_q1ad_ctf_set(state, actor, "lastteam", -50);
    qa_q1ad_ctf_check_team(state, actor);
  }
  state->services.clear_prompt(state->services.ctx, actor);
  if (qa_q1ad_ctf_last_team(state, actor) != NULL)
    qa_q1ad_ctf_announce(state, strcmp(qa_q1ad_ctf_last_team(state, actor), "red") == 0 ? "$qc_ks_joined_red" : "$qc_ks_joined_blue",
                         &actor, "");
  state->services.consume_impulse(state->services.ctx, actor);
  qa_q1ad_ctf_set(state, actor, "stuffColor", 1);
  state->services.respawn(state->services.ctx, actor, qa_q1ad_ctf_spawn_point(state, actor));
  if (input.impulse == 100) qa_q1ad_ctf_show_team_prompt(state, actor);
  return true;
}
