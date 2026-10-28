/* Port of src/app/bootstrap/simulation/prediction/presentation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/prediction/presentation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/arsenal_intent.h"
#include "qa/app/bootstrap/simulation/prediction/runtime.h"
#include "qa/app/bootstrap/simulation/prediction/step.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/prediction.h"

static double axis_value(double value, double speed) {
  double scaled = value * 127 / speed;
  if (scaled > 127) scaled = 127;
  if (scaled < -127) scaled = -127;
  return trunc(scaled);
}
static double angle_word(double degrees) { return (double)(((long long)trunc(degrees * 65536 / 360)) & 65535); }

qa_q3_base_user_command qa_app_presentation_source_command(qa_actor_command input, double milliseconds, double weapon) {
  qa_user_command c = input.command;
  qa_q3_base_user_command out; memset(&out, 0, sizeof out);
  if (c.kind == QA_USER_COMMAND_Q3) {
    out.server_time = c.as.q3.server_time_milliseconds;
    out.angles = (qa_vec3){c.as.q3.angle_words.values[0], c.as.q3.angle_words.values[1], c.as.q3.angle_words.values[2]};
    out.buttons = input.arsenal == NULL ? c.as.q3.buttons
        : (double)((((long long)c.as.q3.buttons) & ~4) | (input.arsenal->use_holdable ? 4 : 0));
    out.weapon = input.arsenal == NULL ? c.as.q3.weapon : weapon;
    out.forwardmove = c.as.q3.forward_move; out.rightmove = c.as.q3.right_move; out.upmove = c.as.q3.up_move;
    return out;
  }
  qa_vec3 angles;
  double forward = 0, side = 0, up = 0, buttons = 0;
  if (c.kind == QA_USER_COMMAND_Q2) {
    angles = (qa_vec3){c.as.q2.angle_shorts.values[0], c.as.q2.angle_shorts.values[1], c.as.q2.angle_shorts.values[2]};
    forward = c.as.q2.forward_move; side = c.as.q2.side_move; up = c.as.q2.up_move; buttons = c.as.q2.buttons;
  } else {
    qa_vec3 a = c.kind == QA_USER_COMMAND_Q1 ? c.as.q1.view_angles
        : c.kind == QA_USER_COMMAND_QW ? c.as.qw.angles : c.as.q2_rerelease.angles;
    angles = (qa_vec3){angle_word(a.x), angle_word(a.y), angle_word(a.z)};
    if (c.kind == QA_USER_COMMAND_Q1) { forward = c.as.q1.forward_move; side = c.as.q1.side_move; up = c.as.q1.up_move; buttons = c.as.q1.buttons; }
    else if (c.kind == QA_USER_COMMAND_QW) { forward = c.as.qw.forward_move; side = c.as.qw.side_move; up = c.as.qw.up_move; buttons = c.as.qw.buttons; }
    else { forward = c.as.q2_rerelease.forward_move; side = c.as.q2_rerelease.side_move; buttons = c.as.q2_rerelease.buttons; }
  }
  double speed = (c.kind == QA_USER_COMMAND_Q1 || c.kind == QA_USER_COMMAND_QW) ? 320 : 200;
  out.server_time = trunc(milliseconds); out.angles = angles;
  out.buttons = (double)((((long long)buttons) & 1) | (input.arsenal && input.arsenal->use_holdable ? 4 : 0));
  out.weapon = weapon;
  out.forwardmove = axis_value(forward, speed); out.rightmove = axis_value(side, speed);
  out.upmove = c.kind == QA_USER_COMMAND_Q2_RERELEASE
      ? ((((long long)buttons) & 8) != 0 ? 127 : (((long long)buttons) & 16) != 0 ? -127 : 0)
      : (c.kind == QA_USER_COMMAND_Q1 || c.kind == QA_USER_COMMAND_QW) && (((long long)buttons) & 2) != 0 ? 127 : axis_value(up, speed);
  return out;
}

typedef struct snapshot_entry { double time; qa_app_movement_prediction_snapshot snapshot; } snapshot_entry;
typedef struct command_entry { double time; qa_app_prediction_command command; } command_entry;
typedef struct state_entry { const qa_q3_base_player_state_record *ps; qa_app_movement_prediction_snapshot snapshot; } state_entry;

struct qa_app_presentation_prediction_adapter {
  qa_app_presentation_prediction_options options;
  qa_q2_rerelease_movement_context rerelease_movement;
  snapshot_entry snapshots[64]; size_t snapshot_count;
  command_entry commands[64]; size_t command_count;
  state_entry states[16]; size_t state_count;
  qa_app_movement_prediction_snapshot latest;
};

static const char *state_kind_name(qa_movement_state_kind kind) {
  switch (kind) {
    case QA_MOVEMENT_STATE_Q1: return "q1-netquake";
    case QA_MOVEMENT_STATE_QW: return "q1-quakeworld";
    case QA_MOVEMENT_STATE_Q2: return "q2-classic";
    case QA_MOVEMENT_STATE_Q2_RERELEASE: return "q2-rerelease";
    case QA_MOVEMENT_STATE_Q3: return "q3";
  }
  return "";
}
static bool same_session(qa_session_id a, qa_session_id b) { return a.token == b.token; }

qa_app_presentation_prediction_adapter *qa_app_presentation_prediction_adapter_create(
    qa_app_presentation_prediction_options options) {
  if (!same_session(options.movement.probe.actor.id.session, options.movement.seat.session))
    qa_range_error("Prediction actor and seat belong to different sessions");
  qa_app_presentation_prediction_adapter *adapter = calloc(1, sizeof *adapter);
  if (!adapter) return NULL;
  adapter->options = options;
  adapter->latest = qa_app_copy_prediction_snapshot(options.initial);
  qa_app_presentation_prediction_adapter_capture(adapter, adapter->latest);
  return adapter;
}
qa_app_presentation_prediction_adapter *qa_app_create_presentation_movement_host(
    qa_app_presentation_prediction_options options) {
  return qa_app_presentation_prediction_adapter_create(options);
}
void qa_app_presentation_prediction_adapter_destroy(qa_app_presentation_prediction_adapter *adapter) {
  if (!adapter) return;
  for (size_t i = 0; i < adapter->snapshot_count; i++) qa_app_movement_prediction_snapshot_destroy(&adapter->snapshots[i].snapshot);
  for (size_t i = 0; i < adapter->state_count; i++) qa_app_movement_prediction_snapshot_destroy(&adapter->states[i].snapshot);
  qa_app_movement_prediction_snapshot_destroy(&adapter->latest);
  free(adapter);
}
const char *qa_app_presentation_prediction_adapter_command_timing(const qa_app_presentation_prediction_adapter *adapter) {
  return adapter->options.movement.probe.profile.kind && strcmp(adapter->options.movement.probe.profile.kind, "q3") == 0 ? "q3" : "provider";
}
void qa_app_presentation_prediction_adapter_capture(qa_app_presentation_prediction_adapter *adapter,
    qa_app_movement_prediction_snapshot snapshot) {
  if (strcmp(state_kind_name(snapshot.state.kind), adapter->options.movement.probe.profile.kind) != 0)
    qa_range_error("Cgame prediction snapshot changed selected movement");
  qa_app_movement_prediction_snapshot copy = qa_app_copy_prediction_snapshot(snapshot);
  /* latest aliases the stored snapshot in TS; keep one owned copy stored. */
  for (size_t i = 0; i < adapter->state_count; i++) qa_app_movement_prediction_snapshot_destroy(&adapter->states[i].snapshot);
  adapter->state_count = 0;
  size_t existing = adapter->snapshot_count;
  for (size_t i = 0; i < adapter->snapshot_count; i++)
    if (adapter->snapshots[i].time == copy.command_time_milliseconds) existing = i;
  if (existing < adapter->snapshot_count) {
    qa_app_movement_prediction_snapshot_destroy(&adapter->snapshots[existing].snapshot);
    adapter->snapshots[existing].snapshot = copy;
  } else {
    if (adapter->snapshot_count >= 64) {
      qa_app_movement_prediction_snapshot_destroy(&adapter->snapshots[0].snapshot);
      memmove(adapter->snapshots, adapter->snapshots + 1, 63 * sizeof *adapter->snapshots);
      adapter->snapshot_count = 63;
    }
    adapter->snapshots[adapter->snapshot_count].time = copy.command_time_milliseconds;
    adapter->snapshots[adapter->snapshot_count].snapshot = copy;
    adapter->snapshot_count++;
    existing = adapter->snapshot_count - 1;
  }
  qa_app_movement_prediction_snapshot_destroy(&adapter->latest);
  adapter->latest = qa_app_copy_prediction_snapshot(adapter->snapshots[existing].snapshot);
}

