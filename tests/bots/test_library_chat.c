/*
 * Ports the bot chat/action checks of ../quake-anthology-ts/tests:
 *   bots/behavior/library/source-behavior.test.ts (chat, elementary actions)
 *   bots/behavior/library/checkpoint.test.ts (chat checkpoint, action bytes)
 *   bots/behavior/library/retail.test.ts (retail chat, needs Q3_DATA)
 *   network/remote-fault-containment.test.ts (signed-char match offset)
 * The remaining expected values are the output of running the same scenarios
 * against the TypeScript library under bun (quoted literally below).
 * GPL-2.0-or-later.
 */
#include <dirent.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/bots/behavior.h"
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/library/call_steps.h"
#include "qa/bots/behavior/library/chat.h"
#include "qa/bots/behavior/library/chat_data.h"
#include "qa/bots/behavior/library/memory.h"
#include "qa/bots/behavior/library/script_sources.h"
#include "qa/content/archive.h"
#include "qa/ui/common/legacy/script/preprocessor.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                                                \
  do {                                                                                             \
    checks++;                                                                                      \
    if (!(cond)) {                                                                                 \
      failures++;                                                                                  \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                              \
    }                                                                                              \
  } while (0)
#define CHECK_STR(actual, expected)                                                                \
  do {                                                                                             \
    checks++;                                                                                      \
    const char *a_ = (actual), *e_ = (expected);                                                   \
    if (a_ == NULL || strcmp(a_, e_) != 0) {                                                       \
      failures++;                                                                                  \
      fprintf(stderr, "FAIL %s:%d: got [%s] want [%s]\n", __FILE__, __LINE__, a_ ? a_ : "(null)", e_); \
    }                                                                                              \
  } while (0)
#define CHECK_OWNED(actual, expected)                                                              \
  do {                                                                                             \
    char *owned_ = (actual);                                                                       \
    CHECK_STR(owned_, expected);                                                                   \
    free(owned_);                                                                                  \
  } while (0)
#define CHECK_ABORTS(...)                                                                          \
  do {                                                                                             \
    checks++;                                                                                      \
    fflush(NULL);                                                                                  \
    pid_t pid = fork();                                                                            \
    if (pid == 0) {                                                                                \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */                       \
      if (freopen("/dev/null", "w", stderr) == NULL)                                               \
        _exit(2);                                                                                  \
      __VA_ARGS__;                                                                                 \
      _exit(0);                                                                                    \
    }                                                                                              \
    int status = 0;                                                                                \
    waitpid(pid, &status, 0);                                                                      \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {                                   \
      failures++;                                                                                  \
      fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__, __LINE__);                         \
    }                                                                                              \
  } while (0)

/* ---- fixture: every leaf callback verifies the sentinel ctx it was given ---- */
#define MAX_LINES 256
typedef struct line_list {
  char *lines[MAX_LINES];
  size_t count;
} line_list;
static void lines_add(line_list *list, char *line) {
  if (list->count == MAX_LINES)
    abort();
  list->lines[list->count++] = line;
}
static void lines_clear(line_list *list) {
  for (size_t i = 0; i < list->count; i++)
    free(list->lines[i]);
  list->count = 0;
}
static char *dup_text(const char *text) {
  char *copy = malloc(strlen(text) + 1);
  strcpy(copy, text);
  return copy;
}
static char *format_text(const char *format, ...) __attribute__((format(printf, 1, 2)));
static char *format_text(const char *format, ...) {
  va_list args, measure;
  va_start(args, format);
  va_copy(measure, args);
  int length = vsnprintf(NULL, 0, format, measure);
  va_end(measure);
  char *text = malloc((size_t)length + 1);
  vsnprintf(text, (size_t)length + 1, format, args);
  va_end(args);
  return text;
}

typedef struct file_entry {
  const char *name;
  const char *text;
} file_entry;
#define FIXTURE_MAGIC 0x5e571e11u
typedef struct fixture {
  uint32_t magic;
  const file_entry *files;
  size_t file_count;
  qa_ui_script_global_defines *globals;
  qa_bot_script_sources sources;
  qa_bot_chat_library chats;
  line_list commands, reports, file_lines, log_lines;
  double now;
  int64_t random_value;
  int random_calls;
  qa_bot_chat_options options;
  double max_messages;
  void (*on_report)(struct fixture *f, const qa_chat_diagnostic *d);
} fixture;
static fixture *fixture_of(void *ctx) {
  fixture *f = ctx;
  if (f == NULL || f->magic != FIXTURE_MAGIC)
    abort();
  return f;
}
static const uint8_t *read_file(void *ctx, const char *path, size_t *out_len) {
  fixture *f = fixture_of(ctx);
  if (strncmp(path, "botfiles/", 9) != 0)
    return NULL;
  for (size_t i = 0; i < f->file_count; i++) {
    if (strcmp(f->files[i].name, path + 9) == 0) {
      *out_len = strlen(f->files[i].text);
      return (const uint8_t *)f->files[i].text;
    }
  }
  return NULL;
}
static void script_print(void *ctx, int severity, const char *text) {
  (void)fixture_of(ctx);
  (void)severity;
  (void)text;
}
static void script_common_print(void *ctx, const char *text) {
  (void)fixture_of(ctx);
  (void)text;
}
static int64_t host_random(void *ctx) {
  fixture *f = fixture_of(ctx);
  f->random_calls++;
  return f->random_value;
}
static double host_time(void *ctx) { return fixture_of(ctx)->now; }
static qa_call_steps host_command(void *ctx, int64_t client, const char *command) {
  fixture *f = fixture_of(ctx);
  lines_add(&f->commands, format_text("%lld:%s", (long long)client, command));
  return qa_call_steps_done(NULL);
}
static void host_report(void *ctx, const qa_chat_diagnostic *d) {
  fixture *f = fixture_of(ctx);
  char *location = d->has_location ? format_text("@%s:%lld:%lld", d->location.path, (long long)d->location.line,
                                                 (long long)d->location.column)
                                   : dup_text("");
  lines_add(&f->reports, format_text("%s/%s/%s/%s%s", qa_chat_severity_name(d->severity),
                                     qa_chat_diagnostic_code_name(d->code), d->source, d->message, location));
  free(location);
  if (f->on_report != NULL)
    f->on_report(f, d);
}
static void fixture_sources(fixture *f, const file_entry *files, size_t count) {
  f->magic = FIXTURE_MAGIC;
  f->files = files;
  f->file_count = count;
  f->globals = qa_ui_script_global_defines_create(NULL, NULL, NULL);
  qa_bot_script_sources_init(&f->sources);
  f->sources.globals = f->globals;
  f->sources.read_file = read_file;
  f->sources.read_ctx = f;
  f->sources.print = script_print;
  f->sources.print_ctx = f;
  f->sources.common_print = script_common_print;
  f->sources.common_print_ctx = f;
}
static void fixture_bind(fixture *f) {
  qa_bot_chat_host host;
  memset(&host, 0, sizeof host);
  host.client_command = host_command;
  host.time = host_time;
  host.report = host_report;
  host.ctx = f;
  host.random.next_int = host_random;
  host.random.ctx = f;
  qa_bot_chat_library_init(&f->chats);
  qa_bot_chat_library_bind(&f->chats, qa_bot_chat_script_reader_from_sources(&f->sources), host, &f->options,
                           &f->sources.memory);
}
static void fixture_init(fixture *f, const file_entry *files, size_t count) {
  memset(f, 0, sizeof *f);
  fixture_sources(f, files, count);
  f->now = 100;
  fixture_bind(f);
}
static void fixture_destroy(fixture *f) {
  qa_bot_chat_library_destroy(&f->chats);
  qa_bot_script_sources_destroy(&f->sources);
  qa_ui_script_global_defines_destroy(f->globals);
  lines_clear(&f->commands);
  lines_clear(&f->reports);
  lines_clear(&f->file_lines);
  lines_clear(&f->log_lines);
}
static size_t live(const fixture *f) { return qa_bot_memory_get_live_allocations(&f->sources.memory); }
static void check_lines(const line_list *list, const char *const *expected, size_t count, int line) {
  checks++;
  bool same = list->count == count;
  for (size_t i = 0; same && i < count; i++)
    same = strcmp(list->lines[i], expected[i]) == 0;
  if (same)
    return;
  failures++;
  fprintf(stderr, "FAIL %s:%d: lines differ\n", __FILE__, line);
  for (size_t i = 0; i < list->count; i++)
    fprintf(stderr, "  got[%zu] %s\n", i, list->lines[i]);
  for (size_t i = 0; i < count; i++)
    fprintf(stderr, "  want[%zu] %s\n", i, expected[i]);
}
#define CHECK_LINES(list, ...)                                                                     \
  do {                                                                                             \
    const char *const expected_[] = {__VA_ARGS__};                                                 \
    check_lines(&(list), expected_, sizeof expected_ / sizeof *expected_, __LINE__);               \
  } while (0)

/* ---- source-behavior.test.ts: chat ---- */
static const file_entry source_files[] = {
    {"syn.c", "1 { [(\"do not\",1),(\"don't\",1)] }"},
    {"rnd.c", "greeting = { \"first\"; \"last\"; }"},
    {"match.c", "1 { 0, \" says \", 1 = (7,9); }"},
    {"rchat.c", "[\"hello\"] = 5 { \"hello\"; }"},
    {"test_t.c", "chat \"test\" { type \"hello\" { \"first\"; \"last\"; } type \"compose\" { 0, \", \", greeting; } }"},
};
static qa_chat_text_source one_variable[8];
static const qa_chat_text_source *ranger_variables(void) {
  memset(one_variable, 0, sizeof one_variable);
  one_variable[0] = qa_chat_text("Ranger");
  return one_variable;
}
static void test_source_behavior_chat(void) {
  static fixture f;
  fixture_init(&f, source_files, sizeof source_files / sizeof *source_files);
  f.now = 10;
  qa_bot_chat_library *chats = &f.chats;
  qa_bot_chat_library_setup(chats);
  int64_t first = qa_bot_chat_library_allocate(chats), second = qa_bot_chat_library_allocate(chats);
  CHECK(qa_bot_chat_library_load_chat_file(chats, first, qa_chat_text("test_t.c"), qa_chat_text("test")));
  CHECK(qa_bot_chat_library_load_chat_file(chats, second, qa_chat_text("test_t.c"), qa_chat_text("test")));
  qa_bot_chat_library_initial_chat(chats, first, qa_chat_text("hello"), 0, NULL);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(chats, first, QA_CHAT_MESSAGE_SIZE), "last");
  qa_bot_chat_library_initial_chat(chats, second, qa_chat_text("hello"), 0, NULL);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(chats, second, QA_CHAT_MESSAGE_SIZE), "first");
  f.now = 50;
  qa_bot_chat_library_initial_chat(chats, second, qa_chat_text("hello"), 0, NULL);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(chats, second, QA_CHAT_MESSAGE_SIZE), "last");
  qa_chat_match match;
  CHECK(qa_bot_chat_library_find_match(chats, "Sarge says hello", 1, &match));
  CHECK(match.type == 7);
  CHECK_OWNED(qa_bot_chat_library_match_variable(chats, &match, 0, QA_CHAT_MESSAGE_SIZE), "Sarge");
  CHECK_OWNED(qa_bot_chat_library_match_variable(chats, &match, 1, QA_CHAT_MESSAGE_SIZE), "hello");
  qa_chat_match unused;
  CHECK(!qa_bot_chat_library_find_match(chats, "Sarge says hello", 2, &unused));
  CHECK_OWNED(qa_bot_chat_library_replace_synonyms(chats, "don't", 1), "do not");
  CHECK_OWNED(qa_bot_chat_library_replace_synonyms(chats, "don't", 2), "don't");
  qa_bot_chat_library_set_name(chats, first, qa_chat_text("Sarge"), true, 4);
  qa_bot_chat_library_initial_chat(chats, first, qa_chat_text("compose"), 0, ranger_variables());
  qa_bot_chat_library_enter_chat(chats, first, 0, QA_CHAT_TEAM, false, 0);
  CHECK_LINES(f.commands, "4:say_team Ranger, last");
  qa_bot_chat_library_queue_console_message(chats, first, 2, qa_chat_text("incoming"));
  f.now = 51;
  qa_console_chat_message message;
  CHECK(qa_bot_chat_library_next_console_message(chats, first, &message));
  CHECK(message.time == 50 && message.type == 2);
  CHECK_STR(message.message, "incoming");
  qa_bot_chat_library_remove_console_message(chats, first, message.handle);
  CHECK(!qa_bot_chat_library_next_console_message(chats, first, &message));
  qa_bot_chat_library_shutdown(chats);
  qa_bot_script_sources_dispose_resources(&f.sources);
  qa_bot_memory_dispose(&f.sources.memory);
  CHECK(live(&f) == 0);
  fixture_destroy(&f);
}

