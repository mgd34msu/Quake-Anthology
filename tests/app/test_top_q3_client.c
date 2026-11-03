/* C checks for src/app/bootstrap/q3-client.ts and q3-client/qvm.ts:
 * tests/app/qvm-command-context.test.ts (qvmClientCommands through the QVM
 * common syscall), tests/app/audit-app-fixes.test.ts (legacyClientCommand
 * null mapping), the CgameStatusView over an abstract QvmCvarServices
 * (q3-client/status.ts, expected values from status.ts semantics), and,
 * over retail baseq3 (QTS_TEST_CORPUS or ~/qfiles), ApplicationQ3Client
 * kind "qvm" with ApplicationQvmClient on vm/ui.qvm and vm/cgame.qvm for a
 * fixed game state and snapshot session: console output, registered
 * resources and draw commands compared with tests/app/oracles/q3_client.txt
 * (bun tests/app/oracles/q3_client_oracle.ts), the shared viewport of the
 * services, the cinematics over ApplicationAssets, prepareVideoGuests and
 * ApplicationVideoRestart, and the remote cgame source.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q3_client.h"
#include "qa/app/bootstrap/q3_seat_client.h"
#include "qa/app/bootstrap/q3_client/qvm.h"
#include "qa/app/bootstrap/q3_client/status.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/common_syscalls.h"
#include "qa/compat/qvm/cvar_syscalls.h"
#include "qa/compat/qvm/legacy_client_abi.h"
#include "qa/compat/qvm/memory.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/foundation/support.h"
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/keys.h"
#include "qa/app/bootstrap/network/remote_q3.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/q3_client/cinematics.h"
#include "qa/app/bootstrap/q3_client/services.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/app/bootstrap/video_restart.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q3/browser_view.h"
#include "qa/network/q3/game_state.h"
#include "qa/settings/config.h"
#include "qa/world/session/session.h"
#include <sys/stat.h>

/* TS Q3GameMemory view of an interpreted allocation (compat/qvm/game_memory.h). */
#define QA_GAME(m) ((const qa_qvm_game_memory[]){qa_qvm_memory_game_memory(m)})

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static uint8_t guest_bytes[4096];
static qa_qvm_memory guest;
static uint8_t word_bytes[64];
static void put_i32(uint8_t *bytes, size_t offset, int32_t value) { memcpy(bytes + offset, &value, 4); }
static int32_t get_i32(const uint8_t *bytes, size_t offset) {
  int32_t value;
  memcpy(&value, bytes + offset, 4);
  return value;
}
static qa_qvm_host_call make_call(const char *role, int32_t code, const int32_t *args, size_t count) {
  memset(word_bytes, 0, sizeof word_bytes);
  put_i32(word_bytes, 0, code);
  for (size_t i = 0; i < count; i++) put_i32(word_bytes, (i + 1) * 4, args[i]);
  qa_qvm_host_call call;
  memset(&call, 0, sizeof call);
  call.kind = "engine";
  call.role = role;
  call.code = code;
  call.guest = &guest;
  call.base.words = word_bytes;
  call.base.words_length = sizeof word_bytes;
  call.base.guest = &guest;
  call.base.memory = guest_bytes;
  call.base.memory_length = sizeof guest_bytes;
  return call;
}

/* ---- qvm-command-context.test.ts ---- */
typedef struct seen_command { char argv[4][64]; size_t argc; qa_command_origin_kind kind; char name[32]; bool has_caller;
                              qa_command_origin_kind caller_kind; qa_seat_id caller_seat; char caller_name[32]; } seen_command;
typedef struct probe_state { int sentinel; seen_command seen[8]; size_t count; } probe_state;
static probe_state probe;
static void probe_handler(void *ctx, qa_command_invocation *command) {
  probe_state *state = ctx;
  CHECK(state == &probe && state->sentinel == 0x5eed);
  if (state->count == 8) return;
  seen_command *seen = &state->seen[state->count++];
  memset(seen, 0, sizeof *seen);
  seen->argc = command->argc < 4 ? command->argc : 4;
  for (size_t i = 0; i < seen->argc; i++) snprintf(seen->argv[i], sizeof seen->argv[i], "%s", command->argv[i]);
  seen->kind = command->source.origin.kind;
  if (command->source.origin.name != NULL) snprintf(seen->name, sizeof seen->name, "%s", command->source.origin.name);
  const qa_command_origin *caller = command->source.origin.caller;
  seen->has_caller = caller != NULL;
  if (caller != NULL) {
    seen->caller_kind = caller->kind;
    seen->caller_seat = caller->seat;
    if (caller->name != NULL) snprintf(seen->caller_name, sizeof seen->caller_name, "%s", caller->name);
  }
}
typedef struct common_state { int sentinel; qa_app_qvm_client_commands *ui; } common_state;
static common_state common_holder = {0xc0de, NULL};
static void common_print(void *ctx, const char *text) { (void)text; CHECK(ctx == &common_holder); }
static double common_milliseconds(void *ctx) { CHECK(ctx == &common_holder); return 0; }
static const char *const *common_arguments(void *ctx, size_t *out_count) { CHECK(ctx == &common_holder); *out_count = 0; return NULL; }
static void ui_execute_now(void *ctx, const char *text) {
  common_state *state = ctx;
  CHECK(state == &common_holder && state->sentinel == 0xc0de);
  qa_app_qvm_client_commands_execute_now(state->ui, text);
}
static void ui_insert(void *ctx, const char *text) {
  common_state *state = ctx;
  CHECK(state == &common_holder);
  qa_app_qvm_client_commands_insert(state->ui, text);
}
static void ui_append(void *ctx, const char *text) {
  common_state *state = ctx;
  CHECK(state == &common_holder);
  qa_app_qvm_client_commands_append(state->ui, text);
}
static void execute_text(qa_qvm_common_services services, int32_t mode, const char *text) {
  qa_qvm_memory_write_string(&guest, 512, text, 256);
  qa_qvm_host_call call = make_call("ui", QA_QVM_UI_IMPORT_UI_CMD_EXECUTETEXT, (const int32_t[]){mode, 512}, 2);
  qa_qvm_host_result result = -1;
  CHECK(qa_qvm_common_syscall(&call, services, &result) && result == 0);
}
static void test_qvm_client_commands(void) {
  memset(&probe, 0, sizeof probe);
  probe.sentinel = 0x5eed;
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "remote-command-context"));
  qa_seat_id seat = qa_identity_owner_seat(&identity, 0);
  qa_client_id client = qa_identity_owner_client(&identity, 7, 2);
  qa_command_context console_context;
  memset(&console_context, 0, sizeof console_context);
  console_context.session = identity.session;
  console_context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_command_context seat_context = console_context;
  seat_context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  seat_context.origin.seat = seat;
  seat_context.origin.client = client;
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  cvar_options.context = console_context;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&cvar_options);
  qa_command_buffer_options buffer_options;
  memset(&buffer_options, 0, sizeof buffer_options);
  buffer_options.dialect = QA_COMMAND_DIALECT_Q3;
  buffer_options.context = console_context;
  buffer_options.cvars = cvars;
  qa_command_buffer *commands = qa_command_buffer_create(&buffer_options);
  CHECK(cvars != NULL && commands != NULL);
  CHECK(qa_command_buffer_register(commands, "probe", probe_handler, &probe, NULL, QA_COMMAND_ENGINE));
  qa_app_qvm_command_target target = qa_app_qvm_command_buffer_target(commands);
  common_holder.ui = qa_app_qvm_client_commands_create(target, &seat_context, "ui");
  qa_qvm_common_services services;
  memset(&services, 0, sizeof services);
  services.role = "ui";
  services.cvars = cvars;
  services.ctx = &common_holder;
  services.print = common_print;
  services.milliseconds = common_milliseconds;
  services.arguments = common_arguments;
  services.commands.ctx = &common_holder;
  services.commands.execute_now = ui_execute_now;
  services.commands.insert = ui_insert;
  services.commands.append = ui_append;
  execute_text(services, 0, "probe now");
  execute_text(services, 1, "probe inserted\n");
  execute_text(services, 2, "probe appended\n");
  qa_app_qvm_client_commands *cgame = qa_app_qvm_client_commands_create(target, &seat_context, "cgame");
  qa_app_qvm_client_commands_append(cgame, "probe cgame\n");
  qa_command_buffer_execute(commands);
  static const char *const words[4] = {"now", "inserted", "appended", "cgame"};
  static const char *const names[4] = {"q3-ui", "q3-ui", "q3-ui", "q3-cgame"};
  CHECK(probe.count == 4);
  for (size_t i = 0; i < 4 && i < probe.count; i++) {
    const seen_command *seen = &probe.seen[i];
    CHECK(seen->argc == 2 && strcmp(seen->argv[0], "probe") == 0 && strcmp(seen->argv[1], words[i]) == 0);
    CHECK(seen->kind == QA_COMMAND_ORIGIN_SCRIPT && strcmp(seen->name, names[i]) == 0);
    CHECK(seen->has_caller && seen->caller_kind == QA_COMMAND_ORIGIN_LOCAL_SEAT && qa_same_seat(seen->caller_seat, seat));
  }
  /* A nested caller keeps its script origin as the caller. */
  qa_command_origin seat_origin = seat_context.origin;
  qa_command_context nested = seat_context;
  nested.origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  nested.origin.name = "caller.cfg";
  nested.origin.caller = &seat_origin;
  qa_app_qvm_client_commands *nested_ui = qa_app_qvm_client_commands_create(target, &nested, "ui");
  qa_app_qvm_client_commands_execute_now(nested_ui, "probe nested");
  CHECK(probe.count == 5);
  if (probe.count == 5) {
    const seen_command *seen = &probe.seen[4];
    CHECK(strcmp(seen->name, "q3-ui") == 0 && seen->caller_kind == QA_COMMAND_ORIGIN_SCRIPT
          && strcmp(seen->caller_name, "caller.cfg") == 0);
  }
  const qa_command_context *source = qa_app_qvm_client_commands_source(nested_ui);
  CHECK(source->origin.caller != NULL && source->origin.caller->caller != NULL
        && source->origin.caller->caller->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT);
  qa_app_qvm_client_commands_destroy(nested_ui);
  qa_app_qvm_client_commands_destroy(cgame);
  qa_app_qvm_client_commands_destroy(common_holder.ui);
  qa_command_buffer_destroy(commands);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);
}

