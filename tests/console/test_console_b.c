/*
 * Port of the key cases in tests/console/console-input.test.ts,
 * session-completion.test.ts, llm.test.ts and metrics.test.ts for
 * the slice-b files (field, source-field, metrics, session,
 * llm, llm-batch). The TS input-router/SeatInput layers are not
 * ported yet, so session cases drive qa_seat_console_input with
 * the same event values the router delivered. The LLM requester
 * is a synchronous double (async flows are synchronous in C).
 * TypeScript `toThrow` cases run in a child and must abort.
 * GPL-2.0-or-later.
 */
#include <math.h>
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

#include "qa/console/field.h"
#include "qa/console/llm.h"
#include "qa/console/llm_batch.h"
#include "qa/console/metrics.h"
#include "qa/console/session.h"

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

/* KeyCode values from src/input/key-codes.ts. */
enum {
  KEY_TAB = 9, KEY_ENTER = 13, KEY_ESCAPE = 27, KEY_BACKSPACE = 127,
  KEY_UP = 132, KEY_DOWN = 133, KEY_LEFT = 134, KEY_RIGHT = 135,
  KEY_CONTROL = 137, KEY_SHIFT = 138, KEY_INSERT = 139, KEY_DELETE = 140,
  KEY_HOME = 143, KEY_END = 144, KEY_KEYPAD_INSERT = 170
};

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

typedef struct collected {
  char *data;
  size_t length;
  size_t capacity;
} collected;

static void collect_text(collected *sink, const char *text) {
  size_t length = strlen(text);
  if (sink->length + length + 1 > sink->capacity) {
    size_t capacity = sink->capacity == 0 ? 256 : sink->capacity;
    while (capacity < sink->length + length + 1) capacity *= 2;
    char *data = (char *)realloc(sink->data, capacity);
    if (data == NULL) return;
    sink->data = data;
    sink->capacity = capacity;
  }
  memcpy(sink->data + sink->length, text, length + 1);
  sink->length += length;
}
static bool collected_contains(const collected *sink, const char *needle) {
  return sink->data != NULL && strstr(sink->data, needle) != NULL;
}
static void collected_reset(collected *sink) {
  free(sink->data);
  sink->data = NULL;
  sink->length = 0;
  sink->capacity = 0;
}

/* --- field (field.ts) --- */

static void check_field_text(qa_console_field *field, const char *want) {
  char *text = qa_console_field_text(field);
  CHECK(text != NULL && strcmp(text, want) == 0);
  free(text);
}

static const char *null_clipboard(void *ctx) {
  (void)ctx;
  return NULL;
}
static const char *paste_clipboard(void *ctx) {
  return *(const char *const *)ctx;
}

static void test_console_field_editing(void) {
  qa_console_field field;
  CHECK(qa_console_field_init(&field, 1023, 78));
  qa_console_field_insert(&field, "echo hi");
  check_field_text(&field, "echo hi");
  CHECK(field.cursor == 7);
  /* Control characters are skipped on insert and stripped by setText. */
  qa_console_field_insert(&field, "\x01\x7f!");
  check_field_text(&field, "echo hi!");
  qa_console_field_set_text(&field, "a\tb\x7f" "c");
  check_field_text(&field, "abc");
  CHECK(field.cursor == 3);
  /* Backspace/Delete/word navigation (field.key + word). */
  qa_console_field_set_text(&field, "one two three");
  CHECK(qa_console_field_key(&field, KEY_LEFT, true, false, null_clipboard, NULL));
  CHECK(field.cursor == 8);
  CHECK(qa_console_field_key(&field, 119, true, false, null_clipboard, NULL)); /* Ctrl+W */
  check_field_text(&field, "one three");
  CHECK(field.cursor == 4);
  CHECK(qa_console_field_key(&field, 107, true, false, null_clipboard, NULL)); /* Ctrl+K */
  check_field_text(&field, "one ");
  CHECK(qa_console_field_key(&field, 97, true, false, null_clipboard, NULL)); /* Ctrl+A */
  CHECK(field.cursor == 0);
  CHECK(qa_console_field_key(&field, KEY_DELETE, false, false, null_clipboard, NULL));
  check_field_text(&field, "ne ");
  CHECK(qa_console_field_key(&field, KEY_END, false, false, null_clipboard, NULL));
  CHECK(qa_console_field_key(&field, KEY_BACKSPACE, false, false, null_clipboard, NULL));
  check_field_text(&field, "ne");
  /* Overstrike via Insert. */
  CHECK(qa_console_field_key(&field, KEY_HOME, false, false, null_clipboard, NULL));
  CHECK(qa_console_field_key(&field, KEY_INSERT, false, false, null_clipboard, NULL));
  CHECK(field.overstrike);
  qa_console_field_insert(&field, "o");
  check_field_text(&field, "oe");
  /* Ctrl+V pastes only the first clipboard line. */
  const char *clip = "pasted\r\nrest";
  CHECK(qa_console_field_key(&field, KEY_INSERT, false, false, null_clipboard, NULL));
  CHECK(qa_console_field_key(&field, 118, true, false, paste_clipboard, &clip));
  check_field_text(&field, "opastede");
  /* Shift+Insert pastes too. */
  const char *clip2 = "X";
  CHECK(qa_console_field_key(&field, KEY_KEYPAD_INSERT, false, true, paste_clipboard, &clip2));
  check_field_text(&field, "opastedXe");
  /* Unhandled keys fall through. */
  CHECK(!qa_console_field_key(&field, 145, false, false, null_clipboard, NULL));
  /* Maximum length caps inserts. */
  qa_console_field small;
  CHECK(qa_console_field_init(&small, 3, 78));
  qa_console_field_insert(&small, "abcdef");
  check_field_text(&small, "abc");
  qa_console_field_set_text(&small, "wxyz");
  check_field_text(&small, "wxy");
  qa_console_field_destroy(&small);
  qa_console_field_destroy(&field);
}

