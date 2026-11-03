/* Port of src/app/bootstrap/simulation/runtime.ts, SharedSimulation frame
 * pipeline: beginQvmSelectedFrame / runQvmSelectedActors (5101-5140),
 * stepAsync (5142-5214), step (5216-5684), currentOutput / snapshot
 * (5686-5695) and the native Q2 step (7257-7333). One pipeline for every
 * source kind, dispatching per source the way TS does.
 * GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qa/app/bootstrap/simulation/classic_guest_player.h"
#include "qa/app/bootstrap/simulation/player_movement.h"
#include "qa/app/bootstrap/simulation/provider_frames.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_player.h"
#include "qa/app/bootstrap/simulation/runtime.h"
#include "qa/app/bootstrap/simulation/q3/guest_artifact.h"
#include "qa/compat/qvm/game.h"
#include "qa/content/q2/foundation/shadow_lights.h"
#include "qa/content/q3/equipment/grapple_profiles.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/adapters.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/world/actors/body.h"

static noreturn void ss_fail(const char *format, ...) {
  char message[1024];
  va_list args; va_start(args, format); vsnprintf(message, sizeof message, format, args); va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static double ss_time_seconds(const qa_app_shared_simulation *sim) { return qa_ss_seconds(sim->source_frame.time); }
static qa_shared_body_table *ss_bodies(qa_app_shared_simulation *sim) { return qa_app_sim_shared_physics_bodies(sim->physics); }
static double ss_health(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  return combat.has_value ? combat.value.health : 0;
}
static bool ss_live(qa_app_shared_simulation *sim, qa_actor_id actor) { return qa_session_actor_registry_is_live(sim->actors, actor); }

/* ---- UserCommand member access (the TS union reads) ---- */
static double ss_buttons(const qa_user_command *command) {
  switch (command->kind) {
  case QA_USER_COMMAND_Q1: return command->as.q1.buttons;
  case QA_USER_COMMAND_QW: return command->as.qw.buttons;
  case QA_USER_COMMAND_Q2: return command->as.q2.buttons;
  case QA_USER_COMMAND_Q2_RERELEASE: return command->as.q2_rerelease.buttons;
  case QA_USER_COMMAND_Q3: return command->as.q3.buttons;
  }
  return 0;
}
static void ss_set_buttons(qa_user_command *command, double buttons) {
  switch (command->kind) {
  case QA_USER_COMMAND_Q1: command->as.q1.buttons = buttons; break;
  case QA_USER_COMMAND_QW: command->as.qw.buttons = buttons; break;
  case QA_USER_COMMAND_Q2: command->as.q2.buttons = buttons; break;
  case QA_USER_COMMAND_Q2_RERELEASE: command->as.q2_rerelease.buttons = buttons; break;
  case QA_USER_COMMAND_Q3: command->as.q3.buttons = buttons; break;
  }
}
/* "upMove" in command ? command.upMove : undefined (q2-rerelease has none). */
static bool ss_up_move(const qa_user_command *command, double *out) {
  switch (command->kind) {
  case QA_USER_COMMAND_Q1: *out = command->as.q1.up_move; return true;
  case QA_USER_COMMAND_QW: *out = command->as.qw.up_move; return true;
  case QA_USER_COMMAND_Q2: *out = command->as.q2.up_move; return true;
  case QA_USER_COMMAND_Q3: *out = command->as.q3.up_move; return true;
  case QA_USER_COMMAND_Q2_RERELEASE: return false;
  }
  return false;
}
static bool ss_up_positive(const qa_user_command *command) { double up = 0; return ss_up_move(command, &up) && up > 0; }
/* "milliseconds" in command ? command.milliseconds : undefined. */
static bool ss_command_milliseconds(const qa_user_command *command, double *out) {
  switch (command->kind) {
  case QA_USER_COMMAND_QW: *out = command->as.qw.milliseconds; return true;
  case QA_USER_COMMAND_Q2: *out = command->as.q2.milliseconds; return true;
  case QA_USER_COMMAND_Q2_RERELEASE: *out = command->as.q2_rerelease.milliseconds; return true;
  default: return false;
  }
}
/* The jump bit of each command dialect: q1 buttons & 2, rerelease buttons & 8, else upMove > 0. */
static bool ss_command_jump(const qa_user_command *command) {
  if (command->kind == QA_USER_COMMAND_Q1 || command->kind == QA_USER_COMMAND_QW) return (qa_js_to_int32(ss_buttons(command)) & 2) != 0;
  if (command->kind == QA_USER_COMMAND_Q2_RERELEASE) return (qa_js_to_int32(command->as.q2_rerelease.buttons) & 8) != 0;
  double up = 0; return ss_up_move(command, &up) && up > 0;
}
static double ss_mask_attack(double buttons) { return (double)(qa_js_to_int32(buttons) & ~1); }

/* orderedActorTurns(this.actors, actor => this.sourcePosition(actor.id), visited) */
static void ss_turn_position(void *ctx, qa_owned_actor actor, double out[2]) {
  qa_app_shared_sim_source_position(ctx, actor.id, out);
}
static qa_app_sim_actor_execution_frame ss_execution_frame(qa_app_shared_simulation *sim, qa_frame_context frame, double time_seconds,
    double elapsed, qa_app_sim_actor_visited *visited) {
  return (qa_app_sim_actor_execution_frame){sim->actors, ss_bodies(sim), sim->physics, sim->scheduler, frame, time_seconds, elapsed, visited};
}

/* ---- beginQvmSelectedFrame (5101-5107) ---- */
void qa_app_shared_sim_begin_qvm_selected_frame(qa_app_shared_simulation *sim) {
  if (!sim->has_selected_arsenal || !qa_ss_is(sim->selected_arsenal.family, "q1")) return;
  qa_frame_context frame = qa_app_shared_sim_selected_q1_frame(sim);
  qa_app_q1_selected_arsenal *arsenal = sim->selected_arsenal.q1;
  qa_q1_begin_frame(qa_app_shared_sim_selected_q1_game(sim), qa_ss_seconds(frame.time), qa_ss_seconds(frame.elapsed));
  if (sim->has_selected_weapon_source && qa_ss_is(sim->selected_weapon_source.kind, "q1"))
    qa_comp_selected_q1_mission_weapons_frame(sim->selected_weapon_source.mission_weapons, qa_ss_seconds(frame.elapsed));
  qa_app_q1_selected_arsenal_frame(arsenal, qa_ss_seconds(frame.time));
}