/* ---- audit-app-fixes.test.ts: legacyClientCommand ---- */
static void test_legacy_client_command(void) {
  size_t count = 0;
  const char *mapped[] = {"cs", "20", "x"};
  char **out = qa_qvm_legacy_client_command(mapped, 3, "q3-1.16n-base", &count);
  CHECK(out != NULL && count == 3 && strcmp(out[0], "cs") == 0 && strcmp(out[1], "12") == 0 && strcmp(out[2], "x") == 0);
  qa_qvm_legacy_client_command_free(out, count);
  const char *unmapped[] = {"cs", "15", "x"};
  CHECK(qa_qvm_legacy_client_command(unmapped, 3, "q3-1.16n-base", &count) == NULL && count == 0);
  const char *word[] = {"cs", "abc"};
  CHECK(qa_qvm_legacy_client_command(word, 2, "q3-1.16n-base", &count) == NULL);
  out = qa_qvm_legacy_client_command(unmapped, 3, "q3-modern", &count);
  CHECK(out != NULL && count == 3 && strcmp(out[1], "15") == 0);
  qa_qvm_legacy_client_command_free(out, count);
}

/* ---- CgameStatusView over an abstract QvmCvarServices ---- */
typedef struct table_state { int sentinel; qa_cvar_registry *registry; size_t calls; } table_state;
static table_state table_holder;
static table_state *table_ctx(void *ctx) {
  table_state *state = ctx;
  CHECK(state == &table_holder && state->sentinel == 0x7ab1e);
  state->calls++;
  return state;
}
static bool t_get(void *ctx, const char *name, qa_cvar_snapshot *out) {
  return qa_cvar_registry_get(table_ctx(ctx)->registry, name, out);
}
static bool t_set(void *ctx, const char *name, const char *value, bool force, qa_cvar_snapshot *out) {
  return qa_cvar_registry_set(table_ctx(ctx)->registry, name, value, force, out);
}
static bool t_set_value(void *ctx, const char *name, double value, qa_cvar_snapshot *out) {
  return qa_cvar_registry_set_value(table_ctx(ctx)->registry, name, value, out);
}
static bool t_reset(void *ctx, const char *name, bool force, qa_cvar_snapshot *out) {
  return qa_cvar_registry_reset(table_ctx(ctx)->registry, name, force, out);
}
static bool t_register(void *ctx, const char *name, const char *value, bool has_flags, double flags, qa_cvar_snapshot *out) {
  return qa_cvar_registry_register(table_ctx(ctx)->registry, name, value, has_flags ? flags : 0, out);
}
static double t_bind(void *ctx, const char *name, const char *value, double flags) {
  return (double)qa_cvar_registry_bind_vm(table_ctx(ctx)->registry, name, value, flags);
}
static bool t_read(void *ctx, double handle, qa_cvar_snapshot *out) {
  return qa_cvar_registry_read_vm(table_ctx(ctx)->registry, handle, out);
}
static char *t_info(void *ctx, double flags, double maximum) {
  return qa_cvar_registry_info_string(table_ctx(ctx)->registry, flags, maximum < 0 ? -1 : (int64_t)maximum);
}
typedef struct visible_state { int sentinel; bool visible; } visible_state;
static visible_state visible_holder = {0x515, false};
static bool status_visible(void *ctx) {
  visible_state *state = ctx;
  CHECK(state == &visible_holder && state->sentinel == 0x515);
  return state->visible;
}
static void test_status_view(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "status-view"));
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.session = identity.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  table_holder.sentinel = 0x7ab1e;
  table_holder.registry = qa_cvar_registry_create(&options);
  table_holder.calls = 0;
  qa_app_qvm_cvar_services services = {&table_holder, t_get, t_set, t_set_value, t_reset, t_register, t_bind, t_read, t_info};
  qa_app_cgame_status_view *view = qa_app_cgame_status_view_create(services, status_visible, &visible_holder);
  memset(guest_bytes, 0, sizeof guest_bytes);
  /* trap_Cvar_Register(&vmCvar at 1024, "cg_drawStatus", "1", CVAR_ARCHIVE). */
  qa_qvm_memory_write_string(&guest, 2048, "cg_drawStatus", 64);
  qa_qvm_memory_write_string(&guest, 2112, "1", 8);
  (void)make_call("cgame", QA_QVM_CGAME_IMPORT_CG_CVAR_REGISTER, (const int32_t[]){1024, 2048, 2112, 1}, 4);
  double result = -1;
  CHECK(qa_app_cgame_status_view_syscall(view, word_bytes, sizeof word_bytes, &guest, &result) && result == 0);
  CHECK(table_holder.calls > 0);
  /* Hidden status: the guest record holds the effective "0" (effectiveStatusCvar). */
  CHECK(strcmp((const char *)guest_bytes + 1024 + 16, "0") == 0 && get_i32(guest_bytes, 1024 + 12) == 0);
  int32_t first_revision = get_i32(guest_bytes, 1024 + 4);
  CHECK(first_revision == 1);
  visible_holder.visible = true;
  qa_app_cgame_status_view_refresh(view);
  CHECK(strcmp((const char *)guest_bytes + 1024 + 16, "1") == 0 && get_i32(guest_bytes, 1024 + 12) == 1);
  CHECK(get_i32(guest_bytes, 1024 + 4) == 2);
  /* An unrelated cvar passes through unmasked. */
  qa_qvm_memory_write_string(&guest, 2048, "cg_fov", 64);
  qa_qvm_memory_write_string(&guest, 2112, "90", 8);
  (void)make_call("cgame", QA_QVM_CGAME_IMPORT_CG_CVAR_REGISTER, (const int32_t[]){1400, 2048, 2112, 1}, 4);
  CHECK(qa_app_cgame_status_view_syscall(view, word_bytes, sizeof word_bytes, &guest, &result));
  CHECK(strcmp((const char *)guest_bytes + 1400 + 16, "90") == 0 && get_i32(guest_bytes, 1400 + 12) == 90);
  /* The registry table runs the same syscall as the registry form. */
  qa_qvm_cvar_table table = qa_qvm_registry_cvar_table(table_holder.registry);
  put_i32(word_bytes, 0, QA_QVM_CGAME_IMPORT_CG_CVAR_UPDATE);
  put_i32(word_bytes, 4, 1400);
  qa_qvm_host_result update = -1;
  CHECK(qa_qvm_cvar_table_syscall("cgame", word_bytes, sizeof word_bytes, QA_GAME(&guest), &table, &update) && update == 0);
  qa_app_cgame_status_view_destroy(view);
  qa_cvar_registry_destroy(table_holder.registry);
  qa_identity_owner_destroy(&identity);
}


/* ==== retail: ApplicationQ3Client over baseq3 cgame.qvm/ui.qvm ==== */
static char **lines; static size_t line_count, line_capacity;
static void out(const char *format, ...) __attribute__((format(printf, 1, 2)));
static bool oracle_closed; /* lines after the oracle's last section are C-only checks */
static void out(const char *format, ...) {
  if (oracle_closed) return;
  char buffer[4096];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  if (line_count == line_capacity) {
    line_capacity = line_capacity == 0 ? 256 : line_capacity * 2;
    lines = realloc(lines, line_capacity * sizeof *lines);
  }
  lines[line_count++] = strdup(buffer);
}
static const char *fx(double value, char *buffer) {
  qa_js_to_fixed(value == 0 ? 0 : value, 3, buffer, 160);
  return buffer;
}
#define F(value) fx((value), (char[160]){0})
/* text.replace(/\\/g, "\\\\").replace(/\n/g, "\\n") */
static const char *escaped(const char *text, char *buffer, size_t size) {
  size_t n = 0;
  for (const char *p = text; *p != '\0' && n + 3 < size; p++) {
    if (*p == '\\') { buffer[n++] = '\\'; buffer[n++] = '\\'; }
    else if (*p == '\n') { buffer[n++] = '\\'; buffer[n++] = 'n'; }
    else buffer[n++] = *p;
  }
  buffer[n] = '\0';
  return buffer;
}

