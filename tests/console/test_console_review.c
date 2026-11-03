/*
 * Confirmation-gate review checks for src/console. Each expected value
 * comes from running the TypeScript under bun. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/console/dedicated.h"
#include "qa/console/field.h"
#include "qa/console/llm_batch.h"
#include "qa/core/cvars/index.h"
#include "qa/console/metrics.h"
#include "qa/console/source_field.h"
#include "qa/foundation/support.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/text/atlas.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

static void ignore_print(void *ctx, const char *text, const qa_command_context *source) {
  (void)ctx;
  (void)text;
  (void)source;
}

/* TS (append into a plain recorder): 16384 16385 "echo \"Dedicated console dropped 4 input line(s)\"\n" "x\n"
 *     0 "x\n" (a full ring and an over-long line drop input; drain reports it).
 * The C command buffer keeps its own size limit, so only the head is compared. */
static void test_dedicated_drops(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "dedicated-review"));
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.session = owner.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  options.print = ignore_print;
  qa_command_buffer *commands = qa_command_buffer_create(&options);
  CHECK(commands != NULL);
  qa_dedicated_console console;
  CHECK(qa_dedicated_console_init(&console));
  for (int i = 0; i < 16386; i++) qa_dedicated_console_feed_text(&console, "x\n");
  char *long_line = malloc(65538);
  memset(long_line, 'y', 65537);
  long_line[65537] = '\0';
  qa_dedicated_console_feed_text(&console, long_line);
  free(long_line);
  qa_dedicated_console_feed_text(&console, "z\n");
  CHECK(qa_dedicated_console_drain(&console, commands) == 16384);
  char *pending = qa_command_buffer_pending_text(commands);
  const char *expected_head = "echo \"Dedicated console dropped 4 input line(s)\"\nx\nx\n";
  CHECK(pending != NULL && strncmp(pending, expected_head, strlen(expected_head)) == 0);
  free(pending);
  CHECK(qa_dedicated_console_drain(&console, commands) == 0);
  qa_dedicated_console_destroy(&console);
  qa_command_buffer_destroy(commands);
  qa_identity_owner_destroy(&owner);
}

static const char *no_clipboard(void *ctx) {
  (void)ctx;
  return NULL;
}

static void check_text(qa_console_field *field, const char *expected, int64_t cursor) {
  char *text = qa_console_field_text(field);
  CHECK(text != NULL && strcmp(text, expected) == 0);
  if (text != NULL && strcmp(text, expected) != 0) fprintf(stderr, "  got \"%s\"\n", text);
  CHECK(field->cursor == cursor);
  if (field->cursor != cursor) fprintf(stderr, "  cursor %lld\n", (long long)field->cursor);
  free(text);
}

/* TS (ConsoleField edits code points): "héllo " 6 / "héllo" 5 / "hжllo" 2 /
 * 5 / "ab " 3 / "/école" 6. */
static void test_field_code_points(void) {
  qa_console_field field;
  CHECK(qa_console_field_init(&field, 6, 78));
  qa_console_field_insert(&field, "h\xc3\xa9llo w\xc3\xb6rld");
  check_text(&field, "h\xc3\xa9llo ", 6);
  qa_console_field_key(&field, 127, false, false, no_clipboard, NULL);
  check_text(&field, "h\xc3\xa9llo", 5);
  qa_console_field_key(&field, 143, false, false, no_clipboard, NULL);
  qa_console_field_key(&field, 135, false, false, no_clipboard, NULL);
  field.overstrike = true;
  qa_console_field_insert(&field, "\xd0\xb6");
  check_text(&field, "h\xd0\xb6llo", 2);
  qa_console_field_key(&field, 135, true, false, no_clipboard, NULL);
  CHECK(field.cursor == 5);
  qa_console_field_set_text(&field, "ab\xc2\xa0" "cd");
  qa_console_field_key(&field, 119, true, false, no_clipboard, NULL);
  check_text(&field, "ab\xc2\xa0", 3);
  qa_console_field_destroy(&field);

  qa_console_field wide;
  CHECK(qa_console_field_init(&wide, 1023, 78));
  qa_console_field_set_text(&wide, "\xc3\xa9");
  const char *names[] = {"\xc3\xa9p\xc3\xa9" "e", "\xc3\xa9" "cole", "\xc3\x89" "a"};
  qa_console_field_complete(&wide, names, 3, false);
  check_text(&wide, "/\xc3\xa9" "cole", 6);
  qa_console_field_destroy(&wide);
}

