/* Port of src/movement/q3/provider.ts. GPL-2.0-or-later. */
#include "qa/movement/q3/provider.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/client_outputs.h"
#include "qa/movement/q3/constants.h"
#include "qa/movement/q3/move.h"

static double f32(double v) { return (double)(float)v; }

qa_q3_move_command qa_q3_command(qa_q3_user_command command) {
  qa_q3_move_command c = {command.server_time_milliseconds,
      {command.angle_words.values[0], command.angle_words.values[1], command.angle_words.values[2]},
      command.buttons, command.weapon, command.forward_move, command.right_move, command.up_move};
  return c;
}
static qa_user_command user_command(qa_q3_move_command command) {
  qa_user_command c = {0};
  c.kind = QA_USER_COMMAND_Q3;
  c.as.q3.server_time_milliseconds = command.server_time;
  c.as.q3.angle_words.values[0] = command.angles.x;
  c.as.q3.angle_words.values[1] = command.angles.y;
  c.as.q3.angle_words.values[2] = command.angles.z;
  c.as.q3.buttons = command.buttons; c.as.q3.weapon = command.weapon;
  c.as.q3.forward_move = command.forwardmove; c.as.q3.right_move = command.rightmove;
  c.as.q3.up_move = command.upmove;
  return c;
}

typedef struct provider_move {
  qa_q3_movement_input input;
  qa_movement_services services;
  qa_q3_movement_provider_options *options;
  qa_q3_motion *motion;
  qa_q3_movement_state source_state;
  bool mode_projected;
  double multiplier;
  qa_arsenal_state arsenal;
  qa_actor_animation_state animation;
  qa_q3_move_command command;
  qa_frame_context frame;
  double substep;
  qa_ordered_movement_effect *effects; size_t effect_count, effect_capacity;
  bool application_open, removed;
  qa_trace_policy selected_policy;
  qa_q3_motion_options *motion_options;
} provider_move;

