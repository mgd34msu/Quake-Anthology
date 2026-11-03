/* Port of src/app/bootstrap/simulation/runtime.ts, SharedSimulation source
 * region: q1ActorHost (2738-2780), q2ActorHost (2782-2816) and createSource
 * (3303-3880), the one dispatch over the six source kinds: native Q2
 * (classic/rerelease guest), Q3 QVM guest, QuakeC, TS Q3, TS Q1, TS Q2.
 * Each TS closure is a static function whose ctx is a record kept by the
 * simulation (qa_app_shared_sim_keep) holding what the closure captured.
 * GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/simulation/classic_guest_source.h"
#include "qa/app/bootstrap/simulation/native_q1_pusher.h"
#include "qa/app/bootstrap/simulation/native_q2_map.h"
#include "qa/app/bootstrap/simulation/q2_native_world.h"
#include "qa/content/q2/rerelease/world_text.h"
#include "qa/app/bootstrap/simulation/quakec_map.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"
#include "qa/app/bootstrap/simulation/save.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include "qa/network/common/js_number.h"
#include "qa/content/q3/base/settings.h"
#include "qa/content/q3/team_arena/foreign_objectives.h"
#include "qa/app/bootstrap/simulation/source_transition.h"
#include "qa/content/catalog/source_program.h"
#include "qa/content/composition/q2/match_selection.h"
#include "qa/content/q1/quakec/id1_attacks.h"
#include "qa/content/q1/quakec/id1_environment.h"
#include "qa/content/q1/quakec/id1_program.h"
#include "qa/content/q1/quakec/id1_projectiles.h"
#include "qa/compat/qc/machine.h"
#include "qa/compat/q2/classic/host.h"
#include "qa/bots/navigation/rerelease_path.h"
#include <time.h>
#include "qa/content/q2/foundation/weapons/presentation.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/settings/server/index.h"
#include "qa/settings/server/q2_owner.h"
#include "qa/world/actors/body.h"

static noreturn void ss_fail(const char *format, ...) {
  char message[1024];
  va_list args; va_start(args, format); vsnprintf(message, sizeof message, format, args); va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static qa_shared_body_table *ss_bodies(qa_app_shared_simulation *sim) { return qa_app_sim_shared_physics_bodies(sim->physics); }
static double ss_time_seconds(const qa_app_shared_simulation *sim) { return qa_ss_seconds(sim->source_frame.time); }
static const char *ss_cvar_flag(bool value) { return value ? "1" : "0"; }
static void ss_cvar_set(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, name, value, true, &snapshot)) qa_cvar_read_free(&snapshot);
}
static void ss_cvar_register(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(cvars, name, value, 0, &snapshot)) qa_cvar_read_free(&snapshot);
}
static bool ss_cvar_exists(qa_cvar_registry *cvars, const char *name) {
  qa_cvar_read read;
  if (!qa_cvar_registry_find(cvars, name, &read)) return false;
  qa_cvar_read_free(&read);
  return true;
}
static void ss_print_event(void *ctx, const char *text) { qa_app_shared_sim_print(ctx, text, NULL); }
static void ss_cvar_registry_release(void *value) { qa_cvar_registry_destroy(value); }
static qa_cvar_registry *ss_cvar_registry(qa_app_shared_simulation *sim, qa_command_dialect dialect,
    void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_cvar_registry_options options; memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context.session = sim->session; options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  options.print = print; options.print_ctx = print_ctx;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  if (!cvars) qa_range_error("Out of memory");
  return cvars;
}

/* ---- ActorHostRuntime (source-hosts.ts) ---- */
typedef struct ss_runtime {
  qa_app_shared_simulation *sim;
  qa_app_sim_actor_host_runtime runtime;
  const char *frame_kind; /* "q2-classic" | "q2-rerelease" | "native-classic" | "native-rerelease" | "elapsed" */
  double frame_milliseconds;
} ss_runtime;
static double ss_runtime_now(void *ctx) { return ss_time_seconds(((ss_runtime *)ctx)->sim); }
static double ss_runtime_frame_seconds(void *ctx) {
  ss_runtime *runtime = ctx;
  if (qa_ss_is(runtime->frame_kind, "q2-classic")) return 0.1;
  if (qa_ss_is(runtime->frame_kind, "q2-rerelease")) return runtime->frame_milliseconds / 1000;
  return qa_ss_seconds(runtime->sim->source_frame.elapsed);
}
/* schedule: (actor, due) => due === null ? this.scheduler.cancel(actor) : this.schedule(actor, due) */
static void ss_runtime_schedule(void *ctx, qa_owned_actor actor, const double *due_seconds) {
  ss_runtime *runtime = ctx;
  if (due_seconds == NULL) qa_frame_scheduler_cancel(runtime->sim->scheduler, actor);
  else qa_app_shared_sim_schedule(runtime->sim, actor, *due_seconds);
}
static qa_app_sim_actor_host_runtime ss_make_runtime(qa_app_shared_simulation *sim, qa_numeric_profile numeric,
    qa_app_sim_source_random *random, const char *frame_kind, double frame_milliseconds) {
  ss_runtime *runtime = qa_app_shared_sim_keep(sim, sizeof *runtime);
  runtime->sim = sim; runtime->frame_kind = frame_kind; runtime->frame_milliseconds = frame_milliseconds;
  runtime->runtime = (qa_app_sim_actor_host_runtime){numeric, random, runtime, ss_runtime_now, ss_runtime_frame_seconds, ss_runtime_schedule};
  return runtime->runtime;
}

/* ---- q1ActorHost (2738-2780) ---- */
typedef struct ss_q1_host {
  qa_app_shared_simulation *sim;
  qa_content_id content;
  qa_q1_monster_movement *movement;
  qa_q1_monster_movement owned_movement;
  qa_numeric_profile visibility_numeric;
  qa_app_sim_actor_host_runtime runtime;
  const char *authority;
  qa_weapon_behavior_projectile_port weapon_behavior;
  qa_original_pickup_admission original_pickups;
  qa_app_sim_q1_actor_host_env env;
  qa_app_sim_native_q1_pusher *pusher; /* the latest game's createNativeQ1PusherServices owner */
} ss_q1_host;
static qa_q1_monster_target_result ss_q1_monster_target(void *ctx, qa_actor_id actor) {
  return qa_app_shared_sim_monster_target_q1(((ss_q1_host *)ctx)->sim, actor);
}
static void ss_q1_register_entity(void *ctx, qa_q1_actor *entity, qa_q1_services *services) {
  ss_q1_host *host = ctx;
  qa_app_sim_actor_execution entry; memset(&entry, 0, sizeof entry);
  entry.kind = QA_APP_SIM_ACTOR_EXECUTION_Q1; entry.q1_entity = entity; entry.q1_services = services; entry.content = host->content;
  entry.actor = entity->actor;
  qa_app_shared_sim_register_actor_execution(host->sim, entry);
}
static bool ss_q1_source_target(void *ctx, qa_actor_id actor, qa_q1_source_target *out) {
  ss_q1_host *host = ctx; qa_app_shared_simulation *sim = host->sim;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  qa_client_id client;
  bool player = qa_app_shared_sim_player_client(sim, actor, &client);
  qa_spatial_actor linked; bool has_linked = qa_shared_linked_actor(sim->scene, actor, &linked);
  memset(out, 0, sizeof *out);
  out->player = player;
  if (player) out->aimed_damage = true;
  else if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) {
    qa_app_sim_quakec_weapon_target target;
    out->aimed_damage = qa_app_sim_quakec_source_weapon_target(entry->quakec->ctx, actor, &target) && target.aimed_damage;
  } else if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) out->aimed_damage = entry->q1_entity->aimed_damage;
  else out->aimed_damage = entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 && (qa_js_to_int32(entry->q2_entity->server_flags) & 4) != 0;
  out->slidebox = entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 ? qa_ss_is(entry->q1_entity->solid, "slidebox")
      : has_linked && qa_ss_is(linked.collision.role, "solid") && linked.collision.monster && !qa_ss_is(linked.collision.shape_kind, "model");
  out->push = entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1
      ? qa_ss_is(entry->q1_entity->movement, "push") || qa_ss_is_q1_barrel(entry->q1_entity->classname)
      : entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2
      ? qa_ss_is(entry->q2_entity->motion, "push") || qa_ss_is(entry->q2_entity->motion, "stop")
      : qa_ss_is(qa_app_shared_sim_grapple_anchor(sim, actor), "brush");
  return true;
}
static double ss_q1_random(void *ctx) { return qa_app_sim_source_random_next_unit(((ss_q1_host *)ctx)->runtime.random); }
static bool ss_q1_walk_move(void *ctx, qa_owned_actor actor, double yaw, double distance) {
  return qa_q1_monster_walk_move(((ss_q1_host *)ctx)->movement, actor, yaw, distance);
}
static bool ss_q1_check_bottom(void *ctx, qa_actor_id actor) { return qa_q1_monster_check_bottom(((ss_q1_host *)ctx)->movement, actor); }
static void ss_q1_move_to_goal(void *ctx, qa_owned_actor actor, qa_actor_id goal, double distance, const char *mode) {
  qa_q1_monster_move_to_goal(((ss_q1_host *)ctx)->movement, actor, goal, distance, mode);
}
static void ss_q1_change_yaw(void *ctx, qa_owned_actor actor) { qa_q1_monster_change_yaw(((ss_q1_host *)ctx)->movement, actor); }
static void ss_q1_pusher_release(void *value) { qa_app_sim_native_q1_pusher_destroy(value); }
/* pusherServices: game => createNativeQ1PusherServices(game, this.physics). */
static qa_q1_pusher_services ss_q1_pusher_services(void *ctx, qa_q1_services *game) {
  ss_q1_host *host = ctx;
  if (host->pusher == NULL || host->pusher->game != game || host->pusher->physics != host->sim->physics) {
    host->pusher = qa_app_sim_native_q1_pusher_create(game, host->sim->physics);
    if (host->pusher == NULL) qa_range_error("Out of memory");
    qa_app_shared_sim_keep_owned(host->sim, host->pusher, ss_q1_pusher_release);
  }
  return qa_app_sim_native_q1_pusher_services(host->pusher);
}
static void ss_q1_schedule_think(void *ctx, qa_owned_actor actor, double due) {
  ss_q1_host *host = ctx; host->runtime.schedule(host->runtime.ctx, actor, &due);
}
static void ss_q1_cancel_think(void *ctx, qa_owned_actor actor) {
  ss_q1_host *host = ctx; host->runtime.schedule(host->runtime.ctx, actor, NULL);
}
static void ss_q1_emit(void *ctx, qa_q1_event event) {
  ss_q1_host *host = ctx; qa_app_shared_simulation *sim = host->sim;
  if (qa_ss_is(event.kind, "weapon")) {
    qa_ss_view_model model; memset(&model, 0, sizeof model);
    model.path = event.view_model; model.frame = event.frame;
    qa_ss_map_set(&sim->view_models, &event.player, &model);
    qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, event.player);
    if (player && event.attack.has) {
      qa_q1_character_actor **character = qa_ss_map_get(&sim->q1_characters, &player->actor);
      qa_q1_base_character_attack attack = {event.attack.kind, (int)event.attack.variant};
      if (character) qa_q1_character_actor_attack(*character, attack);
    }
  }
  if (qa_ss_is(event.kind, "intermission"))
    for (size_t i = 0; i < sim->player_states.count; i++) {
      qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
      player->view_height = 0;
      qa_q2_player_movement_change change; memset(&change, 0, sizeof change);
      change.kind = "freeze"; change.origin = event.origin; change.angles = event.angles;
      qa_app_shared_sim_set_player_movement(sim, player->actor.id, change);
    }
  if (qa_ss_is(event.kind, "teleport-player")) {
    if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, event.player);
    qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, event.player);
    if (player) {
      player->view_angles = event.angles;
      if (player->state.kind == QA_MOVEMENT_STATE_Q1) {
        player->state.as.q1.view_angles = event.angles;
        player->state.as.q1.teleport_time_seconds = event.lock_until;
      }
    }
  }
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q1"; source.q1 = &event;
  qa_source_time time = {QA_SOURCE_TIME_SECONDS, host->runtime.now(host->runtime.ctx)};
  qa_app_shared_sim_emit(sim, host->content, source, &time, NULL);
}
static void ss_q1_transition(void *ctx, qa_transition_intent intent) {
  ss_q1_host *host = ctx;
  qa_transition_intent transition = qa_app_sim_source_level_transition(intent, host->sim->recipe, host->authority);
  qa_event_ring_admit(&host->sim->transitions, &transition);
}
static const qa_actor_id *ss_q1_players(void *ctx, size_t *out_count) { return qa_app_shared_sim_players(((ss_q1_host *)ctx)->sim, out_count); }
static const char *ss_q1_classname(void *ctx, qa_actor_id actor) { return qa_app_shared_sim_classname(((ss_q1_host *)ctx)->sim, actor); }
static bool ss_q1_check_client(void *ctx, qa_owned_actor observer, qa_actor_id *out) {
  ss_q1_host *host = ctx;
  qa_q1_client_eye eye;
  if (!qa_app_shared_sim_q1_visibility_eye(host->sim, observer.id, &eye)) return false;
  return qa_q1_client_visibility_check(host->sim->q1_client_visibility, eye, host->runtime.now(host->runtime.ctx),
      *qa_create_numeric_operations(&host->visibility_numeric), out);
}
static void ss_q1_powerup(void *ctx, qa_owned_actor actor, qa_q1_powerup powerup, double expires) {
  qa_app_shared_sim_powerup(((ss_q1_host *)ctx)->sim, actor, powerup, expires);
}
static void ss_q1_control_player(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 view_offset) {
  qa_app_shared_sim_control_player(((ss_q1_host *)ctx)->sim, actor, origin, angles, view_offset);
}
static void ss_set_actor_gravity_scale(qa_app_shared_simulation *sim, qa_actor_id actor, double scale) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  if (!player) return;
  player->gravity_multiplier = scale;
  double gravity = trunc(qa_app_sim_shared_physics_gravity(sim->physics) * scale);
  if (player->state.kind == QA_MOVEMENT_STATE_Q2) player->state.as.q2.gravity = gravity;
  else if (player->state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) player->state.as.q2_rerelease.gravity = gravity;
  else if (player->state.kind == QA_MOVEMENT_STATE_Q3) player->state.as.q3.gravity = gravity;
}
static void ss_q1_set_gravity(void *ctx, qa_actor_id actor, double scale) { ss_set_actor_gravity_scale(((ss_q1_host *)ctx)->sim, actor, scale); }
static qa_vec3 ss_q1_punch_read(void *ctx, qa_actor_id actor) { return qa_app_sim_q1_player_punch_read(((ss_q1_host *)ctx)->sim->q1_punch, actor); }
static void ss_q1_punch_write(void *ctx, qa_actor_id actor, qa_vec3 angles) { qa_app_sim_q1_player_punch_write(((ss_q1_host *)ctx)->sim->q1_punch, actor, angles); }
static bool ss_world_actor_fn(void *ctx, qa_actor_id *out) { return qa_app_shared_sim_world_actor(ctx, out); }
static double ss_source_order_fn(void *ctx, qa_actor_id a, qa_actor_id b) { return qa_app_shared_sim_source_order(ctx, a, b); }
static const qa_actor_id *ss_observation_ids(void *ctx, size_t *out_count) {
  static __thread qa_actor_id *storage; static __thread size_t capacity;
  qa_app_shared_simulation *sim = ctx;
  size_t count = 0; const qa_actor_observation *observations = qa_session_actor_registry_observations(sim->actors, &count);
  if (count + 1 > capacity) {
    capacity = count + 16;
    storage = realloc(storage, capacity * sizeof *storage);
    if (!storage) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < count; i++) storage[i] = observations[i].id;
  *out_count = count;
  return storage;
}
static bool ss_body_read_fn(void *ctx, qa_actor_id actor, qa_body_state *out) { return qa_app_shared_sim_body(ctx, actor, out); }
static qa_app_sim_actor_host_world ss_host_world(qa_app_shared_simulation *sim, qa_numeric_profile numeric) {
  qa_app_sim_actor_host_world world; memset(&world, 0, sizeof world);
  world.scene = sim->scene; world.numeric = numeric; world.ctx = sim;
  world.world_actor = ss_world_actor_fn; world.source_order = ss_source_order_fn;
  world.observations = ss_observation_ids; world.body_read = ss_body_read_fn;
  return world;
}
/* TS weaponBehavior: the SimulationWeaponBehaviors projectile port. */
static bool ss_port_controls(void *ctx, qa_actor_id projectile) {
  return qa_app_sim_simulation_weapon_behaviors_controls_trajectory(ctx, projectile);
}
static qa_weapon_trajectory_update_result ss_port_launch(void *ctx, qa_weapon_behavior_launch input) {
  return qa_app_sim_simulation_weapon_behaviors_launch(ctx, input);
}
static qa_weapon_trajectory_update_result ss_port_step(void *ctx, qa_owned_actor projectile, qa_body_state body, double time_seconds) {
  return qa_app_sim_simulation_weapon_behaviors_step(ctx, projectile, body, time_seconds);
}
static qa_weapon_behavior_projectile_port ss_weapon_port(qa_app_shared_simulation *sim) {
  return (qa_weapon_behavior_projectile_port){sim->weapon_behavior, ss_port_controls, ss_port_launch, ss_port_step};
}

qa_q1_host qa_app_shared_sim_q1_actor_host(qa_app_shared_simulation *sim, qa_provider_reference source,
    qa_app_sim_actor_host_runtime runtime, const char *role) {
  ss_q1_host *host = qa_app_shared_sim_keep(sim, sizeof *host);
  host->sim = sim; host->content = source.content; host->runtime = runtime;
  host->authority = role ? role : "actor-source";
  if (qa_ss_is(source.provider, sim->recipe.map.entities.provider)) host->movement = &sim->q1_movement;
  else { qa_app_shared_sim_create_monster_movement(sim, runtime.numeric, runtime.random, &host->owned_movement); host->movement = &host->owned_movement; }
  host->visibility_numeric = runtime.numeric;
  host->weapon_behavior = ss_weapon_port(sim);
  host->original_pickups = qa_shared_original_pickup_admission_as_contract(sim->original_pickups);
  qa_q1_host bindings; memset(&bindings, 0, sizeof bindings);
  bindings.ctx = host;
  bindings.has_punch_angles = true; bindings.punch_read = ss_q1_punch_read; bindings.punch_write = ss_q1_punch_write;
  bindings.weapon_behavior = &host->weapon_behavior;
  bindings.actors = sim->actors; bindings.bodies = ss_bodies(sim); bindings.callbacks = sim->callbacks;
  bindings.combat = sim->combat; bindings.inventory = sim->inventory; bindings.original_pickups = &host->original_pickups;
  bindings.monster_target = ss_q1_monster_target;
  bindings.register_entity = ss_q1_register_entity;
  bindings.source_target = ss_q1_source_target;
  bindings.random = ss_q1_random;
  bindings.walk_move = ss_q1_walk_move; bindings.check_bottom = ss_q1_check_bottom;
  bindings.move_to_goal = ss_q1_move_to_goal; bindings.change_yaw = ss_q1_change_yaw;
  bindings.pusher_services = ss_q1_pusher_services;
  bindings.schedule_think = ss_q1_schedule_think; bindings.cancel_think = ss_q1_cancel_think;
  bindings.emit = ss_q1_emit; bindings.transition = ss_q1_transition;
  bindings.players = ss_q1_players; bindings.classname = ss_q1_classname;
  bindings.check_client = ss_q1_check_client; bindings.powerup = ss_q1_powerup;
  bindings.control_player = ss_q1_control_player; bindings.set_gravity = ss_q1_set_gravity;
  return qa_app_sim_create_q1_actor_host(&host->env, bindings, ss_host_world(sim, runtime.numeric));
}

