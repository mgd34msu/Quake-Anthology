/*
 * Port of tests/console/buffer-resize.test.ts (all cases), the
 * discovery.test.ts cases that stay inside slice-a services (the
 * TS seat-routing setup belongs to app/bootstrap; here one cvar
 * registry backs the buffer, so live values and documentation are
 * checked without routing), and the draw.test.ts layout case
 * re-expressed over the draw callbacks of include/qa/console/draw.h
 * (glyph atlas rects belong to the unported text layout layer).
 * commands.ts/dedicated.ts have no dedicated TS suites; their
 * handlers are exercised end-to-end here through a real command
 * buffer, seat console, and config store under /tmp.
 * TypeScript `toThrow` cases run in a child and must abort.
 * GPL-2.0-or-later.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/console/buffer.h"
#include "qa/console/commands.h"
#include "qa/console/dedicated.h"
#include "qa/console/discovery.h"
#include "qa/console/draw.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

/* --- buffer (buffer-resize.test.ts) --- */

typedef struct flat_cells {
  qa_console_cell *cells;
  size_t count;
} flat_cells;

static flat_cells all_cells(qa_console_buffer *buffer) {
  size_t row_count = 0;
  qa_console_row *rows = qa_console_buffer_visible(buffer, 1000, &row_count);
  flat_cells flat = {NULL, 0};
  size_t total = 0;
  for (size_t i = 0; i < row_count; i++) total += rows[i].cell_count;
  flat.cells = (qa_console_cell *)malloc((total + 1) * sizeof *flat.cells);
  for (size_t i = 0; i < row_count; i++) {
    memcpy(flat.cells + flat.count, rows[i].cells,
           rows[i].cell_count * sizeof *flat.cells);
    flat.count += rows[i].cell_count;
  }
  qa_console_rows_free(rows, row_count);
  return flat;
}

static bool cells_equal(const flat_cells *a, const flat_cells *b) {
  if (a->count != b->count) return false;
  for (size_t i = 0; i < a->count; i++) {
    if (a->cells[i].character != b->cells[i].character ||
        a->cells[i].color != b->cells[i].color ||
        a->cells[i].alternate != b->cells[i].alternate) {
      return false;
    }
  }
  return true;
}

static void check_dump(qa_console_buffer *buffer, const char *want) {
  char *dump = qa_console_buffer_dump(buffer);
  CHECK(dump != NULL && strcmp(dump, want) == 0);
  free(dump);
}

static void test_buffer_resize_wrap(void) {
  qa_console_buffer buffer;
  CHECK(qa_console_buffer_init(&buffer, QA_COMMAND_DIALECT_Q3, 40,
                               QA_CONSOLE_DEFAULT_CHARACTER_CAPACITY));
  qa_console_buffer_print(&buffer, "^1abcdefghijklmnopqrstuvwx\n^2second line\n", 1);
  flat_cells original = all_cells(&buffer);
  qa_console_buffer_resize(&buffer, 8);
  flat_cells resized = all_cells(&buffer);
  CHECK(cells_equal(&original, &resized));
  size_t row_count = 0;
  qa_console_row *rows = qa_console_buffer_visible(&buffer, 100, &row_count);
  bool narrow = true;
  for (size_t i = 0; i < row_count; i++) {
    if (rows[i].cell_count > 8) narrow = false;
  }
  CHECK(narrow);
  qa_console_rows_free(rows, row_count);
  qa_console_buffer_resize(&buffer, 40);
  check_dump(&buffer, "abcdefghijklmnopqrstuvwx\nsecond line\n\n");
  qa_console_buffer_print(&buffer, "tail", 2);
  char *dump = qa_console_buffer_dump(&buffer);
  CHECK(dump != NULL && strstr(dump, "second line\ntail\n") != NULL);
  free(dump);
  free(original.cells);
  free(resized.cells);
  qa_console_buffer_destroy(&buffer);
}