static qa_q3_arsenal_controls resolve_controls(qa_app_presentation_prediction_adapter *adapter,
    qa_arsenal_state arsenal, const qa_arsenal_intent *intent, qa_user_command command, const char *product) {
  if (adapter->options.movement.probe.resolve_controls)
    return adapter->options.movement.probe.resolve_controls(adapter->options.movement.probe.host_ctx, arsenal, intent, command, product);
  return qa_app_sim_resolve_q3_arsenal_controls(arsenal, intent, command, product);
}

qa_q3_base_user_command qa_app_presentation_prediction_adapter_submit(
    qa_app_presentation_prediction_adapter *adapter, qa_actor_command input, double milliseconds) {
  if (!qa_same_actor(input.actor, adapter->options.movement.probe.actor.id))
    qa_range_error("Cgame prediction command belongs to another actor");
  qa_app_movement_prediction_snapshot latest = adapter->latest;
  bool arsenal_q3 = latest.arsenal.state.kind && strcmp(latest.arsenal.state.kind, "q3") == 0;
  double weapon = arsenal_q3 && latest.has_q3_arsenal
      ? resolve_controls(adapter, latest.arsenal, input.arsenal, input.command, latest.q3_arsenal.product).requested_weapon : 0;
  if (input.arsenal) {
    if (!input.arsenal->provider || !latest.arsenal.provider || strcmp(input.arsenal->provider, latest.arsenal.provider) != 0)
      qa_range_error("Arsenal command belongs to a different provider");
    if (arsenal_q3 && !latest.has_q3_arsenal) qa_range_error("Cgame has no selected arsenal owner for this intent");
  }
  qa_q3_base_user_command presented = qa_app_presentation_source_command(input, milliseconds, weapon);
  qa_q3_base_user_command source = presented;
  if (latest.state.kind == QA_MOVEMENT_STATE_Q3 && adapter->options.relative_source_command) {
    qa_vec3 delta = {latest.state.as.q3.delta_angle_words.values[0], latest.state.as.q3.delta_angle_words.values[1],
                     latest.state.as.q3.delta_angle_words.values[2]};
    source = adapter->options.relative_source_command(adapter->options.host_ctx, input.source, input.command.kind, presented, delta);
  }
  qa_app_prediction_command stored; memset(&stored, 0, sizeof stored);
  stored.sequence = input.sequence; stored.time_milliseconds = source.server_time;
  stored.command = qa_app_copy_prediction_command(input.command);
  if (input.arsenal) { stored.has_arsenal = true; stored.arsenal = *input.arsenal; }
  size_t existing = adapter->command_count;
  for (size_t i = 0; i < adapter->command_count; i++) if (adapter->commands[i].time == source.server_time) existing = i;
  if (existing < adapter->command_count) adapter->commands[existing].command = stored;
  else {
    if (adapter->command_count >= 64) {
      memmove(adapter->commands, adapter->commands + 1, 63 * sizeof *adapter->commands);
      adapter->command_count = 63;
    }
    adapter->commands[adapter->command_count].time = source.server_time;
    adapter->commands[adapter->command_count].command = stored;
    adapter->command_count++;
  }
  return source;
}

