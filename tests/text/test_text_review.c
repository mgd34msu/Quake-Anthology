/*
 * Confirmation-gate review checks for src/text. Each expected value comes
 * from running the TypeScript under bun. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
#include "qa/text.h"
#include "qa/text/q3_font_registry.h"

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

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *out = malloc(length);
  if (out != NULL) memcpy(out, text, length);
  return out;
}

/* ---- localization ---- */

typedef struct log_lines {
  char lines[8][256];
  int count;
} log_lines;
static void log_warn(void *ctx, const char *text) {
  log_lines *log = ctx;
  if (log->count < 8) snprintf(log->lines[log->count++], 256, "%s", text);
}
static void log_info(void *ctx, const char *text) {
  log_lines *log = ctx;
  if (log->count < 8) snprintf(log->lines[log->count++], 256, "I:%s", text);
}

static void check_localized(qa_localization_table *table, const char *base,
                            const char *const *args, size_t count, const char *expected) {
  char *text = qa_localization_table_localize(table, base, args, count, true, 1024, NULL);
  CHECK(text != NULL && strcmp(text, expected) == 0);
  if (text != NULL && strcmp(text, expected) != 0) fprintf(stderr, "  got \"%s\"\n", text);
  free(text);
}

/* TS: 4 ["loc parse error (bad): expected \"=\", rest of file ignored",
 * "I:Loaded 4 localization strings"]; "x {y} z"; "p { A q { B r {";
 * "one\ntwo"; "e\tf"; "in {place Z". */
static void test_localization_escapes(void) {
  const char *file =
      "a = \"x {{y} z\"\nb = \"p {{ {} q {{ {} r {{\"\nc = one\\ntwo\nd = \"e\\tf\"\nbad oops";
  qa_localization_table table;
  qa_localization_table_init(&table, QA_LOC_Q1_RERELEASE);
  log_lines log = {0};
  qa_lib_log lib = {log_warn, log_info, &log};
  qa_loc_reload_options options = {0};
  options.log = &lib;
  qa_byte_view bytes = {(const uint8_t *)file, strlen(file)};
  CHECK(qa_localization_table_reload(&table, bytes, &options) == 4);
  CHECK(log.count == 2);
  CHECK(strcmp(log.lines[0], "loc parse error (bad): expected \"=\", rest of file ignored") == 0);
  CHECK(strcmp(log.lines[1], "I:Loaded 4 localization strings") == 0);
  check_localized(&table, "$a", NULL, 0, "x {y} z");
  const char *ab[] = {"A", "B"};
  check_localized(&table, "$b", ab, 2, "p { A q { B r {");
  check_localized(&table, "$c", NULL, 0, "one\ntwo");
  check_localized(&table, "$d", NULL, 0, "e\tf");
  const char *z[] = {"Z"};
  check_localized(&table, "in {{place {}", z, 1, "in {place Z");
  qa_localization_table_destroy(&table);
}

/* server-localization.test.ts: escaped braces load, unquoted escapes decode,
 * and file errors reach the log supplied to loadServerLocalizationResources. */
static uint8_t *server_loc_read(void *ctx, const char *path, size_t *out_length) {
  (void)ctx;
  if (strcmp(path, "localization/loc_english.txt") != 0) return NULL;
  const char *text = "brace=\"x {{y}} z\"\nopen=\"lit {{ open\"\nmixed=\"{0} and {{1}}\"\nplain=plain\\nword\nbad \"oops\"";
  size_t length = strlen(text);
  uint8_t *bytes = malloc(length);
  if (bytes == NULL) return NULL;
  memcpy(bytes, text, length);
  *out_length = length;
  return bytes;
}
static void test_server_localization_log(void) {
  log_lines log = {0};
  qa_lib_log lib = {log_warn, NULL, &log};
  qa_localization_table table;
  qa_load_server_localization_resources_logged("english", server_loc_read, NULL,
                                              QA_LOC_Q1_RERELEASE, &lib, &table);
  char *brace = qa_localization_table_lookup(&table, "$brace", NULL, 0);
  CHECK(brace != NULL && strcmp(brace, "x {y}} z") == 0);
  free(brace);
  char *open = qa_localization_table_lookup(&table, "$open", NULL, 0);
  CHECK(open != NULL && strcmp(open, "lit { open") == 0);
  free(open);
  const char *a[] = {"A"};
  char *mixed = qa_localization_table_localize(&table, "$mixed", a, 1, true, 1024, NULL);
  CHECK(mixed != NULL && strcmp(mixed, "A and {1}}") == 0);
  free(mixed);
  char *plain = qa_localization_table_lookup(&table, "$plain", NULL, 0);
  CHECK(plain != NULL && strcmp(plain, "plain\nword") == 0);
  free(plain);
  bool expected = false;
  for (int i = 0; i < log.count; i++) expected = expected || strstr(log.lines[i], "expected") != NULL;
  CHECK(expected);
  qa_localization_table_destroy(&table);
}

