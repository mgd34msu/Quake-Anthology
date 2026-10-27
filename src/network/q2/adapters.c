/* Port of src/network/q2/adapters.ts. GPL-2.0-or-later. */
#include "qa/network/q2/adapters.h"

#include <string.h>

#include "qa/foundation/support.h"

static qa_vec3 vec_of(const float v[3]) {
  qa_vec3 out = {(double)v[0], (double)v[1], (double)v[2]};
  return out;
}
static qa_vec4 color_of(const float v[4]) {
  qa_vec4 out = {(double)v[0], (double)v[1], (double)v[2], (double)v[3]};
  return out;
}
static void set_vec(float target[3], qa_vec3 v) {
  target[0] = (float)v.x;
  target[1] = (float)v.y;
  target[2] = (float)v.z;
}
static void set_color(float target[4], qa_vec4 v) {
  target[0] = (float)v.x;
  target[1] = (float)v.y;
  target[2] = (float)v.z;
  target[3] = (float)v.w;
}

void qa_q2_from_entity(const qa_q2_entity_state *state, bool is_rerelease,
                       uint64_t rerelease_effects, double alpha, double scale,
                       double instance_bits, double loop_volume,
                       double loop_attenuation, double owner, double old_frame,
                       qa_q2_wire_entity *out) {
  qa_q2_wire_entity_init(out);
  out->number = state->number;
  set_vec(out->origin, state->origin);
  set_vec(out->angles, state->angles);
  set_vec(out->old_origin, state->old_origin);
  out->modelindex = state->model_indexes[0];
  out->modelindex2 = state->model_indexes[1];
  out->modelindex3 = state->model_indexes[2];
  out->modelindex4 = state->model_indexes[3];
  out->frame = state->frame;
  out->skinnum = state->skin;
  out->renderfx = state->render_effects;
  out->solid = state->solid;
  out->sound = state->sound;
  out->event = state->event;
  if (is_rerelease) {
    out->effects = (double)(uint32_t)(rerelease_effects & 0xffffffffull);
    out->morefx = (double)(uint32_t)((rerelease_effects >> 32) & 0xffffffffull);
    out->alpha = alpha;
    out->scale = scale;
    out->instance_bits = instance_bits;
    out->loop_volume = loop_volume;
    out->loop_attenuation = loop_attenuation;
    out->owner = owner;
    out->old_frame = old_frame;
  } else {
    out->effects = state->effects;
  }
}

void qa_q2_to_entity(const qa_q2_wire_entity *wire, qa_q2_entity_state *out) {
  memset(out, 0, sizeof *out);
  out->number = wire->number;
  out->origin = vec_of(wire->origin);
  out->angles = vec_of(wire->angles);
  out->old_origin = vec_of(wire->old_origin);
  out->model_indexes[0] = wire->modelindex;
  out->model_indexes[1] = wire->modelindex2;
  out->model_indexes[2] = wire->modelindex3;
  out->model_indexes[3] = wire->modelindex4;
  out->frame = wire->frame;
  out->skin = wire->skinnum;
  out->effects = (double)(uint32_t)(int32_t)wire->effects;
  out->render_effects = (double)(uint32_t)(int32_t)wire->renderfx;
  out->solid = (double)(uint32_t)(int32_t)wire->solid;
  out->sound = wire->sound;
  out->event = wire->event;
}

void qa_q2_to_rerelease_entity(const qa_q2_wire_entity *wire,
                               qa_q2_rerelease_entity_state *out) {
  memset(out, 0, sizeof *out);
  qa_q2_to_entity(wire, &out->base);
  out->effects = (uint64_t)(uint32_t)(int32_t)wire->effects |
                 ((uint64_t)(uint32_t)(int32_t)wire->morefx << 32);
  out->alpha = wire->alpha;
  out->scale = wire->scale;
  out->instance_bits = wire->instance_bits;
  out->loop_volume = wire->loop_volume;
  out->loop_attenuation = wire->loop_attenuation;
  out->owner = wire->owner;
  out->old_frame = wire->old_frame;
}

