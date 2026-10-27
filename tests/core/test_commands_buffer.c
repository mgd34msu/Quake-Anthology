/*
 * Port of the CommandBuffer cases of tests/core/commands.test.ts,
 * tests/core/commands-async-exec.test.ts and
 * tests/core/commands-llm-batch.test.ts against
 * qa/core/commands/index.h. TypeScript `toThrow` cases run in a
 * child and must abort.
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

#include "qa/core/commands/index.h"

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

static bool text_is(const char *got, const char *want) {
  return got != NULL && strcmp(got, want) == 0;
}

typedef struct collected {
  char *data;
  size_t length;
  size_t capacity;
  int prints;
} collected;

static void collect_text(collected *sink, const char *text) {
  size_t length = strlen(text);
  if (sink->length + length + 1 > sink->capacity) {
    size_t cap = sink->capacity == 0 ? 256 : sink->capacity;
    while (cap < sink->length + length + 1) cap *= 2;
    sink->data = (char *)realloc(sink->data, cap);
    sink->capacity = cap;
  }
  memcpy(sink->data + sink->length, text, length + 1);
  sink->length += length;
  sink->prints++;
}
static void print_source(void *ctx, const char *text, const qa_command_context *source) {
  (void)source;
  collect_text((collected *)ctx, text);
}
static bool collected_contains(const collected *sink, const char *needle) {
  return sink->data != NULL && strstr(sink->data, needle) != NULL;
}

typedef struct call_log {
  char calls[32][96];
  int count;
} call_log;

static void log_call(call_log *log, const char *text) {
  if (log->count < 32) (void)snprintf(log->calls[log->count], 96, "%s", text);
  log->count++;
}
static bool calls_are(call_log *log, const char *const *want, int count) {
  if (log->count != count) return false;
  for (int i = 0; i < count; i++)
    if (strcmp(log->calls[i], want[i]) != 0) return false;
  return true;
}

static qa_command_context seat_context(qa_identity_owner *owner, int index) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = owner->session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  context.origin.seat = qa_identity_owner_seat(owner, index);
  context.origin.client = qa_identity_owner_client(owner, index, 0);
  return context;
}
static qa_command_context console_context(qa_identity_owner *owner) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = owner->session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  return context;
}

static qa_cvar_registry *make_cvars(qa_command_dialect dialect, qa_command_context context) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = context;
  return qa_cvar_registry_create(&options);
}
static void register_cvar(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_register(cvars, name, value, 0, &snapshot))
    qa_cvar_read_free(&snapshot);
}
static bool cvar_is(qa_cvar_registry *cvars, const char *name, const char *want) {
  char *value = qa_cvar_registry_variable_string(cvars, name);
  bool ok = text_is(value, want);
  free(value);
  return ok;
}
static qa_command_buffer *make_buffer(qa_command_dialect dialect,
                                      qa_command_context context,
                                      qa_cvar_registry *cvars, collected *printed,
                                      bool builtins) {
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = context;
  options.cvars = cvars;
  options.print = print_source;
  options.print_ctx = printed;
  options.builtins = builtins;
  options.builtins_set = true;
  return qa_command_buffer_create(&options);
}

/* commands.test.ts: registration completion is newest first and Q3
 * touches dispatched entries. */