/* ---- source-behavior.test.ts: elementary actions ---- */
typedef struct action_host {
  uint32_t magic;
  line_list commands;
  bool wait;
  int waits;
} action_host;
static const char *action_wait_run(void *ctx) {
  action_host *h = ctx;
  if (h->magic != FIXTURE_MAGIC)
    abort();
  h->waits++;
  return NULL;
}
typedef struct waiting_steps {
  action_host *host;
  bool yielded;
} waiting_steps;
static qa_call_step_result waiting_next(void *ctx, const char *thrown) {
  waiting_steps *state = ctx;
  qa_call_step_result step = {false, NULL, {NULL, NULL}, thrown};
  if (thrown != NULL)
    return step;
  if (!state->yielded) {
    state->yielded = true;
    step.wait.run = action_wait_run;
    step.wait.ctx = state->host;
    return step;
  }
  step.done = true;
  return step;
}
static qa_call_steps action_command(void *ctx, int64_t client, const char *command) {
  action_host *h = ctx;
  if (h->magic != FIXTURE_MAGIC)
    abort();
  lines_add(&h->commands, format_text("%lld:%s", (long long)client, command));
  if (!h->wait)
    return qa_call_steps_done(NULL);
  waiting_steps *state = calloc(1, sizeof *state);
  state->host = h;
  qa_call_steps steps = {waiting_next, state, free, NULL};
  return steps;
}
static qa_vec3 lazy_direction(void *ctx) {
  ++*(int *)ctx;
  qa_vec3 v = {1, 0, 0};
  return v;
}
static void test_elementary_actions(void) {
  static action_host host = {.magic = FIXTURE_MAGIC};
  qa_bot_memory memory;
  qa_bot_memory_init(&memory);
  static qa_bot_action_buffer actions;
  qa_bot_action_buffer_init(&actions);
  qa_bot_action_host binding = {action_command, &host};
  qa_bot_action_buffer_bind(&actions, binding, &memory);
  qa_bot_action_buffer_setup(&actions, 1);
  qa_bot_action_select_weapon(&actions, 0, 5);
  qa_vec3 x = {1, 0, 0};
  qa_bot_action_move(&actions, 0, x, 500);
  qa_bot_action_jump(&actions, 0);
  qa_bot_input input = qa_bot_action_get_input(&actions, 0, 0.1);
  CHECK(input.speed == 400 && input.weapon == 5 && input.think_time == (float)0.1 &&
        input.action_flags == QA_BOT_ACTION_JUMP);
  qa_bot_action_reset_input(&actions, 0);
  qa_bot_action_jump(&actions, 0);
  CHECK((qa_bot_action_get_input(&actions, 0, 0).action_flags & QA_BOT_ACTION_JUMP) == 0);
  qa_bot_action_reset_input(&actions, 0);
  qa_bot_action_delayed_jump(&actions, 0);
  CHECK(qa_bot_action_get_input(&actions, 0, 0).action_flags == QA_BOT_ACTION_DELAYED_JUMP);
  qa_bot_action_say_team(&actions, 0, "defend");
  CHECK_LINES(host.commands, "0:say_team defend");
  qa_bot_action_buffer_shutdown(&actions);
  CHECK(qa_bot_memory_get_live_allocations(&memory) == 1);
  qa_bot_memory_reset_hunk(&memory);
  CHECK(qa_bot_memory_get_live_allocations(&memory) == 0);
  CHECK_ABORTS(qa_bot_action_jump(&actions, 0));
  qa_bot_memory_dispose(&memory);
  qa_bot_memory_destroy(&memory);
  qa_bot_action_buffer_destroy(&actions);
  lines_clear(&host.commands);

  /* TS oracle: every command, flag, byte image and edge. */
  qa_bot_action_buffer_init(&actions);
  qa_bot_action_buffer_bind(&actions, binding, NULL);
  qa_bot_action_buffer_setup(&actions, 2);
  CHECK(qa_bot_action_buffer_get_max_clients(&actions) == 2);
  qa_bot_action_say(&actions, 0, "a");
  qa_bot_action_tell(&actions, 1, 5, "t");
  qa_bot_action_use_item(&actions, 0, "i");
  qa_bot_action_drop_item(&actions, 0, "d");
  qa_bot_action_use_inventory(&actions, 0, "ui");
  qa_bot_action_drop_inventory(&actions, 0, "di");
  qa_bot_action_command(&actions, 1, "raw");
  CHECK_LINES(host.commands, "0:say a", "1:tell 5, t", "0:use i", "0:drop d", "0:invuse ui", "0:invdrop di", "1:raw");
  qa_bot_action_gesture(&actions, 0);
  qa_bot_action_talk(&actions, 0);
  qa_bot_action_attack(&actions, 0);
  qa_bot_action_use(&actions, 0);
  qa_bot_action_respawn(&actions, 0);
  qa_bot_action_crouch(&actions, 0);
  qa_bot_action_walk(&actions, 0);
  qa_bot_action_move_up(&actions, 0);
  qa_bot_action_move_down(&actions, 0);
  qa_bot_action_move_forward(&actions, 0);
  qa_bot_action_move_back(&actions, 0);
  qa_bot_action_move_left(&actions, 0);
  qa_bot_action_move_right(&actions, 0);
  qa_bot_action_action(&actions, 0, 0x100000);
  qa_vec3 view = {1.1, 2, 3}, direction = {0.1, 0, -0.0};
  qa_bot_action_view(&actions, 0, view);
  qa_bot_action_move(&actions, 0, direction, -999);
  qa_bot_action_select_weapon(&actions, 0, -3);
  input = qa_bot_action_get_input(&actions, 0, 1.1);
  CHECK((double)input.think_time == 1.100000023841858);
  CHECK(input.direction.x == 0.10000000149011612 && input.speed == -400 && input.view_angles.x == 1.100000023841858);
  CHECK(input.action_flags == 1784747 && input.weapon == -3);
  static const uint8_t expected_bytes[40] = {0, 0, 32, 64, 205, 204, 204, 61, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 200, 195,
                                             205, 204, 140, 63, 0, 0, 0, 64, 0, 0, 64, 64, 171, 59, 27, 0, 253, 255, 255, 255};
  CHECK(memcmp(qa_bot_action_get_input_bytes(&actions, 0, 2.5), expected_bytes, 40) == 0);
  qa_bot_action_jump(&actions, 1);
  qa_bot_action_reset_input(&actions, 1);
  CHECK(qa_bot_action_get_input(&actions, 1, 0).action_flags == 128);
  qa_bot_action_delayed_jump(&actions, 1);
  CHECK(qa_bot_action_get_input(&actions, 1, 0).action_flags == 128);
  qa_bot_action_reset_input(&actions, 1);
  CHECK(qa_bot_action_get_input(&actions, 1, 0).action_flags == 0);
  /* A lazy vector evaluates only after the client resolves. */
  int evaluated = 0;
  qa_bot_action_move_lazy(&actions, 0, lazy_direction, &evaluated, 1);
  qa_bot_action_view_lazy(&actions, 0, lazy_direction, &evaluated);
  CHECK(evaluated == 2);
  CHECK_ABORTS(qa_bot_action_move_lazy(&actions, 9, lazy_direction, &evaluated, 1));
  /* Range errors */
  CHECK_ABORTS(qa_bot_action_jump(&actions, 2));
  CHECK_ABORTS(qa_bot_action_jump(&actions, -1));
  CHECK_ABORTS(qa_bot_action_action(&actions, 0, 0x80000000LL));
  CHECK_ABORTS(qa_bot_action_select_weapon(&actions, 0, -0x80000001LL));
  CHECK_ABORTS(qa_bot_action_tell(&actions, 0, 0x80000000LL, "t"));
  CHECK_ABORTS(qa_bot_action_buffer_setup(&actions, -1));
  CHECK_ABORTS(qa_bot_action_buffer_setup(&actions, 0x7fffffffLL / 40 + 1));
  char *huge = malloc(32001);
  memset(huge, 'x', 32000);
  huge[32000] = 0;
  CHECK_ABORTS(qa_bot_action_say(&actions, 0, huge));
  huge[31996] = 0; /* "say " + 31996 = 32000 bytes: still over the buffer */
  CHECK_ABORTS(qa_bot_action_say(&actions, 0, huge));
  huge[31995] = 0;
  lines_clear(&host.commands);
  qa_bot_action_say(&actions, 0, huge);
  CHECK(host.commands.count == 1 && strlen(host.commands.lines[0]) == strlen("0:say ") + 31995);
  free(huge);
  /* Lazy generator: nothing runs before the first step; waits pump through. */
  lines_clear(&host.commands);
  host.wait = true;
  CHECK_ABORTS(qa_bot_action_say(&actions, 0, "async"));
  qa_call_steps steps = qa_bot_action_say_calls(&actions, 0, "async");
  CHECK(host.commands.count == 0);
  CHECK(qa_run_calls(steps) == NULL);
  CHECK(host.waits == 1);
  CHECK_LINES(host.commands, "0:say async");
  host.wait = false;
  lines_clear(&host.commands);
  qa_bot_action_buffer_dispose_resources(&actions);
  CHECK_ABORTS(qa_bot_action_jump(&actions, 0));
  qa_bot_action_buffer_destroy(&actions);
}