static void test_console_field_completion(void) {
  qa_console_field field;
  CHECK(qa_console_field_init(&field, 1023, 78));
  const char *names[] = {"zz_second", "zz_first", "echo", "ZZ_FIRST"};
  qa_console_field_set_text(&field, "zz_");
  qa_console_field_complete(&field, names, 4, false);
  check_field_text(&field, "/zz_first");
  const char *selected = qa_console_field_selected_completion(&field);
  CHECK(selected != NULL && strcmp(selected, "zz_first") == 0);
  qa_console_field_complete(&field, names, 4, false);
  check_field_text(&field, "/zz_second");
  qa_console_field_complete(&field, names, 4, true);
  check_field_text(&field, "/zz_first");
  /* Editing clears the cycle. */
  qa_console_field_insert(&field, "x");
  CHECK(qa_console_field_selected_completion(&field) == NULL);
  /* No match leaves the field alone. */
  qa_console_field_set_text(&field, "qqq");
  qa_console_field_complete(&field, names, 4, false);
  check_field_text(&field, "qqq");
  qa_console_field_destroy(&field);

  qa_completion_result result;
  CHECK(qa_complete_command("/zz_", names, 4, &result));
  CHECK(result.text != NULL && strcmp(result.text, "/zz_") == 0);
  CHECK(result.match_count == 3); /* zz_second, zz_first, ZZ_FIRST */
  qa_completion_result_destroy(&result);
  CHECK(qa_complete_command("ec", names, 4, &result));
  CHECK(result.text != NULL && strcmp(result.text, "echo ") == 0);
  CHECK(result.match_count == 1);
  qa_completion_result_destroy(&result);
  CHECK(qa_complete_command("", names, 4, &result));
  CHECK(result.text != NULL && strcmp(result.text, "") == 0);
  CHECK(result.match_count == 0);
  qa_completion_result_destroy(&result);
}

static void test_console_history(void) {
  qa_console_history history;
  CHECK(qa_console_history_init(&history, 3));
  qa_console_history_add(&history, "");
  CHECK(qa_console_history_line_count(&history) == 0);
  qa_console_history_add(&history, "echo one");
  qa_console_history_add(&history, "echo two");
  qa_console_history_add(&history, "echo three");
  qa_console_history_add(&history, "echo four"); /* evicts "echo one" */
  size_t count = 0;
  const char **lines = qa_console_history_lines(&history, &count);
  CHECK(count == 3 && strcmp(lines[0], "echo two") == 0 &&
        strcmp(lines[2], "echo four") == 0);
  free((void *)lines);
  /* console-input.test.ts draft behavior. */
  CHECK(strcmp(qa_console_history_previous(&history, "unfinished draft"),
               "echo four") == 0);
  CHECK(strcmp(qa_console_history_previous(&history, "echo four"), "echo three") == 0);
  CHECK(strcmp(qa_console_history_next(&history), "echo four") == 0);
  CHECK(strcmp(qa_console_history_next(&history), "unfinished draft") == 0);
  const char *replacement[] = {"", "alpha", "beta"};
  qa_console_history_replace(&history, replacement, 3);
  CHECK(qa_console_history_line_count(&history) == 2);
  CHECK(strcmp(qa_console_history_previous(&history, ""), "beta") == 0);
  qa_console_history_destroy(&history);
}

/* --- source-field (source-field.ts) --- */

typedef struct edit_controls {
  bool shift;
  bool control;
  bool overstrike;
  const uint8_t *clip;
  size_t clip_length;
  bool clip_null;
} edit_controls;

static bool edit_is_down(void *ctx, int64_t key) {
  edit_controls *controls = (edit_controls *)ctx;
  if (key == KEY_SHIFT) return controls->shift;
  if (key == KEY_CONTROL) return controls->control;
  return false;
}
static bool edit_get_overstrike(void *ctx) { return ((edit_controls *)ctx)->overstrike; }
static void edit_set_overstrike(void *ctx, bool value) {
  ((edit_controls *)ctx)->overstrike = value;
}
static bool edit_clip_read(void *ctx, const uint8_t **out, size_t *length) {
  edit_controls *controls = (edit_controls *)ctx;
  if (controls->clip_null) return false;
  *out = controls->clip;
  *length = controls->clip_length;
  return true;
}

static qa_field_controls make_edit_controls(edit_controls *state) {
  qa_field_controls controls;
  memset(&controls, 0, sizeof controls);
  controls.ctx = state;
  controls.is_down = edit_is_down;
  controls.get_overstrike = edit_get_overstrike;
  controls.set_overstrike = edit_set_overstrike;
  controls.clipboard.kind = QA_FIELD_CLIPBOARD_AVAILABLE;
  controls.clipboard.ctx = state;
  controls.clipboard.read = edit_clip_read;
  return controls;
}

static void check_edit_text(qa_edit_field *field, const char *want) {
  char *text = qa_edit_field_text(field);
  CHECK(text != NULL && strcmp(text, want) == 0);
  free(text);
}

