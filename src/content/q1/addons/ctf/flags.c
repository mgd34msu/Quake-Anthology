/* Port of src/content/q1/addons/ctf/flags.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/flags.h"

#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static bool same_team(const char *a, const char *b) { return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0); }

void qa_q1ad_ctf_return_flag(qa_q1ad_ctf_state *state, qa_q1_actor *flag, bool announce) {
  qa_q1_services *game = state->context->game;
  const char *team = qa_q1ad_ctf_flag_team(flag);
  flag->movement = "toss";
  flag->solid = "trigger";
  flag->count = 0;
  flag->has_owner = false;
  qa_q1_body_patch patch = {.has_origin = true, .origin = qa_q1_actor_vector(flag, "ctf.base"),
                            .has_angles = true, .angles = flag->mangle};
  qa_q1_set_body(game, flag, &patch);
  qa_q1_link(game, flag);
  qa_q1_sound(game, flag->actor.id, "items/itembk2.wav", "voice", 1, 1);
  if (announce) {
    size_t count = 0;
    const qa_actor_id *players = game->host.players(game->host.ctx, &count);
    for (size_t i = 0; i < count; i++)
      qa_q1_message(game, &players[i],
                    same_team(qa_q1ad_ctf_team(state, players[i]), team) ? "$qc_ctf_your_returned" : "$qc_ctf_enemy_returned",
                    true, NULL, 0);
  }
}
void qa_q1ad_ctf_drop_flag(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  qa_q1_actor *flag = qa_q1ad_ctf_carried(state, actor);
  if (flag == NULL) return;
  qa_q1ad_ctf_announce(state, same_team(qa_q1ad_ctf_last_team(state, actor), "red") ? "$qc_ks_blue_dropped" : "$qc_ks_red_dropped",
                       &actor, "");
  state->services.log(state->services.ctx, actor, "FLAG-DROP");
  flag->count = 2;
  flag->movement = "toss";
  flag->solid = "trigger";
  flag->movement_flags = 256 | 131072;
  qa_q1ad_set_number(state->context, flag, "ctf.return", game->time + 15);
  qa_q1_body_patch patch = {.has_origin = true, .origin = qa_q1_vsub(qa_q1ad_ctf_body(state, actor).origin, (qa_vec3){0, 0, 24}),
                            .has_velocity = true, .velocity = {0, 0, 300},
                            .has_bounds = true, .bounds = QA_Q1AD_CTF_FLAG_BOUNDS};
  qa_q1_set_body(game, flag, &patch);
  qa_q1_link(game, flag);
  qa_q1ad_ctf_update(state, NULL);
}
void qa_q1ad_ctf_touch_flag(qa_q1ad_ctf_state *state, qa_q1_actor *flag, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  const char *team = qa_q1ad_ctf_team(state, actor);
  const char *own = qa_q1ad_ctf_flag_team(flag);
  if (strcmp(flag->solid, "trigger") != 0 || !qa_q1_is_player(game, &actor) || qa_q1_health(game, actor) <= 0 ||
      state->services.observer(state->services.ctx, actor) || team == NULL ||
      !same_team(team, qa_q1ad_ctf_last_team(state, actor)))
    return;
  if (same_team(team, own)) {
    if (flag->count == 0) {
      if (qa_q1ad_ctf_carried(state, actor) == NULL) return;
      qa_q1ad_ctf_announce(state, strcmp(team, "red") == 0 ? "$qc_ks_blue_captured" : "$qc_ks_red_captured", &actor, "");
      state->services.log(state->services.ctx, actor, "FLAG-CAPTURE");
      qa_q1ad_ctf_grant(state, actor, "q1:key/silver", 0, 1);
      qa_q1ad_ctf_grant(state, actor, "q1:key/gold", 0, 1);
      qa_q1ad_set_number(state->context, qa_q1ad_ctf_world(state), "ctf.lastCapture", game->time);
      qa_q1ad_set_number(state->context, qa_q1ad_ctf_world(state), "ctf.lastCaptureTeam", qa_q1ad_ctf_team_number(team));
      qa_q1_sound(game, qa_q1ad_ctf_owner(state, actor).id, "misc/flagcap.wav", "voice", 0, 1);
      state->services.add_capture(state->services.ctx, team);
      state->services.add_score(state->services.ctx, actor, 15);
      size_t count = 0;
      const qa_actor_id *players = game->host.players(game->host.ctx, &count);
      for (size_t i = 0; i < count; i++) {
        qa_actor_id player = players[i];
        qa_q1ad_ctf_set(state, player, "killed", 0);
        if (same_team(qa_q1ad_ctf_last_team(state, player), team)) {
          if (!qa_same_actor(player, actor)) state->services.add_score(state->services.ctx, player, 10);
          if (qa_q1ad_ctf_number(state, player, "lastReturned") + 4 > game->time) {
            qa_q1ad_ctf_announce(state, "$qc_ks_assist", &player, "");
            state->services.add_score(state->services.ctx, player, 1);
          }
          if (qa_q1ad_ctf_number(state, player, "lastFraggedCarrier") + 6 > game->time) {
            qa_q1ad_ctf_announce(state, "$qc_ks_assist_carrier", &player, "");
            state->services.add_score(state->services.ctx, player, 2);
          }
        } else qa_q1ad_ctf_set(state, player, "lastHurtCarrier", -5);
        const char *last = qa_q1ad_ctf_last_team(state, player);
        if (same_team(last, team)) qa_q1_message(game, &player, "$qc_ctf_team_captured", true, NULL, 0);
        else if (same_team(last, qa_q1ad_ctf_opposite(team))) qa_q1_message(game, &player, "$qc_ctf_your_captured", true, NULL, 0);
      }
      const char *colors[2] = {team, qa_q1ad_ctf_opposite(team)};
      for (int i = 0; i < 2; i++) {
        qa_q1_actor *home = qa_q1ad_ctf_flag(state, colors[i]);
        if (home != NULL) qa_q1ad_ctf_return_flag(state, home, false);
      }
    } else {
      qa_q1ad_ctf_announce(state, strcmp(team, "red") == 0 ? "$qc_ks_red_returned" : "$qc_ks_blue_returned", &actor, "");
      state->services.log(state->services.ctx, actor, "FLAG-RECOVERY");
      state->services.add_score(state->services.ctx, actor, 1);
      qa_q1ad_ctf_set(state, actor, "lastReturned", game->time);
      qa_q1_sound(game, qa_q1ad_ctf_owner(state, actor).id, "doors/runetry.wav", "item", 1, 1);
      qa_q1ad_ctf_return_flag(state, flag, true);
    }
    qa_q1ad_ctf_update(state, NULL);
    return;
  }
  qa_q1ad_ctf_announce(state, "$qc_ks_blue_picked_up", &actor, "");
  state->services.log(state->services.ctx, actor, "FLAG-PICKUP");
  qa_q1_message(game, &actor, "$qc_ctf_have_flag", true, NULL, 0);
  qa_q1_sound(game, qa_q1ad_ctf_owner(state, actor).id, "misc/flagtk.wav", "item", 1, 1);
  qa_q1ad_ctf_grant(state, actor, strcmp(own, "red") == 0 ? "q1:key/gold" : "q1:key/silver", 1, 1);
  qa_q1ad_ctf_set(state, actor, "flagSince", game->time);
  flag->count = 1;
  flag->movement = "noclip";
  flag->solid = "none";
  flag->has_owner = true;
  flag->owner = actor;
  qa_q1_link(game, flag);
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++)
    if (!qa_same_actor(players[i], actor)) {
      const char *side = qa_q1ad_ctf_team(state, players[i]);
      if (same_team(side, team)) qa_q1_message(game, &players[i], "$qc_ctf_your_has", true, NULL, 0);
      else if (same_team(side, qa_q1ad_ctf_opposite(team))) qa_q1_message(game, &players[i], "$qc_ctf_your_taken", true, NULL, 0);
    }
  qa_q1ad_ctf_update(state, NULL);
}

static void schedule_think(qa_q1_services *game, qa_q1_actor *flag, double delay) {
  qa_q1_schedule(game, flag, delay, qa_q1_callback_registry_action(game->named, flag, "ctf:flag_think"));
}
static void flag_think(void *ctx, qa_q1_services *game, qa_q1_actor *flag) {
  qa_q1ad_ctf_state *state = ctx;
  schedule_think(game, flag, 0.1);
  if (flag->count == 0) return;
  if (flag->count == 2) {
    /* QC stores time+15 at drop, then compares time-super_time>15: the authored delay is 30 seconds. */
    if (game->time - qa_q1_actor_number(flag, "ctf.return") > 15) qa_q1ad_ctf_return_flag(state, flag, true);
    qa_q1ad_ctf_update(state, NULL);
    return;
  }
  if (flag->count != 1) qa_range_error("CTF flag has an invalid source state");
  if (!flag->has_owner || !qa_session_actor_registry_is_live(game->host.actors, flag->owner)) {
    qa_q1ad_ctf_return_flag(state, flag, true);
    return;
  }
  qa_actor_id actor = flag->owner;
  if (qa_q1_health(game, actor) <= 0) { qa_q1ad_ctf_drop_flag(state, actor); return; }
  qa_q1_body body = qa_q1ad_ctf_body(state, actor);
  double frame = state->services.input(state->services.ctx, actor).frame;
  qa_q1_basis basis = qa_q1_vectors(body.angles);
  double distance = 14;
  static const double FIRST[6] = {2, 8, 12, 11, 10, 4};
  static const double SECOND[6] = {2, 10, 10, 8, 4, 2};
  /* [..][frame - n] ?? 0: a fractional frame indexes nothing. */
  if (frame >= 29 && frame <= 34) distance += frame == (double)(int)frame ? FIRST[(int)frame - 29] : 0;
  else if (frame >= 35 && frame <= 40) distance += frame == (double)(int)frame ? SECOND[(int)frame - 35] : 0;
  else if (frame >= 103 && frame <= 118) distance += frame <= 106 ? 6 : 7;
  qa_vec3 forward = {basis.forward.x, basis.forward.y, -basis.forward.z};
  qa_q1_body_patch patch = {
      .has_origin = true,
      .origin = qa_q1_vadd(qa_q1_vsub(qa_q1_vadd(body.origin, (qa_vec3){0, 0, -16}), qa_q1_vscale(forward, distance)),
                           qa_q1_vscale(basis.right, 22)),
      .has_angles = true, .angles = qa_q1_vadd(body.angles, (qa_vec3){0, 0, -45})};
  qa_q1_set_body(game, flag, &patch);
  qa_q1_link(game, flag);
  schedule_think(game, flag, 0.01);
}
static void flag_touch(void *ctx, qa_q1_services *game, qa_q1_actor *flag, qa_actor_id actor, const qa_vec3 *normal,
                       const qa_touch_surface *surface) {
  (void)game; (void)normal; (void)surface; /* TS touch reads (flag, actor) */
  qa_q1ad_ctf_touch_flag(ctx, flag, actor);
}
static void place_flag(void *ctx, qa_q1_services *game, qa_q1_actor *flag) {
  qa_q1ad_ctf_state *state = ctx;
  qa_q1_body body = qa_q1_get_body(game, flag);
  qa_vec3 start = qa_q1_vadd(body.origin, (qa_vec3){0, 0, 6});
  qa_q1_trace floor = qa_q1_host_trace(game, (qa_q1_trace_request){
      .start = start, .end = qa_q1_vsub(start, (qa_vec3){0, 0, 256}), .bounds = body.bounds,
      .has_ignore = true, .ignore = flag->actor.id, .monsters = true});
  if (floor.all_solid || floor.fraction == 1) { qa_q1_remove(game, flag); return; }
  flag->solid = "trigger";
  flag->movement = "toss";
  flag->movement_flags = 256 | 131072;
  flag->count = 0;
  flag->mangle = body.angles;
  flag->effects = qa_js_bit_or(flag->effects, 8);
  flag->touch = qa_q1_callback_registry_touch(game->named, flag, "ctf:flag_touch");
  qa_q1ad_set_vector(state->context, flag, "ctf.base", floor.end);
  qa_q1_body_patch patch = {.has_origin = true, .origin = floor.end, .has_velocity = true, .velocity = QA_Q1_ZERO,
                            .has_ground = true, .ground = floor.has_actor ? &floor.actor : NULL};
  qa_q1_set_body(game, flag, &patch);
  qa_q1_link(game, flag);
  schedule_think(game, flag, 0.1);
}
static void spawn_flag(void *ctx, qa_q1_services *game, qa_q1_actor *flag, int index) {
  (void)ctx; /* TS spawn closure uses game and its index only */
  qa_q1ad_set_text(&flag->model, "progs/flag.mdl");
  flag->skin = index;
  flag->effects = index == 0 ? 32 : 16;
  qa_q1_set_bounds(game, flag, QA_Q1AD_CTF_FLAG_BOUNDS);
  qa_q1_schedule(game, flag, 0.2, qa_q1_callback_registry_action(game->named, flag, "ctf:place_flag"));
}
static void spawn_flag_team1(void *ctx, qa_q1_services *game, qa_q1_actor *flag) { spawn_flag(ctx, game, flag, 0); }
static void spawn_flag_team2(void *ctx, qa_q1_services *game, qa_q1_actor *flag) { spawn_flag(ctx, game, flag, 1); }

void qa_q1ad_ctf_register_flags(qa_q1ad_ctf_state *state) {
  qa_q1_services *game = state->context->game;
  qa_q1_callback_registry_register(game->named, "ctf:flag_touch", (qa_q1_callback_handlers){.ctx = state, .touch = flag_touch});
  qa_q1_callback_registry_register(game->named, "ctf:flag_think", (qa_q1_callback_handlers){.ctx = state, .action = flag_think});
  qa_q1_callback_registry_register(game->named, "ctf:place_flag", (qa_q1_callback_handlers){.ctx = state, .action = place_flag});
  qa_q1_register_spawn(game, "item_flag_team1", spawn_flag_team1, state);
  qa_q1_register_spawn(game, "item_flag_team2", spawn_flag_team2, state);
}