/* ---- runQvmSelectedActors (5108-5140) ---- */
static int ss_double_order(const void *a, const void *b) {
  double left = *(const double *)a, right = *(const double *)b;
  return (left > right) - (left < right);
}
typedef struct ss_deadlines { double *values; size_t count, capacity; } ss_deadlines;
static void ss_deadline_add(ss_deadlines *set, double value) {
  for (size_t i = 0; i < set->count; i++) if (set->values[i] == value) return;
  if (set->count == set->capacity) {
    set->capacity = set->capacity ? set->capacity * 2 : 16;
    double *grown = realloc(set->values, set->capacity * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    set->values = grown;
  }
  set->values[set->count++] = value;
}
void qa_app_shared_sim_run_qvm_selected_actors(qa_app_shared_simulation *sim) {
  qa_ss_selected_weapon_source *q2 = sim->has_selected_weapon_source && qa_ss_is(sim->selected_weapon_source.kind, "q2")
      ? &sim->selected_weapon_source : NULL;
  bool has_q3 = sim->selected_q3_source != NULL;
  qa_clock_profile q3; memset(&q3, 0, sizeof q3);
  if (has_q3) {
    q3 = qa_app_sim_provider_timing(sim->recipe, sim->weapon_provider.provider).clock;
    if (q3.kind != QA_CLOCK_Q3) ss_fail("Selected Q3 source lost its clock");
  }
  ss_deadlines deadlines = {0};
  double now = ss_time_seconds(sim) * 1000;
  if (q2) for (double due = q2->next_milliseconds; due <= now; due += q2->interval_milliseconds) ss_deadline_add(&deadlines, due);
  if (has_q3) for (double due = sim->selected_q3_next; due <= now; due += q3.server_frame_milliseconds) ss_deadline_add(&deadlines, due);
  if (sim->has_selected_arsenal && qa_ss_is(sim->selected_arsenal.family, "q1")) ss_deadline_add(&deadlines, now);
  if (deadlines.count > 1) qsort(deadlines.values, deadlines.count, sizeof *deadlines.values, ss_double_order);
  for (size_t index = 0; index < deadlines.count; index++) {
    double due = deadlines.values[index];
    bool run_q2 = q2 && due == q2->next_milliseconds, run_q3 = has_q3 && due == sim->selected_q3_next;
    double previous_q3 = sim->selected_q3_time;
    if (run_q2) {
      bool milliseconds = q2->frame.time.kind == QA_SOURCE_TIME_MILLISECONDS;
      q2->frame = (qa_frame_context){q2->frame.frame + 1, {q2->frame.time.kind, milliseconds ? due : due / 1000},
          {q2->frame.elapsed.kind, milliseconds ? q2->interval_milliseconds : q2->interval_milliseconds / 1000}, QA_FRAME_PHASE_FRAME_ENTRY};
      q2->next_milliseconds = due + q2->interval_milliseconds; q2->map_milliseconds = due;
      if (sim->has_selected_arsenal && qa_ss_is(sim->selected_arsenal.family, "q2")) {
        size_t count = 0; const qa_actor_id *players = qa_app_shared_sim_players(sim, &count);
        qa_actor_id *copy = calloc(count ? count : 1, sizeof *copy);
        if (!copy) qa_range_error("Out of memory");
        memcpy(copy, players, count * sizeof *copy);
        for (size_t i = 0; i < count; i++)
          if (qa_app_q2_selected_arsenal_has(sim->selected_arsenal.q2, copy[i])) qa_app_q2_selected_arsenal_frame(sim->selected_arsenal.q2, copy[i]);
        free(copy);
      }
    }
    if (run_q3) {
      sim->selected_q3_time = due; sim->selected_q3_next = due + q3.server_frame_milliseconds;
      if (sim->selected_q3_source) qa_app_q3_selected_source_synchronize(sim->selected_q3_source);
    }
    qa_app_sim_actor_visited visited = {0};
    qa_app_sim_ordered_actor_turns *turns = qa_app_sim_ordered_actor_turns_create(sim->actors, ss_turn_position, sim);
    qa_owned_actor actor;
    while (qa_app_sim_ordered_actor_turns_next(turns, &visited, &actor)) {
      qa_app_sim_actor_execution *execution = qa_ss_map_get(&sim->actor_executions, &actor.id);
      qa_client_id client;
      if (execution && execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE) { if (run_q3) execution->step(execution->ctx, previous_q3, due); }
      else if (execution && execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 && run_q2
               && qa_q2_entity_services_game(execution->q2_services) == q2->q2_game) {
        qa_app_sim_actor_execution entry = *execution;
        qa_app_sim_actor_execution_frame context = ss_execution_frame(sim, q2->frame, qa_ss_seconds(q2->frame.time), qa_ss_seconds(q2->frame.elapsed), &visited);
        qa_app_sim_execute_actor(&entry, &context);
      } else if (execution && execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 && sim->has_selected_arsenal
                 && qa_ss_is(sim->selected_arsenal.family, "q1")
                 && execution->q1_services == qa_app_shared_sim_selected_q1_game(sim)
                 && !qa_app_shared_sim_player_client(sim, actor.id, &client)) {
        qa_frame_context frame = qa_app_shared_sim_selected_q1_frame(sim);
        qa_app_sim_actor_execution entry = *execution;
        qa_app_sim_actor_execution_frame context = ss_execution_frame(sim, frame, qa_ss_seconds(frame.time), qa_ss_seconds(frame.elapsed), &visited);
        qa_app_sim_execute_actor(&entry, &context);
      }
      qa_app_sim_shared_physics_commit_attachments(sim->physics);
    }
    qa_app_sim_ordered_actor_turns_destroy(turns);
    free(visited.actors);
    for (size_t i = 0; i < sim->weapon_slots.count; i++)
      qa_app_sim_weapon_slot_reconcile(*(qa_app_sim_weapon_slot **)qa_ss_map_value_at(&sim->weapon_slots, i));
  }
  free(deadlines.values);
}

/* ---- snapshot / currentOutput (5686-5695) ---- */
typedef struct ss_snapshot_storage {
  qa_body_snapshot *bodies; size_t body_capacity;
  qa_inventory_snapshot *inventories; size_t inventory_capacity;
  qa_actor_configuration *configurations; size_t configuration_capacity;
} ss_snapshot_storage;
static __thread ss_snapshot_storage ss_snapshot_scratch;
static void *ss_reserve(void *ptr, size_t *capacity, size_t count, size_t size) {
  if (count <= *capacity) return ptr;
  size_t next = count + 16;
  void *grown = realloc(ptr, next * size);
  if (!grown) qa_range_error("Out of memory");
  *capacity = next;
  return grown;
}
qa_world_snapshot qa_app_shared_sim_snapshot(qa_app_shared_simulation *sim) {
  ss_snapshot_storage *storage = &ss_snapshot_scratch;
  size_t observation_count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(sim->actors, &observation_count);
  storage->bodies = ss_reserve(storage->bodies, &storage->body_capacity, observation_count + 1, sizeof *storage->bodies);
  size_t bodies = 0;
  for (size_t i = 0; i < observation_count; i++) {
    qa_body_state body;
    if (qa_app_shared_sim_body(sim, observations[i].id, &body)) storage->bodies[bodies++] = (qa_body_snapshot){observations[i].id, body};
  }
  size_t player_count = 0; const qa_actor_id *players = qa_app_shared_sim_players(sim, &player_count);
  storage->inventories = ss_reserve(storage->inventories, &storage->inventory_capacity, player_count + 1, sizeof *storage->inventories);
  storage->configurations = ss_reserve(storage->configurations, &storage->configuration_capacity, player_count + 1, sizeof *storage->configurations);
  for (size_t i = 0; i < player_count; i++) {
    size_t count = 0; const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, players[i], &count);
    storage->inventories[i] = (qa_inventory_snapshot){players[i], entries, count};
    qa_actor_configuration configuration; memset(&configuration, 0, sizeof configuration);
    configuration.actor = players[i]; configuration.movement = sim->recipe.movement; configuration.character = sim->recipe.character;
    configuration.weapons = sim->recipe.weapons; configuration.inventory = sim->recipe.inventory;
    storage->configurations[i] = configuration;
  }
  qa_world_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot);
  snapshot.session = sim->session; snapshot.frame = sim->source_frame;
  snapshot.actors = observations; snapshot.actor_count = observation_count;
  snapshot.bodies = storage->bodies; snapshot.body_count = bodies;
  snapshot.inventories = storage->inventories; snapshot.inventory_count = player_count;
  snapshot.configurations = storage->configurations; snapshot.configuration_count = player_count;
  snapshot.scene.session = sim->session; snapshot.scene.time = sim->source_frame.time;
  static __thread qa_scene_snapshot_world world;
  world.resource = sim->recipe.map.geometry; world.geometry = sim->world;
  snapshot.scene.world = &world;
  size_t style_count = 0;
  snapshot.scene.light_styles = qa_app_shared_sim_light_styles(sim, ss_time_seconds(sim), &style_count);
  snapshot.scene.light_style_count = style_count;
  snapshot.scene.area_bits = NULL;
  return snapshot;
}
static qa_simulation_output ss_output(qa_app_shared_simulation *sim, bool take_events) {
  qa_simulation_output output; memset(&output, 0, sizeof output);
  output.snapshot = qa_app_shared_sim_snapshot(sim);
  if (take_events) output.events = qa_app_sim_simulation_events_take(sim->events, &output.event_count);
  return output;
}
qa_simulation_output qa_app_shared_sim_current_output(qa_app_shared_simulation *sim) {
  qa_app_shared_sim_assert_open(sim);
  return ss_output(sim, false);
}

/* ---- stepAsync (5142-5214) ---- */
typedef struct ss_qvm_entry { qa_app_q3_player player; qa_q3_base_user_command command; } ss_qvm_entry;
static void ss_stepping_off(void *ctx) { ((qa_app_shared_simulation *)ctx)->stepping = false; }
/* stepAsync's grapple gravity observer: guest.qvmGame().module.observeFunction({ kind: "qvm", module: definition.module,
 * instructionIndex: definition.callbacks.playerMove }, call => ...) and its remove handle. */