static void test_edit_field(void) {
  edit_controls state;
  memset(&state, 0, sizeof state);
  qa_field_controls controls = make_edit_controls(&state);
  qa_edit_field field;
  qa_edit_field_init(&field);
  field.width_in_chars = 10;
  qa_edit_field_set_text(&field, "hello");
  field.cursor = 5;
  check_edit_text(&field, "hello");
  qa_edit_field_char_event(&field, '!', &controls);
  check_edit_text(&field, "hello!");
  qa_edit_field_char_event(&field, 8, &controls); /* backspace */
  check_edit_text(&field, "hello");
  qa_edit_field_char_event(&field, 1, &controls); /* ^A */
  CHECK(field.cursor == 0 && field.scroll == 0);
  qa_edit_field_char_event(&field, 5, &controls); /* ^E */
  CHECK(field.cursor == 5);
  qa_edit_field_key_down(&field, KEY_LEFT, &controls);
  CHECK(field.cursor == 4);
  qa_edit_field_key_down(&field, KEY_DELETE, &controls);
  check_edit_text(&field, "hell");
  qa_edit_field_key_down(&field, KEY_HOME, &controls);
  CHECK(field.cursor == 0);
  qa_edit_field_key_down(&field, KEY_END, &controls);
  CHECK(field.cursor == 4);
  /* Insert toggles overstrike through the controls. */
  qa_edit_field_key_down(&field, KEY_INSERT, &controls);
  CHECK(state.overstrike);
  qa_edit_field_key_down(&field, KEY_HOME, &controls);
  qa_edit_field_char_event(&field, 'H', &controls);
  check_edit_text(&field, "Hell");
  state.overstrike = false;
  /* Ctrl+V (22) pastes clipboard bytes; high bytes go in signed. */
  static const uint8_t clip[] = {'a', 'b', 0, 'c'};
  state.clip = clip;
  state.clip_length = sizeof clip;
  qa_edit_field_char_event(&field, 22, &controls);
  check_edit_text(&field, "Habell");
  /* ^C (3) clears. */
  qa_edit_field_char_event(&field, 3, &controls);
  check_edit_text(&field, "");
  CHECK(qa_edit_field_read_byte(&field, 0) == 0);
  qa_edit_field_write_byte(&field, 0, 'x');
  CHECK(qa_edit_field_read_byte(&field, 0) == 'x');
  /* RangeError cases abort. */
  CHECK_ABORTS(qa_edit_field_read_byte(&field, 256));
  CHECK_ABORTS(qa_edit_field_read_byte(&field, -1));
  CHECK_ABORTS(qa_edit_field_write_byte(&field, 0, 256));
  CHECK_ABORTS(qa_edit_field_char_event(&field, 300, &controls));
  CHECK_ABORTS(qa_edit_field_key_down(&field, 2147483648LL, &controls));
  CHECK_ABORTS({
    qa_edit_field bad;
    qa_edit_field_init(&bad);
    memset(bad.buffer, 'a', sizeof bad.buffer);
    free(qa_edit_field_text(&bad)); /* unterminated */
  });
  CHECK_ABORTS({
    char big[300];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    qa_edit_field_set_text(&field, big);
  });
  qa_edit_field_destroy(&field);
}

/* --- metrics (metrics.test.ts) --- */

static qa_renderer_image test_image(qa_identity_owner *owner, double width, double height) {
  qa_renderer_image image = {0};
  image.owner.identity = owner;
  image.owner.session = owner->session;
  image.owner.generation = 0;
  image.ordinal = 0;
  image.source_is_resource = false;
  image.source_name = "test";
  image.width = width;
  image.height = height;
  return image;
}