static void choice_old(void *ctx, qa_command_invocation *command) {
  (void)command;
  log_call((call_log *)ctx, "old");
}
static void choice_new(void *ctx, qa_command_invocation *command) {
  (void)command;
  log_call((call_log *)ctx, "new");
}
static void dummy_handler(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  (void)command;
}
static void test_registration_completion(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "cmd-registration"));
  qa_command_context context = seat_context(&identity, 0);
  collected printed = {NULL, 0, 0, 0};
  qa_command_buffer *commands =
      make_buffer(QA_COMMAND_DIALECT_Q3, context, NULL, &printed, false);
  CHECK(commands != NULL);
  call_log calls = {{{0}}, 0};
  CHECK(qa_command_buffer_register(commands, "Choice", choice_old, &calls, NULL,
                                   QA_COMMAND_WORLD));
  CHECK(qa_command_buffer_register(commands, "choice", choice_new, &calls, NULL,
                                   QA_COMMAND_WORLD));
  CHECK(qa_command_buffer_register(commands, "other", dummy_handler, NULL, NULL,
                                   QA_COMMAND_WORLD));
  CHECK(!qa_command_buffer_register(commands, "choice", dummy_handler, NULL, NULL,
                                    QA_COMMAND_WORLD));
  char *completed = qa_command_buffer_complete(commands, "ch");
  CHECK(text_is(completed, "choice"));
  free(completed);
  CHECK(qa_command_buffer_execute_now(commands, "CHOICE", NULL, -1) == 1);
  static const char *const want_calls[] = {"new"};
  CHECK(calls_are(&calls, want_calls, 1));
  size_t count = 0;
  char **names = qa_command_buffer_registered_names(commands, &count);
  CHECK(count == 3 && text_is(names[0], "choice") && text_is(names[1], "other") &&
        text_is(names[2], "Choice"));
  for (size_t i = 0; i < count; i++) free(names[i]);
  free(names);
  /* unregister honors the expected handler identity */
  CHECK(!qa_command_buffer_unregister(commands, "choice", choice_old, &calls, true));
  CHECK(qa_command_buffer_unregister(commands, "choice", choice_new, &calls, true));
  CHECK(qa_command_buffer_exists(commands, "Choice"));
  free(printed.data);
  qa_command_buffer_destroy(commands);
  qa_identity_owner_destroy(&identity);
}

/* commands.test.ts: alternating seat buffers retain script source
 * through insertion, wait and nested execution. */
static qa_script_read_result read_note_script(void *ctx, const char *name,
                                              qa_command_context source) {
  (void)ctx;
  (void)name;
  (void)source;
  qa_script_read_result result;
  memset(&result, 0, sizeof result);
  result.kind = QA_SCRIPT_READ_READY;
  result.has_text = true;
  size_t length = strlen("note script;wait;note resumed");
  result.text = (char *)malloc(length + 1);
  if (result.text != NULL) memcpy(result.text, "note script;wait;note resumed", length + 1);
  return result;
}
static void note_seat_handler(void *ctx, qa_command_invocation *command) {
  call_log *log = (call_log *)ctx;
  const qa_command_origin *origin = &command->source.origin;
  const qa_command_origin *caller = origin->kind == QA_COMMAND_ORIGIN_SCRIPT &&
                                            origin->caller != NULL
                                        ? origin->caller
                                        : origin;
  long seat = caller->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? (long)caller->seat.index : -1;
  const char *kind = origin->kind == QA_COMMAND_ORIGIN_SCRIPT ? "script" : "local-seat";
  char entry[96];
  (void)snprintf(entry, sizeof entry, "%ld:%s:%s", seat,
                 command->argc > 1 ? command->argv[1] : "", kind);
  log_call(log, entry);
  if (command->argc > 1 && strcmp(command->argv[1], "nested") == 0) {
    (void)qa_command_invocation_execute_now(command, "note child");
    qa_command_invocation_assert_active(command);
  }
}
static void test_seat_script_source(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "cmd-seats"));
  call_log calls = {{{0}}, 0};
  collected printed = {NULL, 0, 0, 0};
  qa_command_buffer *buffers[2];
  for (int i = 0; i < 2; i++) {
    qa_command_buffer_options options;
    memset(&options, 0, sizeof options);
    options.dialect = QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
    options.context = seat_context(&identity, i);
    options.print = print_source;
    options.print_ctx = &printed;
    options.read_script = read_note_script;
    buffers[i] = qa_command_buffer_create(&options);
    CHECK(buffers[i] != NULL);
    CHECK(qa_command_buffer_register(buffers[i], "note", note_seat_handler, &calls, NULL,
                                     QA_COMMAND_WORLD));
  }
  qa_command_buffer_append(buffers[0], "exec script.cfg;note nested\n", NULL, -1);
  qa_command_buffer_append(buffers[1], "note second\n", NULL, -1);
  (void)qa_command_buffer_execute(buffers[0]);
  (void)qa_command_buffer_execute(buffers[1]);
  (void)qa_command_buffer_execute(buffers[0]);
  static const char *const want[] = {"0:script:script", "1:second:local-seat",
                                     "0:resumed:script", "0:nested:local-seat",
                                     "0:child:local-seat"};
  CHECK(calls_are(&calls, want, 5));
  qa_command_buffer_destroy(buffers[0]);
  qa_command_buffer_destroy(buffers[1]);
  free(printed.data);
  qa_identity_owner_destroy(&identity);
}