static void player_shell(const qa_q2_player_view *view, double type, double flags,
                         double gravity, qa_q2_wire_player *out) {
  out->pmove.pm_type = type;
  out->pmove.pm_flags = flags;
  out->pmove.gravity = gravity;
  set_vec(out->viewangles, view->view_angles);
  set_vec(out->viewoffset, view->view_offset);
  set_vec(out->kick_angles, view->kick_angles);
  set_vec(out->gunangles, view->gun_angles);
  set_vec(out->gunoffset, view->gun_offset);
  out->gunindex = view->gun_index;
  out->gunframe = view->gun_frame;
  out->fov = view->fov;
  out->rdflags = view->render_flags;
}

static void copy_stats(qa_q2_wire_player *out, const double *values, size_t count) {
  if (count > QA_Q2_MAX_STATS_STORAGE) {
    qa_range_error("Q2 wire has at most 64 player stats");
  }
  for (size_t i = 0; i < count; i++) out->stats[i] = (int16_t)(int32_t)values[i];
}

void qa_q2_from_player_classic(const qa_q2_player_state *state, qa_q2_wire_player *out) {
  qa_q2_wire_player_init(out);
  player_shell(&state->view, state->movement.type, state->movement.flags,
               state->movement.gravity, out);
  copy_stats(out, state->view.stats.values, state->view.stats.count);
  for (int i = 0; i < 3; i++) {
    out->pmove.origin[i] = (int32_t)state->movement.origin_eighths.values[i];
    out->pmove.velocity[i] = (int32_t)state->movement.velocity_eighths.values[i];
    out->pmove.delta_angles[i] = (int16_t)(int32_t)state->movement.delta_angle_shorts.values[i];
  }
  out->pmove.pm_time = state->movement.time_eight_milliseconds;
  set_color(out->blend, state->blend);
}

void qa_q2_from_player_rerelease(const qa_q2_rerelease_player_state *state,
                                 qa_q2_wire_player *out) {
  qa_q2_wire_player_init(out);
  player_shell(&state->view, state->movement.type, state->movement.flags,
               state->movement.gravity, out);
  copy_stats(out, state->view.stats.values, state->view.stats.count);
  set_vec(out->pmove.delta_angles_f, state->movement.delta_angles);
  out->pmove.delta_angle_float = true;
  set_vec(out->pmove.origin_f, state->movement.origin);
  set_vec(out->pmove.velocity_f, state->movement.velocity);
  out->pmove.pm_time = state->movement.time_milliseconds;
  out->pmove.viewheight = state->movement.view_height;
  out->pmove.delta_angles[0] = (int16_t)qa_q2_angle2short(state->movement.delta_angles.x);
  out->pmove.delta_angles[1] = (int16_t)qa_q2_angle2short(state->movement.delta_angles.y);
  out->pmove.delta_angles[2] = (int16_t)qa_q2_angle2short(state->movement.delta_angles.z);
  set_color(out->blend, state->screen_blend);
  set_color(out->damage_blend, state->damage_blend);
  out->gunskin = state->gun_skin;
  out->gunrate = state->gun_rate;
  out->team_id = state->team_id;
}

static void player_view_of(const qa_q2_wire_player *wire, double *storage,
                           size_t stat_count, qa_q2_player_view *out) {
  out->view_angles = vec_of(wire->viewangles);
  out->view_offset = vec_of(wire->viewoffset);
  out->kick_angles = vec_of(wire->kick_angles);
  out->gun_angles = vec_of(wire->gunangles);
  out->gun_offset = vec_of(wire->gunoffset);
  out->gun_index = wire->gunindex;
  out->gun_frame = wire->gunframe;
  out->fov = wire->fov;
  out->render_flags = wire->rdflags;
  for (size_t i = 0; i < stat_count; i++) storage[i] = (double)wire->stats[i];
  out->stats.values = storage;
  out->stats.count = stat_count;
}

void qa_q2_to_player(const qa_q2_wire_player *wire, double stats_storage[64],
                     qa_q2_player_state *out) {
  memset(out, 0, sizeof *out);
  /* TS playerView carries all 64 stats, then overrides with 32. */
  player_view_of(wire, stats_storage, 32, &out->view);
  out->movement.type = wire->pmove.pm_type;
  for (int i = 0; i < 3; i++) {
    out->movement.origin_eighths.values[i] = (double)wire->pmove.origin[i];
    out->movement.velocity_eighths.values[i] = (double)wire->pmove.velocity[i];
    out->movement.delta_angle_shorts.values[i] = (double)wire->pmove.delta_angles[i];
  }
  out->movement.flags = wire->pmove.pm_flags;
  out->movement.time_eight_milliseconds = wire->pmove.pm_time;
  out->movement.gravity = wire->pmove.gravity;
  out->blend = color_of(wire->blend);
}