static void test_buffer_resize_partial(void) {
  qa_console_buffer buffer;
  CHECK(qa_console_buffer_init(&buffer, QA_COMMAND_DIALECT_Q1_NETQUAKE, 40,
                               QA_CONSOLE_DEFAULT_CHARACTER_CAPACITY));
  qa_console_buffer_print(&buffer, "\x01" "abcdefghijkl", 1);
  qa_console_buffer_resize(&buffer, 8);
  qa_console_buffer_print(&buffer, "mn", 2);
  check_dump(&buffer, "abcdefgh\nijklmn\n");
  flat_cells cells = all_cells(&buffer);
  bool alternate = cells.count >= 12;
  for (size_t i = 0; i < 12 && i < cells.count; i++) {
    if (!cells.cells[i].alternate) alternate = false;
  }
  CHECK(alternate);
  free(cells.cells);
  qa_console_buffer_resize(&buffer, 40);
  check_dump(&buffer, "abcdefghijklmn\n");
  qa_console_buffer_destroy(&buffer);
}

static bool rows_cell_equal(const qa_console_row *a, const qa_console_row *b) {
  if (a->cell_count != b->cell_count) return false;
  for (size_t i = 0; i < a->cell_count; i++) {
    if (a->cells[i].character != b->cells[i].character ||
        a->cells[i].color != b->cells[i].color ||
        a->cells[i].alternate != b->cells[i].alternate) {
      return false;
    }
  }
  return true;
}

static void test_buffer_resize_scrollback(void) {
  qa_console_buffer buffer;
  CHECK(qa_console_buffer_init(&buffer, QA_COMMAND_DIALECT_Q3, 40,
                               QA_CONSOLE_DEFAULT_CHARACTER_CAPACITY));
  qa_console_buffer_print(&buffer, "first\nsecond\nthird\nfourth\n", 1);
  qa_console_buffer_scroll(&buffer, 2);
  size_t before_count = 0;
  qa_console_row *before = qa_console_buffer_visible(&buffer, 1, &before_count);
  CHECK(before_count == 1);
  qa_console_buffer_resize(&buffer, 8);
  size_t after_count = 0;
  qa_console_row *after = qa_console_buffer_visible(&buffer, 1, &after_count);
  CHECK(after_count == 1 && before_count == 1 &&
        rows_cell_equal(&before[0], &after[0]));
  qa_console_rows_free(after, after_count);
  qa_console_buffer_print(&buffer, "new\n", 2);
  after = qa_console_buffer_visible(&buffer, 1, &after_count);
  CHECK(after_count == 1 && rows_cell_equal(&before[0], &after[0]));
  qa_console_rows_free(after, after_count);
  qa_console_rows_free(before, before_count);
  qa_console_buffer_destroy(&buffer);
}

static void test_buffer_resize_overwrite_cursor(void) {
  qa_console_buffer buffer;
  CHECK(qa_console_buffer_init(&buffer, QA_COMMAND_DIALECT_Q3, 40,
                               QA_CONSOLE_DEFAULT_CHARACTER_CAPACITY));
  qa_console_buffer_print(&buffer, "abcdefghijkl\rXY", 1);
  qa_console_buffer_resize(&buffer, 8);
  qa_console_buffer_print(&buffer, "Z", 2);
  flat_cells cells = all_cells(&buffer);
  char joined[32] = "";
  for (size_t i = 0; i < cells.count && i < sizeof joined - 1; i++) {
    joined[i] = (char)cells.cells[i].character;
  }
  CHECK(strcmp(joined, "XYZdefghijkl") == 0);
  free(cells.cells);
  qa_console_buffer_resize(&buffer, 40);
  qa_console_buffer_print(&buffer, "!", 3);
  check_dump(&buffer, "XYZ!efghijkl\n");
  qa_console_buffer_destroy(&buffer);
  CHECK_ABORTS({
    qa_console_buffer bad;
    qa_console_buffer_init(&bad, QA_COMMAND_DIALECT_Q3, 0, 32768);
  });
}

/* --- shared print collectors --- */

typedef struct collected {
  char *data;
  size_t length;
  size_t capacity;
} collected;

static void collect_text(collected *sink, const char *text) {
  size_t length = strlen(text);
  if (sink->length + length + 1 > sink->capacity) {
    size_t cap = sink->capacity == 0 ? 1024 : sink->capacity;
    while (cap < sink->length + length + 1) cap *= 2;
    sink->data = (char *)realloc(sink->data, cap);
    sink->capacity = cap;
  }
  memcpy(sink->data + sink->length, text, length + 1);
  sink->length += length;
}

static void print_plain(void *ctx, const char *text) {
  collect_text((collected *)ctx, text);
}