/* TS consoleGlyphMetrics top over a capInk {top 1, height 4} charset:
 * U+02C2 0, U+00B2 1, U+00E9 1, U+037E 0 (/[\p{L}\p{N}\p{M}]/u). */
static void test_glyph_metrics_letter_class(void) {
  qa_renderer_image image;
  memset(&image, 0, sizeof image);
  image.width = 128;
  image.height = 128;
  qa_text_atlas atlas;
  CHECK(qa_classic_charset(image, NULL, true, &atlas));
  atlas.has_cap_ink = true;
  atlas.cap_top = 1;
  atlas.cap_height = 4;
  qa_text_font_selection font;
  memset(&font, 0, sizeof font);
  font.classic = &atlas;
  const char *samples[] = {"\xcb\x82", "\xc2\xb2", "\xc3\xa9", "\xcd\xbe"};
  const double tops[] = {0, 1, 1, 0};
  for (int i = 0; i < 4; i++) {
    qa_console_glyph_metrics metrics = qa_console_glyph_metrics_for(&font, samples[i], 8, 8);
    CHECK(metrics.line_height == 8);
    CHECK(metrics.top == tops[i]);
    if (metrics.top != tops[i]) fprintf(stderr, "  sample %d top %g\n", i, metrics.top);
  }
  qa_text_atlas_destroy(&atlas);
}

/* ---- source field ---- */

typedef struct paste_state {
  const uint8_t *bytes;
  size_t length;
  bool overstrike;
} paste_state;
static bool paste_down(void *ctx, int64_t key) {
  (void)ctx;
  (void)key;
  return false;
}
static bool paste_get_overstrike(void *ctx) { return ((paste_state *)ctx)->overstrike; }
static void paste_set_overstrike(void *ctx, bool value) { ((paste_state *)ctx)->overstrike = value; }
static bool paste_read(void *ctx, const uint8_t **out_bytes, size_t *out_length) {
  paste_state *state = ctx;
  *out_bytes = state->bytes;
  *out_length = state->length;
  return true;
}
typedef struct paste_job {
  qa_edit_field *field;
  const qa_field_controls *controls;
} paste_job;
static void paste_run(void *raw) {
  paste_job *job = raw;
  qa_edit_field_char_event(job->field, 22, job->controls);
}

/* TS: "RangeError: Recursive field paste exceeded 32 clipboard reads", pasteDepth 0,
 * then a later paste works: "ab" 2. */
static void test_source_field_paste_depth(void) {
  qa_edit_field field;
  qa_edit_field_init(&field);
  field.width_in_chars = 10;
  static const uint8_t recursive[] = {22};
  paste_state state = {recursive, 1, false};
  qa_field_controls controls = {&state, paste_down, paste_get_overstrike, paste_set_overstrike,
                                {QA_FIELD_CLIPBOARD_AVAILABLE, &state, paste_read}};
  paste_job job = {&field, &controls};
  qa_caught_error caught;
  CHECK(!qa_try(paste_run, &job, &caught));
  CHECK(strcmp(caught.message, "Recursive field paste exceeded 32 clipboard reads") == 0);
  CHECK(field.paste_depth == 0);
  static const uint8_t ab[] = {'a', 'b'};
  state.bytes = ab;
  state.length = 2;
  CHECK(qa_try(paste_run, &job, &caught));
  char *text = qa_edit_field_text(&field);
  CHECK(text != NULL && strcmp(text, "ab") == 0);
  CHECK(field.cursor == 2);
  free(text);
  qa_edit_field_destroy(&field);
}

/* ---- llm batch ---- */

