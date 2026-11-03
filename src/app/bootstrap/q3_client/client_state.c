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
#include "qa/foundation/js.h"
#include "qa/app/bootstrap/q3_client.h"
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
  /* TS readonly source (ApplicationQ3ClientSource) and the borrowed results its
   * members and the Q3ClientState members return (valid until the next call). */
  qa_app_q3_client_source source;
  char **game_state_strings; size_t game_state_count;
  char *system_info_text;
  char **server_command_argv; size_t server_command_argc;
  char *configstring_text;
  bool has_record; qa_q3_source_game_state_record record;
};
static void init_source(qa_app_local_q3_client_state *s);
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
  if (s->retired) qa_raise("Error", 0, 0, "Local Q3 client belongs to a retired gamestate");
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
  init_source(s);
  return s;
}
static void free_strings(char ***strings, size_t *count) {
  for (size_t i = 0; i < *count; i++) free((*strings)[i]);
  free(*strings);
  *strings = NULL;
  *count = 0;
}
static void free_record(qa_app_local_q3_client_state *s) {
  if (!s->has_record) return;
  qa_q3_source_game_state_record_destroy(&s->record);
  s->has_record = false;
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
  free_strings(&state->game_state_strings, &state->game_state_count);
  free_strings(&state->server_command_argv, &state->server_command_argc);
  free(state->system_info_text);
  free(state->configstring_text);
  free_record(state);
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
    qa_raise("Error", 0, 0, "Local Q3 reliable command sequence must advance without gaps");
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
    qa_raise("Error", 0, 0, "Local Q3 snapshot sequence must advance");
  if ((double)snapshot->server_command_number != state->server_command_sequence)
    qa_raise("Error", 0, 0, "Local Q3 snapshot must follow its reliable commands");
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
    int32_t age = qa_js_to_int32((double)snapshot->message_number - state->pings[i].number);
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

/* ---- TS this.source (ApplicationQ3ClientSource) ---- */
static size_t source_game_state(void *ctx, const char *const **strings) {
  qa_app_local_q3_client_state *s = ctx;
  char **copied = NULL;
  size_t count = 0;
  copied = qa_app_local_q3_client_state_game_state_strings(s, &count); /* asserts current */
  free_strings(&s->game_state_strings, &s->game_state_count);
  s->game_state_strings = copied;
  s->game_state_count = count;
  *strings = (const char *const *)copied;
  return count;
}
/* getServerCommand: number => this.getServerCommand(number) (shared with the Q3ClientState member). */
static bool source_server_command(void *ctx, double sequence, const char *const **strings, size_t *out_count) {
  qa_app_local_q3_client_state *s = ctx;
  char **argv = NULL;
  size_t argc = 0;
  bool present = qa_app_local_q3_client_state_get_server_command(s, sequence, &argv, &argc);
  free_strings(&s->server_command_argv, &s->server_command_argc);
  if (!present) return false;
  s->server_command_argv = argv;
  s->server_command_argc = argc;
  *strings = (const char *const *)argv;
  *out_count = argc;
  return true;
}
static qa_actor_id source_actor_at(void *ctx, double number) { return qa_app_local_q3_client_state_actor_at(ctx, number); }
static bool source_demo(void *ctx) { (void)ctx; return false; } /* sourceMode: "live" */
static double source_client_number(void *ctx) { return ((qa_app_local_q3_client_state *)ctx)->client_number; }
static double source_time(void *ctx) { return qa_app_local_q3_client_state_time(ctx); }
static double source_server_message_sequence(void *ctx) { return ((qa_app_local_q3_client_state *)ctx)->server_message_sequence; }
static double source_last_executed_server_command(void *ctx) {
  return ((qa_app_local_q3_client_state *)ctx)->last_executed_server_command;
}
static const char *source_system_info(void *ctx) {
  qa_app_local_q3_client_state *s = ctx;
  char *info = qa_app_local_q3_client_state_system_info(s); /* asserts current */
  free(s->system_info_text);
  s->system_info_text = info;
  return info;
}
static bool source_sound_actor(void *ctx, double number, qa_actor_id *out) {
  return qa_app_local_q3_client_state_sound_actor(ctx, number, out);
}
static bool source_snapshot_ping(void *ctx, double number, double *out) {
  return qa_app_local_q3_client_state_snapshot_ping(ctx, number, out);
}
/* commands: { currentNumber, read: number => { assertCurrent(); ... angles: { x, y, z } } }. */
static double source_command_number(void *ctx) {
  return (double)qa_q3_wire_command_history_current_number(((qa_app_local_q3_client_state *)ctx)->commands);
}
static bool source_command_read(void *ctx, double number, qa_q3_base_user_command *out) {
  qa_app_local_q3_client_state *s = ctx;
  assert_current(s);
  qa_q3_wire_user_command command;
  if (!qa_q3_wire_command_history_read(s->commands, (int64_t)number, &command)) return false;
  out->server_time = command.server_time;
  out->angles = (qa_vec3){command.angles[0], command.angles[1], command.angles[2]};
  out->forwardmove = command.forwardmove;
  out->rightmove = command.rightmove;
  out->upmove = command.upmove;
  out->buttons = command.buttons;
  out->weapon = command.weapon;
  return true;
}
/* current: () => { assertCurrent(); return snapshots.current(); }, read likewise. */
static qa_q3_pres_snapshot_current source_current(void *ctx) {
  qa_app_local_q3_client_state *s = ctx;
  assert_current(s);
  return qa_q3_pres_history_snapshot_source_current(&s->snapshots);
}
static bool source_read(void *ctx, double number, qa_q3_wire_snapshot *out) {
  qa_app_local_q3_client_state *s = ctx;
  assert_current(s);
  return qa_q3_pres_history_snapshot_source_read(&s->snapshots, number, out);
}
static void init_source(qa_app_local_q3_client_state *s) {
  qa_app_q3_client_source *source = &s->source;
  memset(source, 0, sizeof *source);
  source->ctx = s;
  source->game_state = source_game_state;
  source->server_command = source_server_command;
  source->actor_at = source_actor_at;
  source->demo = source_demo;
  source->commands.ctx = s;
  source->commands.current_number = source_command_number;
  source->commands.read = source_command_read;
  source->snapshots.ctx = s;
  source->snapshots.current = source_current;
  source->snapshots.read = source_read;
  source->client_number = source_client_number;
  source->time = source_time;
  source->server_message_sequence = source_server_message_sequence;
  source->last_executed_server_command = source_last_executed_server_command;
  source->system_info = source_system_info;
  /* ...(bindings.soundActor === undefined ? {} : { soundActor }) */
  source->sound_actor = s->bindings.sound_actor == NULL ? NULL : source_sound_actor;
  source->snapshot_ping = source_snapshot_ping;
}
const qa_app_q3_client_source *qa_app_local_q3_client_state_source(qa_app_local_q3_client_state *state) { return &state->source; }

/* ---- LocalQ3ClientState as Q3ClientState (compat/qvm/client-state.ts) ---- */
static double connection_generation(void *ctx) { return ((qa_app_local_q3_client_state *)ctx)->generation; }
static double connection_server_message_sequence(void *ctx) { return ((qa_app_local_q3_client_state *)ctx)->server_message_sequence; }
static double connection_last_executed_server_command(void *ctx) {
  return ((qa_app_local_q3_client_state *)ctx)->last_executed_server_command;
}
static double connection_client_number(void *ctx) { return ((qa_app_local_q3_client_state *)ctx)->client_number; }
/* gameState: ClientGameStateStorage get(index) and copySourceRecord(). */
static const char *connection_configstring(void *ctx, double index) {
  qa_app_local_q3_client_state *s = ctx;
  free(s->configstring_text);
  s->configstring_text = NULL;
  if (!qa_q3_client_game_state_storage_get(s->game_state, (int64_t)index, &s->configstring_text)) qa_range_error("Out of memory");
  return s->configstring_text;
}
static qa_qvmx_source_game_state_record connection_record(void *ctx) {
  qa_app_local_q3_client_state *s = ctx;
  free_record(s);
  if (!qa_q3_client_game_state_storage_copy_source_record(s->game_state, &s->record)) qa_range_error("Out of memory");
  s->has_record = true;
  return qa_qvmx_source_game_state_record_from_network(&s->record);
}
/* commands: Q3CommandHistory currentNumber and read (WireUserCommand, angles as an array). */
static bool connection_command_read(void *ctx, double number, qa_qvmx_wire_user_command *out) {
  qa_q3_wire_user_command command;
  if (!qa_q3_wire_command_history_read(((qa_app_local_q3_client_state *)ctx)->commands, (int64_t)number, &command)) return false;
  *out = qa_qvmx_wire_user_command_from_network(&command);
  return true;
}
qa_q3_client_state qa_app_local_q3_client_state_connection(qa_app_local_q3_client_state *state) {
  qa_q3_client_state connection;
  memset(&connection, 0, sizeof connection);
  connection.ctx = state;
  connection.get_generation = connection_generation;
  connection.get_server_message_sequence = connection_server_message_sequence;
  connection.get_last_executed_server_command = connection_last_executed_server_command;
  connection.get_client_number = connection_client_number;
  connection.snapshot_ping = source_snapshot_ping;
  connection.get_server_command = source_server_command;
  connection.game_state = (qa_q3_client_game_state_view){state, connection_configstring, connection_record};
  connection.commands = (qa_q3_client_commands_view){state, source_command_number, connection_command_read};
  return connection;
}