static void print_source(void *ctx, const char *text,
                         const qa_command_context *source) {
  (void)source;
  collect_text((collected *)ctx, text);
}

static bool collected_contains(const collected *sink, const char *needle) {
  return sink->data != NULL && strstr(sink->data, needle) != NULL;
}

static qa_command_context local_console_context(qa_identity_owner *owner) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = owner->session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  return context;
}

static qa_command_buffer *make_commands(qa_command_dialect dialect,
                                        qa_command_context context,
                                        qa_cvar_registry *cvars,
                                        collected *printed) {
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = context;
  options.cvars = cvars;
  options.print = print_source;
  options.print_ctx = printed;
  return qa_command_buffer_create(&options);
}

/* --- discovery (discovery.test.ts) --- */

static bool help_lines_contain(const qa_console_discovery_entry *entry,
                               const char *want) {
  size_t count = 0;
  char **lines = qa_console_entry_help(entry, &count);
  bool found = false;
  for (size_t i = 0; i < count; i++) {
    if (strcmp(lines[i], want) == 0) found = true;
  }
  qa_console_discovery_lines_free(lines, count);
  return found;
}

static const qa_console_discovery_entry *find_entry(
    const qa_console_discovery_entry *entries, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++) {
    if (entries[i].name != NULL && strcmp(entries[i].name, name) == 0) {
      return &entries[i];
    }
  }
  return NULL;
}

static void test_discovery_live_values(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "discovery"));
  qa_command_context context = local_console_context(&identity);
  qa_cvar_registry_options reg_options;
  memset(&reg_options, 0, sizeof reg_options);
  reg_options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  reg_options.context = context;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&reg_options);
  CHECK(cvars != NULL);
  qa_cvar_snapshot snapshot;
  CHECK(qa_cvar_registry_register(cvars, "sensitivity", "3", 0, &snapshot));
  qa_cvar_read_free(&snapshot);
  static const char *const examples[] = {"sensitivity 4"};
  qa_command_documentation doc = {"Mouse sensitivity.",
                                  "sensitivity [value]", examples, 1, NULL, 0};
  qa_cvar_registry_document(cvars, "sensitivity", doc);
  collected printed = {NULL, 0, 0};
  qa_command_buffer *commands = make_commands(QA_COMMAND_DIALECT_Q2_CLASSIC,
                                              context, cvars, &printed);
  CHECK(commands != NULL);
  qa_console_discovery_registration *remove =
      qa_register_discovery_commands(commands, print_plain, &printed);
  CHECK(remove != NULL);
  CHECK(qa_command_buffer_define_alias(commands, "greet", "echo hello\n"));
  size_t count = 0;
  qa_console_discovery_entry *found =
      qa_find_console_entries(commands, "mouse", &context, &count);
  CHECK(count == 1 && found != NULL &&
        strcmp(found[0].name, "sensitivity") == 0);
  qa_console_discovery_entries_free(found, count);
  qa_console_discovery_entry *all =
      qa_query_console_entries(commands, &context, &count);
  const qa_console_discovery_entry *sensitivity =
      find_entry(all, count, "sensitivity");
  CHECK(sensitivity != NULL &&
        help_lines_contain(sensitivity, "Current: \"3\""));
  CHECK(sensitivity != NULL &&
        help_lines_contain(sensitivity, "Default: \"3\""));
  qa_console_discovery_entries_free(all, count);
  CHECK(qa_cvar_registry_set(cvars, "sensitivity", "5", false, &snapshot));
  qa_cvar_read_free(&snapshot);
  free(printed.data);
  printed = (collected){NULL, 0, 0};
  qa_command_buffer_append(commands, "help sensitivity\nhelp greet\nfind absent\n",
                           &context, -1);
  (void)qa_command_buffer_execute(commands);
  CHECK(collected_contains(&printed, "Current: \"5\""));
  CHECK(collected_contains(&printed, "Expands to: echo hello"));
  CHECK(collected_contains(&printed, "0 match(es)"));
  CHECK(collected_contains(&printed, "Mouse sensitivity."));
  qa_console_discovery_registration_remove(remove);
  CHECK(!qa_command_buffer_exists(commands, "find"));
  free(printed.data);
  qa_command_buffer_destroy(commands);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);
}

static void dummy_handler(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  (void)command;
}