/* commands.test.ts: insertion, aliases, macros, deferred text and
 * waits retain family behavior. */
static void note_args_handler(void *ctx, qa_command_invocation *command) {
  call_log *log = (call_log *)ctx;
  char entry[96] = "";
  for (size_t i = 1; i < command->argc; i++) {
    if (i > 1) strcat(entry, " ");
    strcat(entry, command->argv[i]);
  }
  log_call(log, entry);
}
static void test_insert_alias_macro_defer(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "cmd-insert"));
  qa_command_context context = seat_context(&identity, 0);
  static const qa_command_dialect dialects[] = {
      QA_COMMAND_DIALECT_Q1_NETQUAKE, QA_COMMAND_DIALECT_Q1_QUAKEWORLD,
      QA_COMMAND_DIALECT_Q2_CLASSIC, QA_COMMAND_DIALECT_Q3};
  for (int i = 0; i < 4; i++) {
    collected printed = {NULL, 0, 0, 0};
    qa_command_buffer *buffer = make_buffer(dialects[i], context, NULL, &printed, false);
    CHECK(buffer != NULL);
    qa_command_buffer_append(buffer, "tail", NULL, -1);
    qa_command_buffer_insert(buffer, "head", NULL, -1);
    char *pending = qa_command_buffer_pending_text(buffer);
    bool newline = dialects[i] == QA_COMMAND_DIALECT_Q1_QUAKEWORLD ||
                   dialects[i] == QA_COMMAND_DIALECT_Q3;
    CHECK(text_is(pending, newline ? "head\ntail" : "headtail"));
    free(pending);
    free(printed.data);
    qa_command_buffer_destroy(buffer);
  }
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q2_CLASSIC, context);
  CHECK(cvars != NULL);
  register_cvar(cvars, "word", "expanded");
  collected printed = {NULL, 0, 0, 0};
  qa_command_buffer *buffer =
      make_buffer(QA_COMMAND_DIALECT_Q2_CLASSIC, context, cvars, &printed, true);
  CHECK(buffer != NULL);
  call_log calls = {{{0}}, 0};
  CHECK(qa_command_buffer_register(buffer, "note", note_args_handler, &calls, NULL,
                                   QA_COMMAND_WORLD));
  qa_command_buffer_append(
      buffer, "alias old \"note old\";alias newer \"note $word\";newer;wait 20;note \"$word\"\n",
      NULL, -1);
  qa_command_buffer_copy_to_defer(buffer);
  qa_command_buffer_append(buffer, "note tail\n", NULL, -1);
  qa_command_buffer_insert_from_defer(buffer);
  (void)qa_command_buffer_execute(buffer);
  static const char *const want_first[] = {"expanded"};
  CHECK(calls_are(&calls, want_first, 1));
  char *completed = qa_command_buffer_complete(buffer, "n");
  CHECK(text_is(completed, "note"));
  free(completed);
  size_t count = 0;
  char **aliases = qa_command_buffer_alias_names(buffer, &count);
  CHECK(count == 2 && text_is(aliases[0], "newer") && text_is(aliases[1], "old"));
  for (size_t i = 0; i < count; i++) free(aliases[i]);
  free(aliases);
  char *value = qa_command_buffer_alias_value(buffer, "OLD");
  CHECK(text_is(value, "note old\n"));
  free(value);
  (void)qa_command_buffer_execute(buffer);
  static const char *const want_all[] = {"expanded", "$word", "tail"};
  CHECK(calls_are(&calls, want_all, 3));
  /* copyPendingFrom retains the program without consuming the source */
  qa_command_buffer_append(buffer, "note kept\n", NULL, -1);
  collected printed2 = {NULL, 0, 0, 0};
  qa_command_buffer *replacement =
      make_buffer(QA_COMMAND_DIALECT_Q2_CLASSIC, context, cvars, &printed2, true);
  CHECK(replacement != NULL);
  CHECK(qa_command_buffer_register(replacement, "note", note_args_handler, &calls, NULL,
                                   QA_COMMAND_WORLD));
  qa_command_buffer_copy_pending_from(replacement, buffer);
  char *pending = qa_command_buffer_pending_text(buffer);
  CHECK(text_is(pending, "note kept\n"));
  free(pending);
  (void)qa_command_buffer_execute(replacement);
  CHECK(calls.count == 4 && text_is(calls.calls[3], "kept"));
  free(printed2.data);
  qa_command_buffer_destroy(replacement);
  free(printed.data);
  qa_command_buffer_destroy(buffer);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);
}