/* ---- call-steps ---- */
static void *call_value(void *ctx, const char **failure) {
  (void)failure;
  ++*(int *)ctx;
  return ctx;
}
static void *call_rejects(void *ctx, const char **failure) {
  (void)ctx;
  *failure = "wait rejected";
  return NULL;
}
typedef struct catching_steps {
  int phase;
  const char *caught;
  int counter;
} catching_steps;
static const char *rejecting_wait(void *ctx) {
  (void)ctx;
  return "rejected";
}
static qa_call_step_result catching_next(void *ctx, const char *thrown) {
  catching_steps *state = ctx;
  qa_call_step_result step = {false, NULL, {NULL, NULL}, NULL};
  if (state->phase++ == 0) {
    step.wait.run = rejecting_wait;
    return step;
  }
  state->caught = thrown;
  step.done = true;
  step.value = &state->counter;
  return step;
}
static void test_call_steps(void) {
  int counter = 7;
  CHECK(qa_finish_calls(qa_call_steps_done(&counter)) == &counter);
  CHECK(qa_run_calls(qa_call_steps_done(&counter)) == &counter);
  qa_call_steps pending = qa_wait_for_call(call_value, &counter);
  CHECK(counter == 7); /* lazy */
  CHECK(qa_run_calls(pending) == &counter);
  CHECK(counter == 8);
  CHECK_ABORTS(qa_finish_calls(qa_wait_for_call(call_value, &counter)));
  CHECK_ABORTS(qa_run_calls(qa_wait_for_call(call_rejects, &counter)));
  /* Resumed before the wait ran: the TS "Call resumed" error. */
  qa_call_steps early = qa_wait_for_call(call_value, &counter);
  qa_call_step_result step = qa_call_steps_next(&early, NULL);
  CHECK(!step.done && step.wait.run != NULL);
  CHECK_ABORTS(qa_call_steps_next(&early, NULL));
  qa_call_steps_release(&early);
  /* A rejected wait enters the body through throw, which may catch it. */
  catching_steps state = {0, NULL, 0};
  qa_call_steps catching = {catching_next, &state, NULL, NULL};
  CHECK(qa_run_calls(catching) == &state.counter);
  CHECK_STR(state.caught, "rejected");
}

/* ---- script/data oracle ---- */
#define RCHAT_ALL                                                                                  \
  "[\"hello\"] = 5 { \"hi ~~there\"; \"yo\", 0; }\n[name, !\"bad\"] = 7 { \"name reply\"; }\n[(\"foo \", 0)] = 3 { "       \
  "\"foo got\", 0; }\n[&\"keep\", (\" k \", 1)] = 1 { \"kk\"; }\n[<\"Sarge\",\"Ranger\">] = 2 {\"botnames\";}\n[female] = "   \
  "0.5 {\"f\";}\n[male] = 0.25 {\"m\";}\n[it] = 0.125 {\"i\";}"
#define SYN_ALL "1 { [(\"do not\",1),(\"don't\",1),(\"dont\",2)] 2 { [(\"big\",1),(\"large\",1)] } }"
#define RND_ALL "greeting = { \"first\"; \"last\"; } nest = { \"a\", greeting, \"b\"; }"
#define MATCH_ALL                                                                                  \
  "1 { 0, \" says \", 1 = (7,9); \"exact\" = (1,2); \"x\"|\"y\", 0, \"!\" = (3,4); }\n2 { \"second\" = (5,6); }"
#define TEST_T                                                                                     \
  "chat \"test\" { type \"hello\" { \"first\"; \"last\"; } type \"compose\" { 0, \", \", greeting; } type \"nest\" { nest; } " \
  "type \"tilde\" { \"a~~b~c\"; } type \"big\" { \"this is big, ok\"; } type \"bad\" { missing; } } chat \"other\" { type " \
  "\"z\" { \"z\"; } }"
static const file_entry oracle_files[] = {
    {"syn.c", SYN_ALL},   {"rnd.c", RND_ALL},   {"match.c", MATCH_ALL},
    {"rchat.c", RCHAT_ALL}, {"test_t.c", TEST_T}, {"bad_t.c", "chat \"b\" { nonsense }"},
};
#define ORACLE_COUNT (sizeof oracle_files / sizeof *oracle_files)

static const char *live_text(void *ctx, int64_t maximum_bytes) {
  int64_t *widths = ctx;
  widths[widths[0]++ + 1] = maximum_bytes;
  return "ax";
}
static void test_free_functions(void) {
  CHECK(qa_string_contains_bots_a("Hello World", "WORLD", false) == 6);
  CHECK(qa_string_contains_bots_a("Hello World", "WORLD", true) == -1);
  CHECK(qa_string_contains_bots_a("abc", "", false) == 0);
  CHECK(qa_string_contains_bots_a(NULL, "a", false) == -1);
  CHECK_OWNED(qa_unify_white_spaces_bots_a("  a  b  "), "a  b");
  CHECK_OWNED(qa_unify_white_spaces_bots_a("a!!b"), "a b");
  CHECK_OWNED(qa_unify_white_spaces_bots_a("!!"), "");
  CHECK_OWNED(qa_unify_white_spaces_bots_a("a  b  c"), "a b c");
  CHECK_OWNED(qa_unify_white_spaces_bots_a("!a!b!c!"), "a b c");
  CHECK_OWNED(qa_unify_white_spaces_bots_a("x!!!!y!!z"), "x y!!z");
  CHECK(qa_chat_text_equals("Hello", "hello", false));
  CHECK(!qa_chat_text_equals("Hello", "hello", true));
  CHECK(!qa_chat_text_equals("Hello", NULL, false));
  int64_t widths[8] = {0};
  CHECK(!qa_chat_text_equals_source("ab", qa_chat_text_live(live_text, widths), false));
  CHECK(widths[0] == 2 && widths[1] == 1 && widths[2] == 2);
  /* In-place bounds */
  CHECK_ABORTS(qa_unify_white_spaces_in_place_bots_a(NULL, 0, NULL));
  uint8_t unterminated[2] = {'a', 'b'};
  CHECK_ABORTS(qa_unify_white_spaces_in_place_bots_a(unterminated, 2, NULL));
}