static state_entry *find_state(qa_app_presentation_prediction_adapter *adapter, const qa_q3_base_player_state_record *ps) {
  for (size_t i = 0; i < adapter->state_count; i++) if (adapter->states[i].ps == ps) return &adapter->states[i];
  return NULL;
}
static qa_app_movement_prediction_snapshot seed_snapshot(qa_app_presentation_prediction_adapter *adapter,
    qa_q3_base_player_state_record *ps) {
  state_entry *prior = find_state(adapter, ps);
  qa_app_movement_prediction_snapshot baseline;
  bool found = false;
  if (strcmp(adapter->options.movement.probe.profile.kind, "q3") == 0) { baseline = adapter->latest; found = true; }
  else if (prior && prior->snapshot.command_time_milliseconds == ps->command_time) { baseline = prior->snapshot; found = true; }
  else {
    for (size_t i = 0; i < adapter->snapshot_count; i++)
      if (adapter->snapshots[i].time == ps->command_time) { baseline = adapter->snapshots[i].snapshot; found = true; }
  }
  if (!found) qa_range_error("No selected movement snapshot for source command time %g", ps->command_time);
  return qa_app_read_prediction_source_state(baseline, ps, adapter->options.entities);
}

typedef struct scene_ctx {
  qa_app_presentation_prediction_adapter *adapter;
  qa_q3_base_player_state_record *ps;
  qa_q3_pres_movement_options movement;
} scene_ctx;
static qa_trace_result scene_trace(void *v, qa_trace_query query) {
  scene_ctx *ctx = (scene_ctx *)v;
  qa_scene_queries base = ctx->adapter->options.movement.probe.scene;
  if (!query.policy.kind || strcmp(query.policy.kind, "q3") != 0 || query.target.is_model) return base.trace(base.ctx, query);
  qa_bounds bounds = query.shape.kind && strcmp(query.shape.kind, "point") == 0
      ? (qa_bounds){{0, 0, 0}, {0, 0, 0}} : query.shape.bounds;
  qa_q3_pres_movement_trace traced = ctx->movement.trace(ctx->movement.trace_ctx, query.start, query.end, bounds,
      ctx->ps->client_num, query.policy.contents_mask);
  qa_plane plane = traced.result.has_plane ? traced.result.plane : (qa_plane){{0, 0, 0}, 0};
  qa_trace_result result; memset(&result, 0, sizeof result);
  result.kind = "q3"; result.fraction = traced.result.fraction; result.end = traced.result.end;
  result.all_solid = traced.result.solidity && strcmp(traced.result.solidity, "all-solid") == 0;
  result.start_solid = !(traced.result.solidity && strcmp(traced.result.solidity, "clear") == 0);
  result.source_plane.plane = plane;
  result.source_plane.type = plane.normal.x == 1 ? 0 : plane.normal.y == 1 ? 1 : plane.normal.z == 1 ? 2 : 3;
  result.source_plane.signbits = (double)((plane.normal.x < 0 ? 1 : 0) | (plane.normal.y < 0 ? 2 : 0) | (plane.normal.z < 0 ? 4 : 0));
  result.contact.has_plane = traced.result.has_plane;
  result.contact.plane = plane;
  result.hit = qa_app_prediction_source_hit(traced.entity_num, ctx->adapter->options.entities);
  result.contents = traced.result.contents; result.has_surface_flags = true; result.surface_flags = traced.result.surface_flags;
  return result;
}
static qa_point_contents_result scene_contents(void *v, qa_point_contents_query query) {
  scene_ctx *ctx = (scene_ctx *)v;
  qa_scene_queries base = ctx->adapter->options.movement.probe.scene;
  if (query.policy.kind && strcmp(query.policy.kind, "q3") == 0 && !query.target.is_model) {
    qa_point_contents_result result; memset(&result, 0, sizeof result);
    result.kind = "q3"; result.contents = ctx->movement.point_contents(ctx->movement.trace_ctx, query.point, ctx->ps->client_num);
    return result;
  }
  return base.point_contents(base.ctx, query);
}
static qa_leaf_query_result scene_leaves(void *v, qa_bounds bounds, double limit) {
  scene_ctx *ctx = (scene_ctx *)v;
  qa_scene_queries base = ctx->adapter->options.movement.probe.scene;
  return base.box_leaves(base.ctx, bounds, limit);
}
static bool scene_connected(void *v, double first, double second) {
  scene_ctx *ctx = (scene_ctx *)v;
  qa_scene_queries base = ctx->adapter->options.movement.probe.scene;
  return base.areas_connected(base.ctx, first, second);
}
static bool scene_visible(void *v, double from, double to, const char *kind) {
  scene_ctx *ctx = (scene_ctx *)v;
  qa_scene_queries base = ctx->adapter->options.movement.probe.scene;
  return base.cluster_visible(base.ctx, from, to, kind);
}