/* commands.test.ts: Q3 numeric wait and null callback fallback order,
 * plus buffer limits. */
typedef struct fallback_ctx {
  call_log *log;
  const char *name;
} fallback_ctx;
static int fallback_record(void *ctx, qa_command_invocation *command) {
  (void)command;
  fallback_ctx *record = (fallback_ctx *)ctx;
  log_call(record->log, record->name);
  return 0;
}
static void forward_record(void *ctx, qa_command_invocation *command) {
  (void)command;
  log_call((call_log *)ctx, "forward");
}
static void test_q3_wait_fallback(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "cmd-q3-wait"));
  qa_command_context context = seat_context(&identity, 0);
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q3, context);
  CHECK(cvars != NULL);
  register_cvar(cvars, "rate", "1");
  call_log calls = {{{0}}, 0};
  fallback_ctx client = {&calls, "client"}, server = {&calls, "server"}, ui = {&calls, "ui"};
  collected printed = {NULL, 0, 0, 0};
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context = context;
  options.cvars = cvars;
  options.print = print_source;
  options.print_ctx = &printed;
  options.client_game = fallback_record;
  options.client_game_ctx = &client;
  options.server_game = fallback_record;
  options.server_game_ctx = &server;
  options.ui = fallback_record;
  options.ui_ctx = &ui;
  options.forward_to_server = forward_record;
  options.forward_to_server_ctx = &calls;
  qa_command_buffer *buffer = qa_command_buffer_create(&options);
  CHECK(buffer != NULL);
  CHECK(qa_command_buffer_register_fallback_name(buffer, "rate"));
  qa_command_buffer_append(buffer, "rate 2;wait 2;unknown\n", NULL, -1);
  (void)qa_command_buffer_execute(buffer);
  CHECK(cvar_is(cvars, "rate", "2"));
  CHECK(calls.count == 0);
  (void)qa_command_buffer_execute(buffer);
  CHECK(calls.count == 0);
  (void)qa_command_buffer_execute(buffer);
  static const char *const want[] = {"client", "server", "ui", "forward"};
  CHECK(calls_are(&calls, want, 4));
  free(printed.data);
  qa_command_buffer_destroy(buffer);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);
}
static void test_buffer_limits(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "cmd-limits"));
  collected printed = {NULL, 0, 0, 0};
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context = seat_context(&identity, 0);
  options.print = print_source;
  options.print_ctx = &printed;
  options.max_buffer_length = 8;
  options.builtins = false;
  options.builtins_set = true;
  qa_command_buffer *buffer = qa_command_buffer_create(&options);
  CHECK(buffer != NULL);
  qa_command_buffer_append(buffer, "12345678", NULL, -1);
  char *pending = qa_command_buffer_pending_text(buffer);
  CHECK(text_is(pending, ""));
  free(pending);
  qa_command_buffer_append(buffer, "123456", NULL, -1);
  qa_command_buffer_insert(buffer, "x", NULL, -1);
  pending = qa_command_buffer_pending_text(buffer);
  CHECK(text_is(pending, "x\n123456"));
  free(pending);
  qa_command_buffer_insert(buffer, "y", NULL, -1);
  pending = qa_command_buffer_pending_text(buffer);
  CHECK(text_is(pending, "x\n123456"));
  free(pending);
  CHECK(printed.prints == 2);
  free(printed.data);
  qa_command_buffer_destroy(buffer);
  qa_identity_owner_destroy(&identity);
}