static void test_discovery_pagination(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "discovery-pages"));
  qa_command_context context = local_console_context(&identity);
  collected printed = {NULL, 0, 0};
  qa_command_buffer *commands =
      make_commands(QA_COMMAND_DIALECT_Q3, context, NULL, &printed);
  CHECK(commands != NULL);
  qa_console_discovery_registration *remove =
      qa_register_discovery_commands(commands, print_plain, &printed);
  CHECK(remove != NULL);
  for (int i = 0; i < 31; i++) {
    char name[32];
    (void)snprintf(name, sizeof name, "test_item_%d", i);
    CHECK(qa_command_buffer_register_engine(commands, name, dummy_handler,
                                            NULL, NULL));
  }
  qa_command_buffer_append(commands,
                           "find test_item\nfind test_item 2\nhelp test_item_0\n",
                           NULL, -1);
  (void)qa_command_buffer_execute(commands);
  CHECK(collected_contains(&printed, "31 match(es), page 1/2"));
  CHECK(collected_contains(&printed, "Next page: find \"test_item\" 2"));
  CHECK(collected_contains(&printed, "page 2/2"));
  CHECK(collected_contains(&printed, "No description registered."));
  CHECK(collected_contains(&printed, "Usage: not documented"));
  qa_console_discovery_registration_remove(remove);
  free(printed.data);
  qa_command_buffer_destroy(commands);
  qa_identity_owner_destroy(&identity);
}

/* --- dedicated (dedicated.ts) --- */

static void test_dedicated_lines(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "dedicated"));
  collected printed = {NULL, 0, 0};
  qa_command_buffer *commands = make_commands(
      QA_COMMAND_DIALECT_Q3, local_console_context(&identity), NULL, &printed);
  CHECK(commands != NULL);
  qa_dedicated_console console;
  CHECK(qa_dedicated_console_init(&console));
  CHECK(!qa_dedicated_console_eof(&console));
  qa_dedicated_console_feed_text(&console, "echo a\nsay ");
  qa_dedicated_console_feed_text(&console, "b\r\nlast");
  CHECK(qa_dedicated_console_drain(&console, commands) == 2);
  char *pending = qa_command_buffer_pending_text(commands);
  CHECK(pending != NULL && strcmp(pending, "echo a\nsay b\n") == 0);
  free(pending);
  (void)qa_command_buffer_execute(commands);
  qa_dedicated_console_end(&console);
  CHECK(!qa_dedicated_console_eof(&console));
  CHECK(qa_dedicated_console_drain(&console, commands) == 1);
  pending = qa_command_buffer_pending_text(commands);
  CHECK(pending != NULL && strcmp(pending, "last\n") == 0);
  free(pending);
  CHECK(qa_dedicated_console_eof(&console));
  qa_dedicated_console_close(&console);
  qa_dedicated_console_feed_text(&console, "ignored\n");
  CHECK_ABORTS((void)qa_dedicated_console_drain(&console, commands));
  qa_dedicated_console_destroy(&console);

  qa_dedicated_console failed;
  CHECK(qa_dedicated_console_init(&failed));
  qa_dedicated_console_fail(&failed, "input broke");
  CHECK_ABORTS((void)qa_dedicated_console_drain(&failed, commands));
  qa_dedicated_console_destroy(&failed);
  free(printed.data);
  qa_command_buffer_destroy(commands);
  qa_identity_owner_destroy(&identity);
}

/* --- console commands (commands.ts) --- */

typedef struct command_host {
  qa_seat_console *console;
  qa_seat_id seat;
  qa_config_store store;
  collected printed;
  bool can_chat;
  bool queue_frame_readback;
  char last_focus[16];
  qa_configuration_write_kind write_kind;
  int write_notifications;
} command_host;

static qa_config_store *host_config(void *ctx, qa_seat_id seat) {
  (void)seat;
  return &((command_host *)ctx)->store;
}

static qa_seat_console *host_console(void *ctx, qa_seat_id seat) {
  command_host *host = (command_host *)ctx;
  return qa_same_seat(host->seat, seat) ? host->console : NULL;
}

static char *host_configuration(void *ctx, qa_command_invocation *invocation,
                                char **error_out) {
  (void)ctx;
  (void)invocation;
  *error_out = NULL;
  return dup_text("// test config\n");
}

