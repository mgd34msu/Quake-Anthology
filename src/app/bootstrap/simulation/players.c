/* Port of src/app/bootstrap/simulation/players.ts. The
 * ModClientApplication slice machinery runs through the host
 * input_begin/input_finish callbacks; without them move() is
 * moveCommand() exactly as in TS when applications are inactive.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/players.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/player_input_application.h"
#include "qa/app/bootstrap/simulation/player_jump.h"
#include "qa/app/bootstrap/simulation/player_movement.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q1/netquake.h"
#include "qa/movement/q2/index.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static bool starts(const char *a, const char *prefix) { return a != NULL && strncmp(a, prefix, strlen(prefix)) == 0; }
qa_game_family qa_app_sim_provider_family(const char *provider) {
  if (starts(provider, "q1:")) return QA_GAME_FAMILY_Q1;
  if (starts(provider, "q2:")) return QA_GAME_FAMILY_Q2;
  if (starts(provider, "q3:")) return QA_GAME_FAMILY_Q3;
  qa_range_error("Unknown source provider family: %s", provider ? provider : "(null)");
}
qa_provider_timing qa_app_sim_provider_timing(qa_executable_recipe recipe, const char *provider) {
  for (size_t i = 0; i < recipe.timing_count; i++)
    if (is(recipe.timing[i].provider, provider)) return recipe.timing[i];
  qa_range_error("Recipe has no timing for %s", provider ? provider : "(null)");
}
qa_movement_profile qa_app_sim_movement_profile(qa_executable_recipe recipe) {
  qa_provider_timing timing = qa_app_sim_provider_timing(recipe, recipe.movement.provider);
  qa_movement_profile out; memset(&out, 0, sizeof out);
  out.id = timing.provider; out.clock = timing.clock; out.numeric = timing.numeric;
  qa_q1_movement_parameters q1 = {800, 100, 320, 500, 10, 10, 10, 4, 4, 1};
  qa_q1_movement_parameters qw = {800, 100, 320, 500, 10, 0.7, 10, 4, 4, 1};
  switch (timing.clock.kind) {
    case QA_CLOCK_Q1_NETQUAKE:
      out.kind = "q1-netquake";
      out.edition = (recipe.movement.content && strstr(recipe.movement.content, ":rerelease:")) ? "rerelease" : "classic";
      out.parameters = q1; out.edge_friction = 2; out.no_clip_angle_hack = false; break;
    case QA_CLOCK_Q1_QUAKEWORLD: out.kind = "q1-quakeworld"; out.parameters = qw; break;
    case QA_CLOCK_Q2_CLASSIC: out.kind = "q2-classic"; out.air_accelerate = 0; out.snap_initial = true; break;
    case QA_CLOCK_Q2_RERELEASE: out.kind = "q2-rerelease"; out.air_accelerate = 0; out.n64_physics = false; break;
    case QA_CLOCK_Q3:
      out.kind = "q3"; out.product = "baseq3";
      out.has_fixed_milliseconds = timing.clock.has_fixed_movement_milliseconds;
      out.fixed_milliseconds = timing.clock.fixed_movement_milliseconds;
      out.no_footsteps = false; break;
  }
  return out;
}
static qa_integer_triple eighths(qa_vec3 v) {
  qa_integer_triple out = {{trunc(v.x * 8), trunc(v.y * 8), trunc(v.z * 8)}}; return out;
}
qa_vec3 qa_app_sim_movement_origin(qa_movement_state state) {
  if (state.kind == QA_MOVEMENT_STATE_Q2)
    return (qa_vec3){state.as.q2.origin_eighths.values[0] / 8, state.as.q2.origin_eighths.values[1] / 8, state.as.q2.origin_eighths.values[2] / 8};
  switch (state.kind) {
    case QA_MOVEMENT_STATE_Q1: return state.as.q1.origin;
    case QA_MOVEMENT_STATE_QW: return state.as.qw.origin;
    case QA_MOVEMENT_STATE_Q2_RERELEASE: return state.as.q2_rerelease.origin;
    case QA_MOVEMENT_STATE_Q3: return state.as.q3.origin;
    case QA_MOVEMENT_STATE_Q2: break;
  }
  return (qa_vec3){0, 0, 0};
}
qa_vec3 qa_app_sim_movement_velocity(qa_movement_state state) {
  if (state.kind == QA_MOVEMENT_STATE_Q2)
    return (qa_vec3){state.as.q2.velocity_eighths.values[0] / 8, state.as.q2.velocity_eighths.values[1] / 8, state.as.q2.velocity_eighths.values[2] / 8};
  switch (state.kind) {
    case QA_MOVEMENT_STATE_Q1: return state.as.q1.velocity;
    case QA_MOVEMENT_STATE_QW: return state.as.qw.velocity;
    case QA_MOVEMENT_STATE_Q2_RERELEASE: return state.as.q2_rerelease.velocity;
    case QA_MOVEMENT_STATE_Q3: return state.as.q3.velocity;
    case QA_MOVEMENT_STATE_Q2: break;
  }
  return (qa_vec3){0, 0, 0};
}
static qa_app_sim_selected_profile_player selected_view(qa_app_sim_movement_player *p) {
  qa_app_sim_selected_profile_player out; memset(&out, 0, sizeof out);
  out.profile = p->profile; out.world_gravity = p->world_gravity;
  out.has_source_movement = p->has_source_movement; out.source_movement = p->source_movement;
  qa_app_sim_q2_movement_config cfg;
  out.has_q2_movement_config = qa_app_sim_movement_player_q2_config(p, &cfg); out.q2_movement_config = cfg;
  return out;
}
static qa_app_sim_movement_environment_player environment_view(qa_app_sim_movement_player *p) {
  qa_app_sim_movement_environment_player out; memset(&out, 0, sizeof out);
  out.has_source_environment = p->has_source_environment; out.source_environment = p->source_environment;
  out.gravity_multiplier = p->gravity_multiplier; out.state = p->state; out.flight = p->flight;
  out.movement_speed_multiplier = qa_app_sim_movement_player_speed_multiplier(p);
  return out;
}
static qa_combat_state_result combat_of(qa_app_sim_movement_player *p) {
  return qa_gameplay_authority_read(p->host.combat, p->actor.id);
}
static double health_of(qa_app_sim_movement_player *p) {
  qa_combat_state_result c = combat_of(p); return c.has_value ? c.value.health : 0;
}
static qa_body_state_result body_of(qa_app_sim_movement_player *p) {
  return qa_shared_body_table_read(p->host.bodies, p->actor.id);
}
static bool live_of(qa_app_sim_movement_player *p) {
  return qa_session_actor_registry_is_live(p->host.actors, p->actor.id);
}
qa_movement_profile qa_app_sim_movement_player_prediction_profile(qa_app_sim_movement_player *player) {
  qa_movement_profile profile = qa_app_sim_selected_movement_profile(selected_view(player), NULL);
  if (is(profile.kind, "q1-quakeworld") && player->host.has_quakeworld && player->host.qw_profile)
    return player->host.qw_profile(player->host.ctx, profile);
  return profile;
}
double qa_app_sim_movement_player_speed_multiplier(qa_app_sim_movement_player *player) {
  return player->host.speed_multiplier ? player->host.speed_multiplier(player->host.ctx, player->actor) : 1;
}
static void fixed_environment(qa_app_sim_movement_player *p, qa_movement_environment *env, qa_mod_client_movement_outputs *outputs_out);
qa_movement_environment qa_app_sim_movement_player_prediction_environment(qa_app_sim_movement_player *player) {
  qa_combat_state_result combat = combat_of(player);
  if (!combat.has_value) qa_range_error("Prediction player has no combat state");
  qa_movement_environment out = qa_app_sim_player_movement_environment(environment_view(player), combat.value);
  qa_mod_client_movement_outputs outputs;
  fixed_environment(player, &out, &outputs);
  return out;
}
bool qa_app_sim_movement_player_q2_config(qa_app_sim_movement_player *player, qa_app_sim_q2_movement_config *out) {
  if (!player->host.q2_movement_config) return false;
  return player->host.q2_movement_config(player->host.ctx, out);
}
static qa_mod_client_movement_outputs client_outputs_of(qa_app_sim_movement_player *p) {
  qa_mod_client_movement_outputs outputs;
  memset(&outputs, 0, sizeof outputs);
  if (p->host.client_outputs) outputs = p->host.client_outputs(p->host.ctx);
  if (!is(p->profile.kind, "q1-netquake") && !(is(p->profile.kind, "q1-quakeworld") && p->host.has_quakeworld)) return outputs;
  if (outputs.body_bounds != NULL) {
    if (!p->has_body_shape_base) { p->has_body_shape_base = true; p->body_shape_base = p->bounds; }
    return outputs;
  }
  if (!p->has_body_shape_base) return outputs;
  qa_bounds base = p->body_shape_base, current = p->bounds;
  if (memcmp(&current, &base, sizeof base) == 0) p->has_body_shape_base = false;
  static qa_bounds storage; storage = base; outputs.body_bounds = &storage;
  return outputs;
}
static void accept_body_shape(qa_app_sim_movement_player *p, qa_bounds bounds) {
  p->bounds = bounds;
  qa_body_state_result body = body_of(p);
  if (body.has_value) { qa_body_state b = body.value; b.bounds = bounds; qa_shared_body_table_write(p->host.bodies, p->actor, b); }
}
static void fixed_environment(qa_app_sim_movement_player *p, qa_movement_environment *env, qa_mod_client_movement_outputs *outputs_out) {
  const qa_fixed_movement_pose *pose = p->host.fixed_pose ? p->host.fixed_pose(p->host.ctx, p->actor) : NULL;
  if (pose == NULL && p->fixed_pose_active) {
    qa_app_sim_locomotion_player loco; memset(&loco, 0, sizeof loco);
    loco.profile = p->profile; loco.standing_bounds = p->standing_bounds; loco.character = p->character;
    loco.world_gravity = p->world_gravity; loco.flight = p->flight;
    p->bounds = is(p->profile.kind, "q1-quakeworld") ? qa_app_sim_player_crouched_bounds(loco) : p->standing_bounds;
    p->view_height = p->character == QA_GAME_FAMILY_Q3 ? 26 : 22;
    qa_body_state_result body = body_of(p);
    if (body.has_value) { qa_body_state b = body.value; b.bounds = p->bounds; qa_shared_body_table_write(p->host.bodies, p->actor, b); }
  }
  if (pose != NULL) {
    p->bounds = pose->bounds; p->view_height = pose->view_height;
    qa_body_state_result body = body_of(p);
    if (body.has_value) { qa_body_state b = body.value; b.bounds = pose->bounds; qa_shared_body_table_write(p->host.bodies, p->actor, b); }
  }
  p->fixed_pose_active = pose != NULL;
  env->pose = pose;
  static qa_mod_client_movement_outputs outputs_storage;
  outputs_storage = client_outputs_of(p);
  env->client_outputs = &outputs_storage;
  if (outputs_out) *outputs_out = outputs_storage;
}
static qa_movement_continuation application_continuation(qa_app_sim_movement_player *p) {
  qa_movement_continuation out; memset(&out, 0, sizeof out);
  out.is_continue = live_of(p);
  if (out.is_continue) out.state = qa_app_sim_movement_player_read_state(p);
  return out;
}
static qa_movement_input_continuation applied_begin(void *ctx, qa_user_command command, qa_frame_context frame, qa_movement_state state) {
  qa_app_sim_movement_player *p = ctx;
  qa_movement_continuation committed = qa_app_sim_movement_player_commit(p, state, false, false);
  qa_movement_input_continuation out; memset(&out, 0, sizeof out);
  if (!committed.is_continue) return out;
  qa_user_command replaced; const qa_arsenal_intent *arsenal = NULL;
  bool began = p->host.input_begin && p->host.input_begin(p->host.ctx, p, "movement-slice", command, frame, state,
      p->has_arsenal_intent ? &p->arsenal_intent : NULL, &replaced, &arsenal);
  qa_movement_continuation cont = application_continuation(p);
  out.is_continue = cont.is_continue; out.state = cont.state;
  if (!cont.is_continue) return out;
  if (began && arsenal) { p->has_arsenal_intent = true; p->arsenal_intent = *arsenal; }
  out.command = began ? replaced : command;
  return out;
}
static qa_movement_continuation applied_end(void *ctx, qa_movement_state state, bool has_failed, bool failed, const qa_movement_posture *posture) {
  qa_app_sim_movement_player *p = ctx;
  bool f = has_failed && failed;
  if (posture && !f) { p->bounds = posture->bounds; p->view_height = posture->view_height; }
  if (live_of(p) && !f) qa_app_sim_movement_player_commit(p, state, false, false);
  if (p->host.input_finish) p->host.input_finish(p->host.ctx, f);
  return application_continuation(p);
}
static qa_movement_continuation player_touch(void *ctx, qa_movement_touch_contact contact, qa_movement_state state) {
  qa_app_sim_movement_player *p = ctx;
  return p->host.services.touch(p->host.services.ctx, contact, state);
}
static qa_animation_step_result player_animation_step(void *ctx, qa_animation_step_input input) {
  qa_app_sim_movement_player *p = ctx;
  return p->host.services.animation_step(p->host.services.ctx, input);
}
static qa_weapon_step_result player_weapon_step(void *ctx, qa_weapon_step_input input, qa_movement_state state) {
  qa_app_sim_movement_player *p = ctx;
  qa_movement_continuation before = qa_app_sim_movement_player_commit(p, state, false, false);
  qa_weapon_step_result out; memset(&out, 0, sizeof out);
  if (!before.is_continue) {
    out.arsenal = input.arsenal; out.animation = input.animation;
    static qa_movement_continuation removed; removed = before; out.continuation = &removed;
    return out;
  }
  out = p->host.services.weapon_step(p->host.services.ctx, input, before.state);
  static qa_movement_continuation cont;
  cont = application_continuation(p); out.continuation = &cont;
  return out;
}
qa_app_sim_movement_player *qa_app_sim_movement_player_create(qa_owned_actor actor, qa_client_id client,
    qa_executable_recipe recipe, qa_app_sim_player_movement_host host, qa_vec3 origin, qa_vec3 angles,
    qa_arsenal_state arsenal, const qa_provider_reference *character_definition, const qa_provider_reference *character_appearance) {
  qa_app_sim_movement_player *p = calloc(1, sizeof *p);
  if (!p) qa_range_error("Out of memory");
  p->actor = actor; p->client = client; p->recipe = recipe; p->host = host;
  p->character_definition = character_definition ? *character_definition : recipe.character.definition;
  p->character_appearance = character_appearance ? *character_appearance : recipe.character.appearance;
  p->character = qa_app_sim_provider_family(p->character_definition.provider);
  p->standing_bounds = qa_app_sim_player_standing_bounds(p->character);
  p->bounds = p->standing_bounds;
  p->view_height = p->character == QA_GAME_FAMILY_Q3 ? 26 : 22;
  p->view_angles = angles; p->command_angles = angles;
  p->profile = qa_app_sim_movement_profile(recipe);
  p->arsenal = arsenal;
  p->animation.provider = p->character_definition.provider;
  if (p->character == QA_GAME_FAMILY_Q1) { p->animation.state.kind = "q1"; p->animation.state.frame = 12; }
  else if (p->character == QA_GAME_FAMILY_Q2) { p->animation.state.kind = "q2"; p->animation.state.frame = 0; p->animation.state.end_frame = 39; }
  else { p->animation.state.kind = "q3"; p->animation.state.legs = 22; p->animation.state.torso = 11; }
  p->ground.kind = QA_TRACE_HIT_NONE;
  p->gravity_multiplier = 1; p->world_gravity = 800; p->last_sequence = -1; p->last_weapon_seconds = -INFINITY;
  memset(&p->services, 0, sizeof p->services);
  p->services.ctx = p;
  p->services.scene = host.scene;
  p->services.numeric = *qa_create_numeric_operations(&p->profile.numeric);
  p->services.has_input_application = host.input_applications_active;
  p->services.input_application.ctx = p;
  p->services.input_application.begin = applied_begin;
  p->services.input_application.end = applied_end;
  p->services.touch = player_touch;
  p->services.weapon_step = player_weapon_step;
  p->services.animation_step = player_animation_step;
  qa_vec3 zero = {0, 0, 0};
  if (is(p->profile.kind, "q1-netquake")) {
    p->state.kind = QA_MOVEMENT_STATE_Q1;
    qa_q1_movement_state *s = &p->state.as.q1;
    s->origin = origin; s->velocity = zero; s->angles = angles; s->old_origin = origin;
    s->view_angles = angles; s->move_type = 3; s->flags = 4096; s->ground = p->ground;
    s->water_type = -1; s->health = 100;
  } else if (is(p->profile.kind, "q1-quakeworld")) {
    p->state.kind = QA_MOVEMENT_STATE_QW;
    p->state.as.qw.origin = origin; p->state.as.qw.angles = angles; p->state.as.qw.ground = p->ground;
  } else if (is(p->profile.kind, "q2-classic")) {
    p->state.kind = QA_MOVEMENT_STATE_Q2;
    p->state.as.q2.origin_eighths = eighths(origin); p->state.as.q2.gravity = 800;
  } else if (is(p->profile.kind, "q2-rerelease")) {
    p->state.kind = QA_MOVEMENT_STATE_Q2_RERELEASE;
    p->state.as.q2_rerelease.origin = origin; p->state.as.q2_rerelease.gravity = 800;
    p->state.as.q2_rerelease.view_height = p->view_height;
  } else {
    p->state.kind = QA_MOVEMENT_STATE_Q3;
    qa_q3_movement_state *s = &p->state.as.q3;
    s->origin = origin; s->gravity = 800; s->speed = 320; s->view_angles = angles;
    s->view_height = p->view_height; s->ground = p->ground;
  }
  return p;
}
void qa_app_sim_movement_player_destroy(qa_app_sim_movement_player *player) { free(player); }
bool qa_app_sim_movement_player_set_flight(qa_app_sim_movement_player *player, bool enabled) {
  player->flight = enabled && health_of(player) > 0;
  qa_movement_state state = qa_app_sim_movement_player_read_state(player);
  qa_trace_hit none; memset(&none, 0, sizeof none); none.kind = QA_TRACE_HIT_NONE;
  if (state.kind == QA_MOVEMENT_STATE_Q1) {
    player->state = state;
    player->state.as.q1.move_type = player->flight ? 5 : state.as.q1.move_type == 5 ? 3 : state.as.q1.move_type;
    if (player->flight) { player->state.as.q1.flags = (int)state.as.q1.flags & ~512; player->state.as.q1.ground = none; }
  } else if (player->flight) {
    player->state = state;
    switch (state.kind) {
      case QA_MOVEMENT_STATE_QW: player->state.as.qw.spectator = 0; player->state.as.qw.ground = none; player->state.as.qw.water_jump_time_seconds = 0; break;
      case QA_MOVEMENT_STATE_Q2: player->state.as.q2.type = 0; player->state.as.q2.flags = (int)state.as.q2.flags & ~4; break;
      case QA_MOVEMENT_STATE_Q2_RERELEASE: player->state.as.q2_rerelease.type = 0; player->state.as.q2_rerelease.flags = (int)state.as.q2_rerelease.flags & ~4; break;
      case QA_MOVEMENT_STATE_Q3: player->state.as.q3.movement_type = 0; player->state.as.q3.ground = none; break;
      case QA_MOVEMENT_STATE_Q1: break;
    }
  }
  if (player->flight) {
    player->ground = none;
    qa_body_state_result body = body_of(player);
    if (body.has_value) { qa_body_state b = body.value; b.ground = NULL; qa_shared_body_table_write(player->host.bodies, player->actor, b); }
  }
  return player->flight;
}
void qa_app_sim_movement_player_set_source_view_roll(qa_app_sim_movement_player *player, double roll) {
  (void)qa_session_actor_registry_is_live(player->host.actors, player->actor.id);
  player->view_angles.z = (float)roll;
  qa_movement_state *state = &player->state;
  if (state->kind == QA_MOVEMENT_STATE_Q1) state->as.q1.view_angles.z = player->view_angles.z;
  else if (state->kind == QA_MOVEMENT_STATE_Q3) state->as.q3.view_angles.z = player->view_angles.z;
  else if (state->kind == QA_MOVEMENT_STATE_QW) state->as.qw.angles.z = player->view_angles.z;
}
qa_app_sim_player_view qa_app_sim_movement_player_view(qa_app_sim_movement_player *player) {
  qa_body_state_result body = body_of(player);
  if (!body.has_value) qa_range_error("Player no longer has a body");
  qa_app_sim_player_view out; memset(&out, 0, sizeof out);
  out.origin = body.value.origin; out.angles = player->view_angles; out.view_height = player->view_height;
  return out;
}
static qa_movement_state read_movement_state(qa_app_sim_movement_player *p) {
  qa_body_state_result read = body_of(p);
  if (!read.has_value) qa_range_error("Player no longer has a body");
  qa_body_state body = read.value;
  qa_movement_state state = p->state;
  qa_actor_id world; bool has_world = p->host.world_actor && p->host.world_actor(p->host.ctx, &world);
  if (body.ground == NULL) { p->ground.kind = QA_TRACE_HIT_NONE; }
  else if (has_world && qa_same_actor(*body.ground, world)) { p->ground.kind = QA_TRACE_HIT_WORLD; p->ground.model = 0; }
  else { p->ground.kind = QA_TRACE_HIT_ACTOR; p->ground.actor = *body.ground; }
  double health = health_of(p);
  switch (state.kind) {
    case QA_MOVEMENT_STATE_Q1: {
      state.as.q1.origin = body.origin; state.as.q1.velocity = body.velocity; state.as.q1.angles = body.angles;
      state.as.q1.ground = p->ground;
      state.as.q1.flags = p->ground.kind == QA_TRACE_HIT_NONE ? ((int)state.as.q1.flags & ~512) : ((int)state.as.q1.flags | 512);
      state.as.q1.health = health;
      if (p->host.netquake && p->host.netquake->has_projection)
        state.as.q1 = p->host.netquake->projection_read(p->host.netquake->ctx, state.as.q1);
      return state;
    }
    case QA_MOVEMENT_STATE_QW:
      state.as.qw.origin = body.origin; state.as.qw.velocity = body.velocity; state.as.qw.angles = body.angles;
      state.as.qw.ground = p->ground; state.as.qw.dead = health <= 0;
      if (p->host.has_quakeworld && p->host.qw_read) state.as.qw = p->host.qw_read(p->host.ctx, state.as.qw);
      return state;
    case QA_MOVEMENT_STATE_Q2:
      state.as.q2.origin_eighths = eighths(body.origin); state.as.q2.velocity_eighths = eighths(body.velocity);
      state.as.q2.type = health <= 0 ? ((p->host.gibbed && p->host.gibbed(p->host.ctx)) ? 3 : 2) : state.as.q2.type;
      return state;
    case QA_MOVEMENT_STATE_Q2_RERELEASE:
      state.as.q2_rerelease.origin = body.origin; state.as.q2_rerelease.velocity = body.velocity;
      state.as.q2_rerelease.type = health <= 0 ? ((p->host.gibbed && p->host.gibbed(p->host.ctx)) ? 5 : 4) : state.as.q2_rerelease.type;
      return state;
    case QA_MOVEMENT_STATE_Q3:
      state.as.q3.origin = body.origin; state.as.q3.velocity = body.velocity; state.as.q3.ground = p->ground;
      state.as.q3.movement_type = health <= 0 ? 3 : state.as.q3.movement_type;
      return state;
  }
  return state;
}
qa_movement_state qa_app_sim_movement_player_read_state(qa_app_sim_movement_player *player) {
  qa_movement_state state = read_movement_state(player);
  if (player->host.has_source_client && player->host.project_state)
    return player->host.project_state(player->host.ctx, state);
  return state;
}
qa_movement_continuation qa_app_sim_movement_player_commit(qa_app_sim_movement_player *player, qa_movement_state state, bool link, bool triggers) {
  qa_movement_continuation removed; memset(&removed, 0, sizeof removed);
  if (!live_of(player)) return removed;
  player->state = state;
  qa_trace_hit none; memset(&none, 0, sizeof none); none.kind = QA_TRACE_HIT_NONE;
  if (state.kind == QA_MOVEMENT_STATE_Q1) player->ground = ((int)state.as.q1.flags & 512) != 0 ? state.as.q1.ground : none;
  else if (state.kind == QA_MOVEMENT_STATE_QW) player->ground = state.as.qw.ground;
  else if (state.kind == QA_MOVEMENT_STATE_Q3) player->ground = state.as.q3.ground;
  if (state.kind == QA_MOVEMENT_STATE_Q1) {
    player->water_level = state.as.q1.water_level; player->water_type = state.as.q1.water_type;
    player->view_angles = state.as.q1.view_angles;
  }
  qa_body_state_result read = body_of(player);
  if (!read.has_value) qa_range_error("Player has no body during movement");
  qa_body_state body = read.value;
  if (state.kind == QA_MOVEMENT_STATE_Q1 && player->host.netquake) player->bounds = body.bounds;
  qa_vec3 angles = (state.kind == QA_MOVEMENT_STATE_Q1) ? state.as.q1.angles
      : (state.kind == QA_MOVEMENT_STATE_QW) ? state.as.qw.angles : player->view_angles;
  if (state.kind == QA_MOVEMENT_STATE_QW && player->host.has_quakeworld) {
    if (player->host.qw_write) player->host.qw_write(player->host.ctx, state.as.qw);
    player->bounds = body.bounds;
  } else {
    body.origin = qa_app_sim_movement_origin(state);
    body.velocity = qa_app_sim_movement_velocity(state);
    body.angles = angles; body.bounds = player->bounds;
    static qa_actor_id ground_storage;
    if (player->ground.kind == QA_TRACE_HIT_ACTOR) { ground_storage = player->ground.actor; body.ground = &ground_storage; }
    else if (player->ground.kind == QA_TRACE_HIT_WORLD) {
      qa_actor_id world;
      if (player->host.world_actor && player->host.world_actor(player->host.ctx, &world)) { ground_storage = world; body.ground = &ground_storage; }
      else body.ground = NULL;
    } else body.ground = NULL;
    qa_shared_body_table_write(player->host.bodies, player->actor, body);
  }
  if (state.kind == QA_MOVEMENT_STATE_Q1 && player->host.netquake && player->host.netquake->has_projection)
    player->host.netquake->projection_write(player->host.netquake->ctx, state.as.q1);
  if (link) qa_shared_body_table_link(player->host.bodies, player->actor, NULL);
  if (triggers && player->host.touch_triggers) player->host.touch_triggers(player->host.ctx, player->actor);
  return application_continuation(player);
}
static void accept_arsenal_intent(qa_app_sim_movement_player *p, const qa_arsenal_intent *intent, bool has_intent) {
  if (p->has_arsenal_intent && p->arsenal_intent.impulse != 0
      && (!has_intent || is(intent->provider, p->arsenal_intent.provider))) {
    if (!has_intent) { p->arsenal_intent.weapon = NULL; p->arsenal_intent.use_holdable = false; }
    else {
      qa_arsenal_intent next = *intent;
      next.impulse = intent->impulse != 0 ? intent->impulse : p->arsenal_intent.impulse;
      p->arsenal_intent = next;
    }
  } else if (has_intent) { p->has_arsenal_intent = true; p->arsenal_intent = *intent; }
  else p->has_arsenal_intent = false;
}
void qa_app_sim_movement_player_receive_netquake(qa_app_sim_movement_player *player, qa_actor_command input) {
  if (input.command.kind != QA_USER_COMMAND_Q1) qa_range_error("NetQuake client requires a NetQuake command");
  if (input.sequence <= player->last_sequence) return;
  player->has_netquake_command = true;
  player->netquake_command = input.command.as.q1;
  if (player->netquake_command.impulse == 0 && player->has_netquake_command)
    player->netquake_command.impulse = input.command.as.q1.impulse;
  accept_arsenal_intent(player, input.arsenal, input.arsenal != NULL);
  player->previous_buttons = player->buttons; player->buttons = input.command.as.q1.buttons;
  player->command_angles = input.command.as.q1.view_angles; player->view_angles = input.command.as.q1.view_angles;
  player->last_sequence = input.sequence;
}
static qa_q1_movement_state read_state_q1(qa_app_sim_movement_player *p) {
  qa_movement_state state = qa_app_sim_movement_player_read_state(p);
  if (state.kind != QA_MOVEMENT_STATE_Q1) qa_range_error("NetQuake source changed movement family");
  return state.as.q1;
}
typedef struct hook_env { qa_app_sim_movement_player *player; const char *which; } hook_env;
static qa_movement_continuation player_link(void *ctx, qa_owned_actor actor, qa_movement_state state, bool triggers) {
  (void)actor; return qa_app_sim_movement_player_commit(((hook_env *)ctx)->player, state, true, triggers);
}
static bool player_is_bsp(void *ctx, qa_trace_hit hit) {
  qa_app_sim_movement_player *p = ((hook_env *)ctx)->player;
  return hit.kind == QA_TRACE_HIT_WORLD || (hit.kind == QA_TRACE_HIT_ACTOR && p->host.is_brush && p->host.is_brush(p->host.ctx, hit.actor));
}
static void player_body_shape(void *ctx, qa_bounds bounds) { accept_body_shape(((hook_env *)ctx)->player, bounds); }
static qa_movement_input netquake_input(qa_app_sim_movement_player *p, qa_frame_context frame) {
  qa_movement_environment env; qa_mod_client_movement_outputs outputs;
  qa_combat_state_result combat = combat_of(p);
  qa_movement_state state = qa_app_sim_movement_player_read_state(p);
  qa_movement_profile profile = qa_app_sim_selected_movement_profile(selected_view(p), NULL);
  if (state.kind != QA_MOVEMENT_STATE_Q1 || !is(profile.kind, "q1-netquake") || !combat.has_value)
    qa_range_error("Missing NetQuake movement state");
  env = qa_app_sim_player_movement_environment(environment_view(p), combat.value);
  fixed_environment(p, &env, &outputs);
  qa_q1_user_command command;
  if (p->has_netquake_slice_command) command = p->netquake_slice_command;
  else if (p->has_netquake_command) command = p->netquake_command;
  else { memset(&command, 0, sizeof command); command.view_angles = p->view_angles; }
  qa_movement_input out; memset(&out, 0, sizeof out);
  out.kind = "q1-netquake"; out.actor = p->actor; out.command_sequence = p->last_sequence; out.frame = frame;
  out.current_bounds = &p->bounds; out.shape.kind = "box"; out.shape.bounds = p->bounds;
  out.environment = env; out.arsenal = p->arsenal; out.animation = p->animation; out.execution = "authoritative";
  out.state = state; out.profile = profile;
  out.command.kind = QA_USER_COMMAND_Q1; out.command.as.q1 = command;
  return out;
}
static qa_movement_continuation nq_before(void *ctx, qa_q1_player_input input, qa_movement_state state) {
  qa_app_sim_movement_player *p = ((hook_env *)ctx)->player;
  qa_movement_continuation committed = qa_app_sim_movement_player_commit(p, state, false, false);
  if (!committed.is_continue) return committed;
  if (p->host.netquake && p->host.netquake->before_physics) p->host.netquake->before_physics(p->host.netquake->ctx, input.frame);
  return application_continuation(p);
}
static qa_movement_continuation nq_after(void *ctx, qa_q1_player_input input, qa_movement_state state) {
  qa_app_sim_movement_player *p = ((hook_env *)ctx)->player;
  qa_movement_continuation committed = qa_app_sim_movement_player_commit(p, state, false, false);
  if (!committed.is_continue) return committed;
  if (p->host.netquake && p->host.netquake->after_physics) p->host.netquake->after_physics(p->host.netquake->ctx, input.frame);
  return application_continuation(p);
}
static qa_movement_continuation nq_think(void *ctx, qa_q1_player_input input, qa_movement_state state) {
  qa_app_sim_movement_player *p = ((hook_env *)ctx)->player;
  qa_movement_continuation committed = qa_app_sim_movement_player_commit(p, state, false, false);
  if (!committed.is_continue) return committed;
  if (p->host.netquake && p->host.netquake->think) p->host.netquake->think(p->host.netquake->ctx, input.frame);
  return application_continuation(p);
}
static void player_action_jump(void *ctx, qa_owned_actor actor, const char *action, qa_q1_movement_state state) {
  (void)state; qa_app_sim_movement_player *p = ((hook_env *)ctx)->player;
  if (p->host.jump) p->host.jump(p->host.ctx, actor, action);
}
static qa_trace_shape nq_shape(void *ctx) {
  qa_app_sim_movement_player *p = ((hook_env *)ctx)->player;
  qa_body_state_result body = body_of(p);
  if (!body.has_value) qa_range_error("NetQuake client lost its authoritative body");
  qa_trace_shape out; out.kind = "box"; out.bounds = body.value.bounds; return out;
}
static qa_q1_movement_options netquake_options(qa_app_sim_movement_player *p, qa_q1_movement_hooks *hooks, hook_env *env) {
  qa_vec3 punch; bool has_punch = p->host.source_punch && p->host.source_punch(p->host.ctx, p->actor.id, &punch);
  env->player = p;
  memset(hooks, 0, sizeof *hooks); hooks->ctx = env;
  hooks->body_shape = player_body_shape; hooks->player_action = player_action_jump;
  hooks->link = player_link; hooks->is_bsp = player_is_bsp;
  hooks->before_physics = nq_before; hooks->after_physics = nq_after; hooks->think = nq_think;
  if (p->host.netquake) hooks->shape = nq_shape;
  qa_q1_movement_options out; memset(&out, 0, sizeof out);
  out.has_source_punch_angles = has_punch; if (has_punch) out.source_punch_angles = punch;
  out.has_view_height = true; out.view_height = p->view_height;
  if (p->host.netquake) out.jump_authority = p->host.netquake->jump_authority;
  out.hooks = hooks;
  return out;
}
void qa_app_sim_movement_player_prepare_netquake(qa_app_sim_movement_player *player, qa_frame_context frame,
    const qa_arsenal_intent *(*prepare)(void *ctx, qa_q1_user_command command, const qa_arsenal_intent *arsenal, bool *out_has), void *prepare_ctx) {
  qa_movement_input input = netquake_input(player, frame);
  qa_user_command replaced; const qa_arsenal_intent *arsenal = NULL;
  bool began = player->host.input_begin && player->host.input_begin(player->host.ctx, player, "client-command",
      input.command, frame, input.state, player->has_arsenal_intent ? &player->arsenal_intent : NULL, &replaced, &arsenal);
  player->netquake_application_open = began;
  if (!live_of(player)) {
    if (player->host.input_finish && began) player->host.input_finish(player->host.ctx, false);
    player->netquake_application_open = false; return;
  }
  qa_q1_user_command effective = began && replaced.kind == QA_USER_COMMAND_Q1 ? replaced.as.q1 : input.command.as.q1;
  if (began && arsenal) { player->has_arsenal_intent = true; player->arsenal_intent = *arsenal; }
  if (prepare) {
    bool has = false;
    const qa_arsenal_intent *next = prepare(prepare_ctx, effective, player->has_arsenal_intent ? &player->arsenal_intent : NULL, &has);
    player->has_arsenal_intent = has && next != NULL;
    if (player->has_arsenal_intent) player->arsenal_intent = *next;
  }
  player->buttons = effective.buttons;
  if (player->host.netquake && player->host.netquake->input) player->host.netquake->input(player->host.netquake->ctx, effective);
  qa_q1_movement_hooks hooks; hook_env env;
  qa_q1_movement_options options = netquake_options(player, &hooks, &env);
  qa_movement_input prepared_input = input;
  prepared_input.command.as.q1 = effective;
  prepared_input.state.kind = QA_MOVEMENT_STATE_Q1; prepared_input.state.as.q1 = read_state_q1(player);
  qa_q1_movement_state prepared = qa_prepare_netquake(prepared_input, player->services, options);
  qa_movement_state next; memset(&next, 0, sizeof next); next.kind = QA_MOVEMENT_STATE_Q1; next.as.q1 = prepared;
  qa_app_sim_movement_player_commit(player, next, false, false);
}
void qa_app_sim_movement_player_finish_netquake_input(qa_app_sim_movement_player *player, bool failed) {
  if (player->netquake_slice_open) {
    player->netquake_slice_open = false;
    qa_movement_state state = live_of(player) ? qa_app_sim_movement_player_read_state(player) : player->state;
    applied_end(player, state, true, failed, NULL);
  }
  if (player->host.input_finish && player->netquake_application_open) player->host.input_finish(player->host.ctx, failed);
  player->netquake_application_open = false;
  player->has_netquake_slice_command = false;
}
static void accept_result(qa_app_sim_movement_player *p, qa_movement_result result) {
  p->state = result.state; p->view_angles = result.view_angles; p->view_height = result.view_height;
  p->bounds = result.bounds; p->ground = result.ground; p->water_level = result.water_level; p->water_type = result.water_type;
  p->arsenal = result.arsenal; p->animation = result.animation;
}
qa_movement_result qa_app_sim_movement_player_physics_netquake(qa_app_sim_movement_player *player, qa_frame_context frame) {
  bool observed = player->netquake_application_open;
  qa_q1_movement_hooks hooks; hook_env env;
  qa_q1_movement_options options = netquake_options(player, &hooks, &env);
  qa_movement_result result = qa_physics_netquake(netquake_input(player, frame), player->services, options);
  if (is(result.status, "active") && live_of(player)) {
    accept_result(player, result);
    qa_app_sim_movement_player_commit(player, result.state, false, false);
  }
  if (player->has_netquake_command) player->netquake_command.impulse = 0;
  qa_app_sim_movement_player_finish_netquake_input(player, false);
  if (!observed) return result;
  if (!live_of(player)) {
    qa_movement_result removed = result;
    removed.kind = "q1-netquake"; removed.status = "actor-removed"; removed.actor = player->actor.id;
    return removed;
  }
  if (is(result.status, "active")) { result.state.kind = QA_MOVEMENT_STATE_Q1; result.state.as.q1 = read_state_q1(player); }
  return result;
}
static qa_movement_continuation qw_before(void *ctx, qa_q1_player_input input, qa_movement_state state) {
  qa_app_sim_movement_player *p = ((hook_env *)ctx)->player;
  qa_movement_continuation committed = qa_app_sim_movement_player_commit(p, state, false, false);
  if (!committed.is_continue) return committed;
  if (input.command.kind == QA_USER_COMMAND_QW && p->host.has_quakeworld && p->host.qw_before_physics)
    p->host.qw_before_physics(p->host.ctx, input.command.as.qw, input.frame);
  return application_continuation(p);
}
static qa_movement_continuation plain_after(void *ctx, qa_q1_player_input input, qa_movement_state state) {
  (void)input; return qa_app_sim_movement_player_commit(((hook_env *)ctx)->player, state, false, false);
}
static void qw_state_hook(void *ctx, double level, double type) {
  qa_app_sim_movement_player *p = ((hook_env *)ctx)->player;
  if (p->host.has_quakeworld && p->host.qw_water) p->host.qw_water(p->host.ctx, level, type);
}
static void publish_player_posture(void *ctx, qa_bounds bounds, double view_height) {
  qa_app_sim_movement_player *p = ctx; p->bounds = bounds; p->view_height = view_height;
}
typedef struct move_jump_env { hook_env base; bool source_jump; } move_jump_env;
static void move_player_action(void *ctx, qa_owned_actor actor, const char *action, qa_q1_movement_state state) {
  (void)state; move_jump_env *e = ctx; qa_app_sim_movement_player *p = e->base.player;
  if ((!is(action, "jump") || !e->source_jump) && p->host.jump) p->host.jump(p->host.ctx, actor, action);
}
static qa_movement_result wrong_family(void) {
  qa_range_error("Source callback changed a player's movement provider");
}
static qa_movement_result apply_q3_contacts(qa_app_sim_movement_player *p, qa_movement_result moved, qa_movement_state state) {
  qa_movement_state current = state;
  for (size_t i = 0; i < moved.contact_count; i++) {
    qa_movement_contact contact = moved.contacts[i];
    if (contact.target.kind == QA_TRACE_HIT_NONE) continue;
    qa_movement_touch_contact touch; memset(&touch, 0, sizeof touch);
    touch.self = p->actor; touch.other = contact.target;
    static qa_plane plane_storage; plane_storage = contact.trace.source_plane.plane; touch.plane = &plane_storage;
    static qa_touch_surface surface_storage;
    if (is(contact.trace.kind, "q3")) {
      memset(&surface_storage, 0, sizeof surface_storage);
      surface_storage.name = ""; surface_storage.native_flags = contact.trace.surface_flags;
      touch.surface = &surface_storage;
    }
    qa_movement_continuation cont = p->host.services.touch(p->host.services.ctx, touch, current);
    if (!cont.is_continue) {
      qa_movement_result removed = moved; removed.status = "actor-removed"; return removed;
    }
    if (cont.state.kind != QA_MOVEMENT_STATE_Q3) return wrong_family();
    current = cont.state;
  }
  if (memcmp(&current, &state, sizeof current) != 0) moved.state = current;
  return moved;
}
qa_movement_result qa_app_sim_movement_player_move_command(qa_app_sim_movement_player *player, qa_actor_command input, qa_frame_context frame, const qa_qw_user_command *source_command) {
  accept_arsenal_intent(player, input.arsenal, input.arsenal != NULL);
  bool source_jump = false;
  if (player->host.has_source_client && player->host.before_movement) {
    qa_app_sim_quakec_client_movement source = player->host.before_movement(player->host.ctx, input, frame, source_command);
    input.command = source.command; source_jump = source.source_jump;
  }
  player->state = qa_app_sim_movement_player_read_state(player);
  player->previous_buttons = player->buttons;
  qa_user_command command = input.command;
  player->buttons = command.kind == QA_USER_COMMAND_Q1 ? command.as.q1.buttons
      : command.kind == QA_USER_COMMAND_QW ? command.as.qw.buttons
      : command.kind == QA_USER_COMMAND_Q2 ? command.as.q2.buttons
      : command.kind == QA_USER_COMMAND_Q2_RERELEASE ? command.as.q2_rerelease.buttons : command.as.q3.buttons;
  if (command.kind == QA_USER_COMMAND_Q1) player->view_angles = command.as.q1.view_angles;
  else if (command.kind == QA_USER_COMMAND_QW) player->view_angles = command.as.qw.angles;
  else if (command.kind == QA_USER_COMMAND_Q2_RERELEASE) player->view_angles = command.as.q2_rerelease.angles;
  else {
    const double *words = command.kind == QA_USER_COMMAND_Q2 ? command.as.q2.angle_shorts.values : command.as.q3.angle_words.values;
    player->view_angles = (qa_vec3){words[0] * 360 / 65536, words[1] * 360 / 65536, words[2] * 360 / 65536};
  }
  player->command_angles = player->view_angles;
  qa_combat_state_result combat = combat_of(player);
  if (!combat.has_value) qa_range_error("Player has no combat state");
  qa_movement_environment env = qa_app_sim_player_movement_environment(environment_view(player), combat.value);
  qa_mod_client_movement_outputs outputs; fixed_environment(player, &env, &outputs);
  qa_movement_state state = player->state;
  qa_trace_hit prior_ground = player->ground;
  qa_movement_profile selected = qa_app_sim_selected_movement_profile(selected_view(player), NULL);
  qa_movement_profile profile = (is(selected.kind, "q1-quakeworld") && player->host.has_quakeworld && player->host.qw_profile)
      ? player->host.qw_profile(player->host.ctx, selected) : selected;
  qa_vec3 punch; bool has_punch = player->host.source_punch && player->host.source_punch(player->host.ctx, player->actor.id, &punch);
  move_jump_env jenv; jenv.base.player = player; jenv.source_jump = source_jump;
  qa_q1_movement_hooks hooks; memset(&hooks, 0, sizeof hooks); hooks.ctx = &jenv;
  hooks.body_shape = player_body_shape; hooks.player_action = move_player_action;
  hooks.link = player_link; hooks.is_bsp = player_is_bsp; hooks.qw_state = qw_state_hook;
  hooks.before_physics = qw_before; hooks.after_physics = plain_after;
  qa_q1_movement_options q1_options; memset(&q1_options, 0, sizeof q1_options);
  q1_options.has_source_punch_angles = has_punch; if (has_punch) q1_options.source_punch_angles = punch;
  q1_options.has_view_height = true; q1_options.view_height = player->view_height; q1_options.hooks = &hooks;
  qa_app_sim_provider_player prov; memset(&prov, 0, sizeof prov);
  prov.profile = player->profile; prov.character = player->character; prov.standing_bounds = player->standing_bounds;
  prov.view_height = player->view_height; prov.has_source_movement = player->has_source_movement; prov.source_movement = player->source_movement;
  qa_app_sim_player_movement_hooks phooks; memset(&phooks, 0, sizeof phooks);
  phooks.native_quakeworld = player->host.has_quakeworld;
  phooks.publish_ctx = player; phooks.publish_posture = publish_player_posture;
  phooks.q1 = q1_options; phooks.q2 = player->host.rerelease_movement; phooks.q3 = player->host.q3_hooks;
  phooks.q3_options_storage = &player->q3_provider_options;
  qa_movement_provider provider = qa_app_sim_create_player_movement_provider(prov, phooks);
  qa_movement_input base; memset(&base, 0, sizeof base);
  base.actor = player->actor; base.command_sequence = input.sequence; base.frame = frame;
  base.current_bounds = &player->bounds; base.shape.kind = "box";
  base.shape.bounds = (is(player->profile.kind, "q1-quakeworld") && !player->host.has_quakeworld) ? player->bounds : player->standing_bounds;
  base.environment = env; base.arsenal = player->arsenal; base.animation = player->animation; base.execution = "authoritative";
  base.profile = profile; base.state = state; base.command = command;
  qa_movement_result result;
  bool dispatch = (is(provider.kind, "q1-netquake") && is(profile.kind, "q1-netquake") && state.kind == QA_MOVEMENT_STATE_Q1 && command.kind == QA_USER_COMMAND_Q1)
      || (is(provider.kind, "q1-quakeworld") && is(profile.kind, "q1-quakeworld") && state.kind == QA_MOVEMENT_STATE_QW && command.kind == QA_USER_COMMAND_QW)
      || (is(provider.kind, "q2-classic") && is(profile.kind, "q2-classic") && state.kind == QA_MOVEMENT_STATE_Q2 && command.kind == QA_USER_COMMAND_Q2)
      || (is(provider.kind, "q2-rerelease") && is(profile.kind, "q2-rerelease") && state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE && command.kind == QA_USER_COMMAND_Q2_RERELEASE)
      || (is(provider.kind, "q3") && is(profile.kind, "q3") && state.kind == QA_MOVEMENT_STATE_Q3 && command.kind == QA_USER_COMMAND_Q3);
  if (!dispatch) {
    const char *ck = command.kind == QA_USER_COMMAND_Q1 ? "q1-netquake" : command.kind == QA_USER_COMMAND_QW ? "q1-quakeworld"
        : command.kind == QA_USER_COMMAND_Q2 ? "q2-classic" : command.kind == QA_USER_COMMAND_Q2_RERELEASE ? "q2-rerelease" : "q3";
    qa_range_error("Command %s does not match movement %s", ck, profile.kind);
  }
  base.kind = profile.kind;
  if (is(profile.kind, "q2-rerelease")) { base.view_offset = (qa_vec3){0, 0, player->view_height}; base.snap_initial = true; }
  qa_movement_result moved = provider.move(provider.ctx, base, player->services);
  if ((is(profile.kind, "q2-classic") || is(profile.kind, "q2-rerelease") || is(profile.kind, "q3")) && is(moved.status, "active")) {
    accept_result(player, moved);
    qa_movement_continuation after = qa_app_sim_movement_player_commit(player, moved.state, true, true);
    if (!after.is_continue) { result = moved; result.status = "actor-removed"; }
    else if (is(profile.kind, "q3")) {
      if (after.state.kind != QA_MOVEMENT_STATE_Q3) return wrong_family();
      result = apply_q3_contacts(player, moved, after.state);
    } else {
      qa_movement_state_kind want = is(profile.kind, "q2-classic") ? QA_MOVEMENT_STATE_Q2 : QA_MOVEMENT_STATE_Q2_RERELEASE;
      if (after.state.kind != want) return wrong_family();
      qa_movement_result with_state = moved; with_state.state = after.state;
      result = qa_apply_q2_movement_contacts(base, player->services, with_state);
    }
  } else result = moved;
  if (is(result.status, "active") && live_of(player)) {
    if (!source_jump && !player->host.has_quakeworld
        && qa_app_sim_movement_jumped(state, prior_ground, command, result) && player->host.jump)
      player->host.jump(player->host.ctx, player->actor, "jump");
    accept_result(player, result);
    qa_app_sim_movement_player_commit(player, result.state, true, is(result.kind, "q3"));
  }
  if (live_of(player) && player->host.has_source_client && player->host.after_movement)
    player->host.after_movement(player->host.ctx, player, frame, source_command);
  player->last_sequence = input.sequence;
  return result;
}
qa_movement_result qa_app_sim_movement_player_move(qa_app_sim_movement_player *player, qa_actor_command input, qa_frame_context frame, const qa_qw_user_command *source_command) {
  if (!player->host.input_applications_active || !player->host.input_begin)
    return qa_app_sim_movement_player_move_command(player, input, frame, source_command);
  accept_arsenal_intent(player, input.arsenal, input.arsenal != NULL);
  qa_movement_state state = qa_app_sim_movement_player_read_state(player);
  qa_frame_context app_frame = qa_app_sim_movement_application_frame(input.command, state,
      qa_app_sim_movement_player_prediction_profile(player), frame);
  qa_user_command replaced; const qa_arsenal_intent *arsenal = NULL;
  bool began = player->host.input_begin(player->host.ctx, player, "client-command", input.command, app_frame, state,
      player->has_arsenal_intent ? &player->arsenal_intent : NULL, &replaced, &arsenal);
  if (!live_of(player)) {
    if (began && player->host.input_finish) player->host.input_finish(player->host.ctx, false);
    qa_movement_result removed; memset(&removed, 0, sizeof removed);
    removed.kind = player->profile.kind; removed.status = "actor-removed";
    removed.actor = player->actor.id; removed.command_sequence = input.sequence;
    return removed;
  }
  if (began && arsenal) { player->has_arsenal_intent = true; player->arsenal_intent = *arsenal; }
  qa_actor_command next = input;
  if (began) { next.command = replaced; if (arsenal) { static qa_arsenal_intent storage; storage = *arsenal; next.arsenal = &storage; } }
  qa_movement_result result = qa_app_sim_movement_player_move_command(player, next, frame, source_command);
  if (began && player->host.input_finish) player->host.input_finish(player->host.ctx, false);
  if (!live_of(player)) {
    qa_movement_result removed; memset(&removed, 0, sizeof removed);
    removed.kind = player->profile.kind; removed.status = "actor-removed";
    removed.actor = player->actor.id; removed.command_sequence = input.sequence;
    return removed;
  }
  if (!is(result.status, "active")) return result;
  /* refreshInputResult */
  qa_movement_state current = qa_app_sim_movement_player_read_state(player);
  qa_body_state_result body = body_of(player);
  if (!body.has_value) qa_range_error("Input application lost its live body");
  player->state = current; player->bounds = body.value.bounds;
  const char *want = current.kind == QA_MOVEMENT_STATE_Q1 ? "q1-netquake" : current.kind == QA_MOVEMENT_STATE_QW ? "q1-quakeworld"
      : current.kind == QA_MOVEMENT_STATE_Q2 ? "q2-classic" : current.kind == QA_MOVEMENT_STATE_Q2_RERELEASE ? "q2-rerelease" : "q3";
  if (!is(result.kind, want)) return wrong_family();
  result.bounds = player->bounds; result.view_angles = player->view_angles; result.view_height = player->view_height;
  result.ground = player->ground; result.water_level = player->water_level; result.water_type = player->water_type;
  result.arsenal = player->arsenal; result.animation = player->animation; result.state = current;
  return result;
}