void qa_q2_to_rerelease_player(const qa_q2_wire_player *wire,
                               double stats_storage[64],
                               qa_q2_rerelease_player_state *out) {
  memset(out, 0, sizeof *out);
  player_view_of(wire, stats_storage, 64, &out->view);
  out->movement.type = wire->pmove.pm_type;
  out->movement.origin = vec_of(wire->pmove.origin_f);
  out->movement.velocity = vec_of(wire->pmove.velocity_f);
  out->movement.flags = wire->pmove.pm_flags;
  out->movement.time_milliseconds = wire->pmove.pm_time;
  out->movement.gravity = wire->pmove.gravity;
  if (wire->pmove.delta_angle_float) {
    out->movement.delta_angles = vec_of(wire->pmove.delta_angles_f);
  } else {
    out->movement.delta_angles.x = qa_q2_short2angle((double)wire->pmove.delta_angles[0]);
    out->movement.delta_angles.y = qa_q2_short2angle((double)wire->pmove.delta_angles[1]);
    out->movement.delta_angles.z = qa_q2_short2angle((double)wire->pmove.delta_angles[2]);
  }
  out->movement.view_height = wire->pmove.viewheight;
  out->gun_skin = wire->gunskin;
  out->gun_rate = wire->gunrate;
  out->screen_blend = color_of(wire->blend);
  out->damage_blend = color_of(wire->damage_blend);
  out->team_id = wire->team_id;
}

void qa_q2_from_command_classic(const qa_q2_user_command *command,
                                qa_q2_wire_usercmd *out) {
  qa_q2_wire_usercmd_init(out);
  out->msec = command->milliseconds;
  out->forwardmove = command->forward_move;
  out->sidemove = command->side_move;
  out->buttons = command->buttons;
  for (int i = 0; i < 3; i++) {
    out->angles[i] = (int16_t)(int32_t)command->angle_shorts.values[i];
  }
  out->upmove = command->up_move;
  out->impulse = command->impulse;
  out->lightlevel = command->light_level;
}

void qa_q2_from_command_rerelease(const qa_q2_rerelease_user_command *command,
                                  qa_q2_wire_usercmd *out) {
  qa_q2_wire_usercmd_init(out);
  out->msec = command->milliseconds;
  out->forwardmove = command->forward_move;
  out->sidemove = command->side_move;
  out->buttons = command->buttons;
  out->angles[0] = (int16_t)qa_q2_angle2short(command->angles.x);
  out->angles[1] = (int16_t)qa_q2_angle2short(command->angles.y);
  out->angles[2] = (int16_t)qa_q2_angle2short(command->angles.z);
  out->server_frame = command->server_frame;
}

void qa_q2_to_command(const qa_q2_wire_usercmd *wire, qa_q2_user_command *out) {
  memset(out, 0, sizeof *out);
  out->milliseconds = wire->msec;
  for (int i = 0; i < 3; i++) out->angle_shorts.values[i] = (double)wire->angles[i];
  out->forward_move = wire->forwardmove;
  out->side_move = wire->sidemove;
  out->up_move = wire->upmove;
  out->buttons = wire->buttons;
  out->impulse = wire->impulse;
  out->light_level = wire->lightlevel;
}

void qa_q2_to_rerelease_command(const qa_q2_wire_usercmd *wire, double server_frame,
                                qa_q2_rerelease_user_command *out) {
  memset(out, 0, sizeof *out);
  out->milliseconds = wire->msec;
  out->angles.x = qa_q2_short2angle((double)wire->angles[0]);
  out->angles.y = qa_q2_short2angle((double)wire->angles[1]);
  out->angles.z = qa_q2_short2angle((double)wire->angles[2]);
  out->forward_move = wire->forwardmove;
  out->side_move = wire->sidemove;
  out->buttons = wire->buttons;
  out->server_frame = server_frame;
}