typedef struct ss_player_move_observer {
  qa_qvm_game *game; size_t id;
  qa_app_q3_qvm_server_game *guest; qa_app_q3_player player; double client_stride, scale;
} ss_player_move_observer;
static void ss_observe_player_move(void *ctx, qa_qvm_module_function_call *call, int64_t argument_base) {
  ss_player_move_observer *observer = ctx;
  (void)argument_base; /* the observation's argument(index) reads its own words from 0 */
  /* const movement = call.argument(0) */
  if (call->words_length < 4) qa_range_error("QVM argument index outside source call");
  const uint8_t *word = call->words;
  int32_t movement = (int32_t)((uint32_t)word[0] | ((uint32_t)word[1] << 8) | ((uint32_t)word[2] << 16) | ((uint32_t)word[3] << 24));
  /* pointer = module.memory.view(movement, 4).getInt32(0, true) */
  qa_qvm_writable_view view;
  qa_qvm_memory_view(observer->game->module.memory, movement, 4, 0, &view);
  int32_t pointer = qa_qvm_writable_view_get_int32(&view, 0, true);
  double expected = qa_qvm_game_data_checkpoint(&observer->game->data).clients_word + observer->player.source_entity * observer->client_stride;
  if (pointer != expected) ss_fail("Source Pmove does not target the admitted equipment player");
  qa_q3_player_state state; double stats[16], persistent[16], powerups[16], ammo[16];
  qa_app_q3_guest_records_player(qa_app_q3_qvm_server_game_records(observer->guest), observer->player.source_entity, &state,
      stats, persistent, powerups, ammo);
  state.gravity = trunc(state.gravity * observer->scale);
  qa_qvm_game_data_write_player_state(&observer->game->data, observer->player.source_entity, &state);
}
/* NULL when the prepared guest has no grapple profile (TS definition === null). */
static ss_player_move_observer *ss_observe_grapple_player_move(qa_app_shared_simulation *sim, qa_app_q3_qvm_server_game *guest,
    qa_app_q3_player player, double scale) {
  const qa_app_prepared_q3_game *prepared = sim->options.q3_guest ? sim->options.q3_guest->prepared : NULL;
  qa_qvm_grapple_definition definition;
  /* artifact = prepared?.kind === "qvm" ? prepared.artifact : undefined; definition = artifact === undefined ? null : ... */
  const qa_q3_equipment_qvm_artifact *artifact = prepared != NULL ? qa_app_prepared_q3_equipment_artifact(prepared) : NULL;
  if (artifact == NULL || !qa_q3_grapple_profile(artifact, &definition)) return NULL;
  ss_player_move_observer *observer = calloc(1, sizeof *observer);
  if (observer == NULL) qa_range_error("Out of memory");
  observer->game = qa_app_q3_qvm_server_game_game(guest); observer->guest = guest; observer->player = player;
  observer->client_stride = definition.client_stride; observer->scale = scale;
  qa_guest_callback_reference reference; memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM; reference.module = definition.module;
  reference.instruction_index = definition.callbacks.player_move;
  qa_cleanup node; qa_cleanup_push(&node, free, observer);
  observer->id = qa_qvm_module_observe_function(&observer->game->module, &reference, ss_observe_player_move, observer);
  qa_cleanup_pop(&node);
  return observer;
}
static void ss_remove_observer(void *ctx) {
  ss_player_move_observer *observer = ctx;
  qa_qvm_module_unbind(&observer->game->module, observer->id);
  free(observer);
}
qa_simulation_output qa_app_shared_sim_step_async(qa_app_shared_simulation *sim, qa_input_batch input,
    qa_app_shared_sim_progress_fn progress, void *progress_ctx) {
  if (sim->pending_shared_restore) qa_shared_world_restore_assert_complete(sim->pending_shared_restore);
  if (sim->strategy->step_route(sim) == QA_APP_SOURCE_STEP_NATIVE && progress != NULL) {
    qa_app_shared_sim_assert_open(sim);
    qa_app_shared_sim_assert_bot_restore_ready(sim);
    if (sim->stepping || sim->checkpoint_in_progress) ss_fail("Simulation step is already running or a checkpoint is active");
    return qa_app_shared_sim_step_native_q2_async(sim, input, progress, progress_ctx);
  }
  if (sim->strategy->as_q3_qvm(sim) == NULL) return qa_app_shared_sim_step(sim, input);
  qa_app_shared_sim_assert_open(sim);
  if (sim->stepping || sim->checkpoint_in_progress) ss_fail("Simulation step is already running or a checkpoint is active");
  if (!isfinite(input.elapsed_milliseconds) || input.elapsed_milliseconds < 0) qa_range_error("Host elapsed time must be finite and nonnegative");
  qa_app_q3_qvm_server_game *guest = sim->source.qvm;
  qa_app_q3_guest_records *records = qa_app_q3_qvm_server_game_records(guest);
  ss_qvm_entry *commands = calloc(input.command_count ? input.command_count : 1, sizeof *commands);
  qa_actor_id *seen = calloc(input.command_count ? input.command_count : 1, sizeof *seen);
  if (!commands || !seen) qa_range_error("Out of memory");
  size_t seen_count = 0;
  for (size_t i = 0; i < input.command_count; i++) {
    const qa_actor_command *command = &input.commands[i];
    if (command->source.kind != QA_COMMAND_SOURCE_LOCAL_SEAT || (command->arsenal != NULL && !sim->has_selected_arsenal)
        || !qa_identity_owner_owns_seat(&sim->options.identity, command->source.seat)
        || !qa_app_shared_sim_owns_client(sim, command->source.client))
      ss_fail("Q3 guest local input requires an owned seat");
    qa_app_q3_player player;
    bool found = qa_app_q3_qvm_server_game_player(guest, command->source.client, &player);
    bool duplicate = false;
    for (size_t j = 0; j < seen_count && found; j++) if (qa_same_actor(seen[j], player.actor)) duplicate = true;
    if (!found || !qa_same_actor(player.actor, command->actor) || duplicate)
      ss_fail("Q3 guest local input must target its admitted player once per frame");
    seen[seen_count++] = player.actor;
    qa_app_shared_sim_observe_client_command(sim, command);
    qa_app_shared_sim_require_equipment_player(sim, player.actor);
    if (sim->grapple) qa_app_sim_grapple_runtime_set_jump(sim->grapple, player.actor,
        command->command.kind == QA_USER_COMMAND_Q2_RERELEASE ? (qa_js_to_int32(command->command.as.q2_rerelease.buttons) & 8) != 0
        : ss_up_positive(&command->command));
    qa_app_sim_weapon_slot **slot = qa_ss_map_get(&sim->weapon_slots, &player.actor);
    if (sim->grapple && qa_ss_is(qa_app_sim_grapple_runtime_selection(sim->grapple)->binding, "slot"))
      qa_app_sim_grapple_runtime_input(sim->grapple, player.actor,
          slot && qa_app_sim_weapon_slot_equipment_selected(*slot) && (qa_js_to_int32(ss_buttons(&command->command)) & 1) != 0);
    double *requested = qa_ss_map_get(&sim->native_weapon_requests, &player.actor);
    qa_q3_wire_user_command wire = command->command.kind == QA_USER_COMMAND_Q3
        ? qa_q3_from_user_command(&command->command.as.q3)
        : qa_app_sim_wire_q3_user_command(command->command, trunc(ss_time_seconds(sim) * 1000), requested ? *requested : 0);
    qa_q3_base_user_command native = {wire.server_time, {wire.angles[0], wire.angles[1], wire.angles[2]}, wire.buttons, wire.weapon,
                                      wire.forwardmove, wire.rightmove, wire.upmove};
    if (!sim->has_selected_arsenal && slot && !qa_app_sim_weapon_slot_primary_selected(*slot)) native.buttons = ss_mask_attack(native.buttons);
    if (requested) native.weapon = *requested;
    commands[i] = (ss_qvm_entry){player, native};
  }
  qa_clock_profile profile = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider).clock;
  if (profile.kind != QA_CLOCK_Q3) ss_fail("Q3 guest lost its native frame clock");
  sim->stepping = true;
  qa_cleanup stepping; qa_cleanup_push(&stepping, ss_stepping_off, sim);
  sim->host_milliseconds += input.elapsed_milliseconds;
  sim->selected_milliseconds += input.elapsed_milliseconds;
  if (sim->selected_q3_source) qa_app_q3_selected_source_synchronize(sim->selected_q3_source);
  for (size_t i = 0; i < input.command_count; i++) {
    ss_qvm_entry *entry = &commands[i];
    const qa_app_sim_grapple_source *grapple_source = sim->grapple ? qa_app_sim_grapple_runtime_source(sim->grapple) : NULL;
    if (grapple_source && qa_ss_is(grapple_source->kind, "q3-qvm")) qa_app_sim_qvm_grapple_source_pull(grapple_source->q3_game, entry->player.actor);
    double scale = sim->grapple ? qa_app_sim_grapple_runtime_gravity_scale(sim->grapple, entry->player.actor) : 1;
    ss_player_move_observer *observer = scale == 1 ? NULL : ss_observe_grapple_player_move(sim, guest, entry->player, scale);
    qa_cleanup remove; if (observer) qa_cleanup_push(&remove, ss_remove_observer, observer);
    qa_app_q3_qvm_server_game_think(guest, entry->player, entry->command);
    if (observer) { qa_cleanup_pop(&remove); ss_remove_observer(observer); }
    qa_q3_player_state state; double stats[16], persistent[16], powerups[16], ammo[16];
    qa_app_q3_guest_records_player(records, entry->player.source_entity, &state, stats, persistent, powerups, ammo);
    double *requested = qa_ss_map_get(&sim->native_weapon_requests, &entry->player.actor);
    if (requested && state.weapon == *requested) qa_ss_map_delete(&sim->native_weapon_requests, &entry->player.actor);
  }
  sim->source_scheduling_milliseconds += input.elapsed_milliseconds;
  while (sim->source_scheduling_milliseconds >= qa_js_round(ss_time_seconds(sim) * 1000) + profile.server_frame_milliseconds) {
    sim->source_frame = qa_source_clock_advance(&sim->clock, (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, profile.server_frame_milliseconds},
                                                QA_FRAME_PHASE_FRAME_ENTRY);
    qa_app_shared_sim_advance_q1_punch(sim); qa_app_shared_sim_begin_native_equipment_frame(sim);
    qa_app_shared_sim_begin_qvm_selected_frame(sim);
    /* The guest spawns its world during application-driven initialize,
     * before the first awaited step; admit replacements against it here. */
    qa_app_shared_sim_admit_guest_monsters(sim);
    qa_app_q3_qvm_server_game_run_frame(guest, trunc(ss_time_seconds(sim) * 1000));
    qa_app_shared_sim_apply_guest_travel_carries(sim);
    qa_app_shared_sim_run_guest_monsters(sim);
    qa_app_shared_sim_run_qvm_selected_actors(sim);
    qa_app_shared_sim_assert_open(sim);
    sim->source_frame = qa_source_clock_enter(&sim->clock, QA_FRAME_PHASE_FRAME_EXIT);
    if (sim->mod_owner) qa_session_mods_advance(sim->mod_owner, sim->source_frame);
  }
  qa_simulation_output output = ss_output(sim, true);
  qa_cleanup_pop(&stepping);
  sim->stepping = false;
  free(commands); free(seen);
  return output;
}