qa_bounds qa_app_presentation_prediction_adapter_move_player(qa_app_presentation_prediction_adapter *adapter,
    qa_q3_base_player_state_record *ps, qa_q3_base_user_command command, qa_q3_pres_movement_options movement) {
  qa_app_movement_prediction_snapshot seed = seed_snapshot(adapter, ps);
  double key = movement.has_original_server_time ? movement.original_server_time : command.server_time;
  command_entry *original = NULL;
  for (size_t i = 0; i < adapter->command_count; i++) if (adapter->commands[i].time == key) original = &adapter->commands[i];
  qa_app_prediction_command entry; memset(&entry, 0, sizeof entry);
  if (strcmp(adapter->options.movement.probe.profile.kind, "q3") != 0) {
    if (!original) qa_range_error("No original selected command for cgame time %g", command.server_time);
    entry = original->command;
  } else {
    entry.sequence = original ? original->command.sequence : command.server_time;
    entry.time_milliseconds = command.server_time;
    if (original && original->command.has_arsenal) { entry.has_arsenal = true; entry.arsenal = original->command.arsenal; }
    entry.command.kind = QA_USER_COMMAND_Q3;
    entry.command.as.q3.server_time_milliseconds = command.server_time;
    entry.command.as.q3.angle_words.values[0] = command.angles.x;
    entry.command.as.q3.angle_words.values[1] = command.angles.y;
    entry.command.as.q3.angle_words.values[2] = command.angles.z;
    entry.command.as.q3.buttons = command.buttons; entry.command.as.q3.weapon = command.weapon;
    entry.command.as.q3.forward_move = command.forwardmove; entry.command.as.q3.right_move = command.rightmove;
    entry.command.as.q3.up_move = command.upmove;
  }
  scene_ctx sctx = {adapter, ps, movement};
  qa_scene_queries scene = adapter->options.movement.probe.scene;
  if (strcmp(adapter->options.movement.probe.profile.kind, "q3") == 0) {
    scene.ctx = &sctx; scene.trace = scene_trace; scene.point_contents = scene_contents;
    scene.box_leaves = scene_leaves; scene.areas_connected = scene_connected; scene.cluster_visible = scene_visible;
  }
  state_entry *prior = find_state(adapter, ps);
  qa_app_prediction_step_options options; memset(&options, 0, sizeof options);
  options.scene = scene; options.rerelease_movement = &adapter->rerelease_movement;
  options.has_fixed_milliseconds = movement.has_fixed_msec; options.fixed_milliseconds = movement.fixed_msec;
  options.no_footsteps = movement.no_footsteps; options.gauntlet_hit = movement.gauntlet_hit;
  options.has_trace_mask = true; options.trace_mask = movement.trace_mask;
  options.first_command = !prior || prior->snapshot.command_time_milliseconds != ps->command_time;
  qa_app_prediction_step_result output = qa_app_predict_movement_command(&adapter->options.movement.probe, seed, entry, options, NULL);
  qa_app_movement_prediction_snapshot_destroy(&seed);
  qa_app_write_prediction_source_state(ps, output.player, adapter->options.entities);
  for (size_t i = 0; i < output.result.effect_count; i++) {
    qa_ordered_movement_effect effect = output.result.effects[i];
    if (effect.effect.kind == QA_MOVEMENT_EFFECT_EVENT && effect.effect.value.provider
        && strncmp(effect.effect.value.provider, "q3:", 3) == 0 && adapter->options.publish_event
        && adapter->options.publish_event(adapter->options.host_ctx, output.player.animation.state.kind, effect.effect.value.event))
      qa_q3_base_player_state_record_add_event(ps, effect.effect.value.event, effect.effect.value.parameter);
  }
  qa_bounds bounds = output.result.bounds;
  state_entry *entry_state = find_state(adapter, ps);
  if (!entry_state) {
    if (adapter->state_count >= 16) {
      qa_app_movement_prediction_snapshot_destroy(&adapter->states[0].snapshot);
      memmove(adapter->states, adapter->states + 1, 15 * sizeof *adapter->states);
      adapter->state_count = 15;
    }
    entry_state = &adapter->states[adapter->state_count++];
    entry_state->ps = ps;
  } else qa_app_movement_prediction_snapshot_destroy(&entry_state->snapshot);
  entry_state->snapshot = qa_app_copy_prediction_snapshot(output.player);
  qa_app_movement_prediction_snapshot_destroy(&output.player);
  return bounds;
}

