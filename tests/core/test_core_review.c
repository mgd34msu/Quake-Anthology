/*
 * Confirmation-gate review checks for src/core. Each expected value comes
 * from running the TypeScript under bun. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/core/common_parse.h"
#include "qa/core/cvars/index.h"
#include "qa/core/cvars/mirror.h"
#include "qa/core/game_numeric.h"
#include "qa/core/numeric.h"
#include "qa/core/omnitimer.h"
#include "qa/foundation/support.h"

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

typedef struct mirror_job {
  qa_cvar_mirror_registry owner;
  qa_cvar_mirror_registry mirror;
} mirror_job;

static void mirror_run(void *raw) {
  mirror_job *job = raw;
  const char *names[] = {"foo"};
  qa_shared_cvar_mirror *created = qa_shared_cvar_mirror_create(&job->owner, &job->mirror, names, 1, NULL, NULL);
  if (created != NULL) qa_shared_cvar_mirror_destroy(created);
}

/* TS: new SharedCvarMirror(owner, mirror, ["foo"]) throws
 * Error "Shared engine cvar foo is not declared" (catchable). */
static void test_mirror_undeclared_is_catchable(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "mirror-review"));
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.session = owner.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_cvar_registry *first = qa_cvar_registry_create(&options);
  qa_cvar_registry *second = qa_cvar_registry_create(&options);
  CHECK(first != NULL && second != NULL);
  mirror_job job = {qa_cvar_registry_mirror_view(first), qa_cvar_registry_mirror_view(second)};
  qa_caught_error caught;
  CHECK(!qa_try(mirror_run, &job, &caught));
  CHECK(strcmp(caught.name, "Error") == 0);
  CHECK(strcmp(caught.message, "Shared engine cvar foo is not declared") == 0);
  qa_cvar_registry_destroy(first);
  qa_cvar_registry_destroy(second);
  qa_identity_owner_destroy(&owner);
}

static double fixed_clock(void *ctx) {
  (void)ctx;
  return 5;
}
static void *throwing_operation(void *ctx) {
  (void)ctx;
  qa_raise("Error", 0, 0, "boom");
}
static void measure_run(void *raw) { qa_omni_timer_measure(raw, "a", throwing_operation, NULL); }
static void disable_run(void *raw) { qa_omni_timer_set_enabled(raw, false); }

/* TS: measure("a", () => { throw }) pops in its finally, so a later
 * setEnabled(false) succeeds and report() has a: 1 call. */
static void test_omnitimer_measure_finally(void) {
  qa_omni_timer *timer = qa_omni_timer_create(fixed_clock, NULL, 8);
  qa_omni_timer_set_enabled(timer, true);
  qa_caught_error caught;
  CHECK(!qa_try(measure_run, timer, &caught));
  CHECK(strcmp(caught.message, "boom") == 0);
  CHECK(qa_try(disable_run, timer, &caught));
  size_t count = 0;
  qa_timer_report *rows = qa_omni_timer_report(timer, &count);
  CHECK(count == 1 && strcmp(rows[0].name, "a") == 0 && rows[0].calls == 1);
  free(rows);
  qa_omni_timer_destroy(timer);
}

/* TS NumberInput.byte() is 0 past the end and scanGameFloat clamps its
 * offset (bun: scanGameVector("1 2") = {1,2,0}; scanGameFloat("5", 9) =
 * {0, 1}; scanGameFloat("7") = {7, 2}). */
static void test_game_numeric_end_of_text(void) {
  qa_game_vec3 v = qa_scan_game_vector("1 2");
  CHECK(v.x == 1 && v.y == 2 && v.z == 0);
  qa_game_float_scan scan = qa_scan_game_float("5", 9);
  CHECK(scan.value == 0 && scan.next_offset == 1);
  scan = qa_scan_game_float("7", 0);
  CHECK(scan.value == 7 && scan.next_offset == 2);
}

static void restore_infinite_seed(void *raw) {
  (void)raw;
  qa_q3_random random;
  qa_random_state state = {0};
  state.kind = QA_RANDOM_STATE_Q3_LCG;
  state.seed = 1.0 / 0.0;
  state.draws = 0;
  qa_q3_random_restore(&random, state);
}

/* TS Q3Random.restore rejects a non-int32 seed before any conversion (bun:
 * "Q3 random seed must be a signed 32-bit integer"); nativeAtof hex values
 * from bun: 12, -16, 1, 15.9375. */