/* commands.test.ts: common cvar commands preserve numeric formatting
 * and native toggle policies. */
static void set_cvar(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_set(cvars, name, value, false, &snapshot))
    qa_cvar_read_free(&snapshot);
}
static void test_cvar_commands(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "cmd-cvars"));
  static const qa_command_dialect dialects[] = {
      QA_COMMAND_DIALECT_Q1_NETQUAKE, QA_COMMAND_DIALECT_Q1_QUAKEWORLD,
      QA_COMMAND_DIALECT_Q2_CLASSIC, QA_COMMAND_DIALECT_Q2_RERELEASE,
      QA_COMMAND_DIALECT_Q3};
  for (int i = 0; i < 5; i++) {
    qa_command_context context = seat_context(&identity, 0);
    qa_cvar_registry *cvars = make_cvars(dialects[i], context);
    CHECK(cvars != NULL);
    collected printed = {NULL, 0, 0, 0};
    qa_command_buffer *commands = make_buffer(dialects[i], context, cvars, &printed, true);
    CHECK(commands != NULL);
    register_cvar(cvars, "amount", "0");
    register_cvar(cvars, "cycle", "Red");
    register_cvar(cvars, "truth", "0.0");
    (void)qa_command_buffer_execute_now(commands, "inc amount 0.5", NULL, -1);
    CHECK(cvar_is(cvars, "amount", "0.500000"));
    (void)qa_command_buffer_execute_now(commands, "inc amount 0.5", NULL, -1);
    CHECK(cvar_is(cvars, "amount", "1"));
    (void)qa_command_buffer_execute_now(commands, "dec amount", NULL, -1);
    CHECK(cvar_is(cvars, "amount", "0"));
    (void)qa_command_buffer_execute_now(commands, "toggle truth", NULL, -1);
    CHECK(cvar_is(cvars, "truth", dialects[i] == QA_COMMAND_DIALECT_Q3 ? "1" : "0.0"));
    (void)qa_command_buffer_execute_now(commands, "toggle cycle red BLUE", NULL, -1);
    CHECK(cvar_is(cvars, "cycle", "BLUE"));
    (void)qa_command_buffer_execute_now(commands, "toggle cycle red BLUE", NULL, -1);
    CHECK(cvar_is(cvars, "cycle", "red"));
    set_cvar(cvars, "amount", "1e3");
    (void)qa_command_buffer_execute_now(commands, "inc amount", NULL, -1);
    CHECK(cvar_is(cvars, "amount", "1e3"));
    (void)qa_command_buffer_execute_now(commands, "reset amount", NULL, -1);
    CHECK(cvar_is(cvars, "amount", "0"));
    (void)qa_command_buffer_execute_now(commands, "inc absent", NULL, -1);
    qa_cvar_read absent;
    memset(&absent, 0, sizeof absent);
    CHECK(!qa_cvar_registry_find(cvars, "absent", &absent));
    set_cvar(cvars, "amount", ".");
    (void)qa_command_buffer_execute_now(commands, "inc amount", NULL, -1);
    CHECK(cvar_is(cvars, "amount", "1"));
    set_cvar(cvars, "amount", "16777216");
    (void)qa_command_buffer_execute_now(commands, "inc amount", NULL, -1);
    CHECK(cvar_is(cvars, "amount", "16777216"));
    set_cvar(cvars, "amount", "1");
    (void)qa_command_buffer_execute_now(commands, "inc amount inf", NULL, -1);
    CHECK(cvar_is(cvars, "amount", "inf"));
    CHECK(collected_contains(&printed, "can't inc"));
    free(printed.data);
    qa_command_buffer_destroy(commands);
    qa_cvar_registry_destroy(cvars);
  }
  qa_identity_owner_destroy(&identity);
}

/* commands-async-exec.test.ts / commands-output.test.ts: a pending
 * script read blocks the synchronous drain, settles under the async
 * drain, and publishes completion events; missing scripts complete
 * as missing. */
