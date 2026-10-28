/* Port of src/content/q3/presentation/snapshots.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/snapshots.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/shared/snapshot_state.h"
#include "qa/content/q3/presentation/prediction.h"
#include "qa/core/common_error.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/parse_entities.h"

static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double truncated = trunc(fmod(fabs(value), 4294967296.0));
  uint32_t bits = (uint32_t)truncated;
  if (value < 0) bits = ~bits + 1u;
  int32_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}
static void fmt_num(char *buffer, size_t size, double value) {
  if (value == trunc(value) && fabs(value) < 1e21) {
    snprintf(buffer, size, "%lld", (long long)value);
  } else {
    snprintf(buffer, size, "%g", value);
  }
}

void qa_q3_pres_history_snapshot_source_init(qa_q3_pres_history_snapshot_source *source,
    qa_q3_snapshot_history *history, void *ctx, double (*parse_entities_number)(void *ctx),
    void (*debug_print)(void *ctx, const char *message)) {
  source->history = history;
  source->ctx = ctx;
  source->parse_entities_number = parse_entities_number;
  source->debug_print = debug_print;
}

qa_q3_pres_snapshot_current qa_q3_pres_history_snapshot_source_current(qa_q3_pres_history_snapshot_source *source) {
  qa_q3_wire_snapshot latest;
  qa_q3_pres_snapshot_current result = {0, 0};
  if (qa_q3_snapshot_history_latest(source->history, &latest)) {
    result.number = (double)latest.message_number;
    result.server_time = (double)latest.server_time;
    qa_q3_wire_snapshot_destroy(&latest);
  }
  return result;
}

bool qa_q3_pres_history_snapshot_source_read(qa_q3_pres_history_snapshot_source *source, double number,
    qa_q3_wire_snapshot *out) {
  if (number != trunc(number) || number < -2147483648.0 || number > 2147483647.0) {
    qa_range_error("Snapshot request number must be int32");
  }
  qa_q3_wire_snapshot latest;
  bool has_latest = qa_q3_snapshot_history_latest(source->history, &latest);
  double latest_number = has_latest ? (double)latest.message_number : 0;
  qa_q3_product latest_product = has_latest ? latest.player_state.product : QA_Q3_PRODUCT_BASEQ3;
  if (has_latest) qa_q3_wire_snapshot_destroy(&latest);
  if (number > latest_number) {
    qa_common_error(QA_COMMON_ERROR_DROP, "CL_GetSnapshot: snapshotNumber > cl.snapshot.messageNum");
  }
  if (to_int32(latest_number - number) >= 32 || !has_latest) return false;
  qa_q3_snapshot_history_entry *entry =
      qa_q3_snapshot_history_borrow_slot(source->history, (int64_t)number, latest_product);
  if (!entry->valid) return false;
  qa_q3_wire_snapshot *snapshot = &entry->snapshot;
  qa_q3_source_parse_entities *retained = qa_q3_snapshot_history_parse_entities(source->history);
  double parse_entities_number = retained == NULL ? source->parse_entities_number(source->ctx)
      : (double)qa_q3_source_parse_entities_number(retained);
  if (to_int32(parse_entities_number - (double)snapshot->parse_entities_number) >= QA_Q3_MAX_PARSE_ENTITIES) {
    return false;
  }
  memset(out, 0, sizeof *out);
  out->message_number = snapshot->message_number;
  out->server_time = snapshot->server_time;
  out->delta_number = snapshot->delta_number;
  out->flags = snapshot->flags;
  out->server_command_number = snapshot->server_command_number;
  out->parse_entities_number = snapshot->parse_entities_number;
  /* TS: new Uint8Array(32) with snapshot.areaMask set into it. */
  out->area_mask_len = 32;
  memcpy(out->area_mask, snapshot->area_mask, snapshot->area_mask_len <= 32 ? snapshot->area_mask_len : 32);
  qa_q3_player_state_record_copy(&out->player_state, &snapshot->player_state);
  size_t count = snapshot->entity_count;
  if (count > 256) {
    char message[96];
    snprintf(message, sizeof message, "CL_GetSnapshot: truncated %zu entities to 256\n", count);
    source->debug_print(source->ctx, message);
    count = 256;
  }
  if (count > 0) {
    out->entities = malloc(count * sizeof *out->entities);
    if (out->entities == NULL) return false;
    out->entity_count = count;
    for (size_t index = 0; index < count; index++) {
      const qa_q3_entity_state_record *entity = retained == NULL ? &snapshot->entities[index]
          : qa_q3_source_parse_entities_at(retained, snapshot->parse_entities_number + (int64_t)index);
      qa_q3_entity_state_record_copy(&out->entities[index], entity);
    }
  }
  return true;
}
static qa_q3_pres_snapshot_current history_current(void *ctx) {
  return qa_q3_pres_history_snapshot_source_current(ctx);
}
static bool history_read(void *ctx, double number, qa_q3_wire_snapshot *out) {
  return qa_q3_pres_history_snapshot_source_read(ctx, number, out);
}
qa_q3_pres_snapshot_source qa_q3_pres_history_snapshot_source_as_source(qa_q3_pres_history_snapshot_source *source) {
  qa_q3_pres_snapshot_source result = {source, history_current, history_read};
  return result;
}

