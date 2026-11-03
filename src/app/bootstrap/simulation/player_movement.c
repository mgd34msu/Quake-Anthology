/* Port of src/app/bootstrap/simulation/player-movement.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/player_movement.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/shared_quakeworld_movement.h"
#include "qa/content/q3/foundation/movement_hooks.h"
#include "qa/foundation/support.h"
#include "qa/movement/q1/netquake.h"
#include "qa/movement/q1/quakeworld.h"
#include "qa/movement/q2/index.h"
#include "qa/movement/q3/animation.h"
#include "qa/movement/q3/constants.h"
#include "qa/movement/q3/postures.h"
#include "qa/movement/q3/provider.h"
#include "qa/foundation/js.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
/* sourceMovement.ladders: ClientThink writes it (true/false); spectator and saved
 * options leave it undefined. 1 true, 0 false, -1 undefined. */
static int source_ladders(const qa_app_sim_client_movement_options *movement) {
  return movement->order == QA_APP_SIM_MOVEMENT_ORDER_THINK ? (movement->ladders ? 1 : 0) : -1;
}
static qa_movement_state relocated(qa_movement_state state, qa_vec3 origin, qa_vec3 velocity) {
  if (state.kind == QA_MOVEMENT_STATE_Q2) {
    state.as.q2.origin_eighths.values[0] = trunc(origin.x * 8);
    state.as.q2.origin_eighths.values[1] = trunc(origin.y * 8);
    state.as.q2.origin_eighths.values[2] = trunc(origin.z * 8);
    state.as.q2.velocity_eighths.values[0] = trunc(velocity.x * 8);
    state.as.q2.velocity_eighths.values[1] = trunc(velocity.y * 8);
    state.as.q2.velocity_eighths.values[2] = trunc(velocity.z * 8);
    return state;
  }
  switch (state.kind) {
    case QA_MOVEMENT_STATE_Q1: state.as.q1.origin = origin; state.as.q1.velocity = velocity; break;
    case QA_MOVEMENT_STATE_QW: state.as.qw.origin = origin; state.as.qw.velocity = velocity; break;
    case QA_MOVEMENT_STATE_Q2_RERELEASE: state.as.q2_rerelease.origin = origin; state.as.q2_rerelease.velocity = velocity; break;
    case QA_MOVEMENT_STATE_Q3: state.as.q3.origin = origin; state.as.q3.velocity = velocity; break;
    case QA_MOVEMENT_STATE_Q2: break;
  }
  return state;
}
qa_app_sim_locomotion_player qa_app_sim_capture_player_locomotion(qa_app_sim_locomotion_player player) { return player; }
bool qa_app_sim_player_locomotion_matches(qa_app_sim_locomotion_player player, qa_app_sim_locomotion_player previous) {
  return player.flight == previous.flight
      && memcmp(&player.profile, &previous.profile, sizeof player.profile) == 0
      && player.character == previous.character
      && memcmp(&player.standing_bounds, &previous.standing_bounds, sizeof player.standing_bounds) == 0
      && player.world_gravity == previous.world_gravity
      && player.has_q2_movement_config == previous.has_q2_movement_config
      && (!player.has_q2_movement_config || (player.q2_movement_config.air_accelerate == previous.q2_movement_config.air_accelerate
          && player.q2_movement_config.n64_physics == previous.q2_movement_config.n64_physics))
      && player.has_source_movement == previous.has_source_movement
      && (!player.has_source_movement || (player.source_movement.has_trace_mask == previous.source_movement.has_trace_mask
          && player.source_movement.trace_mask == previous.source_movement.trace_mask
          && player.source_movement.has_fixed_msec == previous.source_movement.has_fixed_msec
          && player.source_movement.fixed_msec == previous.source_movement.fixed_msec
          && player.source_movement.no_footsteps == previous.source_movement.no_footsteps
          && source_ladders(&player.source_movement) == source_ladders(&previous.source_movement)));
}
qa_trace_policy qa_app_sim_player_trace_policy(qa_app_sim_locomotion_player player) {
  qa_trace_policy out; memset(&out, 0, sizeof out);
  if (is(player.profile.kind, "q1-netquake") || is(player.profile.kind, "q1-quakeworld")) {
    out.kind = "q1"; out.move = "normal"; out.has_hull = false; return out;
  }
  if (is(player.profile.kind, "q2-classic")) { out.kind = "q2"; out.contents_mask = 0x02010003; out.leaf_contents = "stored"; return out; }
  if (is(player.profile.kind, "q2-rerelease")) { out.kind = "q2"; out.contents_mask = 0x02010003; out.leaf_contents = "merged"; return out; }
  out.kind = "q3";
  out.contents_mask = (player.has_source_movement && player.source_movement.has_trace_mask) ? player.source_movement.trace_mask : 0x02010001;
  out.curves = true; out.player_curve_clip = true;
  return out;
}
qa_bounds qa_app_sim_player_standing_bounds(qa_game_family character) {
  if (character == QA_GAME_FAMILY_Q3) return qa_q3_source_standing_bounds;
  return (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
}
qa_q3_postures qa_app_sim_player_postures(qa_game_family character, qa_bounds standing_bounds) {
  if (character == QA_GAME_FAMILY_Q3) return qa_q3_source_postures;
  qa_q3_postures out; memset(&out, 0, sizeof out);
  out.standing_view_height = 22;
  out.crouched.bounds.min = standing_bounds.min; out.crouched.bounds.max = standing_bounds.max; out.crouched.bounds.max.z = 4;
  out.crouched.view_height = -2;
  out.dead.bounds.min = standing_bounds.min; out.dead.bounds.max = standing_bounds.max; out.dead.bounds.max.z = -8;
  out.dead.view_height = -16;
  out.invulnerability_expanded = qa_q3_source_postures.invulnerability_expanded;
  return out;
}
qa_app_sim_locomotion_player qa_app_sim_locomotion_template(qa_executable_recipe recipe) {
  qa_game_family character = qa_app_sim_provider_family(recipe.character.definition.provider);
  qa_app_sim_locomotion_player out; memset(&out, 0, sizeof out);
  out.character = character;
  out.profile = qa_app_sim_movement_profile(recipe);
  out.world_gravity = 800;
  out.standing_bounds = qa_app_sim_player_standing_bounds(character);
  return out;
}
qa_bounds qa_app_sim_player_crouched_bounds(qa_app_sim_locomotion_player player) {
  return qa_app_sim_player_postures(player.character, player.standing_bounds).crouched.bounds;
}
qa_movement_profile qa_app_sim_selected_movement_profile(qa_app_sim_selected_profile_player player, const qa_movement_profile *profile) {
  qa_movement_profile selected = profile ? *profile : player.profile;
  if (is(selected.kind, "q1-netquake") || is(selected.kind, "q1-quakeworld")) {
    selected.parameters.gravity = player.world_gravity; return selected;
  }
  if (is(selected.kind, "q3") && player.has_source_movement) {
    selected.has_fixed_milliseconds = player.source_movement.has_fixed_msec;
    selected.fixed_milliseconds = player.source_movement.fixed_msec;
    selected.no_footsteps = player.source_movement.no_footsteps;
    selected.ladders_disabled = source_ladders(&player.source_movement) == 0; /* ladders: sourceMovement.ladders !== false */
    selected.has_ladders = true;
    return selected;
  }
  if (player.has_q2_movement_config && is(selected.kind, "q2-classic")) {
    selected.air_accelerate = player.q2_movement_config.air_accelerate; return selected;
  }
  if (player.has_q2_movement_config && is(selected.kind, "q2-rerelease")) {
    selected.air_accelerate = player.q2_movement_config.air_accelerate;
    selected.n64_physics = player.q2_movement_config.n64_physics;
    return selected;
  }
  return selected;
}
qa_movement_environment qa_app_sim_player_movement_environment(qa_app_sim_movement_environment_player player, qa_combat_state combat) {
  qa_movement_environment out;
  if (!player.has_source_environment) {
    memset(&out, 0, sizeof out);
    out.has_speed_multiplier = player.movement_speed_multiplier != 1;
    out.speed_multiplier = player.movement_speed_multiplier;
    out.health = combat.health;
    out.flight = player.flight && combat.health > 0;
    out.haste = false;
    out.invulnerable = combat.invulnerable;
    out.gravity_multiplier = player.state.kind == QA_MOVEMENT_STATE_Q3 ? 1 : player.gravity_multiplier;
    return out;
  }
  out = player.source_environment;
  if (player.movement_speed_multiplier != 1) { out.has_speed_multiplier = true; out.speed_multiplier = player.movement_speed_multiplier; }
  out.flight = (out.flight || player.flight) && combat.health > 0;
  out.health = combat.health;
  out.invulnerable = out.invulnerable || combat.invulnerable;
  out.gravity_multiplier = player.state.kind == QA_MOVEMENT_STATE_Q3 ? 1 : out.gravity_multiplier;
  return out;
}
typedef struct posture_env {
  qa_game_family character; qa_bounds standing;
  qa_app_sim_client_movement_options source;
} posture_env;
static qa_q3_postures posture_fn(void *ctx, qa_q3_movement_input input) {
  (void)input; posture_env *e = ctx;
  return qa_app_sim_player_postures(e->character, e->standing);
}
static qa_trace_policy policy_fn(void *ctx, qa_q3_movement_input input) {
  (void)input; posture_env *e = ctx;
  qa_trace_policy out; memset(&out, 0, sizeof out);
  out.kind = "q3"; out.contents_mask = e->source.has_trace_mask ? e->source.trace_mask : 0x02010001;
  out.curves = true; out.player_curve_clip = true; return out;
}
qa_movement_provider qa_app_sim_create_player_movement_provider(qa_app_sim_provider_player player, qa_app_sim_player_movement_hooks hooks) {
  qa_movement_profile profile = player.profile;
  if (is(profile.kind, "q1-netquake")) return qa_create_q1_movement_provider(profile.id, hooks.q1);
  if (is(profile.kind, "q1-quakeworld")) {
    if (hooks.native_quakeworld) return qa_create_qw_movement_provider(profile.id, hooks.q1);
    qa_app_sim_provider_player adjusted = player; adjusted.view_height = 22;
    qa_q3_postures postures = qa_app_sim_player_postures(adjusted.character, adjusted.standing_bounds);
    qa_app_sim_qw_postures qw = {postures.standing_view_height, postures.crouched};
    return qa_app_sim_create_shared_quakeworld_movement(profile.id, hooks.q1, player.standing_bounds, qw,
        hooks.publish_posture, hooks.publish_ctx);
  }
  if (is(profile.kind, "q2-classic")) return qa_create_q2_classic_movement_provider(profile.id);
  if (is(profile.kind, "q2-rerelease")) return qa_create_q2_rerelease_movement_provider(profile.id, hooks.q2);
  /* q3: options storage is caller-owned (hooks.q3_options_storage). */
  qa_q3_movement_provider_options *storage = hooks.q3_options_storage;
  if (storage == NULL) qa_range_error("Out of memory");
  memset(storage, 0, sizeof *storage);
  storage->id = profile.id; storage->hooks = hooks.q3;
  posture_env *penv = malloc(sizeof *penv);
  if (!penv) qa_range_error("Out of memory");
  penv->character = player.character; penv->standing = player.standing_bounds;
  penv->source = player.source_movement;
  storage->ctx = penv; storage->postures = posture_fn;
  if (player.has_source_movement) storage->trace_policy = policy_fn;
  return qa_q3_create_movement_provider(storage);
}
void qa_app_sim_release_player_movement_provider(qa_app_sim_player_movement_hooks hooks, qa_movement_provider provider) {
  if (is(provider.kind, "q1-netquake")) free(provider.ctx);
  else if (is(provider.kind, "q1-quakeworld")) {
    if (hooks.native_quakeworld) free(provider.ctx); else qa_app_sim_shared_quakeworld_movement_destroy(provider);
  } else if (is(provider.kind, "q3") && hooks.q3_options_storage != NULL) {
    free(hooks.q3_options_storage->ctx); hooks.q3_options_storage->ctx = NULL;
  }
}
struct qa_app_sim_player_movement_prediction {
  qa_app_sim_prediction_simulation simulation;
  qa_app_sim_movement_prediction_player player;
  qa_movement_profile profile;
  qa_movement_state baseline, initial;
  qa_movement_environment environment;
  double milliseconds; bool crouched; bool native_quakeworld;
  double entity_weapon; bool has_entity_weapon;
  qa_movement_provider provider;
  qa_q2_rerelease_movement_context q2_context;
  qa_q3_movement_provider_options q3_options;
  qa_q3_arsenal_runtime_state runtime;
  qa_movement_services services;
};
static qa_movement_continuation pred_touch(void *ctx, qa_movement_touch_contact contact, qa_movement_state state) {
  (void)ctx; (void)contact;
  qa_movement_continuation out; memset(&out, 0, sizeof out); out.is_continue = true; out.state = state; return out;
}
static qa_weapon_step_result pred_weapon(void *ctx, qa_weapon_step_input input, qa_movement_state state) {
  (void)ctx; (void)state;
  qa_weapon_step_result out; memset(&out, 0, sizeof out); out.arsenal = input.arsenal; out.animation = input.animation; return out;
}
static qa_q3_arsenal_runtime_state pred_runtime_read(void *ctx, qa_owned_actor actor, const char *execution) {
  (void)actor; (void)execution;
  return ((qa_app_sim_player_movement_prediction *)ctx)->runtime;
}
static void pred_runtime_write(void *ctx, qa_owned_actor actor, const char *execution, qa_q3_arsenal_runtime_state state) {
  (void)actor; (void)execution;
  ((qa_app_sim_player_movement_prediction *)ctx)->runtime = state;
}
static bool pred_gauntlet(void *ctx, qa_q3_hook_context context) { (void)ctx; (void)context; return false; }
/* TS createPlayerMovementPrediction non-Q3-arsenal q3Hooks. */
static bool pred_q3_firing(void *ctx, qa_q3_hook_context context) { (void)ctx; (void)context; return false; }
static qa_animation_step_result pred_q3_animation(void *ctx, qa_q3_animation_request request, qa_q3_hook_context context) {
  (void)ctx;
  if (is(context.animation.state.kind, "q3")) return qa_q3_source_animation(request, &context);
  qa_animation_step_result out; memset(&out, 0, sizeof out); out.animation = context.animation; return out;
}
static qa_animation_step_result pred_q3_torso(void *ctx, qa_q3_hook_context context) {
  (void)ctx;
  if (is(context.animation.state.kind, "q3")) return qa_q3_source_torso(QA_Q3_ANIM_TORSO_STAND, &context, true);
  qa_animation_step_result out; memset(&out, 0, sizeof out); out.animation = context.animation; return out;
}
static qa_q3_weapon_phase_result pred_q3_weapon(void *ctx, qa_q3_hook_context context) {
  (void)ctx;
  qa_q3_weapon_phase_result out; memset(&out, 0, sizeof out);
  out.base.arsenal = context.arsenal; out.base.animation = context.animation;
  out.movement_flags = context.motion->pm_flags;
  return out;
}
static qa_animation_step_result pred_anim(void *ctx, qa_animation_step_input input) {
  (void)ctx;
  qa_animation_step_result out; memset(&out, 0, sizeof out); out.animation = input.animation; return out;
}
qa_app_sim_player_movement_prediction *qa_app_sim_create_player_movement_prediction(
    qa_app_sim_prediction_simulation simulation, qa_app_sim_movement_prediction_player player,
    qa_vec3 origin, qa_vec3 velocity, double milliseconds, bool crouched, const qa_movement_profile *selected_profile) {
  qa_app_sim_player_movement_prediction *p = calloc(1, sizeof *p);
  if (!p) qa_range_error("Out of memory");
  p->simulation = simulation; p->player = player; p->milliseconds = milliseconds; p->crouched = crouched;
  qa_app_sim_selected_profile_player sel; memset(&sel, 0, sizeof sel);
  sel.profile = player.profile; sel.world_gravity = player.world_gravity;
  sel.has_source_movement = player.has_source_movement; sel.source_movement = player.source_movement;
  sel.has_q2_movement_config = player.has_q2_movement_config; sel.q2_movement_config = player.q2_movement_config;
  p->profile = selected_profile ? *selected_profile : qa_app_sim_selected_movement_profile(sel, NULL);
  qa_combat_state_result combat = simulation.combat_read(simulation.ctx, player.actor.id);
  qa_combat_state combat_state;
  if (combat.has_value) combat_state = combat.value;
  else if (player.has_source_environment) {
    memset(&combat_state, 0, sizeof combat_state);
    combat_state.health = player.source_environment.health;
    combat_state.invulnerable = player.source_environment.invulnerable;
  } else qa_range_error("Player has no combat state during movement prediction");
  qa_app_sim_movement_environment_player envp; memset(&envp, 0, sizeof envp);
  envp.has_source_environment = player.has_source_environment; envp.source_environment = player.source_environment;
  envp.gravity_multiplier = player.gravity_multiplier; envp.state = player.state; envp.flight = player.flight;
  envp.movement_speed_multiplier = player.movement_speed_multiplier;
  p->environment = qa_app_sim_player_movement_environment(envp, combat_state);
  qa_movement_state baseline;
  bool has_baseline = is(p->profile.kind, "q3") && simulation.q3_baseline
      && simulation.q3_baseline(simulation.ctx, player.client, &baseline, &p->entity_weapon, &p->has_entity_weapon);
  p->baseline = has_baseline ? baseline : player.state;
  p->initial = relocated(p->baseline, origin, velocity);
  if (p->initial.kind == QA_MOVEMENT_STATE_Q3)
    p->initial.as.q3.movement_flags = (qa_js_to_int32(p->initial.as.q3.movement_flags) & ~2) | (crouched ? 1 : 0);
  qa_q3_movement_hooks q3_hooks; memset(&q3_hooks, 0, sizeof q3_hooks);
  if (is(player.arsenal.state.kind, "q3")) {
    const char *product = "baseq3";
    qa_q3_arsenal_runtime_state runtime;
    bool got = simulation.q3_arsenal_runtime && simulation.q3_arsenal_runtime(simulation.ctx, player.client, &product, &runtime);
    p->runtime = got ? runtime : player.has_q3_arsenal ? player.q3_arsenal : qa_q3_spawn_arsenal_runtime("baseq3", 100, 0);
    qa_q3_arsenal_runtime_access access; memset(&access, 0, sizeof access);
    access.ctx = p; access.read = pred_runtime_read; access.write = pred_runtime_write; access.gauntlet_hit = pred_gauntlet;
    q3_hooks = qa_q3_create_source_movement_hooks(access);
    (void)product;
  } else {
    q3_hooks.firing = pred_q3_firing;
    q3_hooks.animation = pred_q3_animation;
    q3_hooks.torso = pred_q3_torso;
    q3_hooks.weapon = pred_q3_weapon;
  }
  p->native_quakeworld = simulation.native_quakeworld;
  qa_app_sim_provider_player prov; memset(&prov, 0, sizeof prov);
  prov.profile = player.profile; prov.character = player.character; prov.standing_bounds = player.standing_bounds;
  prov.view_height = player.view_height; prov.has_source_movement = player.has_source_movement; prov.source_movement = player.source_movement;
  qa_q1_movement_options q1; memset(&q1, 0, sizeof q1); q1.has_view_height = true; q1.view_height = player.view_height;
  qa_q2_rerelease_movement_context_init(&p->q2_context);
  qa_app_sim_player_movement_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.native_quakeworld = p->native_quakeworld; hooks.q1 = q1; hooks.q2 = &p->q2_context; hooks.q3 = q3_hooks;
  hooks.q3_options_storage = &p->q3_options;
  p->provider = qa_app_sim_create_player_movement_provider(prov, hooks);
  memset(&p->services, 0, sizeof p->services);
  p->services.scene = simulation.scene; p->services.numeric = player.numeric;
  p->services.touch = pred_touch; p->services.weapon_step = pred_weapon; p->services.animation_step = pred_anim;
  return p;
}
void qa_app_sim_player_movement_prediction_destroy(qa_app_sim_player_movement_prediction *prediction) {
  if (prediction == NULL) return;
  qa_app_sim_player_movement_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.native_quakeworld = prediction->native_quakeworld; hooks.q3_options_storage = &prediction->q3_options;
  qa_app_sim_release_player_movement_provider(hooks, prediction->provider);
  free(prediction);
}
qa_movement_provider qa_app_sim_player_movement_prediction_provider(qa_app_sim_player_movement_prediction *prediction) { return prediction->provider; }
qa_movement_services qa_app_sim_player_movement_prediction_services(qa_app_sim_player_movement_prediction *prediction) { return prediction->services; }
qa_movement_input qa_app_sim_player_movement_prediction_input(qa_app_sim_player_movement_prediction *p,
    const qa_movement_result *previous, double index, qa_vec3 command_move) {
  if (previous && is(previous->status, "actor-removed")) qa_range_error("Movement prediction removed its actor");
  qa_movement_state state = previous ? previous->state : p->initial;
  double yaw = atan2(command_move.y, command_move.x);
  double horizontal = qa_js_min(400, hypot(command_move.x, command_move.y));
  double up = qa_js_max(-400, qa_js_min(400, command_move.z));
  qa_vec3 angles = {0, yaw * 180 / 3.141592653589793, 0};
  double words[3] = {0, (double)(qa_js_to_int32(qa_js_round(yaw * 65536 / (3.141592653589793 * 2))) & 65535), 0};
  double time = (p->baseline.kind == QA_MOVEMENT_STATE_Q3 ? p->baseline.as.q3.command_time_milliseconds : p->simulation.time_seconds * 1000) + (index + 1) * p->milliseconds;
  qa_bounds bounds = (is(p->profile.kind, "q1-quakeworld") && !p->native_quakeworld)
      ? (previous ? previous->bounds : (p->crouched ? qa_app_sim_player_crouched_bounds((qa_app_sim_locomotion_player){p->player.profile, p->player.standing_bounds, p->player.has_source_movement, p->player.source_movement, p->player.character, p->player.world_gravity, p->player.has_q2_movement_config, p->player.q2_movement_config, p->player.flight}) : p->player.bounds))
      : p->player.standing_bounds;
  qa_movement_input out; memset(&out, 0, sizeof out);
  out.actor = p->player.actor; out.command_sequence = index; out.execution = "prediction";
  out.shape.kind = "box"; out.shape.bounds = bounds;
  out.frame.frame = index;
  out.frame.time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, time};
  out.frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, p->milliseconds};
  out.frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
  out.environment = p->environment;
  out.arsenal = previous ? previous->arsenal : p->player.arsenal;
  out.animation = previous ? previous->animation : p->player.animation;
  out.profile = p->profile; out.state = state;
  if (is(p->profile.kind, "q1-netquake") && state.kind == QA_MOVEMENT_STATE_Q1) {
    out.kind = "q1-netquake";
    out.command.kind = QA_USER_COMMAND_Q1;
    out.command.as.q1.acknowledged_server_time_seconds = time / 1000;
    out.command.as.q1.view_angles = angles; out.command.as.q1.forward_move = horizontal;
    out.command.as.q1.up_move = up; out.command.as.q1.buttons = up > 0 ? 2 : 0;
    return out;
  }
  if (is(p->profile.kind, "q1-quakeworld") && state.kind == QA_MOVEMENT_STATE_QW) {
    out.kind = "q1-quakeworld";
    out.command.kind = QA_USER_COMMAND_QW; out.command.as.qw.milliseconds = p->milliseconds;
    out.command.as.qw.angles = angles; out.command.as.qw.forward_move = horizontal;
    out.command.as.qw.up_move = up; out.command.as.qw.buttons = up > 0 ? 2 : 0;
    return out;
  }
  if (is(p->profile.kind, "q2-classic") && state.kind == QA_MOVEMENT_STATE_Q2) {
    out.kind = "q2-classic";
    out.command.kind = QA_USER_COMMAND_Q2; out.command.as.q2.milliseconds = p->milliseconds;
    for (int i = 0; i < 3; i++) out.command.as.q2.angle_shorts.values[i] = words[i] - state.as.q2.delta_angle_shorts.values[i];
    out.command.as.q2.forward_move = horizontal; out.command.as.q2.up_move = up;
    return out;
  }
  if (is(p->profile.kind, "q2-rerelease") && state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
    out.kind = "q2-rerelease";
    out.view_offset = (qa_vec3){0, 0, p->player.view_height}; out.snap_initial = true;
    out.command.kind = QA_USER_COMMAND_Q2_RERELEASE; out.command.as.q2_rerelease.milliseconds = p->milliseconds;
    out.command.as.q2_rerelease.angles = (qa_vec3){-state.as.q2_rerelease.delta_angles.x, angles.y - state.as.q2_rerelease.delta_angles.y, -state.as.q2_rerelease.delta_angles.z};
    out.command.as.q2_rerelease.forward_move = horizontal;
    out.command.as.q2_rerelease.buttons = up > 0 ? 8 : up < 0 ? 16 : 0;
    out.command.as.q2_rerelease.server_frame = index;
    return out;
  }
  if (is(p->profile.kind, "q3") && state.kind == QA_MOVEMENT_STATE_Q3) {
    out.kind = "q3";
    out.command.kind = QA_USER_COMMAND_Q3; out.command.as.q3.server_time_milliseconds = time;
    out.command.as.q3.angle_words.values[0] = -state.as.q3.delta_angle_words.values[0];
    out.command.as.q3.angle_words.values[1] = (double)((qa_js_to_int32(words[1] - state.as.q3.delta_angle_words.values[1])) & 65535);
    out.command.as.q3.angle_words.values[2] = -state.as.q3.delta_angle_words.values[2];
    out.command.as.q3.forward_move = qa_js_round(horizontal * 127 / 400);
    out.command.as.q3.up_move = qa_js_round(up * 127 / 400);
    out.command.as.q3.weapon = p->has_entity_weapon ? p->entity_weapon
        : (is(p->player.arsenal.state.kind, "q3") ? p->player.arsenal.state.source_weapon : 0);
    return out;
  }
  qa_range_error("Player state differs from selected movement");
}
qa_app_sim_movement_observation qa_app_sim_movement_observation_of(qa_movement_result result) {
  const char *medium = result.water_level == 0 ? "dry"
      : (is(result.kind, "q1-netquake") || is(result.kind, "q1-quakeworld"))
        ? (result.water_type == -4 ? "slime" : result.water_type == -5 ? "lava" : "water")
        : ((qa_js_to_int32(result.water_type) & 16) ? "slime" : (qa_js_to_int32(result.water_type) & 8) ? "lava" : "water");
  bool fall = false;
  for (size_t i = 0; i < result.effect_count; i++)
    if (result.effects[i].effect.kind == QA_MOVEMENT_EFFECT_EVENT
        && (result.effects[i].effect.value.event == QA_Q3_EV_FALL_MEDIUM || result.effects[i].effect.value.event == QA_Q3_EV_FALL_FAR)) fall = true;
  qa_app_sim_movement_observation out = {qa_app_sim_movement_origin(result.state), result.ground.kind != QA_TRACE_HIT_NONE, medium, fall};
  return out;
}