void qa_app_presentation_prediction_adapter_update_view_angles(qa_app_presentation_prediction_adapter *adapter,
    qa_q3_base_player_state_record *ps, qa_q3_base_user_command command) {
  qa_app_movement_prediction_snapshot seed = seed_snapshot(adapter, ps);
  if (seed.state.kind != QA_MOVEMENT_STATE_Q3) {
    command_entry *original = NULL;
    for (size_t i = 0; i < adapter->command_count; i++) if (adapter->commands[i].time == command.server_time) original = &adapter->commands[i];
    if (original) {
      qa_user_command cmd = original->command.command;
      if (cmd.kind == QA_USER_COMMAND_Q1 && seed.state.kind == QA_MOVEMENT_STATE_Q1)
        ps->viewangles = seed.state.as.q1.fix_angle ? seed.state.as.q1.view_angles : cmd.as.q1.view_angles;
      else if (cmd.kind == QA_USER_COMMAND_QW) ps->viewangles = cmd.as.qw.angles;
      else if (cmd.kind == QA_USER_COMMAND_Q2_RERELEASE && seed.state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
        ps->viewangles.x = cmd.as.q2_rerelease.angles.x + seed.state.as.q2_rerelease.delta_angles.x;
        ps->viewangles.y = cmd.as.q2_rerelease.angles.y + seed.state.as.q2_rerelease.delta_angles.y;
        ps->viewangles.z = cmd.as.q2_rerelease.angles.z + seed.state.as.q2_rerelease.delta_angles.z;
      } else if (cmd.kind == QA_USER_COMMAND_Q2 && seed.state.kind == QA_MOVEMENT_STATE_Q2) {
        for (int i = 0; i < 3; i++) {
          double value = (double)((((long long)(cmd.as.q2.angle_shorts.values[i] + seed.state.as.q2.delta_angle_shorts.values[i])) << 16) >> 16) * 360.0 / 65536.0;
          if (i == 0) ps->viewangles.x = value; else if (i == 1) ps->viewangles.y = value; else ps->viewangles.z = value;
        }
      }
    }
    qa_app_movement_prediction_snapshot_destroy(&seed);
    return;
  }
  qa_q3_user_command q3_command; memset(&q3_command, 0, sizeof q3_command);
  q3_command.server_time_milliseconds = command.server_time;
  q3_command.angle_words.values[0] = command.angles.x; q3_command.angle_words.values[1] = command.angles.y;
  q3_command.angle_words.values[2] = command.angles.z;
  q3_command.buttons = command.buttons; q3_command.weapon = command.weapon;
  q3_command.forward_move = command.forwardmove; q3_command.right_move = command.rightmove; q3_command.up_move = command.upmove;
  qa_q3_movement_state state = qa_q3_update_prediction_view(seed.state.as.q3, qa_q3_base_player_state_record_health(ps), q3_command);
  ps->viewangles = state.view_angles;
  ps->delta_angles = (qa_vec3){state.delta_angle_words.values[0], state.delta_angle_words.values[1], state.delta_angle_words.values[2]};
  qa_app_movement_prediction_snapshot_destroy(&seed);
}