typedef struct pending_read {
  int polls;
} pending_read;
static qa_script_read_result poll_pending(void *ctx) {
  pending_read *pending = (pending_read *)ctx;
  qa_script_read_result result;
  memset(&result, 0, sizeof result);
  pending->polls++;
  if (pending->polls < 2) {
    result.kind = QA_SCRIPT_READ_PENDING;
    result.poll_ctx = pending;
    result.poll = poll_pending;
    return result;
  }
  result.kind = QA_SCRIPT_READ_READY;
  result.has_text = true;
  const char *text = "note async\n";
  result.text = (char *)malloc(strlen(text) + 1);
  if (result.text != NULL) memcpy(result.text, text, strlen(text) + 1);
  return result;
}
static qa_script_read_result read_pending_script(void *ctx, const char *name,
                                                 qa_command_context source) {
  (void)name;
  (void)source;
  qa_script_read_result result;
  memset(&result, 0, sizeof result);
  result.kind = QA_SCRIPT_READ_PENDING;
  result.poll_ctx = ctx;
  result.poll = poll_pending;
  return result;
}
static qa_script_read_result read_missing_script(void *ctx, const char *name,
                                                 qa_command_context source) {
  (void)ctx;
  (void)name;
  (void)source;
  qa_script_read_result result;
  memset(&result, 0, sizeof result);
  result.kind = QA_SCRIPT_READ_READY;
  result.has_text = false;
  return result;
}
typedef struct completion_log {
  char names[8][64];
  qa_script_result_kind results[8];
  int count;
} completion_log;
static void record_completion(void *ctx, const qa_script_completion *event) {
  completion_log *log = (completion_log *)ctx;
  if (log->count < 8) {
    (void)snprintf(log->names[log->count], 64, "%s", event->name);
    log->results[log->count] = event->result;
  }
  log->count++;
}
static void count_after_dispatch(void *ctx) { (*(int *)ctx)++; }
static void test_async_exec(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "cmd-async"));
  qa_command_context context = seat_context(&identity, 0);
  collected printed = {NULL, 0, 0, 0};
  pending_read pending = {0};
  completion_log completions = {{{0}}, {0}, 0};
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  options.context = context;
  options.print = print_source;
  options.print_ctx = &printed;
  options.read_script = read_pending_script;
  options.read_script_ctx = &pending;
  qa_command_buffer *buffer = qa_command_buffer_create(&options);
  CHECK(buffer != NULL);
  call_log calls = {{{0}}, 0};
  CHECK(qa_command_buffer_register(buffer, "note", note_args_handler, &calls, NULL,
                                   QA_COMMAND_WORLD));
  int64_t binding = qa_command_buffer_bind_script_completion(buffer, record_completion,
                                                             &completions);
  CHECK(binding > 0);
  qa_command_buffer_append(buffer, "exec later.cfg;note after\n", NULL, -1);
  (void)qa_command_buffer_execute(buffer);
  CHECK(calls.count == 0);
  CHECK(qa_command_buffer_has_pending_commands(buffer));
  int after_count = 0;
  double executed = qa_command_buffer_execute_scripts_async(buffer, count_after_dispatch,
                                                            &after_count, NULL, NULL);
  CHECK(executed == 2);
  static const char *const want[] = {"async", "after"};
  CHECK(calls_are(&calls, want, 2));
  CHECK(after_count >= 2);
  CHECK(completions.count == 1 && text_is(completions.names[0], "later.cfg") &&
        completions.results[0] == QA_SCRIPT_COMPLETED);
  CHECK(collected_contains(&printed, "execing later.cfg\n"));
  qa_command_buffer_unbind_script_completion(buffer, binding);
  free(printed.data);
  qa_command_buffer_destroy(buffer);

  collected printed2 = {NULL, 0, 0, 0};
  completion_log missing = {{{0}}, {0}, 0};
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  options.context = context;
  options.print = print_source;
  options.print_ctx = &printed2;
  options.read_script = read_missing_script;
  options.on_script_complete = record_completion;
  options.on_script_complete_ctx = &missing;
  buffer = qa_command_buffer_create(&options);
  CHECK(buffer != NULL);
  (void)qa_command_buffer_execute_now(buffer, "exec absent.cfg", NULL, -1);
  (void)qa_command_buffer_execute(buffer);
  CHECK(collected_contains(&printed2, "couldn't exec absent.cfg\n"));
  CHECK(missing.count == 1 && missing.results[0] == QA_SCRIPT_MISSING);
  free(printed2.data);
  qa_command_buffer_destroy(buffer);
  qa_identity_owner_destroy(&identity);
}