static void test_console_metrics(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "console-metrics"));
  qa_text_atlas charset;
  CHECK(qa_classic_charset(test_image(&owner, 128, 128), NULL, true, &charset));
  qa_text_font_selection font = {0};
  font.classic = &charset;
  static const struct {
    double width, height, ratio, expected;
  } cases[] = {{640, 480, 1, 2}, {960, 720, 1, 2}, {1280, 960, 2, 4},
               {320, 240, 1, 1}, {640, 240, 1, 2}};
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    qa_console_metrics_options options = {cases[i].width, cases[i].height,
                                          cases[i].ratio, 0, &font};
    qa_console_metrics metrics = qa_console_metrics_for(&options);
    CHECK(metrics.scale == cases[i].expected);
    CHECK(metrics.cell_width == 8 * cases[i].expected);
    CHECK(metrics.columns * metrics.cell_width + 16 * metrics.scale <= cases[i].width);
  }
  for (double requested = 1; requested <= 4; requested++) {
    qa_console_metrics_options big = {1920, 1080, 1, requested, &font};
    CHECK(qa_console_metrics_for(&big).scale == requested);
    qa_console_metrics_options small = {64, 32, 1, requested, &font};
    CHECK(qa_console_metrics_for(&small).scale == 1);
  }
  qa_console_metrics_options nan_case = {640, 480, 1, NAN, &font};
  CHECK(qa_console_metrics_for(&nan_case).scale == 2);
  for (double width = 640; width <= 960; width += 320) {
    for (double requested = 1; requested <= 4; requested++) {
      qa_console_metrics_options options = {width, width * 3 / 4, 1, requested, &font};
      CHECK(qa_console_metrics_for(&options).line_height == 8 * requested);
    }
  }
  /* Proportional glyphs fit the fixed cell (metrics.test.ts). */
  qa_text_atlas proportional = {0};
  proportional.kind = QA_TEXT_ATLAS_KFONT;
  proportional.line_height = 8;
  proportional.glyphs = (qa_text_glyph_entry *)calloc(1, sizeof(qa_text_glyph_entry));
  proportional.glyph_capacity = 1;
  proportional.glyph_count = 1;
  proportional.glyphs[0].codepoint = 65;
  proportional.glyphs[0].glyph =
      (qa_atlas_glyph){0, 0, 12, 8, 12, false};
  qa_text_font_selection atlas_font = {0};
  atlas_font.is_atlas = true;
  atlas_font.font = &proportional;
  atlas_font.classic = &charset;
  CHECK(qa_console_cell_width(&atlas_font, 16) == 16);
  qa_console_glyph_metrics glyph = qa_console_glyph_metrics_for(&atlas_font, "A", 16, 16);
  CHECK(fabs(glyph.line_height - 32.0 / 3) < 1e-9);
  /* Q2 rerelease qconfont cap ink (metrics.test.ts). */
  qa_text_atlas qconfont = {0};
  qconfont.kind = QA_TEXT_ATLAS_KFONT;
  qconfont.line_height = 14;
  qconfont.has_cap_ink = true;
  qconfont.cap_top = 4 * 8 / 14.0;
  qconfont.cap_height = 6 * 8 / 14.0;
  qconfont.glyphs = (qa_text_glyph_entry *)calloc(4, sizeof(qa_text_glyph_entry));
  qconfont.glyph_capacity = 4;
  qconfont.glyph_count = 4;
  qconfont.glyphs[0] = (qa_text_glyph_entry){77, {0, 0, 8, 14, 8, false}};
  qconfont.glyphs[1] = (qa_text_glyph_entry){233, {26, 128, 8, 14, 8, false}};
  qconfont.glyphs[2] = (qa_text_glyph_entry){508, {0, 0, 14, 14, 14, false}};
  qconfont.glyphs[3] = (qa_text_glyph_entry){9733, {0, 0, 14, 14, 14, false}};
  qa_text_font_selection qcon = {0};
  qcon.is_atlas = true;
  qcon.font = &qconfont;
  qcon.classic = &charset;
  CHECK(qa_console_cell_width(&qcon, 16) == 16);
  qa_console_glyph_metrics ascii = qa_console_glyph_metrics_for(&qcon, "M", 16, 16);
  CHECK(fabs(ascii.line_height - 28) < 1e-9);
  CHECK(fabs(ascii.top - 8) < 1e-9);
  CHECK(fabs(6 * ascii.line_height / qconfont.line_height - 12) < 1e-9);
  qa_console_glyph_metrics e_acute =
      qa_console_glyph_metrics_for(&qcon, "\xc3\xa9", 16, 16); /* é */
  CHECK(fabs(e_acute.line_height - ascii.line_height) < 1e-9 &&
        fabs(e_acute.top - ascii.top) < 1e-9);
  qa_console_glyph_metrics star =
      qa_console_glyph_metrics_for(&qcon, "\xe2\x98\x85", 16, 16); /* ★ U+2605 */
  CHECK(fabs(star.line_height - 16) < 1e-9);
  CHECK(fabs(star.top) < 1e-9);
  free(proportional.glyphs);
  free(qconfont.glyphs);
  qa_text_atlas_destroy(&charset);
  qa_identity_owner_destroy(&owner);
}

/* --- session (console-input + session-completion) --- */

typedef struct seat_host {
  collected printed; /* command buffer output */
  collected chats;
  char last_focus[32];
  int focus_calls;
} seat_host;

static void print_source(void *ctx, const char *text, const qa_command_context *source) {
  (void)source;
  collect_text((collected *)ctx, text);
}
static double host_now(void *ctx) {
  (void)ctx;
  return 100;
}
static bool host_connected(void *ctx) {
  (void)ctx;
  return true;
}
static void host_focus(void *ctx, qa_seat_input_focus focus) {
  seat_host *host = (seat_host *)ctx;
  host->focus_calls++;
  (void)snprintf(host->last_focus, sizeof host->last_focus, "%s",
                 focus.kind != NULL ? focus.kind : "");
}
static void host_chat(void *ctx, const char *text, bool team, bool has_target, double target) {
  (void)team;
  (void)has_target;
  (void)target;
  collect_text(&((seat_host *)ctx)->chats, text);
  collect_text(&((seat_host *)ctx)->chats, "\n");
}

typedef struct seat_fixture {
  qa_identity_owner identity;
  qa_seat_id seat;
  qa_command_context context;
  qa_cvar_registry *cvars;
  qa_command_buffer *commands;
  seat_host host;
  qa_seat_console console;
  qa_seat_console_options options;
} seat_fixture;

static bool seat_fixture_init(seat_fixture *f, qa_command_dialect dialect, bool staged) {
  memset(f, 0, sizeof *f);
  if (!qa_identity_owner_create(&f->identity, "console-b")) return false;
  f->seat = qa_identity_owner_seat(&f->identity, 0);
  f->context.session = f->identity.session;
  f->context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  f->context.origin.seat = f->seat;
  f->context.origin.client = qa_identity_owner_client(&f->identity, 0, 0);
  qa_cvar_registry_options reg_options;
  memset(&reg_options, 0, sizeof reg_options);
  reg_options.dialect = dialect;
  reg_options.context = f->context;
  f->cvars = qa_cvar_registry_create(&reg_options);
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = f->context;
  options.cvars = f->cvars;
  options.print = print_source;
  options.print_ctx = &f->host.printed;
  f->commands = qa_command_buffer_create(&options);
  if (f->cvars == NULL || f->commands == NULL) return false;
  f->options.staged = staged;
  f->options.seat = f->seat;
  f->options.dialect = dialect;
  f->options.context = f->context;
  f->options.commands = f->commands;
  f->options.cvars = f->cvars;
  f->options.now = host_now;
  f->options.connected = host_connected;
  f->options.clipboard = null_clipboard;
  f->options.focus = host_focus;
  f->options.focus_ctx = &f->host;
  f->options.chat = host_chat;
  f->options.chat_ctx = &f->host;
  return qa_seat_console_init(&f->console, &f->options);
}

static void seat_fixture_destroy(seat_fixture *f) {
  qa_seat_console_destroy(&f->console);
  qa_command_buffer_destroy(f->commands);
  qa_cvar_registry_destroy(f->cvars);
  collected_reset(&f->host.printed);
  collected_reset(&f->host.chats);
  qa_identity_owner_destroy(&f->identity);
}

