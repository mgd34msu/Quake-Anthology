/* Port of src/app/bootstrap/simulation/prediction/step.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/prediction/step.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/arsenal_intent.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include "qa/content/q3/foundation/character.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q1/index.h"
#include "qa/movement/q2/index.h"
#include "qa/movement/q3/animation.h"
#include "qa/movement/q3/constants.h"
#include "qa/movement/q3/postures.h"
#include "qa/movement/q3/provider.h"

qa_movement_state qa_app_copy_movement_state(qa_movement_state state) { return state; }

qa_app_movement_prediction_snapshot qa_app_copy_prediction_snapshot(qa_app_movement_prediction_snapshot s) {
  qa_app_movement_prediction_snapshot copy = s;
  copy.state = qa_app_copy_movement_state(s.state);
  if (s.arsenal.ammo_count) {
    qa_inventory_entry *ammo = malloc(s.arsenal.ammo_count * sizeof *ammo);
    if (!ammo) qa_range_error("Out of memory");
    memcpy(ammo, s.arsenal.ammo, s.arsenal.ammo_count * sizeof *ammo);
    copy.arsenal.ammo = ammo;
  } else copy.arsenal.ammo = NULL;
  return copy;
}

typedef struct step_ctx {
  const qa_app_movement_probe_options *configuration;
  qa_app_prediction_command entry;
  qa_app_prediction_step_options options;
  qa_app_movement_prediction_snapshot source;
  bool has_runtime;
  qa_q3_arsenal_runtime_state runtime;
  qa_frame_context frame;
  qa_movement_environment environment;
  qa_user_command command;
} step_ctx;

static qa_q3_arsenal_controls resolve_controls(step_ctx *ctx, qa_arsenal_state arsenal, qa_user_command command, const char *product) {
  if (ctx->configuration->resolve_controls)
    return ctx->configuration->resolve_controls(ctx->configuration->host_ctx, arsenal,
        ctx->entry.has_arsenal ? &ctx->entry.arsenal : NULL, command, product);
  return qa_app_sim_resolve_q3_arsenal_controls(arsenal,
      ctx->entry.has_arsenal ? &ctx->entry.arsenal : NULL, command, product);
}

static qa_weapon_step_result weapon_step(step_ctx *ctx, qa_weapon_step_input input) {
  if (ctx->configuration->movement_only || !input.arsenal.state.kind || strcmp(input.arsenal.state.kind, "q3") != 0) {
    qa_weapon_step_result result; memset(&result, 0, sizeof result);
    result.arsenal = input.arsenal; result.animation = input.animation; return result;
  }
  if (!ctx->has_runtime) qa_range_error("Q3 prediction needs the snapshot's private arsenal runtime");
  qa_q3_arsenal_controls controls = resolve_controls(ctx, input.arsenal, input.command, ctx->runtime.product);
  if (input.command.kind == QA_USER_COMMAND_Q3) controls.use_holdable = (((long long)input.command.as.q3.buttons) & 4) != 0;
  qa_q3_arsenal_step stepped = qa_q3_step_arsenal(input, ctx->runtime, controls, NULL, NULL);
  ctx->runtime = stepped.runtime;
  qa_weapon_step_result result = stepped.base;
  return result;
}

static qa_movement_continuation touch_cb(void *v, qa_movement_touch_contact contact, qa_movement_state state) {
  (void)v; (void)contact;
  qa_movement_continuation result; memset(&result, 0, sizeof result);
  result.is_continue = true; result.state = state; return result;
}
static qa_weapon_step_result weapon_cb(void *v, qa_weapon_step_input input, qa_movement_state state) {
  (void)state; return weapon_step((step_ctx *)v, input);
}
static qa_animation_step_result animation_cb(void *v, qa_animation_step_input input) {
  step_ctx *ctx = (step_ctx *)v;
  if (!ctx->configuration->movement_only && input.animation.state.kind && strcmp(input.animation.state.kind, "q3") == 0)
    return qa_q3_step_character_animation(input, ctx->has_runtime ? ctx->runtime.product : "baseq3",
        ctx->source.environment.health <= 0, ctx->has_runtime ? ctx->runtime.event_sequence : 0);
  qa_animation_step_result result; memset(&result, 0, sizeof result);
  result.animation = input.animation; return result;
}
static qa_movement_continuation link_cb(void *v, qa_owned_actor actor, qa_movement_state state, bool touch) {
  (void)v; (void)actor; (void)touch;
  qa_movement_continuation result; memset(&result, 0, sizeof result);
  result.is_continue = true; result.state = state; return result;
}
static bool is_bsp_cb(void *v, qa_trace_hit hit) {
  step_ctx *ctx = (step_ctx *)v;
  return ctx->configuration->is_brush ? ctx->configuration->is_brush(ctx->configuration->host_ctx, hit) : hit.kind == QA_TRACE_HIT_WORLD;
}
static qa_movement_continuation phase_cb(void *v, qa_q1_player_input input, qa_movement_state state) {
  (void)v; (void)input;
  qa_movement_continuation result; memset(&result, 0, sizeof result);
  result.is_continue = true; result.state = state; return result;
}
static bool firing_cb(void *v, qa_q3_hook_context context) {
  (void)v; return (((long long)context.command.buttons) & 1) != 0 && context.motion->health > 0;
}
static qa_animation_step_result q3_animation_cb(void *v, qa_q3_animation_request request, qa_q3_hook_context context) {
  step_ctx *ctx = (step_ctx *)v;
  if (!ctx->configuration->movement_only && context.animation.state.kind && strcmp(context.animation.state.kind, "q3") == 0)
    return qa_q3_source_animation(request, &context);
  qa_animation_step_result result; memset(&result, 0, sizeof result);
  result.animation = context.animation; return result;
}
static qa_animation_step_result q3_torso_cb(void *v, qa_q3_hook_context context) {
  step_ctx *ctx = (step_ctx *)v;
  if (!ctx->configuration->movement_only && context.animation.state.kind && strcmp(context.animation.state.kind, "q3") == 0)
    return qa_q3_source_torso(11, &context, true);
  qa_animation_step_result result; memset(&result, 0, sizeof result);
  result.animation = context.animation; return result;
}
static qa_q3_weapon_phase_result q3_weapon_cb(void *v, qa_q3_hook_context context) {
  step_ctx *ctx = (step_ctx *)v;
  qa_weapon_step_input input; memset(&input, 0, sizeof input);
  input.actor = ctx->configuration->actor;
  input.command = ctx->command;
  if (input.command.kind == QA_USER_COMMAND_Q3) {
    input.command.as.q3.buttons = context.command.buttons;
    input.command.as.q3.weapon = context.command.weapon;
  }
  input.frame = context.frame; input.arsenal = context.arsenal; input.animation = context.animation;
  input.environment = ctx->environment; input.gauntlet_hit = ctx->options.gauntlet_hit;
  qa_q3_weapon_phase_result result; memset(&result, 0, sizeof result);
  result.base = weapon_step(ctx, input);
  result.movement_flags = !ctx->has_runtime ? context.motion->pm_flags
      : (double)(((long long)context.motion->pm_flags & ~(QA_Q3_FLAG_RESPAWNED | QA_Q3_FLAG_USE_ITEM_HELD))
          | (ctx->runtime.respawned ? QA_Q3_FLAG_RESPAWNED : 0) | (ctx->runtime.use_item_held ? QA_Q3_FLAG_USE_ITEM_HELD : 0));
  return result;
}
static qa_q3_postures postures_cb(void *v, qa_q3_movement_input input) {
  step_ctx *ctx = (step_ctx *)v;
  if (ctx->source.animation.state.kind && strcmp(ctx->source.animation.state.kind, "q3") == 0) return qa_q3_source_postures;
  (void)input;
  qa_q3_postures postures; memset(&postures, 0, sizeof postures);
  postures.standing_view_height = ctx->configuration->standing_view_height;
  postures.crouched.bounds.min = ctx->configuration->standing_bounds.min;
  postures.crouched.bounds.max = ctx->configuration->standing_bounds.max; postures.crouched.bounds.max.z = 4;
  postures.crouched.view_height = -2;
  postures.dead.bounds.min = ctx->configuration->standing_bounds.min;
  postures.dead.bounds.max = ctx->configuration->standing_bounds.max; postures.dead.bounds.max.z = -8;
  postures.dead.view_height = -16;
  postures.invulnerability_expanded = qa_q3_source_postures.invulnerability_expanded;
  return postures;
}
static qa_trace_policy trace_policy_cb(void *v, qa_q3_movement_input input) {
  step_ctx *ctx = (step_ctx *)v; (void)input;
  qa_trace_policy policy; memset(&policy, 0, sizeof policy);
  policy.kind = "q3"; policy.contents_mask = ctx->options.has_trace_mask ? ctx->options.trace_mask : 0;
  policy.curves = true; policy.player_curve_clip = true; return policy;
}

qa_app_prediction_step_result qa_app_predict_movement_command(const qa_app_movement_probe_options *configuration,
    qa_app_movement_prediction_snapshot snapshot, qa_app_prediction_command entry,
    qa_app_prediction_step_options options, const qa_trace_shape *shape_in) {
  step_ctx ctx; memset(&ctx, 0, sizeof ctx);
  ctx.configuration = configuration; ctx.entry = entry; ctx.options = options;
  ctx.source = qa_app_copy_prediction_snapshot(snapshot);
  const qa_movement_profile *profile = &configuration->profile;
  qa_user_command command = entry.command;
  if (entry.angle_space && strcmp(entry.angle_space, "absolute") == 0) {
    if (configuration->relative_command) command = configuration->relative_command(configuration->host_ctx, entry, ctx.source.state);
    else {
      qa_app_sim_relative_movement_input relative_input = {entry.command, entry.angle_space};
      command = qa_app_sim_relative_movement_command(relative_input, ctx.source.state).command;
    }
  }
  bool arsenal_q3 = ctx.source.arsenal.state.kind && strcmp(ctx.source.arsenal.state.kind, "q3") == 0;
  if (!configuration->movement_only && arsenal_q3 && ctx.source.has_q3_arsenal) {
    qa_q3_arsenal_controls controls = resolve_controls(&ctx, ctx.source.arsenal, command, ctx.source.q3_arsenal.product);
    if (command.kind == QA_USER_COMMAND_Q3 && entry.has_arsenal) {
      command.as.q3.weapon = controls.requested_weapon;
      command.as.q3.buttons = (double)((((long long)command.as.q3.buttons) & ~4) | (controls.use_holdable ? 4 : 0));
    }
  } else if (!configuration->movement_only && entry.has_arsenal) {
    if (!entry.arsenal.provider || !ctx.source.arsenal.provider || strcmp(entry.arsenal.provider, ctx.source.arsenal.provider) != 0)
      qa_range_error("Arsenal command belongs to a different provider");
    if (arsenal_q3) qa_range_error("Prediction has no selected arsenal owner for this intent");
  }
  qa_movement_environment environment = ctx.source.environment;
  if (ctx.source.state.kind == QA_MOVEMENT_STATE_Q3) environment.gravity_multiplier = 1;
  ctx.environment = environment; ctx.command = command;
  double duration = (command.kind == QA_USER_COMMAND_Q1 || command.kind == QA_USER_COMMAND_Q3)
      ? (entry.time_milliseconds - ctx.source.command_time_milliseconds > 0 ? entry.time_milliseconds - ctx.source.command_time_milliseconds : 0)
      : command.kind == QA_USER_COMMAND_QW ? command.as.qw.milliseconds
      : command.kind == QA_USER_COMMAND_Q2 ? command.as.q2.milliseconds : command.as.q2_rerelease.milliseconds;
  bool source_seconds = profile->kind && (strcmp(profile->kind, "q1-netquake") == 0 || strcmp(profile->kind, "q1-quakeworld") == 0);
  qa_frame_context frame; memset(&frame, 0, sizeof frame);
  frame.frame = entry.sequence; frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
  frame.time.kind = source_seconds ? QA_SOURCE_TIME_SECONDS : QA_SOURCE_TIME_MILLISECONDS;
  frame.time.value = source_seconds ? entry.time_milliseconds / 1000 : entry.time_milliseconds;
  frame.elapsed.kind = frame.time.kind;
  frame.elapsed.value = source_seconds ? duration / 1000 : duration;
  ctx.frame = frame;
  ctx.has_runtime = ctx.source.has_q3_arsenal;
  ctx.runtime = ctx.source.q3_arsenal;
  qa_movement_services services; memset(&services, 0, sizeof services);
  services.ctx = &ctx; services.scene = options.scene;
  const qa_numeric_operations *numeric = qa_create_numeric_operations(&profile->numeric);
  if (numeric) services.numeric = *numeric;
  services.touch = touch_cb; services.weapon_step = weapon_cb; services.animation_step = animation_cb;
  qa_trace_shape shape;
  if (shape_in) shape = *shape_in;
  else { memset(&shape, 0, sizeof shape); shape.kind = "box"; shape.bounds = configuration->standing_bounds; }
  qa_movement_input base; memset(&base, 0, sizeof base);
  base.actor = configuration->actor; base.command_sequence = entry.sequence; base.frame = frame;
  base.shape = shape; base.current_bounds = &ctx.source.bounds; base.environment = environment;
  base.arsenal = ctx.source.arsenal; base.animation = ctx.source.animation; base.execution = "prediction";
  base.command = command; base.state = ctx.source.state; base.profile = *profile;
  qa_movement_state state = ctx.source.state;
  qa_q1_movement_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.ctx = &ctx; hooks.link = link_cb; hooks.is_bsp = is_bsp_cb;
  hooks.before_physics = phase_cb; hooks.after_physics = phase_cb;
  qa_q1_movement_options q1_options; memset(&q1_options, 0, sizeof q1_options);
  q1_options.has_view_height = true; q1_options.view_height = configuration->standing_view_height;
  q1_options.hooks = &hooks;
  qa_movement_result result; memset(&result, 0, sizeof result);
  const char *pk = profile->kind ? profile->kind : "";
  if (strcmp(pk, "q1-netquake") == 0 && state.kind == QA_MOVEMENT_STATE_Q1 && command.kind == QA_USER_COMMAND_Q1) {
    base.kind = "q1-netquake";
    qa_movement_provider provider = qa_create_q1_movement_provider(profile->id, q1_options);
    result = provider.move(provider.ctx, base, services); free(provider.ctx);
  } else if (strcmp(pk, "q1-quakeworld") == 0 && state.kind == QA_MOVEMENT_STATE_QW && command.kind == QA_USER_COMMAND_QW) {
    base.kind = "q1-quakeworld";
    bool stance = environment.client_outputs && environment.client_outputs->has_stance;
    if (stance) { base.shape.kind = "box"; base.shape.bounds = ctx.source.bounds; }
    qa_movement_provider provider = qa_create_qw_movement_provider(profile->id, q1_options);
    result = provider.move(provider.ctx, base, services); free(provider.ctx);
  } else if (strcmp(pk, "q2-classic") == 0 && state.kind == QA_MOVEMENT_STATE_Q2 && command.kind == QA_USER_COMMAND_Q2) {
    base.kind = "q2-classic"; base.profile.snap_initial = false;
    qa_movement_provider provider = qa_create_q2_classic_movement_provider(profile->id);
    result = provider.move(provider.ctx, base, services);
  } else if (strcmp(pk, "q2-rerelease") == 0 && state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE && command.kind == QA_USER_COMMAND_Q2_RERELEASE) {
    base.kind = "q2-rerelease"; base.snap_initial = options.first_command; base.view_offset = ctx.source.view_offset;
    qa_movement_provider provider = qa_create_q2_rerelease_movement_provider(profile->id, options.rerelease_movement);
    result = provider.move(provider.ctx, base, services);
  } else if (strcmp(pk, "q3") == 0 && state.kind == QA_MOVEMENT_STATE_Q3 && command.kind == QA_USER_COMMAND_Q3) {
    base.kind = "q3";
    base.profile.has_fixed_milliseconds = options.has_fixed_milliseconds;
    base.profile.fixed_milliseconds = options.fixed_milliseconds;
    base.profile.no_footsteps = options.no_footsteps;
    qa_q3_movement_provider_options *provider_options = malloc(sizeof *provider_options);
    if (!provider_options) qa_range_error("Out of memory");
    memset(provider_options, 0, sizeof *provider_options);
    provider_options->ctx = &ctx; provider_options->id = profile->id;
    provider_options->hooks.ctx = &ctx; provider_options->hooks.firing = firing_cb;
    provider_options->hooks.animation = q3_animation_cb; provider_options->hooks.torso = q3_torso_cb;
    provider_options->hooks.weapon = q3_weapon_cb;
    provider_options->postures = postures_cb;
    provider_options->trace_policy = options.has_trace_mask ? trace_policy_cb : NULL;
    qa_movement_provider provider = qa_q3_create_movement_provider(provider_options);
    result = provider.move(provider.ctx, base, services);
    free(provider_options);
  } else {
    qa_range_error("Prediction command %d and state %d do not match selected %s", (int)command.kind, (int)state.kind, pk);
  }
  if (!result.status || strcmp(result.status, "active") != 0)
    qa_range_error("Private movement prediction cannot remove an authoritative actor");
  if (result.kind && (strcmp(result.kind, "q2-classic") == 0 || strcmp(result.kind, "q2-rerelease") == 0)
      && result.arsenal.state.kind && strcmp(result.arsenal.state.kind, "q3") == 0) {
    qa_weapon_step_input input; memset(&input, 0, sizeof input);
    input.actor = configuration->actor; input.command = command; input.frame = frame;
    input.environment = environment; input.arsenal = result.arsenal; input.animation = result.animation;
    input.gauntlet_hit = options.gauntlet_hit;
    qa_weapon_step_result stepped = weapon_step(&ctx, input);
    size_t previous = result.effect_count;
    qa_ordered_movement_effect *effects = malloc((previous + stepped.effect_count + 1) * sizeof *effects);
    if (!effects) qa_range_error("Out of memory");
    if (previous) memcpy(effects, result.effects, previous * sizeof *effects);
    for (size_t i = 0; i < stepped.effect_count; i++) {
      effects[previous + i].effect = stepped.effects[i]; effects[previous + i].substep = 0;
      effects[previous + i].sequence = (double)(previous + i); effects[previous + i].time = frame.time;
    }
    result.arsenal = stepped.arsenal; result.animation = stepped.animation;
    result.effects = effects; result.effect_count = previous + stepped.effect_count;
  }
  if (configuration->movement_only) { result.arsenal = ctx.source.arsenal; result.animation = ctx.source.animation; }
  qa_app_prediction_step_result out; memset(&out, 0, sizeof out);
  out.player = ctx.source; out.player.environment = environment;
  out.player.sequence = entry.sequence; out.player.command_time_milliseconds = entry.time_milliseconds;
  out.player.state = result.state; out.player.arsenal = result.arsenal; out.player.animation = result.animation;
  out.player.bounds = result.bounds; out.player.view_angles = result.view_angles; out.player.view_height = result.view_height;
  out.player.has_contact = true; out.player.contact_ground = result.ground;
  out.player.contact_water_level = result.water_level; out.player.contact_water_type = result.water_type;
  out.player.has_q3_arsenal = ctx.has_runtime; out.player.q3_arsenal = ctx.runtime;
  out.result = result;
  return out;
}