/* ---- q2ActorHost (2782-2816) ---- */
typedef struct ss_q2_host {
  qa_app_shared_simulation *sim;
  qa_content_id content;
  qa_provider_reference source;
  qa_app_sim_actor_host_runtime runtime;
  qa_ss_q2_monster_fn read_monster; void *read_monster_ctx;
  const char *authority;
  qa_weapon_behavior_projectile_port weapon_behavior;
  qa_original_pickup_admission original_pickups;
  qa_app_sim_q2_actor_host_env env;
} ss_q2_host;
typedef struct ss_q2_monster_read { ss_q2_host *host; qa_actor_id actor; } ss_q2_monster_read;
static qa_q2m_state *ss_q2_read_monster(void *ctx) {
  ss_q2_monster_read *read = ctx;
  return read->host->read_monster ? read->host->read_monster(read->host->read_monster_ctx, read->actor) : NULL;
}
static double ss_q2_gravity(void *ctx) { return qa_app_sim_shared_physics_gravity(((ss_q2_host *)ctx)->sim->physics); }
static qa_q2_monster_target_result ss_q2_monster_target(void *ctx, qa_actor_id actor) {
  qa_q1_monster_target_result target = qa_app_shared_sim_monster_target_q1(((ss_q2_host *)ctx)->sim, actor);
  return (qa_q2_monster_target_result){target.has_value, target.value};
}
static qa_q2_weapon_target_result ss_q2_weapon_target(void *ctx, qa_actor_id actor) {
  return qa_app_shared_sim_q2_weapon_target(((ss_q2_host *)ctx)->sim, actor);
}
static void ss_q2_register_entity(void *ctx, qa_q2_entity *entity, qa_q2_entity_services *services) {
  ss_q2_host *host = ctx;
  ss_q2_monster_read *read = qa_app_shared_sim_keep(host->sim, sizeof *read);
  read->host = host; read->actor = entity->actor.id;
  qa_app_sim_actor_execution entry; memset(&entry, 0, sizeof entry);
  entry.kind = QA_APP_SIM_ACTOR_EXECUTION_Q2; entry.q2_entity = entity; entry.q2_services = services;
  entry.content = host->content; entry.actor = entity->actor;
  entry.ctx = read; entry.read_monster = ss_q2_read_monster;
  qa_app_shared_sim_register_actor_execution(host->sim, entry);
}
static double ss_q2_now(void *ctx) { ss_q2_host *host = ctx; return host->runtime.now(host->runtime.ctx); }
static double ss_q2_frame_seconds(void *ctx) { ss_q2_host *host = ctx; return host->runtime.frame_seconds(host->runtime.ctx); }
static double ss_q2_random(void *ctx) { return qa_app_sim_source_random_next_unit(((ss_q2_host *)ctx)->runtime.random); }
static void ss_q2_schedule(void *ctx, qa_owned_actor actor, const double *due) {
  ss_q2_host *host = ctx; host->runtime.schedule(host->runtime.ctx, actor, due);
}
static qa_actor_id ss_q2_world_actor(void *ctx) {
  qa_actor_id actor;
  if (!qa_app_shared_sim_world_actor(((ss_q2_host *)ctx)->sim, &actor)) ss_fail("Map has no source world actor");
  return actor;
}
static qa_q2_player_view_result ss_q2_player_view_state(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q2_host *)ctx)->sim;
  qa_q2_player_view_result result; memset(&result, 0, sizeof result);
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(sim, actor, &client)) return result;
  result.has_value = true;
  result.value.view_angles = qa_app_shared_sim_player_view(sim, actor).angles;
  if ((sim->strategy->as_q2(sim) != NULL)) {
    qa_q2_base_player_state *state = qa_q2_players_get_state(sim->source.product->players, actor);
    result.value.old_velocity = state ? state->old_velocity : qa_ss_zero;
  } else result.value.old_velocity = sim->grapple ? qa_app_sim_grapple_runtime_previous_velocity(sim->grapple, actor) : qa_ss_zero;
  return result;
}
static void ss_q2_prepare_level_change(void *ctx, const char *map, const qa_q2_landmark_carry *landmark, double server_flags) {
  qa_app_shared_simulation *sim = ((ss_q2_host *)ctx)->sim;
  free(sim->level_change_map);
  sim->has_level_change = true; sim->level_change_map = qa_ss_strdup(map);
  sim->has_level_change_landmark = landmark != NULL; if (landmark) sim->level_change_landmark = *landmark;
  sim->level_change_server_flags = server_flags;
}
static const qa_actor_id *ss_q2_players(void *ctx, size_t *out_count) { return qa_app_shared_sim_players(((ss_q2_host *)ctx)->sim, out_count); }
static bool ss_q2_is_player(void *ctx, qa_actor_id actor) {
  qa_client_id client; return qa_app_shared_sim_player_client(((ss_q2_host *)ctx)->sim, actor, &client);
}
static bool ss_q2_is_monster(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q2_host *)ctx)->sim;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) {
    qa_app_sim_quakec_weapon_target target;
    return qa_app_sim_quakec_source_weapon_target(entry->quakec->ctx, actor, &target) && target.monster;
  }
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2) return (qa_js_to_int32(entry->q2_entity->server_flags) & 4) != 0;
  return entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 && entry->q1_entity->monster != NULL;
}
static void ss_q2_touch_triggers(void *ctx, qa_owned_actor actor) {
  qa_app_sim_shared_physics_touch_triggers(((ss_q2_host *)ctx)->sim->physics, actor);
}
static void ss_q2_key_consumed(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q2_host *)ctx)->sim;
  if ((sim->strategy->as_q2(sim) == NULL)) return;
  qa_q2_game_services *game = sim->source.product->services;
  qa_q2_entity *entity = game->entity(game->ctx, &actor);
  if (entity) qa_q2_players_consumed_key(sim->source.product->players, entity, game);
}
static void ss_q2_set_solid(void *ctx, qa_owned_actor actor, const char *solid, const double *model) {
  qa_app_sim_shared_solid shared; memset(&shared, 0, sizeof shared);
  shared.solid = solid; shared.has_model = model != NULL; if (model) shared.model = *model; shared.family = "q2";
  qa_app_sim_shared_physics_set_solid(((ss_q2_host *)ctx)->sim->physics, actor, shared);
}
static void ss_q2_set_motion(void *ctx, qa_q2_motion motion) {
  qa_app_shared_simulation *sim = ((ss_q2_host *)ctx)->sim;
  qa_app_sim_shared_physics_set_motion(sim->physics, motion.actor, motion);
  /* A Q1-movement player's corpse is integrated by its movement provider,
   * so the source death motion must project onto the NetQuake movetype
   * (mirrors the foreign-character host in attachQ2Character). */
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, motion.actor.id);
  if (player && player->state.kind == QA_MOVEMENT_STATE_Q1
      && (qa_ss_is(motion.kind, "bounce") || qa_ss_is(motion.kind, "toss") || qa_ss_is(motion.kind, "new-toss")))
    player->state.as.q1.move_type = qa_ss_is(motion.kind, "bounce") ? 10 : 6;
}
static void ss_q2_set_area_portal(void *ctx, double portal, bool open) {
  qa_app_shared_simulation *sim = ((ss_q2_host *)ctx)->sim;
  qa_ss_map_set(&sim->area_portals, &portal, &open);
  qa_shared_set_area_portal_state(sim->scene, portal, open);
}
static void ss_q2_emit(void *ctx, qa_q2_presentation_event event) {
  ss_q2_host *host = ctx; qa_app_shared_simulation *sim = host->sim;
  if (qa_ss_is(event.kind, "model")) {
    qa_ss_map_set(&sim->source_models, &event.actor, &event);
    if (qa_ss_map_has(&sim->source_model_read_order, &event.actor)) qa_ss_map_delete(&sim->source_model_read_order, &event.actor);
  }
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q2"; source.q2 = &event;
  qa_source_time time = (sim->strategy->as_q2(sim) != NULL) && qa_ss_is(host->source.content, sim->recipe.map.entities.content)
      ? sim->source_frame.time : (qa_source_time){QA_SOURCE_TIME_SECONDS, host->runtime.now(host->runtime.ctx)};
  qa_app_shared_sim_emit(sim, host->content, source, &time, NULL);
}
static void ss_q2_transition(void *ctx, qa_transition_intent intent) {
  ss_q2_host *host = ctx; qa_app_shared_simulation *sim = host->sim;
  if (qa_ss_is(host->authority, "primary-world") && !sim->checking_q2_rules && (sim->strategy->as_q2(sim) != NULL)
      && qa_ss_is(intent.kind, "campaign-level")) {
    char *map = NULL;
    const char *name = sim->has_level_change ? sim->level_change_map : NULL;
    if (!name) {
      /* intent.map.replace(/^q2:/, "") */
      const char *full = intent.map ? intent.map : "";
      map = qa_ss_strdup(strncmp(full, "q2:", 3) == 0 ? full + 3 : full);
      name = map;
    }
    qa_q2_players_begin_intermission(sim->source.product->players, sim->source.product->services, name,
        sim->has_level_change && sim->has_level_change_landmark ? &sim->level_change_landmark : NULL);
    free(map);
    return;
  }
  qa_transition_intent transition = qa_app_sim_source_level_transition(intent, sim->recipe, host->authority);
  qa_event_ring_admit(&sim->transitions, &transition);
}
static void ss_q2_diagnostic(void *ctx, const char *message) { qa_app_shared_sim_print(((ss_q2_host *)ctx)->sim, message, NULL); }

qa_q2_foundation_host qa_app_shared_sim_q2_actor_host(qa_app_shared_simulation *sim, qa_provider_reference source,
    qa_app_sim_actor_host_runtime runtime, qa_ss_q2_monster_fn monster, void *monster_ctx, const char *role) {
  ss_q2_host *host = qa_app_shared_sim_keep(sim, sizeof *host);
  host->sim = sim; host->content = source.content; host->source = source; host->runtime = runtime;
  host->read_monster = monster; host->read_monster_ctx = monster_ctx;
  host->authority = role ? role : "actor-source";
  host->weapon_behavior = ss_weapon_port(sim);
  host->original_pickups = qa_shared_original_pickup_admission_as_contract(sim->original_pickups);
  qa_q2_foundation_host bindings; memset(&bindings, 0, sizeof bindings);
  bindings.ctx = host;
  bindings.weapon_behavior = &host->weapon_behavior;
  bindings.actors = sim->actors; bindings.bodies = ss_bodies(sim); bindings.callbacks = sim->callbacks;
  bindings.combat = sim->combat; bindings.inventory = sim->inventory; bindings.original_pickups = &host->original_pickups;
  bindings.gravity = ss_q2_gravity;
  bindings.monster_target = ss_q2_monster_target; bindings.weapon_target = ss_q2_weapon_target;
  bindings.register_entity = ss_q2_register_entity;
  if (qa_app_sim_source_random_has_rerelease(runtime.random)) bindings.rerelease_random = qa_app_sim_source_random_rerelease(runtime.random);

  bindings.now = ss_q2_now; bindings.frame_seconds = ss_q2_frame_seconds; bindings.random = ss_q2_random; bindings.schedule = ss_q2_schedule;
  bindings.world_actor = ss_q2_world_actor;
  bindings.player_view_state = ss_q2_player_view_state;
  bindings.prepare_level_change = ss_q2_prepare_level_change;
  bindings.players = ss_q2_players; bindings.is_player = ss_q2_is_player; bindings.is_monster = ss_q2_is_monster;
  bindings.touch_triggers = ss_q2_touch_triggers; bindings.key_consumed = ss_q2_key_consumed;
  bindings.set_solid = ss_q2_set_solid; bindings.set_motion = ss_q2_set_motion; bindings.set_area_portal = ss_q2_set_area_portal;
  bindings.emit = ss_q2_emit; bindings.transition = ss_q2_transition; bindings.diagnostic = ss_q2_diagnostic;
  qa_app_sim_actor_host_world world = ss_host_world(sim, runtime.numeric);
  world.leaf_contents = source.content && strstr(source.content, ":rerelease:") ? "merged" : "stored";
  return qa_app_sim_create_q2_actor_host(&host->env, bindings, world);
}

/* ---- createSource: native Q2 (3307-3387) ---- */
typedef struct ss_native {
  qa_app_shared_simulation *sim;
  qa_app_sim_actor_host_runtime runtime;
  const qa_app_sim_native_q2_guest_options *guest;
  qa_q2_foundation_host engine, rerelease_engine;
  qa_compat_q2_classic_damage_provenance provenance;
  qa_compat_q2_rerelease_foreign_damage_services foreign_damage;
  qa_original_pickup_admission pickups;
  char *map_name, *map_entities, *spawn_point;
  bool has_revisit;
  const qa_q2_classic_visited_level *classic_revisit;
  const qa_app_sim_q2_rerelease_visited_level *rerelease_revisit;
  qa_classic_original_save_files *files;
  qa_windows_capabilities capabilities;
} ss_native;
static qa_attack_provenance ss_native_provenance_value(qa_app_shared_simulation *sim) {
  qa_attack_provenance attack; memset(&attack, 0, sizeof attack);
  attack.sequence = sim->attack_sequence++;
  attack.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, ss_time_seconds(sim)};
  attack.weapon = NULL;
  attack.weapon_provider = sim->recipe.map.entities.provider; attack.combat_provider = sim->recipe.combat.provider;
  attack.inventory_provider = sim->recipe.inventory.provider; attack.movement_provider = sim->recipe.movement.provider;
  return attack;
}
/* TS damageProvenance: () => ({...}): the C table passes the call's actors, which TS does not read. */
static qa_attack_provenance ss_native_provenance(void *ctx, qa_actor_id attacker, qa_actor_id inflictor, qa_actor_id target) {
  (void)attacker; (void)inflictor; (void)target;
  return ss_native_provenance_value(((ss_native *)ctx)->sim);
}
static void ss_native_admit(void *ctx, qa_raw_entity_view record, qa_owned_actor actor) {
  ss_native *native = ctx;
  qa_session_actor_registry_assert_owned(native->sim->actors, actor);
  qa_actor_id_result current = record.current_actor(record.ctx);
  if (!current.has_value || !qa_same_actor(current.value, actor.id))
    ss_fail("Native actor projection lost shared identity");
}
static void ss_native_collision(void *ctx, qa_owned_actor actor, qa_actor_collision collision) {
  qa_app_sim_shared_physics_set_collision(((ss_native *)ctx)->sim->physics, actor, collision);
}
static void ss_native_print(void *ctx, const char *text) {
  ss_native *native = ctx; native->guest->print(native->guest->print_ctx, text);
}
static const char *const *ss_native_command(void *ctx, size_t *out_count, const char **out_args) {
  (void)ctx; *out_count = 0; *out_args = ""; return NULL;
}
static void ss_native_add_command(void *ctx, const char *text) {
  ss_native *native = ctx; native->guest->add_command(native->guest->cmd_ctx, text);
}
static void ss_native_debug_graph(void *ctx, double value, double color) {
  ss_native *native = ctx; native->guest->debug_graph(native->guest->graph_ctx, value, color);
}
static void ss_native_debug_shapes(void *ctx, qa_compat_q2_rerelease_debug_shapes_event event) {
  qa_app_shared_sim_submit_debug_shapes(((ss_native *)ctx)->sim, event);
}
static void ss_native_world_text(void *ctx, qa_compat_q2_rerelease_world_text_event event) {
  qa_app_shared_simulation *sim = ((ss_native *)ctx)->sim;
  /* worldText: event => worldTextStore.submit({ ...event.text, content }, timeSeconds, event.lifetime): event.text is
   * q2WorldText(...) of the guest arguments (RereleaseWorldTextImports); the store copies the text. */
  const qa_compat_q2_world_text_input *in = &event.text; char buffer[512];
  qa_world_text text = qa_q2_world_text(in->origin, in->has_angles ? &in->angles : NULL, in->text != NULL ? in->text : "", in->color,
      in->size, in->depth_test, buffer, sizeof buffer);
  text.content = sim->recipe.map.entities.content;
  if (!qa_world_text_store_submit(&sim->world_text_store, &text, ss_time_seconds(sim), event.lifetime)) qa_range_error("Out of memory");
}
/* revisit.restoreServerState: world.restoreConfigstrings(...) then the area portals. */
static void ss_native_restore_portal(qa_app_shared_simulation *sim, double portal, bool open) {
  qa_ss_map_set(&sim->area_portals, &portal, &open);
  qa_shared_set_area_portal_state(sim->scene, portal, open);
}
static void ss_classic_revisit_restore(void *ctx, qa_app_sim_classic_guest_world *world) {
  ss_native *native = ctx;
  const qa_q2_classic_level_state *state = &native->classic_revisit->state;
  qa_app_sim_configstring *values = calloc(state->configstring_count ? state->configstring_count : 1, sizeof *values);
  if (!values) qa_range_error("Out of memory");
  for (size_t i = 0; i < state->configstring_count; i++) values[i] = (qa_app_sim_configstring){state->configstrings[i].index, state->configstrings[i].value};
  qa_app_sim_classic_guest_world_restore_configstrings(world, values, state->configstring_count);
  free(values);
  for (size_t i = 0; i < state->portal_count; i++) ss_native_restore_portal(native->sim, state->portals[i].portal, state->portals[i].open);
}
static void ss_rerelease_revisit_restore(void *ctx, qa_app_sim_rerelease_guest_world *world) {
  ss_native *native = ctx;
  const qa_app_sim_q2_rerelease_level_state *state = &native->rerelease_revisit->state;
  qa_app_sim_configstring *values = calloc(state->configstring_count ? state->configstring_count : 1, sizeof *values);
  if (!values) qa_range_error("Out of memory");
  for (size_t i = 0; i < state->configstring_count; i++) values[i] = (qa_app_sim_configstring){state->configstrings[i].index, state->configstrings[i].value};
  qa_app_sim_rerelease_guest_world_restore_configstrings(world, values, state->configstring_count);
  free(values);
  for (size_t i = 0; i < state->portal_count; i++) ss_native_restore_portal(native->sim, state->portals[i].portal, state->portals[i].open);
}
static bool ss_module_identity_equal(qa_module_identity a, qa_module_identity b) {
  return qa_ss_is(a.id, b.id) && qa_ss_is(a.artifact_path, b.artifact_path) && qa_ss_is(a.digest, b.digest)
      && ((a.revision == NULL && b.revision == NULL) || qa_ss_is(a.revision, b.revision));
}
static qa_windows_file *ss_native_open_file(void *ctx, const char *path, bool read, bool write, int creation) {
  /* TS { ...native.capabilities, openFile: files.openFile }: the overlay's
   * qa_q2_classic_file is field-for-field qa_windows_file. */
  return (qa_windows_file *)qa_classic_original_save_files_open_file(((ss_native *)ctx)->files, path, read, write, creation);
}
/* The spread's other capability members keep the caller's own ctx. */
#define SS_CAPS(ctx) ((const qa_windows_capabilities *)((ss_native *)(ctx))->guest->capabilities)
static bool ss_native_now(void *ctx, double *out) { return SS_CAPS(ctx)->now_milliseconds(SS_CAPS(ctx)->ctx, out); }
static bool ss_native_counter(void *ctx, int64_t *out) { return SS_CAPS(ctx)->performance_counter(SS_CAPS(ctx)->ctx, out); }
static void ss_native_stdout(void *ctx, qa_windows_stream stream, const uint8_t *bytes, size_t len) {
  SS_CAPS(ctx)->standard_output(SS_CAPS(ctx)->ctx, stream, bytes, len);
}
static size_t ss_native_stdin(void *ctx, size_t length, uint8_t *out) { return SS_CAPS(ctx)->standard_input(SS_CAPS(ctx)->ctx, length, out); }
#undef SS_CAPS
static qa_q2_classic_file *ss_native_fallback_open(void *ctx, const char *path, bool read, bool write, int creation) {
  const qa_windows_capabilities *capabilities = ctx;
  return capabilities->open_file ? (qa_q2_classic_file *)capabilities->open_file(capabilities->ctx, path, read, write, creation) : NULL;
}
static void ss_create_native(qa_app_shared_simulation *sim, bool loading, qa_provider_timing timing, qa_ss_source_runtime *out) {
  const qa_executable_recipe *recipe = &sim->recipe;
  const qa_app_sim_native_q2_guest_options *guest = qa_app_shared_sim_q2_guest(sim);
  bool classic = qa_ss_is(guest->edition, "classic");
  const qa_app_sim_native_q2_travel *retained = sim->options.native_q2_travel;
  if (retained) {
    /* nativeModuleIdentity(native.prepared) over the edition's prepared guest. */
    char revision[1024]; qa_module_identity expected;
    if (classic) {
      const qa_app_sim_prepared_classic_guest *prepared = guest->prepared;
      expected = qa_app_sim_prepared_module_identity(&prepared->execution, prepared->has_primary, &prepared->primary, revision, sizeof revision);
    } else {
      const qa_app_sim_prepared_rerelease_guest *prepared = guest->prepared;
      expected = qa_app_sim_prepared_module_identity(&prepared->execution, prepared->has_primary, &prepared->primary, revision, sizeof revision);
    }
    qa_module_identity world_module = retained->rerelease ? qa_app_sim_rerelease_guest_world_module(retained->rerelease_travel.world)
                                                          : qa_app_sim_classic_guest_world_module(retained->classic.world);
    double world_clients = retained->rerelease
        ? qa_app_sim_rerelease_guest_services_options_of(qa_app_sim_rerelease_guest_world_services(retained->rerelease_travel.world))->base.max_clients
        : qa_app_sim_classic_guest_services_options_of(qa_app_sim_classic_guest_world_services(retained->classic.world))->max_clients;
    if (retained->rerelease == classic || sim->options.restore || !ss_module_identity_equal(world_module, expected)
        || world_clients != sim->options.max_clients)
      ss_fail("Native travel requires the same source module and client capacity");
  }
  qa_cvar_registry *cvars = retained
      ? (retained->rerelease ? qa_app_sim_rerelease_guest_services_options_of(qa_app_sim_rerelease_guest_world_services(retained->rerelease_travel.world))->base.cvars
                             : qa_app_sim_classic_guest_services_options_of(qa_app_sim_classic_guest_world_services(retained->classic.world))->cvars)
      : !sim->options.restore && sim->options.source_registry ? sim->options.source_registry : NULL;
  ss_native *native = qa_app_shared_sim_keep(sim, sizeof *native);
  native->sim = sim; native->guest = guest;
  if (!cvars) cvars = ss_cvar_registry(sim, classic ? QA_COMMAND_DIALECT_Q2_CLASSIC : QA_COMMAND_DIALECT_Q2_RERELEASE,
                                       ss_native_print, native);
  if (!retained && !sim->options.source_registry)
    qa_cvar_registry_apply_archive(cvars, sim->options.source_archive, sim->options.source_archive_count);
  if (!retained) {
    char clients[32], skill[32];
    const char *names[] = {"maxclients", "skill", "deathmatch", "coop", "sv_gravity", "sv_airaccelerate"};
    const char *values[] = {qa_ss_number_text(sim->options.max_clients, clients), qa_ss_number_text(sim->options.skill, skill),
        ss_cvar_flag(qa_ss_is(sim->options.mode, "deathmatch")), ss_cvar_flag(qa_ss_is(sim->options.mode, "coop")), "800", "0"};
    for (size_t i = 0; i < 6; i++) {
      if (!ss_cvar_exists(cvars, names[i])) ss_cvar_register(cvars, names[i], values[i]);
      ss_cvar_set(cvars, names[i], values[i]);
    }
  }
  if (!sim->options.restore) ss_cvar_set(cvars, "nextserver", sim->options.q2_next_server ? sim->options.q2_next_server : "");
  sim->q2_server_registry = cvars;
  qa_app_shared_sim_initialize_server_settings(sim, cvars);
  if (sim->options.restore) {
    qa_save_value *saved = qa_app_sim_saved_source_cvars(*sim->options.restore);
    qa_restore_q2_server_cvars(cvars, saved);
    qa_save_value_free(saved);
  }
  native->runtime = ss_make_runtime(sim, timing.numeric, sim->random, classic ? "q2-classic" : "native-rerelease", 25);
  native->engine = qa_app_shared_sim_q2_actor_host(sim, recipe->map.entities, native->runtime, NULL, NULL, "primary-world");
  native->pickups = qa_shared_original_pickup_admission_as_contract(sim->original_pickups);
  native->provenance = (qa_compat_q2_classic_damage_provenance){native, ss_native_provenance};
  qa_app_sim_classic_guest_services_options services; memset(&services, 0, sizeof services);
  services.pickups = &native->pickups;
  services.damage_provenance = &native->provenance;
  services.engine = &native->engine; services.scene = sim->scene; services.cvars = cvars;
  services.numeric = *qa_create_numeric_operations(&timing.numeric);
  services.map_path = recipe->map.geometry.requested_path; services.max_clients = sim->options.max_clients;
  services.admit = (qa_app_sim_guest_admit){native, ss_native_admit};
  services.collision = (qa_app_sim_guest_collision){native, ss_native_collision};
  services.print = (qa_app_sim_guest_print){native, ss_native_print};
  services.command = (qa_app_sim_guest_command){native, ss_native_command};
  services.add_command = (qa_app_sim_guest_add_command){native, ss_native_add_command};
  services.debug_graph = (qa_app_sim_guest_debug_graph){native, ss_native_debug_graph};
  native->map_name = qa_ss_map_name(recipe->map.geometry.requested_path);
  qa_app_sim_native_q2_map_geometry geometry = {sim->world_kind, sim->world_entities,
      (double)(sim->world.kind == QA_DECODED_WORLD_Q1 ? sim->world.as.q1->model_count
               : sim->world.kind == QA_DECODED_WORLD_Q2 ? sim->world.as.q2->model_count : sim->world.as.q3->model_count)};
  char *prepared_entities = qa_app_sim_prepare_native_q2_map(geometry, classic ? "classic" : "rerelease", sim->options.mode);
  native->map_entities = qa_ss_strdup(qa_app_shared_sim_guest_monster_entities(sim, prepared_entities, guest->edition));
  free(prepared_entities);
  native->spawn_point = qa_ss_strdup(sim->initial_spawn_point);
  qa_app_shared_sim_keep_owned(sim, native->map_name, free); /* the string lives as long as the simulation */
  qa_app_shared_sim_keep_owned(sim, native->map_entities, free); /* the string lives as long as the simulation */
  qa_app_shared_sim_keep_owned(sim, native->spawn_point, free); /* the string lives as long as the simulation */
  qa_app_sim_classic_guest_map map = {native->map_name, native->map_entities, native->spawn_point};
  memset(out, 0, sizeof *out);
  qa_ss_map_init(&out->visited, QA_SS_KEY_STRING, sizeof(void *));
  qa_ss_map_init(&out->clients, QA_SS_KEY_CLIENT, sizeof(qa_owned_actor));
  out->kind = QA_SS_SOURCE_Q2_NATIVE;
  if (!classic) {
    if (retained && !retained->rerelease) ss_fail("Native travel edition changed");
    qa_app_sim_rerelease_guest_services_options rr; memset(&rr, 0, sizeof rr);
    rr.base = services;
    native->rerelease_engine = qa_app_shared_sim_q2_actor_host(sim, recipe->map.entities, native->runtime, NULL, NULL, "primary-world");
    rr.base.engine = &native->rerelease_engine;
    /* TS preparedNativePrimary(native.prepared): the declared profile, else the built-in one for the artifact digest. */
    if (qa_app_shared_sim_native_primary(sim) != NULL && guest->semantic_bindings == NULL) {
      native->foreign_damage.ctx = native; native->foreign_damage.provenance = ss_native_provenance;
      rr.foreign_damage = &native->foreign_damage;
    }
    rr.frame_milliseconds = 25;
    rr.localize = *(const qa_app_sim_rerelease_localize *)guest->localize;
    rr.clipboard = *(const qa_app_sim_rerelease_clipboard *)guest->clipboard;
    rr.debug_shapes = (qa_app_sim_rerelease_debug_shapes){native, ss_native_debug_shapes};
    rr.world_text = (qa_app_sim_rerelease_world_text){native, ss_native_world_text};
    rr.navigation = qa_app_shared_sim_rerelease_navigation_services(sim);
    if (guest->semantic_bindings) rr.semantic_bindings = guest->semantic_bindings;
    if (retained) for (size_t i = 0; i < retained->rerelease_travel.visited_count; i++) {
      const qa_app_sim_q2_rerelease_visited_level *level = &retained->rerelease_travel.visited[i].level;
      qa_ss_map_set(&out->visited, &retained->rerelease_travel.visited[i].map, &level);
    }
    const qa_app_sim_q2_rerelease_visited_level **level = qa_cvar_registry_variable_value(cvars, "deathmatch") == 0
        ? qa_ss_map_get(&out->visited, &recipe->map.geometry.requested_path) : NULL;
    qa_app_sim_rerelease_guest_revisit revisit; memset(&revisit, 0, sizeof revisit);
    if (level) { native->rerelease_revisit = *level; revisit.level = (*level)->level; revisit.ctx = native; revisit.restore_server_state = ss_rerelease_revisit_restore; }
    qa_app_sim_rerelease_guest_map_services binding; memset(&binding, 0, sizeof binding);
    qa_app_sim_rerelease_guest_map_services_of(&rr, &binding);
    out->edition = "rerelease";
    if (retained && loading) {
      sim->has_pending_native_travel = true;
      sim->pending_native_travel = (qa_ss_pending_native_travel){.edition = "rerelease", .retained = retained, .map = map,
          .rerelease_services = binding, .has_revisit = level != NULL};
      out->rerelease_world = retained->rerelease_travel.world;
      return;
    }
    if (!retained) {
      qa_app_sim_rerelease_guest_world_options options; memset(&options, 0, sizeof options);
      options.prepared = guest->prepared; options.services = rr; options.capabilities = *(const qa_windows_capabilities *)guest->capabilities;
      out->rerelease_world = qa_app_sim_rerelease_guest_world_create(&options);
    } else out->rerelease_world = qa_app_sim_rerelease_guest_world_travel(retained->rerelease_travel.world, map, &binding, level ? &revisit : NULL);
    return;
  }
  if (retained && retained->rerelease) ss_fail("Native travel edition changed");
  const qa_windows_capabilities *capabilities = guest->capabilities;
  native->files = retained ? retained->classic.files : qa_classic_original_save_files_create(ss_native_fallback_open, (void *)capabilities);
  if (!native->files) qa_range_error("Out of memory");
  out->files = native->files;
  if (retained) for (size_t i = 0; i < retained->classic.visited_count; i++) {
    const qa_q2_classic_visited_level *level = &retained->classic.visited[i].level;
    qa_ss_map_set(&out->visited, &retained->classic.visited[i].map, &level);
  }
  const qa_q2_classic_visited_level **revisit = qa_cvar_registry_variable_value(cvars, "deathmatch") == 0
      ? qa_ss_map_get(&out->visited, &recipe->map.geometry.requested_path) : NULL;
  if (revisit) native->classic_revisit = *revisit;
  qa_app_sim_classic_guest_map_services binding; memset(&binding, 0, sizeof binding);
  qa_app_sim_classic_guest_map_services_of(&services, &binding);
  out->edition = "classic";
  if (retained && loading) {
    sim->has_pending_native_travel = true;
    sim->pending_native_travel = (qa_ss_pending_native_travel){.edition = "classic", .retained = retained, .map = map,
        .classic_services = binding, .has_revisit = revisit != NULL};
    out->classic_world = retained->classic.world;
    return;
  }
  if (!retained) {
    native->capabilities = *capabilities;
    native->capabilities.ctx = native; native->capabilities.open_file = ss_native_open_file;
    if (capabilities->now_milliseconds) native->capabilities.now_milliseconds = ss_native_now;
    if (capabilities->performance_counter) native->capabilities.performance_counter = ss_native_counter;
    if (capabilities->standard_output) native->capabilities.standard_output = ss_native_stdout;
    if (capabilities->standard_input) native->capabilities.standard_input = ss_native_stdin;
    qa_app_sim_classic_guest_world_options options; memset(&options, 0, sizeof options);
    options.prepared = guest->prepared; options.services = services; options.capabilities = native->capabilities;
    out->classic_world = qa_app_sim_classic_guest_world_create(&options);
  } else if (!revisit) out->classic_world = qa_app_sim_classic_guest_world_travel(retained->classic.world, map, &binding, NULL);
  else out->classic_world = qa_app_shared_sim_classic_travel_revisit(sim, native, retained->classic.world, map, &binding, false, NULL, NULL);
}

