/* Port of src/movement/q2/index.ts. GPL-2.0-or-later. */
#include "qa/movement/q2/index.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/world.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/client_outputs.h"
#include "qa/movement/q2/classic.h"
#include "qa/movement/q2/dimensions.h"
#include "qa/movement/q2/math.h"

static const qa_vec3 zero_vec = {0, 0, 0};

static bool str_eq(const char *a, const char *b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}

static qa_vec3 vector_of(const qa_q2_vec3 source) {
  const qa_vec3 result = {source[0], source[1], source[2]};
  return result;
}

static void source_vector(qa_vec3 value, qa_q2_vec3 out) {
  out[0] = value.x;
  out[1] = value.y;
  out[2] = value.z;
}

static qa_bounds body_bounds_of(qa_trace_shape shape) {
  if (str_eq(shape.kind, "point")) {
    const qa_bounds zero_bounds = {{0, 0, 0}, {0, 0, 0}};
    return zero_bounds;
  }
  return shape.bounds;
}

static qa_q2_cplane source_plane(qa_bsp_plane value) {
  qa_q2_cplane plane;
  memset(&plane, 0, sizeof plane);
  source_vector(value.plane.normal, plane.normal);
  plane.dist = value.plane.distance;
  plane.type = value.type;
  plane.signbits = value.signbits;
  return plane;
}

static qa_bsp_plane scene_plane(qa_q2_cplane value) {
  qa_bsp_plane plane;
  memset(&plane, 0, sizeof plane);
  plane.plane.normal = vector_of(value.normal);
  plane.plane.distance = value.dist;
  plane.type = value.type;
  plane.signbits = value.signbits;
  return plane;
}

static void command_duration(double milliseconds) {
  if (!isfinite(milliseconds) || milliseconds != trunc(milliseconds) || milliseconds < 0 ||
      milliseconds > 255)
    qa_range_error("Quake II usercmd duration must fit its source byte");
}

/* TS traceAdapter: canonicalizes hit entities for the pmove call. */
typedef struct trace_adapter {
  const qa_movement_input *input;
  const qa_movement_services *services;
  const char *leaf_contents;
  qa_trace_hit *entities;
  size_t entity_count;
  size_t entity_capacity;
} trace_adapter;

static void adapter_init(trace_adapter *adapter, const qa_movement_input *input,
                         const qa_movement_services *services) {
  memset(adapter, 0, sizeof *adapter);
  adapter->input = input;
  adapter->services = services;
  adapter->leaf_contents = str_eq(input->kind, "q2-classic") ? "stored" : "merged";
}

static void adapter_destroy(trace_adapter *adapter) { free(adapter->entities); }