/* The fixture (mirrored in tests/app/oracles/q3_client_oracle.ts). */
typedef struct configstring { int index; const char *value; } configstring;
static const configstring fixture_strings[] = {
  {0, "\\sv_hostname\\fixture\\g_gametype\\0\\sv_maxclients\\8\\timelimit\\0\\fraglimit\\20\\dmflags\\0\\version\\Q3 1.32 fixture\\protocol\\68\\mapname\\q3dm1"},
  {1, "\\sv_serverid\\7\\sv_pure\\0\\timescale\\1\\g_syncronousClients\\0\\sv_cheats\\0"},
  {20, "baseq3-1"},
  {21, "0"},
  {33, "maps/q3dm1.bsp"},
  {544, "n\\Fixture\\t\\0\\model\\sarge\\hmodel\\sarge\\c1\\4\\c2\\5\\hc\\100\\w\\0\\l\\0\\tt\\0\\tl\\0"},
};
static const double snapshot_times[] = {1000, 1050, 1100, 1150, 1200};
#define SNAPSHOT_TIME_COUNT 5
#define FRAMES 4

typedef struct fixture {
  int sentinel;
  qa_identity_owner *ids;
  double clock;
  double snapshot;
  qa_q3_client_game_state_storage *game_state;
  char **strings; size_t string_count; /* copyStrings until the next call */
  char *game_state_text;             /* gameState.get until the next call */
  char **prints; size_t print_count, print_capacity;
} fixture;
#define FIXTURE_SENTINEL 0x3c11e
static fixture fx_holder;
static fixture *fix(void *ctx) {
  fixture *f = ctx;
  CHECK(f == &fx_holder && f->sentinel == FIXTURE_SENTINEL);
  return f;
}
static void record(fixture *f, const char *prefix, const char *text) {
  if (f->print_count == f->print_capacity) {
    f->print_capacity = f->print_capacity == 0 ? 64 : f->print_capacity * 2;
    f->prints = realloc(f->prints, f->print_capacity * sizeof *f->prints);
  }
  size_t length = strlen(prefix) + strlen(text) + 1;
  f->prints[f->print_count] = malloc(length);
  snprintf(f->prints[f->print_count++], length, "%s%s", prefix, text);
}
static void flush_prints(fixture *f, const char *label) {
  for (size_t i = 0; i < f->print_count; i++) {
    char text[4096];
    out("%s print %s", label, escaped(f->prints[i], text, sizeof text));
    free(f->prints[i]);
  }
  f->print_count = 0;
}
static double snapshot_time(const fixture *f) {
  size_t index = f->snapshot < 1 ? 0 : (size_t)f->snapshot - 1;
  return snapshot_times[index < SNAPSHOT_TIME_COUNT ? index : SNAPSHOT_TIME_COUNT - 1];
}
static void fixture_player_state(double server_time, qa_q3_player_state_record *state) {
  memset(state, 0, sizeof *state);
  qa_q3_player_state_record_init(state, QA_Q3_PRODUCT_BASEQ3, 0, 2, 0);
  state->command_time = (int32_t)server_time;
  state->origin = (qa_vec3){-352, 1112, 48};
  state->viewangles = (qa_vec3){0, 90, 0};
  state->viewheight = 26;
  state->client_num = 0;
  state->speed = 320;
  state->gravity = 800;
  state->stats[0] = 100;
  state->stats[2] = (1 << 1) | (1 << 2);
  state->stats[6] = 100;
  state->ammo[2] = 100;
  state->persistant[3] = 0;
}
static qa_q3_wire_user_command wire_command(double number) {
  qa_q3_wire_user_command command;
  memset(&command, 0, sizeof command);
  size_t index = (size_t)(number < SNAPSHOT_TIME_COUNT ? number : SNAPSHOT_TIME_COUNT) - 1;
  command.server_time = (int32_t)snapshot_times[index];
  command.weapon = 2;
  return command;
}
/* Q3ClientState. */
static double cs_generation(void *ctx) { (void)fix(ctx); return 1; }
static double cs_message(void *ctx) { (void)fix(ctx); return 1; }
static double cs_executed(void *ctx) { (void)fix(ctx); return 0; }
static double cs_client_number(void *ctx) { (void)fix(ctx); return 0; }
static bool cs_ping(void *ctx, double number, double *out_ping) { (void)number; (void)fix(ctx); *out_ping = 0; return true; }
static bool cs_server_command(void *ctx, double sequence, const char *const **tokens, size_t *count) {
  (void)sequence; (void)tokens; (void)fix(ctx); *count = 0; return false;
}
static const char *cs_get(void *ctx, double index) {
  fixture *f = fix(ctx);
  free(f->game_state_text);
  f->game_state_text = NULL;
  CHECK(qa_q3_client_game_state_storage_get(f->game_state, (int64_t)index, &f->game_state_text));
  return f->game_state_text;
}
static qa_q3_source_game_state_record held_record;
static qa_qvmx_source_game_state_record cs_record(void *ctx) {
  fixture *f = fix(ctx);
  qa_q3_source_game_state_record_destroy(&held_record);
  CHECK(qa_q3_client_game_state_storage_copy_source_record(f->game_state, &held_record));
  return qa_qvmx_source_game_state_record_from_network(&held_record);
}
static double cs_command_number(void *ctx) { return fix(ctx)->snapshot; }
static bool cs_command_read(void *ctx, double number, qa_qvmx_wire_user_command *out_command) {
  fixture *f = fix(ctx);
  if (number < 1 || number > f->snapshot) return false;
  qa_q3_wire_user_command command = wire_command(number);
  *out_command = qa_qvmx_wire_user_command_from_network(&command);
  return true;
}
/* ApplicationQ3ClientSource. */
static void free_strings(fixture *f) {
  for (size_t i = 0; i < f->string_count; i++) free(f->strings[i]);
  free(f->strings);
  f->strings = NULL;
  f->string_count = 0;
}
static size_t src_game_state(void *ctx, const char *const **strings) {
  fixture *f = fix(ctx);
  free_strings(f);
  CHECK(qa_q3_client_game_state_storage_copy_strings(f->game_state, &f->strings, &f->string_count));
  *strings = (const char *const *)f->strings;
  return f->string_count;
}
static bool src_server_command(void *ctx, double sequence, const char *const **strings, size_t *count) {
  (void)sequence; (void)strings; (void)fix(ctx); *count = 0; return false;
}
static qa_actor_id src_actor_at(void *ctx, double number) {
  fixture *f = fix(ctx);
  return qa_identity_owner_actor(f->ids, (int64_t)number, 0);
}
static bool src_demo(void *ctx) { (void)fix(ctx); return false; }
static double src_client_number(void *ctx) { (void)fix(ctx); return 0; }
static double src_time(void *ctx) { return snapshot_time(fix(ctx)); }
static double src_message(void *ctx) { (void)fix(ctx); return 1; }
static double src_executed(void *ctx) { (void)fix(ctx); return 0; }
static const char *src_system_info(void *ctx) {
  const char *value = cs_get(ctx, 1);
  return value != NULL ? value : "";
}
static bool src_ping(void *ctx, double number, double *out_ping) { (void)number; (void)fix(ctx); *out_ping = 0; return true; }
static double src_commands_number(void *ctx) { return fix(ctx)->snapshot; }
static bool src_commands_read(void *ctx, double number, qa_q3_base_user_command *out_command) {
  fixture *f = fix(ctx);
  if (number < 1 || number > f->snapshot) return false;
  qa_q3_wire_user_command command = wire_command(number);
  memset(out_command, 0, sizeof *out_command);
  out_command->server_time = command.server_time;
  out_command->weapon = command.weapon;
  return true;
}
static qa_q3_pres_snapshot_current src_current(void *ctx) {
  fixture *f = fix(ctx);
  qa_q3_pres_snapshot_current current = {f->snapshot, snapshot_time(f)};
  return current;
}
static bool src_read(void *ctx, double number, qa_q3_wire_snapshot *out_snapshot) {
  fixture *f = fix(ctx);
  if (number < 1 || number > f->snapshot || number > SNAPSHOT_TIME_COUNT) return false;
  memset(out_snapshot, 0, sizeof *out_snapshot);
  out_snapshot->message_number = (int64_t)number;
  out_snapshot->server_time = (int64_t)snapshot_times[(size_t)number - 1];
  out_snapshot->delta_number = (int64_t)number - 1;
  out_snapshot->area_mask_len = 32;
  fixture_player_state(snapshot_times[(size_t)number - 1], &out_snapshot->player_state);
  return true;
}
/* Options callbacks. */
static bool opt_save_font(void *ctx) { (void)fix(ctx); return false; }
static qa_rect opt_viewport(void *ctx) { (void)fix(ctx); return (qa_rect){0, 0, 640, 480}; }
static double opt_now(void *ctx) { return fix(ctx)->clock; }
static void opt_reliable(void *ctx, const char *text) { record(fix(ctx), "reliable ", text); }
static void opt_console(void *ctx, const char *text) { record(fix(ctx), "console ", text); }
static void opt_print(void *ctx, const char *text) { record(fix(ctx), "", text); }
static qa_app_qvm_client_state_info opt_client_state(void *ctx) {
  (void)fix(ctx);
  qa_app_qvm_client_state_info info = {8, 0, 0, "localhost", ""};
  return info;
}
/* Input and audio hosts. */
static void act_quit(void *ctx) { (void)fix(ctx); }
static void act_execute(void *ctx, const char *name, const char *const *list, size_t count, const qa_seat_id *seat,
                        const qa_command_context *context) {
  (void)name; (void)list; (void)count; (void)seat; (void)context; (void)fix(ctx);
}
static void act_print(void *ctx, const char *text) { (void)text; (void)fix(ctx); }
static double input_now(void *ctx) { return fix(ctx)->clock; }
static qa_app_sim_player_view no_player_view(void *ctx, qa_actor_id actor) {
  (void)actor; (void)fix(ctx);
  qa_app_sim_player_view view;
  memset(&view, 0, sizeof view);
  view.angles = (qa_vec3){0, 90, 0};
  view.view_height = 26;
  return view;
}
static void audio_print(void *ctx, const char *text) { (void)text; (void)fix(ctx); }
static void keys_print(void *ctx, const char *text) { (void)text; (void)fix(ctx); }
/* Q3BrowserHost: an empty browser. */
static qa_q3_browser_list_state b_list(void *ctx, int64_t source) {
  (void)source; (void)fix(ctx);
  qa_q3_browser_list_state state = {NULL, 0, false, 0, 0};
  return state;
}
static bool b_resolve(void *ctx, const char *text, qa_q3_mirror_network_address *address) {
  (void)text; (void)address; (void)fix(ctx); return false;
}
static void b_add(void *ctx, int64_t source, const qa_q3_mirror_network_address *address) { (void)source; (void)address; (void)fix(ctx); }
static void b_thunk(void *ctx) { (void)fix(ctx); }
static void b_master(void *ctx, int64_t source, const char *remote, int64_t protocol, const char *const *keywords, size_t count,
                     qa_q3_browser_assert_fn assert_current, void *assert_ctx) {
  (void)source; (void)remote; (void)protocol; (void)keywords; (void)count; (void)assert_current; (void)assert_ctx; (void)fix(ctx);
}
static bool b_load(void *ctx, qa_q3_browser_assert_fn assert_current, void *assert_ctx, qa_q3_browser_cache_view *view) {
  (void)assert_current; (void)assert_ctx; (void)view; (void)fix(ctx); return false;
}
static void b_save(void *ctx, const qa_q3_browser_cache_view *view) { (void)view; (void)fix(ctx); }
static double b_now(void *ctx) { return fix(ctx)->clock; }
static double b_max_ping(void *ctx) { (void)fix(ctx); return 800; }
static double b_resend(void *ctx) { (void)fix(ctx); return 1000; }
static void b_print(void *ctx, const char *text) { (void)text; (void)fix(ctx); }