/* ---- createSource: QuakeC (3523-3589) ---- */
typedef struct ss_quakec {
  qa_app_shared_simulation *sim;
  qa_app_sim_quakec_source *game;
  qa_app_sim_quakec_actor_source actor_source;
  qa_app_sim_quakec_source_restore restore;
  char *entity_text;
} ss_quakec;
static bool ss_qc_motion(void *ctx, qa_owned_actor actor, qa_body_state body, qa_q2_motion *out) { return qa_app_sim_quakec_source_motion(ctx, actor, body, out); }
static bool ss_qc_collision(void *ctx, qa_owned_actor actor, qa_app_sim_shared_solid *out) { return qa_app_sim_quakec_source_collision(ctx, actor, out); }
static qa_app_sim_shared_physics_flags ss_qc_flags(void *ctx, qa_owned_actor actor) { return qa_app_sim_quakec_source_flags(ctx, actor); }
static void ss_qc_write_flags(void *ctx, qa_owned_actor actor, qa_app_sim_shared_physics_flags changes) { qa_app_sim_quakec_source_write_flags(ctx, actor, changes); }
static bool ss_qc_reserved(void *ctx, qa_actor_id actor) { return qa_app_sim_quakec_source_is_reserved_client(ctx, actor); }
static bool ss_qc_run_once(void *ctx, qa_actor_id actor, qa_frame_context frame) { return qa_app_sim_quakec_source_run_actor_once(ctx, actor, frame); }
static bool ss_qc_move_type(void *ctx, qa_actor_id actor, double *out) { return qa_app_sim_quakec_source_read_move_type(ctx, actor, out); }
static void ss_qc_run_think(void *ctx, qa_owned_actor actor, qa_frame_context frame) { qa_app_sim_quakec_source_run_think(ctx, actor, frame); }
static void ss_qc_water(void *ctx, qa_owned_actor actor) { qa_app_sim_quakec_source_check_water_transition(ctx, actor); }
static qa_q1_pusher_services ss_qc_pusher(void *ctx) { return *qa_app_sim_quakec_source_pusher_services(ctx); }
static const char *ss_qc_foreign_classname(void *ctx, qa_actor_id actor) {
  ss_quakec *qc = ctx; qa_app_shared_simulation *sim = qc->sim;
  /* this.selectedQ3Source?.records.nativeByActor(actor)?.classname ?? this.classname(actor) */
  if (sim->selected_q3_source) {
    const char *native = qa_app_q3_selected_source_native_classname(sim->selected_q3_source, actor);
    if (native) return native;
  }
  return qa_app_shared_sim_classname(sim, actor);
}
static void ss_qc_client_spawned(void *ctx, qa_owned_actor actor) { qa_app_shared_sim_quake_c_client_spawned(((ss_quakec *)ctx)->sim, actor); }
static const qa_q1qc_pickup_policy *ss_qc_pickup_policy(void *ctx) { return ((ss_quakec *)ctx)->sim->selected_pickup_policy; }
static bool ss_qc_owns_weapon(void *ctx, qa_actor_id actor, qa_item_id item) {
  qa_app_shared_simulation *sim = ((ss_quakec *)ctx)->sim;
  return sim->selected_supply == NULL ? qa_shared_inventory_table_count(sim->inventory, actor, item) > 0
                                      : qa_shared_pickup_admission_owns(sim->selected_supply, actor, item);
}
static bool ss_qc_give_inventory(void *ctx, qa_actor_id actor, const char *const *args, size_t arg_count) {
  qa_app_shared_simulation *sim = ((ss_quakec *)ctx)->sim;
  /* args[0]?.toLowerCase() */
  char category[32] = "";
  if (arg_count > 0 && args[0]) {
    size_t i = 0;
    for (; args[0][i] && i + 1 < sizeof category; i++) category[i] = (char)(args[0][i] >= 'A' && args[0][i] <= 'Z' ? args[0][i] + 32 : args[0][i]);
    category[i] = '\0';
    if (strlen(args[0]) >= sizeof category) category[0] = '\0';
  }
  if (qa_ss_is(category, "weapons") || qa_ss_is(category, "ammo")) return qa_app_shared_sim_grant_selected_arsenal(sim, actor, category);
  return qa_app_shared_sim_give_selected_item(sim, actor, args, arg_count);
}
static void ss_qc_bind_inventory(void *ctx, qa_owned_actor actor, qa_inventory_state_binding binding) {
  qa_app_shared_simulation *sim = ((ss_quakec *)ctx)->sim;
  const qa_inventory_entry *entries = NULL; size_t entry_count = 0;
  if (sim->options.restore)
    for (size_t i = 0; i < sim->options.restore->inventory_count; i++) {
      qa_owned_actor saved;
      if (qa_session_actor_registry_resolve_saved(sim->actors, sim->options.restore->inventories[i].actor, &saved)
          && qa_same_owned_actor(saved, actor)) {
        entries = sim->options.restore->inventories[i].entries; entry_count = sim->options.restore->inventories[i].entry_count;
        break;
      }
    }
  qa_app_shared_sim_bind_source_inventory(sim, actor, binding, entries, entry_count);
}
static bool ss_qc_primary_weapon_selected(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_quakec *)ctx)->sim;
  if (sim->has_selected_arsenal) return false;
  qa_app_sim_weapon_slot **slot = qa_ss_map_get(&sim->weapon_slots, &actor);
  return slot ? qa_app_sim_weapon_slot_primary_selected(*slot) : true;
}
static void ss_qc_admit(void *ctx, qa_owned_actor actor, int64_t slot, qa_app_sim_quakec_source *source) {
  ss_quakec *qc = ctx; (void)slot;
  qc->actor_source.ctx = source;
  qa_app_sim_actor_execution entry; memset(&entry, 0, sizeof entry);
  entry.kind = QA_APP_SIM_ACTOR_EXECUTION_QUAKEC; entry.actor = actor; entry.quakec = &qc->actor_source;
  entry.content = qc->sim->recipe.map.entities.content;
  qa_app_shared_sim_register_actor_execution(qc->sim, entry);
}
static void ss_qc_change_level(void *ctx, const char *map) {
  qa_app_shared_simulation *sim = ((ss_quakec *)ctx)->sim;
  char name[512]; snprintf(name, sizeof name, "q1:%s", map);
  qa_transition_intent intent; memset(&intent, 0, sizeof intent);
  if (sim->recipe.campaign.is_campaign) {
    intent.kind = "campaign-level"; intent.campaign = sim->recipe.campaign.mission.provider;
    intent.map = qa_app_shared_sim_intern(sim, name); intent.spawn_point = ""; intent.gate_count = 0; intent.cause = NULL;
  } else {
    intent.kind = "match-rotation"; intent.match = sim->recipe.match.provider; intent.map = qa_app_shared_sim_intern(sim, name);
  }
  qa_event_ring_admit(&sim->transitions, &intent);
}
static qa_attack_provenance ss_qc_attack(qa_app_shared_simulation *sim, ss_quakec *qc, qa_q1_id1_damage_call call, qa_source_time time) {
  qa_attack_provenance attack; memset(&attack, 0, sizeof attack);
  sim->damage_scratch[0] = call.attacker; sim->damage_scratch[1] = call.inflictor;
  attack.sequence = sim->attack_sequence++; attack.time = time;
  attack.attacker = &sim->damage_scratch[0]; attack.inflictor = &sim->damage_scratch[1];
  attack.weapon_provider = sim->weapon_provider.provider; attack.combat_provider = sim->recipe.combat.provider;
  attack.inventory_provider = sim->recipe.inventory.provider; attack.movement_provider = sim->recipe.movement.provider;
  attack.cause.kind = QA_ATTACK_CAUSE_Q1; attack.cause.death_type = qa_app_sim_quakec_source_death_type(qc->game, call.target);
  return attack;
}
static qa_damage_request ss_qc_damage_request(void *ctx, qa_q1_id1_damage_call call) {
  ss_quakec *qc = ctx; qa_app_shared_simulation *sim = qc->sim;
  qa_damage_request request; memset(&request, 0, sizeof request);
  request.target = call.target; request.amount = call.amount; request.delivery = "direct";
  qa_q1_id1_synchronous_attack attack;
  if (qa_q1_id1_sync_attacks_resolve(qa_app_sim_quakec_source_attacks(qc->game), call, &attack)) {
    request.knockback = attack.knockback; request.direction = attack.direction; request.point = attack.point; request.normal = attack.normal;
    request.attack = ss_qc_attack(sim, qc, call, (qa_source_time){QA_SOURCE_TIME_SECONDS, attack.time});
    request.attack.weapon = attack.weapon;
    return request;
  }
  qa_q1_id1_physics_callback callback;
  bool has_callback = qa_app_sim_quakec_source_current_physics_callback(qc->game, &callback);
  qa_q1_id1_environmental_damage environment;
  bool has_environment = qa_q1_id1_environment_resolve(qa_app_sim_quakec_source_environment(qc->game), call,
      has_callback ? &callback : NULL, &environment);
  qa_q1_id1_projectile_attack projectile;
  if (qa_q1_id1_projectile_attacks_resolve(qa_app_sim_quakec_source_projectiles(qc->game), call, &projectile)) {
    qa_body_state target, inflictor;
    if (!qa_app_shared_sim_body(sim, call.target, &target) || !qa_app_shared_sim_body(sim, call.inflictor, &inflictor))
      ss_fail("QC projectile damage lost its source bodies");
    qa_vec3 delta = {target.origin.x - inflictor.origin.x - (inflictor.bounds.min.x + inflictor.bounds.max.x) * 0.5,
                     target.origin.y - inflictor.origin.y - (inflictor.bounds.min.y + inflictor.bounds.max.y) * 0.5,
                     target.origin.z - inflictor.origin.z - (inflictor.bounds.min.z + inflictor.bounds.max.z) * 0.5};
    double length = qa_js_hypot3(delta.x, delta.y, delta.z);
    qa_vec3 direction = length == 0 ? qa_ss_zero : (qa_vec3){delta.x / length, delta.y / length, delta.z / length};
    qa_compat_qc_world_host *world = qa_app_sim_quakec_source_world_host(qc->game);
    request.knockback = call.amount * qa_q1_id1_damage_multiplier(qa_app_sim_quakec_source_q1qc_machine(qc->game),
        qa_compat_qc_world_host_reference(world, call.attacker), qa_compat_qc_world_host_reference(world, call.inflictor));
    request.direction = direction;
    request.point = projectile.has_trace ? projectile.trace_point : target.origin;
    request.normal = projectile.has_trace ? projectile.trace_normal : qa_ss_zero;
    request.attack = ss_qc_attack(sim, qc, call, (qa_source_time){QA_SOURCE_TIME_SECONDS, projectile.time});
    if (projectile.has_launch) { request.attack.has_originating_projectile = true; request.attack.originating_projectile = call.inflictor; }
    request.attack.weapon = projectile.weapon;
    return request;
  }
  if (!has_environment) {
    qa_body_state target;
    if (!qa_app_shared_sim_body(sim, call.target, &target)) ss_fail("Native QuakeC damage lost its target body");
    request.knockback = 0; request.direction = qa_ss_zero; request.point = target.origin; request.normal = qa_ss_zero;
    qa_compat_qc_machine *machine = qa_app_sim_quakec_source_machine(qc->game);
    double time = qa_compat_qc_words_float(&machine->globals, qa_compat_qc_machine_global_offset(machine, "time"));
    request.attack = ss_qc_attack(sim, qc, call, (qa_source_time){QA_SOURCE_TIME_SECONDS, time});
    request.attack.weapon = NULL;
    return request;
  }
  request.knockback = environment.knockback; request.direction = environment.direction;
  request.point = environment.point; request.normal = qa_ss_zero;
  request.attack = ss_qc_attack(sim, qc, call, (qa_source_time){QA_SOURCE_TIME_SECONDS, environment.time});
  request.attack.weapon = NULL;
  request.attack.cause = environment.cause;
  return request;
}
static void ss_qc_print(void *ctx, const char *text) { qa_app_shared_sim_print(((ss_quakec *)ctx)->sim, text, NULL); }
static bool ss_recipe_weapons_differ(const qa_executable_recipe *recipe) {
  for (size_t i = 0; i < recipe->weapons.count; i++)
    if (!qa_ss_is(recipe->weapons.values[i].provider, recipe->map.entities.provider)
        || !qa_ss_is(recipe->weapons.values[i].content, recipe->map.entities.content)) return true;
  return false;
}
static void ss_create_quakec(qa_app_shared_simulation *sim, qa_ss_source_runtime *out) {
  const qa_executable_recipe *recipe = &sim->recipe;
  const qa_app_sim_prepared_quakec_source *prepared = sim->options.prepared_quakec;
  const qa_guest_checkpoint *checkpoint = sim->options.restore ? qa_app_sim_simulation_quakec_checkpoint(*sim->options.restore) : NULL;
  ss_quakec *qc = qa_app_shared_sim_keep(sim, sizeof *qc);
  qc->sim = sim;
  qc->actor_source = (qa_app_sim_quakec_actor_source){NULL, ss_qc_motion, ss_qc_collision, ss_qc_flags, ss_qc_write_flags,
      ss_qc_reserved, ss_qc_run_once, ss_qc_move_type, ss_qc_run_think, ss_qc_water, ss_qc_pusher};
  char *map_entities = qa_app_sim_quakec_map_entities(sim->world, sim->options.mode);
  qc->entity_text = qa_ss_strdup(qa_app_shared_sim_guest_monster_entities(sim, map_entities, qa_app_shared_sim_guest_map_edition(sim)));
  free(map_entities);
  qa_app_shared_sim_keep_owned(sim, qc->entity_text, free); /* the string lives as long as the simulation */
  qa_app_sim_quakec_source_options options; memset(&options, 0, sizeof options);
  options.ctx = qc;
  options.source_registry = sim->options.source_registry;
  options.recipe = recipe; options.world = sim->world; options.entity_text = qc->entity_text; options.scene = sim->scene;
  options.original_save_candidate = sim->options.original_save_candidate;
  options.actors = sim->actors; options.callbacks = sim->callbacks; options.physics = sim->physics; options.combat = sim->combat;
  options.inventory = sim->inventory; options.pickups = qa_shared_original_pickup_admission_as_contract(sim->original_pickups);
  options.events = sim->events; options.random = sim->random;
  options.skill = (int)sim->options.skill; options.mode = sim->options.mode; options.max_clients = sim->options.max_clients;
  options.initial_source_time_seconds = ss_time_seconds(sim);
  options.foreign_classname = ss_qc_foreign_classname;
  if (ss_recipe_weapons_differ(recipe)) options.client_spawned = ss_qc_client_spawned;
  options.pickup_policy = ss_qc_pickup_policy;
  options.owns_weapon = ss_qc_owns_weapon;
  options.give_inventory = ss_qc_give_inventory;
  options.bind_inventory = ss_qc_bind_inventory;
  bool stage_owned = false;
  qa_q1qc_weapon_stage *stage = qa_app_sim_prepared_quakec_weapon_stage(prepared, &stage_owned);
  if (stage) options.primary_weapon_selected = ss_qc_primary_weapon_selected;
  if (stage_owned) qa_q1qc_weapon_stage_destroy(stage);
  if (checkpoint) {
    qc->restore.checkpoint = checkpoint;
    qc->restore.clients = sim->options.restored_clients; qc->restore.client_count = sim->options.restored_client_count;
    options.restore = &qc->restore;
  }
  options.admit = ss_qc_admit;
  options.change_level = ss_qc_change_level;
  options.damage_request = ss_qc_damage_request;
  options.print = ss_qc_print;
  memset(out, 0, sizeof *out);
  qa_ss_map_init(&out->visited, QA_SS_KEY_STRING, sizeof(void *));
  qa_ss_map_init(&out->clients, QA_SS_KEY_CLIENT, sizeof(qa_owned_actor));
  out->kind = QA_SS_SOURCE_QUAKEC;
  qc->game = out->quakec = qa_app_sim_quakec_source_create(prepared, options);
}