typedef qa_q3_pres_snapshot_runtime runtime_t;
static qa_q3_pres_client_entity *entity_at(runtime_t *runtime, double number) {
  return qa_q3_pres_client_game_state_entity_at(runtime->state, number);
}
static void reset_entity(runtime_t *runtime, qa_q3_pres_client_entity *entity, double server_time) {
  if (entity->snapshot_time < (double)to_int32(runtime->state->time - 300)) entity->previous_event = 0;
  entity->trail_time = server_time;
  entity->lerp_origin = entity->current_state.origin;
  entity->lerp_angles = entity->current_state.angles;
  if (entity->current_state.e_type == QA_Q3_BASE_ET_PLAYER) {
    runtime->host.reset_player_entity(runtime->host.ctx, entity);
  }
}

static bool initial_snapshot(runtime_t *runtime, const qa_q3_wire_snapshot *input) {
  qa_q3_pres_client_game_state *state = runtime->state;
  if (runtime->has_snap) qa_q3_pres_retail_snapshot_destroy(&runtime->snap_storage);
  runtime->has_snap = false;
  if (!qa_q3_pres_retail_snapshot_from_wire(&runtime->snap_storage, input)) return false;
  runtime->has_snap = true;
  qa_q3_pres_retail_snapshot *snapshot = &runtime->snap_storage;
  state->snap = snapshot;
  qa_q3_base_player_state_to_entity_state(&snapshot->player_state,
      &entity_at(runtime, snapshot->player_state.client_num)->current_state, false);
  if (!qa_q3_pres_build_solid_list(state)) return false;
  runtime->host.execute_server_commands(runtime->host.ctx, (double)snapshot->server_command_number);
  runtime->host.respawn(runtime->host.ctx);
  for (size_t index = 0; index < snapshot->entity_count; index++) {
    qa_q3_base_entity_state *entry = &snapshot->entities[index];
    qa_q3_pres_client_entity *entity = entity_at(runtime, entry->number);
    qa_q3_entity_state_record_copy_from(&entity->current_state, entry);
    entity->interpolate = false;
    entity->current_valid = true;
    reset_entity(runtime, entity, (double)snapshot->server_time);
    runtime->host.check_events(runtime->host.ctx, entity);
  }
  return true;
}

static bool next_snapshot(runtime_t *runtime, const qa_q3_wire_snapshot *input) {
  qa_q3_pres_client_game_state *state = runtime->state;
  qa_q3_pres_retail_snapshot *previous = state->snap;
  if (previous == NULL) qa_range_error("CG_SetNextSnap requires cg.snap");
  if (runtime->has_next_snap) qa_q3_pres_retail_snapshot_destroy(&runtime->next_snap_storage);
  runtime->has_next_snap = false;
  if (!qa_q3_pres_retail_snapshot_from_wire(&runtime->next_snap_storage, input)) return false;
  runtime->has_next_snap = true;
  qa_q3_pres_retail_snapshot *snapshot = &runtime->next_snap_storage;
  state->next_snap = snapshot;
  qa_q3_base_player_state_to_entity_state(&snapshot->player_state,
      &entity_at(runtime, snapshot->player_state.client_num)->next_state, false);
  entity_at(runtime, previous->player_state.client_num)->interpolate = true;
  for (size_t index = 0; index < snapshot->entity_count; index++) {
    qa_q3_base_entity_state *entry = &snapshot->entities[index];
    qa_q3_pres_client_entity *entity = entity_at(runtime, entry->number);
    qa_q3_entity_state_record_copy_from(&entity->next_state, entry);
    entity->interpolate = entity->current_valid && ((entity->current_state.e_flags ^ entry->e_flags) & 4) == 0;
  }
  state->next_frame_teleport =
      ((to_int32(snapshot->player_state.e_flags) ^ to_int32(previous->player_state.e_flags)) & 4) != 0 ||
      snapshot->player_state.client_num != previous->player_state.client_num ||
      ((to_int32((double)snapshot->flags) ^ to_int32((double)previous->flags)) & 4) != 0;
  return qa_q3_pres_build_solid_list(state);
}