static bool output_mode(provider_move *m, qa_mod_client_movement_mode *mode) {
  return qa_client_movement_mode(m->input.environment.client_outputs, m->input.environment.health, mode);
}
static double movement_speed(provider_move *m, double speed) {
  return m->multiplier == 1 ? speed : (double)qa_int32(trunc(f32(f32(speed) * f32(m->multiplier))));
}
static qa_q3_movement_state movement_state(provider_move *m) {
  qa_q3_movement_state state = m->source_state;
  qa_q3_motion *motion = m->motion;
  state.command_time_milliseconds = motion->command_time;
  state.movement_type = m->mode_projected ? m->source_state.movement_type : motion->pm_type;
  state.bob_cycle = motion->bob_cycle; state.movement_flags = motion->pm_flags;
  state.movement_time_milliseconds = motion->pm_time;
  state.origin = motion->origin; state.velocity = motion->velocity;
  state.delta_angle_words.values[0] = motion->delta_angles.x;
  state.delta_angle_words.values[1] = motion->delta_angles.y;
  state.delta_angle_words.values[2] = motion->delta_angles.z;
  state.ground = motion->ground; state.movement_direction = motion->movement_dir;
  state.flags = motion->e_flags; state.view_angles = motion->viewangles;
  state.view_height = motion->viewheight; state.movement_frame = motion->pmove_framecount;
  state.predictable_event_sequence = motion->event_sequence;
  return state;
}
static qa_movement_state wrapped_state(provider_move *m) {
  qa_movement_state state = {0};
  state.kind = QA_MOVEMENT_STATE_Q3; state.as.q3 = movement_state(m);
  return state;
}
static void resume(provider_move *m, qa_movement_state wrapped) {
  if (wrapped.kind != QA_MOVEMENT_STATE_Q3)
    qa_range_error("Input callback changed Quake III movement family");
  qa_q3_movement_state state = wrapped.as.q3;
  m->source_state = state;
  qa_mod_client_movement_mode mode;
  bool has_mode = output_mode(m, &mode);
  m->mode_projected = has_mode;
  qa_q3_motion *motion = m->motion;
  motion->command_time = state.command_time_milliseconds;
  motion->pm_type = has_mode ? qa_client_movement_type(QA_MOVEMENT_STATE_Q3, mode) : state.movement_type;
  motion->bob_cycle = state.bob_cycle; motion->pm_flags = state.movement_flags;
  motion->pm_time = state.movement_time_milliseconds;
  motion->origin = state.origin; motion->velocity = state.velocity; motion->ground = state.ground;
  motion->delta_angles = (qa_vec3){state.delta_angle_words.values[0], state.delta_angle_words.values[1],
                                    state.delta_angle_words.values[2]};
  motion->movement_dir = state.movement_direction; motion->e_flags = state.flags;
  motion->viewangles = state.view_angles; motion->viewheight = state.view_height;
  motion->pmove_framecount = state.movement_frame; motion->event_sequence = state.predictable_event_sequence;
  motion->grapple_point = state.grapple_point;
  motion->gravity = trunc(state.gravity * m->input.environment.gravity_multiplier);
  motion->speed = movement_speed(m, state.speed);
}
static qa_q3_hook_context hook_context(provider_move *m) {
  qa_q3_hook_context context = {m->input, m->motion, movement_state(m), m->command, m->frame,
                                m->arsenal, m->animation, m->services};
  return context;
}
static void append_effect(provider_move *m, qa_movement_effect effect) {
  if (effect.kind == QA_MOVEMENT_EFFECT_EVENT) {
    effect.value.sequence = m->motion->event_sequence;
    m->motion->event_sequence = (double)qa_int32(m->motion->event_sequence + 1);
  }
  if (m->effect_count == m->effect_capacity) {
    m->effect_capacity = m->effect_capacity ? m->effect_capacity * 2 : 8;
    m->effects = realloc(m->effects, m->effect_capacity * sizeof *m->effects);
    if (m->effects == NULL) qa_range_error("Out of memory");
  }
  m->effects[m->effect_count] = (qa_ordered_movement_effect){
      m->substep, (double)m->effect_count, m->frame.time, effect};
  m->effect_count++;
}
static qa_trace_policy policy_for(provider_move *m, double contents_mask) {
  qa_trace_policy policy = m->selected_policy;
  policy.kind = "q3"; policy.contents_mask = contents_mask;
  return policy;
}
static qa_trace_result opt_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
                                 qa_actor_id pass_actor, double mask) {
  provider_move *m = ctx;
  bool point = m->input.shape.kind != NULL && strcmp(m->input.shape.kind, "point") == 0;
  qa_trace_shape shape = point ? m->input.shape : (qa_trace_shape){m->input.shape.kind, bounds};
  qa_trace_query query = {0};
  query.start = start; query.end = end; query.shape = shape;
  query.target.is_model = false;
  query.numeric = m->input.profile.numeric; query.policy = policy_for(m, mask);
  /* pass_actor is a local copy; the scene reads it synchronously. */
  qa_actor_id pass_copy = pass_actor; query.pass_actor = &pass_copy;
  return m->services.scene.trace(m->services.scene.ctx, query);
}
static double opt_contents(void *ctx, qa_vec3 point, qa_actor_id pass_actor) {
  provider_move *m = ctx;
  qa_point_contents_query query = {0};
  query.point = point; query.target.is_model = false;
  query.numeric = m->input.profile.numeric; query.policy = policy_for(m, -1);
  qa_actor_id pass_copy = pass_actor; query.pass_actor = &pass_copy;
  qa_point_contents_result contents = m->services.scene.point_contents(m->services.scene.ctx, query);
  if (contents.kind == NULL || strcmp(contents.kind, "q3") != 0)
    qa_range_error("Q3 point contents queries require Q3 content flags");
  return contents.contents;
}
static void application_end(provider_move *m) {
  if (!m->services.has_input_application || strcmp(m->input.execution, "authoritative") != 0) return;
  (void)m->services.input_application.end(m->services.input_application.ctx, wrapped_state(m), false, false, NULL);
}
static bool opt_begin_step(void *ctx, qa_q3_motion *state, qa_q3_move_command *active_command,
                           double msec, double index) {
  (void)state;
  provider_move *m = ctx;
  /* TS bodyBounds is a getter; refresh per substep. */
  const qa_mod_client_movement_outputs *outputs = m->input.environment.client_outputs;
  m->motion_options->has_body_bounds = outputs != NULL && outputs->body_bounds != NULL;
  m->motion_options->body_bounds = outputs != NULL ? outputs->body_bounds : NULL;
  if (m->removed) return false;
  m->command = *active_command; m->substep = index;
  m->frame = m->input.frame;
  m->frame.time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, m->command.server_time};
  m->frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, msec};
  bool authoritative = strcmp(m->input.execution, "authoritative") == 0;
  if (authoritative && m->services.has_input_application) {
    m->application_open = true;
    qa_movement_input_continuation before = m->services.input_application.begin(
        m->services.input_application.ctx, user_command(m->command), m->frame, wrapped_state(m));
    if (!before.is_continue) { m->removed = true; return false; }
    if (before.command.kind != QA_USER_COMMAND_Q3)
      qa_range_error("Input output changed command dialect");
    *active_command = qa_q3_command(before.command.as.q3);
    m->command = *active_command;
    resume(m, before.state);
  }
  bool has_stance = outputs != NULL && outputs->has_stance;
  qa_user_command effective = qa_client_stance_command(user_command(m->command), has_stance,
                                                       has_stance && outputs->stance);
  if (effective.kind != QA_USER_COMMAND_Q3)
    qa_range_error("Client stance changed movement dialect");
  *active_command = qa_q3_command(effective.as.q3);
  m->command = *active_command;
  qa_mod_client_movement_mode mode;
  if (output_mode(m, &mode)) {
    m->mode_projected = true;
    m->motion->pm_type = qa_client_movement_type(QA_MOVEMENT_STATE_Q3, mode);
  }
  return true;
}
static bool opt_end_step(void *ctx, qa_q3_motion *state) {
  (void)state;
  provider_move *m = ctx;
  m->application_open = false;
  qa_movement_continuation after = m->services.input_application.end(
      m->services.input_application.ctx, wrapped_state(m), false, false, NULL);
  if (!after.is_continue) { m->removed = true; return false; }
  resume(m, after.state);
  return true;
}
static void opt_event(void *ctx, double event) {
  provider_move *m = ctx;
  qa_movement_effect effect = {0};
  effect.kind = QA_MOVEMENT_EFFECT_EVENT;
  effect.value = (qa_predictable_movement_event){m->options->id, m->motion->event_sequence, event, 0};
  append_effect(m, effect);
}
static void opt_animation(void *ctx, qa_q3_animation_request request) {
  provider_move *m = ctx;
  qa_animation_step_result update = m->options->hooks.animation(m->options->hooks.ctx, request, hook_context(m));
  m->animation = update.animation;
  for (size_t i = 0; i < update.effect_count; i++) append_effect(m, update.effects[i]);
}
static bool opt_weapon(void *ctx) {
  provider_move *m = ctx;
  qa_q3_weapon_phase_result update = m->options->hooks.weapon(m->options->hooks.ctx, hook_context(m));
  m->arsenal = update.base.arsenal; m->animation = update.base.animation;
  m->motion->pm_flags = update.movement_flags;
  for (size_t i = 0; i < update.base.effect_count; i++) append_effect(m, update.base.effects[i]);
  if (update.base.continuation != NULL && !update.base.continuation->is_continue) {
    m->removed = true; return false;
  }
  if (update.base.continuation != NULL && update.base.continuation->is_continue) {
    resume(m, update.base.continuation->state);
    int weapon_flags = QA_Q3_FLAG_RESPAWNED | QA_Q3_FLAG_USE_ITEM_HELD;
    m->motion->pm_flags = (double)((qa_int32(m->motion->pm_flags) & ~weapon_flags) |
                                    (qa_int32(update.movement_flags) & weapon_flags));
  }
  return true;
}
static void opt_torso(void *ctx) {
  provider_move *m = ctx;
  qa_animation_step_result update = m->options->hooks.torso(m->options->hooks.ctx, hook_context(m));
  m->animation = update.animation;
  for (size_t i = 0; i < update.effect_count; i++) append_effect(m, update.effects[i]);
}
static bool opt_firing(void *ctx) {
  provider_move *m = ctx;
  return m->options->hooks.firing(m->options->hooks.ctx, hook_context(m));
}
static void opt_contact(void *ctx, qa_trace_result trace) {
  provider_move *m = ctx;
  if (trace.hit.kind == QA_TRACE_HIT_NONE) return;
  qa_movement_effect effect = {0};
  effect.kind = QA_MOVEMENT_EFFECT_TOUCH; effect.target = trace.hit; effect.substep = m->substep;
  append_effect(m, effect);
}