/* ---- createSource: TS Q1 (3687-3744) ---- */
typedef struct ss_q1 {
  qa_app_shared_simulation *sim;
  qa_cvar_registry *cvars;
  qa_content_id content;
  qa_provider_id campaign;
  qa_comp_q1_services services;
  qa_q1_options options;
  qa_comp_q1_source_selection selection;
} ss_q1;
static double ss_q1_cvar(void *ctx, const char *name) { return qa_cvar_registry_variable_value(((ss_q1 *)ctx)->cvars, name); }
static void ss_q1_set_cvar(void *ctx, const char *name, const char *value) {
  ss_q1 *q1 = ctx;
  ss_cvar_set(q1->cvars, name, value);
  if (qa_ss_is(name, "sv_gravity")) qa_app_shared_sim_set_world_gravity(q1->sim, qa_cvar_registry_variable_value(q1->cvars, name));
  if (qa_ss_is(name, "skill")) {
    double skill = qa_cvar_registry_variable_value(q1->cvars, name);
    if (skill != 0 && skill != 1 && skill != 2 && skill != 3) ss_fail("Q1 skill must be 0..3");
    q1->sim->q1_campaign.skill = (int)skill;
  }
}
static void ss_q1_composition_emit(void *ctx, const qa_comp_q1_event *event) {
  ss_q1 *q1 = ctx; qa_app_shared_simulation *sim = q1->sim;
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  if (event->kind == QA_COMP_Q1_EVENT_LEVEL_PRESENTATION) {
    source.kind = "q1-level"; source.q1_level = event->finale;
    qa_app_shared_sim_emit(sim, q1->content, source, NULL, NULL);
    return;
  }
  if (event->kind == QA_COMP_Q1_EVENT_ADDON) {
    qa_app_shared_sim_emit_q1_addon(sim, q1->content, event->addon_event, sim->source_frame.time);
    return;
  }
  source.kind = "q1-composition"; source.q1_composition = event;
  qa_app_shared_sim_emit(sim, q1->content, source, NULL, NULL);
}
static qa_comp_q1_selected_player ss_q1_selected_player(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q1 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_q1_character_actor **character = qa_ss_map_get(&sim->q1_characters, &player->actor);
  /* lifecycle = this.q1Characters.get(player.actor)?.lifecycle, life = lifecycle?.life */
  qa_comp_q1_selected_player selected; memset(&selected, 0, sizeof selected);
  const char *life = character ? qa_q1_character_actor_life(*character) : NULL;
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  selected.dead_flag = qa_ss_is(life, "dying") ? 1 : qa_ss_is(life, "dead") ? 2 : qa_ss_is(life, "respawnable") ? 3
      : (combat.has_value ? combat.value.health : 0) > 0 ? 0 : 2;
  selected.is_bot = qa_app_sim_bot_services_is_bot(&sim->bot_services, actor);
  selected.view_angles = player->view_angles;
  qa_q2_base_player_view *view = qa_ss_map_get(&sim->q2_views, &actor);
  selected.view_offset = character ? qa_q1_character_actor_view_offset(*character) : view ? view->offset : (qa_vec3){0, 0, player->view_height};
  selected.frame = qa_ss_is(player->animation.state.kind, "q1") || qa_ss_is(player->animation.state.kind, "q2") ? player->animation.state.frame : 0;
  double water_type = player->water_type;
  selected.water_type = water_type == -3 || water_type == 32 ? "water" : water_type == -4 || water_type == 16 ? "slime"
      : water_type == -5 || water_type == 8 ? "lava" : "empty";
  selected.water_level = player->water_level == 3 ? 3 : player->water_level == 2 ? 2 : player->water_level == 1 ? 1 : 0;
  selected.teleport_until = player->state.kind == QA_MOVEMENT_STATE_Q1 ? player->state.as.q1.teleport_time_seconds : 0;
  return selected;
}
static void ss_q1_set_observer(void *ctx, qa_actor_id actor, bool enabled) {
  qa_app_shared_simulation *sim = ((ss_q1 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_q2_player_movement_change change; memset(&change, 0, sizeof change);
  change.kind = "noclip"; change.enabled = enabled;
  qa_app_shared_sim_set_player_movement(sim, actor, change);
  qa_combat_trait_changes traits; memset(&traits, 0, sizeof traits);
  traits.has_can_take_damage = true; traits.can_take_damage = !enabled;
  qa_gameplay_authority_set_traits(sim->combat, player->actor, traits);
  qa_shared_body_table_link(ss_bodies(sim), player->actor, NULL);
}
static void ss_q1_place_player(void *ctx, qa_owned_actor actor, qa_q1_actor *spot, const qa_q1_travel_state *travel) {
  qa_app_shared_simulation *sim = ((ss_q1 *)ctx)->sim;
  qa_app_shared_sim_place_q1_player(sim, qa_app_shared_sim_require_player(sim, actor.id), spot, travel);
}
static void ss_q1_disconnect(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q1 *)ctx)->sim;
  qa_session_actor_registry_release(sim->actors, qa_app_shared_sim_require_player(sim, actor)->actor);
}
static void ss_q1_teleport(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity, double until) {
  qa_app_shared_simulation *sim = ((ss_q1 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_q2_player_movement_change change; memset(&change, 0, sizeof change);
  change.kind = "teleport"; change.origin = origin; change.angles = angles; change.velocity = velocity;
  change.command_angles = player->command_angles;
  change.hold_milliseconds = qa_js_max(0, (until - ss_time_seconds(sim)) * 1000); change.spectator = false;
  qa_app_shared_sim_set_player_movement(sim, actor, change);
}
static qa_q1_services *ss_q1_weapon_services(void *ctx, qa_actor_id actor) {
  (void)actor;
  qa_app_shared_simulation *sim = ((ss_q1 *)ctx)->sim;
  if (sim->has_selected_weapon_source && qa_ss_is(sim->selected_weapon_source.kind, "q1")) return sim->selected_weapon_source.q1_game;
  return !sim->has_selected_arsenal ? sim->strategy->q1_game(sim) : NULL;
}
static bool ss_q1_cheat_arsenal(void *ctx, qa_actor_id actor, const char *category) {
  qa_app_shared_simulation *sim = ((ss_q1 *)ctx)->sim;
  if (category == NULL) return qa_app_shared_sim_grant_selected_arsenal(sim, actor, "weapons") && qa_app_shared_sim_grant_selected_arsenal(sim, actor, "ammo");
  return qa_app_shared_sim_grant_selected_arsenal(sim, actor, category);
}
static bool ss_q1_give_selected_item(void *ctx, qa_actor_id actor, const char *const *args, size_t count) {
  return qa_app_shared_sim_give_selected_item(((ss_q1 *)ctx)->sim, actor, args, count);
}
static qa_item_id ss_q1_selected_weapon(void *ctx, qa_actor_id actor) { return qa_app_shared_sim_player_ui(((ss_q1 *)ctx)->sim, actor).active_weapon; }
static qa_item_id ss_q1_selected_ammo(void *ctx, qa_actor_id actor) {
  qa_app_sim_player_ui ui = qa_app_shared_sim_player_ui(((ss_q1 *)ctx)->sim, actor);
  return ui.has_ammo ? ui.ammo_item : NULL;
}
static bool ss_q1_select_weapon(void *ctx, qa_actor_id actor, qa_item_id item) {
  qa_app_shared_simulation *sim = ((ss_q1 *)ctx)->sim;
  if (sim->strategy->q1_game(sim) == NULL) return false;
  if (sim->has_selected_arsenal) return qa_app_shared_sim_selected_arsenal_select(sim, actor, item);
  return sim->strategy->select_weapon(sim, qa_app_shared_sim_require_player(sim, actor)->actor, item);
}
static void ss_q1_weapon_changed(void *ctx, qa_actor_id actor, qa_item_id acquired) {
  (void)acquired;
  qa_app_shared_simulation *sim = ((ss_q1 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  player->arsenal = qa_app_shared_sim_arsenal(sim, player);
}
static bool ss_q1_prompt_supported(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q1 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  return player != NULL && sim->options.prompt_supported != NULL && sim->options.prompt_supported(sim->options.prompt_ctx, player->client);
}
static void ss_q1_restart_session(void *ctx, const char *map, double flags) {
  ss_q1 *q1 = ctx; qa_app_shared_simulation *sim = q1->sim;
  sim->q1_restart = true; sim->q1_campaign.flags = flags;
  char name[512]; snprintf(name, sizeof name, "q1:%s", map);
  qa_transition_intent intent; memset(&intent, 0, sizeof intent);
  intent.kind = "campaign-level"; intent.campaign = q1->campaign; intent.map = qa_app_shared_sim_intern(sim, name);
  intent.spawn_point = ""; intent.gate_count = 0; intent.cause = NULL;
  qa_event_ring_admit(&sim->transitions, &intent);
}
static void ss_q1_finish_campaign(void *ctx) {
  ss_q1 *q1 = ctx;
  qa_transition_intent intent; memset(&intent, 0, sizeof intent);
  intent.kind = "campaign-complete"; intent.campaign = q1->campaign; intent.gate_count = 0;
  qa_event_ring_admit(&q1->sim->transitions, &intent);
}
static void ss_create_q1(qa_app_shared_simulation *sim, qa_app_sim_actor_host_runtime runtime, qa_provider_id campaign, qa_ss_source_runtime *out) {
  const qa_executable_recipe *recipe = &sim->recipe;
  qa_content_id content = recipe->map.entities.content;
  ss_q1 *q1 = qa_app_shared_sim_keep(sim, sizeof *q1);
  q1->sim = sim; q1->content = content; q1->campaign = campaign;
  qa_q1_host host = qa_app_shared_sim_q1_actor_host(sim, recipe->map.entities, runtime, "primary-world");
  qa_cvar_registry *cvars = sim->options.source_registry;
  if (!cvars) {
    cvars = ss_cvar_registry(sim, QA_COMMAND_DIALECT_Q1_NETQUAKE, ss_print_event, sim);
    qa_app_shared_sim_keep_owned(sim, cvars, ss_cvar_registry_release); /* TS: garbage with the simulation */
  }
  q1->cvars = cvars;
  if (!sim->options.restore && !sim->options.source_registry)
    qa_cvar_registry_apply_archive(cvars, sim->options.source_archive, sim->options.source_archive_count);
  char skill[32];
  if (!sim->options.source_registry) {
    const char *names[] = {"skill", "deathmatch", "coop", "teamplay", "sv_gravity", "sv_maxspeed", "samelevel", "timelimit",
                           "fraglimit", "gamecfg", "sv_cheats", "footsteps"};
    const char *values[] = {qa_ss_number_text(sim->q1_campaign.skill, skill), ss_cvar_flag(qa_ss_is(sim->options.mode, "deathmatch")),
                            ss_cvar_flag(qa_ss_is(sim->options.mode, "coop")), "0", "800", "320", "0", "0", "0", "0", "0", "1"};
    for (size_t i = 0; i < 12; i++) ss_cvar_register(cvars, names[i], values[i]);
  }
  ss_cvar_set(cvars, "skill", qa_ss_number_text(sim->q1_campaign.skill, skill));
  ss_cvar_set(cvars, "deathmatch", ss_cvar_flag(qa_ss_is(sim->options.mode, "deathmatch")));
  ss_cvar_set(cvars, "coop", ss_cvar_flag(qa_ss_is(sim->options.mode, "coop")));
  qa_app_shared_sim_initialize_server_settings(sim, cvars);
  qa_save_value *saved_cvars = sim->options.restore ? qa_app_sim_saved_source_cvars(*sim->options.restore) : NULL;
  if (saved_cvars) { qa_cvar_registry_restore_save_state(cvars, saved_cvars); qa_save_value_free(saved_cvars); }
  if (qa_ss_is(recipe->match.provider, "q1:horde") && !sim->options.restore) {
    if (!ss_cvar_exists(cvars, "horde")) ss_cvar_register(cvars, "horde", "0");
    ss_cvar_set(cvars, "horde", "1");
  }
  qa_comp_q1_services *services = &q1->services;
  memset(services, 0, sizeof *services);
  services->ctx = q1;
  services->shared_grapple = qa_app_shared_sim_shared_grapple(sim);
  services->cvar = ss_q1_cvar; services->set_cvar = ss_q1_set_cvar; services->emit = ss_q1_composition_emit;
  services->selected_player = ss_q1_selected_player; services->set_observer = ss_q1_set_observer;
  services->place_player = ss_q1_place_player; services->disconnect = ss_q1_disconnect; services->teleport = ss_q1_teleport;
  services->weapon_services = ss_q1_weapon_services; services->cheat_arsenal = ss_q1_cheat_arsenal;
  services->give_selected_item = ss_q1_give_selected_item;
  services->selected_weapon = ss_q1_selected_weapon; services->selected_ammo = ss_q1_selected_ammo;
  services->select_weapon = ss_q1_select_weapon; services->weapon_changed = ss_q1_weapon_changed;
  services->prompt_supported = ss_q1_prompt_supported;
  services->restart_session = ss_q1_restart_session; services->finish_campaign = ss_q1_finish_campaign;
  bool found = false;
  char *selected = qa_selected_source_program(recipe->execution, recipe->execution_count, recipe->map.entities, &found);
  /* Quake 64 is the id1 campaign on N64-authored maps; its BSP/texture
   * dialect is handled by the unified Q1 map reader, not a new program. */
  const char *program = selected && qa_ss_is(selected, "quake64") ? "id1" : selected ? selected : "undefined";
  static const char *const programs[] = {"id1", "hipnotic", "rogue", "dopa", "mg1", "mg3", "ctf"};
  int index = -1;
  for (int i = 0; i < 7; i++) if (qa_ss_is(program, programs[i])) index = i;
  if (index < 0) { char message[256]; snprintf(message, sizeof message, "Unsupported Q1 source program %s", program); free(selected); ss_fail("%s", message); }
  free(selected);
  qa_q1_options *options = &q1->options;
  memset(options, 0, sizeof *options);
  options->edition = content && strstr(content, ":rerelease:") ? "rerelease" : "classic";
  options->skill = sim->q1_campaign.skill;
  options->physics_edition = qa_app_shared_sim_q1_physics_edition(sim);
  options->deathmatch = qa_ss_is(sim->options.mode, "deathmatch") ? 1 : 0;
  options->coop = qa_ss_is(sim->options.mode, "coop");
  options->max_clients = sim->options.max_clients; options->has_max_clients = true;
  qa_provider_reference foreign;
  if (qa_app_shared_sim_foreign_q2_identity(sim, &foreign)) options->foreign_world = true;
  options->campaign = campaign; options->combat_provider = recipe->combat.provider;
  options->movement_provider = recipe->movement.provider; options->inventory_provider = recipe->inventory.provider;
  options->gravity = 800;
  qa_comp_q1_source_selection *selection = &q1->selection;
  memset(selection, 0, sizeof *selection);
  selection->program = (qa_comp_q1_program)index;
  selection->campaign = sim->q1_campaign.binding;
  selection->has_horde = true; selection->horde = qa_ss_is(recipe->match.provider, "q1:horde");
  selection->registered = true; selection->official_campaign = index == QA_COMP_Q1_ID1;
  memset(out, 0, sizeof *out);
  qa_ss_map_init(&out->visited, QA_SS_KEY_STRING, sizeof(void *));
  qa_ss_map_init(&out->clients, QA_SS_KEY_CLIENT, sizeof(qa_owned_actor));
  out->kind = QA_SS_SOURCE_Q1;
  out->q1_composition = qa_app_shared_sim_keep(sim, sizeof *out->q1_composition);
  if (!qa_comp_create_q1_source_composition(out->q1_composition, host, options, selection, services)) qa_range_error("Out of memory");
  out->q1_game = out->q1_composition->game;
  out->q1_cvars = cvars;
}

/* ---- createSource: TS Q2 (3746-3879) ---- */
typedef struct ss_q2 {
  qa_app_shared_simulation *sim;
  qa_content_id content;
  qa_q2w_weapons *weapons;
  qa_cvar_registry *server_cvars;
  qa_q2_product_runtime *product; /* owningMonsters/product, set after creation */
  qa_q2_foundation_host host;
  qa_q2_match_selection match;
  qa_q2_rerelease_item_state item_state;
  qa_q2_cvar_binding binding;
  qa_q2_composition_options common;
} ss_q2;
/* Releases the server cvar binding's pulled password strings and its value bindings. The product's player rules may
 * already be gone at teardown, so the binding forgets them first. */
static void ss_q2_binding_release(void *value) {
  qa_q2_cvar_binding *binding = value;
  binding->rules = NULL;
  qa_q2_cvar_binding_destroy(binding);
}
static void ss_q2w_emit(void *ctx, qa_q2w_event event) { ss_q2 *q2 = ctx; qa_app_shared_sim_weapon_event(q2->sim, q2->content, &event); }
static void ss_q2w_noise(void *ctx, qa_actor_id actor, qa_vec3 origin, bool secondary) {
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  sim->strategy->required_monster_noise(sim, actor, origin, secondary, "Q2 monster noise before source admission");
}
static void ss_q2w_dodge(void *ctx, qa_actor_id monster, qa_actor_id attacker, double eta, qa_trace_result trace) {
  qa_app_shared_sim_q2_monster_dodge(((ss_q2 *)ctx)->sim, monster, &attacker, eta, &trace);
}
static void ss_q2w_ammo_changed(void *ctx, qa_actor_id actor, qa_item_id ammo) {
  (void)ammo;
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  size_t count = 0; const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, actor, &count);
  double *counts = calloc(count ? count : 1, sizeof *counts);
  if (!counts) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) counts[i] = entries[i].count;
  qa_network_event event; memset(&event, 0, sizeof event);
  event.kind = QA_NETWORK_EVENT_Q2_INVENTORY; event.counts = (qa_double_view){counts, count};
  qa_app_sim_simulation_events_message(sim->events, event, true, actor, false, 0);
  free(counts);
}
static bool ss_q2w_can_target(void *ctx, const qa_actor_id *attacker, qa_actor_id target) {
  (void)ctx; return attacker == NULL || !qa_same_actor(*attacker, target);
}
static void ss_q2_player_push(void *ctx, qa_actor_id actor, qa_vec3 velocity) {
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor, &body)) return;
  body.velocity = velocity;
  qa_shared_body_table_write(ss_bodies(sim), player->actor, body);
  player->state = qa_app_sim_movement_player_read_state(player);
  sim->strategy->pushed(sim, actor, velocity);
}
static void ss_q2_set_actor_gravity(void *ctx, qa_actor_id actor, double gravity) {
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  if (qa_app_shared_sim_player(sim, actor)) { ss_set_actor_gravity_scale(sim, actor, gravity); return; }
  sim->strategy->set_entity_gravity(sim, actor, gravity);
}
static qa_q2_base_local_time ss_q2_local_time(void *ctx) {
  (void)ctx;
  time_t now = time(NULL); struct tm local; localtime_r(&now, &local);
  return (qa_q2_base_local_time){local.tm_hour, local.tm_min, local.tm_sec};
}
static double ss_q2_weapon_respawn_seconds(void *ctx) { return qa_cvar_registry_variable_value(((ss_q2 *)ctx)->server_cvars, "g_weapon_respawn_time"); }
static void ss_q2_weapon_picked(void *ctx, qa_actor_id actor, qa_item_id item, bool first) {
  ss_q2 *q2 = ctx;
  qa_q2w_state *state = qa_q2w_ballistics_state(&q2->weapons->base, actor);
  size_t count = 0; const qa_q2w_definition *const *definitions = qa_q2w_weapons_registered_definitions(q2->weapons, &count);
  const qa_q2w_definition *definition = NULL;
  for (size_t i = 0; i < count; i++) if (qa_ss_is(definitions[i]->item, item)) { definition = definitions[i]; break; }
  if (state && definition && first) state->pending = definition->name;
}
static void ss_q2_silencer(void *ctx, qa_actor_id actor, double charges) {
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  sim->strategy->require_q2(sim, "Q2 silencer before source admission");
  qa_q2_game_services *game; qa_q2w_weapons *weapons;
  if (!qa_app_shared_sim_q2_item_weapon_source(sim, &game, &weapons)) ss_fail("Q2 silencer has no arsenal source");
  qa_q2w_ballistics_grant_silencer(&weapons->base, actor, game, charges);
}
static void ss_q2_power_armor(void *ctx, qa_actor_id actor, const char *kind) { (void)ctx; (void)actor; (void)kind; }
static void ss_q2_persistent_inventory(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)game; qa_ss_map_set(&((ss_q2 *)ctx)->sim->pending_start_items, &entity->actor.id, NULL);
}
static bool ss_q2_grant(void *ctx, qa_actor_id actor, const char *category) { return qa_app_shared_sim_grant_selected_arsenal(((ss_q2 *)ctx)->sim, actor, category); }
static bool ss_q2_give(void *ctx, qa_actor_id actor, const char *const *args, size_t count) { return qa_app_shared_sim_give_selected_item(((ss_q2 *)ctx)->sim, actor, args, count); }
static bool ss_q2_weapon_state(void *ctx, qa_actor_id actor, qa_q2_character_weapon *out) {
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  qa_q2_game_services *game; qa_q2w_weapons *weapons;
  if (!qa_app_shared_sim_q2_item_weapon_source(sim, &game, &weapons)) return false;
  qa_q2w_state *state = qa_q2w_ballistics_state(&weapons->base, actor);
  if (!state) return false;
  memset(out, 0, sizeof *out);
  out->q2_name = state->weapon;
  out->ammo = state->weapon == NULL ? NULL : qa_q2w_weapons_definition(weapons, state->weapon)->ammo;
  qa_q2w_recoil_result recoil = qa_q2w_weapon_recoil(state, game->options.edition, game->host->now(game->host->ctx));
  out->kick_origin = recoil.kick_origin; out->kick_angles = recoil.kick_angles;
  out->loop_sound = state->loop_sound;
  return true;
}
static qa_q2_player_movement ss_q2_movement(void *ctx, qa_actor_id actor) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(((ss_q2 *)ctx)->sim, actor);
  qa_q2_player_movement movement; memset(&movement, 0, sizeof movement);
  movement.view_angles = player->view_angles; movement.command_angles = player->command_angles;
  movement.water_level = player->water_level;
  double type = player->water_type;
  movement.water_type = type < 0 ? (type == -3 ? 32 : type == -4 ? 16 : type == -5 ? 8 : 0) : type;
  movement.grounded = player->ground.kind != QA_TRACE_HIT_NONE;
  movement.ducked = player->bounds.max.z < player->standing_bounds.max.z;
  movement.buttons = player->buttons;
  movement.up_move = player->up_move;
  movement.standing_bounds = player->standing_bounds; movement.animate_q2 = player->character == QA_GAME_FAMILY_Q2;
  return movement;
}
static void ss_q2_set_movement(void *ctx, qa_actor_id actor, qa_q2_player_movement_change change) {
  qa_app_shared_sim_set_player_movement(((ss_q2 *)ctx)->sim, actor, change);
}
static bool ss_q2_select_empty(void *ctx) { return qa_cvar_registry_variable_value(((ss_q2 *)ctx)->server_cvars, "g_select_empty") != 0; }
static void ss_q2_player_spawned(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)game;
  ss_q2 *q2 = ctx; qa_app_shared_simulation *sim = q2->sim;
  qa_actor_id actor = entity->actor.id;
  if (sim->has_selected_arsenal) {
    qa_q2w_ballistics_reset_silencer(&q2->weapons->base, actor);
    qa_q2_game_services *item_game; qa_q2w_weapons *item_weapons;
    if (qa_app_shared_sim_q2_item_weapon_source(sim, &item_game, &item_weapons)) qa_q2w_ballistics_reset_silencer(&item_weapons->base, actor);
  }
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_q1_character_actor **q1 = qa_ss_map_get(&sim->q1_characters, &player->actor);
  if (q1) qa_q1_character_actor_respawn(*q1, NULL);
  if (sim->has_selected_arsenal && qa_app_shared_sim_selected_arsenal_has(sim, actor)) {
    qa_app_shared_sim_selected_arsenal_remove(sim, actor);
    player->arsenal = qa_app_shared_sim_selected_arsenal_admit(sim, entity->actor, 100, false);
  }
  if (sim->selected_q3_source) qa_app_q3_selected_source_respawn(sim->selected_q3_source, actor);
  if (sim->hand_grenades) qa_app_sim_hand_grenade_runtime_respawn(sim->hand_grenades, actor);
  if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, actor);
  if (!sim->has_selected_arsenal || qa_app_shared_sim_selected_arsenal_has(sim, actor)) qa_app_shared_sim_admit_grapple(sim, actor);
  if (qa_ss_map_has(&sim->pending_start_items, &actor) && (!sim->has_selected_arsenal || qa_app_shared_sim_selected_arsenal_has(sim, actor)))
    qa_app_shared_sim_give_q2_start_items(sim, actor);
}
static void ss_q2_player_emit(void *ctx, qa_q2_player_event event) {
  ss_q2 *q2 = ctx; qa_app_shared_simulation *sim = q2->sim;
  if (qa_ss_is(event.kind, "view")) qa_ss_map_set(&sim->q2_views, &event.actor, &event.view);
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q2-player"; source.q2_player = &event;
  qa_app_shared_sim_emit(sim, q2->content, source, NULL, NULL);
}
static void ss_q2_player_noise(void *ctx, qa_actor_id actor, qa_vec3 origin) {
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  sim->strategy->required_monster_noise(sim, actor, origin, false, "Q2 noise before source entry");
}
static qa_q2w_input ss_q2_weapon_input(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  return qa_app_shared_sim_q2_weapon_input(sim, qa_app_shared_sim_require_player(sim, actor));
}
static bool ss_q2_banned(void *ctx, const char *address) { (void)ctx; (void)address; return false; }
static qa_q2m_state *ss_q2_owning_monster(void *ctx, qa_actor_id actor) {
  ss_q2 *q2 = ctx;
  if (!q2->product) return NULL;
  qa_q2m_context *context = qa_q2m_monsters_context(&q2->product->monsters, actor);
  return context ? context->state : NULL;
}
static double ss_q2_dm_read(void *ctx) { return qa_q2_source_deathmatch_flags(((ss_q2 *)ctx)->server_cvars); }
static void ss_q2_dm_write(void *ctx, double flags) {
  qa_cvar_registry *cvars = ((ss_q2 *)ctx)->server_cvars;
  qa_cvar_read before; bool has = qa_cvar_registry_find(cvars, "dmflags", &before);
  double value = has ? qa_js_string_to_number(before.value) : 0;
  double latched = has ? qa_js_string_to_number(before.latched_value ? before.latched_value : before.value) : 0;
  if (has) qa_cvar_read_free(&before);
  uint32_t changed = (uint32_t)(qa_js_to_int32(value) ^ qa_js_to_int32(flags));
  uint32_t desired = ((uint32_t)qa_js_to_int32(latched) & ~changed) | ((uint32_t)qa_js_to_int32(flags) & changed);
  char text[32];
  ss_cvar_set(cvars, "dmflags", qa_ss_number_text(flags, text));
  if ((double)desired != flags) { qa_cvar_snapshot snapshot; qa_cvar_registry_stage(cvars, "dmflags", qa_ss_number_text((double)desired, text), &snapshot); }
}
static double ss_q2_services_gravity(void *ctx) { return qa_app_sim_shared_physics_gravity(((ss_q2 *)ctx)->sim->physics); }
static void ss_q2_services_emit(void *ctx, qa_q2_composition_event event) {
  ss_q2 *q2 = ctx; qa_app_shared_simulation *sim = q2->sim;
  if (event.kind == QA_Q2_COMPOSITION_EVENT_GRAPPLE_PREDICTION) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, event.actor);
    if (player->state.kind == QA_MOVEMENT_STATE_Q2) {
      int32_t flags = qa_js_to_int32(player->state.as.q2.flags);
      player->state.as.q2.flags = event.suppressed ? (flags | 64) : (flags & ~64);
    } else if (player->state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
      int32_t flags = qa_js_to_int32(player->state.as.q2_rerelease.flags);
      player->state.as.q2_rerelease.flags = event.suppressed ? (flags | 64) : (flags & ~64);
    }
    return;
  }
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q2-composition"; source.q2_composition = &event;
  qa_content_id content = event.kind == QA_Q2_COMPOSITION_EVENT_CTF || event.kind == QA_Q2_COMPOSITION_EVENT_LMCTF
      ? sim->recipe.match.content : q2->content;
  qa_app_shared_sim_emit(sim, content, source, NULL, NULL);
}
static qa_q2_composition_powerups ss_q2_foreign_powerups(void *ctx, qa_actor_id actor) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(((ss_q2 *)ctx)->sim, actor);
  ss_fail("Foreign arsenal powerups are not bound for %s", player->arsenal.provider ? player->arsenal.provider : "");
}
/* Q2RereleaseHooks. */
static void ss_rr_emit(void *ctx, qa_q2_rerelease_event event) {
  ss_q2 *q2 = ctx; qa_app_shared_simulation *sim = q2->sim;
  if (qa_ss_is(event.kind, "debug-shapes")) {
    qa_compat_q2_rerelease_debug_shapes_event shapes; memset(&shapes, 0, sizeof shapes);
    shapes.lines = event.lines; shapes.line_count = event.line_count; shapes.lifetime_milliseconds = event.lifetime_milliseconds;
    qa_app_shared_sim_submit_debug_shapes(sim, shapes);
    return;
  }
  if (qa_ss_is(event.kind, "world-text")) {
    qa_world_text text = event.text_value; text.content = q2->content;
    if (!qa_world_text_store_submit(&sim->world_text_store, &text, ss_time_seconds(sim), event.lifetime)) qa_range_error("Out of memory");
    return;
  }
  if (qa_ss_is(event.kind, "screen-blend") && event.has_actor) {
    qa_q2_base_player_view *view = qa_ss_map_get(&sim->q2_views, &event.actor);
    if (view) view->blend = event.blend;
  }
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q2-rerelease"; source.q2_rerelease = &event;
  qa_app_shared_sim_emit(sim, q2->content, source, NULL, NULL);
}
static const char *ss_rr_light_style(void *ctx, double style) { return qa_app_sim_simulation_events_light_style(((ss_q2 *)ctx)->sim->events, style); }
static qa_q2rr_player_identity ss_rr_player_identity(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  if (!sim->options.player_identity) ss_fail("Q2 rerelease admission requires session seat identity");
  double seat = 0; const char *social = NULL;
  sim->options.player_identity(sim->options.identity_ctx, qa_app_shared_sim_require_player(sim, actor)->client, &seat, &social);
  return (qa_q2rr_player_identity){seat, social};
}
static bool ss_rr_clip_trigger(void *ctx, qa_q2_entity *trigger, qa_actor_id actor, qa_q2_game_services *game) {
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  qa_body_state body; double model;
  bool has_body = qa_app_shared_sim_body(sim, actor, &body);
  qa_body_state brush = game->body(game->ctx, trigger->actor);
  if (!has_body || !qa_ss_source_model(trigger->model, &model)) ss_fail("Exact Q2 trigger clipping requires a brush model and live player body");
  qa_trace_query query; memset(&query, 0, sizeof query);
  query.start = body.origin; query.end = body.origin;
  query.shape.kind = "box"; query.shape.bounds = body.bounds;
  query.target.is_model = true; query.target.model = model; query.target.origin = brush.origin; query.target.angles = brush.angles;
  query.policy.kind = "q2"; query.policy.contents_mask = -1; query.policy.leaf_contents = "stored";
  query.numeric = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider).numeric;
  query.pass_actor = &actor;
  qa_trace_result trace = qa_shared_trace(sim->scene, query);
  return trace.start_solid || trace.all_solid;
}
static qa_q2rr_navigation ss_rr_navigation(void *ctx, qa_vec3 start, qa_vec3 goal, const qa_actor_id *actor) {
  (void)actor;
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  qa_q2rr_navigation result; memset(&result, 0, sizeof result);
  if (!sim->native_navigation) { result.kind = "no-navigation"; return result; }
  qa_rerelease_path_request request; memset(&request, 0, sizeof request);
  request.start = start; request.goal = goal; request.flags = 0xffffffff; request.ignore_node_flags = true;
  qa_rerelease_path_info path = qa_rerelease_path_to_goal(qa_app_sim_application_bot_navigation_runtime(sim->native_navigation), request);
  if (path.code < 5) { result.kind = "path"; result.distance_squared = path.distance_squared; result.points = path.points; result.point_count = path.point_count; }
  else result.kind = "unreachable";
  return result;
}
static bool ss_rr_monsters_searching(void *ctx, const qa_actor_id *player) {
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  return sim->strategy->monsters_searching(sim, player);
}
static bool ss_rr_grounded_on_world(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q2 *)ctx)->sim;
  qa_trace_hit ground = qa_app_shared_sim_require_player(sim, actor)->ground;
  qa_actor_id world; bool has_world = qa_app_shared_sim_world_actor(sim, &world);
  return ground.kind == QA_TRACE_HIT_WORLD || (ground.kind == QA_TRACE_HIT_ACTOR && has_world && qa_same_actor(ground.actor, world));
}
static void ss_rr_set_world_gravity(void *ctx, double gravity) { qa_app_shared_sim_set_world_gravity(((ss_q2 *)ctx)->sim, gravity); }