/* ---- step (5216-5684) ---- */
typedef struct ss_step {
  qa_app_shared_simulation *sim;
  qa_input_batch input;
  qa_ss_round_settlement settlement;
  qa_q2_lmctf *lmctf;
  bool paused;
  double previous_selected_milliseconds;
  qa_clock_profile profile;
  qa_ss_step_plan plan;
  bool has_selected_q3_clock; qa_clock_profile selected_q3_clock;
  qa_ss_selected_weapon_source *selected_q2;
  bool commands_pending;
  qa_simulation_output output;
} ss_step;
/* The prepareNetQuake arsenal callback (5375-5380). */
typedef struct ss_netquake_prepare {
  ss_step *step; qa_app_sim_movement_player *player;
  qa_ss_map *pending; qa_arsenal_intent arsenal;
} ss_netquake_prepare;
static const qa_arsenal_intent *ss_prepare_netquake(void *ctx, qa_q1_user_command effective, const qa_arsenal_intent *arsenal, bool *out_has) {
  ss_netquake_prepare *prepare = ctx; qa_app_shared_simulation *sim = prepare->step->sim;
  qa_actor_command *received = qa_ss_map_get(prepare->pending, &prepare->player->actor.id);
  if (sim->grapple) qa_app_sim_grapple_runtime_set_jump(sim->grapple, prepare->player->actor.id, (qa_js_to_int32(effective.buttons) & 2) != 0);
  if (!received) { *out_has = arsenal != NULL; return arsenal; }
  qa_actor_command command = *received;
  command.command.kind = QA_USER_COMMAND_Q1; command.command.as.q1 = effective;
  if (arsenal) command.arsenal = arsenal;
  qa_actor_command prepared = qa_app_shared_sim_prepare_arsenal_command(sim, prepare->player, command, false);
  if (!prepared.arsenal) { *out_has = false; return NULL; }
  prepare->arsenal = *prepared.arsenal; *out_has = true;
  return &prepare->arsenal;
}
/* The per-command execute closure (5440-5515). */
typedef struct ss_execute {
  ss_step *step;
  qa_app_sim_movement_player *player;
  qa_actor_command command;
  qa_ss_map *pending_q1_pre_think;
  qa_arsenal_intent arsenal_storage;
} ss_execute;
static void ss_execute_command(ss_execute *execute, qa_user_command effective, const qa_arsenal_intent *arsenal) {
  ss_step *step = execute->step; qa_app_shared_simulation *sim = step->sim;
  qa_app_sim_movement_player *player = execute->player;
  execute->command.command = effective;
  if (arsenal) { execute->arsenal_storage = *arsenal; execute->command.arsenal = &execute->arsenal_storage; }
  double *pre_think = execute->pending_q1_pre_think ? qa_ss_map_get(execute->pending_q1_pre_think, &player) : NULL;
  if (pre_think && *pre_think == execute->command.sequence) {
    qa_ss_map_delete(execute->pending_q1_pre_think, &player);
    if (!sim->strategy->pre_think(sim, player, &execute->command)) return;
  }
  execute->command = qa_app_shared_sim_prepare_arsenal_command(sim, player, execute->command, step->paused);
  if (step->paused && step->lmctf && !qa_q2_lmctf_can_move(step->lmctf, player->actor.id)) {
    player->last_sequence = execute->command.sequence;
    return;
  }
  if (player->has_cutscene) {
    player->previous_buttons = player->buttons; player->buttons = ss_buttons(&execute->command.command); player->last_sequence = execute->command.sequence;
    return;
  }
  if (sim->strategy->intermission_active(sim)) {
    player->previous_buttons = player->buttons; player->buttons = ss_buttons(&execute->command.command); player->last_sequence = execute->command.sequence;
    sim->strategy->request_intermission_exit(sim, player->actor.id, player->buttons);
    return;
  }
  const qa_user_command *movement = &execute->command.command;
  if (sim->grapple) qa_app_sim_grapple_runtime_set_jump(sim->grapple, player->actor.id, ss_command_jump(movement));
  if (!sim->strategy->player_think(sim, player, &execute->command)) {
    double gravity_multiplier = player->gravity_multiplier;
    double source_gravity = sim->strategy->gravity_scale(sim, player->actor.id);
    player->gravity_multiplier *= source_gravity * (sim->grapple ? qa_app_sim_grapple_runtime_gravity_scale(sim->grapple, player->actor.id) : 1);
    if ((sim->strategy->tracks_gravity(sim) || sim->grapple) && (player->state.kind == QA_MOVEMENT_STATE_Q2
        || player->state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE || player->state.kind == QA_MOVEMENT_STATE_Q3)) {
      double gravity = trunc(qa_app_sim_shared_physics_gravity(sim->physics) * player->gravity_multiplier);
      if (player->state.kind == QA_MOVEMENT_STATE_Q2) player->state.as.q2.gravity = gravity;
      else if (player->state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) player->state.as.q2_rerelease.gravity = gravity;
      else player->state.as.q3.gravity = gravity;
    }
    qa_app_sim_relative_movement_input relative_input = {execute->command.command, execute->command.angle_space};
    qa_app_sim_relative_movement_input relative_out = qa_app_sim_relative_movement_command(relative_input, qa_app_sim_movement_player_read_state(player));
    qa_actor_command relative = execute->command;
    relative.command = relative_out.command; relative.angle_space = relative_out.angle_space;
    qa_frame_context movement_frame = sim->source_frame;
    movement_frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
    movement_frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, qa_ss_is(player->profile.kind, "q1-netquake")
        ? qa_js_min(100, qa_js_max(1, step->input.elapsed_milliseconds)) : step->input.elapsed_milliseconds};
    qa_movement_result moved = qa_mod_client_applications_active(sim->mod_client_applications)
        ? qa_app_sim_movement_player_move_command(player, relative, movement_frame, NULL)
        : qa_app_sim_movement_player_move(player, relative, movement_frame, NULL);
    sim->strategy->movement_applied(sim, player->actor.id, &moved);
    player->gravity_multiplier = gravity_multiplier;
  }
  if (!ss_live(sim, player->actor.id)) return;
  if (player->state.kind == QA_MOVEMENT_STATE_Q1) player->state.as.q1.punch_angles = qa_app_sim_q1_player_punch_read(sim->q1_punch, player->actor.id);
  if (!step->paused && sim->has_selected_arsenal && (qa_ss_is(player->profile.kind, "q2-classic")
      || qa_ss_is(player->profile.kind, "q2-rerelease")) && ss_live(sim, player->actor.id)) {
    qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, player->actor.id);
    if (!combat.has_value) ss_fail("Selected arsenal owner has no combat state");
    double milliseconds = 0;
    if (!ss_command_milliseconds(&execute->command.command, &milliseconds)) milliseconds = qa_js_min(100, qa_js_max(1, step->input.elapsed_milliseconds));
    qa_weapon_step_input weapon; memset(&weapon, 0, sizeof weapon);
    weapon.actor = player->actor; weapon.command = execute->command.command;
    weapon.frame = sim->source_frame; weapon.frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
    weapon.frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, milliseconds};
    weapon.arsenal = qa_app_shared_sim_arsenal(sim, player); weapon.animation = player->animation;
    qa_app_sim_movement_environment_player environment = {player->has_source_environment, player->source_environment,
        player->gravity_multiplier, player->state, player->flight, qa_app_sim_movement_player_speed_multiplier(player)};
    weapon.environment = qa_app_sim_player_movement_environment(environment, combat.value); weapon.gauntlet_hit = false;
    qa_weapon_step_result result = qa_app_shared_sim_weapon_step(sim, weapon);
    /* A selected arsenal read is malloc'd and weaponStep's selected path never returns its input arsenal; the
     * result is the player's adopted arsenal (qa_app_shared_sim_store_player_arsenal). */
    if (sim->has_selected_arsenal) free((void *)weapon.arsenal.ammo);
    player->arsenal = result.arsenal; player->animation = result.animation;
  }
  sim->strategy->after_player_command(sim, player, step->paused);
  qa_q2_character_actor **q2 = qa_ss_map_get(&sim->q2_characters, &player->actor);
  if (q2) qa_q2_character_actor_after_client_think(*q2);
  qa_q1_character_actor **q1 = qa_ss_map_get(&sim->q1_characters, &player->actor);
  if (q1) qa_q1_character_actor_post_move(*q1);
  if (!step->paused) qa_app_sim_shared_physics_commit_attachments(sim->physics);
}
static void ss_execute_operation(void *ctx, qa_user_command command, const qa_arsenal_intent *arsenal) { ss_execute_command(ctx, command, arsenal); }

/* TS finishFrameState(run, paused): character animation and end-of-frame hooks for the selected source after its actors have run. */
static void ss_finish_frame_state(qa_app_shared_simulation *sim, bool run, bool paused) {
  sim->strategy->finish_frame(sim, run);
  for (size_t i = 0; i < sim->characters.count; i++) {
    qa_owned_actor actor = qa_ss_map_key_at(&sim->characters, i)->owned;
    qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
    if (player) player->animation = qa_q3_character_actor_get_animation(*(qa_q3_character_actor **)qa_ss_map_value_at(&sim->characters, i));
  }
  for (size_t i = 0; i < sim->q2_characters.count; i++) {
    qa_owned_actor actor = qa_ss_map_key_at(&sim->q2_characters, i)->owned;
    qa_q2_character_actor *character = *(qa_q2_character_actor **)qa_ss_map_value_at(&sim->q2_characters, i);
    double *tick = qa_ss_map_get(&sim->character_ticks, &actor);
    if (!(paused || ss_time_seconds(sim) + 0.001 >= (tick ? *tick : 0))) continue;
    double next = ss_time_seconds(sim) + 0.1;
    qa_ss_map_set(&sim->character_ticks, &actor, &next);
    qa_q2_character_actor_end_frame(character, false);
    qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
    if (player) {
      player->animation.provider = sim->recipe.character.definition.provider;
      memset(&player->animation.state, 0, sizeof player->animation.state);
      player->animation.state.kind = "q2"; player->animation.state.frame = character->entity.frame;
      player->animation.state.end_frame = character->state.animation_end; player->animation.state.priority = character->state.animation_priority;
      player->animation.state.duck = character->state.animation_duck; player->animation.state.run = character->state.animation_run;
    }
  }
  for (size_t i = 0; i < sim->q1_characters.count; i++) {
    qa_owned_actor actor = qa_ss_map_key_at(&sim->q1_characters, i)->owned;
    qa_q1_character_actor *character = *(qa_q1_character_actor **)qa_ss_map_value_at(&sim->q1_characters, i);
    qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
    if (!player) continue;
    double invisibility = sim->strategy->q1_powerup(sim, actor.id, "invisibility");
    qa_q1_character_source_pose pose = qa_app_shared_sim_q1_character_pose(sim, player->actor.id);
    qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor.id);
    int32_t buttons = qa_js_to_int32(player->buttons);
    qa_q1_character_input input; memset(&input, 0, sizeof input);
    input.axe_pose = pose.has_axe_pose ? pose.axe_pose : qa_ss_is(qa_app_shared_sim_player_ui(sim, player->actor.id).active_weapon, "q1:weapon/axe");
    input.attack = (buttons & 1) != 0; input.jump = (buttons & 2) != 0; input.use = (buttons & 4) != 0;
    input.water_level = player->water_level >= 3 ? 3 : player->water_level == 2 ? 2 : player->water_level == 1 ? 1 : 0;
    input.water_type = player->water_type == -3 ? "water" : player->water_type == -4 ? "slime" : player->water_type == -5 ? "lava" : "empty";
    input.invisible = invisibility > ss_time_seconds(sim);
    input.invulnerable = combat.has_value && combat.value.invulnerable;
    qa_q1_character_presentation frame = qa_q1_character_actor_frame(character, ss_time_seconds(sim), input);
    sim->strategy->q1_character_frame(sim, actor.id, &frame);
    player->animation.provider = sim->recipe.character.definition.provider;
    memset(&player->animation.state, 0, sizeof player->animation.state);
    player->animation.state.kind = "q1"; player->animation.state.frame = frame.frame;
    player->animation.state.next_frame_seconds = ss_time_seconds(sim) + 0.1;
  }
}

/* TS StepBoundaryState. */
typedef struct ss_boundary_state {
  const qa_ss_boundary *boundary;
  bool run, command_turn;
  double previous_q3_time;
  const qa_actor_command *bot_commands; size_t bot_command_count;
  bool applications; /* this.modClientApplications.active */
  qa_ss_map pending_netquake;     /* pendingNetQuake: ActorId -> ActorCommand */
  qa_ss_map pending_q1_pre_think; /* pendingQ1PreThink: MovementPlayer -> sequence */
  bool prepares_q1_clients;
} ss_boundary_state;
typedef enum ss_flow { SS_FLOW_GO, SS_FLOW_NEXT, SS_FLOW_BREAK } ss_flow;
#define SS_BOUNDARY_LOCALS \
  qa_app_shared_simulation *sim = step->sim; \
  bool active = qa_ss_is(step->settlement.kind, "active"); \
  bool map_run = step->plan.map_run; \
  double elapsed = step->plan.elapsed; \
  const qa_ss_boundary *boundary = b->boundary; \
  bool run = b->run; \
  qa_ss_selected_weapon_source *selected_q2 = step->selected_q2; \
  (void)active; (void)map_run; (void)elapsed; (void)boundary; (void)run; (void)selected_q2;
static void ss_conclude_source_frame(ss_step *step, bool active, double elapsed);