static void test_numeric_review(void) {
  qa_caught_error caught;
  CHECK(!qa_try(restore_infinite_seed, NULL, &caught));
  CHECK(strcmp(caught.message, "Q3 random seed must be a signed 32-bit integer") == 0);
  CHECK(qa_native_atof("0x1.8p3") == 12 && qa_native_atof("  -0x10") == -16);
  CHECK(qa_native_atof("0x1p") == 1 && qa_native_atof("0xffp-4zz") == 15.9375);
}

/* ---- core/commands/index.ts review (review-c). Expected texts: bun on the TS CommandBuffer. ---- */
typedef struct text_sink {
  char data[8192];
  size_t length;
  int prints;
} text_sink;
static void sink_print(void *ctx, const char *text, const qa_command_context *source) {
  (void)source;
  text_sink *sink = (text_sink *)ctx;
  size_t length = strlen(text);
  if (sink->length + length < sizeof sink->data) {
    memcpy(sink->data + sink->length, text, length + 1);
    sink->length += length;
  }
  sink->prints++;
}
static void sink_clear(text_sink *sink) {
  sink->data[0] = '\0';
  sink->length = 0;
  sink->prints = 0;
}
static qa_command_buffer *review_buffer(qa_command_dialect dialect, qa_command_context context,
                                        text_sink *sink, double max_buffer, double max_command,
                                        qa_cvar_registry *cvars) {
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = context;
  options.cvars = cvars;
  options.print = sink_print;
  options.print_ctx = sink;
  options.max_buffer_length = max_buffer;
  options.max_command_length = max_command;
  return qa_command_buffer_create(&options);
}
static bool text_equals(char *owned, const char *want) {
  bool same = owned != NULL && strcmp(owned, want) == 0;
  free(owned);
  return same;
}
typedef struct batch_job {
  qa_command_buffer *buffer;
  const char *text;
  qa_command_context source;
  double count;
} batch_job;
static void batch_run(void *raw) {
  batch_job *job = (batch_job *)raw;
  job->count = qa_command_buffer_execute_batch(job->buffer, job->text, job->source, NULL, NULL);
}
typedef struct append_job {
  qa_command_buffer *buffer;
  const char *text;
  const qa_command_context *source;
} append_job;
static void append_run(void *raw) {
  append_job *job = (append_job *)raw;
  qa_command_buffer_append(job->buffer, job->text, job->source, -1);
}
static bool failing_prefix(void *ctx) {
  (void)ctx;
  qa_raise("Error", 0, 0, "boom");
}
typedef struct prefix_job {
  qa_prepared_program *prepared;
} prefix_job;
static void prefix_job_run(void *raw) {
  prefix_job *job = (prefix_job *)raw;
  (void)job->prepared->prepare_prefix(job->prepared->ctx, failing_prefix, NULL);
}
static void validate_job_run(void *raw) {
  prefix_job *job = (prefix_job *)raw;
  job->prepared->validate_publication(job->prepared->ctx);
}