/* ---- frame dump ---- */
static void image_key(const qa_renderer_image *image, char *buffer, size_t size) {
  const char *source = image->source_is_resource ? image->source_resource.requested_path : image->source_name;
  snprintf(buffer, size, "%s:%sx%s:%s", QA_JS_NUMBER(image->ordinal), QA_JS_NUMBER(image->width), QA_JS_NUMBER(image->height),
           source != NULL ? source : "");
}
static const char *operation_kind(qa_image_resource_operation_kind kind) {
  switch (kind) {
    case QA_IMAGE_OP_CREATE: return "create-image";
    case QA_IMAGE_OP_UPDATE: return "update-image";
    case QA_IMAGE_OP_RELEASE: return "release-image";
    default: return "texture-mode";
  }
}
static const char *render_kind(qa_render_operation_kind kind) {
  static const char *const names[] = {"draw", "object-opacity", "q2-fog", "depth-atlas", "depth-range", "cull", "polygon-offset",
                                      "disable-portal-clip", "sky-side", "shadow-volume", "shadow-finish"};
  return (size_t)kind < sizeof names / sizeof names[0] ? names[kind] : "other";
}
static void dump_batch(const char *prefix, const qa_draw_batch *batch) {
  double p = 0, c = 0, t = 0;
  size_t count = batch->texturing_is_pair ? batch->pair_vertex_count : batch->vertex_count;
  for (size_t i = 0; i < count; i++) {
    const qa_render_vertex *v = batch->texturing_is_pair ? &batch->pair_vertices[i].vertex : &batch->vertices[i];
    p += v->position.x + v->position.y + v->position.z + v->position.w;
    c += v->color.x + v->color.y + v->color.z + v->color.w;
    t += v->tex_coord.x + v->tex_coord.y;
  }
  out("%s v=%zu i=%zu p=%s c=%s t=%s depthWrite=%s", prefix, count, batch->indices.count, F(p), F(c), F(t),
      batch->state.depth_write ? "true" : "false");
}
static void dump_command(const char *prefix, const qa_render_command *command) {
  char key[512];
  switch (command->kind) {
    case QA_RENDER_COMMAND_DRAW_BUFFER:
      out("%s draw-buffer %s clear=%s", prefix, command->buffer, command->clear ? "true" : "false");
      return;
    case QA_RENDER_COMMAND_IMAGE_RESOURCE:
      if (command->operation.kind == QA_IMAGE_OP_TEXTURE_MODE) snprintf(key, sizeof key, "none");
      else image_key(&command->operation.image, key, sizeof key);
      out("%s image-resource %s %s", prefix, operation_kind(command->operation.kind), key);
      return;
    case QA_RENDER_COMMAND_SET_COLOR:
      out("%s set-color %s %s %s %s", prefix, F(command->color.x), F(command->color.y), F(command->color.z), F(command->color.w));
      return;
    case QA_RENDER_COMMAND_STRETCH_PIC:
      image_key(&command->image, key, sizeof key);
      out("%s stretch-pic %s %s %s %s %s %s %s %s %s", prefix, F(command->rect.x), F(command->rect.y), F(command->rect.width),
          F(command->rect.height), F(command->uv.s1), F(command->uv.t1), F(command->uv.s2), F(command->uv.t2), key);
      return;
    case QA_RENDER_COMMAND_VIEW: {
      const qa_render_view *view = &command->view;
      qa_rect viewport = view->state.viewport;
      const char *clear = view->state.clear == NULL ? "null" : view->state.clear->color == NULL ? "depth" : "color";
      out("%s view %s %s %s %s clear=%s ops=%zu", prefix, F(viewport.x), F(viewport.y), F(viewport.width), F(viewport.height), clear,
          view->operation_count);
      for (size_t i = 0; i < view->operation_count; i++) {
        const qa_render_operation *operation = &view->operations[i];
        out("%s.%zu %s batches=%zu", prefix, i, render_kind(operation->kind), operation->batch_count);
        for (size_t b = 0; b < operation->batch_count; b++) {
          char batch_prefix[256];
          snprintf(batch_prefix, sizeof batch_prefix, "%s.%zu.%zu", prefix, i, b);
          dump_batch(batch_prefix, &operation->batches[b]);
        }
      }
      return;
    }
    default: out("%s swap-buffers", prefix);
  }
}
static void dump_resources(qa_app_application_q3_client *client, const char *label) {
  qa_q3_pres_q3_renderer_resources *resources = qa_app_application_q3_client_resource_diagnostics(client);
  qa_q3_pres_registered_model *models = NULL;
  size_t model_count = 0;
  CHECK(qa_q3_pres_q3_renderer_resources_registered_models(resources, &models, &model_count));
  static const char *const kinds[] = {"default", "model", "inline"};
  for (size_t i = 0; i < model_count; i++)
    out("%s model %s %s %s", label, QA_JS_NUMBER(models[i].handle), models[i].path, kinds[models[i].model.kind]);
  free(models);
  qa_q3_pres_registered_skin *skins = NULL;
  size_t skin_count = 0;
  CHECK(qa_q3_pres_q3_renderer_resources_registered_skins(resources, &skins, &skin_count));
  for (size_t i = 0; i < skin_count; i++)
    out("%s skin %s %s %zu", label, QA_JS_NUMBER(skins[i].handle), skins[i].path, skins[i].skin->surface_count);
  free(skins);
}