/* TS beginStepBoundary: align the selected sources and monster clocks with one boundary and decide whether it runs the map and takes commands. */
static void ss_begin_step_boundary(ss_step *step, const qa_ss_boundary *boundary, ss_boundary_state *b) {
  qa_app_shared_simulation *sim = step->sim;
  bool map_run = step->plan.map_run;
  b->boundary = boundary;
  if (map_run) qa_app_shared_sim_capture_presentation_snapshot(sim);
  b->previous_q3_time = sim->selected_q3_time;
  if (boundary->q3 && step->has_selected_q3_clock) {
    sim->selected_q3_time = boundary->milliseconds; sim->selected_q3_next = boundary->milliseconds + step->selected_q3_clock.server_frame_milliseconds;
  }
  if (sim->selected_q3_source) qa_app_q3_selected_source_synchronize(sim->selected_q3_source);
  bool run = b->run = boundary->map;
  b->command_turn = step->commands_pending && (run || !map_run);
  if (b->command_turn) step->commands_pending = false;
  qa_ss_selected_weapon_source *selected_q2 = step->selected_q2;
  if (run && selected_q2) selected_q2->map_milliseconds = boundary->milliseconds;
  if (boundary->q2 && selected_q2) {
    bool milliseconds = selected_q2->frame.time.kind == QA_SOURCE_TIME_MILLISECONDS;
    qa_source_time_kind kind = milliseconds ? QA_SOURCE_TIME_MILLISECONDS : QA_SOURCE_TIME_SECONDS;
    selected_q2->frame = (qa_frame_context){selected_q2->frame.frame + 1,
        {kind, milliseconds ? boundary->milliseconds : boundary->milliseconds / 1000},
        {kind, milliseconds ? selected_q2->interval_milliseconds : selected_q2->interval_milliseconds / 1000}, QA_FRAME_PHASE_FRAME_ENTRY};
    selected_q2->next_milliseconds = boundary->milliseconds + selected_q2->interval_milliseconds;
  }
  if (map_run && !run) {
    qa_app_shared_sim_begin_monster_frames(sim, boundary->milliseconds, false);
    qa_app_shared_sim_begin_foreign_world_frame(sim, boundary->milliseconds, false);
    qa_app_shared_sim_begin_foreign_q1_world_frame(sim, false);
  }
  b->bot_commands = NULL; b->bot_command_count = 0;
  b->applications = qa_mod_client_applications_active(sim->mod_client_applications);
  qa_ss_map_init(&b->pending_netquake, QA_SS_KEY_ACTOR, sizeof(qa_actor_command));
  qa_ss_map_init(&b->pending_q1_pre_think, QA_SS_KEY_POINTER, sizeof(double));
  b->prepares_q1_clients = sim->strategy->prepares_q1_clients(sim);
}

/* TS admitBoundaryInputs: prepare Q1 clients, then queue bot commands for a map boundary. */
static ss_flow ss_admit_boundary_inputs(ss_step *step, ss_boundary_state *b) {
  SS_BOUNDARY_LOCALS
  if (run) {
    if (active) {
      qa_frame_context frame = qa_source_clock_frame(&sim->clock);
      frame.frame += 1; frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 100}; frame.phase = QA_FRAME_PHASE_FRAME_ENTRY;
      sim->source_frame = frame;
    } else if (!step->plan.has_fixed) {
      qa_frame_context frame = qa_source_clock_frame(&sim->clock);
      frame.elapsed = (qa_source_time){QA_SOURCE_TIME_SECONDS, elapsed}; frame.phase = QA_FRAME_PHASE_FRAME_ENTRY;
      sim->source_frame = frame;
    } else {
      qa_source_time_kind kind = qa_source_clock_frame(&sim->clock).time.kind;
      sim->source_frame = qa_source_clock_advance(&sim->clock, (qa_source_time){kind, kind == QA_SOURCE_TIME_SECONDS ? elapsed : step->plan.fixed},
                                                  QA_FRAME_PHASE_FRAME_ENTRY);
    }
    qa_app_shared_sim_begin_monster_frames(sim, boundary->milliseconds, true);
    qa_app_shared_sim_begin_foreign_world_frame(sim, boundary->milliseconds, true);
    qa_app_shared_sim_begin_foreign_q1_world_frame(sim, true);
    if (sim->has_selected_arsenal && qa_ss_is(sim->selected_arsenal.family, "q1")) {
      qa_frame_context frame = qa_app_shared_sim_selected_q1_frame(sim);
      qa_q1_begin_frame(qa_app_shared_sim_selected_q1_game(sim), qa_ss_seconds(frame.time), qa_ss_seconds(frame.elapsed));
      if (sim->has_selected_weapon_source && qa_ss_is(sim->selected_weapon_source.kind, "q1"))
        qa_comp_selected_q1_mission_weapons_frame(sim->selected_weapon_source.mission_weapons, qa_ss_seconds(frame.elapsed));
      qa_app_q1_selected_arsenal_frame(sim->selected_arsenal.q1, qa_ss_seconds(frame.time));
    }
    if (!step->paused) qa_app_shared_sim_advance_q1_punch(sim);
    if (sim->hand_grenades) sim->equipment_frame = qa_app_sim_provider_frame(sim->source_frame, step->profile,
        qa_app_sim_provider_timing(sim->recipe, qa_app_sim_hand_grenade_runtime_selection(sim->hand_grenades)->source.provider).clock);
    if (sim->grapple) {
      sim->grapple_frame = qa_app_sim_provider_frame(sim->source_frame, step->profile,
          qa_app_sim_provider_timing(sim->recipe, qa_app_sim_grapple_runtime_selection(sim->grapple)->source.provider).clock);
      const qa_app_sim_grapple_source *source = qa_app_sim_grapple_runtime_source(sim->grapple);
      if (qa_ss_is(source->kind, "q1-threewave"))
        qa_q1_begin_frame(source->q1_game, qa_ss_seconds(sim->grapple_frame.time), qa_ss_seconds(sim->grapple_frame.elapsed));
      else if (qa_ss_is(source->kind, "q3-qvm")) {
        qa_app_sim_qvm_grapple_source_begin_frame(source->q3_game, qa_js_round(qa_ss_seconds(sim->grapple_frame.time) * 1000), sim->grapple_frame.frame);
        for (size_t i = 0; i < sim->player_states.count; i++)
          qa_app_sim_qvm_grapple_source_pull(source->q3_game, (*(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i))->actor.id);
      }
    }
    if (sim->strategy->intermission_fading(sim)) {
      sim->checking_q2_rules = true;
      sim->strategy->fade_intermission(sim);
      sim->checking_q2_rules = false;
      sim->source_frame = qa_source_clock_enter(&sim->clock, QA_FRAME_PHASE_FRAME_EXIT);
      if (qa_event_ring_pending(&sim->transitions, &sim->transitions_cursor) != 0) return SS_FLOW_BREAK;
      return SS_FLOW_NEXT;
    }
    sim->strategy->begin_timed_frame(sim, elapsed);
    if (b->prepares_q1_clients)
      b->bot_commands = qa_app_sim_bot_services_frame(&sim->bot_services, sim->source_frame.time.kind == QA_SOURCE_TIME_MILLISECONDS
          ? sim->source_frame.time.value : trunc(ss_time_seconds(sim) * 1000), elapsed * 1000, &b->bot_command_count);
    if (b->prepares_q1_clients) {
      size_t total = step->input.command_count + b->bot_command_count;
      for (size_t i = 0; i < total; i++) {
        const qa_actor_command *command = i < step->input.command_count ? &step->input.commands[i] : &b->bot_commands[i - step->input.command_count];
        qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, command->actor);
        if (!qa_ss_is(player->profile.kind, "q1-netquake") || command->sequence <= player->last_sequence) continue;
        qa_app_shared_sim_observe_client_command(sim, command);
        if (b->applications) { qa_app_sim_movement_player_receive_netquake(player, *command); qa_ss_map_set(&b->pending_netquake, &player->actor.id, command); }
        else {
          qa_app_sim_movement_player_receive_netquake(player, qa_app_shared_sim_prepare_arsenal_command(sim, player, *command, false));
          if (sim->grapple) qa_app_sim_grapple_runtime_set_jump(sim->grapple, player->actor.id, (qa_js_to_int32(ss_buttons(&command->command)) & 2) != 0);
        }
        if (sim->strategy->intermission_active(sim)) sim->strategy->request_intermission_exit(sim, player->actor.id, player->buttons);
      }
      for (size_t i = 0; i < sim->player_states.count; i++) {
        qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
        if (!qa_ss_is(player->profile.kind, "q1-netquake") || player->has_cutscene || player->intermission) continue;
        double gravity_multiplier = player->gravity_multiplier;
        player->gravity_multiplier *= sim->grapple ? qa_app_sim_grapple_runtime_gravity_scale(sim->grapple, player->actor.id) : 1;
        qa_frame_context frame = sim->source_frame; frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
        ss_netquake_prepare prepare = {step, player, &b->pending_netquake, {0}};
        qa_app_sim_movement_player_prepare_netquake(player, frame, b->applications ? ss_prepare_netquake : NULL, &prepare);
        player->gravity_multiplier = gravity_multiplier;
      }
    }
    sim->strategy->begin_guest_frame(sim, sim->source_frame);
    qa_app_source_pre_frame_input pre_frame = {elapsed, step->input.commands, step->input.command_count, b->applications, &b->pending_q1_pre_think};
    sim->strategy->pre_frame(sim, &pre_frame);
  }
  if (sim->strategy->intermission_fading(sim)) return SS_FLOW_NEXT;
  if (run && !active) {
    qa_app_shared_sim_tick_horde_wave(sim); qa_app_shared_sim_tick_tag(sim);
    qa_app_shared_sim_tick_lmctf(sim); qa_app_shared_sim_tick_deathball(sim);
  }
  if (run && !b->prepares_q1_clients && !active)
    b->bot_commands = qa_app_sim_bot_services_frame(&sim->bot_services, sim->source_frame.time.kind == QA_SOURCE_TIME_MILLISECONDS
        ? sim->source_frame.time.value : trunc(ss_time_seconds(sim) * 1000), elapsed * 1000, &b->bot_command_count);
  return SS_FLOW_GO;
}