static void test_library_oracle(void) {
  static fixture f;
  fixture_init(&f, oracle_files, ORACLE_COUNT);
  qa_bot_chat_library *c = &f.chats;
  qa_bot_chat_library_setup(c);
  qa_chat_configuration_counts counts = qa_bot_chat_library_configuration_counts(c);
  CHECK(counts.synonyms == 2 && counts.random_lists == 2 && counts.matches == 4 && counts.replies == 8);

  int64_t h = qa_bot_chat_library_allocate(c);
  CHECK(h == 1);
  CHECK(qa_bot_chat_library_load_chat_file(c, h, qa_chat_text("test_t.c"), qa_chat_text("test")));
  qa_initial_chat initial;
  CHECK(qa_bot_chat_library_dump_initial_chat(c, h, &initial));
  CHECK(initial.type_count == 6);
  CHECK_STR(initial.types[0].name, "bad");
  CHECK_STR(initial.types[5].name, "hello");
  CHECK_STR(initial.types[5].messages[0].text, "last");
  CHECK(initial.types[5].messages[1].time == -40);
  qa_initial_chat_destroy(&initial);
  CHECK(qa_bot_chat_library_num_initial_chats(c, h, qa_chat_text("HELLO")) == 2);
  CHECK(qa_bot_chat_library_num_initial_chats(c, h, qa_chat_text("nope")) == 0);
  CHECK(qa_bot_chat_library_num_initial_chats(c, h, qa_chat_text(NULL)) == 0);
  qa_bot_chat_library_initial_chat(c, h, qa_chat_text("hello"), 0, NULL);
  CHECK(qa_bot_chat_library_chat_length(c, h) == 4);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(c, h, QA_CHAT_MESSAGE_SIZE), "last");
  qa_bot_chat_library_initial_chat(c, h, qa_chat_text("compose"), 0, ranger_variables());
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(c, h, 5), "Rang");
  qa_bot_chat_library_initial_chat(c, h, qa_chat_text("nest"), 0, NULL);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(c, h, QA_CHAT_MESSAGE_SIZE), "alastb");
  qa_bot_chat_library_initial_chat(c, h, qa_chat_text("tilde"), 0, NULL);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(c, h, QA_CHAT_MESSAGE_SIZE), "a~bc");
  qa_bot_chat_library_initial_chat(c, h, qa_chat_text("big"), 2, NULL);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(c, h, QA_CHAT_MESSAGE_SIZE), "this is big, ok");
  qa_bot_chat_library_initial_chat(c, h, qa_chat_text("bad"), 0, NULL);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(c, h, QA_CHAT_MESSAGE_SIZE), "");
  qa_bot_chat_library_initial_chat(c, h, qa_chat_text("none"), 0, NULL);

  qa_chat_match m;
  CHECK(qa_bot_chat_library_find_match(c, "Sarge says hello\n\n", 1, &m));
  CHECK_STR(m.text, "Sarge says hello");
  CHECK(m.type == 7 && m.subtype == 9 && m.variables[0].present && m.variables[0].offset == 0 && m.variables[0].length == 5);
  CHECK(m.variables[1].offset == 11 && m.variables[1].length == 5 && !m.variables[2].present);
  CHECK(qa_bot_chat_library_find_match(c, "exact", 1, &m) && m.type == 1 && m.subtype == 2 && !m.variables[0].present);
  CHECK(qa_bot_chat_library_find_match(c, "y-mid!", 1, &m) && m.type == 3 && m.variables[0].offset == 1 && m.variables[0].length == 4);
  CHECK(qa_bot_chat_library_find_match(c, "second", 2, &m) && m.type == 5);
  CHECK(!qa_bot_chat_library_find_match(c, "nothing", 3, &m));
  CHECK(qa_bot_chat_library_find_match(c, "Sarge says hello", 1, &m));
  CHECK_OWNED(qa_bot_chat_library_match_variable(c, &m, 0, QA_CHAT_MESSAGE_SIZE), "Sarge");
  CHECK_OWNED(qa_bot_chat_library_match_variable(c, &m, 1, 3), "he");
  CHECK_OWNED(qa_bot_chat_library_match_variable(c, &m, 2, QA_CHAT_MESSAGE_SIZE), "");
  CHECK_OWNED(qa_bot_chat_library_match_variable(c, &m, 9, QA_CHAT_MESSAGE_SIZE), "");
  CHECK_OWNED(qa_bot_chat_library_replace_synonyms(c, "I don't, dont know", 1), "I do not, dont know");
  CHECK_OWNED(qa_bot_chat_library_replace_synonyms(c, "big", 3), "big");
  CHECK_OWNED(qa_bot_chat_library_replace_synonyms(c, "do not don't", 1), "do not do not");
  CHECK_OWNED(qa_bot_chat_library_replace_weighted_synonyms(c, "do not dont", 1), "do not dont");
  CHECK_OWNED(qa_bot_chat_library_replace_reply_synonyms(c, "  I don't dont\xff don't", 1), "  I do not dont\xff do not");
  CHECK_STR(qa_bot_chat_library_random_string(c, "greeting"), "last");
  CHECK(qa_bot_chat_library_random_string(c, "nope") == NULL);

  qa_bot_chat_library_set_name(c, h, qa_chat_text("Sarge"), true, 3);
  qa_bot_chat_library_set_gender(c, h, 1);
  CHECK(qa_bot_chat_library_reply_chat(c, h, qa_chat_text("hello there"), 0, 0, NULL));
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(c, h, QA_CHAT_MESSAGE_SIZE), "yo");
  CHECK(qa_bot_chat_library_reply_chat(c, h, qa_chat_text("Sarge hello"), 0, 0, NULL));
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(c, h, QA_CHAT_MESSAGE_SIZE), "name reply");
  CHECK(qa_bot_chat_library_reply_chat(c, h, qa_chat_text("foo bar"), 0, 1, NULL));
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(c, h, QA_CHAT_MESSAGE_SIZE), "foo gotbar");
  CHECK(qa_bot_chat_library_reply_chat(c, h, qa_chat_text("zzz"), 0, 0, NULL));
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(c, h, QA_CHAT_MESSAGE_SIZE), "botnames");
  qa_bot_chat_library_reset(c);

  qa_bot_chat_library_initial_chat(c, h, qa_chat_text("hello"), 0, NULL);
  qa_bot_chat_library_enter_chat(c, h, 9, QA_CHAT_ALL, false, 0);
  qa_bot_chat_library_initial_chat(c, h, qa_chat_text("hello"), 0, NULL);
  qa_bot_chat_library_enter_chat(c, h, 9, QA_CHAT_TELL, false, 0);
  qa_bot_chat_library_initial_chat(c, h, qa_chat_text("hello"), 0, NULL);
  qa_bot_chat_library_enter_chat(c, h, 9, QA_CHAT_TEAM, true, 11);
  qa_bot_chat_library_enter_chat(c, h, 9, QA_CHAT_ALL, false, 0);
  CHECK_LINES(f.commands, "3:say first", "3:tell 9 last", "11:say_team last");

  qa_bot_chat_library_queue_console_message(c, h, 2, qa_chat_text("one"));
  char long_text[301];
  memset(long_text, 'x', 300);
  long_text[300] = 0;
  qa_bot_chat_library_queue_console_message(c, h, 3, qa_chat_text(long_text));
  CHECK(qa_bot_chat_library_num_console_messages(c, h) == 2);
  qa_console_chat_message n;
  CHECK(qa_bot_chat_library_next_console_message(c, h, &n));
  CHECK(n.handle == 1 && n.time == 100 && n.type == 2);
  CHECK_STR(n.message, "one");
  qa_bot_chat_library_remove_console_message(c, h, n.handle);
  CHECK(qa_bot_chat_library_next_console_message(c, h, &n));
  CHECK(strlen(n.message) == 256 && n.handle == 2);
  CHECK(qa_bot_chat_library_num_console_messages(c, h) == 1);
  CHECK(qa_bot_chat_library_num_console_messages(c, 0) == 0);
  CHECK(qa_bot_chat_library_num_console_messages(c, 65) == 0);
  CHECK(qa_bot_chat_library_num_console_messages(c, 40) == 0);
  CHECK(qa_bot_chat_library_chat_length(c, 40) == 0);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(c, 40, QA_CHAT_MESSAGE_SIZE), "");

  int64_t h2 = qa_bot_chat_library_allocate(c);
  CHECK(!qa_bot_chat_library_load_chat_file(c, h2, qa_chat_text("test_t.c"), qa_chat_text("absent")));
  CHECK(!qa_bot_chat_library_load_chat_file(c, h2, qa_chat_text("gone.c"), qa_chat_text("x")));
  CHECK(!qa_bot_chat_library_load_chat_file(c, h2, qa_chat_text("bad_t.c"), qa_chat_text("b")));
  CHECK(qa_bot_chat_library_load_chat_file(c, h2, qa_chat_text("test_t.c"), qa_chat_text("TEST")));
  CHECK(qa_bot_chat_library_dump_initial_chat(c, h2, &initial));
  CHECK(initial.type_count == 6);
  qa_initial_chat_destroy(&initial);
  CHECK_LINES(f.reports, "info/loaded/syn.c/loaded syn.c", "info/loaded/rnd.c/loaded rnd.c",
              "info/loaded/match.c/loaded match.c", "info/loaded/rchat.c/loaded rchat.c",
              "info/loaded/test_t.c/loaded test from test_t.c",
              "error/expansion-error//BotConstructChat: unknown random string missing",
              "fatal/invalid-variable//BotMatchVariable: variable out of range",
              "fatal/invalid-handle//chat state handle 0 out of range",
              "fatal/invalid-handle//chat state handle 65 out of range", "fatal/invalid-handle//invalid chat state 40",
              "fatal/invalid-handle//invalid chat state 40", "fatal/invalid-handle//invalid chat state 40",
              "error/load-error/test_t.c/couldn't find chat absent in test_t.c",
              "fatal/load-error/test_t.c/couldn't load chat absent from test_t.c",
              "error/load-error/gone.c/counldn't load gone.c", "fatal/load-error/gone.c/couldn't load chat x from gone.c",
              "error/load-error/bad_t.c/expected type found nonsense\n@bad_t.c:1:12",
              "fatal/load-error/bad_t.c/couldn't load chat b from bad_t.c",
              "info/loaded/test_t.c/loaded TEST from test_t.c");

  /* findMatchInto and replaceSynonymsInPlace against caller buffers. */
  uint8_t bytes[256];
  qa_chat_match_buffer buffer;
  memset(&buffer, 0, sizeof buffer);
  buffer.bytes = bytes;
  buffer.length = sizeof bytes;
  CHECK(qa_bot_chat_library_find_match_into(c, qa_chat_text("Sarge says hello\n"), 1, &buffer));
  CHECK_STR((const char *)bytes, "Sarge says hello");
  CHECK(buffer.type == 7 && buffer.subtype == 9 && buffer.variables[0].offset == 0 && buffer.variables[0].length == 5);
  CHECK(buffer.variables[1].offset == 11 && buffer.variables[1].length == 5 && buffer.variables[2].offset == -1 && buffer.variables[2].length == 0);
  qa_chat_match_buffer null_buffer;
  memset(&null_buffer, 0, sizeof null_buffer);
  CHECK_ABORTS(qa_bot_chat_library_find_match_into(c, qa_chat_text("x"), 1, &null_buffer));
  fixture_destroy(&f);
}

/* Tracing writes: proves the writes channel reaches its ctx and does the work. */
typedef struct write_trace {
  uint32_t magic;
  int sets, copies, clears;
} write_trace;
static void trace_set(void *ctx, uint8_t *d, size_t n, size_t i, uint8_t v) {
  write_trace *t = ctx;
  if (t->magic != FIXTURE_MAGIC || i >= n)
    abort();
  t->sets++;
  d[i] = v;
}
static void trace_copy(void *ctx, uint8_t *d, const uint8_t *s, size_t n) {
  write_trace *t = ctx;
  if (t->magic != FIXTURE_MAGIC)
    abort();
  t->copies++;
  memmove(d, s, n);
}
static void trace_clear(void *ctx, uint8_t *d, size_t n, size_t start) {
  write_trace *t = ctx;
  if (t->magic != FIXTURE_MAGIC)
    abort();
  t->clears++;
  memset(d + start, 0, n - start);
}
typedef struct live_span {
  uint32_t magic;
  uint8_t *bytes;
  size_t length;
} live_span;
static uint8_t *live_span_bytes(void *ctx, size_t *out_length) {
  live_span *span = ctx;
  if (span->magic != FIXTURE_MAGIC)
    abort();
  *out_length = span->length;
  return span->bytes;
}
static void test_in_place(void) {
  static fixture f;
  fixture_init(&f, oracle_files, ORACLE_COUNT);
  qa_bot_chat_library_setup(&f.chats);
  uint8_t bytes[32] = "I don't care";
  live_span span = {FIXTURE_MAGIC, bytes, sizeof bytes};
  qa_bot_chat_library_replace_synonyms_in_place(&f.chats, live_span_bytes, &span, 1, NULL);
  CHECK_STR((const char *)bytes, "I do not care");
  write_trace trace = {FIXTURE_MAGIC, 0, 0, 0};
  qa_chat_buffer_writes writes = {trace_set, trace_copy, trace_clear, &trace};
  memset(bytes, 0, sizeof bytes);
  strcpy((char *)bytes, "I don't care");
  qa_bot_chat_library_replace_synonyms_in_place(&f.chats, live_span_bytes, &span, 1, &writes);
  CHECK_STR((const char *)bytes, "I do not care");
  CHECK(trace.sets > 0 && trace.clears == 2);
  /* The replacement must fit the span: TS chatSpan's RangeError. */
  uint8_t tight[8] = "don't";
  live_span tight_span = {FIXTURE_MAGIC, tight, sizeof tight - 2};
  CHECK_ABORTS(qa_bot_chat_library_replace_synonyms_in_place(&f.chats, live_span_bytes, &tight_span, 1, NULL));
  uint8_t ws[16] = "x!!!!y!!z";
  trace = (write_trace){FIXTURE_MAGIC, 0, 0, 0};
  qa_unify_white_spaces_in_place_bots_a(ws, sizeof ws, &writes);
  CHECK_STR((const char *)ws, "x y!!z");
  CHECK(trace.sets == 1 && trace.copies == 1);
  uint8_t match_bytes[256];
  qa_chat_match_buffer buffer;
  memset(&buffer, 0, sizeof buffer);
  buffer.bytes = match_bytes;
  buffer.length = sizeof match_bytes;
  buffer.writes = &writes;
  trace = (write_trace){FIXTURE_MAGIC, 0, 0, 0};
  CHECK(qa_bot_chat_library_find_match_into(&f.chats, qa_chat_text("exact\n"), 1, &buffer));
  CHECK_STR((const char *)match_bytes, "exact");
  CHECK(trace.sets == 7 && trace.clears == 1); /* 6 copied bytes, one trimmed newline */
  fixture_destroy(&f);
}