static qa_app_options parsed_options(const char *root, const char *store) {
  const char *argv[] = {"--game", "q3-baseq3", "--map", "q3dm1", "--content-root", root};
  qa_content_root_search search = {NULL, NULL};
  qa_app_application_command command = qa_app_parse_application_command(argv, 6, search, NULL);
  CHECK(strcmp(command.kind, "run") == 0);
  command.options.user_content_root = strdup(store);
  return command.options;
}
/* input.guestCommands (append's dialect is TS undefined). */
static qa_app_application_input *guest_input_holder;
static qa_app_application_input *guest_of(void *ctx) {
  CHECK(ctx == guest_input_holder);
  return ctx;
}
static double guest_execute_now(void *ctx, const char *text, const qa_command_context *source) {
  return qa_app_application_input_guest_execute_now(guest_of(ctx), text, source);
}
static void guest_insert(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_application_input_guest_insert(guest_of(ctx), text, source);
}
static void guest_append(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_application_input_guest_append(guest_of(ctx), text, source, -1);
}
/* ApplicationQ3Client as WorldSeatPresentation's q3Client (q3_seat_client.h): every member forwards to the client, and
 * frame() runs the presentation's effect and camera closures with their own (sentinel) context. */
typedef struct seat_hooks_state { int sentinel; int effects, cameras; } seat_hooks_state;
static seat_hooks_state *seat_hooks_of(void *ctx) {
  seat_hooks_state *state = ctx;
  if (state->sentinel != 0x5ea7) { fprintf(stderr, "wrong seat hooks ctx\n"); abort(); }
  return state;
}
static qa_app_seat_q3_effects seat_hook_effects(void *ctx, qa_scene_camera camera, qa_source_scene_order *source) {
  (void)camera; (void)source;
  seat_hooks_of(ctx)->effects++;
  qa_app_seat_q3_effects none;
  memset(&none, 0, sizeof none);
  return none;
}
static qa_scene_camera seat_hook_camera(void *ctx, qa_scene_camera camera) {
  seat_hooks_of(ctx)->cameras++;
  return camera;
}
static void test_seat_view(qa_app_application_q3_client *client, qa_seat_id seat, fixture *f) {
  qa_app_seat_q3_client view = qa_app_application_q3_client_seat(client);
  CHECK(view.ctx == client && strcmp(view.kind, "qvm") == 0 && qa_same_seat(view.seat, seat));
  CHECK(qa_same_actor(view.actor, qa_app_application_q3_client_options_of(client)->local->player.actor));
  CHECK(view.cvars(view.ctx) == qa_app_application_q3_client_cvars(client));
  qa_scene_camera direct = qa_app_application_q3_client_camera(client), seen = view.camera(view.ctx);
  CHECK(memcmp(&direct, &seen, sizeof direct) == 0);
  qa_scene_camera weapon = qa_app_application_q3_client_supplemental_weapon_camera(client, direct);
  qa_scene_camera weapon_seen = view.supplemental_weapon_camera(view.ctx, direct);
  CHECK(memcmp(&weapon, &weapon_seen, sizeof weapon) == 0);
  bool visible = false, warning = false, visible_seen = true, warning_seen = true;
  qa_app_application_q3_client_weapon_hud_view(client, &visible, &warning);
  view.weapon_hud_view(view.ctx, &visible_seen, &warning_seen);
  CHECK(visible == visible_seen && warning == warning_seen);
  bool shared = false, shared_seen = false;
  CHECK(qa_app_application_q3_client_shared_equipment_view_visible(client, &shared)
        == view.shared_equipment_view_visible(view.ctx, &shared_seen) && shared == shared_seen);
  size_t held = 0, held_seen = 0, bodies = 0, bodies_seen = 0;
  CHECK(qa_app_application_q3_client_shared_held_weapons(client, &held) == view.shared_held_weapons(view.ctx, &held_seen) && held == held_seen);
  CHECK(qa_app_application_q3_client_shared_bodies(client, &bodies) == view.shared_bodies(view.ctx, &bodies_seen) && bodies == bodies_seen);
  f->snapshot = 4;
  qa_rect viewport = {0, 0, 640, 480};
  view.prepare(view.ctx, FRAMES + 1, viewport, NULL, 0, true, NULL, 0, true, false);
  seat_hooks_state state = {0x5ea7, 0, 0};
  qa_app_seat_q3_frame_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = &state; hooks.effects = seat_hook_effects; hooks.camera = seat_hook_camera;
  const qa_render_command *commands = NULL;
  size_t command_count = 0;
  CHECK(view.frame(view.ctx, &hooks, &commands, &command_count));
  CHECK(commands != NULL && command_count > 0 && state.effects > 0 && state.cameras > 0);
  qa_app_application_q3_client_prepare(client, FRAMES + 1, &viewport, NULL, 0, true, NULL, 0, true, false);
  qa_render_frame direct_frame = qa_app_application_q3_client_frame(client, NULL, NULL, NULL);
  CHECK(direct_frame.command_count == command_count);
  flush_prints(f, "seat");
}

static void close_client(void *ctx) { qa_app_application_q3_client_close(ctx); }
static void shutdown_client(void *ctx) { qa_app_application_q3_client_shutdown(ctx); }

/* prepareVideoGuests: the seat's viewport and publication closures. */
typedef struct video_seat { int sentinel; qa_app_application_q3_client *published; size_t viewports; } video_seat;
static video_seat video_seat_holder;
static qa_rect seat_viewport(void *ctx, qa_sdl_window *window) {
  video_seat *seat = ctx;
  CHECK(seat == &video_seat_holder && seat->sentinel == 0x51ee7 && window != NULL);
  seat->viewports++;
  return (qa_rect){0, 0, 640, 480};
}
static void seat_publish(void *ctx, qa_app_application_q3_client *client) {
  video_seat *seat = ctx;
  CHECK(seat == &video_seat_holder && seat->sentinel == 0x51ee7);
  seat->published = client;
}
typedef struct video_assert { int sentinel; size_t calls; bool reject; } video_assert;
static video_assert video_assert_holder;
static void video_current(void *ctx) {
  video_assert *state = ctx;
  CHECK(state == &video_assert_holder && state->sentinel == 0xa55e);
  state->calls++;
  if (state->reject) qa_raise("Error", 0, 0, "Guest video source changed during preparation");
}
/* ApplicationVideoRestart host over the prepared guests. */
typedef struct video_host {
  int sentinel;
  qa_app_application_input *input;
  qa_app_application_q3_client *client;
  size_t prepares, windows, published, settled, prints, failed;
  char last_print[256];
  bool fail_publish;
} video_host;
static video_host video_host_holder;
static video_host *vh(void *ctx) {
  video_host *host = ctx;
  CHECK(host == &video_host_holder && host->sentinel == 0x1d30);
  return host;
}
static qa_app_capture *vh_capture(void *ctx) { (void)vh(ctx); return NULL; }
static bool vh_prepare(void *ctx, qa_app_prepared_video_presentation *out_presentation);
static qa_app_native_renderer *video_renderer;
static void vh_publish_window(void *ctx, qa_sdl_window *window) {
  video_host *host = vh(ctx);
  CHECK(window != NULL);
  host->windows++;
  qa_app_application_input_publish_window(host->input, qa_app_input_window_from_sdl(window));
}
static void vh_published(void *ctx, const char *kind) {
  video_host *host = vh(ctx);
  CHECK(strcmp(kind, "cpu") == 0);
  host->published++;
  if (host->fail_publish) qa_raise("Error", 0, 0, "publication rejected");
}
static void vh_settled(void *ctx) { vh(ctx)->settled++; }
static void vh_print(void *ctx, const char *text, const qa_command_context *source) {
  video_host *host = vh(ctx);
  (void)source;
  host->prints++;
  snprintf(host->last_print, sizeof host->last_print, "%s", text);
}
static void vh_failed(void *ctx, const qa_caught_error *error) { (void)error; vh(ctx)->failed++; }
static bool vh_prepare(void *ctx, qa_app_prepared_video_presentation *out_presentation) {
  video_host *host = vh(ctx);
  host->prepares++;
  qa_app_video_guest_seat seat = {host->client, &video_seat_holder, seat_viewport, seat_publish};
  return qa_app_prepare_video_guests(host->input, video_renderer, &seat, 1, video_current, &video_assert_holder, out_presentation);
}