/* TS applyBoundaryCommands: apply the human and bot commands that fall on this boundary. */
static void ss_apply_boundary_commands(ss_step *step, ss_boundary_state *b) {
  SS_BOUNDARY_LOCALS
  size_t input_count = b->command_turn ? step->input.command_count : 0;
  for (size_t i = 0; i < input_count + b->bot_command_count; i++) {
    qa_actor_command command = i < input_count ? step->input.commands[i] : b->bot_commands[i - input_count];
    if (step->paused) ss_set_buttons(&command.command, ss_mask_attack(ss_buttons(&command.command)));
    qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, command.actor);
    if (!player) continue;
    if (command.sequence <= player->last_sequence) continue;
    qa_app_shared_sim_observe_client_command(sim, &command);
    ss_execute execute = {step, player, command, b->applications ? &b->pending_q1_pre_think : NULL, {0}};
    if (b->applications) {
      qa_app_sim_relative_movement_input relative_input = {command.command, command.angle_space};
      qa_app_sim_relative_movement_input relative = qa_app_sim_relative_movement_command(relative_input, qa_app_sim_movement_player_read_state(player));
      execute.command.command = relative.command; execute.command.angle_space = relative.angle_space;
      qa_frame_context frame = sim->source_frame; frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
      frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, step->input.elapsed_milliseconds};
      qa_app_sim_movement_player_with_input_command(player, execute.command.command, frame, ss_execute_operation, &execute, execute.command.arsenal);
    } else ss_execute_command(&execute, command.command, command.arsenal);
  }
}

/* TS advanceBoundaryActors: advance executions, sources and characters through one unpaused boundary. */
static void ss_advance_boundary_actors(ss_step *step, ss_boundary_state *b) {
  SS_BOUNDARY_LOCALS
  if (!step->paused) {
    if (run) sim->strategy->begin_actor_turns(sim, sim->source_frame);
    qa_app_sim_actor_visited visited = {0};
    qa_app_sim_ordered_actor_turns *turns = qa_app_sim_ordered_actor_turns_create(sim->actors, ss_turn_position, sim);
    qa_owned_actor actor;
    while (qa_app_sim_ordered_actor_turns_next(turns, &visited, &actor)) {
      if (sim->grapple && qa_ss_is(qa_app_sim_grapple_runtime_source(sim->grapple)->kind, "q3-qvm")
          && qa_ss_is(actor.owner, qa_app_sim_grapple_runtime_selection(sim->grapple)->source.provider)) continue;
      if (sim->mod_owner && qa_session_mods_owns(sim->mod_owner, actor.owner)) continue;
      qa_app_sim_actor_execution *found = qa_ss_map_get(&sim->actor_executions, &actor.id);
      qa_app_sim_actor_execution entry; if (found) entry = *found;
      const qa_app_sim_actor_execution *execution = found ? &entry : NULL;
      qa_app_sim_movement_player *client_player = qa_app_shared_sim_player_state(sim, actor);
      if (run && client_player && qa_ss_is(client_player->profile.kind, "q1-netquake") && b->prepares_q1_clients) {
        sim->strategy->before_client_actor(sim, actor);
        if (ss_live(sim, actor.id) && !client_player->has_cutscene && !client_player->intermission) {
          double gravity_multiplier = client_player->gravity_multiplier;
          client_player->gravity_multiplier *= sim->grapple ? qa_app_sim_grapple_runtime_gravity_scale(sim->grapple, actor.id) : 1;
          qa_frame_context frame = sim->source_frame; frame.phase = QA_FRAME_PHASE_ENTITY_PHYSICS;
          qa_app_sim_movement_player_physics_netquake(client_player, frame);
          client_player->gravity_multiplier = gravity_multiplier;
          if (!ss_live(sim, actor.id)) continue;
          if (client_player->state.kind == QA_MOVEMENT_STATE_Q1)
            client_player->state.as.q1.punch_angles = qa_app_sim_q1_player_punch_read(sim->q1_punch, actor.id);
          qa_app_shared_sim_sync_quake_c_client_view(sim, client_player);
        } else qa_app_sim_movement_player_finish_netquake_input(client_player, false);
        if (boundary->q2) qa_app_shared_sim_frame_selected_q2_weapon(sim, actor);
        if (ss_live(sim, actor.id)) {
          qa_q2_character_actor **character = qa_ss_map_get(&sim->q2_characters, &actor);
          if (character) qa_q2_character_actor_begin_frame(*character);
          qa_app_shared_sim_step_hand_grenade(sim, client_player->actor.id, ss_health(sim, actor.id) > 0 ? "alive" : "dead");
          if (sim->grapple) qa_app_sim_grapple_runtime_step(sim->grapple, actor.id,
              ss_health(sim, actor.id) > 0 && !client_player->intermission && !client_player->has_cutscene);
          qa_app_shared_sim_player_weapon(sim, client_player);
        }
        qa_app_sim_shared_physics_commit_attachments(sim->physics); continue;
      }
      if (execution && execution->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) {
        if (!run) continue;
        qa_app_sim_quakec_source_before_actor(execution->quakec->ctx, actor);
        if (ss_live(sim, actor.id)) {
          qa_app_sim_actor_execution_frame context = ss_execution_frame(sim, sim->source_frame, ss_time_seconds(sim), elapsed, &visited);
          qa_app_sim_execute_actor(execution, &context);
        }
        qa_app_shared_sim_run_quake_world_new_missile(sim);
        if (boundary->q2 && client_player) qa_app_shared_sim_frame_selected_q2_weapon(sim, actor);
        qa_app_sim_shared_physics_commit_attachments(sim->physics); continue;
      }
      if (execution && execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE) {
        if (boundary->q3) execution->step(execution->ctx, b->previous_q3_time, sim->selected_q3_time);
        qa_app_sim_shared_physics_commit_attachments(sim->physics); continue;
      }
      if (execution && execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3) {
        if (!b->command_turn) continue;
        execution->step(execution->ctx, step->previous_selected_milliseconds, sim->selected_milliseconds);
        qa_app_sim_shared_physics_commit_attachments(sim->physics); continue;
      }
      qa_q2_game_services *execution_game = execution && execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2
          ? qa_q2_entity_services_game(execution->q2_services) : NULL;
      bool selected_actor = selected_q2 && execution_game && execution_game == selected_q2->q2_game;
      if (selected_actor) {
        if (boundary->q2) {
          qa_app_sim_actor_execution_frame context = ss_execution_frame(sim, selected_q2->frame, qa_ss_seconds(selected_q2->frame.time),
              selected_q2->interval_milliseconds / 1000, &visited);
          qa_app_sim_execute_actor(execution, &context);
          qa_app_sim_shared_physics_commit_attachments(sim->physics);
        }
        continue;
      }
      if (boundary->q2) qa_app_shared_sim_frame_selected_q2_weapon(sim, actor);
      qa_app_sim_selected_monster_source **monster_entry = execution_game ? qa_ss_map_get(&sim->monster_sources, &execution_game->options.provider) : NULL;
      qa_app_sim_selected_monster_source *monster_source = monster_entry ? *monster_entry : NULL;
      if (execution_game && monster_source && qa_ss_is(monster_source->kind, "q2")
          && execution->q2_services == monster_source->q2->game) {
        bool live_monster = run ? (sim->selected_monsters == NULL || qa_app_sim_selected_monsters_before_turn(sim->selected_monsters, actor.id))
                                : (sim->selected_monsters == NULL || qa_app_sim_selected_monsters_active(sim->selected_monsters, actor.id));
        if (run && live_monster) sim->strategy->retouch(sim, actor);
        if (map_run && live_monster && monster_source->clock.advanced && ss_live(sim, actor.id)) {
          qa_frame_context frame = monster_source->clock.frame;
          qa_app_sim_actor_execution_frame context = ss_execution_frame(sim, frame, qa_ss_seconds(frame.time), qa_ss_seconds(frame.elapsed), &visited);
          qa_app_sim_execute_actor(execution, &context);
          qa_app_sim_shared_physics_commit_attachments(sim->physics);
        }
        continue;
      }
      if (execution_game && sim->has_foreign_world && execution->q2_services == sim->foreign_world.modules->game) {
        if (map_run && run && sim->foreign_world.clock.advanced && ss_live(sim, actor.id)) {
          qa_frame_context frame = sim->foreign_world.clock.frame;
          qa_app_sim_actor_execution_frame context = ss_execution_frame(sim, frame, qa_ss_seconds(frame.time), qa_ss_seconds(frame.elapsed), &visited);
          qa_app_sim_execute_actor(execution, &context);
          qa_app_sim_shared_physics_commit_attachments(sim->physics);
        }
        continue;
      }
      if (execution && execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 && sim->has_foreign_q1_world
          && execution->q1_services == sim->foreign_q1_world.modules->game) {
        if (map_run && run && sim->foreign_q1_world.clock.advanced && ss_live(sim, actor.id)) {
          qa_frame_context frame = sim->foreign_q1_world.clock.frame;
          qa_app_sim_actor_execution_frame context = ss_execution_frame(sim, frame, qa_ss_seconds(frame.time), qa_ss_seconds(frame.elapsed), &visited);
          qa_app_sim_execute_actor(execution, &context);
          qa_app_sim_shared_physics_commit_attachments(sim->physics);
        }
        continue;
      }
      if (!run || (sim->selected_monsters && !qa_app_sim_selected_monsters_before_turn(sim->selected_monsters, actor.id))) continue;
      qa_app_sim_movement_player *equipment_player = qa_app_shared_sim_player_state(sim, actor);
      if (equipment_player) {
        qa_app_shared_sim_step_hand_grenade(sim, equipment_player->actor.id, NULL);
        if (sim->grapple)
          qa_app_sim_grapple_runtime_step(sim->grapple, actor.id, ss_health(sim, actor.id) > 0 && !equipment_player->intermission
              && !equipment_player->has_cutscene
              && !(equipment_player->state.kind == QA_MOVEMENT_STATE_Q3 && equipment_player->state.as.q3.movement_type == QA_Q3_PM_SPECTATOR)
              && !sim->strategy->spectating(sim, actor.id));
      }
      if (!ss_live(sim, actor.id)) continue;
      sim->strategy->retouch(sim, actor);
      if (!ss_live(sim, actor.id)) continue;
      if (execution && !qa_app_shared_sim_player_state(sim, actor)) {
        qa_provider_id source_provider = execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 ? qa_q1_services_provider(execution->q1_services)
            : execution_game ? execution_game->options.provider : NULL;
        qa_app_sim_selected_monster_source **monster = source_provider ? qa_ss_map_get(&sim->monster_sources, &source_provider) : NULL;
        const void *services = execution->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 ? (const void *)execution->q1_services : (const void *)execution->q2_services;
        const qa_app_sim_grapple_source *grapple = sim->grapple ? qa_app_sim_grapple_runtime_source(sim->grapple) : NULL;
        qa_frame_context frame = monster ? (*monster)->clock.frame
            : sim->has_selected_arsenal && qa_ss_is(sim->selected_arsenal.family, "q1") && services == (const void *)qa_app_shared_sim_selected_q1_game(sim)
            ? qa_app_shared_sim_selected_q1_frame(sim)
            : grapple && (services == grapple->q1_game || services == grapple->q2_game) ? sim->grapple_frame
            : sim->hand_grenades && services == (const void *)qa_app_sim_hand_grenade_runtime_independent_game(sim->hand_grenades) ? sim->equipment_frame
            : sim->source_frame;
        qa_app_sim_actor_execution_frame context = ss_execution_frame(sim, frame, qa_ss_seconds(frame.time), elapsed, &visited);
        qa_app_sim_execute_actor(execution, &context);
        qa_app_sim_shared_physics_commit_attachments(sim->physics);
        continue;
      }
      if (sim->strategy->run_player_actor(sim, actor)) continue;
      if (qa_app_shared_sim_player_state(sim, actor)) {
        qa_q2_character_actor **character = qa_ss_map_get(&sim->q2_characters, &actor);
        if (character) qa_q2_character_actor_begin_frame(*character);
        qa_app_shared_sim_player_weapon(sim, qa_app_shared_sim_player_state(sim, actor));
        qa_app_sim_shared_physics_commit_attachments(sim->physics); continue;
      }
      qa_frame_context frame = sim->source_frame; frame.phase = QA_FRAME_PHASE_ENTITY_THINK;
      qa_frame_scheduler_run(sim->scheduler, actor.id, frame, QA_THINK_DURING_PHYSICS);
      if (ss_live(sim, actor.id)) qa_app_sim_shared_physics_step(sim->physics, actor, elapsed);
      qa_app_sim_shared_physics_commit_attachments(sim->physics);
    }
    qa_app_sim_ordered_actor_turns_destroy(turns);
    free(visited.actors);
  }
}