static void test_command_buffer_review(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "review-c"));
  qa_command_context console;
  memset(&console, 0, sizeof console);
  console.session = identity.session;
  console.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  text_sink sink;
  sink_clear(&sink);

  /* Q2 insert past the buffer prints and drops (bun: "Cbuf_InsertText overflowed\n"; pending unchanged). */
  qa_command_buffer *b = review_buffer(QA_COMMAND_DIALECT_Q2_CLASSIC, console, &sink, 16, 0, NULL);
  qa_command_buffer_append(b, "echo aaaaaa\n", NULL, -1);
  qa_command_buffer_insert(b, "echo bbbb", NULL, -1);
  CHECK(text_equals(qa_command_buffer_pending_text(b), "echo aaaaaa\n"));
  CHECK(strcmp(sink.data, "Cbuf_InsertText overflowed\n") == 0);
  qa_command_buffer_destroy(b);

  /* insertFromDefer past the buffer prints and keeps both buffers. */
  sink_clear(&sink);
  b = review_buffer(QA_COMMAND_DIALECT_Q2_CLASSIC, console, &sink, 16, 0, NULL);
  qa_command_buffer_append(b, "echo aaaaaa\n", NULL, -1);
  qa_command_buffer_copy_to_defer(b);
  qa_command_buffer_append(b, "echo bbbbbb\n", NULL, -1);
  qa_command_buffer_insert_from_defer(b);
  CHECK(text_equals(qa_command_buffer_pending_text(b), "echo bbbbbb\n"));
  CHECK(text_equals(qa_command_buffer_deferred_text(b), "echo aaaaaa\n"));
  CHECK(strcmp(sink.data, "Cbuf_InsertText overflowed\n") == 0);
  qa_command_buffer_destroy(b);

  /* A line past maxCommandLength is discarded with a message; the next line runs. */
  sink_clear(&sink);
  b = review_buffer(QA_COMMAND_DIALECT_Q1_NETQUAKE, console, &sink, 0, 8, NULL);
  qa_command_buffer_append(b, "echo abcdefghij;echo hi\n", NULL, -1);
  CHECK(qa_command_buffer_execute(b) == 1);
  CHECK(strcmp(sink.data, "Command line overflows the line buffer; line discarded\nhi \n") == 0);
  qa_command_buffer_destroy(b);

  /* A self-expanding alias stops at the 65536 dispatch budget and discards the rest. */
  sink_clear(&sink);
  b = review_buffer(QA_COMMAND_DIALECT_Q1_NETQUAKE, console, &sink, 0, 0, NULL);
  qa_command_buffer_append(b, "alias loop loop\nloop\necho after\n", NULL, -1);
  CHECK(qa_command_buffer_execute(b) == 65536);
  CHECK(text_equals(qa_command_buffer_pending_text(b), ""));
  CHECK(strcmp(sink.data, "Command buffer exceeded its dispatch budget; pending commands discarded\n") == 0);
  qa_command_buffer_destroy(b);

  /* An oversized token ends only its own line. */
  sink_clear(&sink);
  b = review_buffer(QA_COMMAND_DIALECT_Q1_NETQUAKE, console, &sink, 8192, 4096, NULL);
  char line[1200] = "echo ";
  memset(line + 5, 'x', 1100);
  strcpy(line + 1105, "\necho after\n");
  qa_command_buffer_append(b, line, NULL, -1);
  CHECK(qa_command_buffer_execute(b) == 1);
  CHECK(strcmp(sink.data, "Command token overflows source com_token; command ignored\nafter \n") == 0);
  size_t argc = 0;
  char *const *argv = qa_command_buffer_tokenized_arguments(b, &argc);
  CHECK(argc == 2 && strcmp(argv[0], "echo") == 0 && strcmp(argv[1], "after") == 0);
  qa_command_buffer_destroy(b);

  /* A paused batch raises and still restores the program, so the next batch runs. */
  sink_clear(&sink);
  b = review_buffer(QA_COMMAND_DIALECT_Q1_NETQUAKE, console, &sink, 0, 0, NULL);
  batch_job batch = {b, "echo one;wait;echo two", console, 0};
  qa_caught_error caught;
  CHECK(!qa_try(batch_run, &batch, &caught));
  CHECK(strcmp(caught.message,
               "Command batch paused or deferred execution; remaining batch discarded. Use immediate commands.") == 0);
  batch.text = "echo three";
  CHECK(qa_try(batch_run, &batch, &caught) && batch.count == 1);
  CHECK(strcmp(sink.data, "one \nthree \n") == 0);
  qa_command_buffer_destroy(b);

  /* Retired producers are shared with a prepared program (TS copyProgramState shares the set). */
  sink_clear(&sink);
  b = review_buffer(QA_COMMAND_DIALECT_Q1_NETQUAKE, console, &sink, 0, 0, NULL);
  qa_command_buffer_options prepared_options;
  memset(&prepared_options, 0, sizeof prepared_options);
  prepared_options.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  prepared_options.context = console;
  prepared_options.print = sink_print;
  prepared_options.print_ctx = &sink;
  qa_prepared_program *prepared = qa_command_buffer_prepare_program(b, &prepared_options);
  static const int producer_instance = 0;
  qa_command_producer producer;
  memset(&producer, 0, sizeof producer);
  producer.kind = "client-module";
  producer.instance = &producer_instance;
  qa_command_context produced = console;
  produced.producer = &producer;
  qa_command_buffer_discard_producer(b, &producer_instance);
  append_job append = {prepared->commands, "echo x\n", &produced};
  CHECK(!qa_try(append_run, &append, &caught));
  CHECK(strcmp(caught.message, "Command producer is closed") == 0);
  prepared->destroy(prepared->ctx);
  qa_command_buffer_destroy(b);

  /* A failing preparation prefix leaves the prepared program "failed". */
  b = review_buffer(QA_COMMAND_DIALECT_Q1_NETQUAKE, console, &sink, 0, 0, NULL);
  prepared = qa_command_buffer_prepare_program(b, &prepared_options);
  prefix_job prefix = {prepared};
  CHECK(!qa_try(prefix_job_run, &prefix, &caught));
  CHECK(strcmp(caught.message, "boom") == 0);
  CHECK(!qa_try(validate_job_run, &prefix, &caught));
  CHECK(strcmp(caught.message, "Prepared command program is failed") == 0);
  CHECK(!qa_command_buffer_has_preparation_prefix(prepared->commands));
  prepared->destroy(prepared->ctx);
  qa_command_buffer_destroy(b);
  qa_identity_owner_destroy(&identity);
}