/* commands-llm-batch.test.ts: a bounded batch runs without draining
 * other pending input, and a paused batch aborts. */
static bool never_aborted(void *ctx) {
  (void)ctx;
  return false;
}
static void outer_batch_handler(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  /* The synchronous C llm_exec shape: a batch submitted from inside a
   * handler (the TS runs it after an await, outside any frame). */
  (void)qa_command_buffer_execute_batch(command->buffer, "note inner\n",
                                        command->source, never_aborted, NULL);
}
static void test_batch(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "cmd-batch"));
  qa_command_context context = console_context(&identity);
  collected printed = {NULL, 0, 0, 0};
  qa_command_buffer *buffer =
      make_buffer(QA_COMMAND_DIALECT_Q3, context, NULL, &printed, true);
  CHECK(buffer != NULL);
  call_log calls = {{{0}}, 0};
  CHECK(qa_command_buffer_register(buffer, "note", note_args_handler, &calls, NULL,
                                   QA_COMMAND_WORLD));
  qa_command_buffer_append(buffer, "note keep\n", NULL, -1);
  double count = qa_command_buffer_execute_batch(buffer, "note batch\n", context,
                                                 never_aborted, NULL);
  CHECK(count == 1);
  static const char *const want[] = {"batch"};
  CHECK(calls_are(&calls, want, 1));
  char *pending = qa_command_buffer_pending_text(buffer);
  CHECK(text_is(pending, "note keep\n"));
  free(pending);
  (void)qa_command_buffer_execute(buffer);
  CHECK(calls.count == 2 && text_is(calls.calls[1], "keep"));
  CHECK(qa_command_buffer_register(buffer, "outer", outer_batch_handler, NULL, NULL,
                                   QA_COMMAND_WORLD));
  qa_command_buffer_append(buffer, "note outerkeep\n", NULL, -1);
  (void)qa_command_buffer_execute_now(buffer, "outer", NULL, -1);
  CHECK(calls.count == 3 && text_is(calls.calls[2], "inner"));
  pending = qa_command_buffer_pending_text(buffer);
  CHECK(text_is(pending, "note outerkeep\n"));
  free(pending);
  CHECK_ABORTS((void)qa_command_buffer_execute_batch(buffer, "wait\n", context,
                                                     never_aborted, NULL));
  CHECK_ABORTS((void)qa_command_buffer_define_alias(buffer, "x", "y"));
  free(printed.data);
  qa_command_buffer_destroy(buffer);
  qa_identity_owner_destroy(&identity);
}

/* commands.test.ts preparation cases: the candidate runs its prefix
 * separately and publication preserves the inherited tail. */
typedef struct prepare_ctx {
  qa_prepared_program *program;
} prepare_ctx;
static bool run_prefix(void *ctx) {
  prepare_ctx *prepare = (prepare_ctx *)ctx;
  (void)qa_command_buffer_execute_now(prepare->program->commands, "echo prep", NULL, -1);
  return true;
}
static void test_prepare_program(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "cmd-prepare"));
  qa_command_context context = console_context(&identity);
  collected printed = {NULL, 0, 0, 0};
  qa_command_buffer *authority =
      make_buffer(QA_COMMAND_DIALECT_Q2_CLASSIC, context, NULL, &printed, true);
  CHECK(authority != NULL);
  qa_command_buffer_append(authority, "echo tail\n", NULL, -1);
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  options.context = context;
  options.print = print_source;
  options.print_ctx = &printed;
  qa_prepared_program *program = qa_command_buffer_prepare_program(authority, &options);
  CHECK(program != NULL && program->commands != NULL);
  char *pending = qa_command_buffer_pending_text(program->commands);
  CHECK(text_is(pending, "echo tail\n"));
  free(pending);
  prepare_ctx prepare = {program};
  CHECK(program->prepare_prefix(program->ctx, run_prefix, &prepare));
  CHECK(collected_contains(&printed, "prep \n"));
  CHECK(!collected_contains(&printed, "tail \n"));
  program->validate_publication(program->ctx);
  program->publish(program->ctx);
  CHECK(qa_command_buffer_has_pending_commands(authority));
  (void)qa_command_buffer_execute(authority);
  CHECK(collected_contains(&printed, "tail \n"));
  CHECK(qa_command_buffer_program_complete(authority));
  program->destroy(program->ctx);
  free(printed.data);
  qa_command_buffer_destroy(authority);
  qa_identity_owner_destroy(&identity);
}