/* ---- logging, developer integrity, debug timings ---- */
static double debug_ms(void *ctx) {
  (void)fixture_of(ctx);
  return 5;
}
static void option_log_write(void *ctx, const char *text) { lines_add(&fixture_of(ctx)->log_lines, dup_text(text)); }
static bool option_has_file(void *ctx) {
  (void)fixture_of(ctx);
  return true;
}
static void option_file_write(void *ctx, const char *text) { lines_add(&fixture_of(ctx)->file_lines, dup_text(text)); }
static bool option_true(void *ctx) {
  (void)fixture_of(ctx);
  return true;
}
static char *join_lines(const line_list *list) {
  size_t size = 1;
  for (size_t i = 0; i < list->count; i++)
    size += strlen(list->lines[i]);
  char *text = calloc(size, 1);
  for (size_t i = 0; i < list->count; i++)
    strcat(text, list->lines[i]);
  return text;
}
static void test_logging(void) {
  static fixture f;
  memset(&f, 0, sizeof f);
  fixture_sources(&f, oracle_files, ORACLE_COUNT);
  f.now = 100;
  f.random_value = 0x7fff;
  f.options.ctx = &f;
  f.options.developer = option_true;
  f.options.debug_milliseconds = debug_ms;
  f.options.log_write = option_log_write;
  f.options.log_has_file = option_has_file;
  f.options.log_file_write = option_file_write;
  f.options.has_max_messages = true;
  f.options.max_messages = 2;
  fixture_bind(&f);
  qa_bot_chat_library *c = &f.chats;
  qa_bot_chat_library_setup(c);
  int64_t h = qa_bot_chat_library_allocate(c);
  CHECK(qa_bot_chat_library_load_chat_file(c, h, qa_chat_text("test_t.c"), qa_chat_text("test")));
  qa_bot_chat_library_dump_synonym_list(c);
  qa_bot_chat_library_dump_random_string_list(c);
  qa_bot_chat_library_dump_match_templates(c);
  qa_bot_chat_library_dump_reply_chat(c);
  qa_bot_chat_library_log_initial_chat(c, h);
  CHECK_OWNED(join_lines(&f.file_lines),
              "1 : [(\"do not\", 1.00), (\"don't\", 1.00), (\"dont\", 2.00)]\n3 : [(\"big\", 1.00), (\"large\", 1.00)]\n"
              "greeting = {\"last\", \"first\"}\nnest = {\"a\x01rgreeting\x01" "b\"}\n{ 0, \" says \", 1 = (7, 9);}\n"
              "{ \"exact\" = (1, 2);}\n{ \"x\"|\"y\", 0, \"!\" = (3, 4);}\n{ \"second\" = (5, 6);}\nBotDumpReplyChat:\n"
              "[it] = 0\n{\n\t\"i\";\n}\n[male] = 0\n{\n\t\"m\";\n}\n[female] = 0\n{\n\t\"f\";\n}\n[] = 2\n{\n\t\"botnames\";\n}\n"
              "[(\" k \", 1), &\"keep\"] = 1\n{\n\t\"kk\";\n}\n[(\"foo \", 0)] = 3\n{\n\t\"foo got\x01v0\x01\";\n}\n"
              "[!\"bad\", name] = 7\n{\n\t\"name reply\";\n}\n[\"hello\"] = 5\n{\n\t\"yo\x01v0\x01\";\n\t\"hi ~~there\";\n}\n");
  CHECK_LINES(f.log_lines, "missing = {\"missing\"} //MISSING RANDOM\r\n", "nest = {\"nest\"} //MISSING RANDOM\r\n",
              "greeting = {\"greeting\"} //MISSING RANDOM\r\n", "{", " type \"bad\"", " {", "  numchatmessages = 1",
              "  \"\x01rmissing\x01\"", " }", " type \"big\"", " {", "  numchatmessages = 1", "  \"this is big, ok\"", " }",
              " type \"tilde\"", " {", "  numchatmessages = 1", "  \"a~~b~c\"", " }", " type \"nest\"", " {",
              "  numchatmessages = 1", "  \"\x01rnest\x01\"", " }", " type \"compose\"", " {", "  numchatmessages = 1",
              "  \"\x01v0\x01, \x01rgreeting\x01\"", " }", " type \"hello\"", " {", "  numchatmessages = 2", "  \"last\"",
              "  \"first\"", " }", "}");
  CHECK_LINES(f.reports, "info/loaded/syn.c/loaded syn.c", "info/loaded/rnd.c/loaded rnd.c",
              "info/loaded/rnd.c/random strings 0 msec", "info/loaded/match.c/loaded match.c",
              "info/loaded/rchat.c/loaded rchat.c", "info/loaded//setup chat AI 0 msec",
              "info/loaded/test_t.c/loaded test from test_t.c", "warning/missing-random//missing random string missing",
              "warning/missing-random//missing random string nest", "warning/missing-random//missing random string greeting",
              "info/loaded//initial chats loaded in 0 msec");
  size_t before_heap_full = f.reports.count;
  qa_bot_chat_library_queue_console_message(c, h, 1, qa_chat_text("a"));
  qa_bot_chat_library_queue_console_message(c, h, 1, qa_chat_text("b"));
  CHECK(f.reports.count == before_heap_full);
  qa_bot_chat_library_queue_console_message(c, h, 1, qa_chat_text("c"));
  CHECK_STR(f.reports.lines[f.reports.count - 1], "error/message-heap-full//empty console message heap");
  /* Random 0x7fff selects the last weight bucket boundary. */
  CHECK_OWNED(qa_bot_chat_library_replace_weighted_synonyms(c, "do not don't dont", 1), "do not don't dont");
  fixture_destroy(&f);
}

/* ---- configuration failures: reports, published lists, live hunk blocks ---- */
typedef struct bad_config {
  const char *replace;
  const char *text;
  int64_t synonyms, randoms, matches, replies;
  size_t live;
  const char *reports[4];
} bad_config;
static void test_bad_configurations(void) {
  static const bad_config cases[] = {
      {"syn.c", "1 { [(\"only\",1)] }", 0, 2, 4, 8, 57,
       {"error/load-error/syn.c/synonym must have at least two entries\n@syn.c:1:16", "info/loaded/rnd.c/loaded rnd.c",
        "info/loaded/match.c/loaded match.c", "info/loaded/rchat.c/loaded rchat.c"}},
      {"syn.c", "1 { [(\"\",1),(\"a\",1)] }", 0, 2, 4, 8, 57,
       {"error/load-error/syn.c/empty string@syn.c:1:7", "info/loaded/rnd.c/loaded rnd.c",
        "info/loaded/match.c/loaded match.c", "info/loaded/rchat.c/loaded rchat.c"}},
      {"rnd.c", "1 2", 2, 0, 4, 8, 57,
       {"info/loaded/syn.c/loaded syn.c", "error/load-error/rnd.c/unknown random 1@rnd.c:1:1",
        "info/loaded/match.c/loaded match.c", "info/loaded/rchat.c/loaded rchat.c"}},
      {"match.c", "1 { 0, 1 = (1,1); }", 2, 2, 0, 8, 42,
       {"info/loaded/syn.c/loaded syn.c", "info/loaded/rnd.c/loaded rnd.c",
        "error/load-error/match.c/not allowed to have adjacent variables\n@match.c:1:8", "info/loaded/rchat.c/loaded rchat.c"}},
      {"match.c", "1 { 9 = (1,1); }", 2, 2, 0, 8, 41,
       {"info/loaded/syn.c/loaded syn.c", "info/loaded/rnd.c/loaded rnd.c",
        "error/load-error/match.c/can't have more than 8 match variables\n@match.c:1:5", "info/loaded/rchat.c/loaded rchat.c"}},
      {"rchat.c", "oops", 2, 2, 4, 0, 21,
       {"info/loaded/syn.c/loaded syn.c", "info/loaded/rnd.c/loaded rnd.c", "info/loaded/match.c/loaded match.c",
        "error/load-error/rchat.c/expected [, found oops@rchat.c:1:1"}},
  };
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    static fixture f;
    file_entry files[ORACLE_COUNT];
    memcpy(files, oracle_files, sizeof files);
    for (size_t j = 0; j < ORACLE_COUNT; j++)
      if (strcmp(files[j].name, cases[i].replace) == 0)
        files[j].text = cases[i].text;
    fixture_init(&f, files, ORACLE_COUNT);
    qa_bot_chat_library_setup(&f.chats);
    qa_chat_configuration_counts counts = qa_bot_chat_library_configuration_counts(&f.chats);
    CHECK(counts.synonyms == cases[i].synonyms && counts.random_lists == cases[i].randoms &&
          counts.matches == cases[i].matches && counts.replies == cases[i].replies);
    check_lines(&f.reports, cases[i].reports, 4, __LINE__);
    if (live(&f) != cases[i].live)
      fprintf(stderr, "case %zu live %zu want %zu\n", i, live(&f), cases[i].live);
    CHECK(live(&f) == cases[i].live);
    fixture_destroy(&f);
  }
  static fixture f;
  static const file_entry only_syn[] = {{"syn.c", ""}};
  fixture_init(&f, only_syn, 1);
  qa_bot_chat_library_setup(&f.chats);
  CHECK_LINES(f.reports, "info/loaded/syn.c/loaded syn.c", "error/load-error/rnd.c/counldn't load rnd.c",
              "error/load-error/match.c/counldn't load match.c", "error/load-error/rchat.c/counldn't load rchat.c");
  CHECK(live(&f) == 1);
  fixture_destroy(&f);
}
static void test_reply_warnings(void) {
  static fixture f;
  file_entry files[ORACLE_COUNT];
  memcpy(files, oracle_files, sizeof files);
  for (size_t j = 0; j < ORACLE_COUNT; j++)
    if (strcmp(files[j].name, "rchat.c") == 0)
      files[j].text = "[\"a\", &\"b\"] = 1 {\"x\";}\n[&\"a\"] = 1 {\"y\";}\n[!\"good\", \"very good\"] = 1 {\"z\";}\n[(0, \"x\", 1), "
                      "\"s\"] = 1 {\"q\";}\n[&\"zz\", (\"nomatch\")] = 1 {\"w\";}\n[!\"mat\", (\"match\", 0)] = 1 {\"w\";}";
  fixture_init(&f, files, ORACLE_COUNT);
  qa_bot_chat_library_setup(&f.chats);
  CHECK_LINES(f.reports, "info/loaded/syn.c/loaded syn.c", "info/loaded/rnd.c/loaded rnd.c", "info/loaded/match.c/loaded match.c",
              "warning/parse-warning/rchat.c/all keys have a & or ! prefix@rchat.c:2:6",
              "warning/parse-warning/rchat.c/the key good with prefix ! is inside the key very good@rchat.c:3:22",
              "warning/parse-warning/rchat.c/variables from the match template(s) could be invalid when outputting one of "
              "the chat messages@rchat.c:4:18",
              "warning/parse-warning/rchat.c/one of the match templates does not leave space for the key zz with the & "
              "prefix@rchat.c:5:20",
              "warning/parse-warning/rchat.c/the key mat with prefix ! is inside the match template string match@rchat.c:6:22",
              "info/loaded/rchat.c/loaded rchat.c");
  fixture_destroy(&f);

  static const file_entry no_replies[] = {{"syn.c", SYN_ALL}, {"rnd.c", RND_ALL}, {"match.c", MATCH_ALL}, {"rchat.c", ""}};
  fixture_init(&f, no_replies, 4);
  qa_bot_chat_library_setup(&f.chats);
  CHECK_STR(f.reports.lines[f.reports.count - 2], "info/loaded/rchat.c/loaded rchat.c");
  CHECK_STR(f.reports.lines[f.reports.count - 1], "info/empty-replies//no rchats");
  fixture_destroy(&f);
}
static void test_test_modes(void) {
  static fixture f;
  memset(&f, 0, sizeof f);
  fixture_sources(&f, oracle_files, ORACLE_COUNT);
  f.now = 100;
  f.options.ctx = &f;
  f.options.no_chat = option_true;
  fixture_bind(&f);
  qa_bot_chat_library_setup(&f.chats);
  CHECK(qa_bot_chat_library_configuration_counts(&f.chats).replies == 0);
  fixture_destroy(&f);

  memset(&f, 0, sizeof f);
  fixture_sources(&f, oracle_files, ORACLE_COUNT);
  f.now = 100;
  f.options.ctx = &f;
  f.options.test_initial_chats = option_true;
  f.options.test_reply_chats = option_true;
  fixture_bind(&f);
  qa_bot_chat_library *c = &f.chats;
  qa_bot_chat_library_setup(c);
  int64_t h = qa_bot_chat_library_allocate(c);
  CHECK(qa_bot_chat_library_load_chat_file(c, h, qa_chat_text("test_t.c"), qa_chat_text("test")));
  size_t base = f.reports.count;
  CHECK(qa_bot_chat_library_num_initial_chats(c, h, qa_chat_text("hello")) == 2);
  qa_bot_chat_library_initial_chat(c, h, qa_chat_text("tilde"), 0, NULL);
  qa_bot_chat_library_enter_chat(c, h, 0, 0, false, 0);
  CHECK(qa_bot_chat_library_reply_chat(c, h, qa_chat_text("hello"), 0, 0, NULL));
  CHECK(f.reports.count == base + 4);
  CHECK_STR(f.reports.lines[base], "info/test-output//hello has 2 chat lines");
  CHECK_STR(f.reports.lines[base + 1], "info/test-output//-------------------");
  CHECK_STR(f.reports.lines[base + 2], "info/test-output//a~bc");
  CHECK_STR(f.reports.lines[base + 3], "info/test-output//name reply");
  CHECK(f.commands.count == 0);
  fixture_destroy(&f);

  /* Expansion limits */
  static char large_rnd[300];
  snprintf(large_rnd, sizeof large_rnd, "large = { \"%0200d\"; } r = { large, large; }", 0);
  file_entry big_files[] = {{"syn.c", SYN_ALL}, {"rnd.c", large_rnd}, {"match.c", MATCH_ALL}, {"rchat.c", RCHAT_ALL},
                            {"test_t.c", "chat \"t\" { type \"o\" { r; } }"}};
  fixture_init(&f, big_files, 5);
  qa_bot_chat_library_setup(&f.chats);
  h = qa_bot_chat_library_allocate(&f.chats);
  CHECK(qa_bot_chat_library_load_chat_file(&f.chats, h, qa_chat_text("test_t.c"), qa_chat_text("t")));
  qa_bot_chat_library_initial_chat(&f.chats, h, qa_chat_text("o"), 0, NULL);
  CHECK_STR(f.reports.lines[f.reports.count - 1],
            "error/expansion-error//BotConstructChat: message \"\x01rlarge\x01\x01rlarge\x01\" too long");
  char *message = qa_bot_chat_library_get_chat_message(&f.chats, h, QA_CHAT_MESSAGE_SIZE);
  CHECK(strlen(message) == 200);
  free(message);
  fixture_destroy(&f);
  file_entry loop_files[] = {{"syn.c", SYN_ALL}, {"rnd.c", "loop = { loop; }"}, {"match.c", MATCH_ALL}, {"rchat.c", RCHAT_ALL},
                             {"test_t.c", "chat \"t\" { type \"loop\" { loop; } }"}};
  fixture_init(&f, loop_files, 5);
  qa_bot_chat_library_setup(&f.chats);
  h = qa_bot_chat_library_allocate(&f.chats);
  CHECK(qa_bot_chat_library_load_chat_file(&f.chats, h, qa_chat_text("test_t.c"), qa_chat_text("t")));
  qa_bot_chat_library_initial_chat(&f.chats, h, qa_chat_text("loop"), 0, NULL);
  CHECK_STR(f.reports.lines[f.reports.count - 2], "warning/expansion-limit//too many expansions in chat message");
  CHECK_STR(f.reports.lines[f.reports.count - 1], "warning/expansion-limit//\x01rloop\x01");
  fixture_destroy(&f);
}