static void ss_create_q2(qa_app_shared_simulation *sim, qa_app_sim_actor_host_runtime runtime, qa_provider_id campaign, qa_ss_source_runtime *out) {
  const qa_executable_recipe *recipe = &sim->recipe;
  qa_content_id content = recipe->map.entities.content;
  bool rerelease_content = content && strstr(content, ":rerelease:");
  ss_q2 *q2 = qa_app_shared_sim_keep(sim, sizeof *q2);
  q2->sim = sim; q2->content = content;
  q2->weapons = qa_app_shared_sim_keep(sim, sizeof *q2->weapons);
  qa_q2w_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.ctx = q2; hooks.emit = ss_q2w_emit; hooks.noise = ss_q2w_noise; hooks.dodge = ss_q2w_dodge;
  hooks.lag_is_history = false; /* lagCompensation: { kind: "current-world" } */
  hooks.ammo_changed = ss_q2w_ammo_changed; hooks.can_target = ss_q2w_can_target;
  if (!qa_q2w_weapons_init(q2->weapons, hooks)) qa_range_error("Out of memory");
  qa_q2_composition_options *common = &q2->common;
  memset(common, 0, sizeof *common);
  common->entity_hooks = (qa_q2_composition_entity_hooks){q2, ss_q2_player_push, ss_q2_set_actor_gravity, ss_q2_local_time};
  common->item_hooks.ctx = q2;
  if (rerelease_content) { common->item_hooks.weapon_respawn_seconds = ss_q2_weapon_respawn_seconds; common->item_hooks.has_weapon_respawn_seconds = true; }
  common->item_hooks.weapon_picked = ss_q2_weapon_picked; common->item_hooks.silencer = ss_q2_silencer; common->item_hooks.power_armor = ss_q2_power_armor;
  qa_q2_player_hooks *player_hooks = &common->player_hooks;
  player_hooks->ctx = q2;
  player_hooks->persistent_inventory_initialized = ss_q2_persistent_inventory;
  player_hooks->grant_selected_arsenal = ss_q2_grant; player_hooks->give_selected_item = ss_q2_give;
  player_hooks->weapon_state = ss_q2_weapon_state; player_hooks->movement = ss_q2_movement; player_hooks->set_movement = ss_q2_set_movement;
  player_hooks->select_empty = ss_q2_select_empty;
  player_hooks->player_spawned = ss_q2_player_spawned; player_hooks->emit = ss_q2_player_emit; player_hooks->noise = ss_q2_player_noise;
  player_hooks->weapon_input = ss_q2_weapon_input; player_hooks->banned = ss_q2_banned;
  q2->host = qa_app_shared_sim_q2_actor_host(sim, recipe->map.entities, runtime, ss_q2_owning_monster, q2, "primary-world");
  qa_cvar_registry *server_cvars = sim->options.source_registry;
  if (!server_cvars) {
    server_cvars = ss_cvar_registry(sim, rerelease_content ? QA_COMMAND_DIALECT_Q2_RERELEASE : QA_COMMAND_DIALECT_Q2_CLASSIC,
                                                      ss_print_event, sim);
    qa_app_shared_sim_keep_owned(sim, server_cvars, ss_cvar_registry_release); /* TS: garbage with the simulation */
  }
  q2->server_cvars = server_cvars;
  sim->q2_server_registry = server_cvars;
  qa_register_q2_server_cvars(server_cvars, recipe->match.provider);
  if (!sim->options.restore && !sim->options.source_registry)
    qa_cvar_registry_apply_archive(server_cvars, sim->options.source_archive, sim->options.source_archive_count);
  for (size_t i = 0; i < sim->options.q2_cvar_count; i++) ss_cvar_set(server_cvars, sim->options.q2_cvars[i].name, sim->options.q2_cvars[i].value);
  char skill[32];
  ss_cvar_set(server_cvars, "skill", qa_ss_number_text(sim->options.skill, skill));
  ss_cvar_set(server_cvars, "deathmatch", ss_cvar_flag(qa_ss_is(sim->options.mode, "deathmatch")));
  ss_cvar_set(server_cvars, "coop", ss_cvar_flag(qa_ss_is(sim->options.mode, "coop")));
  qa_app_shared_sim_initialize_server_settings(sim, server_cvars);
  if (!sim->options.restore) ss_cvar_set(server_cvars, "nextserver", sim->options.q2_next_server ? sim->options.q2_next_server : "");
  qa_save_value *saved_cvars = sim->options.restore ? qa_app_sim_saved_source_cvars(*sim->options.restore) : NULL;
  if (saved_cvars) qa_restore_q2_server_cvars(server_cvars, saved_cvars);
  common->host = &q2->host;
  common->weapons = q2->weapons;
  q2->match = qa_source_q2_match_selection(recipe->match.provider, server_cvars,
      sim->options.has_travel && qa_ss_is(sim->options.travel.source_kind, "q2") ? sim->options.travel.lmctf : NULL);
  common->match = &q2->match;
  common->player_rules.spawn_point = sim->initial_spawn_point; common->player_rules_mask = QA_Q2_RULE_SPAWN_POINT;
  char *map_name = qa_ss_map_name(recipe->map.geometry.requested_path);
  qa_app_shared_sim_keep_owned(sim, map_name, free); /* the string lives as long as the simulation */
  common->options.map_name = map_name;
  common->options.skill = sim->options.skill;
  common->options.mode = qa_ss_is(sim->options.mode, "deathmatch") ? QA_Q2_MODE_DEATHMATCH : qa_ss_is(sim->options.mode, "coop") ? QA_Q2_MODE_COOP : QA_Q2_MODE_SINGLEPLAYER;
  common->options.deathmatch_flags = qa_q2_source_deathmatch_flags(server_cvars);
  if (qa_cvar_registry_dialect(server_cvars) == QA_COMMAND_DIALECT_Q2_RERELEASE && qa_ss_is(sim->options.mode, "deathmatch")
      && qa_cvar_registry_variable_value(server_cvars, "g_instagib") != 0)
    common->options.instagib = true; /* TS { instagib: true } */
  common->options.max_clients = sim->options.max_clients; common->options.provider = recipe->map.entities.provider;
  common->options.campaign = campaign; common->options.combat_provider = recipe->combat.provider;
  common->options.movement_provider = recipe->movement.provider; common->options.inventory_provider = recipe->inventory.provider;
  qa_q2_composition_services *services = &common->services;
  services->ctx = q2;
  if (qa_q2_rerelease_item_services(server_cvars, &q2->item_state)) {
    services->random_items = qa_q2_rerelease_random_items; services->drop_quad_fire = qa_q2_rerelease_drop_quad_fire;
    services->ctx = q2; /* random_items/drop_quad_fire read q2->item_state through ss_q2 (first member is sim). */
  }
  services->deathmatch_flags_read = ss_q2_dm_read; services->deathmatch_flags_write = ss_q2_dm_write;
  services->shared_grapple = (qa_shared_grapple_control *)qa_app_shared_sim_shared_grapple(sim);
  services->gravity = ss_q2_services_gravity; services->hunter_camera = false;
  services->strong_mines = qa_cvar_registry_variable_value(server_cvars, "g_dm_strong_mines") != 0;
  services->emit = ss_q2_services_emit; services->foreign_powerups = ss_q2_foreign_powerups;
  bool found = false;
  char *selected = qa_selected_source_program(recipe->execution, recipe->execution_count, recipe->map.entities, &found);
  const char *program = selected && (qa_ss_is(selected, "ctf") || qa_ss_is(selected, "lmctf")) ? "baseq2" : selected ? selected : "undefined";
  static const char *const programs[] = {"baseq2", "xatrix", "rogue", "mg2", "n64"};
  int index = -1;
  for (int i = 0; i < 5; i++) if (qa_ss_is(program, programs[i])) index = i;
  char message[256];
  /* MG2/N64 campaign content only exists in the rerelease source tree, so
   * its entities/monsters/players composition is the rerelease one even
   * when the selected product edition (movement/combat/timing) is classic. */
  if (rerelease_content || qa_ss_is(program, "mg2") || qa_ss_is(program, "n64")) {
    if (index < 0) { snprintf(message, sizeof message, "Unsupported Q2 rerelease program %s", program); free(selected); ss_fail("%s", message); }
    common->rerelease_hooks = (qa_q2_rerelease_hooks){q2, ss_rr_emit, ss_rr_light_style, ss_rr_player_identity, ss_rr_clip_trigger,
        ss_rr_navigation, ss_rr_monsters_searching, NULL, NULL, NULL, NULL, ss_rr_grounded_on_world, ss_q2_player_push,
        ss_q2_set_actor_gravity, ss_rr_set_world_gravity};
    common->edition = QA_Q2_EDITION_RERELEASE;
    if (sim->options.has_travel && qa_ss_is(sim->options.travel.source_kind, "q2") && sim->options.travel.rerelease)
      common->campaign = (qa_q2_rerelease_campaign_state *)sim->options.travel.rerelease;
  } else {
    if (index < 0 || index > 2) { snprintf(message, sizeof message, "Unsupported Q2 classic program %s", program); free(selected); ss_fail("%s", message); }
    common->edition = QA_Q2_EDITION_CLASSIC;
  }
  free(selected);
  common->program = (qa_q2_program)index;
  common->options.edition = common->edition;
  qa_q2_product_runtime *product = qa_create_q2_product_runtime(common);
  if (!product) qa_range_error("Out of memory");
  q2->product = product;
  qa_bind_q2_server_cvars(&q2->binding, server_cvars, qa_q2_players_rules(product->players),
      product->rerelease ? &product->rerelease_options : NULL,
      product->match.lmctf ? qa_q2_lmctf_get_rules(product->match.lmctf) : NULL,
      product->match.ctf ? &qa_q2_ctf_get_context(product->match.ctf)->rules.capture_limit : NULL);
  qa_app_shared_sim_keep_owned(sim, &q2->binding, ss_q2_binding_release); /* TS: garbage with the simulation */
  qa_cvar_registry_set_server_active(server_cvars, true);
  if (saved_cvars) { qa_restore_q2_server_cvars(server_cvars, saved_cvars); qa_save_value_free(saved_cvars); }
  memset(out, 0, sizeof *out);
  qa_ss_map_init(&out->visited, QA_SS_KEY_STRING, sizeof(void *));
  qa_ss_map_init(&out->clients, QA_SS_KEY_CLIENT, sizeof(qa_owned_actor));
  out->kind = QA_SS_SOURCE_Q2;
  out->product = product; out->q2_weapons = q2->weapons;
}