/* ApplicationQ3Cinematics over the client's ApplicationQ3Assets (retail video/idlogo.RoQ). */
typedef struct cinematic_state {
  int sentinel;
  double now;
  size_t queued, stopped, paused, picture_draws, applied;
  qa_material_picture picture;
} cinematic_state;
static cinematic_state cin_holder;
static cinematic_state *cin(void *ctx) {
  cinematic_state *state = ctx;
  CHECK(state == &cin_holder && state->sentinel == 0xc1e);
  return state;
}
static double cin_now(void *ctx) { return cin(ctx)->now; }
static void cin_queue(void *ctx, const qa_audio_stream_target *target, const qa_stream_pcm *pcm) {
  (void)target; (void)pcm; cin(ctx)->queued++;
}
static void cin_stop(void *ctx, const char *id) { (void)id; cin(ctx)->stopped++; }
static void cin_pause(void *ctx, const char *id, bool paused) { (void)id; (void)paused; cin(ctx)->paused++; }
static void cin_set_color(void *ctx, const qa_vec4 *color) { (void)color; (void)cin(ctx); }
static void cin_stretch(void *ctx, qa_rect rect, qa_text_texture_rect uv, const qa_picture_asset *picture) {
  (void)rect; (void)uv;
  cinematic_state *state = cin(ctx);
  if (picture->kind == QA_PICTURE_MATERIAL) { state->picture_draws++; state->picture = picture->material; }
}
static void cin_apply(void *ctx, qa_image_resource_operation operation) {
  (void)operation; cin(ctx)->applied++;
}
typedef struct cinematic_capture { qa_app_application_q3_cinematics *c; qa_save_value *value; } cinematic_capture;
static void capture_body(void *ctx) {
  cinematic_capture *capture = ctx;
  capture->value = qa_app_application_q3_cinematics_capture_checkpoint(capture->c);
}
static void test_cinematics(qa_app_application_q3_assets *media, qa_app_application_assets *assets, qa_seat_id seat) {
  memset(&cin_holder, 0, sizeof cin_holder);
  cin_holder.sentinel = 0xc1e;
  qa_cinematic_mixer mixer = {&cin_holder, cin_queue, cin_stop, cin_pause, NULL, NULL};
  qa_app_application_q3_cinematics *c = qa_app_application_q3_cinematics_create(media, mixer, seat, cin_now, &cin_holder, NULL);
  qa_rect rect = {10, 20, 320, 240};
  CHECK(qa_app_application_q3_cinematics_play_guest(c, "missing-movie", rect, 0) == -1);
  size_t allocated = qa_app_application_assets_images(assets)->allocation_count;
  double handle = qa_app_application_q3_cinematics_play_guest(c, "idlogo", rect, 0);
  CHECK(handle == 0);
  /* The movie image is allocated in the ApplicationAssets registry. */
  CHECK(qa_app_application_assets_images(assets)->allocation_count == allocated + 1);
  CHECK(qa_app_application_q3_cinematics_play_guest(c, "idlogo", rect, 0) == handle);
  for (int step = 1; step <= 4; step++) {
    cin_holder.now = step * 100;
    CHECK(qa_app_application_q3_cinematics_run_guest(c, handle) == 1);
  }
  qa_draw2d draw;
  memset(&draw, 0, sizeof draw);
  draw.commands.ctx = &cin_holder;
  draw.commands.target = (qa_rect){0, 0, 640, 480};
  draw.commands.set_color = cin_set_color;
  draw.commands.stretch_pixels = cin_stretch;
  draw.space = QA_COORD_PIXELS;
  qa_app_application_q3_cinematics_draw_guest(c, handle, &draw);
  CHECK(cin_holder.picture_draws == 1 && cin_holder.picture.compiled != NULL);
  CHECK(cin_holder.picture.name != NULL && strstr(cin_holder.picture.name, "q3-cinematic:") == cin_holder.picture.name);
  if (cin_holder.picture.compiled != NULL) {
    /* The implicit picture material binds the playback as a video map. */
    const qa_compiled_material *compiled = cin_holder.picture.compiled;
    CHECK(compiled->registered.stage_count >= 1);
    if (compiled->registered.stage_count >= 1) {
      const qa_mat_registered_stage *stage = &compiled->registered.stages[0];
      CHECK(strcmp(stage->kind, "loaded") == 0 && strcmp(stage->binding.kind, "video") == 0);
      const qa_dynamic_image_source *source = &stage->binding.source.source;
      CHECK(source->resolve != NULL);
      if (source->resolve != NULL) {
        qa_renderer_image image = source->resolve(source->ctx, cin_apply, &cin_holder);
        CHECK(cin_holder.applied >= 1 && image.width > 0 && image.height > 0);
      }
    }
  }
  cinematic_capture capture = {c, NULL};
  qa_caught_error caught;
  CHECK(!qa_try(capture_body, &capture, &caught) && strcmp(caught.message, "Cinematic mixer cannot checkpoint its owned PCM lanes") == 0);
  qa_app_application_q3_cinematics_stop(c, handle);
  CHECK(qa_app_application_q3_cinematics_run_guest(c, handle) == 2);
  qa_save_value *checkpoint = qa_app_application_q3_cinematics_capture_checkpoint(c);
  qa_app_application_q3_cinematics *restored = qa_app_application_q3_cinematics_create(media, mixer, seat, cin_now, &cin_holder, NULL);
  qa_app_application_q3_cinematics_restore_checkpoint(restored, checkpoint);
  qa_save_value *again = qa_app_application_q3_cinematics_capture_checkpoint(restored);
  size_t first_length = 0, second_length = 0;
  uint8_t *first = qa_encode_checkpoint_value(checkpoint, &first_length);
  uint8_t *second = qa_encode_checkpoint_value(again, &second_length);
  CHECK(first != NULL && second != NULL && first_length == second_length && memcmp(first, second, first_length) == 0);
  free(first);
  free(second);
  qa_save_value_free(again);
  qa_save_value_free(checkpoint);
  qa_app_application_q3_cinematics_destroy(restored);
  qa_app_application_q3_cinematics_close(c);
  qa_app_application_q3_cinematics_destroy(c);
}