/* ---- retirement: a host callback that retires a running load ---- */
typedef struct retire_name {
  fixture *f;
  int64_t handle;
  int calls;
} retire_name;
static const char *retiring_name(void *ctx, int64_t maximum_bytes) {
  (void)maximum_bytes;
  retire_name *r = ctx;
  if (++r->calls == 1)
    qa_bot_chat_library_free_chat_file(&r->f->chats, r->handle); /* bumps the revision */
  return "test";
}
static void test_retirement(void) {
  static fixture f;
  fixture_init(&f, oracle_files, ORACLE_COUNT);
  qa_bot_chat_library_setup(&f.chats);
  int64_t h = qa_bot_chat_library_allocate(&f.chats);
  retire_name retire = {&f, h, 0};
  size_t base = f.reports.count;
  /* A live name is wrapped in chatSourceCallback; retirement abandons the load. */
  CHECK(!qa_bot_chat_library_load_chat_file(&f.chats, h, qa_chat_text("test_t.c"), qa_chat_text_live(retiring_name, &retire)));
  CHECK(retire.calls == 1);
  CHECK(f.reports.count == base);
  qa_initial_chat initial;
  CHECK(!qa_bot_chat_library_dump_initial_chat(&f.chats, h, &initial));
  /* The library still works afterwards. */
  CHECK(qa_bot_chat_library_load_chat_file(&f.chats, h, qa_chat_text("test_t.c"), qa_chat_text("test")));
  fixture_destroy(&f);
}

/* Setup retired from inside a report: reader report (longjmp through the engine)
 * and post-parse report. Expected values are the TS oracle's. */
static const file_entry retire_files[] = {
    {"syn.c", "1 { [(\"a\",1),(\"b\",1)] }"},
    {"rnd.c", "r = { \"x\"; }"},
    {"match.c", "1 { \"m\" = (1,2); }"},
    {"rchat.c", "[&\"keep\"] = 1 { \"y\"; }\n[\"z\"] = 1 {\"w\";}"},
    {"t.c", "chat \"t\" { type \"a\" { \"A\"; } }"},
};
static void shutdown_on_warning(fixture *f, const qa_chat_diagnostic *d) {
  if (d->code == QA_CHAT_DIAG_PARSE_WARNING)
    qa_bot_chat_library_shutdown(&f->chats);
}
static void dispose_after_random(fixture *f, const qa_chat_diagnostic *d) {
  if (strcmp(d->message, "loaded rnd.c") == 0)
    qa_bot_chat_library_dispose_resources(&f->chats);
}
static void shutdown_after_match(fixture *f, const qa_chat_diagnostic *d) {
  if (strcmp(d->message, "loaded match.c") == 0)
    qa_bot_chat_library_shutdown(&f->chats);
}
static void free_chat_after_load(fixture *f, const qa_chat_diagnostic *d) {
  if (strcmp(d->message, "loaded t from t.c") == 0)
    qa_bot_chat_library_free_chat_file(&f->chats, 1);
}
static void check_zero_counts(const qa_bot_chat_library *l) {
  qa_chat_configuration_counts counts = qa_bot_chat_library_configuration_counts(l);
  CHECK(counts.synonyms == 0 && counts.random_lists == 0 && counts.matches == 0 && counts.replies == 0);
}
static void test_setup_retirement(void) {
  static fixture f;
  fixture_init(&f, retire_files, 5);
  f.on_report = shutdown_on_warning;
  qa_bot_chat_library_setup(&f.chats);
  check_zero_counts(&f.chats);
  CHECK_LINES(f.reports, "info/loaded/syn.c/loaded syn.c", "info/loaded/rnd.c/loaded rnd.c", "info/loaded/match.c/loaded match.c",
              "warning/parse-warning/rchat.c/all keys have a & or ! prefix@rchat.c:1:9");
  CHECK(live(&f) == 12);
  qa_bot_chat_library_setup(&f.chats);
  check_zero_counts(&f.chats);
  fixture_destroy(&f);

  fixture_init(&f, retire_files, 5);
  f.on_report = dispose_after_random;
  qa_bot_chat_library_setup(&f.chats);
  check_zero_counts(&f.chats);
  CHECK_LINES(f.reports, "info/loaded/syn.c/loaded syn.c", "info/loaded/rnd.c/loaded rnd.c");
  fixture_destroy(&f);

  fixture_init(&f, retire_files, 5);
  f.on_report = shutdown_after_match;
  qa_bot_chat_library_setup(&f.chats);
  check_zero_counts(&f.chats);
  CHECK_LINES(f.reports, "info/loaded/syn.c/loaded syn.c", "info/loaded/rnd.c/loaded rnd.c", "info/loaded/match.c/loaded match.c");
  fixture_destroy(&f);

  fixture_init(&f, retire_files, 5);
  f.on_report = free_chat_after_load;
  qa_bot_chat_library_setup(&f.chats);
  int64_t h = qa_bot_chat_library_allocate(&f.chats);
  CHECK(!qa_bot_chat_library_load_chat_file(&f.chats, h, qa_chat_text("t.c"), qa_chat_text("t")));
  qa_initial_chat initial;
  CHECK(!qa_bot_chat_library_dump_initial_chat(&f.chats, h, &initial));
  CHECK_STR(f.reports.lines[f.reports.count - 1], "info/loaded/t.c/loaded t from t.c");
  fixture_destroy(&f);
}