/* TS closeStepBoundary: close monster frames, reconcile weapon slots and conclude the source frame; true when a level change ends the step. */
static bool ss_close_step_boundary(ss_step *step, ss_boundary_state *b) {
  SS_BOUNDARY_LOCALS
  if (map_run) for (size_t i = 0; i < sim->monster_sources.count; i++) {
    qa_app_sim_selected_monster_source *source = *(qa_app_sim_selected_monster_source **)qa_ss_map_value_at(&sim->monster_sources, i);
    if (qa_ss_is(source->kind, "q2") && source->clock.advanced) qa_q2m_monsters_end_frame(&source->q2->monsters, qa_q2_entity_services_game(source->q2->game));
  }
  if (map_run && sim->has_foreign_world && sim->foreign_world.clock.advanced)
    qa_q2m_monsters_end_frame(&sim->foreign_world.modules->monsters, qa_q2_entity_services_game(sim->foreign_world.modules->game));
  if (run) for (size_t i = 0; i < sim->weapon_slots.count; i++)
    qa_app_sim_weapon_slot_reconcile(*(qa_app_sim_weapon_slot **)qa_ss_map_value_at(&sim->weapon_slots, i));
  if (run || step->paused) ss_finish_frame_state(sim, run, step->paused);
  if (run) ss_conclude_source_frame(step, active, elapsed);
  return sim->strategy->watches_transitions(sim) && qa_event_ring_pending(&sim->transitions, &sim->transitions_cursor) != 0;
}
/* TS concludeSourceFrame: source end-of-frame work and the clock's frame-exit transition once a frame has run. */
static void ss_conclude_source_frame(ss_step *step, bool active, double elapsed) {
  qa_app_shared_simulation *sim = step->sim;
  {
    sim->strategy->conclude_frame(sim);
    if (active) {
      sim->source_frame = qa_source_clock_advance(&sim->clock, (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 100}, QA_FRAME_PHASE_FRAME_EXIT);
      step->settlement.completed++; sim->source_round_settlement.completed = step->settlement.completed;
    } else if (!step->plan.has_fixed) sim->source_frame = qa_source_clock_advance(&sim->clock, (qa_source_time){QA_SOURCE_TIME_SECONDS, elapsed}, QA_FRAME_PHASE_FRAME_EXIT);
    else sim->source_frame = qa_source_clock_enter(&sim->clock, QA_FRAME_PHASE_FRAME_EXIT);
    if (sim->mod_owner) qa_session_mods_advance(sim->mod_owner, sim->source_frame);
  }
}

static void ss_step_body(void *ctx) {
  ss_step *step = ctx; qa_app_shared_simulation *sim = step->sim;
  sim->strategy->sync_gravity(sim);
  for (size_t i = 0; i < step->input.command_count; i++) {
    const qa_actor_command *command = &step->input.commands[i];
    qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, command->actor);
    /* A client released earlier in the frame leaves a stale command behind; it carries no request. */
    if (!player) continue;
    if (command->source.kind != QA_COMMAND_SOURCE_BOT && (command->source.client.slot != player->client.slot
        || command->source.client.generation != player->client.generation || !qa_app_shared_sim_owns_client(sim, command->source.client)))
      ss_fail("Command client does not own this player");
  }
  sim->host_milliseconds += step->input.elapsed_milliseconds;
  if (sim->q1_pause_state) { step->output = ss_output(sim, true); return; }
  step->lmctf = sim->strategy->lmctf(sim);
  step->paused = step->lmctf && qa_lmctf_match_paused(qa_q2_lmctf_get_match(step->lmctf));
  step->previous_selected_milliseconds = sim->selected_milliseconds;
  if (!step->paused) sim->selected_milliseconds += step->input.elapsed_milliseconds;
  if (!step->paused) sim->source_scheduling_milliseconds += step->input.elapsed_milliseconds;
  step->profile = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider).clock;
  step->plan = qa_app_shared_sim_plan_step(sim, step->input, step->paused);
  step->selected_q2 = sim->has_selected_weapon_source && qa_ss_is(sim->selected_weapon_source.kind, "q2") ? &sim->selected_weapon_source : NULL;
  if (sim->selected_q3_source) { step->has_selected_q3_clock = true; step->selected_q3_clock = qa_app_sim_provider_timing(sim->recipe, sim->weapon_provider.provider).clock; }
  sim->has_presentation_tick_milliseconds = step->plan.has_fixed; sim->presentation_tick_milliseconds = step->plan.fixed;
  sim->presentation_ahead = true;
  step->commands_pending = true;
  for (size_t i = 0; i < step->plan.boundary_count; i++) {
    ss_boundary_state b; memset(&b, 0, sizeof b);
    ss_begin_step_boundary(step, &step->plan.boundaries[i], &b);
    ss_flow flow = ss_admit_boundary_inputs(step, &b);
    bool stop = flow == SS_FLOW_BREAK;
    if (flow == SS_FLOW_GO) {
      ss_apply_boundary_commands(step, &b);
      ss_advance_boundary_actors(step, &b);
      stop = ss_close_step_boundary(step, &b);
    }
    qa_ss_map_free(&b.pending_netquake); qa_ss_map_free(&b.pending_q1_pre_think);
    if (stop) break;
  }
  sim->strategy->after_step(sim);
  step->output = ss_output(sim, true);
}
qa_simulation_output qa_app_shared_sim_step(qa_app_shared_simulation *sim, qa_input_batch input) {
  if (sim->pending_shared_restore) qa_shared_world_restore_assert_complete(sim->pending_shared_restore);
  qa_app_shared_sim_assert_open(sim);
  qa_app_shared_sim_assert_bot_restore_ready(sim);
  if (sim->stepping || sim->checkpoint_in_progress) ss_fail("Simulation step is already running or a checkpoint is active");
  qa_app_source_step_route route = sim->strategy->step_route(sim);
  if (route == QA_APP_SOURCE_STEP_NATIVE) return qa_app_shared_sim_step_native_q2(sim, input);
  if (route == QA_APP_SOURCE_STEP_AWAITED) ss_fail("Q3 guest requires the awaited simulation step");
  if (!isfinite(input.elapsed_milliseconds) || input.elapsed_milliseconds < 0) qa_range_error("Host elapsed time must be finite and nonnegative");
  qa_ss_round_settlement settlement = sim->source_round_settlement;
  if (qa_ss_is(settlement.kind, "failed")) ss_fail("Source round settlement failed; close the session");
  if (qa_ss_is(settlement.kind, "active")) {
    if (!sim->strategy->settles(sim, settlement.source)) ss_fail("Source round settlement belongs to a retired source");
    if (settlement.completed >= 4) ss_fail("Source round settlement already completed four frames");
    if (input.elapsed_milliseconds != 100 || input.command_count != 0) ss_fail("Source round settlement requires one empty 100ms input");
  } else if (qa_ss_is(settlement.kind, "ready")) sim->source_round_settlement = (qa_ss_round_settlement){"none", NULL, 0};
  sim->stepping = true;
  qa_app_shared_sim_flush_guest_monster_report(sim);
  ss_step step; memset(&step, 0, sizeof step);
  step.sim = sim; step.input = input; step.settlement = settlement;
  qa_caught_error caught;
  bool ok = qa_try(ss_step_body, &step, &caught);
  qa_ss_step_plan_free(&step.plan);
  /* finally { this.primaryCommandBlocks.clear(); this.stepping = false; } */
  qa_ss_map_clear(&sim->primary_command_blocks);
  sim->stepping = false;
  if (!ok) {
    if (qa_ss_is(settlement.kind, "active")) sim->source_round_settlement = (qa_ss_round_settlement){"failed", settlement.source, settlement.completed};
    qa_rethrow(&caught);
  }
  return step.output;
}