/* ---- subtitles ---- */

typedef struct reasons {
  char lines[4][256];
  int count;
} reasons;
static void record_reason(void *ctx, const char *reason) {
  reasons *r = ctx;
  if (r->count < 4) snprintf(r->lines[r->count++], 256, "%s", reason);
}

static const char *SUBTITLES =
    "1\n00:00:01,000 --> 00:00:02,000\nA\n\n2\n00:00:05,000 --> 00:00:03,000\nB\n\n"
    "3\n00:99:00,000 --> 00:00:04,000\nC\n\n4\n00:00:04,000 --> 00:00:06,000\n<v  >D</v>";

typedef struct parse_job {
  const char *text;
} parse_job;
static void parse_without_skipped(void *raw) {
  parse_job *job = raw;
  qa_caption_cue *cues = NULL;
  size_t count = 0;
  qa_parse_subtitle_text(job->text, strlen(job->text), "ns", NULL, NULL, &cues, &count);
  qa_caption_cues_free(cues, count);
}

static void test_subtitle_skipped(void) {
  reasons r = {0};
  qa_caption_cue *cues = NULL;
  size_t count = 0;
  CHECK(qa_parse_subtitle_text(SUBTITLES, strlen(SUBTITLES), "ns", record_reason, &r, &cues,
                               &count));
  CHECK(r.count == 2);
  CHECK(strcmp(r.lines[0], "ns: skipped invalid subtitle cue (Invalid caption cue interval)") == 0);
  CHECK(strcmp(r.lines[1],
               "ns: skipped invalid subtitle cue (Invalid subtitle timestamp 00:99:00,000)") == 0);
  CHECK(count == 2);
  if (count == 2) {
    CHECK(strcmp(cues[0].id, "ns:0") == 0 && cues[0].start_milliseconds == 1000 &&
          cues[0].duration_milliseconds == 1000 && strcmp(cues[0].text, "A") == 0 &&
          cues[0].speaker == NULL);
    CHECK(strcmp(cues[1].id, "ns:1") == 0 && cues[1].start_milliseconds == 4000 &&
          cues[1].duration_milliseconds == 2000 && strcmp(cues[1].text, "D") == 0 &&
          cues[1].speaker != NULL && strcmp(cues[1].speaker, " ") == 0);
  }
  qa_caption_cues_free(cues, count);
  /* Without skipped the first error propagates as a catchable RangeError. */
  parse_job job = {SUBTITLES};
  qa_caught_error caught;
  CHECK(!qa_try(parse_without_skipped, &job, &caught));
  CHECK(strcmp(caught.name, "RangeError") == 0 &&
        strcmp(caught.message, "Invalid caption cue interval") == 0);
}

typedef struct media_state {
  char failures[4][256];
  int failure_count;
  char warns[4][256];
  int warn_count;
} media_state;

static uint8_t *media_read(void *ctx, const char *path, size_t *out_length) {
  (void)ctx;
  if (strcmp(path, "m.srt") != 0) return NULL;
  const char *text =
      "\xef\xbb\xbf\xef\xbb\xbf"
      "1\n00:00:01,000 --> 00:00:02,000\nA\n\n2\n00:00:05,000 --> 00:00:03,000\nB";
  *out_length = strlen(text);
  return (uint8_t *)dup_text(text);
}
static const char *media_language(void *ctx) {
  (void)ctx;
  return "english";
}
static void media_failed(void *ctx, const char *error) {
  media_state *state = ctx;
  if (state->failure_count < 4) snprintf(state->failures[state->failure_count++], 256, "%s", error);
}
static void media_warn(void *ctx, const char *text) {
  media_state *state = ctx;
  if (state->warn_count < 4) snprintf(state->warns[state->warn_count++], 256, "%s", text);
}

/* TS: 0 / ["RangeError: Invalid caption cue interval"] /
 * ["m.srt: skipped invalid subtitle cue (Invalid caption cue interval)\n"] 1 A m.srt:0 */