/* ---- createSource (3303-3880): the one dispatch. ---- */
void qa_app_shared_sim_create_source(qa_app_shared_simulation *sim, bool loading, qa_ss_source_runtime *out) {
  const qa_executable_recipe *recipe = &sim->recipe;
  qa_provider_id campaign = recipe->campaign.is_campaign ? recipe->campaign.mission.provider : recipe->map.entities.provider;
  qa_provider_timing timing = qa_app_sim_provider_timing(sim->recipe, recipe->map.entities.provider);
  if (sim->options.has_q2_guest) { ss_create_native(sim, loading, timing, out); return; }
  if (sim->options.q3_guest) { qa_app_shared_sim_create_q3_qvm_source(sim, out); return; }
  if (sim->options.prepared_quakec) { ss_create_quakec(sim, out); return; }
  const char *frame_kind = timing.clock.kind == QA_CLOCK_Q2_CLASSIC ? "q2-classic"
      : timing.clock.kind == QA_CLOCK_Q2_RERELEASE ? "q2-rerelease" : "elapsed";
  qa_app_sim_actor_host_runtime runtime = ss_make_runtime(sim, timing.numeric, sim->random, frame_kind, timing.clock.frame_milliseconds);
  if (recipe->map.entities.provider && strncmp(recipe->map.entities.provider, "q3:", 3) == 0) {
    qa_app_shared_sim_create_q3_source(sim, out);
    return;
  }
  if (qa_app_sim_provider_family(recipe->map.entities.provider) == QA_GAME_FAMILY_Q1) { ss_create_q1(sim, runtime, campaign, out); return; }
  ss_create_q2(sim, runtime, campaign, out);
}

void qa_app_shared_sim_source_runtime_destroy(qa_app_shared_simulation *sim, qa_ss_source_runtime *source) {
  (void)sim;
  switch (source->kind) {
  case QA_SS_SOURCE_QUAKEC: if (source->quakec) qa_app_sim_quakec_source_destroy(source->quakec); break;
  case QA_SS_SOURCE_Q1: if (source->q1_composition) qa_comp_q1_source_composition_destroy(source->q1_composition); break;
  case QA_SS_SOURCE_Q3:
    if (source->q3) qa_app_q3_source_runtime_destroy(source->q3);
    qa_save_value_free(source->q3_restore_state);
    if (source->q3_host) qa_app_q3_created_source_host_destroy(source->q3_host);
    break;
  case QA_SS_SOURCE_Q3_QVM:
    /* The bindings borrow the game: they go first (TS beforeRetire closes them before game.retire). */
    if (source->qvm_weapons) qa_qvm_primary_weapons_destroy(source->qvm_weapons);
    if (source->qvm_pickups) qa_qvm_primary_pickups_destroy(source->qvm_pickups);
    if (source->qvm_combat) qa_qvm_combat_bindings_destroy(source->qvm_combat);
    if (source->qvm_catalog) qa_q3_guest_catalog_destroy(source->qvm_catalog);
    if (source->qvm) qa_app_q3_qvm_server_game_destroy(source->qvm);
    if (source->qvm_state) qa_app_q3_server_state_destroy(source->qvm_state);
    break;
  case QA_SS_SOURCE_Q2_NATIVE:
    if (source->classic_world) qa_app_sim_classic_guest_world_destroy(source->classic_world);
    if (source->rerelease_world) qa_app_sim_rerelease_guest_world_destroy(source->rerelease_world);
    if (source->files && !(sim->options.native_q2_travel && !((const qa_app_sim_native_q2_travel *)sim->options.native_q2_travel)->rerelease
                           && ((const qa_app_sim_native_q2_travel *)sim->options.native_q2_travel)->classic.files == source->files))
      qa_classic_original_save_files_destroy(source->files);
    break;
  case QA_SS_SOURCE_Q2:
    if (source->product) qa_q2_product_runtime_destroy(source->product);
    if (source->q2_weapons) qa_q2w_weapons_destroy(source->q2_weapons);
    break;
  case QA_SS_SOURCE_LOADING: break;
  }
  qa_ss_map_free(&source->visited);
  qa_ss_map_free(&source->clients);
  memset(source, 0, sizeof *source);
}

/* load (969-972): simulation.source = await travel(nextFrame). */
void qa_app_shared_sim_run_pending_native_travel(qa_app_shared_simulation *sim, qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx) {
  qa_ss_pending_native_travel *travel = &sim->pending_native_travel;
  if (qa_ss_is(travel->edition, "rerelease")) {
    const qa_app_sim_q2_rerelease_visited_level **level = travel->has_revisit
        ? qa_ss_map_get(&sim->source.visited, &sim->recipe.map.geometry.requested_path) : NULL;
    ss_native native; memset(&native, 0, sizeof native); native.sim = sim;
    qa_app_sim_rerelease_guest_revisit revisit; memset(&revisit, 0, sizeof revisit);
    ss_native *kept = qa_app_shared_sim_keep(sim, sizeof *kept); *kept = native;
    if (level) { kept->rerelease_revisit = *level; revisit.level = (*level)->level; revisit.ctx = kept; revisit.restore_server_state = ss_rerelease_revisit_restore; }
    sim->source.rerelease_world = qa_app_sim_rerelease_guest_world_travel_loading(travel->retained->rerelease_travel.world, travel->map,
        &travel->rerelease_services, next_frame, frame_ctx, level ? &revisit : NULL);
    return;
  }
  const qa_q2_classic_visited_level **revisit = travel->has_revisit
      ? qa_ss_map_get(&sim->source.visited, &sim->recipe.map.geometry.requested_path) : NULL;
  if (!revisit) {
    sim->source.classic_world = qa_app_sim_classic_guest_world_travel_loading(travel->retained->classic.world, travel->map,
        &travel->classic_services, next_frame, frame_ctx, NULL);
    return;
  }
  ss_native *kept = qa_app_shared_sim_keep(sim, sizeof *kept);
  kept->sim = sim; kept->classic_revisit = *revisit;
  sim->source.classic_world = qa_app_shared_sim_classic_travel_revisit(sim, kept, travel->retained->classic.world, travel->map,
      &travel->classic_services, true, next_frame, frame_ctx);
}

/* files.withTravelLevel(revisit.level, levelPath => world.travel(map, services,
 * { levelPath, restoreServerState })) and its Loading form. */
typedef struct ss_travel_level {
  ss_native *native; qa_app_sim_classic_guest_world *world; qa_app_sim_classic_guest_map map;
  const qa_app_sim_classic_guest_map_services *binding; bool loading;
  qa_app_shared_sim_next_frame_fn next_frame; void *frame_ctx;
} ss_travel_level;
static void *ss_travel_with_level(void *ctx, const char *level_path) {
  ss_travel_level *travel = ctx;
  qa_app_sim_classic_guest_revisit revisit = {level_path, travel->native, ss_classic_revisit_restore};
  return travel->loading
      ? qa_app_sim_classic_guest_world_travel_loading(travel->world, travel->map, travel->binding, travel->next_frame, travel->frame_ctx, &revisit)
      : qa_app_sim_classic_guest_world_travel(travel->world, travel->map, travel->binding, &revisit);
}
qa_app_sim_classic_guest_world *qa_app_shared_sim_classic_travel_revisit(qa_app_shared_simulation *sim, void *native_ctx,
    qa_app_sim_classic_guest_world *world, qa_app_sim_classic_guest_map map, const qa_app_sim_classic_guest_map_services *binding,
    bool loading, qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx) {
  ss_native *native = native_ctx;
  ss_travel_level travel = {native, world, map, binding, loading, next_frame, frame_ctx};
  qa_classic_original_save_files *files = sim->source.files ? sim->source.files : native->files;
  const qa_q2_classic_visited_level *level = native->classic_revisit;
  return loading
      ? qa_classic_original_save_files_with_travel_level_loading(files, level->level.bytes, level->level.length, ss_travel_with_level, &travel)
      : qa_classic_original_save_files_with_travel_level(files, level->level.bytes, level->level.length, ss_travel_with_level, &travel);
}

/* ---- createSource TS Q3 branch (runtime.ts 3567-3650) ----
 * The createQ3SourceHost operations close over the simulation; ss_q3 is the
 * record they share (kept for the simulation's lifetime). */
typedef struct ss_q3 {
  qa_app_shared_simulation *sim;
  qa_content_id content;
  qa_weapon_behavior_projectile_port weapon_behavior;
  qa_original_pickup_admission original_pickups;
  qa_app_sim_cvar_name_value *cvars; size_t cvar_count;
  qa_app_q3_created_source_host *created;
} ss_q3;
static qa_app_shared_simulation *ss_q3_sim(void *ctx) { return ((ss_q3 *)ctx)->sim; }
static bool ss_q3_grant(void *ctx, qa_actor_id actor, const char *category) { return qa_app_shared_sim_grant_selected_arsenal(ss_q3_sim(ctx), actor, category); }
static bool ss_q3_give(void *ctx, qa_actor_id actor, const char *const *args, size_t argc) { return qa_app_shared_sim_give_selected_item(ss_q3_sim(ctx), actor, args, argc); }
static double ss_q3_now(void *ctx) { return trunc(ss_time_seconds(ss_q3_sim(ctx)) * 1000); }
static void ss_q3_schedule(void *ctx, qa_owned_actor actor, bool has_due, double due) {
  qa_app_shared_simulation *sim = ss_q3_sim(ctx);
  if (!has_due) qa_frame_scheduler_cancel(sim->scheduler, actor);
  else qa_app_shared_sim_schedule(sim, actor, due / 1000);
}
static void ss_q3_run_think(void *ctx, qa_owned_actor actor, double time) {
  (void)time;
  qa_app_shared_simulation *sim = ss_q3_sim(ctx);
  qa_frame_context frame = sim->source_frame; frame.phase = QA_FRAME_PHASE_ENTITY_THINK;
  qa_frame_scheduler_run(sim->scheduler, actor.id, frame, QA_THINK_DURING_PHYSICS);
}
static void ss_q3_collision(void *ctx, qa_owned_actor actor, qa_actor_collision collision) {
  qa_app_sim_shared_physics_set_collision(ss_q3_sim(ctx)->physics, actor, collision);
}
static qa_victim_armor_context ss_q3_armor(void *ctx, qa_damage_request request) { return qa_app_shared_sim_victim_armor_context(ss_q3_sim(ctx), request); }
static bool ss_q3_primary_allowed(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ss_q3_sim(ctx);
  if (sim->has_selected_arsenal) return false;
  qa_app_sim_weapon_slot **slot = qa_ss_map_get(&sim->weapon_slots, &actor);
  return slot == NULL || qa_app_sim_weapon_slot_primary_selected(*slot);
}
static qa_q3_source_pickup_admission ss_q3_admit_pickup(void *ctx, qa_q3_source_pickup_descriptor item) {
  return qa_app_shared_sim_admit_selected_q3_pickup(ss_q3_sim(ctx), item);
}
static qa_q3_source_pickup_preview ss_q3_preview_pickup(void *ctx, qa_q3_source_pickup_descriptor item) {
  return qa_app_shared_sim_preview_selected_q3_pickup(ss_q3_sim(ctx), item);
}
static void ss_q3_ammo_timer(void *ctx, qa_actor_id actor, double weapon, double value) {
  qa_app_shared_simulation *sim = ss_q3_sim(ctx);
  if (sim->selected_ammo_timers) qa_app_q3_mapped_ammo_regeneration_stored(sim->selected_ammo_timers, actor, weapon, value);
}
static bool ss_q3_timer_ownership(void *ctx, qa_actor_id actor, qa_q3_ta_client_timer_ownership *out) {
  memset(out, 0, sizeof *out);
  out->ordinary_decay = true;
  out->has_ammo = qa_app_shared_sim_selected_ammo(ss_q3_sim(ctx), actor, &out->ammo, &out->ammo_count);
  return true;
}
static double ss_q3_speed_multiplier(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ss_q3_sim(ctx);
  if (sim->selected_q3_source && qa_app_q3_selected_source_owns_equipment(sim->selected_q3_source))
    return qa_app_q3_selected_source_speed_multiplier(sim->selected_q3_source, actor);
  qa_q3_game_entity *entity = (sim->strategy->as_q3(sim) != NULL)
      ? qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor) : NULL;
  if (!entity || !entity->client) ss_fail("Q3 movement speed lost its client");
  return qa_q3_ta_client_speed_multiplier(&entity->client->ps);
}
static qa_q3_game_entity *ss_q3_foreign(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return NULL; }
static bool ss_q3_is_player(void *ctx, qa_actor_id actor) { return qa_app_shared_sim_player(ss_q3_sim(ctx), actor) != NULL; }
/* moverActors.observe/write/link/release (3594-3611). */
static bool ss_q3_mover_observe(void *ctx, qa_actor_id id, qa_q3_shared_mover_body *out) {
  qa_app_shared_simulation *sim = ss_q3_sim(ctx);
  qa_owned_actor actor; qa_body_state state;
  if (!qa_session_actor_registry_resolve_owned(sim->actors, id, &actor) || !qa_app_shared_sim_body(sim, id, &state)) return false;
  qa_linked_body_result linked = qa_shared_body_table_linked(ss_bodies(sim), id);
  if (!linked.has_value) return false;
  qa_q2_motion motion;
  bool has_motion = qa_app_sim_shared_physics_motion_of_actor(sim->physics, id, &motion);
  memset(out, 0, sizeof *out);
  out->actor = actor; out->state = state; out->absolute_bounds = linked.value.absolute_bounds;
  out->clip_mask = has_motion ? motion.clip_mask : 1;
  if (state.ground) { out->has_ground = true; out->ground = *state.ground; out->state.ground = &out->ground; }
  out->kind = qa_shared_body_table_attachment(ss_bodies(sim), id).has_value ? QA_Q3_MOVER_BODY_ATTACHED
      : qa_app_shared_sim_player(sim, id) != NULL ? QA_Q3_MOVER_BODY_PLAYER
      : !has_motion || qa_ss_is(motion.kind, "stationary") || qa_ss_is(motion.kind, "push") || qa_ss_is(motion.kind, "stop")
          ? QA_Q3_MOVER_BODY_FIXED : QA_Q3_MOVER_BODY_MOVABLE;
  return true;
}
static void ss_q3_mover_write(void *ctx, qa_owned_actor actor, qa_vec3 origin, const qa_actor_id *ground) {
  qa_app_shared_simulation *sim = ss_q3_sim(ctx);
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor.id, &body)) return;
  body.origin = origin; body.ground = ground;
  qa_shared_body_table_write(ss_bodies(sim), actor, body);
}
static void ss_q3_mover_link(void *ctx, qa_owned_actor actor) {
  qa_app_shared_simulation *sim = ss_q3_sim(ctx);
  if (qa_session_actor_registry_is_live(sim->actors, actor.id)) qa_shared_body_table_link(ss_bodies(sim), actor, NULL);
}
static void ss_q3_mover_release(void *ctx, qa_owned_actor actor) { qa_session_actor_registry_release(ss_q3_sim(ctx)->actors, actor); }
/* sourceCommand (3613-3619). */
static qa_q3_base_user_command ss_q3_source_command(void *ctx, qa_actor_command input) {
  qa_app_shared_simulation *sim = ss_q3_sim(ctx);
  qa_q3_game_entity *entity = (sim->strategy->as_q3(sim) != NULL)
      ? qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, input.actor) : NULL;
  if (!entity || !entity->client) ss_fail("Q3 source command has no actual client");
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, input.actor);
  qa_app_sim_q3_command_player command_player = {player->arsenal, sim->recipe.map.entities.content, player->profile};
  qa_q3_base_user_command command = qa_app_sim_q3_source_command(input, command_player, sim->source_scheduling_milliseconds,
      entity->client->ps.weapon);
  /* input.command.kind: only "q3" is distinguished by relativeQ3SourceCommand. */
  return qa_app_sim_relative_q3_source_command(input.source, input.command.kind == QA_USER_COMMAND_Q3 ? "q3" : "foreign", command,
      entity->client->ps.delta_angles, input.angle_space);
}
static void ss_q3_spawn_player(void *ctx, qa_q3_game_entity *entity, qa_q3_ta_spawn_pose pose) { qa_app_shared_sim_spawn_q3_player(ss_q3_sim(ctx), entity, pose); }
static void ss_q3_select_spawn_weapon(void *ctx, qa_q3_game_entity *entity) { qa_app_shared_sim_select_q3_spawn_weapon(ss_q3_sim(ctx), entity); }
static qa_q3_ta_client_movement_result ss_q3_move_client(void *ctx, qa_q3_game_entity *entity, qa_q3_base_user_command command,
    qa_q3_ta_client_movement_options options) {
  return qa_app_shared_sim_move_q3_client(ss_q3_sim(ctx), entity, command, options);
}
static void ss_q3_emit(void *ctx, qa_app_q3_source_event event) {
  ss_q3 *q3 = ctx;
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q3-source"; source.q3_source = &event;
  qa_app_shared_sim_emit(q3->sim, q3->content, source, NULL, NULL);
}
static double ss_q3_client_number(void *ctx, qa_actor_id actor) {
  return (double)qa_app_shared_sim_require_player(ss_q3_sim(ctx), actor)->client.slot;
}
static void ss_q3_out_of_range(char *out, size_t size, const char *product, const qa_q3_ta_foreign_objective_adaptation *result) {
  /* JSON.stringify(objectives) of the missing/unsupported result. */
  if (result->kind == QA_Q3_TA_OBJECTIVE_MISSING) {
    size_t used = (size_t)snprintf(out, size, "{\"kind\":\"missing-objectives\",\"classnames\":[");
    for (size_t i = 0; i < result->missing_count && used < size; i++)
      used += (size_t)snprintf(out + used, size - used, "%s\"%s\"", i ? "," : "", result->missing[i]);
    if (used < size) snprintf(out + used, size - used, "]}");
  } else snprintf(out, size, "{\"kind\":\"unsupported-mode\",\"product\":\"%s\",\"gameType\":%s}", product, QA_JS_NUMBER(result->game_type));
}
void qa_app_shared_sim_create_q3_source(qa_app_shared_simulation *sim, qa_ss_source_runtime *out) {
  const qa_executable_recipe *recipe = &sim->recipe;
  bool found = false;
  char *selected = qa_selected_source_program(recipe->execution, recipe->execution_count, recipe->map.entities, &found);
  const char *product = found && selected && strcmp(selected, "missionpack") == 0 ? "missionpack" : "baseq3";
  free(selected);
  qa_q3_base_product product_kind = qa_ss_is(product, "missionpack") ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
  ss_q3 *q3 = qa_app_shared_sim_keep(sim, sizeof *q3);
  q3->sim = sim; q3->content = recipe->map.entities.content;
  /* Rules-driven team launches put every client straight into play instead of
   * the source's pick-a-team spectator step. */
  bool ctf = qa_ss_is(recipe->match.provider, "q3:ctf");
  q3->cvar_count = sim->options.q3_cvar_count + (ctf ? 2 : 0);
  q3->cvars = qa_app_shared_sim_keep(sim, (q3->cvar_count ? q3->cvar_count : 1) * sizeof *q3->cvars);
  for (size_t i = 0; i < sim->options.q3_cvar_count; i++) q3->cvars[i] = sim->options.q3_cvars[i];
  if (ctf) {
    q3->cvars[sim->options.q3_cvar_count] = (qa_app_sim_cvar_name_value){"g_teamAutoJoin", "1"};
    q3->cvars[sim->options.q3_cvar_count + 1] = (qa_app_sim_cvar_name_value){"g_teamForceBalance", "1"};
  }
  q3->original_pickups = qa_shared_original_pickup_admission_as_contract(sim->original_pickups);
  q3->weapon_behavior = ss_weapon_port(sim);
  qa_app_q3_host_operations operations; memset(&operations, 0, sizeof operations);
  qa_app_q3_source_host *base = &operations.base;
  base->ctx = q3;
  base->actors = sim->actors; base->bodies = ss_bodies(sim); base->callbacks = sim->callbacks;
  base->combat = sim->combat; base->inventory = sim->inventory;
  base->grant_selected_arsenal = ss_q3_grant; base->give_selected_item = ss_q3_give;
  base->scene = sim->scene; base->death_animations = &sim->death_animations;
  base->now = ss_q3_now; base->schedule = ss_q3_schedule; base->run_think = ss_q3_run_think;
  base->collision = ss_q3_collision; base->armor_context = ss_q3_armor;
  base->primary_attack_allowed = ss_q3_primary_allowed;
  base->admit_pickup = ss_q3_admit_pickup;
  base->original_pickups = &q3->original_pickups;
  base->ammo_timer_stored = ss_q3_ammo_timer;
  base->timer_ownership = ss_q3_timer_ownership;
  base->speed_multiplier = ss_q3_speed_multiplier;
  base->preview_pickup = ss_q3_preview_pickup;
  /* Foreign arsenal/world actors (Q1/Q2 projectiles, gibs) live in the shared simulation
   * without Q3 entity projections; Q3 queries report them as no entity. */
  base->foreign = ss_q3_foreign;
  base->is_player = ss_q3_is_player;
  base->mover_actors.ctx = q3;
  base->mover_actors.observe = ss_q3_mover_observe; base->mover_actors.write = ss_q3_mover_write;
  base->mover_actors.link = ss_q3_mover_link; base->mover_actors.release = ss_q3_mover_release;
  base->source_command = ss_q3_source_command;
  base->spawn_player = ss_q3_spawn_player; base->select_spawn_weapon = ss_q3_select_spawn_weapon;
  base->move_client = ss_q3_move_client;
  operations.bots = sim->bot_services.source;
  operations.emit_ctx = q3; operations.emit = ss_q3_emit; operations.client_number = ss_q3_client_number;
  operations.session = qa_session_actor_registry_session(sim->actors);
  char *map_name = qa_ss_map_name(recipe->map.geometry.requested_path);
  qa_app_q3_host_settings settings; memset(&settings, 0, sizeof settings);
  settings.game_type = ctf ? QA_Q3_BASE_GT_CTF : qa_ss_is(sim->options.mode, "singleplayer") ? 2 : 0;
  settings.single_player = qa_ss_is(sim->options.mode, "singleplayer");
  settings.max_clients = sim->options.max_clients;
  settings.map_name = qa_app_shared_sim_intern(sim, map_name);
  free(map_name);
  if (!sim->options.restore && sim->options.source_registry) settings.source_registry = sim->options.source_registry;
  if (!sim->options.restore && sim->options.source_archive) {
    settings.source_archive = sim->options.source_archive; settings.source_archive_count = sim->options.source_archive_count;
  }
  settings.cvars = (const qa_app_q3_host_cvar *)q3->cvars; settings.cvar_count = q3->cvar_count;
  q3->created = qa_app_create_q3_source_host(operations, settings);
  if (!q3->created) ss_fail("Out of memory");
  out->q3_host = q3->created;
  qa_app_q3_source_host *host = qa_app_q3_created_source_host_get(q3->created);
  if (!sim->options.restore) {
    size_t count = 0;
    const qa_q3_base_cvar_definition *definitions = qa_q3_base_game_cvar_definitions(product_kind, &count);
    for (size_t i = 0; i < count; i++) {
      qa_cvar_snapshot result;
      if (qa_cvar_registry_register(host->cvars, definitions[i].name, definitions[i].value, definitions[i].flags, &result)) qa_cvar_read_free(&result);
    }
    qa_app_shared_sim_initialize_server_settings(sim, host->cvars);
  }
  /* Q3's client layer owns cl_running; game code (bot admission) reads it to decide
   * whether a local client should finish deferred model loads. */
  const char *running = sim->options.dedicated ? "0" : "1";
  if (!ss_cvar_exists(host->cvars, "cl_running")) ss_cvar_register(host->cvars, "cl_running", running);
  else ss_cvar_set(host->cvars, "cl_running", running);
  double configured = qa_cvar_registry_variable_value(host->cvars, "g_gametype");
  if (!sim->options.restore && (configured < 0 || configured >= QA_Q3_BASE_GT_MAX_GAME_TYPE)) {
    /* G_RegisterCvars resets an out-of-range gametype to free-for-all. */
    char message[96];
    snprintf(message, sizeof message, "g_gametype %s is out of range, defaulting to 0\n", QA_JS_NUMBER(configured));
    qa_app_shared_sim_print(sim, message, NULL);
    ss_cvar_set(host->cvars, "g_gametype", "0");
  }
  qa_q3_ta_world_kind world_kind = qa_ss_is(sim->world_kind, "q1-bsp") ? QA_Q3_TA_WORLD_Q1_BSP
      : qa_ss_is(sim->world_kind, "q2-bsp") ? QA_Q3_TA_WORLD_Q2_BSP : QA_Q3_TA_WORLD_Q3_BSP;
  qa_q3_ta_foreign_objective_adaptation objectives = qa_q3_ta_adapt_foreign_q3_objectives(world_kind, sim->world_entities,
      product_kind, qa_cvar_registry_variable_value(host->cvars, "g_gametype"), NULL, 0);
  if (objectives.kind != QA_Q3_TA_OBJECTIVE_READY) {
    char detail[512];
    ss_q3_out_of_range(detail, sizeof detail, product, &objectives);
    qa_q3_ta_foreign_objective_adaptation_destroy(&objectives);
    ss_fail("Selected Q3 objectives are unavailable: %s", detail);
  }
  qa_app_shared_sim_create_foreign_world(sim);
  qa_app_q3_source_options options; memset(&options, 0, sizeof options);
  options.recipe = sim->recipe; options.weapon_behavior = &q3->weapon_behavior;
  options.weapon_provider = sim->weapon_provider; options.product = product;
  options.entities = qa_app_shared_sim_intern(sim, objectives.entities);
  qa_q3_ta_foreign_objective_adaptation_destroy(&objectives);
  options.foreign_world = sim->has_foreign_world || sim->has_foreign_q1_world;
  options.seed = sim->options.seed; options.max_clients = sim->options.max_clients; options.build_date = "TypeScript port";
  options.session_carry = sim->options.q3_session;
  qa_app_q3_source_runtime_mode mode = {false, NULL};
  if (sim->options.restore) {
    qa_provider_checkpoint checkpoint = qa_app_sim_simulation_provider_checkpoint(*sim->options.restore, "q3:native");
    out->q3_restore_state = qa_decode_checkpoint_value(checkpoint.bytes.bytes, checkpoint.bytes.length);
    if (!out->q3_restore_state) ss_fail("Out of memory");
    mode.restore = true; mode.state = out->q3_restore_state;
  }
  out->kind = QA_SS_SOURCE_Q3;
  out->q3 = qa_app_q3_source_runtime_new(options, *host, mode);
  if (!out->q3) ss_fail("Out of memory");
  out->q3_owner = qa_app_q3_source_runtime_owner(out->q3);
}