/* ---- native Q2 step (7257-7333) ---- */
void qa_app_shared_sim_begin_native_q2_step(qa_app_shared_simulation *sim, qa_input_batch input) {
  if (sim->strategy->as_q2_native(sim) == NULL || sim->stepping) ss_fail("Native Quake II frame is unavailable");
  if (!isfinite(input.elapsed_milliseconds) || input.elapsed_milliseconds < 0) qa_range_error("Invalid host frame interval");
  sim->stepping = true;
}
static bool ss_native_frame_continue(void *ctx, double frame_milliseconds) { (void)ctx; (void)frame_milliseconds; return true; }
qa_simulation_output qa_app_shared_sim_step_native_q2(qa_app_shared_simulation *sim, qa_input_batch input) {
  qa_app_shared_sim_begin_native_q2_step(sim, input);
  qa_cleanup stepping; qa_cleanup_push(&stepping, ss_stepping_off, sim);
  /* Synchronous callers consume the original complete batch. */
  qa_app_shared_sim_native_q2_frames(sim, input, ss_native_frame_continue, NULL);
  qa_simulation_output output = ss_output(sim, true);
  qa_cleanup_pop(&stepping);
  sim->stepping = false;
  return output;
}
typedef struct ss_native_progress {
  qa_app_shared_simulation *sim; qa_app_shared_sim_progress_fn progress; void *progress_ctx;
  double started, elapsed_milliseconds;
} ss_native_progress;
static double ss_now_milliseconds(void) {
  struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000 + (double)now.tv_nsec / 1e6;
}
static bool ss_native_frame_progress(void *ctx, double frame_milliseconds) {
  ss_native_progress *state = ctx; qa_app_shared_simulation *sim = state->sim;
  /* Yield only between completed source ticks, after enough work to justify another presentation. */
  const double presentation_budget_milliseconds = 8;
  state->elapsed_milliseconds += frame_milliseconds;
  if (sim->source_scheduling_milliseconds < ss_time_seconds(sim) * 1000 + frame_milliseconds
      || ss_now_milliseconds() - state->started < presentation_budget_milliseconds) return true;
  qa_simulation_output output = ss_output(sim, true);
  qa_simulation_progress progress = {&output, state->elapsed_milliseconds, sim->source_scheduling_milliseconds - ss_time_seconds(sim) * 1000};
  state->progress(state->progress_ctx, progress);
  state->started = ss_now_milliseconds(); state->elapsed_milliseconds = 0;
  return true;
}
qa_simulation_output qa_app_shared_sim_step_native_q2_async(qa_app_shared_simulation *sim, qa_input_batch input,
    qa_app_shared_sim_progress_fn progress, void *progress_ctx) {
  qa_app_shared_sim_begin_native_q2_step(sim, input);
  qa_cleanup stepping; qa_cleanup_push(&stepping, ss_stepping_off, sim);
  ss_native_progress state = {sim, progress, progress_ctx, ss_now_milliseconds(), 0};
  qa_app_shared_sim_native_q2_frames(sim, input, ss_native_frame_progress, &state);
  qa_simulation_output output = ss_output(sim, true);
  qa_cleanup_pop(&stepping);
  sim->stepping = false;
  return output;
}
static const char *ss_source_kind_name(qa_command_source_kind kind) {
  return kind == QA_COMMAND_SOURCE_LOCAL_SEAT ? "local-seat" : kind == QA_COMMAND_SOURCE_REMOTE_CLIENT ? "remote-client" : "bot";
}
/* *nativeQ2Frames(input): each `yield frameMilliseconds` calls frame(). */
void qa_app_shared_sim_native_q2_frames(qa_app_shared_simulation *sim, qa_input_batch input,
    bool (*frame)(void *ctx, double frame_milliseconds), void *frame_ctx) {
  if (sim->strategy->as_q2_native(sim) == NULL) ss_fail("Native Quake II source is unavailable");
  bool classic = qa_ss_is(sim->source.edition, "classic");
  for (size_t i = 0; i < input.command_count; i++) {
    const qa_actor_command *command = &input.commands[i];
    double slot = (double)qa_app_shared_sim_native_q2_client(sim, command->actor).slot + 1;
    double up = 0;
    bool jump = command->command.kind == QA_USER_COMMAND_Q2_RERELEASE ? (qa_js_to_int32(command->command.as.q2_rerelease.buttons) & 8) != 0
        : ss_up_move(&command->command, &up) && up > 0;
    qa_ss_character_command character = {(qa_js_to_int32(ss_buttons(&command->command)) & 1) != 0, jump};
    qa_ss_map_set(&sim->native_character_commands, &command->actor, &character);
    qa_app_shared_sim_observe_client_command(sim, command);
    qa_app_shared_sim_require_equipment_player(sim, command->actor);
    if (sim->grapple) qa_app_sim_grapple_runtime_set_jump(sim->grapple, command->actor, jump);
    qa_app_sim_weapon_slot **equipment = qa_ss_map_get(&sim->weapon_slots, &command->actor);
    if (sim->grapple && qa_ss_is(qa_app_sim_grapple_runtime_selection(sim->grapple)->binding, "slot"))
      qa_app_sim_grapple_runtime_input(sim->grapple, command->actor,
          equipment && qa_app_sim_weapon_slot_equipment_selected(*equipment) && (qa_js_to_int32(ss_buttons(&command->command)) & 1) != 0);
    const qa_app_sim_grapple_source *grapple_source = sim->grapple ? qa_app_sim_grapple_runtime_source(sim->grapple) : NULL;
    if (grapple_source && qa_ss_is(grapple_source->kind, "q3-qvm")) qa_app_sim_qvm_grapple_source_pull(grapple_source->q3_game, command->actor);
    qa_actor_command gated = *command;
    if (equipment && !qa_app_sim_weapon_slot_primary_selected(*equipment)) ss_set_buttons(&gated.command, ss_mask_attack(ss_buttons(&gated.command)));
    qa_vec3 *velocity = qa_ss_map_get(&sim->native_equipment_velocity, &command->actor);
    qa_owned_actor owner; bool has_owner = qa_session_actor_registry_resolve_owned(sim->actors, command->actor, &owner);
    qa_app_q3_selected_source *selected = sim->selected_q3_source && qa_app_q3_selected_source_owns_equipment(sim->selected_q3_source)
        && qa_app_shared_sim_equipment_player_available(sim, command->actor) ? sim->selected_q3_source : NULL;
    qa_game_family character_family = qa_app_sim_provider_family(sim->recipe.character.definition.provider);
    qa_fixed_movement_pose pose; bool has_pose = false;
    if (selected && has_owner) {
      /* playerPostures({ character, standingBounds, viewHeight: character === "q3" ? 26 : 22 }): playerPostures reads
       * character and standingBounds only (the standing eye height is a character constant). */
      qa_q3_postures postures = qa_app_sim_player_postures(character_family, qa_app_sim_player_standing_bounds(character_family));
      has_pose = qa_app_q3_selected_source_fixed_pose(selected, owner, postures, &pose);
    }
    double speed_multiplier = selected ? qa_app_q3_selected_source_speed_multiplier(selected, command->actor) : 1;
    qa_equipment_movement movement; memset(&movement, 0, sizeof movement);
    bool has_movement = !(sim->grapple == NULL && velocity == NULL && selected == NULL);
    if (has_movement) {
      if (velocity) movement.velocity = velocity;
      movement.gravity_scale = sim->grapple ? qa_app_sim_grapple_runtime_gravity_scale(sim->grapple, command->actor) : 1;
      movement.prediction_suppressed = (sim->grapple && qa_app_sim_grapple_runtime_prediction(sim->grapple, command->actor))
          || speed_multiplier != 1 || has_pose;
      movement.has_speed_multiplier = true; movement.speed_multiplier = speed_multiplier;
      if (has_pose) movement.pose = &pose;
    }
    bool observing_input = qa_mod_client_applications_active(sim->mod_client_applications);
    if (observing_input) qa_ss_map_delete(&sim->native_equipment_velocity, &command->actor);
    if (classic) qa_app_sim_classic_guest_world_think(sim->source.classic_world, slot,
        qa_app_sim_classic_guest_local_command(ss_source_kind_name(gated.source.kind), gated.command,
            qa_app_sim_classic_guest_world_player_state(sim->source.classic_world, slot)),
        has_movement ? &movement : NULL);
    else qa_app_sim_rerelease_guest_world_think(sim->source.rerelease_world, slot,
        qa_app_sim_rerelease_guest_local_command(gated, qa_app_sim_rerelease_guest_world_player_state(sim->source.rerelease_world, slot)),
        has_movement ? &movement : NULL);
    if (!observing_input) qa_ss_map_delete(&sim->native_equipment_velocity, &command->actor);
  }
  sim->host_milliseconds += input.elapsed_milliseconds; sim->source_scheduling_milliseconds += input.elapsed_milliseconds;
  double frame_milliseconds = classic ? 100
      : qa_app_sim_rerelease_guest_services_options_of(qa_app_sim_rerelease_guest_world_services(sim->source.rerelease_world))->frame_milliseconds;
  sim->has_presentation_tick_milliseconds = true; sim->presentation_tick_milliseconds = frame_milliseconds;
  sim->presentation_ahead = false;
  while (sim->source_scheduling_milliseconds >= qa_js_round(ss_time_seconds(sim) * 1000) + frame_milliseconds) {
    qa_app_shared_sim_capture_presentation_snapshot(sim);
    sim->source_frame = qa_source_clock_advance(&sim->clock, classic ? (qa_source_time){QA_SOURCE_TIME_SECONDS, frame_milliseconds / 1000}
        : (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, frame_milliseconds}, QA_FRAME_PHASE_FRAME_ENTRY);
    sim->selected_milliseconds += frame_milliseconds;
    qa_app_shared_sim_advance_q1_punch(sim); qa_app_shared_sim_begin_native_equipment_frame(sim); qa_app_shared_sim_begin_qvm_selected_frame(sim);
    if (classic) qa_app_sim_classic_guest_world_frame(sim->source.classic_world, frame_milliseconds);
    else qa_app_sim_rerelease_guest_world_frame(sim->source.rerelease_world, frame_milliseconds);
    qa_app_shared_sim_tick_native_characters(sim);
    qa_app_shared_sim_run_guest_monsters(sim);
    if (sim->hand_grenades) {
      size_t count = 0; const qa_actor_id *players = qa_app_shared_sim_players(sim, &count);
      qa_actor_id *copy = calloc(count ? count : 1, sizeof *copy);
      if (!copy) qa_range_error("Out of memory");
      memcpy(copy, players, count * sizeof *copy);
      for (size_t i = 0; i < count; i++)
        qa_app_shared_sim_step_hand_grenade(sim, copy[i], qa_app_shared_sim_equipment_player_available(sim, copy[i]) ? "alive" : "dead");
      free(copy);
    }
    qa_app_shared_sim_run_qvm_selected_actors(sim);
    sim->source_frame = qa_source_clock_enter(&sim->clock, QA_FRAME_PHASE_FRAME_EXIT);
    if (sim->mod_owner) qa_session_mods_advance(sim->mod_owner, sim->source_frame);
    if (!frame(frame_ctx, frame_milliseconds)) break;
  }
}