static qa_movement_result provider_do_move(void *ctx, qa_movement_input input_in, qa_movement_services services) {
  qa_q3_movement_provider_options *options = ctx;
  qa_q3_movement_input input = input_in;
  if (input.profile.numeric.arithmetic.kind != QA_ARITHMETIC_BINARY32 ||
      services.numeric.profile.arithmetic.kind != QA_ARITHMETIC_BINARY32)
    qa_range_error("The Q3 movement implementation requires binary32 operations");
  qa_q3_movement_state source = input.state.as.q3;
  provider_move m = {0};
  m.input = input; m.services = services; m.options = options;
  m.multiplier = input.environment.has_speed_multiplier ? input.environment.speed_multiplier : 1;
  m.source_state = source;
  qa_mod_client_movement_mode initial_mode;
  bool has_initial_mode = output_mode(&m, &initial_mode);
  m.mode_projected = has_initial_mode;
  qa_q3_motion motion = {0};
  motion.command_time = source.command_time_milliseconds;
  motion.pm_type = has_initial_mode ? qa_client_movement_type(QA_MOVEMENT_STATE_Q3, initial_mode)
                                    : source.movement_type;
  motion.bob_cycle = source.bob_cycle; motion.pm_flags = source.movement_flags;
  motion.pm_time = source.movement_time_milliseconds;
  motion.origin = source.origin; motion.velocity = source.velocity;
  motion.gravity = trunc(source.gravity * input.environment.gravity_multiplier);
  motion.speed = movement_speed(&m, source.speed);
  motion.delta_angles = (qa_vec3){source.delta_angle_words.values[0], source.delta_angle_words.values[1],
                                  source.delta_angle_words.values[2]};
  motion.ground = source.ground; motion.movement_dir = source.movement_direction;
  motion.grapple_point = source.grapple_point; motion.e_flags = source.flags;
  motion.viewangles = source.view_angles; motion.viewheight = source.view_height;
  motion.pmove_framecount = source.movement_frame; motion.event_sequence = source.predictable_event_sequence;
  motion.actor = input.actor.id; motion.health = input.environment.health;
  motion.flight = input.environment.flight; motion.invulnerable = input.environment.invulnerable;
  motion.product = input.profile.product;
  m.motion = &motion;
  bool point = input.shape.kind != NULL && strcmp(input.shape.kind, "point") == 0;
  qa_bounds standing = point ? (qa_bounds){{0, 0, 0}, {0, 0, 0}} : input.shape.bounds;
  if (options->trace_policy != NULL) m.selected_policy = options->trace_policy(options->ctx, input);
  else {
    m.selected_policy = (qa_trace_policy){0};
    m.selected_policy.kind = "q3"; m.selected_policy.curves = true; m.selected_policy.player_curve_clip = true;
    m.selected_policy.contents_mask = (double)(1 | 0x10000 |
        (source.movement_type == QA_Q3_PM_SPECTATOR ? 0 : 0x2000000));
  }
  m.arsenal = input.arsenal; m.animation = input.animation;
  m.command = qa_q3_command(input.command.as.q3); m.frame = input.frame;
  bool authoritative = strcmp(input.execution, "authoritative") == 0;
  qa_q3_motion_options motion_options = {0};
  m.motion_options = &motion_options;
  motion_options.ctx = &m;
  motion_options.pose = input.environment.pose;
  motion_options.trace = opt_trace; motion_options.point_contents = opt_contents;
  motion_options.standing_bounds = standing;
  motion_options.has_current_bounds = input.current_bounds != NULL;
  if (input.current_bounds != NULL) motion_options.current_bounds = *input.current_bounds;
  const qa_mod_client_movement_outputs *outputs = input.environment.client_outputs;
  motion_options.has_body_bounds = outputs != NULL && outputs->body_bounds != NULL;
  motion_options.body_bounds = outputs != NULL ? outputs->body_bounds : NULL;
  motion_options.postures = options->postures(options->ctx, input);
  motion_options.has_trace_mask = true; motion_options.trace_mask = m.selected_policy.contents_mask;
  motion_options.has_fixed_msec = input.profile.has_fixed_milliseconds;
  motion_options.fixed_msec = input.profile.fixed_milliseconds;
  motion_options.no_footsteps = input.profile.no_footsteps;
  motion_options.diagnostics = options->diagnostics;
  motion_options.begin_step = opt_begin_step;
  motion_options.end_step = (authoritative && services.has_input_application) ? opt_end_step : NULL;
  motion_options.event = opt_event; motion_options.animation = opt_animation;
  motion_options.weapon = opt_weapon; motion_options.torso = opt_torso;
  motion_options.firing = opt_firing; motion_options.contact = opt_contact;
  qa_q3_motion_result result = qa_q3_move_player(&motion, m.command, &motion_options);
  if (m.application_open) application_end(&m);
  qa_movement_result out = {0};
  out.kind = "q3"; out.actor = input.actor.id; out.command_sequence = input.command_sequence;
  out.effects = m.effects; out.effect_count = m.effect_count;
  if (m.removed) {
    out.status = "actor-removed";
    free(result.contacts);
    return out;
  }
  out.status = "active";
  out.state = wrapped_state(&m);
  out.bounds = result.bounds; out.view_angles = motion.viewangles; out.view_height = motion.viewheight;
  out.ground = motion.ground; out.water_level = result.waterlevel; out.water_type = result.watertype;
  out.horizontal_speed = result.xyspeed;
  qa_movement_contact *contacts = NULL;
  if (result.contact_count != 0) {
    contacts = malloc(result.contact_count * sizeof *contacts);
    if (contacts == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < result.contact_count; i++)
      contacts[i] = (qa_movement_contact){result.contacts[i].hit, result.contacts[i], m.substep};
  }
  out.contacts = contacts; out.contact_count = result.contact_count;
  free(result.contacts);
  out.arsenal = m.arsenal; out.animation = m.animation;
  (void)qa_add3; /* referenced through core math link parity */
  return out;
}

qa_movement_provider qa_q3_create_movement_provider(qa_q3_movement_provider_options *options) {
  qa_movement_provider provider = {"q3", options->id, options, provider_do_move};
  return provider;
}