static void host_write_notify(void *ctx, qa_configuration_write_result result) {
  command_host *host = (command_host *)ctx;
  host->write_kind = result.kind;
  host->write_notifications++;
}

static qa_configuration_write_notify host_write_started(
    void *ctx, qa_command_context source, const char *path) {
  (void)source;
  (void)path;
  qa_configuration_write_notify notify = {host_write_notify, ctx};
  return notify;
}

static void host_print(void *ctx, const char *text) {
  collect_text(&((command_host *)ctx)->printed, text);
}
static bool host_can_chat(void *ctx) { return ((command_host *)ctx)->can_chat; }
static bool host_has_capture(void *ctx, qa_seat_id seat) {
  (void)ctx;
  (void)seat;
  return false;
}
static const char *host_map_name(void *ctx) {
  (void)ctx;
  return "q3dm1";
}
static void host_queue(void *ctx, qa_console_queued_operation_fn operation,
                       void *op_ctx, bool frame_readback) {
  ((command_host *)ctx)->queue_frame_readback = frame_readback;
  operation(op_ctx);
}
static double host_now(void *ctx) {
  (void)ctx;
  return 0;
}
static bool host_connected(void *ctx) {
  (void)ctx;
  return true;
}
static const char *host_clipboard(void *ctx) {
  (void)ctx;
  return NULL;
}
static void host_focus(void *ctx, qa_seat_input_focus focus) {
  command_host *host = (command_host *)ctx;
  (void)snprintf(host->last_focus, sizeof host->last_focus, "%s",
                 focus.kind != NULL ? focus.kind : "");
}
static void host_chat(void *ctx, const char *text, bool team, bool has_target,
                      double target) {
  (void)ctx;
  (void)text;
  (void)team;
  (void)has_target;
  (void)target;
}

static char *read_file_text(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  char *data = NULL;
  size_t length = 0;
  size_t capacity = 0;
  for (;;) {
    if (length + 257 > capacity) {
      capacity = capacity == 0 ? 1024 : capacity * 2;
      data = (char *)realloc(data, capacity);
    }
    size_t got = fread(data + length, 1, 256, file);
    length += got;
    if (got < 256) break;
  }
  (void)fclose(file);
  if (data != NULL) data[length] = '\0';
  return data;
}

static void test_console_commands(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "console-commands"));
  qa_seat_id seat = qa_identity_owner_seat(&identity, 0);
  qa_command_context seat_context;
  memset(&seat_context, 0, sizeof seat_context);
  seat_context.session = identity.session;
  seat_context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  seat_context.origin.seat = seat;
  seat_context.origin.client = qa_identity_owner_client(&identity, 0, 0);
  qa_cvar_registry_options reg_options;
  memset(&reg_options, 0, sizeof reg_options);
  reg_options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  reg_options.context = seat_context;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&reg_options);
  CHECK(cvars != NULL);
  command_host host;
  memset(&host, 0, sizeof host);
  host.seat = seat;
  CHECK(qa_config_store_init(&host.store, "/tmp/qa-console-a"));
  qa_command_buffer *commands = make_commands(QA_COMMAND_DIALECT_Q2_CLASSIC,
                                              seat_context, cvars,
                                              &host.printed);
  CHECK(commands != NULL);
  qa_seat_console console;
  qa_seat_console_options console_options;
  memset(&console_options, 0, sizeof console_options);
  console_options.seat = seat;
  console_options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  console_options.context = seat_context;
  console_options.commands = commands;
  console_options.cvars = cvars;
  console_options.now = host_now;
  console_options.connected = host_connected;
  console_options.clipboard = host_clipboard;
  console_options.focus = host_focus;
  console_options.focus_ctx = &host;
  console_options.chat = host_chat;
  CHECK(qa_seat_console_init(&console, &console_options));
  host.console = &console;
  qa_console_command_services services;
  memset(&services, 0, sizeof services);
  services.ctx = &host;
  services.commands = commands;
  services.config = host_config;
  services.configuration = host_configuration;
  services.configuration_write_started = host_write_started;
  services.console = host_console;
  services.can_chat = host_can_chat;
  services.has_capture = host_has_capture;
  services.map_name = host_map_name;
  services.print = host_print;
  services.queue = host_queue;
  qa_console_command_registration *registration =
      qa_register_console_commands(&services);
  CHECK(registration != NULL);
  CHECK(qa_command_buffer_exists(commands, "condump"));
  qa_seat_console_print(&console, "hello console\n");
  qa_command_buffer_append(commands, "condump out\n", &seat_context, -1);
  (void)qa_command_buffer_execute(commands);
  char *dumped = read_file_text("/tmp/qa-console-a/out.txt");
  CHECK(dumped != NULL && strstr(dumped, "hello console") != NULL);
  free(dumped);
  qa_command_buffer_append(commands, "writeconfig\nmessagemode\nscreenshot\n",
                           &seat_context, -1);
  (void)qa_command_buffer_execute(commands);
  char *written = read_file_text("/tmp/qa-console-a/config.cfg");
  CHECK(written != NULL && strcmp(written, "// test config\n") == 0);
  free(written);
  CHECK(host.write_notifications == 1 &&
        host.write_kind == QA_CONFIGURATION_WRITTEN);
  CHECK(collected_contains(&host.printed,
                           "Chat requires an active connection.\n"));
  CHECK(collected_contains(&host.printed,
                           "Screenshot requires an active seat renderer\n"));
  qa_seat_console_print(&console, "to clear\n");
  qa_command_buffer_append(commands, "toggleconsole\nclear\n", &seat_context,
                           -1);
  (void)qa_command_buffer_execute(commands);
  CHECK(strcmp(host.last_focus, "console") == 0);
  check_dump(&console.buffer, "\n");
  qa_console_command_registration_remove(registration);
  CHECK(!qa_command_buffer_exists(commands, "condump"));
  (void)remove("/tmp/qa-console-a/out.txt");
  (void)remove("/tmp/qa-console-a/config.cfg");
  qa_seat_console_destroy(&console);
  qa_config_store_destroy(&host.store);
  free(host.printed.data);
  qa_command_buffer_destroy(commands);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);
}