static bool transition(runtime_t *runtime) {
  qa_q3_pres_client_game_state *state = runtime->state;
  qa_q3_pres_retail_snapshot *previous = state->snap;
  qa_q3_pres_retail_snapshot *next = state->next_snap;
  if (previous == NULL) qa_common_error(QA_COMMON_ERROR_DROP, "CG_TransitionSnapshot: NULL cg.snap");
  if (next == NULL) qa_common_error(QA_COMMON_ERROR_DROP, "CG_TransitionSnapshot: NULL cg.nextSnap");
  runtime->host.execute_server_commands(runtime->host.ctx, (double)next->server_command_number);
  for (size_t index = 0; index < previous->entity_count; index++) {
    entity_at(runtime, previous->entities[index].number)->current_valid = false;
  }
  state->snap = next;
  qa_q3_pres_client_entity *local = entity_at(runtime, next->player_state.client_num);
  qa_q3_base_player_state_to_entity_state(&next->player_state, &local->current_state, false);
  local->interpolate = false;
  for (size_t index = 0; index < next->entity_count; index++) {
    qa_q3_base_entity_state *entry = &next->entities[index];
    qa_q3_pres_client_entity *entity = entity_at(runtime, entry->number);
    qa_q3_entity_state_record_copy_from(&entity->current_state, &entity->next_state);
    entity->current_valid = true;
    if (!entity->interpolate) reset_entity(runtime, entity, (double)next->server_time);
    entity->interpolate = false;
    runtime->host.check_events(runtime->host.ctx, entity);
    entity->snapshot_time = (double)next->server_time;
  }
  state->next_snap = NULL;
  if (((to_int32(next->player_state.e_flags) ^ to_int32(previous->player_state.e_flags)) & 4) != 0) {
    state->this_frame_teleport = true;
  }
  if (runtime->host.demo_playback || (to_int32(next->player_state.pm_flags) & 4096) != 0 ||
      runtime->host.no_predict || runtime->host.synchronous_clients) {
    runtime->host.transition_player_state(runtime->host.ctx, &next->player_state, &previous->player_state);
  }
  /* The previous retail snapshot (GC-owned in TS) dies here; the next
   * storage becomes the snap storage. Foreign snap pointers (not owned
   * by this runtime) are left alone. */
  if (runtime->has_snap && previous == &runtime->snap_storage) {
    qa_q3_pres_retail_snapshot_destroy(&runtime->snap_storage);
    runtime->has_snap = false;
  }
  if (runtime->has_next_snap && next == &runtime->next_snap_storage) {
    runtime->snap_storage = runtime->next_snap_storage;
    memset(&runtime->next_snap_storage, 0, sizeof runtime->next_snap_storage);
    runtime->has_next_snap = false;
    runtime->has_snap = true;
    state->snap = &runtime->snap_storage;
  }
  return true;
}

/* True with a caller-owned snapshot in out; false is TS null. */
static bool read_next_snapshot(runtime_t *runtime, qa_q3_wire_snapshot *out) {
  qa_q3_pres_client_game_state *state = runtime->state;
  if (state->latest_snapshot_num > (double)to_int32(state->processed_snapshot_num + 1000)) {
    char latest[32], processed[32], message[160];
    fmt_num(latest, sizeof latest, state->latest_snapshot_num);
    fmt_num(processed, sizeof processed, state->processed_snapshot_num);
    snprintf(message, sizeof message,
        "WARNING: CG_ReadNextSnapshot: way out of range, %s > %s", latest, processed);
    runtime->host.warn(runtime->host.ctx, message);
  }
  while (state->processed_snapshot_num < state->latest_snapshot_num) {
    state->processed_snapshot_num = to_int32(state->processed_snapshot_num + 1);
    bool found = runtime->host.source.read(runtime->host.source.ctx, state->processed_snapshot_num, out);
    runtime->host.lagometer_snapshot(runtime->host.ctx, found ? out : NULL);
    if (found) return true;
  }
  return false;
}