static void send_key(seat_fixture *f, double code, bool down) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.seat = f->seat;
  event.time_milliseconds = 100;
  event.kind = "key";
  event.code = code;
  event.down = down;
  qa_seat_input_focus focus;
  memset(&focus, 0, sizeof focus);
  focus.kind = "console";
  (void)qa_seat_console_input(&f->console, &event, focus);
}
static void press_key(seat_fixture *f, double code) {
  send_key(f, code, true);
  send_key(f, code, false);
}
static void send_text(seat_fixture *f, const char *text) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.seat = f->seat;
  event.time_milliseconds = 100;
  event.kind = "text";
  event.text = text;
  qa_seat_input_focus focus;
  memset(&focus, 0, sizeof focus);
  focus.kind = "console";
  (void)qa_seat_console_input(&f->console, &event, focus);
}

static void test_seat_console_input(void) {
  seat_fixture f;
  CHECK(seat_fixture_init(&f, QA_COMMAND_DIALECT_Q3, false));
  /* Toggle open suppresses the paired backquote text event. */
  press_key(&f, 96);
  send_text(&f, "`");
  CHECK(strcmp(f.host.last_focus, "console") == 0);
  check_field_text(&f.console.field, "");
  send_text(&f, "echo plain");
  press_key(&f, KEY_ENTER);
  (void)qa_command_buffer_execute(f.commands);
  CHECK(collected_contains(&f.host.printed, "plain"));
  CHECK(f.host.chats.data == NULL);
  /* Unknown bare text chats while connected on q3. */
  send_text(&f, "unrecognized_console_command");
  press_key(&f, KEY_ENTER);
  (void)qa_command_buffer_execute(f.commands);
  CHECK(collected_contains(&f.host.chats, "unrecognized_console_command\n"));
  /* Explicit unknown commands never chat. */
  collected_reset(&f.host.chats);
  send_text(&f, "/explicit_unknown_console_command");
  press_key(&f, KEY_ENTER);
  (void)qa_command_buffer_execute(f.commands);
  CHECK(f.host.chats.data == NULL);
  /* History drafts round-trip. */
  send_text(&f, "echo history_one");
  press_key(&f, KEY_ENTER);
  send_text(&f, "echo history_two");
  press_key(&f, KEY_ENTER);
  send_text(&f, "unfinished draft");
  press_key(&f, KEY_UP);
  check_field_text(&f.console.field, "echo history_two");
  press_key(&f, KEY_UP);
  check_field_text(&f.console.field, "echo history_one");
  press_key(&f, KEY_DOWN);
  check_field_text(&f.console.field, "echo history_two");
  press_key(&f, KEY_DOWN);
  check_field_text(&f.console.field, "unfinished draft");
  /* Escape closes to game focus; animate moves toward the target. */
  press_key(&f, KEY_ESCAPE);
  CHECK(strcmp(f.host.last_focus, "game") == 0);
  CHECK(fabs(qa_seat_console_animate(&f.console, true, 100, 3, 0.5) - 0.3) < 1e-9);
  CHECK(fabs(qa_seat_console_animate(&f.console, false, 1000, 3, 0.5)) < 1e-9);
  seat_fixture_destroy(&f);
}

static void noop_handler(void *ctx, qa_command_invocation *invocation) {
  (void)ctx;
  (void)invocation;
}

static void test_seat_console_completion(void) {
  seat_fixture f;
  CHECK(seat_fixture_init(&f, QA_COMMAND_DIALECT_Q3, false));
  static const char *const examples[] = {"zz_first 1"};
  qa_command_documentation doc = {"First command", "zz_first <value>", examples, 1,
                                  NULL, 0};
  CHECK(qa_command_buffer_register(f.commands, "zz_first", noop_handler, NULL, &doc,
                                   QA_COMMAND_WORLD));
  CHECK(qa_command_buffer_register(f.commands, "zz_second", noop_handler, NULL, NULL,
                                   QA_COMMAND_WORLD));
  qa_console_field_set_text(&f.console.field, "zz_");
  press_key(&f, KEY_TAB);
  check_field_text(&f.console.field, "/zz_first");
  qa_console_discovery_entry *entry = qa_seat_console_selected_completion_entry(&f.console);
  CHECK(entry != NULL && entry->usage != NULL &&
        strcmp(entry->usage, "zz_first <value>") == 0);
  qa_console_discovery_entries_free(entry, 1);
  /* Shift+Tab reverses the cycle. */
  send_key(&f, KEY_SHIFT, true);
  press_key(&f, KEY_TAB);
  send_key(&f, KEY_SHIFT, false);
  check_field_text(&f.console.field, "/zz_second");
  /* History navigation drops the selected entry. */
  qa_console_history_add(&f.console.history, "echo historical");
  press_key(&f, KEY_UP);
  check_field_text(&f.console.field, "echo historical");
  CHECK(qa_seat_console_selected_completion_entry(&f.console) == NULL);
  press_key(&f, KEY_DOWN);
  check_field_text(&f.console.field, "/zz_second");
  CHECK(qa_seat_console_selected_completion_entry(&f.console) == NULL);
  seat_fixture_destroy(&f);
}