/* False is the TS null (hit kind "none"). */
static bool canonical(trace_adapter *adapter, qa_trace_hit hit, qa_trace_hit *out) {
  if (hit.kind == QA_TRACE_HIT_NONE) return false;
  for (size_t i = 0; i < adapter->entity_count; i++) {
    const qa_trace_hit candidate = adapter->entities[i];
    const bool prior = candidate.kind == QA_TRACE_HIT_WORLD && hit.kind == QA_TRACE_HIT_WORLD
                           ? candidate.model == hit.model
                           : candidate.kind == QA_TRACE_HIT_ACTOR &&
                                 hit.kind == QA_TRACE_HIT_ACTOR &&
                                 qa_same_actor(candidate.actor, hit.actor);
    if (prior) {
      *out = candidate;
      return true;
    }
  }
  if (adapter->entity_count == adapter->entity_capacity) {
    const size_t capacity = adapter->entity_capacity == 0 ? 8 : adapter->entity_capacity * 2;
    qa_trace_hit *grown = realloc(adapter->entities, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    adapter->entities = grown;
    adapter->entity_capacity = capacity;
  }
  adapter->entities[adapter->entity_count++] = hit;
  *out = hit;
  return true;
}

static qa_q2_trace adapter_trace(trace_adapter *adapter, const qa_q2_vec3 start,
                                 const qa_q2_vec3 mins, const qa_q2_vec3 maxs,
                                 const qa_q2_vec3 end, double mask, bool world_only) {
  const qa_movement_input *input = adapter->input;
  qa_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = vector_of(start);
  query.end = vector_of(end);
  if (str_eq(input->shape.kind, "point")) {
    query.shape.kind = "point";
  } else {
    query.shape.kind = input->shape.kind;
    query.shape.bounds.min = vector_of(mins);
    query.shape.bounds.max = vector_of(maxs);
  }
  query.target.is_model = world_only;
  query.target.model = 0;
  query.target.origin = zero_vec;
  query.target.angles = zero_vec;
  query.policy.kind = "q2";
  query.policy.contents_mask = mask;
  query.policy.leaf_contents = adapter->leaf_contents;
  query.numeric = input->profile.numeric;
  query.pass_actor = &input->actor.id;
  const qa_trace_result result = adapter->services->scene.trace(
      adapter->services->scene.ctx, query);
  if (!str_eq(result.kind, "q2"))
    qa_range_error("SceneQueries must return Q2 trace fields for a Q2 query policy");
  qa_q2_trace trace;
  memset(&trace, 0, sizeof trace);
  trace.allsolid = result.all_solid;
  trace.startsolid = result.start_solid;
  trace.fraction = result.fraction;
  source_vector(result.end, trace.endpos);
  trace.plane = source_plane(result.source_plane);
  trace.plane2 = result.has_secondary ? source_plane(result.secondary_plane) : qa_q2_plane();
  trace.has_surface = result.surface != NULL;
  if (trace.has_surface) {
    trace.surface = *result.surface;
    trace.surface_ref = result.surface;
  }
  trace.has_surface2 = result.has_secondary && result.secondary_surface != NULL;
  if (trace.has_surface2) {
    trace.surface2 = *result.secondary_surface;
    trace.surface2_ref = result.secondary_surface;
  }
  trace.contents = result.contents;
  trace.has_ent = canonical(adapter, result.hit, &trace.ent);
  trace.source = result;
  return trace;
}

static double adapter_point_contents(trace_adapter *adapter, const qa_q2_vec3 point) {
  const qa_movement_input *input = adapter->input;
  qa_point_contents_query query;
  memset(&query, 0, sizeof query);
  query.point = vector_of(point);
  query.target.is_model = false;
  query.policy.kind = "q2";
  query.policy.contents_mask = -1;
  query.policy.leaf_contents = adapter->leaf_contents;
  query.numeric = input->profile.numeric;
  query.pass_actor = &input->actor.id;
  const qa_point_contents_result result = adapter->services->scene.point_contents(
      adapter->services->scene.ctx, query);
  if (!str_eq(result.kind, "q2"))
    qa_range_error("SceneQueries must return Q2 contents for a Q2 query policy");
  return str_eq(adapter->leaf_contents, "stored") ? result.stored : result.merged;
}

static qa_q2_trace classic_trace_fn(void *data, qa_q2_vec3 start, qa_q2_vec3 mins,
                                    qa_q2_vec3 maxs, qa_q2_vec3 end) {
  return adapter_trace(data, start, mins, maxs, end, QA_Q2_MASK_CLASSIC_PLAYERSOLID, false);
}

static qa_q2_trace rerelease_trace_fn(void *data, qa_q2_vec3 start, qa_q2_vec3 mins,
                                      qa_q2_vec3 maxs, qa_q2_vec3 end, bool has_pass,
                                      qa_q2_movement_entity pass, double mask) {
  (void)has_pass;
  (void)pass;
  return adapter_trace(data, start, mins, maxs, end, mask, false);
}

static qa_q2_trace rerelease_clip_fn(void *data, qa_q2_vec3 start, qa_q2_vec3 mins,
                                     qa_q2_vec3 maxs, qa_q2_vec3 end, double mask) {
  return adapter_trace(data, start, mins, maxs, end, mask, true);
}

static double adapter_point_contents_fn(void *data, qa_q2_vec3 point) {
  return adapter_point_contents(data, point);
}

static qa_trace_result contact_trace(const qa_q2_trace *trace) {
  const qa_trace_result *result = &trace->source;
  if (!str_eq(result->kind, "q2"))
    qa_range_error("Q2 movement received a foreign trace representation");
  qa_trace_result contact = *result;
  contact.source_plane = scene_plane(trace->plane);
  if (trace->has_surface) {
    /* Adapter-built traces carry the scene-owned surface reference. */
    contact.surface = trace->surface_ref;
  } else {
    contact.surface = NULL;
  }
  if (result->contact.has_plane) {
    qa_bsp_plane plane = scene_plane(trace->plane);
    contact.contact.plane = plane.plane;
  }
  return contact;
}

typedef struct contact_list {
  qa_movement_contact *contacts;
  size_t count;
} contact_list;

static contact_list movement_contacts(const qa_q2_trace *traces, size_t trace_count) {
  contact_list list;
  memset(&list, 0, sizeof list);
  qa_trace_hit touched[QA_Q2_MAXTOUCH];
  size_t touched_count = 0;
  if (trace_count > 0) {
    list.contacts = malloc(trace_count * sizeof *list.contacts);
    if (list.contacts == NULL) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < trace_count; i++) {
    const qa_q2_trace *trace = &traces[i];
    if (!trace->has_ent) continue;
    bool seen = false;
    for (size_t j = 0; j < touched_count; j++)
      if (qa_q2_same_hit(touched[j], trace->ent)) seen = true;
    if (seen) continue;
    touched[touched_count++] = trace->ent;
    list.contacts[list.count].target = trace->ent;
    list.contacts[list.count].trace = contact_trace(trace);
    list.contacts[list.count].substep = 0;
    list.count++;
  }
  return list;
}

typedef struct effects_list {
  qa_ordered_movement_effect *effects;
  size_t count;
  size_t capacity;
} effects_list;

static void push_effect(effects_list *list, qa_ordered_movement_effect effect) {
  if (list->count == list->capacity) {
    const size_t capacity = list->capacity == 0 ? 4 : list->capacity * 2;
    qa_ordered_movement_effect *grown = realloc(list->effects, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    list->effects = grown;
    list->capacity = capacity;
  }
  list->effects[list->count++] = effect;
}

/* TS touchContacts. Returns false for the actor-removed continuation. */
static bool touch_contacts(const qa_movement_input *input,
                           const qa_movement_services *services, qa_movement_state initial,
                           const qa_movement_contact *contacts, size_t contact_count,
                           effects_list *effects, qa_movement_state *out_state) {
  qa_movement_state state = initial;
  const bool rerelease_input = str_eq(input->kind, "q2-rerelease");
  for (size_t i = 0; i < contact_count; i++) {
    const qa_movement_contact *contact = &contacts[i];
    if (contact->target.kind == QA_TRACE_HIT_NONE) continue;
    qa_ordered_movement_effect ordered;
    memset(&ordered, 0, sizeof ordered);
    ordered.substep = 0;
    ordered.sequence = (double)effects->count;
    ordered.time = input->frame.time;
    ordered.effect.kind = QA_MOVEMENT_EFFECT_TOUCH;
    ordered.effect.target = contact->target;
    ordered.effect.substep = 0;
    push_effect(effects, ordered);
    const bool has_rr_trace = rerelease_input && str_eq(contact->trace.kind, "q2");
    qa_movement_touch_contact touch;
    memset(&touch, 0, sizeof touch);
    touch.self = input->actor;
    touch.other = contact->target;
    qa_plane plane;
    qa_touch_surface surface;
    if (has_rr_trace) {
      touch.has_source_trace = true;
      touch.source_trace_trace = contact->trace;
      touch.source_trace_inverted = true;
      plane = contact->trace.source_plane.plane;
      touch.plane = &plane;
      if (contact->trace.surface != NULL) {
        surface.name = contact->trace.surface->name;
        surface.native_flags = contact->trace.surface->flags;
        surface.native_value = contact->trace.surface->value;
        touch.surface = &surface;
      }
    }
    const qa_movement_continuation continuation =
        services->touch(services->ctx, touch, state);
    if (!continuation.is_continue) return false;
    const qa_movement_state_kind expected =
        rerelease_input ? QA_MOVEMENT_STATE_Q2_RERELEASE : QA_MOVEMENT_STATE_Q2;
    if ((rerelease_input || str_eq(input->kind, "q2-classic")) &&
        continuation.state.kind != expected)
      qa_range_error("Touch changed Quake II movement family during one source command");
    if (!rerelease_input && !str_eq(input->kind, "q2-classic") &&
        continuation.state.kind != state.kind)
      qa_range_error("Touch changed Quake II movement family during one source command");
    state = continuation.state;
  }
  *out_state = state;
  return true;
}

static qa_q2_movement_result move_q2_classic_physics(qa_q2_movement_input input,
                                                     qa_movement_services services) {
  const qa_mod_client_movement_outputs *output = input.environment.client_outputs;
  qa_mod_client_movement_mode mode;
  const bool has_mode = qa_client_movement_mode(output, input.environment.health, &mode);
  const qa_user_command command = qa_client_stance_command(
      input.command, output != NULL && output->has_stance, output != NULL && output->stance);
  if (command.kind != QA_USER_COMMAND_Q2)
    qa_range_error("Client output changed movement dialect");
  input.command = command;
  command_duration(input.command.as.q2.milliseconds);

  qa_q2_movement_math vec_math;
  qa_q2_movement_math_init(&vec_math, &services.numeric, QA_Q2_MATH_CLASSIC);
  trace_adapter scene;
  adapter_init(&scene, &input, &services);
  const qa_fixed_movement_pose *pose = input.environment.pose;
  const qa_bounds body = pose != NULL ? pose->bounds : body_bounds_of(input.shape);

  qa_q2_classic_pmove pm;
  memset(&pm, 0, sizeof pm);
  pm.ctx = &scene;
  pm.s.pm_type = pose == NULL
                     ? (has_mode ? qa_client_movement_type(QA_MOVEMENT_STATE_Q2, mode)
                                 : input.state.as.q2.type)
                     : QA_Q2_PM_FREEZE;
  for (int i = 0; i < 3; i++) {
    pm.s.origin[i] = input.state.as.q2.origin_eighths.values[i];
    pm.s.velocity[i] = pose == NULL ? input.state.as.q2.velocity_eighths.values[i] : 0;
    pm.s.delta_angles[i] = input.state.as.q2.delta_angle_shorts.values[i];
    pm.cmd.angles[i] = input.command.as.q2.angle_shorts.values[i];
  }
  pm.s.pm_flags = input.state.as.q2.flags;
  pm.s.pm_time = input.state.as.q2.time_eight_milliseconds;
  pm.s.gravity = input.state.as.q2.gravity;
  pm.cmd.msec = input.command.as.q2.milliseconds;
  pm.cmd.forwardmove = input.command.as.q2.forward_move;
  pm.cmd.sidemove = input.command.as.q2.side_move;
  pm.cmd.upmove = input.command.as.q2.up_move;
  pm.cmd.buttons = input.command.as.q2.buttons;
  pm.cmd.impulse = input.command.as.q2.impulse;
  pm.cmd.lightlevel = input.command.as.q2.light_level;
  pm.snapinitial = input.profile.snap_initial;
  pm.numtouch = 0;
  qa_q2_math_vec3(&vec_math, 0, 0, 0, pm.viewangles);
  pm.viewheight = 0;
  source_vector(body.min, pm.mins);
  source_vector(body.max, pm.maxs);
  pm.character_bounds = body;
  pm.has_previous_bounds = true;
  pm.previous_bounds = input.current_bounds != NULL ? *input.current_bounds : body;
  if (pose == NULL && output != NULL && output->body_bounds != NULL) {
    pm.has_body_bounds = true;
    pm.body_bounds = *output->body_bounds;
  }
  pm.trace = classic_trace_fn;
  pm.pointcontents = adapter_point_contents_fn;

  qa_q2_pmove_classic(&pm, &services.numeric, input.profile.air_accelerate,
                      input.profile.has_strafejump_hack && input.profile.strafejump_hack,
                      input.environment.flight && input.environment.health > 0,
                      input.environment.has_speed_multiplier
                          ? input.environment.speed_multiplier
                          : 1);
  if (pose != NULL) {
    pm.s.pm_type = input.state.as.q2.type;
    pm.viewheight = pose->view_height;
    pm.s.pm_flags = pose->crouched ? qa_q2_flag_set(pm.s.pm_flags, QA_Q2_PMF_DUCKED)
                                   : qa_q2_flag_clear(pm.s.pm_flags, QA_Q2_PMF_DUCKED);
  }
  qa_movement_state state;
  memset(&state, 0, sizeof state);
  state.kind = QA_MOVEMENT_STATE_Q2;
  state.as.q2.type = has_mode ? input.state.as.q2.type : pm.s.pm_type;
  for (int i = 0; i < 3; i++) {
    state.as.q2.origin_eighths.values[i] = pm.s.origin[i];
    state.as.q2.velocity_eighths.values[i] = pm.s.velocity[i];
    state.as.q2.delta_angle_shorts.values[i] = pm.s.delta_angles[i];
  }
  state.as.q2.flags = pm.s.pm_flags;
  state.as.q2.time_eight_milliseconds = pm.s.pm_time;
  state.as.q2.gravity = pm.s.gravity;

  const double touch_count = pm.numtouch < 0 ? 0 : pm.numtouch;
  const contact_list contacts =
      movement_contacts(pm.touchtraces, (size_t)touch_count);
  adapter_destroy(&scene);

  const qa_numeric_operations *n = &services.numeric;
  qa_q2_movement_result result;
  memset(&result, 0, sizeof result);
  result.kind = "q2-classic";
  result.status = "active";
  result.actor = input.actor.id;
  result.command_sequence = input.command_sequence;
  result.state = state;
  result.bounds.min = vector_of(pm.mins);
  result.bounds.max = vector_of(pm.maxs);
  result.view_angles = vector_of(pm.viewangles);
  result.view_height = pm.viewheight;
  if (pm.has_groundentity) {
    result.ground = pm.groundentity;
  } else {
    result.ground.kind = QA_TRACE_HIT_NONE;
  }
  result.water_level = pm.waterlevel;
  result.water_type = pm.watertype;
  result.horizontal_speed = qa_q2_n_square_root(
      n, qa_q2_n_add(n, qa_q2_n_multiply(n, pm.s.velocity[0] / 8, pm.s.velocity[0] / 8),
                     qa_q2_n_multiply(n, pm.s.velocity[1] / 8, pm.s.velocity[1] / 8)));
  result.contacts = contacts.contacts;
  result.contact_count = contacts.count;
  result.effects = NULL;
  result.effect_count = 0;
  result.arsenal = input.arsenal;
  result.animation = input.animation;
  return result;
}

static qa_movement_result removed_result(const char *kind, const qa_movement_input *input,
                                         const qa_ordered_movement_effect *effects,
                                         size_t effect_count) {
  qa_movement_result result;
  memset(&result, 0, sizeof result);
  result.kind = kind;
  result.status = "actor-removed";
  result.actor = input->actor.id;
  result.command_sequence = input->command_sequence;
  result.effects = effects;
  result.effect_count = effect_count;
  return result;
}

qa_q2_movement_result qa_move_q2_classic(qa_q2_movement_input input,
                                         qa_movement_services services) {
  const bool use_application =
      str_eq(input.execution, "authoritative") && services.has_input_application;
  if (!use_application) return move_q2_classic_physics(input, services);
  const qa_movement_input_application *application = &services.input_application;
  qa_frame_context frame = input.frame;
  frame.elapsed.kind = QA_SOURCE_TIME_MILLISECONDS;
  frame.elapsed.value = input.command.as.q2.milliseconds;
  /* TS catch: end(input.state, true) then rethrow; C errors abort. */
  const qa_movement_input_continuation before =
      application->begin(application->ctx, input.command, frame, input.state);
  qa_q2_movement_result result;
  if (!before.is_continue) {
    result = removed_result("q2-classic", &input, NULL, 0);
  } else if (before.state.kind == QA_MOVEMENT_STATE_Q2 &&
             before.command.kind == QA_USER_COMMAND_Q2) {
    qa_movement_input next = input;
    next.command = before.command;
    next.state = before.state;
    result = move_q2_classic_physics(next, services);
  } else {
    qa_range_error("Input callback changed Quake II movement family");
  }
  qa_movement_posture posture;
  qa_movement_continuation after;
  if (str_eq(result.status, "active")) {
    posture.bounds = result.bounds;
    posture.view_height = result.view_height;
    after = application->end(application->ctx, result.state, true, false, &posture);
  } else {
    after = application->end(application->ctx, input.state, false, false, NULL);
  }
  if (!after.is_continue || str_eq(result.status, "actor-removed")) {
    qa_movement_result removed =
        removed_result("q2-classic", &input, result.effects, result.effect_count);
    free((void *)result.contacts);
    return removed;
  }
  if (after.state.kind != QA_MOVEMENT_STATE_Q2)
    qa_range_error("Input callback changed Quake II movement family");
  result.state = after.state;
  return result;
}

static qa_q2_rerelease_movement_result move_q2_rerelease_physics(
    qa_q2_rerelease_movement_input input, qa_movement_services services,
    qa_q2_rerelease_movement_context *context) {
  const qa_mod_client_movement_outputs *output = input.environment.client_outputs;
  qa_mod_client_movement_mode mode;
  const bool has_mode = qa_client_movement_mode(output, input.environment.health, &mode);
  const qa_user_command command = qa_client_stance_command(
      input.command, output != NULL && output->has_stance, output != NULL && output->stance);
  if (command.kind != QA_USER_COMMAND_Q2_RERELEASE)
    qa_range_error("Client output changed movement dialect");
  input.command = command;
  command_duration(input.command.as.q2_rerelease.milliseconds);

  qa_q2_movement_math vec_math;
  qa_q2_movement_math_init(&vec_math, &services.numeric, QA_Q2_MATH_CLASSIC);
  trace_adapter scene;
  adapter_init(&scene, &input, &services);
  const qa_fixed_movement_pose *pose = input.environment.pose;
  const qa_bounds body = pose != NULL ? pose->bounds : body_bounds_of(input.shape);

  qa_q2_kex_pmove pm;
  memset(&pm, 0, sizeof pm);
  pm.ctx = &scene;
  pm.s.pm_type = pose == NULL
                     ? (has_mode ? qa_client_movement_type(QA_MOVEMENT_STATE_Q2_RERELEASE, mode)
                                 : input.state.as.q2_rerelease.type)
                     : QA_Q2_KEX_PM_FREEZE;
  source_vector(input.state.as.q2_rerelease.origin, pm.s.origin);
  source_vector(pose == NULL ? input.state.as.q2_rerelease.velocity : zero_vec, pm.s.velocity);
  pm.s.pm_flags = input.state.as.q2_rerelease.flags;
  pm.s.pm_time = input.state.as.q2_rerelease.time_milliseconds;
  pm.s.gravity = input.state.as.q2_rerelease.gravity;
  source_vector(input.state.as.q2_rerelease.delta_angles, pm.s.delta_angles);
  pm.s.viewheight = input.state.as.q2_rerelease.view_height;
  pm.cmd.msec = input.command.as.q2_rerelease.milliseconds;
  source_vector(input.command.as.q2_rerelease.angles, pm.cmd.angles);
  pm.cmd.forwardmove = input.command.as.q2_rerelease.forward_move;
  pm.cmd.sidemove = input.command.as.q2_rerelease.side_move;
  pm.cmd.buttons = input.command.as.q2_rerelease.buttons;
  pm.cmd.server_frame = input.command.as.q2_rerelease.server_frame;
  pm.snapinitial = input.snap_initial;
  pm.touch.num = 0;
  qa_q2_math_vec3(&vec_math, 0, 0, 0, pm.viewangles);
  source_vector(body.min, pm.mins);
  source_vector(body.max, pm.maxs);
  pm.character_bounds = body;
  pm.has_previous_bounds = true;
  pm.previous_bounds = input.current_bounds != NULL ? *input.current_bounds : body;
  if (pose == NULL && output != NULL && output->body_bounds != NULL) {
    pm.has_body_bounds = true;
    pm.body_bounds = *output->body_bounds;
  }
  pm.groundplane = qa_q2_plane();
  pm.has_player = true;
  pm.player.kind = QA_TRACE_HIT_ACTOR;
  pm.player.actor = input.actor.id;
  pm.trace = rerelease_trace_fn;
  pm.clip = rerelease_clip_fn;
  pm.pointcontents = adapter_point_contents_fn;
  source_vector(input.view_offset, pm.viewoffset);
  pm.screen_blend[0] = 0;
  pm.screen_blend[1] = 0;
  pm.screen_blend[2] = 0;
  pm.screen_blend[3] = 0;
  pm.rdflags = 0;
  pm.jump_sound = false;
  pm.step_clip = false;
  pm.impact_delta = 0;

  const qa_q2_pm_config config = {input.profile.air_accelerate, input.profile.n64_physics};
  qa_q2_pmove_rerelease(&pm, &services.numeric, config, context,
                        input.environment.flight && input.environment.health > 0,
                        input.environment.has_speed_multiplier
                            ? input.environment.speed_multiplier
                            : 1);
  if (pose != NULL) {
    pm.s.pm_type = input.state.as.q2_rerelease.type;
    pm.s.viewheight = pose->view_height;
    pm.s.pm_flags = pose->crouched ? qa_q2_flag_set(pm.s.pm_flags, QA_Q2_PMF_DUCKED)
                                   : qa_q2_flag_clear(pm.s.pm_flags, QA_Q2_PMF_DUCKED);
  }
  qa_movement_state state;
  memset(&state, 0, sizeof state);
  state.kind = QA_MOVEMENT_STATE_Q2_RERELEASE;
  state.as.q2_rerelease.type = has_mode ? input.state.as.q2_rerelease.type : pm.s.pm_type;
  state.as.q2_rerelease.origin = vector_of(pm.s.origin);
  state.as.q2_rerelease.velocity = vector_of(pm.s.velocity);
  state.as.q2_rerelease.flags = pm.s.pm_flags;
  state.as.q2_rerelease.time_milliseconds = pm.s.pm_time;
  state.as.q2_rerelease.gravity = pm.s.gravity;
  state.as.q2_rerelease.delta_angles = vector_of(pm.s.delta_angles);
  state.as.q2_rerelease.view_height = pm.s.viewheight;

  const double touch_count = pm.touch.num < 0 ? 0 : pm.touch.num;
  const contact_list contacts = movement_contacts(pm.touch.traces, (size_t)touch_count);
  adapter_destroy(&scene);

  const qa_numeric_operations *n = &services.numeric;
  qa_q2_rerelease_movement_result result;
  memset(&result, 0, sizeof result);
  result.kind = "q2-rerelease";
  result.status = "active";
  result.actor = input.actor.id;
  result.command_sequence = input.command_sequence;
  result.state = state;
  result.screen_blend.x = pm.screen_blend[0];
  result.screen_blend.y = pm.screen_blend[1];
  result.screen_blend.z = pm.screen_blend[2];
  result.screen_blend.w = pm.screen_blend[3];
  result.render_flags = pm.rdflags;
  result.jump_sound = pm.jump_sound;
  result.step_clip = pm.step_clip;
  result.impact_delta = pm.impact_delta;
  result.bounds.min = vector_of(pm.mins);
  result.bounds.max = vector_of(pm.maxs);
  result.view_angles = vector_of(pm.viewangles);
  result.view_height = pm.s.viewheight;
  if (pm.has_groundentity) {
    result.ground = pm.groundentity;
  } else {
    result.ground.kind = QA_TRACE_HIT_NONE;
  }
  result.water_level = pm.waterlevel;
  result.water_type = pm.watertype;
  result.horizontal_speed = qa_q2_n_square_root(
      n, qa_q2_n_add(n, qa_q2_n_multiply(n, pm.s.velocity[0], pm.s.velocity[0]),
                     qa_q2_n_multiply(n, pm.s.velocity[1], pm.s.velocity[1])));
  result.contacts = contacts.contacts;
  result.contact_count = contacts.count;
  result.effects = NULL;
  result.effect_count = 0;
  result.arsenal = input.arsenal;
  result.animation = input.animation;
  return result;
}

qa_q2_rerelease_movement_result qa_move_q2_rerelease(
    qa_q2_rerelease_movement_input input, qa_movement_services services,
    qa_q2_rerelease_movement_context *context) {
  const bool use_application =
      str_eq(input.execution, "authoritative") && services.has_input_application;
  if (!use_application) return move_q2_rerelease_physics(input, services, context);
  const qa_movement_input_application *application = &services.input_application;
  qa_frame_context frame = input.frame;
  frame.elapsed.kind = QA_SOURCE_TIME_MILLISECONDS;
  frame.elapsed.value = input.command.as.q2_rerelease.milliseconds;
  const qa_movement_input_continuation before =
      application->begin(application->ctx, input.command, frame, input.state);
  qa_q2_rerelease_movement_result result;
  if (!before.is_continue) {
    result = removed_result("q2-rerelease", &input, NULL, 0);
  } else if (before.state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE &&
             before.command.kind == QA_USER_COMMAND_Q2_RERELEASE) {
    qa_movement_input next = input;
    next.command = before.command;
    next.state = before.state;
    result = move_q2_rerelease_physics(next, services, context);
  } else {
    qa_range_error("Input callback changed Quake II movement family");
  }
  qa_movement_posture posture;
  qa_movement_continuation after;
  if (str_eq(result.status, "active")) {
    posture.bounds = result.bounds;
    posture.view_height = result.view_height;
    after = application->end(application->ctx, result.state, true, false, &posture);
  } else {
    after = application->end(application->ctx, input.state, false, false, NULL);
  }
  if (!after.is_continue || str_eq(result.status, "actor-removed")) {
    qa_movement_result removed =
        removed_result("q2-rerelease", &input, result.effects, result.effect_count);
    free((void *)result.contacts);
    return removed;
  }
  if (after.state.kind != QA_MOVEMENT_STATE_Q2_RERELEASE)
    qa_range_error("Input callback changed Quake II movement family");
  result.state = after.state;
  return result;
}

static qa_movement_result classic_provider_move(void *data, qa_movement_input input,
                                                qa_movement_services services) {
  (void)data;
  return qa_move_q2_classic(input, services);
}

static qa_movement_result rerelease_provider_move(void *data, qa_movement_input input,
                                                  qa_movement_services services) {
  return qa_move_q2_rerelease(input, services, data);
}

qa_movement_provider qa_create_q2_classic_movement_provider(qa_provider_id id) {
  qa_movement_provider provider;
  memset(&provider, 0, sizeof provider);
  provider.kind = "q2-classic";
  provider.id = id;
  provider.ctx = NULL;
  provider.move = classic_provider_move;
  return provider;
}

qa_movement_provider qa_create_q2_rerelease_movement_provider(
    qa_provider_id id, qa_q2_rerelease_movement_context *context) {
  qa_movement_provider provider;
  memset(&provider, 0, sizeof provider);
  provider.kind = "q2-rerelease";
  provider.id = id;
  provider.ctx = context;
  provider.move = rerelease_provider_move;
  return provider;
}

qa_trace_shape qa_q2_player_shape(void) {
  qa_trace_shape shape;
  memset(&shape, 0, sizeof shape);
  shape.kind = "box";
  shape.bounds = qa_q2_player_bounds;
  return shape;
}

qa_movement_result qa_apply_q2_movement_contacts(qa_movement_input input,
                                                 qa_movement_services services,
                                                 qa_movement_result result) {
  if (!str_eq(input.kind, result.kind) || !qa_same_actor(input.actor.id, result.actor))
    qa_range_error("Q2 contact result does not belong to this movement input");
  if (str_eq(result.status, "actor-removed")) return result;
  effects_list effects;
  memset(&effects, 0, sizeof effects);
  for (size_t i = 0; i < result.effect_count; i++) push_effect(&effects, result.effects[i]);
  qa_movement_state next_state;
  const bool continued = touch_contacts(&input, &services, result.state, result.contacts,
                                        result.contact_count, &effects, &next_state);
  free((void *)result.effects);
  if (!continued) {
    qa_movement_result removed;
    memset(&removed, 0, sizeof removed);
    removed.kind = result.kind;
    removed.status = "actor-removed";
    removed.actor = result.actor;
    removed.command_sequence = result.command_sequence;
    removed.effects = effects.effects;
    removed.effect_count = effects.count;
    free((void *)result.contacts);
    return removed;
  }
  if (str_eq(result.kind, "q2-classic") && next_state.kind == QA_MOVEMENT_STATE_Q2) {
    result.state = next_state;
    result.effects = effects.effects;
    result.effect_count = effects.count;
    return result;
  }
  if (str_eq(result.kind, "q2-rerelease") && next_state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
    result.state = next_state;
    result.effects = effects.effects;
    result.effect_count = effects.count;
    return result;
  }
  qa_range_error("Q2 contacts changed the movement family");
}