static void test_media_captions_errors(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "x"));
  qa_seat_id seat = qa_identity_owner_seat(&owner, 0);
  media_state state = {0};
  qa_live_language live = {&state, media_language, media_failed};
  qa_seat_media_captions captions;
  qa_seat_media_captions_init(&captions, seat, media_read, &state, NULL, false, &live);
  qa_caption_playback_state playback = {"m.cin", 1500, "playing"};
  qa_caption_preferences preferences = {true, true, true};
  qa_active_caption *active = NULL;
  size_t active_count = 0;
  CHECK(qa_seat_media_captions_active(&captions, playback, preferences, &active, &active_count));
  CHECK(active_count == 0);
  qa_active_captions_free(active, active_count);
  CHECK(state.failure_count == 1 &&
        strcmp(state.failures[0], "RangeError: Invalid caption cue interval") == 0);
  qa_seat_media_captions_destroy(&captions);

  qa_seat_media_captions warned;
  qa_seat_media_captions_init(&warned, seat, media_read, &state, NULL, true, NULL);
  qa_seat_media_captions_set_warn(&warned, media_warn, &state);
  CHECK(qa_seat_media_captions_prepare(&warned, "m.cin", "english"));
  CHECK(qa_seat_media_captions_active(&warned, playback, preferences, &active, &active_count));
  CHECK(state.warn_count == 1 &&
        strcmp(state.warns[0],
               "m.srt: skipped invalid subtitle cue (Invalid caption cue interval)\n") == 0);
  CHECK(active_count == 1);
  if (active_count == 1) {
    CHECK(strcmp(active[0].localized_text, "A") == 0 && strcmp(active[0].cue->id, "m.srt:0") == 0);
  }
  qa_active_captions_free(active, active_count);
  qa_seat_media_captions_destroy(&warned);
  qa_identity_owner_destroy(&owner);
}

/* ---- q3 font registry ---- */

static double no_length(void *ctx, const char *path) {
  (void)ctx;
  (void)path;
  return -1;
}
static bool no_file(void *ctx, const char *path, qa_retained_font_file *out) {
  (void)ctx;
  (void)path;
  (void)out;
  return false;
}
static void no_free(void *ctx, qa_retained_font_file file) {
  (void)ctx;
  (void)file;
}
static bool material_picture(void *ctx, const char *path, qa_material_picture *out) {
  (void)ctx;
  out->name = path;
  out->order = 1;
  out->compiled = NULL;
  return true;
}
static void no_sync(void *ctx) { (void)ctx; }

typedef struct restore_job {
  qa_q3_renderer_font_registry *registry;
  const qa_save_value *tree;
} restore_job;
static void restore_run(void *raw) {
  restore_job *job = raw;
  qa_q3_renderer_font_registry_restore_checkpoint(job->registry, job->tree);
}

/* TS: "SaveFormatError: client-fonts: invalid generated font atlas", then
 * captureCheckpoint() -> {"freeType":null,"generated":[],"fonts":[]}. */
static void test_font_restore_failure_settles_pending(void) {
  qa_font_file_reader reader = {NULL, no_length, no_file, no_free};
  qa_q3_renderer_font_registry registry;
  qa_q3_renderer_font_registry_init(&registry, reader, material_picture, NULL, no_sync, NULL,
                                    NULL);
  qa_save_value *tree = qa_save_value_object();
  qa_save_value *generated = qa_save_value_array();
  qa_save_value *row = qa_save_value_object();
  uint8_t rgba[4] = {0};
  qa_save_value_set(row, "name", qa_save_value_string("a"));
  qa_save_value_set(row, "rgba", qa_save_value_bytes(rgba, sizeof rgba));
  qa_save_value_push(generated, row);
  qa_save_value_set(tree, "freeType", qa_save_value_null());
  qa_save_value_set(tree, "generated", generated);
  qa_save_value_set(tree, "fonts", qa_save_value_array());
  restore_job job = {&registry, tree};
  qa_caught_error caught;
  CHECK(!qa_try(restore_run, &job, &caught));
  CHECK(strcmp(caught.name, "SaveFormatError") == 0);
  CHECK(strstr(caught.message, "invalid generated font atlas") != NULL);
  CHECK(registry.pending == 0);
  qa_save_value *captured = qa_q3_renderer_font_registry_capture_checkpoint(&registry);
  CHECK(captured != NULL);
  if (captured != NULL) {
    const qa_save_value *kind = qa_save_value_get(captured, "freeType");
    CHECK(kind != NULL && kind->kind == QA_SAVE_NULL);
    qa_save_value_free(captured);
  }
  qa_save_value_free(tree);
  qa_q3_renderer_font_registry_destroy(&registry);
}

int main(void) {
  test_font_restore_failure_settles_pending();
  test_localization_escapes();
  test_server_localization_log();
  test_subtitle_skipped();
  test_media_captions_errors();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