static void noop_handler(void *ctx, qa_command_invocation *invocation) {
  (void)ctx;
  (void)invocation;
}

static void check_batch_error(qa_command_buffer *commands, const qa_command_context *source,
                              const char *text, const char *expected) {
  char **lines = NULL;
  size_t count = 0;
  char *error = NULL;
  CHECK(!qa_validate_llm_batch(text, commands, source, &lines, &count, &error));
  CHECK(error != NULL && strcmp(error, expected) == 0);
  if (error != NULL && strcmp(error, expected) != 0) fprintf(stderr, "  got \"%s\"\n", error);
  qa_llm_batch_lines_free(lines, count);
  free(error);
}

/* TS: quit/say rejected as indirect; "set connect 2" rejected because the cvar
 * name is indirect; "set sensitivity 2" accepted; the names line omits
 * "bad name!". */
static void test_llm_batch_indirect(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "llm-test"));
  qa_command_context source;
  memset(&source, 0, sizeof source);
  source.session = owner.session;
  source.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  source.origin.seat = qa_identity_owner_seat(&owner, 0);
  source.origin.client = qa_identity_owner_client(&owner, 0, 0);
  qa_cvar_registry_options reg_options;
  memset(&reg_options, 0, sizeof reg_options);
  reg_options.dialect = QA_COMMAND_DIALECT_Q3;
  reg_options.context = source;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&reg_options);
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(cvars, "connect", "1", 0, &snapshot)) qa_cvar_read_free(&snapshot);
  if (qa_cvar_registry_register(cvars, "sensitivity", "3", 0, &snapshot)) qa_cvar_read_free(&snapshot);
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context = source;
  options.cvars = cvars;
  qa_command_buffer *commands = qa_command_buffer_create(&options);
  (void)qa_command_buffer_register(commands, "quit", noop_handler, NULL, NULL, QA_COMMAND_WORLD);
  (void)qa_command_buffer_register(commands, "say", noop_handler, NULL, NULL, QA_COMMAND_WORLD);
  (void)qa_command_buffer_register(commands, "bad name!", noop_handler, NULL, NULL, QA_COMMAND_WORLD);
  check_batch_error(commands, &source, "quit",
                    "quit cannot be generated by llm_exec. Ask for literal console commands without "
                    "scripts, bindings, delays, or LLM calls.");
  check_batch_error(commands, &source, "say hi",
                    "say cannot be generated by llm_exec. Ask for literal console commands without "
                    "scripts, bindings, delays, or LLM calls.");
  check_batch_error(commands, &source, "set connect 2",
                    "connect cannot be changed by llm_exec because its value runs as a command.");
  char **lines = NULL;
  size_t count = 0;
  char *error = NULL;
  CHECK(qa_validate_llm_batch("set sensitivity 2", commands, &source, &lines, &count, &error));
  CHECK(count == 1 && strcmp(lines[0], "set sensitivity 2") == 0);
  qa_llm_batch_lines_free(lines, count);
  free(error);
  char *catalog = qa_llm_console_catalog(commands, &source, "", QA_LLM_CATALOG_ASK, &error);
  CHECK(catalog != NULL);
  if (catalog != NULL) {
    const char *names = strstr(catalog, "Registered names: ");
    const char *end = names == NULL ? NULL : strchr(names, '\n');
    CHECK(names != NULL && end != NULL);
    if (names != NULL && end != NULL) {
      size_t length = (size_t)(end - names);
      char *line = malloc(length + 1);
      memcpy(line, names, length);
      line[length] = '\0';
      CHECK(strstr(line, "bad name!") == NULL);
      CHECK(strstr(line, "command:quit") != NULL && strstr(line, "cvar:connect") != NULL);
      free(line);
    }
    CHECK(strstr(catalog, "bad name!") != NULL); /* documentation still lists it */
  }
  free(catalog);
  free(error);
  qa_command_buffer_destroy(commands);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&owner);
}

int main(void) {
  test_llm_batch_indirect();
  test_source_field_paste_depth();
  test_dedicated_drops();
  test_field_code_points();
  test_glyph_metrics_letter_class();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