static void test_seat_console_adopt(void) {
  seat_fixture f;
  CHECK(seat_fixture_init(&f, QA_COMMAND_DIALECT_Q3, false));
  qa_console_history_add(&f.console.history, "echo menu");
  qa_console_field_set_text(&f.console.field, "unfinished draft");
  qa_seat_console candidate;
  seat_host candidate_host;
  memset(&candidate_host, 0, sizeof candidate_host);
  qa_seat_console_options candidate_options = f.options;
  candidate_options.staged = true;
  candidate_options.focus_ctx = &candidate_host;
  candidate_options.chat_ctx = &candidate_host;
  CHECK(qa_seat_console_init(&candidate, &candidate_options));
  qa_seat_console_print(&candidate, "new init\n");
  qa_seat_input_focus focus;
  memset(&focus, 0, sizeof focus);
  focus.kind = "console";
  qa_seat_console_adopt(&f.console, &candidate, focus);
  check_field_text(&f.console.field, "unfinished draft");
  CHECK(qa_console_history_line_count(&f.console.history) == 1);
  char *dump = qa_console_buffer_dump(&f.console.buffer);
  CHECK(dump != NULL && strstr(dump, "new init") != NULL);
  free(dump);
  /* The adopted (source) focus callback is now retained. */
  qa_seat_console_close(&f.console);
  CHECK(strcmp(candidate_host.last_focus, "game") == 0);
  CHECK_ABORTS(qa_seat_console_adopt(&f.console, &candidate, focus));
  qa_seat_console_destroy(&candidate);
  seat_fixture_destroy(&f);
}

/* --- llm + llm-batch (llm.test.ts) --- */

typedef struct llm_fake {
  const char *answer; /* next answer text; NULL fails the request */
  const char *error;  /* failure message when answer is NULL */
  bool stream;        /* deliver the answer through on_text */
  int requests;
  collected primed;
  char *last_prompt;
  char *last_instructions;
  char *last_session;
} llm_fake;

static char *fake_request(void *ctx, const qa_llm_request_input *input, char **out_error) {
  llm_fake *fake = (llm_fake *)ctx;
  fake->requests++;
  free(fake->last_prompt);
  free(fake->last_instructions);
  free(fake->last_session);
  fake->last_prompt = dup_text(input->prompt);
  fake->last_instructions = dup_text(input->instructions);
  fake->last_session = dup_text(input->session_instructions);
  if (fake->answer == NULL) {
    *out_error = fake->error == NULL ? NULL : dup_text(fake->error);
    return NULL;
  }
  if (fake->stream && input->on_text != NULL) input->on_text(input->on_text_ctx, fake->answer);
  return dup_text(fake->answer);
}
static void fake_prime(void *ctx, const char *instructions) {
  collect_text(&((llm_fake *)ctx)->primed, instructions);
}

typedef struct llm_fixture {
  qa_identity_owner identity;
  qa_command_context source;
  qa_command_context second;
  qa_cvar_registry *cvars;
  qa_command_buffer *commands;
  collected output; /* llm print callback text */
  int effects;
  int64_t effect_seats[8];
  llm_fake fake;
  qa_llm_commands_registration *registration;
} llm_fixture;

static void game_action_handler(void *ctx, qa_command_invocation *invocation) {
  llm_fixture *f = (llm_fixture *)ctx;
  if (f->effects < 8) f->effect_seats[f->effects] = invocation->source.origin.seat.index;
  f->effects++;
}

static qa_command_context seat_context(llm_fixture *f, int64_t seat_index) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = f->identity.session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  context.origin.seat = qa_identity_owner_seat(&f->identity, seat_index);
  context.origin.client = qa_identity_owner_client(&f->identity, seat_index, 0);
  return context;
}

static bool llm_fixture_init(llm_fixture *f, qa_command_dialect dialect) {
  memset(f, 0, sizeof *f);
  if (!qa_identity_owner_create(&f->identity, "llm-test")) return false;
  f->source = seat_context(f, 0);
  f->second = seat_context(f, 1);
  qa_cvar_registry_options reg_options;
  memset(&reg_options, 0, sizeof reg_options);
  reg_options.dialect = dialect;
  reg_options.context = f->source;
  f->cvars = qa_cvar_registry_create(&reg_options);
  if (f->cvars == NULL) return false;
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(f->cvars, "sensitivity", "3", 0, &snapshot))
    qa_cvar_read_free(&snapshot);
  if (qa_cvar_registry_register(f->cvars, "password", "SECRET-MUST-STAY-LOCAL", 0, &snapshot))
    qa_cvar_read_free(&snapshot);
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = f->source;
  options.cvars = f->cvars;
  f->commands = qa_command_buffer_create(&options);
  if (f->commands == NULL) return false;
  static const char *const examples[] = {"game_action"};
  qa_command_documentation doc = {"Perform the game action.", "game_action", examples, 1,
                                  NULL, 0};
  (void)qa_command_buffer_register(f->commands, "game_action", game_action_handler, f,
                                   &doc, QA_COMMAND_WORLD);
  qa_llm_command_requester requester;
  memset(&requester, 0, sizeof requester);
  requester.ctx = &f->fake;
  requester.request = fake_request;
  requester.prime = fake_prime;
  f->registration =
      qa_register_llm_commands(f->commands, print_source, &f->output, &requester);
  return f->registration != NULL;
}

static void llm_fixture_destroy(llm_fixture *f) {
  qa_llm_commands_registration_remove(f->registration);
  qa_command_buffer_destroy(f->commands);
  qa_cvar_registry_destroy(f->cvars);
  collected_reset(&f->output);
  collected_reset(&f->fake.primed);
  free(f->fake.last_prompt);
  free(f->fake.last_instructions);
  free(f->fake.last_session);
  qa_identity_owner_destroy(&f->identity);
}

static void send_command(llm_fixture *f, const char *text, const qa_command_context *source) {
  collected line = {0};
  collect_text(&line, text);
  collect_text(&line, "\n");
  qa_command_buffer_append(f->commands, line.data, source, -1);
  (void)qa_command_buffer_execute(f->commands);
  collected_reset(&line);
}