/* Escapes that only a hand-written message can produce (JS Number(key)). */
static void test_escapes(void) {
  static fixture f;
  static const file_entry files[] = {
      {"syn.c", ""}, {"rnd.c", ""}, {"match.c", ""}, {"rchat.c", ""},
      {"t.c", "chat \"t\" { type \"ws\" { \"a\x01v 0x0 \x01" "b\"; } type \"x\" { \"\x01vx\x01\"; } type \"q\" { \"p\x01q\"; } "
              "type \"big\" { \"\x01v9\x01\"; } type \"exp\" { \"\x01v1e0\x01!\"; } }"},
  };
  fixture_init(&f, files, 5);
  qa_bot_chat_library_setup(&f.chats);
  int64_t h = qa_bot_chat_library_allocate(&f.chats);
  CHECK(qa_bot_chat_library_load_chat_file(&f.chats, h, qa_chat_text("t.c"), qa_chat_text("t")));
  qa_chat_text_source vars[8];
  memset(vars, 0, sizeof vars);
  vars[0] = qa_chat_text("V0");
  vars[1] = qa_chat_text("V1");
  qa_bot_chat_library_initial_chat(&f.chats, h, qa_chat_text("ws"), 0, vars);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(&f.chats, h, QA_CHAT_MESSAGE_SIZE), "aV0b");
  qa_bot_chat_library_initial_chat(&f.chats, h, qa_chat_text("x"), 0, vars);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(&f.chats, h, QA_CHAT_MESSAGE_SIZE), "");
  CHECK_STR(f.reports.lines[f.reports.count - 1], "error/expansion-error//message variable x outside 0..7");
  qa_bot_chat_library_initial_chat(&f.chats, h, qa_chat_text("q"), 0, vars);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(&f.chats, h, QA_CHAT_MESSAGE_SIZE), "pq");
  CHECK_STR(f.reports.lines[f.reports.count - 1], "fatal/expansion-error//BotConstructChat: message \"p\x01q\" invalid escape char");
  qa_bot_chat_library_initial_chat(&f.chats, h, qa_chat_text("big"), 0, vars);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(&f.chats, h, QA_CHAT_MESSAGE_SIZE), "");
  CHECK_STR(f.reports.lines[f.reports.count - 1], "error/expansion-error//message variable 9 outside 0..7");
  qa_bot_chat_library_initial_chat(&f.chats, h, qa_chat_text("exp"), 0, vars);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(&f.chats, h, QA_CHAT_MESSAGE_SIZE), "V1!");
  fixture_destroy(&f);
}

/* ---- ChatDataParser used directly, default memories (TS oracle values) ---- */
static qa_chat_data_parser *open_parser(const char *text) {
  qa_ui_script_source source;
  memset(&source, 0, sizeof source);
  source.path = "p.c";
  source.text = (const uint8_t *)text;
  source.text_length = strlen(text);
  qa_ui_script_preprocessor_options options;
  memset(&options, 0, sizeof options);
  qa_ui_script_source_reader *reader = qa_ui_script_source_reader_open(&source, NULL, NULL, &options);
  return qa_chat_data_parser_create(reader, "p.c", NULL, NULL, NULL, NULL);
}
static void test_parser(void) {
  qa_chat_data_parser *p = open_parser("1 { 0, \" x \" = (3,4); \"a\"|\"b\" = (5,6); }");
  qa_match_template *matches;
  size_t count;
  CHECK(qa_chat_data_parser_matches(p, &matches, &count) && count == 2);
  CHECK(matches[0].context == 1 && matches[0].type == 3 && matches[0].subtype == 4 && matches[0].piece_count == 2);
  CHECK(matches[0].pieces[0].kind == QA_MATCH_PIECE_VARIABLE && matches[0].pieces[0].index == 0);
  CHECK_STR(matches[0].pieces[1].alternatives[0], " x ");
  CHECK(matches[1].type == 5 && matches[1].pieces[0].alternative_count == 2);
  CHECK_STR(matches[1].pieces[0].alternatives[1], "b");
  for (size_t i = 0; i < count; i++)
    qa_match_template_destroy(&matches[i]);
  free(matches);
  qa_chat_data_parser_dispose(p);
  qa_chat_data_parser_destroy(p);

  p = open_parser("[\"k\", !\"n\", name] = 2.5 { \"one\"; \"two\", 1, rr; }");
  qa_reply_chat *replies;
  CHECK(qa_chat_data_parser_replies(p, &replies, &count) && count == 1);
  CHECK(replies[0].key_count == 3 && replies[0].priority == 2.5 && replies[0].message_count == 2);
  CHECK(replies[0].keys[0].kind == QA_REPLY_TEST_NAME && replies[0].keys[1].mode == QA_REPLY_KEY_NOT);
  CHECK_STR(replies[0].keys[1].text, "n");
  CHECK_STR(replies[0].keys[2].text, "k");
  CHECK_STR(replies[0].messages[0].text, "two\x01v1\x01\x01rrr\x01");
  CHECK_STR(replies[0].messages[1].text, "one");
  CHECK(replies[0].messages[1].time == -40);
  qa_reply_chat_destroy(&replies[0]);
  free(replies);
  qa_chat_data_parser_dispose(p);
  qa_chat_data_parser_destroy(p);

  p = open_parser("chat \"n\" { type \"a\" { \"1\"; \"2\"; } type \"b\" { \"3\"; } } chat \"o\" { type \"c\" { \"4\"; } }");
  qa_initial_chat initial;
  CHECK(qa_chat_data_parser_initial(p, qa_chat_text("N"), false, &initial) && initial.type_count == 2);
  CHECK_STR(initial.types[0].name, "b");
  CHECK_STR(initial.types[1].messages[0].text, "2");
  CHECK_STR(initial.types[1].messages[1].text, "1");
  qa_initial_chat_destroy(&initial);
  qa_chat_data_parser_destroy(p);

  p = open_parser("chat \"n\" { type \"a\" { \"1\"; } }");
  CHECK(!qa_chat_data_parser_initial(p, qa_chat_text("missing"), false, &initial));
  CHECK(qa_chat_data_parser_failure(p) == QA_CHAT_PARSER_INITIAL_NOT_FOUND);
  CHECK_STR(qa_chat_data_parser_failure_chat_name(p), "missing");
  CHECK_STR(qa_chat_data_parser_failure_message(p), "couldn't find chat missing");
  qa_chat_data_parser_destroy(p);

  p = open_parser("chat \"n\" { bad");
  CHECK(!qa_chat_data_parser_initial(p, qa_chat_text("n"), false, &initial));
  CHECK(qa_chat_data_parser_failure(p) == QA_CHAT_PARSER_LANGUAGE_ERROR);
  CHECK_STR(qa_chat_data_parser_failure_message(p), "p.c:1:12: expected type found bad\n");
  CHECK(qa_chat_data_parser_diagnostic_count(p) == 1);
  CHECK_STR(qa_chat_data_parser_diagnostic(p, 0)->message, "expected type found bad\n");
  qa_chat_data_parser_dispose(p);
  qa_chat_data_parser_destroy(p);

  p = open_parser("nope");
  CHECK(!qa_chat_data_parser_matches(p, &matches, &count));
  CHECK(qa_chat_data_parser_failure(p) == QA_CHAT_PARSER_LANGUAGE_ERROR);
  CHECK_STR(qa_chat_data_parser_diagnostic(p, 0)->message, "expected integer, found nope\n");
  qa_chat_data_parser_destroy(p);

  p = open_parser("5 { [(\"a\",1.5),(\"b\",2)] }");
  qa_stored_synonyms synonyms;
  CHECK(qa_chat_data_parser_synonyms(p, &synonyms) && synonyms.count == 1);
  CHECK(qa_stored_synonym_group_context(&synonyms, 0) == 5 && qa_stored_synonym_group_total_weight(&synonyms, 0) == 3.5);
  CHECK_STR(qa_stored_synonym_text(&synonyms, 0, 1), "b");
  CHECK(qa_stored_synonym_weight(&synonyms, 0, 0) == 1.5);
  qa_stored_synonyms_destroy(&synonyms);
  qa_chat_data_parser_destroy(p);

  p = open_parser("n = { \"1\"; 2, m; }");
  qa_stored_randoms randoms;
  CHECK(qa_chat_data_parser_randoms(p, &randoms) && randoms.count == 1);
  CHECK_STR(qa_stored_random_name(&randoms, 0), "n");
  CHECK(qa_stored_random_num_messages(&randoms, 0) == 2);
  CHECK_STR(qa_stored_random_message(&randoms, 0, 0), "\x01v2\x01\x01rm\x01");
  CHECK_STR(qa_stored_random_message(&randoms, 0, 1), "1");
  CHECK(qa_stored_random_message(&randoms, 0, 2) == NULL);
  qa_stored_randoms_destroy(&randoms);
  qa_chat_data_parser_dispose(p);
  qa_chat_data_parser_destroy(p);
}