/* ---- core/common-parse.ts review (review-c): a caught TextParseError releases the partial entity list
   (LeakSanitizer run); texts from bun parseEntities. ---- */
typedef struct entities_job {
  const char *text;
  qa_entities entities;
} entities_job;
static void entities_job_run(void *raw) {
  entities_job *job = (entities_job *)raw;
  (void)qa_parse_entities(job->text, strlen(job->text), NULL, &job->entities);
}
static void test_parse_entities_errors(void) {
  static const char *const texts[] = {"{ \"a\" \"b\" } x", "{ \"a\" \"b\"\n \"c\" }", "{ \"a\" \"b\"\n \"c\"\n \"d\" }",
                                      "{ \"a\" \"b\" "};
  static const char *const messages[] = {"<entities>:1:13: expected \"{\"", "<entities>:2:6: missing value for entity key \"c\"",
                                         "<entities>:3:2: missing value for entity key \"c\"",
                                         "<entities>:1:11: expected key or \"}\""};
  for (size_t i = 0; i < 4; i++) {
    entities_job job;
    memset(&job, 0, sizeof job);
    job.text = texts[i];
    qa_caught_error caught;
    CHECK(!qa_try(entities_job_run, &job, &caught));
    CHECK(strcmp(caught.name, "TextParseError") == 0 && strcmp(caught.message, messages[i]) == 0);
  }
}

/* ---- core/cvars/index.ts review (review-c): prepareCandidate() without a print uses the owner's print
   (bun: ["level: positive required\n"]). ---- */
static char *positive_level(void *ctx, const char *value) {
  (void)ctx;
  if (strtod(value, NULL) > 0) return NULL;
  char *message = malloc(32);
  strcpy(message, "positive required");
  return message;
}
static void ignore_level(void *ctx, const char *value) {
  (void)ctx;
  (void)value;
}
static void owner_print(void *ctx, const char *text) {
  text_sink *sink = (text_sink *)ctx;
  sink_print(sink, text, NULL);
}
static void test_candidate_print_default(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "review-c-candidate"));
  text_sink sink;
  sink_clear(&sink);
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.session = identity.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  options.print = owner_print;
  options.print_ctx = &sink;
  qa_cvar_registry *live = qa_cvar_registry_create(&options);
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(live, "level", "2", 0, &snapshot)) qa_cvar_read_free(&snapshot);
  qa_cvar_value_binding binding = {NULL, positive_level, ignore_level};
  (void)qa_cvar_registry_bind_value(live, "level", binding);
  qa_cvar_transfer *next = qa_cvar_registry_prepare_candidate(live, NULL, NULL, false);
  if (qa_cvar_registry_set(next->cvars, "level", "0", false, &snapshot)) qa_cvar_read_free(&snapshot);
  CHECK(strcmp(sink.data, "level: positive required\n") == 0);
  next->destroy(next->ctx);
  free(next);
  qa_cvar_registry_destroy(live);
  qa_identity_owner_destroy(&identity);
}

int main(void) {
  test_candidate_print_default();
  test_parse_entities_errors();
  test_command_buffer_review();
  test_numeric_review();
  test_game_numeric_end_of_text();
  test_omnitimer_measure_finally();
  test_mirror_undeclared_is_catchable();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