static void test_llm_commands(void) {
  llm_fixture f;
  CHECK(llm_fixture_init(&f, QA_COMMAND_DIALECT_Q3));
  /* PLATFORM-009: registering sends nothing; the first request carries the
   * live catalog. */
  CHECK(f.fake.primed.data == NULL || f.fake.primed.length == 0);
  /* Ask: answer printed in the invoking console. */
  f.fake.answer = "first answer";
  send_command(&f, "llm_ask \"first question\"", &f.source);
  CHECK(f.fake.last_session != NULL &&
        strstr(f.fake.last_session, "This session stays open for the whole program run") != NULL);
  CHECK(f.fake.last_session != NULL && strstr(f.fake.last_session, "command:game_action") != NULL);
  CHECK(f.fake.last_session != NULL && strstr(f.fake.last_session, "cvar:password") != NULL);
  CHECK(f.fake.last_session != NULL && strstr(f.fake.last_session, "SECRET-MUST-STAY-LOCAL") == NULL);
  CHECK(f.fake.requests == 1);
  CHECK(f.fake.last_prompt != NULL && strcmp(f.fake.last_prompt, "first question") == 0);
  CHECK(f.fake.last_session != NULL &&
        strstr(f.fake.last_session, "Examples: game_action") != NULL);
  CHECK(f.output.data != NULL && strcmp(f.output.data,
               "LLM request started. Use llm_cancel to cancel.\nfirst answer\n") == 0);
  /* Streamed ask answers get one trailing newline. */
  collected_reset(&f.output);
  f.fake.stream = true;
  f.fake.answer = "streamed";
  send_command(&f, "llm_ask \"again\"", &f.source);
  CHECK(f.output.data != NULL && strcmp(f.output.data,
               "LLM request started. Use llm_cancel to cancel.\nstreamed\n") == 0);
  f.fake.stream = false;
  /* Exec validates the whole batch before any action runs. */
  collected_reset(&f.output);
  f.fake.answer = "game_action; imaginary_command";
  send_command(&f, "llm_exec \"do a thing\"", &f.source);
  CHECK(f.effects == 0);
  CHECK(collected_contains(&f.output,
                            "LLM request failed: Unknown command or setting "
                            "\"imaginary_command\"."));
  /* Exec executes cvars and commands in the original context. */
  collected_reset(&f.output);
  f.fake.answer = "sensitivity 5; game_action";
  send_command(&f, "llm_exec \"make changes\"", &f.second);
  char *value = qa_cvar_registry_variable_string(f.cvars, "sensitivity");
  CHECK(value != NULL && strcmp(value, "5") == 0);
  free(value);
  CHECK(f.effects == 1 && f.effect_seats[0] == 1);
  CHECK(collected_contains(&f.output, "sensitivity 5; game_action\n"));
  /* Indirect commands are rejected wholesale. */
  collected_reset(&f.output);
  f.fake.answer = "game_action; wait";
  send_command(&f, "llm_exec \"do it\"", &f.source);
  CHECK(f.effects == 1);
  CHECK(collected_contains(&f.output, "wait cannot be generated by llm_exec."));
  /* Script origins cannot start requests. */
  qa_command_origin seat_origin = f.source.origin;
  qa_command_context scripted = f.source;
  scripted.origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  scripted.origin.name = "injected.cfg";
  scripted.origin.caller = &seat_origin;
  int before = f.fake.requests;
  collected_reset(&f.output);
  send_command(&f, "llm_exec \"game_action\"", &scripted);
  CHECK(f.fake.requests == before);
  CHECK(collected_contains(&f.output, "require direct input"));
  /* Usage and cancel with nothing pending. */
  collected_reset(&f.output);
  send_command(&f, "llm_ask", &f.source);
  CHECK(collected_contains(&f.output, "Usage: llm_ask <question>\n"));
  collected_reset(&f.output);
  send_command(&f, "llm_cancel", &f.source);
  CHECK(collected_contains(&f.output, "No LLM request is running in this console.\n"));
  collected_reset(&f.output);
  send_command(&f, "llm_cancel now", &f.source);
  CHECK(collected_contains(&f.output, "Usage: llm_cancel\n"));
  /* Request failures print the message; non-Error failures are unknown. */
  collected_reset(&f.output);
  f.fake.answer = NULL;
  f.fake.error = "provider down";
  send_command(&f, "llm_ask \"q\"", &f.source);
  CHECK(collected_contains(&f.output, "LLM request failed: provider down\n"));
  collected_reset(&f.output);
  f.fake.error = NULL;
  send_command(&f, "llm_ask \"q\"", &f.source);
  CHECK(collected_contains(&f.output, "LLM request failed: Unknown error\n"));
  llm_fixture_destroy(&f);
  /* Aliases expand before validation and execution (TS llm.test.ts uses a
   * q1-netquake fixture here; defineAlias throws on q3). */
  llm_fixture g;
  CHECK(llm_fixture_init(&g, QA_COMMAND_DIALECT_Q1_NETQUAKE));
  CHECK(qa_command_buffer_define_alias(g.commands, "safe",
                                       "game_action; sensitivity 8\n"));
  g.fake.answer = "safe";
  send_command(&g, "llm_exec \"use safe\"", &g.source);
  value = qa_cvar_registry_variable_string(g.cvars, "sensitivity");
  CHECK(value != NULL && strcmp(value, "8") == 0);
  free(value);
  CHECK(g.effects == 1);
  CHECK(collected_contains(&g.output, "game_action; sensitivity 8\n"));
  llm_fixture_destroy(&g);
}