/* ---- checkpoint.test.ts ---- */
static const file_entry checkpoint_files[] = {
    {"syn.c", "1 { [(\"do not\",1),(\"don't\",1)] }"},
    {"rnd.c", "greeting = { \"first\"; \"last\"; }"},
    {"match.c", "1 { 0, \" says \", 1 = (7,9); }"},
    {"rchat.c", "[\"hello\"] = 5 { \"hello\"; }"},
    {"test_t.c", "chat \"test\" { type \"hello\" { \"first\"; \"last\"; } }"},
};
static void test_checkpoint(void) {
  static fixture f, g;
  fixture_init(&f, checkpoint_files, 5);
  f.now = 10;
  qa_bot_chat_library_setup(&f.chats);
  int64_t first = qa_bot_chat_library_allocate(&f.chats), second = qa_bot_chat_library_allocate(&f.chats);
  CHECK(qa_bot_chat_library_load_chat_file(&f.chats, first, qa_chat_text("test_t.c"), qa_chat_text("test")));
  CHECK(qa_bot_chat_library_load_chat_file(&f.chats, second, qa_chat_text("test_t.c"), qa_chat_text("test")));
  qa_bot_chat_library_initial_chat(&f.chats, first, qa_chat_text("hello"), 0, NULL);
  qa_bot_chat_library_queue_console_message(&f.chats, first, 4, qa_chat_text("queued"));
  qa_bot_memory_capture capture;
  CHECK(qa_bot_memory_checkpoint(&f.sources.memory, &capture));
  qa_save_value *image = qa_bot_chat_library_checkpoint(&f.chats, &capture);
  int before = f.random_calls;
  /* The saved image round-trips through the persistence envelope. */
  size_t encoded_length = 0;
  uint8_t *encoded = qa_encode_checkpoint_value(image, &encoded_length);
  qa_save_value *decoded = qa_decode_checkpoint_value(encoded, encoded_length);
  free(encoded);

  fixture_init(&g, checkpoint_files, 5);
  g.now = 10;
  qa_bot_memory_restored allocations;
  qa_bot_memory_restore(&g.sources.memory, capture.image, &allocations);
  qa_bot_chat_library_restore(&g.chats, decoded, &allocations);
  CHECK(f.random_calls == before && g.random_calls == 0);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(&g.chats, first, QA_CHAT_MESSAGE_SIZE), "last");
  qa_bot_chat_library_initial_chat(&g.chats, second, qa_chat_text("hello"), 0, NULL);
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(&g.chats, second, QA_CHAT_MESSAGE_SIZE), "first");
  qa_chat_match match;
  CHECK(qa_bot_chat_library_find_match(&g.chats, "Sarge says hello", 1, &match) && match.type == 7);
  qa_chat_configuration_counts counts = qa_bot_chat_library_configuration_counts(&g.chats);
  CHECK(counts.synonyms == 1 && counts.random_lists == 1 && counts.matches == 1 && counts.replies == 1);
  CHECK_OWNED(qa_bot_chat_library_replace_synonyms(&g.chats, "don't", 1), "do not");
  CHECK_STR(qa_bot_chat_library_random_string(&g.chats, "greeting"), "last");
  CHECK(qa_bot_chat_library_reply_chat(&g.chats, second, qa_chat_text("hello"), 0, 0, NULL));
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(&g.chats, second, QA_CHAT_MESSAGE_SIZE), "hello");
  qa_console_chat_message queued;
  CHECK(qa_bot_chat_library_next_console_message(&g.chats, first, &queued) && queued.type == 4);
  CHECK_STR(queued.message, "queued");
  CHECK(qa_bot_chat_library_num_console_messages(&g.chats, first) == 1);
  qa_bot_chat_library_queue_console_message(&g.chats, first, 5, qa_chat_text("after"));
  CHECK(qa_bot_chat_library_num_console_messages(&g.chats, first) == 2);
  /* Checkpointing the restored library reproduces the same shape. */
  qa_bot_memory_capture again;
  CHECK(qa_bot_memory_checkpoint(&g.sources.memory, &again));
  qa_save_value *second_image = qa_bot_chat_library_checkpoint(&g.chats, &again);
  CHECK(second_image != NULL);
  /* A restore over a used library is refused. */
  qa_save_value_free(second_image);
  CHECK_ABORTS(qa_bot_chat_library_restore(&g.chats, decoded, &allocations));
  /* Shutdown over restored state keeps the source accounting. */
  qa_bot_chat_library_shutdown(&g.chats);
  qa_bot_memory_capture_destroy(&again);
  qa_bot_memory_restored_destroy(&allocations);
  qa_save_value_free(decoded);
  qa_save_value_free(image);
  qa_bot_memory_capture_destroy(&capture);
  fixture_destroy(&g);
  fixture_destroy(&f);

  /* Action bytes across a memory checkpoint (the action part of the "bot
   * allocation references" test; BotLibVars and BotMoveStateStore are not in
   * this assignment). */
  qa_bot_memory memory, restored_memory;
  qa_bot_memory_init(&memory);
  qa_bot_memory_init(&restored_memory);
  static action_host host = {.magic = FIXTURE_MAGIC};
  qa_bot_action_host binding = {action_command, &host};
  static qa_bot_action_buffer actions, restored_actions;
  qa_bot_action_buffer_init(&actions);
  qa_bot_action_buffer_bind(&actions, binding, &memory);
  qa_bot_action_buffer_setup(&actions, 4);
  qa_vec3 direction = {1, 0, 0};
  qa_bot_action_move(&actions, 2, direction, 213);
  qa_bot_input input = qa_bot_action_get_input(&actions, 2, 0.25);
  CHECK(qa_bot_memory_checkpoint(&memory, &capture));
  image = qa_bot_action_buffer_checkpoint(&actions, &capture);
  qa_bot_memory_dispose(&memory);
  qa_bot_memory_restore(&restored_memory, capture.image, &allocations);
  qa_bot_action_buffer_init(&restored_actions);
  qa_bot_action_buffer_bind(&restored_actions, binding, &restored_memory);
  qa_bot_action_buffer_restore(&restored_actions, image, &allocations);
  qa_bot_input again_input = qa_bot_action_get_input(&restored_actions, 2, 0.25);
  CHECK(again_input.speed == input.speed && again_input.speed == 213 && again_input.think_time == input.think_time);
  CHECK(again_input.think_time == 0.25f && again_input.direction.x == input.direction.x && again_input.direction.x == 1);
  CHECK(again_input.action_flags == input.action_flags && again_input.weapon == input.weapon);
  CHECK(qa_bot_action_buffer_get_max_clients(&restored_actions) == 4);
  /* Restoring over live inputs and mismatched sizes are refused. */
  CHECK_ABORTS(qa_bot_action_buffer_restore(&restored_actions, image, &allocations));
  qa_save_value_free(image);
  qa_bot_memory_restored_destroy(&allocations);
  qa_bot_memory_capture_destroy(&capture);
  qa_bot_action_buffer_destroy(&actions);
  qa_bot_action_buffer_destroy(&restored_actions);
  qa_bot_memory_destroy(&memory);
  qa_bot_memory_destroy(&restored_memory);
}

/* ---- remote-fault-containment.test.ts ---- */
static void test_signed_char_offset(void) {
  static fixture f;
  static const file_entry files[] = {{"syn.c", ""}, {"rnd.c", ""}, {"match.c", "1 { 0, \": \", 1, \" help \", 2 = (7,9); }"}, {"rchat.c", ""}};
  fixture_init(&f, files, 4);
  f.now = 0;
  qa_bot_chat_library_setup(&f.chats);
  char text[256];
  char padding[131];
  memset(padding, 'a', 130);
  padding[130] = 0;
  snprintf(text, sizeof text, "bob: %s help z", padding);
  qa_chat_match match;
  CHECK(qa_bot_chat_library_find_match(&f.chats, text, 1, &match) && match.type == 7);
  /* Variable 2 starts past 127: its signed-char offset reads as absent. */
  CHECK(match.variables[0].present && match.variables[1].present && !match.variables[2].present);
  fixture_destroy(&f);
}

/* ---- retail.test.ts: skipped without the data, as the TS test is ---- */
typedef struct pack_file {
  char *path;
  uint8_t *bytes;
  size_t length;
} pack_file;
typedef struct pack_set {
  pack_file *files;
  size_t count;
} pack_set;
static int compare_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void pack_add_directory(pack_set *set, const char *root, const char *directory) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", root, directory);
  DIR *dir = opendir(path);
  if (dir == NULL)
    return;
  char *names[64];
  size_t name_count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL && name_count < 64) {
    size_t length = strlen(entry->d_name);
    if (length > 4 && strcmp(entry->d_name + length - 4, ".pk3") == 0)
      names[name_count++] = dup_text(entry->d_name);
  }
  closedir(dir);
  qsort(names, name_count, sizeof *names, compare_names);
  for (size_t i = 0; i < name_count; i++) {
    char archive_path[1100];
    snprintf(archive_path, sizeof archive_path, "%s/%s", path, names[i]);
    free(names[i]);
    qa_archive_handle *archive = qa_open_archive(archive_path, false, (qa_archive_format)0);
    if (archive == NULL)
      abort();
    size_t entry_count = archive->entry_count;
    const qa_archive_entry *entries = archive->entries;
    for (size_t e = 0; e < entry_count; e++) {
      if (entries[e].is_directory || strncmp(entries[e].path, "botfiles/", 9) != 0)
        continue;
      size_t length = 0;
      uint8_t *bytes = qa_archive_read_entry_sync_entry(archive, &entries[e], &length);
      /* BotAssetFiles.add: a later archive replaces an earlier file. */
      size_t slot = set->count;
      for (size_t k = 0; k < set->count; k++)
        if (strcmp(set->files[k].path, entries[e].path) == 0)
          slot = k;
      if (slot == set->count) {
        set->files = realloc(set->files, (set->count + 1) * sizeof *set->files);
        set->files[slot].path = dup_text(entries[e].path);
        set->count++;
      } else {
        free(set->files[slot].bytes);
      }
      set->files[slot].bytes = bytes;
      set->files[slot].length = length;
    }
    qa_archive_destroy(archive);
  }
}
static const uint8_t *pack_read(void *ctx, const char *path, size_t *out_len) {
  const pack_set *set = ctx;
  for (size_t i = 0; i < set->count; i++) {
    if (strcasecmp(set->files[i].path, path) == 0) {
      *out_len = set->files[i].length;
      return set->files[i].bytes;
    }
  }
  return NULL;
}
static void test_retail_product(const char *root, bool missionpack) {
  pack_set set = {NULL, 0};
  pack_add_directory(&set, root, "baseq3");
  if (missionpack)
    pack_add_directory(&set, root, "missionpack");
  static fixture f;
  memset(&f, 0, sizeof f);
  fixture_sources(&f, NULL, 0);
  f.sources.read_file = pack_read;
  f.sources.read_ctx = &set;
  f.now = 100;
  fixture_bind(&f);
  /* Retail text is long: report lines are irrelevant here and may exceed the list. */
  f.chats.host.report = NULL;
  qa_bot_chat_library *c = &f.chats;
  qa_bot_chat_library_setup(c);
  qa_chat_configuration_counts counts = qa_bot_chat_library_configuration_counts(c);
  CHECK(counts.synonyms == 191 && counts.random_lists == 249 && counts.matches == 182 && counts.replies == 457);
  int64_t state = qa_bot_chat_library_allocate(c);
  CHECK(qa_bot_chat_library_load_chat_file(c, state, qa_chat_text("bots/sarge_t.c"), qa_chat_text("sarge")));
  qa_initial_chat initial;
  CHECK(qa_bot_chat_library_dump_initial_chat(c, state, &initial));
  CHECK(initial.type_count == 99);
  size_t messages = 0;
  for (size_t i = 0; i < initial.type_count; i++)
    messages += initial.types[i].message_count;
  CHECK(messages == 395);
  qa_initial_chat_destroy(&initial);
  qa_bot_chat_library_initial_chat(c, state, qa_chat_text("hit_talking"), 0, ranger_variables());
  CHECK_OWNED(qa_bot_chat_library_get_chat_message(c, state, QA_CHAT_MESSAGE_SIZE),
              "Shootin' a man while he's talkin' just rubs me raw, Ranger.");
  qa_bot_chat_library_shutdown(c);
  qa_bot_script_sources_dispose_resources(&f.sources);
  qa_bot_memory_dispose(&f.sources.memory);
  CHECK(live(&f) == 0);
  fixture_destroy(&f);
  for (size_t i = 0; i < set.count; i++) {
    free(set.files[i].path);
    free(set.files[i].bytes);
  }
  free(set.files);
}
static void test_retail(void) {
  const char *root = getenv("Q3_DATA");
  char joined[1024];
  if (root == NULL) {
    const char *corpus = getenv("QTS_TEST_CORPUS");
    if (corpus == NULL)
      corpus = "";
    snprintf(joined, sizeof joined, "%s/q3a", corpus);
    root = joined;
  }
  char pak[1100], team[1100];
  snprintf(pak, sizeof pak, "%s/baseq3/pak0.pk3", root);
  snprintf(team, sizeof team, "%s/missionpack/pak0.pk3", root);
  struct stat info;
  if (stat(pak, &info) != 0 || stat(team, &info) != 0) {
    printf("SKIP retail chat: set Q3_DATA or QTS_TEST_CORPUS\n");
    return;
  }
  test_retail_product(root, false);
  test_retail_product(root, true);
  printf("ran retail chat for baseq3 and missionpack\n");
}

int main(void) {
  test_free_functions();
  test_source_behavior_chat();
  test_elementary_actions();
  test_call_steps();
  test_library_oracle();
  test_in_place();
  test_logging();
  test_bad_configurations();
  test_reply_warnings();
  test_test_modes();
  test_retirement();
  test_setup_retirement();
  test_escapes();
  test_parser();
  test_checkpoint();
  test_signed_char_offset();
  test_retail();
  printf("test_library_chat: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