/* ---- createSource Q3 QVM guest branch (runtime.ts 3344-3497) ----
 * The closures share ss_qvm (kept for the simulation's lifetime). game is
 * the local `game` (set once constructed); current is guestGame.current. */
typedef struct ss_qvm {
  qa_app_shared_simulation *sim;
  const qa_app_sim_q3_guest_options *guest;
  qa_app_q3_qvm_server_game *game, *current;
  qa_qvm_combat_bindings *combat;
  qa_qvm_primary_pickups *pickups;
  qa_qvm_primary_weapons *weapons;
  qa_q3_guest_catalog *catalog;
  const qa_qvm_inventory_profile *inventory;
  qa_qvm_combat_source combat_source;
  qa_qvm_catalog_records records; /* catalog records through the compat type (rebuilt per read) */
  qa_qvm_inventory_weapon *inventory_weapons; size_t inventory_weapon_count;
  qa_inventory_entry *arsenal_ammo;
  qa_fixed_movement_pose pose; qa_movement_body_shape body; qa_owned_actor body_owner;
  qa_mod_client_movement_outputs outputs;
} ss_qvm;
static ss_qvm *ss_qvm_of(void *ctx) { return ctx; }
static double ss_qvm_now(qa_app_shared_simulation *sim) { return trunc(ss_time_seconds(sim) * 1000); }
static qa_app_q3_guest_records *ss_qvm_records(ss_qvm *q) { return qa_app_q3_qvm_server_game_records(q->game); }
static qa_q3_base_user_command ss_qvm_filter_command(void *ctx, double slot, qa_q3_base_user_command command) {
  ss_qvm *q = ctx; qa_app_shared_simulation *sim = q->sim;
  if (q->current == NULL || qa_app_prepared_q3_primary_weapons(q->guest->prepared) != NULL || !sim->has_selected_arsenal) return command;
  qa_actor_id reference;
  if (!qa_app_q3_guest_records_reference(qa_app_q3_qvm_server_game_records(q->current), slot, &reference)) return command;
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_owned(sim->actors, reference, &actor)) return command;
  qa_app_sim_weapon_slot **weapon_slot = qa_ss_map_get(&sim->weapon_slots, &actor.id);
  if (weapon_slot == NULL || qa_app_sim_weapon_slot_primary_selected(*weapon_slot)) return command;
  command.buttons = (double)(qa_js_to_int32(command.buttons) & ~1);
  return command;
}
static double ss_qvm_state_now(void *ctx) { return ss_qvm_now(ss_qvm_of(ctx)->sim); }
static void ss_qvm_print(void *ctx, const char *text) { ss_qvm *q = ctx; q->guest->print(q->guest->print_ctx, text); }
/* The catalog's weapons through the compat inventory type. */
static const qa_qvm_inventory_weapon *ss_qvm_weapons(void *ctx, size_t *out_count) {
  ss_qvm *q = ctx;
  if (q->catalog == NULL) ss_fail("QVM item catalog is not bound");
  size_t count = 0;
  const qa_q3_guest_weapon *weapons = qa_q3_guest_catalog_weapons(q->catalog, &count);
  free(q->inventory_weapons);
  q->inventory_weapons = calloc(count ? count : 1, sizeof *q->inventory_weapons);
  if (!q->inventory_weapons) ss_fail("Out of memory");
  for (size_t i = 0; i < count; i++) {
    q->inventory_weapons[i].weapon = weapons[i].weapon; q->inventory_weapons[i].item = weapons[i].item; q->inventory_weapons[i].ammo = weapons[i].ammo;
  }
  q->inventory_weapon_count = count;
  *out_count = count;
  return q->inventory_weapons;
}
typedef struct ss_qvm_client { ss_qvm *q; qa_owned_actor actor; } ss_qvm_client;
static double ss_qvm_inventory_client(void *ctx) {
  ss_qvm_client *c = ctx; qa_app_shared_simulation *sim = c->q->sim;
  qa_session_actor_registry_assert_owned(sim->actors, c->actor);
  double slot;
  if (!qa_app_q3_guest_records_slot(ss_qvm_records(c->q), c->actor.id, &slot) || qa_app_q3_guest_records_is_input_retired(ss_qvm_records(c->q), slot))
    ss_fail("QVM inventory owner is no longer admitted");
  return slot;
}
static qa_qvm_inventory_options ss_qvm_inventory_options(ss_qvm *q, ss_qvm_client *client, double (*slot)(void *ctx)) {
  qa_qvm_inventory_options options; memset(&options, 0, sizeof options);
  qa_qvm_game *game = qa_app_q3_qvm_server_game_game(q->game);
  options.module = &game->module; options.data = &game->data; options.profile = *q->inventory;
  options.weapons.ctx = q; options.weapons.get = ss_qvm_weapons;
  options.ctx = client; options.client = slot;
  return options;
}
/* records.admit (3367-3381). */
static void ss_qvm_admit(void *ctx, qa_owned_actor actor) {
  ss_qvm *q = ctx; qa_app_shared_simulation *sim = q->sim;
  if (q->combat) qa_qvm_combat_bindings_admit(q->combat, actor);
  double slot;
  if (q->inventory == NULL || !qa_app_q3_guest_records_slot(ss_qvm_records(q), actor.id, &slot)
      || slot >= qa_qvm_game_data_get_num_clients(qa_app_q3_qvm_server_game_engine(q->game)->data)) return;
  const qa_inventory_entry *saved = NULL; size_t saved_count = 0;
  if (sim->options.restore)
    for (size_t i = 0; i < sim->options.restore->inventory_count; i++) {
      qa_owned_actor resolved;
      if (qa_session_actor_registry_resolve_saved(sim->actors, sim->options.restore->inventories[i].actor, &resolved)
          && resolved.token == actor.token && qa_same_actor(resolved.id, actor.id)) {
        saved = sim->options.restore->inventories[i].entries; saved_count = sim->options.restore->inventories[i].entry_count; break;
      }
    }
  ss_qvm_client *client = qa_app_shared_sim_keep(sim, sizeof *client);
  client->q = q; client->actor = actor;
  qa_app_shared_sim_bind_source_inventory(sim, actor, qa_qvm_inventory_binding(ss_qvm_inventory_options(q, client, ss_qvm_inventory_client)),
      saved, saved_count);
}
static void ss_qvm_collision(void *ctx, qa_owned_actor actor, qa_actor_collision collision) {
  qa_app_sim_shared_physics_set_collision(ss_qvm_of(ctx)->sim->physics, actor, collision);
}
static double ss_qvm_game_now(void *ctx) { return ss_qvm_now(ss_qvm_of(ctx)->sim); }
static void ss_qvm_assert_current(void *ctx) { qa_app_shared_sim_assert_open(ss_qvm_of(ctx)->sim); }
static void ss_qvm_client_changed(void *ctx, const char *kind, qa_actor_id actor) { qa_app_shared_sim_notify_client_event(ss_qvm_of(ctx)->sim, kind, actor); }
static void ss_qvm_before_retire(void *ctx) {
  ss_qvm *q = ctx;
  if (q->weapons) qa_qvm_primary_weapons_close(q->weapons);
  if (q->pickups) qa_qvm_primary_pickups_close(q->pickups);
  if (q->combat) qa_qvm_combat_bindings_close(q->combat);
  if (q->catalog) qa_q3_guest_catalog_close(q->catalog);
}
static void ss_qvm_source_restored(void *ctx) { ss_qvm *q = ctx; if (q->catalog) qa_q3_guest_catalog_reset(q->catalog); }
/* arsenal (3388-3393). */
static bool ss_qvm_arsenal(void *ctx, qa_actor_id actor, qa_arsenal_state *out) {
  ss_qvm *q = ctx; qa_app_shared_simulation *sim = q->sim;
  if (q->catalog == NULL) ss_fail("QVM item catalog is not bound");
  qa_app_q3_guest_records *records = ss_qvm_records(q);
  double slot = qa_app_q3_guest_records_require_slot(records, actor);
  qa_q3_player_state state; double stats[16], persistent[16], powerups[16], ammo[16];
  qa_app_q3_guest_records_player(records, slot, &state, stats, persistent, powerups, ammo);
  memset(out, 0, sizeof *out);
  out->provider = sim->recipe.map.entities.provider;
  if (q->weapons == NULL) {
    size_t count = 0;
    const qa_q3_guest_weapon *weapons = qa_q3_guest_catalog_weapons(q->catalog, &count);
    for (size_t i = 0; i < count; i++) if (weapons[i].weapon == state.weapon) { out->active_weapon = weapons[i].item; break; }
  } else out->active_weapon = qa_qvm_primary_weapons_active(q->weapons, actor);
  out->state.kind = "q3"; out->state.source_weapon = state.weapon; out->state.state = state.weapon_state;
  out->state.time_milliseconds = state.weapon_time_milliseconds;
  out->ammo = qa_shared_inventory_table_entries(sim->inventory, actor, &out->ammo_count);
  return true;
}
/* botCommand (3394-3396). */
static void ss_qvm_bot_command(void *ctx, qa_actor_id actor, qa_q3_base_user_command command) {
  ss_qvm *q = ctx; qa_app_shared_simulation *sim = q->sim;
  qa_actor_command input; memset(&input, 0, sizeof input);
  input.actor = actor;
  input.source.kind = QA_COMMAND_SOURCE_BOT; input.source.provider = q->guest->prepared->module.id;
  const qa_mod_client_command_receipt *previous = qa_ss_map_get(&sim->mod_client_commands, &actor);
  input.sequence = (previous ? previous->input.sequence : 0) + 1;
  input.command.kind = QA_USER_COMMAND_Q3;
  input.command.as.q3.server_time_milliseconds = command.server_time;
  input.command.as.q3.angle_words.values[0] = command.angles.x; input.command.as.q3.angle_words.values[1] = command.angles.y;
  input.command.as.q3.angle_words.values[2] = command.angles.z;
  input.command.as.q3.buttons = command.buttons; input.command.as.q3.weapon = command.weapon;
  input.command.as.q3.forward_move = command.forwardmove; input.command.as.q3.right_move = command.rightmove;
  input.command.as.q3.up_move = command.upmove;
  qa_app_shared_sim_observe_client_command(sim, &input);
}
/* beforeDisconnect (3397-3400). */
static void ss_qvm_before_disconnect(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ss_qvm_of(ctx)->sim;
  qa_app_shared_sim_notify_client_event(sim, "disconnecting", actor);
  if (qa_session_actor_registry_is_live(sim->actors, actor)) {
    if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, actor);
    qa_app_shared_sim_step_hand_grenade(sim, actor, "removing");
  }
}
/* projectInventory (3404-3411). */
typedef struct ss_qvm_projection { ss_qvm *q; qa_owned_actor owner; qa_actor_id actor; } ss_qvm_projection;
static double ss_qvm_projection_client(void *ctx) {
  ss_qvm_projection *p = ctx;
  qa_session_actor_registry_assert_owned(p->q->sim->actors, p->owner);
  double slot = qa_app_q3_guest_records_require_slot(ss_qvm_records(p->q), p->actor);
  if (qa_app_q3_guest_records_is_input_retired(ss_qvm_records(p->q), slot)) ss_fail("Original inventory projection client is retired");
  return slot;
}
static void ss_qvm_project_inventory(ss_qvm *q, qa_actor_id actor, qa_item_id item, double count, qa_qvm_inventory_words *out) {
  ss_qvm_projection projection; memset(&projection, 0, sizeof projection);
  projection.q = q; projection.actor = actor;
  if (!qa_session_actor_registry_resolve_owned(q->sim->actors, actor, &projection.owner) || q->inventory == NULL)
    ss_fail("Original inventory projection has no current source owner");
  ss_qvm_client unused = {q, projection.owner};
  qa_qvm_inventory_options options = ss_qvm_inventory_options(q, &unused, ss_qvm_projection_client);
  options.ctx = &projection;
  if (!qa_qvm_inventory_projection(options, item, count, out)) ss_fail("Out of memory");
}
/* QvmCombatBindings source (3414-3424). */
static bool ss_qvm_combat_slot(void *ctx, qa_actor_id actor, double *out) { return qa_app_q3_guest_records_slot(ss_qvm_records(ctx), actor, out); }
static bool ss_qvm_source_actor(void *ctx, double slot, qa_owned_actor *out) {
  ss_qvm *q = ctx; qa_actor_id reference;
  return qa_app_q3_guest_records_reference(ss_qvm_records(q), slot, &reference) && qa_session_actor_registry_resolve_owned(q->sim->actors, reference, out);
}
static void ss_qvm_provenance(void *ctx, const qa_actor_id *attacker, const qa_actor_id *inflictor, qa_actor_id target, qa_attack_provenance *out) {
  (void)attacker; (void)inflictor; (void)target;
  ss_qvm *q = ctx; qa_app_shared_simulation *sim = q->sim;
  out->sequence = sim->attack_sequence++;
  out->time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, ss_qvm_now(sim)};
  out->weapon = NULL;
  out->weapon_provider = sim->recipe.map.entities.provider; out->combat_provider = sim->recipe.combat.provider;
  out->inventory_provider = sim->recipe.inventory.provider; out->movement_provider = sim->recipe.movement.provider;
}
static void ss_qvm_after_free(void *ctx, double pointer, qa_qvm_module_function_call *call) {
  ss_qvm *q = ctx; if (q->pickups) qa_qvm_primary_pickups_after_free(q->pickups, pointer, call);
}
/* QvmPrimaryPickups options (3426-3449). */
static const qa_qvm_catalog_records *ss_qvm_catalog_records(void *ctx) {
  ss_qvm *q = ctx;
  qa_qvm_catalog_records_destroy(&q->records);
  size_t count = 0;
  const qa_q3_guest_qvm_catalog_record *records = qa_q3_guest_catalog_records(q->catalog, &count);
  q->records.entries = calloc(count ? count : 1, sizeof *q->records.entries);
  if (!q->records.entries) ss_fail("Out of memory");
  for (size_t i = 0; i < count; i++) {
    q->records.entries[i].class_name = strdup(records[i].class_name ? records[i].class_name : "");
    q->records.entries[i].pickup_name = strdup(records[i].pickup_name ? records[i].pickup_name : "");
    q->records.entries[i].type = records[i].type; q->records.entries[i].tag = records[i].tag;
    q->records.entries[i].index = records[i].index; q->records.entries[i].address = records[i].address;
  }
  q->records.count = count;
  return &q->records;
}
static bool ss_qvm_pickup_actor(void *ctx, double slot, qa_owned_actor *out) {
  ss_qvm *q = ctx;
  if (qa_app_q3_guest_records_is_input_retired(ss_qvm_records(q), slot)) return false;
  return qa_session_actor_registry_at_source(q->sim->actors, q->sim->recipe.map.entities.provider, (int64_t)slot, out);
}
static bool ss_qvm_pickup_current(void *ctx, qa_owned_actor actor, double slot) {
  ss_qvm *q = ctx; qa_owned_actor resolved; double current;
  return qa_session_actor_registry_resolve_owned(q->sim->actors, actor.id, &resolved) && resolved.token == actor.token
      && qa_app_q3_guest_records_slot(ss_qvm_records(q), actor.id, &current) && current == slot
      && !qa_app_q3_guest_records_is_input_retired(ss_qvm_records(q), slot);
}
static const qa_q3_guest_weapon *ss_qvm_weapon_with_tag(ss_qvm *q, double tag) {
  size_t count = 0;
  const qa_q3_guest_weapon *weapons = qa_q3_guest_catalog_weapons(q->catalog, &count);
  for (size_t i = 0; i < count; i++) if (weapons[i].weapon == tag) return &weapons[i];
  return NULL;
}
static qa_qvm_pickup_resolved_item ss_qvm_resolve_item(void *ctx, const qa_qvm_catalog_record *record) {
  ss_qvm *q = ctx; qa_app_shared_simulation *sim = q->sim;
  const qa_qvm_pickup_profile *profile = qa_app_prepared_q3_primary_pickups(q->guest->prepared);
  const qa_q3_guest_weapon *weapon = ss_qvm_weapon_with_tag(q, record->tag);
  char fallback[256]; snprintf(fallback, sizeof fallback, "q3:%s", record->class_name);
  qa_item_id item = record->type == profile->items.weapon_type ? (weapon ? weapon->item : NULL)
      : record->type == profile->items.ammo_type ? (weapon ? weapon->ammo : NULL) : NULL;
  if (item == NULL) item = qa_app_shared_sim_intern(sim, fallback);
  qa_qvm_pickup_resolved_item resolved = {item, NULL};
  qa_pickup_resource *resource = NULL;
  if (record->type == QA_Q3_BASE_IT_ARMOR) {
    resource = qa_app_shared_sim_keep(sim, sizeof *resource); resource->kind = QA_PICKUP_RESOURCE_PROTECTION; resource->channel = QA_PROTECTION_REGULAR;
  } else if (record->type == profile->items.weapon_type || record->type == profile->items.ammo_type) {
    resource = qa_app_shared_sim_keep(sim, sizeof *resource); resource->kind = QA_PICKUP_RESOURCE_INVENTORY; resource->item = item;
  }
  resolved.resource = resource;
  return resolved;
}
static qa_source_time ss_qvm_pickup_time(void *ctx) { return (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, ss_qvm_now(ss_qvm_of(ctx)->sim)}; }
static bool ss_qvm_supply_ammo(void *ctx, const qa_qvm_catalog_record *record, qa_item_id *out) {
  const qa_q3_guest_weapon *weapon = ss_qvm_weapon_with_tag(ctx, record->tag);
  if (weapon == NULL || weapon->ammo == NULL) return false;
  *out = weapon->ammo; return true;
}
static bool ss_qvm_supply_owns(void *ctx, qa_owned_actor actor, qa_item_id item) {
  qa_app_shared_simulation *sim = ss_qvm_of(ctx)->sim;
  if (sim->selected_supply) return qa_shared_pickup_admission_owns(sim->selected_supply, actor.id, item);
  return qa_shared_inventory_table_count(sim->inventory, actor.id, item) > 0;
}
static void ss_qvm_supply_project(void *ctx, qa_owned_actor actor, qa_item_id item, double count, qa_qvm_inventory_words *out) {
  ss_qvm_project_inventory(ctx, actor.id, item, count, out);
}
typedef struct ss_qvm_run { qa_qvm_pickup_execute_fn execute; void *execute_ctx; int32_t result; } ss_qvm_run;
static void *ss_qvm_run_source_execute(void *ctx, qa_source_pickup_selection selection, qa_source_pickup_lifetime lifetime) {
  (void)lifetime;
  ss_qvm_run *run = ctx; run->result = run->execute(run->execute_ctx, selection);
  return run;
}
static int32_t ss_qvm_run_source(void *ctx, qa_original_pickup_offer offer, qa_qvm_pickup_execute_fn execute, void *execute_ctx) {
  ss_qvm_run run = {execute, execute_ctx, 0};
  (void)qa_shared_original_pickup_admission_run_source(ss_qvm_of(ctx)->sim->original_pickups, offer, ss_qvm_run_source_execute, &run);
  return run.result;
}
static void ss_qvm_retire(void *ctx, qa_owned_actor actor) {
  qa_app_shared_simulation *sim = ss_qvm_of(ctx)->sim; qa_owned_actor resolved;
  if (qa_session_actor_registry_resolve_owned(sim->actors, actor.id, &resolved) && resolved.token == actor.token)
    qa_session_actor_registry_release(sim->actors, actor);
}
/* QvmPrimaryWeapons services (3452-3495). */
static bool ss_qvm_weapon_actor(void *ctx, double slot, qa_actor_id *out) { return qa_app_q3_guest_records_reference(ss_qvm_records(ctx), slot, out); }
static bool ss_qvm_weapon_selected(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ss_qvm_of(ctx)->sim;
  if (sim->has_selected_arsenal) return false;
  qa_app_sim_weapon_slot **slot = qa_ss_map_get(&sim->weapon_slots, &actor);
  return slot == NULL || qa_app_sim_weapon_slot_primary_selected(*slot);
}
static void ss_qvm_body_current_actor(void *ctx) { ss_qvm *q = ctx; qa_session_actor_registry_assert_owned(q->sim->actors, q->body_owner); }
static bool ss_qvm_equipment_movement(void *ctx, qa_actor_id actor, qa_qvm_equipment_motion *out) {
  ss_qvm *q = ctx; qa_app_shared_simulation *sim = q->sim;
  qa_app_q3_selected_source *source = sim->selected_q3_source;
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(sim->actors, actor, &owner)) return false;
  memset(out, 0, sizeof *out);
  qa_body_state body;
  if (qa_app_shared_sim_body(sim, actor, &body)) {
    memset(&q->body, 0, sizeof q->body);
    q->body.current = body.bounds;
    if (qa_app_shared_sim_active_client_outputs(sim, actor, &q->outputs) && q->outputs.body_bounds != NULL) {
      q->body.has_requested = true; q->body.requested = *q->outputs.body_bounds;
    }
    q->body_owner = owner; q->body.ctx = q; q->body.current_actor = ss_qvm_body_current_actor;
    out->body = &q->body;
  }
  if (source == NULL || !qa_app_q3_selected_source_owns_equipment(source) || !sim->has_selected_arsenal
      || !qa_ss_is(sim->selected_arsenal.family, "q3") || !qa_app_q3_selected_arsenal_has(sim->selected_arsenal.q3, actor)
      || q->weapons == NULL || !qa_qvm_primary_weapons_available(q->weapons, actor, false)) {
    out->speed_multiplier = 1; out->pose = NULL; out->owns_holdable_input = false;
    return true;
  }
  qa_game_family character = qa_app_sim_provider_family(sim->recipe.character.definition.provider);
  qa_q3_postures postures = qa_app_sim_player_postures(character, qa_app_sim_player_standing_bounds(character));
  out->pose = qa_app_q3_selected_source_fixed_pose(source, owner, postures, &q->pose) ? &q->pose : NULL;
  double haste = qa_qvm_primary_weapons_powerup_until(q->weapons, actor, QA_QVM_PRIMARY_POWERUP_HASTE);
  out->speed_multiplier = qa_app_q3_selected_source_speed_multiplier(source, actor) / (haste == 0 ? 1 : 1.3);
  out->owns_holdable_input = qa_app_q3_selected_arsenal_owns_holdable_input(sim->selected_arsenal.q3, actor);
  return true;
}
static void ss_qvm_attempted(void *ctx, qa_actor_id actor, double weapon) {
  qa_app_shared_simulation *sim = ss_qvm_of(ctx)->sim;
  double *requested = qa_ss_map_get(&sim->native_weapon_requests, &actor);
  if (requested == NULL || *requested != weapon) qa_ss_map_delete(&sim->native_weapon_requests, &actor);
}
static void ss_qvm_accepted(void *ctx, qa_actor_id actor, double weapon) { qa_ss_map_set(&ss_qvm_of(ctx)->sim->native_weapon_requests, &actor, &weapon); }
static void ss_qvm_give(void *ctx, qa_actor_id actor, const char *category) { (void)qa_app_shared_sim_grant_selected_arsenal(ss_qvm_of(ctx)->sim, actor, category); }
static bool ss_qvm_give_item(void *ctx, qa_actor_id actor, const char *name) { const char *args[1] = {name}; return qa_app_shared_sim_give_selected_item(ss_qvm_of(ctx)->sim, actor, args, 1); }
static bool ss_qvm_drop(void *ctx, qa_actor_id actor, qa_qvm_primary_weapon_drop_result *out) {
  ss_qvm *q = ctx; qa_app_shared_simulation *sim = q->sim;
  if (!sim->has_selected_arsenal || !qa_app_shared_sim_selected_arsenal_has(sim, actor)) return false;
  memset(out, 0, sizeof *out); out->has_ammo_words = true;
  qa_app_sim_player_ui ui = qa_app_shared_sim_selected_arsenal_ui(sim, actor);
  if (ui.active_weapon == NULL) return true;
  const qa_pickup_supply_profile *supply = sim->selected_original_supply;
  if (supply == NULL) ss_fail("Selected original weapons lost their admitted supply");
  size_t catalog_count = 0, weapon_count = 0, source_count = 0;
  const qa_selected_pickup_weapon *catalog = qa_app_shared_sim_selected_arsenal_catalog(sim, &catalog_count);
  const qa_q3_guest_weapon *weapons = qa_q3_guest_catalog_weapons(q->catalog, &weapon_count);
  qa_pickup_weapon *original = calloc(weapon_count + 1, sizeof *original);
  if (!original) ss_fail("Out of memory");
  for (size_t i = 0; i < weapon_count; i++) { original[i].item = weapons[i].item; original[i].ammo = weapons[i].ammo; }
  qa_weapon_source_entry *sources = qa_selected_weapon_sources(*supply, catalog, catalog_count, original, weapon_count, &source_count);
  free(original);
  qa_item_id item = NULL;
  for (size_t i = 0; i < source_count; i++) if (qa_ss_is(sources[i].item, ui.active_weapon)) item = sources[i].source;
  free(sources);
  if (item == NULL) return true;
  const qa_q3_guest_weapon *source_weapon = NULL;
  for (size_t i = 0; i < weapon_count; i++) if (qa_ss_is(weapons[i].item, item)) { source_weapon = &weapons[i]; break; }
  if (source_weapon == NULL) ss_fail("Selected death drop has no admitted source weapon for %s", ui.active_weapon);
  out->weapon = source_weapon->weapon; out->ammo = ui.has_ammo ? ui.ammo_count : 0;
  if (source_weapon->ammo != NULL) {
    qa_qvm_inventory_words words;
    ss_qvm_project_inventory(q, actor, source_weapon->ammo, ui.has_ammo ? ui.ammo_count : 0, &words);
    qa_qvm_inventory_word *kept = qa_app_shared_sim_keep(sim, (words.count ? words.count : 1) * sizeof *kept);
    memcpy(kept, words.entries, words.count * sizeof *kept);
    out->ammo_words = kept; out->ammo_word_count = words.count;
    qa_qvm_inventory_words_destroy(&words);
  }
  return true;
}
static void ss_qvm_completed(void *ctx, qa_actor_id actor, bool reached) {
  qa_app_shared_simulation *sim = ss_qvm_of(ctx)->sim;
  if (sim->qvm_weapon_decision_count != 0) {
    qa_ss_qvm_weapon_decision *decision = &sim->qvm_weapon_decisions[sim->qvm_weapon_decision_count - 1];
    if (qa_same_actor(decision->application->identity.actor, actor)) decision->reached = reached;
  }
  qa_app_sim_weapon_slot **slot = qa_ss_map_get(&sim->weapon_slots, &actor);
  if (slot) qa_app_sim_weapon_slot_reconcile(*slot);
}
/* restoreCheckpoint resolveClient (3488-3492). */
static qa_client_id ss_qvm_resolve_client(void *ctx, qa_app_q3_saved_guest_client_id saved) {
  qa_app_shared_simulation *sim = ss_qvm_of(ctx)->sim;
  for (size_t i = 0; i < sim->options.restored_client_count; i++) {
    qa_client_id client = sim->options.restored_clients[i];
    if ((double)client.slot != saved.slot) continue;
    if (!qa_identity_owner_owns_client(&sim->options.identity, client)) break;
    return client;
  }
  ss_fail("Saved Q3 guest client has no mapped session identity");
}
void qa_app_shared_sim_create_q3_qvm_source(qa_app_shared_simulation *sim, qa_ss_source_runtime *out) {
  const qa_executable_recipe *recipe = &sim->recipe;
  const qa_app_sim_q3_guest_options *guest = sim->options.q3_guest;
  const qa_app_prepared_q3_game *prepared = guest->prepared;
  ss_qvm *q = qa_app_shared_sim_keep(sim, sizeof *q);
  q->sim = sim; q->guest = guest;
  /* Without a qualified primary weapon interface the guest runs as a black box: its
   * own weapons stay silent while a foreign weapon is active. */
  char *map_name = qa_ss_map_name(recipe->map.geometry.requested_path);
  qa_app_q3_server_state_options state_options; memset(&state_options, 0, sizeof state_options);
  state_options.session = qa_session_actor_registry_session(sim->actors);
  state_options.ctx = q; state_options.now = ss_qvm_state_now; state_options.print = ss_qvm_print;
  state_options.filter_user_command = ss_qvm_filter_command;
  state_options.settings.game_type = guest->has_game_type ? guest->game_type
      : qa_ss_is(recipe->match.provider, "q3:ctf") ? QA_Q3_BASE_GT_CTF : qa_ss_is(sim->options.mode, "singleplayer") ? 2 : 0;
  state_options.settings.single_player = qa_ss_is(sim->options.mode, "singleplayer");
  state_options.settings.max_clients = sim->options.max_clients;
  state_options.settings.map_name = qa_app_shared_sim_intern(sim, map_name);
  free(map_name);
  if (!sim->options.restore && sim->options.source_registry) state_options.settings.source_registry = sim->options.source_registry;
  if (!sim->options.restore && sim->options.source_archive) {
    state_options.settings.source_archive = sim->options.source_archive; state_options.settings.source_archive_count = sim->options.source_archive_count;
  }
  state_options.settings.cvars = (const qa_app_q3_host_cvar *)sim->options.q3_cvars; state_options.settings.cvar_count = sim->options.q3_cvar_count;
  qa_app_q3_server_state *state = qa_app_q3_server_state_create(state_options);
  if (!state) ss_fail("Out of memory");
  out->qvm_state = state;
  qa_cvar_registry *cvars = qa_app_q3_server_state_cvars(state);
  ss_cvar_set(cvars, "fs_game", guest->game_directory);
  qa_app_shared_sim_initialize_server_settings(sim, cvars);
  q->inventory = qa_app_prepared_q3_primary_inventory(prepared);
  qa_app_q3_qvm_server_game_options options; memset(&options, 0, sizeof options);
  /* module: guest.prepared.kind === "qvm" ? { kind: "qvm", artifact } : { kind: "native", image }. */
  options.module.kind = prepared->kind;
  if (strcmp(prepared->kind, "qvm") == 0) options.module.artifact = &prepared->artifact;
  else options.module.image = &prepared->image.image;
  options.state = state;
  options.records.actors = sim->actors; options.records.bodies = ss_bodies(sim); options.records.scene = sim->scene;
  options.records.provider = recipe->map.entities.provider; options.records.ctx = q;
  options.records.admit = ss_qvm_admit; options.records.collision = ss_qvm_collision;
  options.mounts = (qa_content_mounted *)sim->options.mounts; options.writable = guest->writable; options.common = guest->common;
  options.max_clients = sim->options.max_clients; options.seed = sim->options.seed;
  options.has_dedicated = true; options.dedicated = sim->options.dedicated;
  options.entity_text = qa_app_shared_sim_guest_monster_entities(sim, sim->world_entities, qa_app_shared_sim_guest_map_edition(sim));
  options.ctx = q; options.now = ss_qvm_game_now; options.assert_current = ss_qvm_assert_current;
  options.client_changed = ss_qvm_client_changed; options.before_retire = ss_qvm_before_retire;
  options.source_restored = ss_qvm_source_restored;
  if (q->inventory != NULL) options.arsenal = ss_qvm_arsenal;
  options.bot_command = ss_qvm_bot_command; options.before_disconnect = ss_qvm_before_disconnect;
  qa_app_q3_qvm_server_game *game = qa_app_q3_qvm_server_game_create(options);
  if (!game) ss_fail("Out of memory");
  q->game = game;
  out->kind = QA_SS_SOURCE_Q3_QVM; out->qvm = game;
  /* new Q3GuestCatalog(prepared.kind === "qvm" ? { artifact, module: game.qvmGame().module } : null,
   * items ?? primary.pickups?.items, weapons, selection, nativeInventory). */
  bool interpreted = strcmp(prepared->kind, "qvm") == 0;
  qa_qvm_game *qvm = interpreted ? qa_app_q3_qvm_server_game_game(game) : NULL;
  qa_q3_guest_qvm_module module; memset(&module, 0, sizeof module);
  if (qvm != NULL) { module.profile_module = qvm->module.profile.module; module.memory_bytes = qvm->module.memory->bytes; module.memory_length = qvm->module.memory->length; }
  const qa_qvm_pickup_profile *pickups = qa_app_prepared_q3_primary_pickups(prepared);
  qa_q3_guest_qvm_item_layout layout; bool has_layout = qa_app_prepared_q3_guest_layout(prepared, &layout);
  if (!has_layout && pickups != NULL) {
    qa_app_prepared_q3_game declared = *prepared; declared.has_items = true; declared.items = pickups->items;
    has_layout = qa_app_prepared_q3_guest_layout(&declared, &layout);
  }
  const qa_qvm_primary_weapon_profile *primary_weapons = qa_app_prepared_q3_primary_weapons(prepared);
  qa_q3_guest_selection *selection = NULL; size_t selection_count = 0;
  if (primary_weapons) {
    selection_count = primary_weapons->stage.selection.value_count;
    selection = qa_app_shared_sim_keep(sim, (selection_count ? selection_count : 1) * sizeof *selection);
    for (size_t i = 0; i < selection_count; i++) {
      selection[i].value = primary_weapons->stage.selection.values[i].value; selection[i].item = primary_weapons->stage.selection.values[i].item;
    }
  }
  qa_q3_guest_qvm_inventory_profile inventory_view; memset(&inventory_view, 0, sizeof inventory_view);
  if (q->inventory && q->inventory->private_profile) {
    inventory_view.has_storage = true;
    inventory_view.storage = q->inventory->private_profile->storage; inventory_view.storage_count = q->inventory->private_profile->storage_count;
  }
  q->catalog = qa_q3_guest_catalog_create(qa_app_prepared_q3_equipment_artifact(prepared), module, has_layout ? &layout : NULL,
      prepared->weapons.values, prepared->weapons.count, primary_weapons != NULL, selection, selection_count,
      q->inventory ? &inventory_view : NULL);
  if (!q->catalog) ss_fail("Out of memory");
  out->qvm_catalog = q->catalog;
  const qa_qvm_primary_combat_profile *native_combat = qa_app_prepared_q3_primary_combat(prepared);
  if (native_combat != NULL) {
    q->combat_source.actors = sim->actors; q->combat_source.ctx = q; q->combat_source.actor = ss_qvm_source_actor;
    q->combat_source.provenance = ss_qvm_provenance; q->combat_source.after_free = ss_qvm_after_free;
    qa_qvm_combat_bindings_options combat; memset(&combat, 0, sizeof combat);
    combat.game = qvm; combat.artifact = &prepared->artifact; combat.definition = *native_combat;
    combat.bodies = ss_bodies(sim); combat.combat = sim->combat; combat.source = &q->combat_source;
    combat.ctx = q; combat.slot = ss_qvm_combat_slot;
    q->combat = qa_qvm_combat_bindings_create(combat);
    if (!q->combat) ss_fail("Out of memory");
  }
  out->qvm_combat = q->combat;
  out->qvm_inventory = q->inventory;
  if (pickups != NULL) {
    qa_qvm_primary_pickups_options pick; memset(&pick, 0, sizeof pick);
    pick.game = qvm; pick.artifact = &prepared->artifact; pick.profile = *pickups;
    pick.has_catalog = true; pick.catalog_ctx = q; pick.catalog_records = ss_qvm_catalog_records;
    pick.ctx = q; pick.actor = ss_qvm_pickup_actor; pick.current = ss_qvm_pickup_current; pick.resolve_item = ss_qvm_resolve_item;
    pick.time = ss_qvm_pickup_time;
    pick.has_supply = true; pick.supply_ctx = q; pick.supply_ammo = ss_qvm_supply_ammo; pick.supply_owns = ss_qvm_supply_owns;
    pick.supply_project = ss_qvm_supply_project;
    pick.run_source = ss_qvm_run_source;
    pick.own_free_hook = q->combat == NULL; pick.retire = ss_qvm_retire;
    q->pickups = qa_qvm_primary_pickups_create(pick);
    if (!q->pickups) ss_fail("Out of memory");
  }
  out->qvm_pickups = q->pickups;
  if (primary_weapons != NULL) {
    qa_qvm_primary_weapon_services services; memset(&services, 0, sizeof services);
    services.ctx = q; services.actor = ss_qvm_weapon_actor; services.slot = ss_qvm_combat_slot; services.selected = ss_qvm_weapon_selected;
    services.has_equipment_movement = true; services.equipment_movement = ss_qvm_equipment_movement;
    services.attempted = ss_qvm_attempted; services.accepted = ss_qvm_accepted; services.give = ss_qvm_give;
    services.give_item = ss_qvm_give_item; services.drop = ss_qvm_drop; services.completed = ss_qvm_completed;
    q->weapons = qa_qvm_primary_weapons_create(qvm, &prepared->artifact, *primary_weapons, services);
    if (!q->weapons) ss_fail("Out of memory");
  }
  out->qvm_weapons = q->weapons;
  if (sim->options.restore) {
    const qa_guest_checkpoint *checkpoint = qa_app_sim_simulation_q3_guest_checkpoint(*sim->options.restore);
    if (checkpoint == NULL) ss_fail("Saved Q3 guest memory is missing");
    qa_app_q3_qvm_server_game_restore_checkpoint(game, checkpoint, ss_qvm_resolve_client, q);
  }
  q->current = game;
}