/* llm.test.ts PLATFORM-006 (messages from bun). */
static void test_llm_batch_platform006(void) {
  llm_fixture f;
  CHECK(llm_fixture_init(&f, QA_COMMAND_DIALECT_Q3));
  static const char *const lines[] = {"quit", "connect evil.example.org", "say hello",
                                      "writeconfig config", "condump autoexec.cfg",
                                      "echo \"bind w quit\"; condump autoexec.cfg",
                                      "disconnect", "unbindall", "map q3dm1"};
  static const char *const names[] = {"quit", "connect", "say", "writeconfig", "condump",
                                      "condump", "disconnect", "unbindall", "map"};
  for (size_t i = 0; i < sizeof lines / sizeof *lines; i++) {
    char **batch = NULL;
    size_t count = 0;
    char *error = NULL;
    char want[256];
    snprintf(want, sizeof want,
             "%s cannot be generated by llm_exec. Ask for literal console commands without "
             "scripts, bindings, delays, or LLM calls.",
             names[i]);
    CHECK(!qa_validate_llm_batch(lines[i], f.commands, &f.source, &batch, &count, &error));
    CHECK(error != NULL && strcmp(error, want) == 0);
    free(error);
  }
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(f.cvars, "nextmap", "", 0, &snapshot)) qa_cvar_read_free(&snapshot);
  char **batch = NULL;
  size_t count = 0;
  char *error = NULL;
  CHECK(!qa_validate_llm_batch("set nextmap \"exec evil.cfg\"", f.commands, &f.source, &batch,
                               &count, &error));
  CHECK(error != NULL &&
        strcmp(error, "nextmap cannot be changed by llm_exec because its value runs as a command.") == 0);
  free(error);
  error = NULL;
  CHECK(qa_validate_llm_batch("game_action; sensitivity 4", f.commands, &f.source, &batch, &count,
                              &error));
  CHECK(count == 2 && strcmp(batch[0], "game_action") == 0 && strcmp(batch[1], "sensitivity 4") == 0);
  qa_llm_batch_lines_free(batch, count);
  llm_fixture_destroy(&f);
}

static void test_llm_batch_validation(void) {
  llm_fixture f;
  CHECK(llm_fixture_init(&f, QA_COMMAND_DIALECT_Q2_CLASSIC));
  char **batch = NULL;
  size_t count = 0;
  char *error = NULL;
  CHECK(!qa_validate_llm_batch("game_action; echo \"unterminated", f.commands, &f.source,
                               &batch, &count, &error));
  CHECK(error != NULL && strcmp(error, "Response contains an unterminated quote.") == 0);
  free(error);
  error = NULL;
  CHECK(!qa_validate_llm_batch("game_action // note", f.commands, &f.source, &batch,
                               &count, &error));
  CHECK(error != NULL &&
        strcmp(error, "Return commands only, without comments or Markdown.") == 0);
  free(error);
  error = NULL;
  CHECK(!qa_validate_llm_batch("game_action; $password", f.commands, &f.source, &batch,
                               &count, &error));
  CHECK(error != NULL && strstr(error, "Variable macro expansion") != NULL);
  free(error);
  error = NULL;
  CHECK(!qa_validate_llm_batch("", f.commands, &f.source, &batch, &count, &error));
  CHECK(error != NULL && strstr(error, "The model returned no commands.") != NULL);
  free(error);
  error = NULL;
  CHECK(!qa_validate_llm_batch("set missing_cvar 1", f.commands, &f.source, &batch, &count,
                               &error));
  CHECK(error != NULL && strstr(error, "requires an existing console setting.") != NULL);
  free(error);
  error = NULL;
  CHECK(qa_validate_llm_batch("sensitivity 5; game_action", f.commands, &f.source, &batch,
                              &count, &error));
  CHECK(count == 2 && strcmp(batch[0], "sensitivity 5") == 0 &&
        strcmp(batch[1], "game_action") == 0);
  qa_llm_batch_lines_free(batch, count);
  /* Exec catalog carries usage but never values or alias bodies. */
  CHECK(qa_command_buffer_define_alias(f.commands, "custom_alias",
                                       "echo SECRET-ALIAS-BODY\n"));
  error = NULL;
  char *instructions =
      qa_llm_exec_instructions(f.commands, &f.source, "perform game action", &error);
  CHECK(instructions != NULL);
  CHECK(instructions != NULL &&
        strstr(instructions, "game_action: game_action. Perform the game action.") != NULL);
  CHECK(instructions != NULL && strstr(instructions, "alias:custom_alias") != NULL);
  CHECK(instructions != NULL && strstr(instructions, "SECRET-MUST-STAY-LOCAL") == NULL);
  CHECK(instructions != NULL && strstr(instructions, "SECRET-ALIAS-BODY") == NULL);
  free(instructions);
  free(error);
  char *docs = qa_llm_relevant_console_docs(f.commands, &f.source, "zzzz",
                                            QA_LLM_CATALOG_ASK);
  CHECK(docs != NULL &&
        strcmp(docs, "No additional command documentation matched this prompt; use the "
                     "session catalog.") == 0);
  free(docs);
  /* Disposal unregisters all three commands. */
  qa_llm_commands_registration_remove(f.registration);
  f.registration = NULL;
  CHECK(!qa_command_buffer_exists(f.commands, "llm_ask"));
  CHECK(!qa_command_buffer_exists(f.commands, "llm_exec"));
  CHECK(!qa_command_buffer_exists(f.commands, "llm_cancel"));
  llm_fixture_destroy(&f);
}

int main(void) {
  test_console_field_editing();
  test_console_field_completion();
  test_console_history();
  test_edit_field();
  test_console_metrics();
  test_seat_console_input();
  test_seat_console_completion();
  test_seat_console_adopt();
  test_llm_commands();
  test_llm_batch_platform006();
  test_llm_batch_validation();
  printf("console_b: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