static bool process(runtime_t *runtime) {
  qa_q3_pres_client_game_state *state = runtime->state;
  qa_q3_pres_snapshot_current latest = runtime->host.source.current(runtime->host.source.ctx);
  state->latest_snapshot_time = latest.server_time;
  if (latest.number < state->latest_snapshot_num) {
    qa_common_error(QA_COMMON_ERROR_DROP, "CG_ProcessSnapshots: n < cg.latestSnapshotNum");
  }
  state->latest_snapshot_num = latest.number;
  while (state->snap == NULL) {
    qa_q3_wire_snapshot snapshot;
    if (!read_next_snapshot(runtime, &snapshot)) return true;
    bool ok = true;
    if (((int32_t)snapshot.flags & 2) == 0) ok = initial_snapshot(runtime, &snapshot);
    qa_q3_wire_snapshot_destroy(&snapshot);
    if (!ok) return false;
  }
  for (;;) {
    if (state->next_snap == NULL) {
      qa_q3_wire_snapshot snapshot;
      if (!read_next_snapshot(runtime, &snapshot)) break;
      int64_t server_time = snapshot.server_time;
      bool ok = next_snapshot(runtime, &snapshot);
      qa_q3_wire_snapshot_destroy(&snapshot);
      if (!ok) return false;
      if (server_time < state->snap->server_time) {
        qa_common_error(QA_COMMON_ERROR_DROP, "CG_ProcessSnapshots: Server time went backwards");
      }
    }
    qa_q3_pres_retail_snapshot *next = state->next_snap;
    if (next == NULL) qa_range_error("CG_ProcessSnapshots: missing next snapshot");
    if (state->time >= (double)state->snap->server_time && state->time < (double)next->server_time) break;
    if (!transition(runtime)) return false;
  }
  if (state->time < (double)state->snap->server_time) state->time = (double)state->snap->server_time;
  if (state->next_snap != NULL && state->next_snap->server_time <= state->time) {
    qa_common_error(QA_COMMON_ERROR_DROP, "CG_ProcessSnapshots: cg.nextSnap->serverTime <= cg.time");
  }
  return true;
}

/* TS exclusive(): reentrant transitions abort with the TS Error text. */
typedef bool (*operation_fn)(runtime_t *runtime, const qa_q3_wire_snapshot *input);
static bool exclusive(runtime_t *runtime, operation_fn operation, const qa_q3_wire_snapshot *input) {
  if (runtime->transitioning) qa_range_error("Snapshot transition already in progress");
  runtime->transitioning = true;
  bool result = operation(runtime, input);
  runtime->transitioning = false;
  return result;
}
static bool op_initial(runtime_t *runtime, const qa_q3_wire_snapshot *input) {
  return initial_snapshot(runtime, input);
}
static bool op_transition(runtime_t *runtime, const qa_q3_wire_snapshot *input) {
  (void)input;
  return transition(runtime);
}
static bool op_process(runtime_t *runtime, const qa_q3_wire_snapshot *input) {
  (void)input;
  return process(runtime);
}

void qa_q3_pres_snapshot_runtime_init(runtime_t *runtime, qa_q3_pres_client_game_state *state,
    qa_q3_pres_snapshot_host host) {
  memset(runtime, 0, sizeof *runtime);
  runtime->state = state;
  runtime->host = host;
}
void qa_q3_pres_snapshot_runtime_destroy(runtime_t *runtime) {
  if (runtime->has_snap) {
    if (runtime->state != NULL && runtime->state->snap == &runtime->snap_storage) runtime->state->snap = NULL;
    qa_q3_pres_retail_snapshot_destroy(&runtime->snap_storage);
  }
  if (runtime->has_next_snap) {
    if (runtime->state != NULL && runtime->state->next_snap == &runtime->next_snap_storage) {
      runtime->state->next_snap = NULL;
    }
    qa_q3_pres_retail_snapshot_destroy(&runtime->next_snap_storage);
  }
  memset(runtime, 0, sizeof *runtime);
}
bool qa_q3_pres_snapshot_runtime_set_initial_snapshot(runtime_t *runtime, const qa_q3_wire_snapshot *input) {
  return exclusive(runtime, op_initial, input);
}
bool qa_q3_pres_snapshot_runtime_transition_snapshot(runtime_t *runtime) {
  return exclusive(runtime, op_transition, NULL);
}
bool qa_q3_pres_snapshot_runtime_process_snapshots(runtime_t *runtime) {
  return exclusive(runtime, op_process, NULL);
}
bool qa_q3_pres_snapshot_runtime_set_next_snapshot(runtime_t *runtime, const qa_q3_wire_snapshot *input) {
  if (runtime->transitioning) qa_range_error("Snapshot transition already in progress");
  return next_snapshot(runtime, input);
}