/* ---- network/remote-q3.ts attach(): the cgame source over a connection ---- */
typedef struct remote_state { int sentinel; qa_identity_owner *ids; size_t actors, generations; } remote_state;
static remote_state remote_holder;
static remote_state *rs(void *ctx) {
  remote_state *state = ctx;
  CHECK(state == &remote_holder && state->sentinel == 0x7e70);
  return state;
}
static double r_next_generation(void *ctx, double slot) { (void)slot; rs(ctx)->generations++; return 3; }
static qa_actor_id r_identity_actor(void *ctx, double slot, double generation) {
  remote_state *state = rs(ctx);
  state->actors++;
  return qa_identity_owner_actor(state->ids, (int64_t)slot, (int64_t)generation);
}
static void r_print(void *ctx, const char *text) { (void)text; (void)rs(ctx); }
static void r_thunk(void *ctx) { (void)rs(ctx); }
static void r_message_print(void *ctx, const char *text) { (void)text; (void)rs(ctx); }
static void test_remote_source(void) {
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "remote-source"));
  remote_holder = (remote_state){0x7e70, &ids, 0, 0};
  qa_app_q3_remote_options options;
  memset(&options, 0, sizeof options);
  options.ctx = &remote_holder;
  options.session = ids.session;
  options.client = qa_identity_owner_client(&ids, 0, 1);
  options.next_generation = r_next_generation;
  options.identity_actor = r_identity_actor;
  options.print = r_print;
  qa_app_q3_remote *remote = qa_app_q3_remote_create(options);
  CHECK(remote != NULL);
  qa_q3_client_bindings bindings;
  memset(&bindings, 0, sizeof bindings);
  bindings.assert_current = r_thunk; bindings.print = r_message_print; bindings.print_ctx = &remote_holder;
  bindings.clear_active = r_thunk; bindings.ctx = &remote_holder;
  qa_q3_connection_identity who = {qa_identity_owner_client(&ids, 0, 1), true, qa_identity_owner_seat(&ids, 0)};
  qa_q3_client_mode mode;
  memset(&mode, 0, sizeof mode);
  mode.kind = QA_Q3_CLIENT_MODE_NETWORK; mode.challenge = 9; mode.qport = 195;
  qa_q3_client_connection *connection = NULL;
  CHECK(qa_q3_client_connection_init(&connection, who, QA_Q3_PRODUCT_BASEQ3, &mode, &bindings));
  qa_q3_client_connection_set_client_number(connection, 5);
  qa_app_q3_remote_attach(remote, connection);
  const qa_app_q3_client_source *source = qa_app_q3_remote_cgame_source(remote);
  CHECK(source->game_state != NULL && source->server_command != NULL && source->actor_at != NULL && source->demo != NULL);
  CHECK(source->commands.current_number != NULL && source->commands.read != NULL);
  CHECK(source->snapshots.current != NULL && source->snapshots.read != NULL);
  CHECK(source->client_number != NULL && source->time != NULL && source->server_message_sequence != NULL);
  CHECK(source->last_executed_server_command != NULL && source->system_info != NULL && source->snapshot_ping != NULL);
  CHECK(source->sound_actor == NULL); /* remote-q3.ts has no soundActor */
  CHECK(!source->demo(source->ctx));
  CHECK(source->client_number(source->ctx) == 5);
  CHECK(source->time(source->ctx) == 0 && source->server_message_sequence(source->ctx) == 0);
  CHECK(source->last_executed_server_command(source->ctx) == 0);
  CHECK(strcmp(source->system_info(source->ctx), "") == 0);
  qa_q3_client_game_state_storage_begin_entries(qa_q3_client_connection_game_state(connection));
  qa_q3_client_game_state_storage_append(qa_q3_client_connection_game_state(connection), 1, "\\sv_pure\\0");
  CHECK(strcmp(source->system_info(source->ctx), "\\sv_pure\\0") == 0);
  CHECK(source->commands.current_number(source->commands.ctx) == 0);
  qa_q3_base_user_command command;
  qa_q3_wire_user_command wire;
  memset(&wire, 0, sizeof wire);
  wire.server_time = 2000; wire.angles[0] = 10; wire.angles[1] = 20; wire.angles[2] = 30; wire.weapon = 5; wire.forwardmove = 127;
  qa_q3_wire_command_history_append(qa_q3_client_connection_commands(connection), &wire);
  CHECK(source->commands.current_number(source->commands.ctx) == 1);
  CHECK(source->commands.read(source->commands.ctx, 1, &command));
  CHECK(command.server_time == 2000 && command.angles.x == 10 && command.angles.y == 20 && command.angles.z == 30);
  CHECK(command.weapon == 5 && command.forwardmove == 127);
  double ping = -1;
  CHECK(!source->snapshot_ping(source->ctx, 1, &ping));
  qa_actor_id actor = source->actor_at(source->ctx, 7);
  CHECK(remote_holder.actors == 1 && qa_same_actor(actor, qa_identity_owner_actor(&ids, 7, 3)));
  qa_q3_pres_snapshot_current current = source->snapshots.current(source->snapshots.ctx);
  CHECK(current.number == 0);
  qa_q3_client_connection_destroy(connection);
  qa_app_q3_remote_destroy(remote);
  qa_identity_owner_destroy(&ids);
}
static void test_retail(const char *root) {
  char store[] = "build/agents/app-q3c2/tmp/q3-client-XXXXXX";
  CHECK(mkdtemp(store) != NULL);
  qa_app_options content_options = parsed_options(root, store);
  qa_app_content_host content_host;
  memset(&content_host, 0, sizeof content_host);
  qa_app_loaded_content *content = qa_app_load_application_content_native(content_host, content_options, NULL, false, NULL, NULL, NULL);
  CHECK(content != NULL);
  if (content == NULL) return;
  qa_app_options options = parsed_options(root, store);
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "q3-client"));
  fixture *f = &fx_holder;
  memset(f, 0, sizeof *f);
  f->sentinel = FIXTURE_SENTINEL;
  f->ids = &ids;
  int owner_symbol = 0;
  qa_renderer_resource_owner owner = {&owner_symbol, ids.session, 0};
  qa_app_application_assets_options asset_options;
  memset(&asset_options, 0, sizeof asset_options);
  qa_app_application_assets *assets = qa_app_application_assets_create(content, owner, NULL, &asset_options);
  qa_world_scene *world = qa_app_application_assets_load_world(assets);
  qa_app_native_renderer_host renderer_host;
  memset(&renderer_host, 0, sizeof renderer_host);
  qa_app_native_renderer *renderer = qa_app_native_renderer_open(renderer_host, "cpu", 640, 480, true, 1, false, owner, NULL);
  if (renderer == NULL) {
    fprintf(stderr, "skip retail: no SDL window\n");
    qa_app_application_assets_destroy(assets);
    qa_app_loaded_content_close(content);
    qa_app_loaded_content_destroy(content);
    qa_app_options_destroy(&options);
    qa_identity_owner_destroy(&ids);
    return;
  }
  video_renderer = renderer;
  qa_session_client *session_client = qa_session_client_create(qa_identity_owner_client(&ids, 0, 1));
  qa_seat_id seat_id = qa_identity_owner_seat(&ids, 0);
  qa_session_seat *seat = qa_session_seat_create(seat_id, session_client);
  qa_actor_id actor = qa_identity_owner_actor(&ids, 0, 0);
  qa_config_store settings;
  CHECK(qa_config_store_init(&settings, store));
  qa_app_sim_presentation_access simulation;
  memset(&simulation, 0, sizeof simulation);
  simulation.ctx = f;
  simulation.player_view = no_player_view;
  qa_app_local_player player = {seat, actor};
  qa_app_input_open_args args;
  memset(&args, 0, sizeof args);
  args.window = qa_app_input_window_from_sdl(qa_app_native_renderer_window(renderer));
  args.players = &player; args.player_count = 1;
  args.options = &options; args.dialect = QA_COMMAND_DIALECT_Q3; args.simulation = &simulation;
  args.actions.ctx = f; args.actions.quit = act_quit; args.actions.execute = act_execute; args.actions.print = act_print;
  args.now = input_now; args.now_ctx = f; args.settings = &settings;
  qa_app_application_input *input = qa_app_application_input_open(&args);
  qa_app_local_input *local = qa_app_application_input_local(input, 0);
  guest_input_holder = input;
  qa_app_audio_options audio_options;
  memset(&audio_options, 0, sizeof audio_options);
  audio_options.defer_output = true;
  qa_app_application_audio *audio = qa_app_application_audio_create(content, opt_now, f, 1, "missing-custom-model", audio_print, f,
                                                                     &audio_options);
  qa_cvar_registry_options key_cvar_options;
  memset(&key_cvar_options, 0, sizeof key_cvar_options);
  key_cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  key_cvar_options.context.session = ids.session;
  key_cvar_options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_cvar_registry *key_cvars = qa_cvar_registry_create(&key_cvar_options);
  qa_app_keys *keys = qa_app_keys_create(keys_print, f);
  qa_app_key_profile *profile = qa_app_keys_prepare(keys, &options, qa_app_loaded_content_catalog(content), key_cvars);
  CHECK(profile != NULL);
  qa_shared_scene_queries *queries = qa_create_scene_queries(world->map);
  qa_q3_browser_view_options browser_options;
  memset(&browser_options, 0, sizeof browser_options);
  browser_options.browser.ctx = f;
  browser_options.browser.q3_list = b_list;
  browser_options.browser.resolve_q3 = b_resolve;
  browser_options.browser.add_q3 = b_add;
  browser_options.browser.remove_q3 = b_add;
  browser_options.browser.scan_q3 = b_thunk;
  browser_options.browser.request_q3_master = b_master;
  browser_options.browser.load_q3_cache = b_load;
  browser_options.browser.save_q3_cache = b_save;
  browser_options.browser.assert_open = b_thunk;
  browser_options.ctx = f;
  browser_options.now = b_now;
  browser_options.max_ping = b_max_ping;
  browser_options.status_resend_time = b_resend;
  browser_options.print = b_print;
  browser_options.print_ctx = f;
  qa_q3_browser_view *browser = NULL;
  CHECK(qa_q3_browser_view_init(&browser, &browser_options));
  CHECK(qa_q3_client_game_state_storage_init(&f->game_state, NULL, NULL));
  qa_q3_client_game_state_storage_begin_entries(f->game_state);
  for (size_t i = 0; i < sizeof fixture_strings / sizeof fixture_strings[0]; i++)
    qa_q3_client_game_state_storage_append(f->game_state, fixture_strings[i].index, fixture_strings[i].value);
  qa_q3_client_state connection;
  memset(&connection, 0, sizeof connection);
  connection.ctx = f;
  connection.get_generation = cs_generation;
  connection.get_server_message_sequence = cs_message;
  connection.get_last_executed_server_command = cs_executed;
  connection.get_client_number = cs_client_number;
  connection.snapshot_ping = cs_ping;
  connection.get_server_command = cs_server_command;
  connection.game_state = (qa_q3_client_game_state_view){f, cs_get, cs_record};
  connection.commands = (qa_q3_client_commands_view){f, cs_command_number, cs_command_read};
  qa_app_q3_client_source source;
  memset(&source, 0, sizeof source);
  source.ctx = f;
  source.game_state = src_game_state;
  source.server_command = src_server_command;
  source.actor_at = src_actor_at;
  source.demo = src_demo;
  source.commands = (qa_q3_pres_command_source){f, src_commands_number, src_commands_read};
  source.snapshots = (qa_q3_pres_snapshot_source){f, src_current, src_read};
  source.client_number = src_client_number;
  source.time = src_time;
  source.server_message_sequence = src_message;
  source.last_executed_server_command = src_executed;
  source.system_info = src_system_info;
  source.snapshot_ping = src_ping;
  qa_app_q3_client_cvars *guest_cvars = qa_app_application_input_guest_cvars(input, seat_id);
  qa_app_application_q3_client_options client_options;
  memset(&client_options, 0, sizeof client_options);
  client_options.kind = QA_APP_Q3_CLIENT_QVM;
  client_options.ctx = f;
  client_options.save_font_data = opt_save_font;
  client_options.assets = assets;
  client_options.renderer = (qa_app_qvm_display_renderer){8, NULL, NULL};
  client_options.queries = queries;
  client_options.local = local;
  client_options.audio = audio;
  client_options.viewport = opt_viewport;
  client_options.now = opt_now;
  client_options.reliable = opt_reliable;
  client_options.console = opt_console;
  client_options.print = opt_print;
  client_options.command_bindings = qa_app_application_input_client_commands(input);
  client_options.command_registration = qa_app_application_input_client_command_registration(input, seat_id, NULL);
  client_options.local_server = true;
  client_options.source = &source;
  client_options.connection = connection;
  client_options.browser = browser;
  client_options.keys = profile;
  client_options.guest_cvars = qa_app_q3_client_cvars_services(guest_cvars);
  client_options.guest_input = qa_app_application_input_guest_input(input, seat_id);
  client_options.command_buffer.ctx = input;
  client_options.command_buffer.execute_now = guest_execute_now;
  client_options.command_buffer.insert = guest_insert;
  client_options.command_buffer.append = guest_append;
  client_options.client_state = opt_client_state;
  qa_app_application_q3_client *client = qa_app_application_q3_client_create(&client_options, NULL);
  flush_prints(f, "init");
  dump_resources(client, "init");
  for (int frame = 1; frame <= FRAMES; frame++) {
    f->snapshot = frame;
    f->clock = frame * 50;
    qa_rect viewport = {0, 0, 640, 480};
    qa_app_application_q3_client_prepare(client, frame, &viewport, NULL, 0, true, NULL, 0, true, false);
    char label[32];
    snprintf(label, sizeof label, "frame%d", frame);
    flush_prints(f, label);
    qa_render_frame rendered = qa_app_application_q3_client_frame(client, NULL, NULL, NULL);
    out("frame%d commands=%zu", frame, rendered.command_count);
    for (size_t i = 0; i < rendered.command_count; i++) {
      char prefix[64];
      snprintf(prefix, sizeof prefix, "frame%d.%zu", frame, i);
      dump_command(prefix, &rendered.commands[i]);
    }
  }
  dump_resources(client, "final");
  oracle_closed = true;
  test_cinematics(qa_app_application_q3_client_media(client), assets, seat_id);
  /* The services share the client's viewport (TS viewportValue): a prepare
   * with another viewport reaches the recorder and the 2D sink. */
  qa_rect half = {320, 0, 320, 240};
  f->snapshot = 4;
  qa_app_application_q3_client_prepare(client, FRAMES + 1, &half, NULL, 0, true, NULL, 0, true, false);
  qa_render_frame shared = qa_app_application_q3_client_frame(client, NULL, NULL, NULL);
  bool offset_view = false;
  for (size_t i = 0; i < shared.command_count; i++)
    if (shared.commands[i].kind == QA_RENDER_COMMAND_VIEW && shared.commands[i].view.state.viewport.x == 320) offset_view = true;
  CHECK(offset_view);
  flush_prints(f, "shared");
  test_seat_view(client, seat_id, f);
  /* prepareVideoGuests and ApplicationVideoRestart over the QVM seat. */
  video_seat_holder = (video_seat){0x51ee7, NULL, 0};
  video_assert_holder = (video_assert){0xa55e, 0, false};
  memset(&video_host_holder, 0, sizeof video_host_holder);
  video_host_holder.sentinel = 0x1d30;
  video_host_holder.input = input;
  video_host_holder.client = client;
  qa_app_video_restart_host host = {&video_host_holder, vh_capture, NULL, vh_prepare, vh_publish_window, vh_published, vh_settled,
                                    vh_print, vh_failed};
  qa_app_video_restart *video = qa_app_video_restart_create(renderer, host);
  qa_app_video_restart_request(video, "cpu", NULL);
  CHECK(qa_app_video_restart_pending(video));
  CHECK(qa_app_video_restart_drain(video));
  CHECK(video_host_holder.prepares == 1 && video_host_holder.windows == 1 && video_host_holder.published == 1);
  CHECK(video_host_holder.settled == 1 && video_assert_holder.calls == 1 && video_host_holder.failed == 0);
  CHECK(strcmp(video_host_holder.last_print, "Renderer restarted (cpu).\n") == 0);
  CHECK(qa_app_video_restart_generation(video) == 1 && !qa_app_video_restart_pending(video));
  qa_app_application_q3_client *reopened = video_seat_holder.published;
  CHECK(reopened != NULL && reopened != client && video_seat_holder.viewports > 0);
  if (reopened != NULL) {
    /* The reopened client keeps the cvars and draws through the reopened modules. */
    CHECK(qa_app_application_q3_client_cvars(reopened) == qa_app_application_q3_client_cvars(client));
    qa_app_application_q3_client_destroy(client);
    client = reopened;
    qa_rect viewport = {0, 0, 640, 480};
    qa_app_application_q3_client_prepare(client, FRAMES + 2, &viewport, NULL, 0, true, NULL, 0, true, false);
    qa_render_frame again = qa_app_application_q3_client_frame(client, NULL, NULL, NULL);
    CHECK(again.command_count > 0);
    size_t before = video_seat_holder.viewports;
    qa_app_application_q3_client_prepare(client, FRAMES + 3, NULL, NULL, 0, true, NULL, 0, true, false);
    CHECK(video_seat_holder.viewports > before);
  }
  /* A rejected restart prints and settles without failing the source. */
  video_host_holder.client = client;
  video_assert_holder.reject = true;
  qa_app_video_restart_request(video, "cpu", NULL);
  CHECK(!qa_app_video_restart_drain(video));
  CHECK(video_host_holder.failed == 0 && video_host_holder.settled == 2);
  CHECK(strcmp(video_host_holder.last_print, "Video restart rejected: Guest video source changed during preparation\n") == 0);
  qa_app_video_restart_destroy(video);
  /* The reopened ui.qvm initialized again. */
  bool reparsed = false;
  for (size_t i = 0; i < f->print_count; i++) reparsed = reparsed || strcmp(f->prints[i], "35 arenas parsed\n") == 0;
  CHECK(reparsed);
  flush_prints(f, "video");
  qa_caught_error caught;
  CHECK(qa_try(shutdown_client, client, &caught));
  flush_prints(f, "shutdown");
  CHECK(qa_try(close_client, client, &caught));
  qa_app_application_q3_client_destroy(client);
  for (size_t i = 0; i < f->print_count; i++) free(f->prints[i]);
  free(f->prints);
  free_strings(f);
  free(f->game_state_text);
  qa_q3_source_game_state_record_destroy(&held_record);
  qa_q3_client_game_state_storage_destroy(f->game_state);
  qa_app_q3_client_cvars_destroy(guest_cvars);
  qa_q3_browser_view_destroy(browser);
  qa_shared_scene_queries_destroy(queries);
  qa_app_keys_destroy(keys);
  qa_cvar_registry_destroy(key_cvars);
  qa_app_application_audio_destroy(audio);
  qa_app_application_input_destroy(input);
  qa_session_seat_destroy(seat);
  qa_session_client_destroy(session_client);
  qa_app_native_renderer_destroy(renderer);
  qa_app_application_assets_destroy(assets);
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
  qa_config_store_destroy(&settings);
  qa_app_options_destroy(&options);
  qa_identity_owner_destroy(&ids);
}