/* --- draw (draw.test.ts layout case) --- */

typedef struct glyph_log {
  uint32_t points[16384];
  double xs[16384];
  double ys[16384];
  size_t count;
  int pics;
} glyph_log;

static glyph_log glyphs_seen;

static void recording_pic(void *ctx, qa_rect rect, const void *picture,
                          qa_rect clip) {
  (void)rect;
  (void)picture;
  (void)clip;
  ((glyph_log *)ctx)->pics++;
}

static void fixed_glyph_metrics(void *ctx, const void *font, uint32_t character,
                                double line_height, double cell_width,
                                double *out_line_height, double *out_top) {
  (void)ctx;
  (void)font;
  (void)character;
  (void)cell_width;
  *out_line_height = line_height;
  *out_top = 0;
}

static double fixed_layout_width(void *ctx, uint32_t character, double scale,
                                 qa_vec4 color, bool alternate,
                                 double line_height) {
  (void)ctx;
  (void)character;
  (void)scale;
  (void)color;
  (void)alternate;
  return line_height;
}

static void recording_character(void *ctx, uint32_t character, double scale,
                                qa_vec4 color, bool alternate,
                                double line_height, double x, double y,
                                qa_rect clip) {
  (void)scale;
  (void)color;
  (void)alternate;
  (void)line_height;
  (void)clip;
  glyph_log *log = (glyph_log *)ctx;
  if (log->count < 16384) {
    log->points[log->count] = character;
    log->xs[log->count] = x;
    log->ys[log->count] = y;
    log->count++;
  }
}

static void line_at(double y, char *out, size_t capacity) {
  size_t at = 0;
  for (size_t i = 0; i < glyphs_seen.count && at + 1 < capacity; i++) {
    if (glyphs_seen.ys[i] == y && glyphs_seen.points[i] < 128) {
      out[at++] = (char)glyphs_seen.points[i];
    }
  }
  out[at] = '\0';
}