/* Constructor 791-805: game.bindInput(definition, { applications, clientOutputs, identity, live,
 * accepted, frame, spawned, movement, onRelease }). */
static bool ss_input_client_outputs(void *ctx, qa_actor_id actor, qa_mod_client_movement_outputs *out) {
  return qa_app_shared_sim_active_client_outputs(ctx, actor, out);
}
static bool ss_input_identity(void *ctx, double slot, qa_mod_client_identity *out) {
  qa_app_shared_simulation *sim = ctx;
  qa_app_q3_player players[64];
  size_t count = qa_app_q3_qvm_server_game_players(sim->source.qvm, players, 64);
  for (size_t i = 0; i < count && i < 64; i++)
    if (players[i].source_entity == slot) { out->client = players[i].client; out->actor = players[i].actor; return true; }
  return false;
}
static bool ss_input_live(void *ctx, qa_mod_client_identity identity) {
  qa_app_shared_simulation *sim = ctx; qa_client_id client;
  return qa_session_actor_registry_is_live(sim->actors, identity.actor)
      && qa_app_shared_sim_player_client(sim, identity.actor, &client) && qa_same_client(client, identity.client);
}
static const qa_mod_client_command_receipt *ss_input_accepted(void *ctx, qa_actor_id actor) {
  return qa_ss_map_get(&((qa_app_shared_simulation *)ctx)->mod_client_commands, &actor);
}
static qa_frame_context ss_input_frame(void *ctx) { return ((qa_app_shared_simulation *)ctx)->source_frame; }
static void ss_input_spawned(void *ctx, qa_mod_client_identity identity) { qa_app_shared_sim_qvm_client_spawned(ctx, identity.actor); }
static int32_t ss_input_movement(void *ctx, qa_qvm_module_function_call *call, const char *kind, qa_qvm_input_run_fn run, void *run_ctx) {
  qa_app_shared_simulation *sim = ctx;
  if ((sim->strategy->as_q3_qvm(sim) != NULL) && sim->source.qvm_weapons != NULL)
    return qa_qvm_primary_weapons_equipment_movement(sim->source.qvm_weapons, call, kind, run, run_ctx);
  return run(run_ctx);
}
typedef struct ss_input_release { void (*listener)(void *ctx, qa_actor_id actor); void *listener_ctx; } ss_input_release;
static void ss_input_released(void *ctx, qa_owned_actor actor) { ss_input_release *r = ctx; r->listener(r->listener_ctx, actor.id); }
typedef struct ss_input_unsubscribe { qa_session_actor_registry *actors; int64_t id; } ss_input_unsubscribe;
static void ss_input_unsubscribe_run(void *ctx) { ss_input_unsubscribe *u = ctx; qa_session_actor_registry_off_release(u->actors, u->id); }
static qa_mod_client_unsubscribe ss_input_on_release(void *ctx, void (*listener)(void *ctx, qa_actor_id actor), void *listener_ctx) {
  qa_app_shared_simulation *sim = ctx;
  ss_input_release *release = qa_app_shared_sim_keep(sim, sizeof *release);
  release->listener = listener; release->listener_ctx = listener_ctx;
  ss_input_unsubscribe *unsubscribe = qa_app_shared_sim_keep(sim, sizeof *unsubscribe);
  unsubscribe->actors = sim->actors;
  unsubscribe->id = qa_session_actor_registry_on_release(sim->actors, ss_input_released, release);
  return (qa_mod_client_unsubscribe){unsubscribe, ss_input_unsubscribe_run};
}
void qa_app_shared_sim_bind_qvm_input(qa_app_shared_simulation *sim) {
  if ((sim->strategy->as_q3_qvm(sim) == NULL)) ss_fail("QVM input requires the Q3 guest source");
  const qa_qvm_input_definition *definition = qa_app_prepared_q3_primary_input(sim->options.q3_guest->prepared);
  if (definition == NULL) return;
  qa_qvm_input_services services; memset(&services, 0, sizeof services);
  services.ctx = sim; services.applications = sim->mod_client_applications;
  services.client_outputs = ss_input_client_outputs; services.identity = ss_input_identity; services.live = ss_input_live;
  services.accepted = ss_input_accepted; services.frame = ss_input_frame; services.spawned = ss_input_spawned;
  services.movement = ss_input_movement; services.on_release = ss_input_on_release;
  qa_app_q3_qvm_server_game_bind_input(sim->source.qvm, definition, services);
}