static void compare_oracle(void) {
  const char *dump = getenv("Q3_CLIENT_DUMP"); /* writes the C lines for diffing */
  if (dump != NULL) {
    FILE *target = fopen(dump, "w");
    if (target != NULL) {
      for (size_t i = 0; i < line_count; i++) fprintf(target, "%s\n", lines[i]);
      fclose(target);
    }
  }
  FILE *file = fopen("tests/app/oracles/q3_client.txt", "r");
  CHECK(file != NULL);
  if (file == NULL) return;
  char buffer[4096];
  size_t index = 0, shown = 0;
  while (fgets(buffer, sizeof buffer, file) != NULL) {
    buffer[strcspn(buffer, "\n")] = '\0';
    checks++;
    if (index >= line_count) {
      if (shown++ < 20) fprintf(stderr, "missing C line for oracle: %s\n", buffer);
      failures++;
    } else if (strcmp(buffer, lines[index]) != 0) {
      failures++;
      if (shown++ < 20) fprintf(stderr, "oracle: %s\n     C: %s\n", buffer, lines[index]);
    }
    index++;
  }
  fclose(file);
  CHECK(index == line_count && line_count > 100);
  if (index != line_count) fprintf(stderr, "oracle lines %zu, C lines %zu\n", index, line_count);
}

int main(void) {
  qa_qvm_memory_init(&guest, guest_bytes, sizeof guest_bytes, NULL, NULL);
  test_qvm_client_commands();
  test_legacy_client_command();
  test_status_view();
  test_remote_source();
  const char *configured = getenv("QTS_TEST_CORPUS");
  const char *home = getenv("HOME");
  char root[512];
  snprintf(root, sizeof root, "%s", configured != NULL && configured[0] != '\0' ? configured : "");
  if (root[0] == '\0') snprintf(root, sizeof root, "%s/qfiles", home != NULL ? home : "");
  char probe_path[600];
  snprintf(probe_path, sizeof probe_path, "%s/Quake 3 Arena/baseq3/pak0.pk3", root);
  struct stat status;
  if (stat(probe_path, &status) == 0) {
    test_retail(root);
    compare_oracle();
  } else fprintf(stderr, "skip: no retail corpus at %s\n", root);
  for (size_t i = 0; i < line_count; i++) free(lines[i]);
  free(lines);
  qa_qvm_memory_destroy(&guest);
  printf("test_top_q3_client: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