static qa_bounds host_move(void *v, qa_q3_pres_source_player_state *state, qa_q3_pres_user_command command, qa_q3_pres_movement_options options) {
  return qa_app_presentation_prediction_adapter_move_player((qa_app_presentation_prediction_adapter *)v, state, command, options);
}
static void host_view(void *v, qa_q3_pres_source_player_state *state, qa_q3_pres_user_command command) {
  qa_app_presentation_prediction_adapter_update_view_angles((qa_app_presentation_prediction_adapter *)v, state, command);
}
qa_q3_pres_movement_host qa_app_presentation_prediction_adapter_host(qa_app_presentation_prediction_adapter *adapter) {
  qa_q3_pres_movement_host host; memset(&host, 0, sizeof host);
  host.command_timing = qa_app_presentation_prediction_adapter_command_timing(adapter);
  host.ctx = adapter; host.move_player = host_move; host.update_view_angles = host_view;
  return host;
}

struct qa_app_simulation_prediction_host {
  qa_app_simulation_prediction_source source;
  qa_actor_id actor;
  qa_app_presentation_prediction_adapter *adapter;
};
static bool sim_actor_at(void *v, double number, qa_actor_id *out) {
  qa_app_simulation_prediction_host *host = (qa_app_simulation_prediction_host *)v;
  return host->source.actor_at_slot && host->source.actor_at_slot(host->source.ctx, number, out);
}
static bool sim_number_of(void *v, qa_actor_id actor, double *out) {
  qa_app_simulation_prediction_host *host = (qa_app_simulation_prediction_host *)v;
  return host->source.slot_of_actor && host->source.slot_of_actor(host->source.ctx, actor, out);
}
static bool sim_is_brush(void *v, qa_trace_hit hit) {
  qa_app_simulation_prediction_host *host = (qa_app_simulation_prediction_host *)v;
  return hit.kind == QA_TRACE_HIT_WORLD
      || (hit.kind == QA_TRACE_HIT_ACTOR && host->source.actor_is_inline_model && host->source.actor_is_inline_model(host->source.ctx, hit.actor));
}
static qa_app_movement_prediction_snapshot sim_capture(qa_app_simulation_prediction_host *host, const qa_q3_base_player_state_record *ps, bool has_ps) {
  if (!has_ps || !ps) qa_range_error("Prediction snapshot has no matching source client");
  qa_app_simulation_movement_player_view player; memset(&player, 0, sizeof player);
  if (!host->source.movement_player || !host->source.movement_player(host->source.ctx, host->actor, &player))
    qa_range_error("Cgame prediction requires an admitted source player");
  qa_app_movement_prediction_snapshot base; memset(&base, 0, sizeof base);
  base.sequence = player.last_sequence; base.command_time_milliseconds = ps->command_time;
  base.state = player.state; base.arsenal = player.arsenal; base.animation = player.animation;
  if (player.has_source_environment) base.environment = player.source_environment;
  else {
    base.environment.health = qa_q3_base_player_state_record_health(ps);
    base.environment.gravity_multiplier = player.gravity_multiplier;
  }
  base.bounds = player.bounds; base.view_angles = player.view_angles; base.view_height = player.view_height;
  base.view_offset = (qa_vec3){0, 0, player.view_height};
  base.has_contact = true; base.contact_ground = player.ground;
  base.contact_water_level = player.water_level; base.contact_water_type = player.water_type;
  qa_app_prediction_source_entities entities; memset(&entities, 0, sizeof entities);
  entities.ctx = host; entities.actor_at = sim_actor_at; entities.number_of = sim_number_of;
  qa_app_movement_prediction_snapshot captured = qa_app_read_prediction_source_state(base, ps, entities);
  return captured;
}
qa_app_simulation_prediction_host *qa_app_create_simulation_prediction_host(
    qa_app_simulation_prediction_source source, qa_actor_id actor, qa_seat_id seat) {
  qa_app_simulation_movement_player_view player; memset(&player, 0, sizeof player);
  bool has_player = source.movement_player && source.movement_player(source.ctx, actor, &player);
  bool has_source = source.has_q3_source && source.has_q3_source(source.ctx);
  if (!has_player || !has_source) qa_range_error("Cgame prediction requires an admitted source player");
  qa_app_simulation_prediction_host *host = calloc(1, sizeof *host);
  if (!host) return NULL;
  host->source = source; host->actor = actor;
  const qa_q3_base_player_state_record *ps = source.source_client_state ? source.source_client_state(source.ctx, actor) : NULL;
  qa_app_movement_prediction_snapshot initial = sim_capture(host, ps, ps != NULL);
  qa_app_presentation_prediction_options options; memset(&options, 0, sizeof options);
  options.movement.probe.actor = player.actor;
  options.movement.seat = seat;
  options.movement.probe.recipe = source.recipe;
  options.movement.probe.profile = player.profile;
  if (player.profile.kind && (strcmp(player.profile.kind, "q1-netquake") == 0 || strcmp(player.profile.kind, "q1-quakeworld") == 0))
    options.movement.probe.profile.parameters.gravity = player.world_gravity;
  options.movement.probe.standing_bounds = player.standing_bounds;
  options.movement.probe.standing_view_height = player.character_is_q3 ? 26 : 22;
  options.movement.probe.scene = source.scene;
  options.movement.probe.host_ctx = host;
  options.movement.probe.is_brush = sim_is_brush;
  options.movement.probe.resolve_controls = source.resolve_controls;
  options.initial = initial;
  options.entities.ctx = host; options.entities.actor_at = sim_actor_at; options.entities.number_of = sim_number_of;
  options.host_ctx = source.host_ctx;
  options.relative_source_command = source.relative_source_command;
  options.publish_event = source.publish_event;
  host->adapter = qa_app_create_presentation_movement_host(options);
  if (!host->adapter) { qa_app_movement_prediction_snapshot_destroy(&initial); free(host); return NULL; }
  return host;
}
void qa_app_simulation_prediction_host_destroy(qa_app_simulation_prediction_host *host) {
  if (!host) return;
  qa_app_presentation_prediction_adapter_destroy(host->adapter);
  free(host);
}
qa_app_presentation_prediction_adapter *qa_app_simulation_prediction_host_adapter(qa_app_simulation_prediction_host *host) {
  return host->adapter;
}
void qa_app_simulation_prediction_host_capture_source(qa_app_simulation_prediction_host *host,
    const qa_q3_base_player_state_record *ps, bool has_ps) {
  qa_app_movement_prediction_snapshot captured = sim_capture(host, ps, has_ps);
  qa_app_presentation_prediction_adapter_capture(host->adapter, captured);
  qa_app_movement_prediction_snapshot_destroy(&captured);
}
