/* Port of src/content/q1/addons/ctf/flags.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/flags.h"

#include <string.h>

#include "qa/foundation/support.h"

static qa_q1ad_ctf_state *state_of(qa_q1ad_context *context) { return context->slice; }

void qa_q1ad_ctf_return_flag(qa_q1ad_ctf_state *state, qa_q1ad_actor *flag, bool announce) {
  qa_q1ad_context *context = state->context;
  const char *team = qa_q1ad_ctf_flag_team(flag);
  flag->movement = "toss";
  flag->solid = "trigger";
  flag->count = 0;
  flag->has_owner = false;
  qa_q1ad_body_patch patch = {.has_origin = true, .origin = qa_q1ad_actor_vector(flag, "ctf.base"),
                              .has_angles = true, .angles = flag->mangle};
  qa_q1ad_set_body(context, flag, &patch);
  qa_q1ad_link(context, flag);
  qa_q1ad_sound_entity(context, flag, "items/itembk2.wav", NULL);
  if (announce) {
    qa_actor_id players[64];
    size_t count = qa_q1ad_players(context, players, 64);
    for (size_t i = 0; i < count; i++) {
      const char *mine = qa_q1ad_ctf_team(state, players[i]);
      qa_q1ad_message(context, players[i],
          mine != NULL && strcmp(mine, team) == 0 ? "$qc_ctf_your_returned" : "$qc_ctf_enemy_returned",
          false, NULL, 0);
    }
  }
}
void qa_q1ad_ctf_drop_flag(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_actor *flag = qa_q1ad_ctf_carried(state, actor);
  if (flag == NULL) return;
  const char *last = qa_q1ad_ctf_last_team(state, actor);
  qa_q1ad_ctf_announce(state, last != NULL && strcmp(last, "red") == 0
      ? "$qc_ks_blue_dropped" : "$qc_ks_red_dropped", &actor, "");
  if (state->services.log != NULL) state->services.log(state->services.ctx, actor, "FLAG-DROP");
  flag->count = 2;
  flag->movement = "toss";
  flag->solid = "trigger";
  flag->movement_flags = 256 | 131072;
  qa_q1ad_set_number(context, flag, "ctf.return", context->game.time + 15);
  qa_q1ad_body body = qa_q1ad_ctf_body(state, actor);
  qa_q1ad_body_patch patch = {
      .has_origin = true, .origin = qa_sub3(body.origin, (qa_vec3){0, 0, 24}),
      .has_velocity = true, .velocity = {0, 0, 300},
      .has_bounds = true, .bounds = QA_Q1AD_CTF_FLAG_BOUNDS};
  qa_q1ad_set_body(context, flag, &patch);
  qa_q1ad_link(context, flag);
  qa_q1ad_ctf_update(state, NULL);
}
void qa_q1ad_ctf_touch_flag(qa_q1ad_ctf_state *state, qa_q1ad_actor *flag, qa_actor_id actor) {
  qa_q1ad_context *context = state->context;
  const char *team = qa_q1ad_ctf_team(state, actor);
  const char *own = qa_q1ad_ctf_flag_team(flag);
  const char *last = qa_q1ad_ctf_last_team(state, actor);
  bool observer = state->services.observer != NULL && state->services.observer(state->services.ctx, actor);
  if (flag->solid == NULL || strcmp(flag->solid, "trigger") != 0 ||
      !qa_q1ad_is_player(context, &actor) || qa_q1ad_health(context, actor) <= 0 || observer ||
      team == NULL || last == NULL || strcmp(team, last) != 0) return;
  if (strcmp(team, own) == 0) {
    if (flag->count == 0) {
      if (qa_q1ad_ctf_carried(state, actor) == NULL) return;
      qa_q1ad_ctf_announce(state, strcmp(team, "red") == 0 ? "$qc_ks_blue_captured" : "$qc_ks_red_captured", &actor, "");
      if (state->services.log != NULL) state->services.log(state->services.ctx, actor, "FLAG-CAPTURE");
      qa_q1ad_ctf_grant(state, actor, "q1:key/silver", 0, 1);
      qa_q1ad_ctf_grant(state, actor, "q1:key/gold", 0, 1);
      qa_q1ad_actor *world = qa_q1ad_ctf_world(state);
      qa_q1ad_set_number(context, world, "ctf.lastCapture", context->game.time);
      qa_q1ad_set_number(context, world, "ctf.lastCaptureTeam", qa_q1ad_ctf_team_number(team));
      qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
      qa_q1ad_sound_actor(context, owner.id, "misc/flagcap.wav", "voice");
      if (state->services.add_capture != NULL) state->services.add_capture(state->services.ctx, team);
      if (state->services.add_score != NULL) state->services.add_score(state->services.ctx, actor, 15);
      qa_actor_id players[64];
      size_t count = qa_q1ad_players(context, players, 64);
      for (size_t i = 0; i < count; i++) {
        qa_actor_id player = players[i];
        qa_q1ad_ctf_set(state, player, "killed", 0);
        const char *player_team = qa_q1ad_ctf_last_team(state, player);
        bool mine = player_team != NULL && strcmp(player_team, team) == 0;
        if (mine) {
          if (!qa_same_actor(player, actor) && state->services.add_score != NULL)
            state->services.add_score(state->services.ctx, player, 10);
          if (qa_q1ad_ctf_number(state, player, "lastReturned") + 4 > context->game.time) {
            qa_q1ad_ctf_announce(state, "$qc_ks_assist", &player, "");
            if (state->services.add_score != NULL) state->services.add_score(state->services.ctx, player, 1);
          }
          if (qa_q1ad_ctf_number(state, player, "lastFraggedCarrier") + 6 > context->game.time) {
            qa_q1ad_ctf_announce(state, "$qc_ks_assist_carrier", &player, "");
            if (state->services.add_score != NULL) state->services.add_score(state->services.ctx, player, 2);
          }
        } else qa_q1ad_ctf_set(state, player, "lastHurtCarrier", -5);
        qa_q1ad_message(context, player, mine ? "$qc_ctf_team_captured" : "$qc_ctf_your_captured", false, NULL, 0);
      }
      const char *colors[2] = {team, qa_q1ad_ctf_opposite(team)};
      for (int i = 0; i < 2; i++) {
        qa_q1ad_actor *home = qa_q1ad_ctf_flag(state, colors[i]);
        if (home != NULL) qa_q1ad_ctf_return_flag(state, home, false);
      }
    } else {
      qa_q1ad_ctf_announce(state, strcmp(team, "red") == 0 ? "$qc_ks_red_returned" : "$qc_ks_blue_returned", &actor, "");
      if (state->services.log != NULL) state->services.log(state->services.ctx, actor, "FLAG-RECOVERY");
      if (state->services.add_score != NULL) state->services.add_score(state->services.ctx, actor, 1);
      qa_q1ad_ctf_set(state, actor, "lastReturned", context->game.time);
      qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
      qa_q1ad_sound_actor(context, owner.id, "doors/runetry.wav", "item");
      qa_q1ad_ctf_return_flag(state, flag, true);
    }
    qa_q1ad_ctf_update(state, NULL);
    return;
  }
  qa_q1ad_ctf_announce(state, "$qc_ks_blue_picked_up", &actor, "");
  if (state->services.log != NULL) state->services.log(state->services.ctx, actor, "FLAG-PICKUP");
  qa_q1ad_message(context, actor, "$qc_ctf_have_flag", false, NULL, 0);
  qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
  qa_q1ad_sound_actor(context, owner.id, "misc/flagtk.wav", "item");
  qa_q1ad_ctf_grant(state, actor, strcmp(own, "red") == 0 ? "q1:key/gold" : "q1:key/silver", 1, 1);
  qa_q1ad_ctf_set(state, actor, "flagSince", context->game.time);
  flag->count = 1;
  flag->movement = "noclip";
  flag->solid = "none";
  flag->owner = actor;
  flag->has_owner = true;
  qa_q1ad_link(context, flag);
  qa_actor_id players[64];
  size_t count = qa_q1ad_players(context, players, 64);
  for (size_t i = 0; i < count; i++)
    if (!qa_same_actor(players[i], actor)) {
      const char *mine = qa_q1ad_ctf_team(state, players[i]);
      qa_q1ad_message(context, players[i],
          mine != NULL && strcmp(mine, team) == 0 ? "$qc_ctf_your_has" : "$qc_ctf_your_taken", false, NULL, 0);
    }
  qa_q1ad_ctf_update(state, NULL);
}
static void flag_think(qa_q1ad_context *context, qa_q1ad_actor *flag) {
  qa_q1ad_ctf_state *state = state_of(context);
  qa_q1ad_schedule(context, flag, 0.1, "ctf:flag_think");
  if (flag->count == 0) return;
  if (flag->count == 2) {
    /* QC stores time+15 at drop, then compares time-super_time>15: the authored delay is 30 seconds. */
    if (context->game.time - qa_q1ad_actor_number(flag, "ctf.return") > 15)
      qa_q1ad_ctf_return_flag(state, flag, true);
    qa_q1ad_ctf_update(state, NULL);
    return;
  }
  if (flag->count != 1) qa_range_error("CTF flag has an invalid source state");
  if (!flag->has_owner || !(context->game.is_live == NULL ||
      context->game.is_live(context->game.ctx, flag->owner))) {
    qa_q1ad_ctf_return_flag(state, flag, true);
    return;
  }
  qa_actor_id actor = flag->owner;
  if (qa_q1ad_health(context, actor) <= 0) { qa_q1ad_ctf_drop_flag(state, actor); return; }
  qa_q1ad_body body = qa_q1ad_ctf_body(state, actor);
  double frame = state->services.input != NULL ? state->services.input(state->services.ctx, actor).frame : 0;
  qa_angle_vectors basis = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_Q1);
  double distance = 14;
  static const double FIRST[6] = {2, 8, 12, 11, 10, 4};
  static const double SECOND[6] = {2, 10, 10, 8, 4, 2};
  if (frame >= 29 && frame <= 34) distance += FIRST[(int)frame - 29];
  else if (frame >= 35 && frame <= 40) distance += SECOND[(int)frame - 35];
  else if (frame >= 103 && frame <= 118) distance += frame <= 106 ? 6 : 7;
  qa_vec3 forward = basis.forward;
  forward.z = -basis.forward.z;
  qa_vec3 origin = qa_add3(qa_sub3(qa_add3(body.origin, (qa_vec3){0, 0, -16}),
                                   qa_scale3(forward, distance)),
                           qa_scale3(basis.right, 22));
  qa_q1ad_body_patch patch = {.has_origin = true, .origin = origin, .has_angles = true,
      .angles = qa_add3(body.angles, (qa_vec3){0, 0, -45})};
  qa_q1ad_set_body(context, flag, &patch);
  qa_q1ad_link(context, flag);
  qa_q1ad_schedule(context, flag, 0.01, "ctf:flag_think");
}
static void flag_touch(qa_q1ad_context *context, qa_q1ad_actor *flag,
                       const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other != NULL) qa_q1ad_ctf_touch_flag(state_of(context), flag, *other);
}
static void place_flag(qa_q1ad_context *context, qa_q1ad_actor *flag) {
  qa_q1ad_ctf_state *state = state_of(context);
  (void)state;
  qa_q1ad_body body = qa_q1ad_body_of(context, flag);
  qa_vec3 start = qa_add3(body.origin, (qa_vec3){0, 0, 6});
  qa_actor_id self = flag->actor.id;
  qa_q1ad_trace floor_trace = {.fraction = 1, .end = qa_sub3(start, (qa_vec3){0, 0, 256})};
  if (context->game.trace != NULL)
    (void)context->game.trace(context->game.ctx, start, floor_trace.end, body.bounds, &self, true, &floor_trace);
  if (floor_trace.all_solid || floor_trace.fraction == 1) { qa_q1ad_remove(context, flag); return; }
  flag->solid = "trigger";
  flag->movement = "toss";
  flag->movement_flags = 256 | 131072;
  flag->count = 0;
  flag->mangle = body.angles;
  flag->effects = (double)((int32_t)flag->effects | 8);
  qa_q1ad_bind(flag->touch, "ctf:flag_touch");
  qa_q1ad_set_vector(context, flag, "ctf.base", floor_trace.end);
  qa_q1ad_body_patch patch = {.has_origin = true, .origin = floor_trace.end,
      .has_velocity = true, .velocity = {0, 0, 0}, .has_ground = true,
      .ground = floor_trace.actor, .ground_null = !floor_trace.has_actor};
  qa_q1ad_set_body(context, flag, &patch);
  qa_q1ad_link(context, flag);
  qa_q1ad_schedule(context, flag, 0.1, "ctf:flag_think");
}
static void spawn_flag(qa_q1ad_context *context, qa_q1ad_actor *flag) {
  bool first = flag->classname != NULL && strcmp(flag->classname, "item_flag_team1") == 0;
  flag->model = "progs/flag.mdl";
  flag->skin = first ? 0 : 1;
  flag->effects = first ? 32 : 16;
  qa_q1ad_set_bounds(context, flag, QA_Q1AD_CTF_FLAG_BOUNDS);
  qa_q1ad_schedule(context, flag, 0.2, "ctf:place_flag");
}
void qa_q1ad_ctf_register_flags(qa_q1ad_ctf_state *state) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_register_named(context, "ctf:flag_touch", (qa_q1ad_handlers){.touch = flag_touch});
  qa_q1ad_register_named(context, "ctf:flag_think", (qa_q1ad_handlers){.action = flag_think});
  qa_q1ad_register_named(context, "ctf:place_flag", (qa_q1ad_handlers){.action = place_flag});
  qa_q1ad_register_spawn(context, "item_flag_team1", false, spawn_flag);
  qa_q1ad_register_spawn(context, "item_flag_team2", false, spawn_flag);
}