/* stuffcmds from the command line, producer discard, and the Q1
 * unknown-command print. */
static void test_stuffcmds_and_misc(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "cmd-misc"));
  qa_command_context context = console_context(&identity);
  collected printed = {NULL, 0, 0, 0};
  static const char *const command_line[] = {"prog", "+note a", "-x", "+note b"};
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  options.context = context;
  options.print = print_source;
  options.print_ctx = &printed;
  options.command_line = command_line;
  options.command_line_count = 4;
  qa_command_buffer *buffer = qa_command_buffer_create(&options);
  CHECK(buffer != NULL);
  call_log calls = {{{0}}, 0};
  CHECK(qa_command_buffer_register(buffer, "note", note_args_handler, &calls, NULL,
                                   QA_COMMAND_WORLD));
  (void)qa_command_buffer_execute_now(buffer, "stuffcmds", NULL, -1);
  (void)qa_command_buffer_execute(buffer);
  static const char *const want[] = {"a", "b"};
  CHECK(calls_are(&calls, want, 2));
  (void)qa_command_buffer_execute_now(buffer, "nosuchthing", NULL, -1);
  CHECK(collected_contains(&printed, "Unknown command \"nosuchthing\"\n"));
  CHECK(qa_command_buffer_maximum_buffer_length(buffer) == 8192);
  CHECK(qa_command_buffer_maximum_command_length(buffer) == 1024);
  free(printed.data);
  qa_command_buffer_destroy(buffer);

  /* Producer-scoped pending work is discarded on unbind shape. */
  collected printed2 = {NULL, 0, 0, 0};
  qa_command_buffer *produced =
      make_buffer(QA_COMMAND_DIALECT_Q2_CLASSIC, seat_context(&identity, 0), NULL,
                  &printed2, true);
  CHECK(produced != NULL);
  static const qa_module_identity module = {"mod:1", "mod.bin", "digest", "r1"};
  int instance_token = 0;
  qa_command_producer producer;
  memset(&producer, 0, sizeof producer);
  producer.kind = "client-module";
  producer.module = module;
  producer.instance = &instance_token;
  qa_command_context produced_context = seat_context(&identity, 0);
  produced_context.producer = &producer;
  qa_command_buffer_append(produced, "echo produced\n", &produced_context, -1);
  qa_command_buffer_append(produced, "echo plain\n", NULL, -1);
  CHECK(qa_command_buffer_producer_pending(produced, &instance_token));
  qa_command_buffer_discard_producer(produced, &instance_token);
  CHECK(!qa_command_buffer_producer_pending(produced, &instance_token));
  char *pending = qa_command_buffer_pending_text(produced);
  CHECK(text_is(pending, "echo plain\n"));
  free(pending);
  free(printed2.data);
  qa_command_buffer_destroy(produced);
  qa_identity_owner_destroy(&identity);
}

int main(void) {
  test_registration_completion();
  test_seat_script_source();
  test_insert_alias_macro_defer();
  test_q3_wait_fallback();
  test_buffer_limits();
  test_cvar_commands();
  test_async_exec();
  test_batch();
  test_prepare_program();
  test_stuffcmds_and_misc();
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("all %d checks passed\n", checks);
  return 0;
}
