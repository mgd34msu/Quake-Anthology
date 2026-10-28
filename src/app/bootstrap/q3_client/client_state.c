/* Port of src/app/bootstrap/q3-client/client-state.ts.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/q3_client/client_state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/presentation/snapshots.h"
#include "qa/core/common_error.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/client_server_command.h"
#include "qa/network/q3/parse_entities.h"
#include "qa/network/q3/snapshot_history.h"
typedef struct text_entry { double sequence; char *text; } text_entry;
typedef struct ping_entry { double number; double ping; } ping_entry;
struct qa_app_local_q3_client_state {
  qa_app_local_q3_client_bindings bindings;
  qa_q3_client_game_state_storage *game_state;
  qa_q3_wire_command_history *commands;
  qa_q3_source_parse_entities *parse_entities;
  qa_q3_snapshot_history *history;
  qa_q3_pres_history_snapshot_source snapshots;
  qa_q3_server_command_executor *executor;
  text_entry *server_commands; size_t command_count, command_capacity;
  ping_entry *pings; size_t ping_count, ping_capacity;
  bool retired;
  double generation;
  double server_message_sequence;
  double server_command_sequence;
  double last_executed_server_command;
  double client_number;
};
static void drop_message(void *ctx, const char *message) {
  (void)ctx;
  qa_common_error(QA_COMMON_ERROR_DROP, message);
}
static double parse_entities_number(void *ctx) {
  qa_app_local_q3_client_state *s = ctx;
  return qa_q3_source_parse_entities_number(s->parse_entities);
}
static void debug_print(void *ctx, const char *message) {
  qa_app_local_q3_client_state *s = ctx;
  s->bindings.print(s->bindings.ctx, message);
}
static void assert_current(qa_app_local_q3_client_state *s) {
  if (s->retired) qa_range_error("Local Q3 client belongs to a retired gamestate");
  s->bindings.assert_current(s->bindings.ctx);
}
qa_app_local_q3_client_state *qa_app_local_q3_client_state_create(
    const qa_q3_gamestate *initial, qa_app_local_q3_client_bindings bindings) {
  qa_app_local_q3_client_state *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->bindings = bindings;
  s->client_number = (double)initial->client_number;
  s->server_command_sequence = (double)initial->command_sequence;
  s->last_executed_server_command = (double)initial->command_sequence;
  s->generation = 1;
  if (!qa_q3_client_game_state_storage_init(&s->game_state, drop_message, NULL)
      || !qa_q3_wire_command_history_init(&s->commands)
      || !qa_q3_source_parse_entities_init(&s->parse_entities)
      || !qa_q3_snapshot_history_init(&s->history, s->parse_entities)
      || !qa_q3_server_command_executor_init(&s->executor))
    qa_range_error("Out of memory");
  qa_q3_pres_history_snapshot_source_init(&s->snapshots, s->history, s, parse_entities_number, debug_print);
  qa_q3_client_game_state_storage_begin_entries(s->game_state);
  for (size_t i = 0; i < initial->entry_count; i++)
    if (initial->entries[i].kind == QA_Q3_GS_CONFIGSTRING)
      qa_q3_client_game_state_storage_append(s->game_state, initial->entries[i].index, initial->entries[i].value);
  return s;
}
void qa_app_local_q3_client_state_destroy(qa_app_local_q3_client_state *state) {
  if (state == NULL) return;
  qa_q3_server_command_executor_destroy(state->executor);
  qa_q3_snapshot_history_destroy(state->history);
  qa_q3_source_parse_entities_destroy(state->parse_entities);
  qa_q3_wire_command_history_destroy(state->commands);
  qa_q3_client_game_state_storage_destroy(state->game_state);
  for (size_t i = 0; i < state->command_count; i++) free(state->server_commands[i].text);
  free(state->server_commands);
  free(state->pings);
  free(state);
}
qa_q3_client_game_state_storage *qa_app_local_q3_client_state_game_state(qa_app_local_q3_client_state *state) { return state->game_state; }
qa_q3_wire_command_history *qa_app_local_q3_client_state_commands(qa_app_local_q3_client_state *state) { return state->commands; }
double qa_app_local_q3_client_state_client_number(const qa_app_local_q3_client_state *state) { return state->client_number; }
double qa_app_local_q3_client_state_generation(const qa_app_local_q3_client_state *state) { return state->generation; }
double qa_app_local_q3_client_state_server_message_sequence(const qa_app_local_q3_client_state *state) { return state->server_message_sequence; }
double qa_app_local_q3_client_state_server_command_sequence(const qa_app_local_q3_client_state *state) { return state->server_command_sequence; }
double qa_app_local_q3_client_state_last_executed_server_command(const qa_app_local_q3_client_state *state) { return state->last_executed_server_command; }
double qa_app_local_q3_client_state_time(qa_app_local_q3_client_state *state) {
  assert_current(state);
  return qa_q3_pres_history_snapshot_source_current(&state->snapshots).server_time;
}
bool qa_app_local_q3_client_state_current(qa_app_local_q3_client_state *state, qa_q3_wire_snapshot *out) {
  assert_current(state);
  qa_q3_pres_snapshot_current current = qa_q3_pres_history_snapshot_source_current(&state->snapshots);
  return qa_q3_pres_history_snapshot_source_read(&state->snapshots, current.number, out);
}
bool qa_app_local_q3_client_state_read(qa_app_local_q3_client_state *state, double number, qa_q3_wire_snapshot *out) {
  assert_current(state);
  return qa_q3_pres_history_snapshot_source_read(&state->snapshots, number, out);
}
char **qa_app_local_q3_client_state_game_state_strings(qa_app_local_q3_client_state *state, size_t *out_count) {
  assert_current(state);
  char **out = NULL;
  if (!qa_q3_client_game_state_storage_copy_strings(state->game_state, &out, out_count))
    qa_range_error("Out of memory");
  return out;
}
char *qa_app_local_q3_client_state_system_info(qa_app_local_q3_client_state *state) {
  assert_current(state);
  char *out = NULL;
  if (qa_q3_client_game_state_storage_get(state->game_state, 1, &out) && out != NULL) return out;
  free(out);
  out = strdup("");
  if (out == NULL) qa_range_error("Out of memory");
  return out;
}
qa_actor_id qa_app_local_q3_client_state_actor_at(qa_app_local_q3_client_state *state, double number) {
  assert_current(state);
  return state->bindings.actor_at(state->bindings.ctx, number);
}
bool qa_app_local_q3_client_state_has_sound_actor(const qa_app_local_q3_client_state *state) {
  return state->bindings.sound_actor != NULL;
}
bool qa_app_local_q3_client_state_sound_actor(qa_app_local_q3_client_state *state, double number, qa_actor_id *out) {
  assert_current(state);
  if (state->bindings.sound_actor == NULL) return false;
  return state->bindings.sound_actor(state->bindings.ctx, number, out);
}
void qa_app_local_q3_client_state_receive_server_command(qa_app_local_q3_client_state *state,
                                                         double sequence, const char *text) {
  assert_current(state);
  if (sequence != trunc(sequence) || sequence != state->server_command_sequence + 1)
    qa_range_error("Local Q3 reliable command sequence must advance without gaps");
  state->server_command_sequence = sequence;
  if (state->command_count == state->command_capacity) {
    size_t cap = state->command_capacity == 0 ? 16 : state->command_capacity * 2;
    text_entry *next = realloc(state->server_commands, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    state->server_commands = next; state->command_capacity = cap;
  }
  /* TS text.slice(0, 1023) (UTF-16 units; bytes here). */
  size_t len = strlen(text);
  if (len > 1023) len = 1023;
  char *copy = malloc(len + 1);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, text, len);
  copy[len] = '\0';
  state->server_commands[state->command_count].sequence = sequence;
  state->server_commands[state->command_count].text = copy;
  state->command_count++;
  for (size_t i = 0; i < state->command_count; i++) {
    if (state->server_commands[i].sequence == sequence - 64) {
      free(state->server_commands[i].text);
      memmove(&state->server_commands[i], &state->server_commands[i + 1],
              (state->command_count - i - 1) * sizeof *state->server_commands);
      state->command_count--;
      break;
    }
  }
}
void qa_app_local_q3_client_state_receive_snapshot(qa_app_local_q3_client_state *state,
                                                   const qa_q3_wire_snapshot *snapshot, double ping) {
  assert_current(state);
  if ((double)snapshot->message_number <= state->server_message_sequence)
    qa_range_error("Local Q3 snapshot sequence must advance");
  if ((double)snapshot->server_command_number != state->server_command_sequence)
    qa_range_error("Local Q3 snapshot must follow its reliable commands");
  int64_t start = qa_q3_source_parse_entities_number(state->parse_entities);
  for (size_t i = 0; i < snapshot->entity_count; i++) {
    qa_q3_source_entity_state *at = qa_q3_source_parse_entities_at(
        state->parse_entities, qa_q3_source_parse_entities_number(state->parse_entities));
    qa_q3_entity_state_record_copy_from(at, &snapshot->entities[i]);
    qa_q3_source_parse_entities_advance(state->parse_entities);
  }
  qa_q3_server_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.kind = QA_Q3_OP_SNAPSHOT;
  operation.validity.kind = QA_Q3_SNAP_VALID;
  operation.snapshot = *snapshot;
  operation.snapshot.parse_entities_number = start;
  /* Deep-copy entities so publish owns its list independently. */
  operation.snapshot.entities = NULL;
  operation.snapshot.entity_count = snapshot->entity_count;
  if (snapshot->entity_count > 0) {
    operation.snapshot.entities = malloc(snapshot->entity_count * sizeof *operation.snapshot.entities);
    if (operation.snapshot.entities == NULL) qa_range_error("Out of memory");
    memcpy(operation.snapshot.entities, snapshot->entities,
           snapshot->entity_count * sizeof *operation.snapshot.entities);
  }
  qa_q3_snapshot_history_publish(state->history, &operation);
  free(operation.snapshot.entities);
  state->server_message_sequence = (double)snapshot->message_number;
  size_t kept = 0;
  for (size_t i = 0; i < state->ping_count; i++) {
    int32_t age = (int32_t)((double)snapshot->message_number - state->pings[i].number);
    if (age < 32) state->pings[kept++] = state->pings[i];
  }
  state->ping_count = kept;
  if (state->ping_count == state->ping_capacity) {
    size_t cap = state->ping_capacity == 0 ? 16 : state->ping_capacity * 2;
    ping_entry *next = realloc(state->pings, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    state->pings = next; state->ping_capacity = cap;
  }
  state->pings[state->ping_count].number = (double)snapshot->message_number;
  state->pings[state->ping_count].ping = ping;
  state->ping_count++;
}
bool qa_app_local_q3_client_state_snapshot_ping(qa_app_local_q3_client_state *state, double number, double *out) {
  assert_current(state);
  qa_q3_wire_snapshot snapshot;
  if (!qa_app_local_q3_client_state_read(state, number, &snapshot)) return false;
  qa_q3_wire_snapshot_destroy(&snapshot);
  for (size_t i = 0; i < state->ping_count; i++)
    if (state->pings[i].number == number) { *out = state->pings[i].ping; return true; }
  return false;
}
static void thunk_assert(void *ctx) { assert_current(ctx); }
static void thunk_system_info(void *ctx) {
  qa_app_local_q3_client_state *s = ctx;
  char *info = NULL;
  if (!qa_q3_client_game_state_storage_get(s->game_state, 1, &info) || info == NULL) {
    free(info);
    s->bindings.system_info(s->bindings.ctx, "");
    return;
  }
  s->bindings.system_info(s->bindings.ctx, info);
  free(info);
}
static void thunk_map_restart(void *ctx) {
  qa_app_local_q3_client_state *s = ctx;
  qa_q3_wire_command_history_restart(s->commands);
  s->bindings.map_restart(s->bindings.ctx);
}
static void thunk_level_shot(void *ctx) {
  qa_app_local_q3_client_state *s = ctx;
  s->bindings.level_shot(s->bindings.ctx);
}
static bool thunk_local_server(void *ctx) { (void)ctx; return true; }
bool qa_app_local_q3_client_state_get_server_command(qa_app_local_q3_client_state *state, double sequence,
                                                     char ***argv_out, size_t *argc_out) {
  assert_current(state);
  if (sequence <= state->server_command_sequence - 64)
    qa_common_error(QA_COMMON_ERROR_DROP, "CL_GetServerCommand: a reliable command was cycled out");
  if (sequence > state->server_command_sequence)
    qa_common_error(QA_COMMON_ERROR_DROP, "CL_GetServerCommand: requested a command not received");
  const char *command = NULL;
  for (size_t i = 0; i < state->command_count; i++)
    if (state->server_commands[i].sequence == sequence) command = state->server_commands[i].text;
  if (command == NULL)
    qa_common_error(QA_COMMON_ERROR_DROP, "CL_GetServerCommand: command predates local gamestate");
  state->last_executed_server_command = sequence;
  qa_q3_server_command_bindings bindings = {thunk_assert, thunk_system_info, thunk_map_restart,
                                            thunk_level_shot, thunk_local_server, state};
  return qa_q3_server_command_executor_execute(state->executor, command, state->game_state,
                                               &bindings, argv_out, argc_out);
}
void qa_app_local_q3_client_state_retire(qa_app_local_q3_client_state *state) {
  if (state->retired) return;
  state->retired = true;
  state->generation++;
  qa_q3_snapshot_history_clear(state->history);
  qa_q3_source_parse_entities_clear(state->parse_entities);
  qa_q3_wire_command_history_clear(state->commands);
  qa_q3_client_game_state_storage_clear(state->game_state);
  for (size_t i = 0; i < state->command_count; i++) free(state->server_commands[i].text);
  state->command_count = 0;
  state->ping_count = 0;
  qa_q3_server_command_executor_clear(state->executor);
}