static void test_draw_help_below_input(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "console-draw-320"));
  qa_seat_id seat = qa_identity_owner_seat(&identity, 0);
  memset(&glyphs_seen, 0, sizeof glyphs_seen);
  qa_console_draw_target target;
  memset(&target, 0, sizeof target);
  target.ctx = &glyphs_seen;
  target.seat = seat;
  target.width = 320;
  target.height = 240;
  target.draw_pic = recording_pic;
  qa_console_text_presentation text;
  memset(&text, 0, sizeof text);
  text.ctx = &glyphs_seen;
  text.seat = seat;
  text.glyph_metrics = fixed_glyph_metrics;
  text.layout_width = fixed_layout_width;
  text.draw_character = recording_character;
  char field_text[181];
  field_text[0] = '\0';
  for (int i = 0; i < 30; i++) strcat(field_text, "draft_");
  qa_console_field_view field = {field_text, 180, 0, false};
  qa_console_buffer buffer;
  CHECK(qa_console_buffer_init(&buffer, QA_COMMAND_DIALECT_Q3, 38,
                               QA_CONSOLE_DEFAULT_CHARACTER_CAPACITY));
  for (int i = 0; i < 60; i++) {
    char line[32];
    (void)snprintf(line, sizeof line, "scrollback %d\n", i);
    qa_console_buffer_print(&buffer, line, i);
  }
  size_t row_count = 0;
  qa_console_row *rows = qa_console_buffer_visible(&buffer, 60, &row_count);
  char usage[256] = "setting <";
  for (int i = 0; i < 12; i++) strcat(usage, "long_argument_");
  strcat(usage, ">");
  qa_console_discovery_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.kind = QA_CONSOLE_ENTRY_CVAR;
  entry.name = (char *)"setting";
  entry.usage = usage;
  entry.summary = (char *)"Actual registered summary";
  entry.value = (char *)"42";
  entry.reset_value = (char *)"0";
  qa_console_draw_options options;
  memset(&options, 0, sizeof options);
  options.draw = &target;
  options.text = &text;
  options.rows = rows;
  options.row_count = row_count;
  options.field = &field;
  options.height = 120;
  options.scale = 1;
  options.now_ms = 0;
  options.selected_entry = &entry;
  qa_draw_console(&options);
  bool inside = glyphs_seen.count > 0;
  for (size_t i = 0; i < glyphs_seen.count; i++) {
    if (glyphs_seen.xs[i] < 8 || glyphs_seen.xs[i] + 8 > 312 ||
        glyphs_seen.ys[i] < 0 || glyphs_seen.ys[i] + 8 > 120) {
      inside = false;
    }
  }
  CHECK(inside);
  char line[512];
  line_at(86, line, sizeof line);
  CHECK(line[0] == ']');
  CHECK(strlen(line) > 0 && line[strlen(line) - 1] == '|');
  line_at(94, line, sizeof line);
  CHECK(strncmp(line, "Usage: setting <", 16) == 0);
  CHECK(strlen(line) >= 3 && strcmp(line + strlen(line) - 3, "...") == 0);
  line_at(102, line, sizeof line);
  CHECK(strcmp(line, "Actual registered summary") == 0);
  line_at(110, line, sizeof line);
  CHECK(strcmp(line, "Current: \"42\"") == 0);
  CHECK(field.cursor == 180 && field.scroll == 0);
  /* Caret blink off phase: no caret glyph. */
  memset(&glyphs_seen, 0, sizeof glyphs_seen);
  qa_console_field_view small = {"a", 1, 0, false};
  options.field = &small;
  options.rows = NULL;
  options.row_count = 0;
  options.selected_entry = NULL;
  options.now_ms = 256;
  qa_draw_console(&options);
  bool caret = false;
  for (size_t i = 0; i < glyphs_seen.count; i++) {
    if (glyphs_seen.points[i] == (uint32_t)'|') caret = true;
  }
  CHECK(!caret && glyphs_seen.count == 2);
  /* Foreign-seat text aborts, as the TS throws. */
  qa_console_text_presentation foreign = text;
  foreign.seat = qa_identity_owner_seat(&identity, 1);
  options.text = &foreign;
  CHECK_ABORTS(qa_draw_console(&options));
  qa_console_rows_free(rows, row_count);
  qa_console_buffer_destroy(&buffer);
  qa_identity_owner_destroy(&identity);
}

int main(void) {
  test_buffer_resize_wrap();
  test_buffer_resize_partial();
  test_buffer_resize_scrollback();
  test_buffer_resize_overwrite_cursor();
  test_discovery_live_values();
  test_discovery_pagination();
  test_dedicated_lines();
  test_console_commands();
  test_draw_help_below_input();
  if (failures == 0) {
    printf("console-a: %d checks passed\n", checks);
    return 0;
  }
  fprintf(stderr, "console-a: %d/%d checks FAILED\n", failures, checks);
  return 1;
}
