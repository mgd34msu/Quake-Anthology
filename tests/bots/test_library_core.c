/*
 * C reproduction of the TS oracle for the bot library core: goals, weights,
 * character, weapons, libvars, structure and bsp-entities. Cases come from
 * tests/bots/behavior/library/{source-behavior,round-restart,checkpoint,retail}.test.ts,
 * tests/bots/behavior/log-save.test.ts and tests/app/q3-runtime-warnings.test.ts.
 * The expected literals of the *_oracle functions were printed by running the
 * TypeScript modules on the same inputs. Every callback checks a sentinel ctx.
 * Retail cases need QA_BOT_RETAIL_ROOT or $HOME/qfiles/Quake 3 Arena; absent data
 * skips them and says so, matching test.skipIf. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/bots/behavior.h"
#include "qa/content/archive.h"
#include "qa/core/numeric.h"
#include "qa/persistence/value.h"

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
#define CHECK_STR(a, b)                                                                   \
  do {                                                                                    \
    checks++;                                                                             \
    const char *check_a = (a), *check_b = (b);                                            \
    if (strcmp(check_a, check_b) != 0) {                                                  \
      failures++;                                                                         \
      fprintf(stderr, "FAIL %s:%d: [%s] != [%s]\n", __FILE__, __LINE__, check_a, check_b); \
    }                                                                                     \
  } while (0)
#define CHECK_ABORTS(...)                                                \
  do {                                                                   \
    checks++;                                                            \
    fflush(stderr);                                                      \
    pid_t pid = fork();                                                  \
    if (pid == 0) {                                                      \
      (void)prctl(PR_SET_DUMPABLE, 0);                                   \
      if (freopen("/dev/null", "w", stderr) == NULL) _exit(2);           \
      __VA_ARGS__;                                                       \
      _exit(0);                                                          \
    }                                                                    \
    int status = 0;                                                      \
    waitpid(pid, &status, 0);                                            \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {         \
      failures++;                                                        \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,      \
              __LINE__, #__VA_ARGS__);                                   \
    }                                                                    \
  } while (0)

static qa_vec3 v3(double x, double y, double z) { return (qa_vec3){x, y, z}; }
static bool v3_eq(qa_vec3 a, double x, double y, double z) { return a.x == x && a.y == y && a.z == z; }

/* ---- text log for callback expectations ---- */
typedef struct text_log {
  char *items[256];
  size_t count;
} text_log;
static void log_add(text_log *log, const char *text) {
  if (log->count < 256) log->items[log->count++] = strdup(text);
}
static void log_clear(text_log *log) {
  for (size_t i = 0; i < log->count; i++) free(log->items[i]);
  log->count = 0;
}
#define CHECK_LOG(log, ...)                                                 \
  do {                                                                      \
    const char *expected[] = {__VA_ARGS__};                                 \
    size_t expected_count = sizeof expected / sizeof expected[0];           \
    CHECK((log)->count == expected_count);                                  \
    for (size_t li = 0; li < expected_count && li < (log)->count; li++)     \
      CHECK_STR((log)->items[li], expected[li]);                            \
  } while (0)

/* ---- BotAssetFiles / BotScriptSources fixture ---- */
typedef struct file_entry {
  const char *path;
  const char *text;
} file_entry;
typedef struct fixture_sources {
  const void *sentinel;
  const file_entry *files;
  size_t file_count;
  qa_ui_script_global_defines *globals;
  qa_bot_script_sources sources;
  qa_bot_script_reader reader;
} fixture_sources;
static const uint8_t *fixture_read(void *ctx, const char *path, size_t *out_len) {
  fixture_sources *f = ctx;
  CHECK(f->sentinel == f);
  for (size_t i = 0; i < f->file_count; i++) {
    char full[128];
    snprintf(full, sizeof full, "botfiles/%s", f->files[i].path);
    if (strcmp(full, path) == 0) {
      *out_len = strlen(f->files[i].text);
      return (const uint8_t *)f->files[i].text;
    }
  }
  return NULL;
}
static void fixture_print(void *ctx, int severity, const char *text) {
  fixture_sources *f = ctx;
  CHECK(f->sentinel == f);
  (void)severity;
  (void)text;
}
static void fixture_common(void *ctx, const char *text) {
  fixture_sources *f = ctx;
  CHECK(f->sentinel == f);
  (void)text;
}
static void fixture_open(fixture_sources *f, const file_entry *files, size_t count) {
  memset(f, 0, sizeof *f);
  f->sentinel = f;
  f->files = files;
  f->file_count = count;
  f->globals = qa_ui_script_global_defines_create(NULL, NULL, NULL);
  qa_bot_script_sources_init(&f->sources);
  f->sources.globals = f->globals;
  f->sources.read_file = fixture_read;
  f->sources.read_ctx = f;
  f->sources.print = fixture_print;
  f->sources.print_ctx = f;
  f->sources.common_print = fixture_common;
  f->sources.common_print_ctx = f;
  f->reader = qa_bot_script_sources_reader(&f->sources);
}
static void fixture_close(fixture_sources *f) {
  qa_bot_script_sources_destroy(&f->sources);
  qa_ui_script_global_defines_destroy(f->globals);
}
#define FILES(array) array, sizeof array / sizeof array[0]

static qa_weight_inventory inv(const double *values, size_t count) {
  return (qa_weight_inventory){values, count, NULL, NULL};
}
#define INV(...) inv((const double[]){__VA_ARGS__}, sizeof((const double[]){__VA_ARGS__}) / sizeof(double))
typedef struct random_fixture {
  const void *sentinel;
  int64_t value;
  int64_t calls;
} random_fixture;
static int64_t random_next(void *ctx) {
  random_fixture *r = ctx;
  CHECK(r->sentinel == r);
  r->calls++;
  return r->value;
}
static qa_bot_random random_of(random_fixture *r, int64_t value) {
  r->sentinel = r;
  r->value = value;
  r->calls = 0;
  return (qa_bot_random){random_next, r};
}
static void store_with_memory(qa_weight_config_store_bots_a *store, fixture_sources *f, qa_bot_memory *memory) {
  qa_weight_config_store_options options;
  memset(&options, 0, sizeof options);
  options.memory = memory;
  qa_weight_config_store_bots_a_init(store);
  qa_weight_config_store_bots_a_configure(store, &f->reader, &options);
}

/* ---- source-behavior: fuzzy weights ---- */
static void test_fuzzy_weights(void) {
  static const file_entry files[] = {{"rules.c",
                                      "weight \"linear\" switch(0) { case 0: return 10; case 10: return 20; default: return 30; }\n"
                                      "weight \"uncertain\" switch(0) { case 0: return balance(4,2,6); case 10: { switch(1) { default: return 100; } } default: return 20; }\n"
                                      "weight \"range\" return balance(4,2,6);"}};
  fixture_sources f;
  fixture_open(&f, FILES(files));
  qa_weight_config_store_bots_a store;
  store_with_memory(&store, &f, &f.sources.memory);
  qa_weight_config_bots_a *config = qa_weight_config_store_load(&store, "rules.c", NULL);
  CHECK(qa_weight_config_evaluate(config, 0, INV(5, 0)) == 20);
  CHECK(qa_weight_config_evaluate(config, 0, INV(10, 0)) == 30);
  CHECK(qa_weight_config_evaluate(config, 0, INV(-1, 0)) == 10);
  random_fixture r;
  CHECK(qa_weight_config_evaluate_undecided(config, 1, INV(5, 0), random_of(&r, 1804289383)) == 100);
  CHECK(r.calls == 1);
  CHECK(qa_weight_config_evaluate_undecided(config, 2, INV(0, 0), random_of(&r, 0x17fff)) == 6);
  CHECK(qa_weight_config_evaluate_undecided(config, 2, INV(0, 0), random_of(&r, 0)) == 2);
  CHECK_ABORTS(qa_weight_config_evaluate(config, 0, inv(NULL, 0)));
  CHECK_ABORTS(qa_weight_config_evaluate(config, 9, INV(0, 0)));
  CHECK_ABORTS(qa_weight_config_evaluate(config, 0, INV(0.5, 0)));
  qa_weight_config_store_shutdown(&store);
  CHECK_ABORTS(qa_weight_config_find(config, "linear"));
  qa_weight_config_store_bots_a_destroy(&store);
  fixture_close(&f);
}

/* ---- weights oracle ---- */
static void capture_print(void *ctx, int severity, const char *text) {
  text_log *log = ctx;
  char line[1024];
  snprintf(line, sizeof line, "%d:%s", severity, text);
  log_add(log, line);
}
static void weights_error_case(const char *name, const char *text, const char *kind, const char *message,
                               const char *printed1, const char *printed2) {
  char path[32];
  snprintf(path, sizeof path, "%s.c", name);
  file_entry file = {path, text};
  fixture_sources f;
  fixture_open(&f, &file, 1);
  text_log log = {{NULL}, 0};
  qa_weight_config_store_options options;
  memset(&options, 0, sizeof options);
  options.memory = &f.sources.memory;
  options.print = capture_print;
  options.print_ctx = &log;
  qa_weight_config_store_bots_a store;
  qa_weight_config_store_bots_a_init(&store);
  qa_weight_config_store_bots_a_configure(&store, &f.reader, &options);
  qa_weight_config_error error = {QA_WEIGHT_CONFIG_ERROR_NONE, NULL};
  qa_weight_config_bots_a *config = qa_weight_config_store_load(&store, path, &error);
  if (kind != NULL) {
    CHECK(config == NULL);
    CHECK(error.kind == QA_WEIGHT_CONFIG_ERROR_PARSE);
    CHECK_STR(error.message, message);
    CHECK(log.count == 1);
    if (log.count == 1) CHECK_STR(log.items[0], printed1);
    qa_weight_config_error_destroy(&error);
  } else {
    CHECK(config != NULL);
    CHECK(qa_weight_config_weight_count(config) == 1);
    CHECK(log.count == 2);
    if (log.count == 2) {
      CHECK_STR(log.items[0], printed1);
      CHECK_STR(log.items[1], printed2);
    }
  }
  log_clear(&log);
  qa_weight_config_store_bots_a_destroy(&store);
  fixture_close(&f);
}
static void breed_report(void *ctx, const char *message) { log_add(ctx, message); }
static void test_weights_oracle(void) {
  weights_error_case("bad1", "nope", "E", "bad1.c:1:1: invalid name nope\n", "3:file bad1.c, line 1: invalid name nope\n\n", NULL);
  weights_error_case("bad2", "weight \"a\" switch(0) { case 1: return 1; }", NULL, NULL,
                     "2:file bad2.c, line 1: switch without default\n\n", "1:loaded bad2.c\n");
  weights_error_case("bad3", "weight \"a\" return -5;", NULL, NULL,
                     "2:file bad3.c, line 1: negative value set to zero\n\n", "1:loaded bad3.c\n");
  weights_error_case("bad4", "weight \"a\" switch(0) { default: return 1; default: return 2; }", "E",
                     "bad4.c:1:43: switch already has a default\n", "3:file bad4.c, line 1: switch already has a default\n\n", NULL);
  weights_error_case("bad5", "weight \"a\" return", "E", "bad5.c:1:1: couldn't read expected token",
                     "3:file bad5.c, line 1: couldn't read expected token\n", NULL);
  weights_error_case("bad6", "weight 5 return 1;", "E", "bad6.c:1:8: expected a string, found 5",
                     "3:file bad6.c, line 1: expected a string, found 5\n", NULL);
  static const file_entry files[] = {
      {"w.c", "weight \"a\" return balance(4,2,6); weight \"b\" switch(1) { case 3: return balance(10,8,12); default: { switch(0) { default: return 7; } } }"},
      {"o.c", "weight \"a\" return balance(1,0,2); weight \"b\" switch(1) { case 3: return balance(3,2,4); default: { switch(0) { default: return 7; } } }"},
      {"x.c", "weight \"a\" return 1;"}};
  fixture_sources f;
  fixture_open(&f, FILES(files));
  qa_weight_config_store_bots_a store;
  store_with_memory(&store, &f, &f.sources.memory);
  qa_weight_config_bots_a *c = qa_weight_config_store_load(&store, "w.c", NULL);
  CHECK(qa_weight_config_store_load(&store, "w.c", NULL) == c); /* cache identity */
  CHECK_STR(qa_weight_config_name(c, 0), "a");
  CHECK_STR(qa_weight_config_name(c, 1), "b");
  CHECK(qa_weight_config_max_inventory_index(c) == 1);
  CHECK(qa_weight_config_find(c, "b") == 1);
  CHECK(qa_weight_config_find(c, "q") == -1);
  qa_weight_config_scale_weight(c, "a", 0.3);
  CHECK((double)qa_weight_config_evaluate(c, 0, INV(0, 0)) == 2.4000000953674316);
  qa_weight_config_scale_balance_range(c, 2);
  random_fixture r;
  CHECK(qa_weight_config_evaluate_undecided(c, 0, INV(0, 0), random_of(&r, 0)) == 0);
  CHECK(qa_weight_config_evaluate_undecided(c, 0, INV(0, 0), random_of(&r, 0x7fff)) == 8);
  qa_weight_config_evolve(c, random_of(&r, 12345));
  CHECK((double)qa_weight_config_evaluate(c, 0, INV(0, 0)) == 1.4140081405639648);
  CHECK(qa_weight_config_evaluate_undecided(c, 0, INV(0, 0), random_of(&r, 0)) == 0);
  CHECK(qa_weight_config_evaluate_undecided(c, 0, INV(0, 0), random_of(&r, 0x7fff)) == 8);
  CHECK((double)qa_weight_config_evaluate(c, 1, INV(0, 1)) == 9.014007568359375);
  CHECK(qa_weight_config_evaluate(c, 1, INV(0, 5)) == 7);
  qa_weight_config_bots_a *other = qa_weight_config_store_load(&store, "o.c", NULL);
  qa_string_list errors = qa_weight_config_interbreed_from(c, other, other, NULL, NULL);
  CHECK(errors.count == 0);
  qa_string_list_destroy(&errors);
  CHECK(qa_weight_config_evaluate(c, 0, INV(0, 0)) == 1);
  CHECK(qa_weight_config_evaluate(c, 1, INV(0, 1)) == 3);
  qa_weight_config_bots_a *small = qa_weight_config_store_load(&store, "x.c", NULL);
  text_log log = {{NULL}, 0};
  errors = qa_weight_config_interbreed_from(c, small, small, breed_report, &log);
  CHECK(errors.count == 1);
  CHECK_LOG(&log, "cannot interbreed weight configs, unequal numweights");
  qa_string_list_destroy(&errors);
  log_clear(&log);
  qa_weight_config_error error = {QA_WEIGHT_CONFIG_ERROR_NONE, NULL};
  CHECK(qa_weight_config_store_load(&store, "missing.c", &error) == NULL);
  CHECK(error.kind == QA_WEIGHT_CONFIG_ERROR_LOAD);
  CHECK_STR(error.message, "could not load weight config missing.c");
  qa_weight_config_error_destroy(&error);
  qa_weight_config_store_bots_a_destroy(&store);
  fixture_close(&f);
}

/* ---- checkpoint.test: fuzzy checkpoint ---- */
static void test_fuzzy_checkpoint(void) {
  static const file_entry files[] = {{"rules.c", "weight \"test\" switch(0) { case 1: return balance(4,2,6); default: return 12; }"}};
  fixture_sources f;
  fixture_open(&f, FILES(files));
  qa_bot_memory *memory = &f.sources.memory;
  qa_weight_config_store_bots_a weights;
  store_with_memory(&weights, &f, memory);
  qa_weight_config_bots_a *config = qa_weight_config_store_load(&weights, "rules.c", NULL);
  qa_weight_config_scale_weight(config, "test", 0.3);
  random_fixture r;
  qa_weight_config_evolve(config, random_of(&r, 12345));
  int64_t reference = qa_weight_config_store_reference(&weights, config);
  float before = qa_weight_config_evaluate(config, 0, INV(0));
  qa_bot_memory_capture capture;
  CHECK(qa_bot_memory_checkpoint(memory, &capture));
  qa_save_value *image = qa_weight_config_store_checkpoint(&weights, &capture);
  size_t image_length = 0, memory_length = 0;
  uint8_t *encoded_image = qa_encode_checkpoint_value(image, &image_length);
  uint8_t *encoded_memory = qa_encode_checkpoint_value(capture.image, &memory_length);
  qa_bot_memory_dispose(memory);

  fixture_sources g;
  fixture_open(&g, FILES(files));
  qa_bot_memory *restored_memory = &g.sources.memory;
  qa_save_value *memory_value = qa_decode_checkpoint_value(encoded_memory, memory_length);
  qa_bot_memory_restored allocations;
  qa_bot_memory_restore(restored_memory, memory_value, &allocations);
  qa_weight_config_store_bots_a restored;
  store_with_memory(&restored, &g, restored_memory);
  qa_save_value *image_value = qa_decode_checkpoint_value(encoded_image, image_length);
  qa_weight_config_store_restore(&restored, image_value, &allocations);
  qa_weight_config_bots_a *again = qa_weight_config_store_resolve(&restored, (double)reference);
  CHECK(qa_weight_config_evaluate(again, 0, INV(0)) == before);
  CHECK(qa_weight_config_store_load(&restored, "rules.c", NULL) == again);
  qa_bot_memory_capture second;
  CHECK(qa_bot_memory_checkpoint(restored_memory, &second));
  qa_save_value *second_image = qa_weight_config_store_checkpoint(&restored, &second);
  size_t second_length = 0;
  uint8_t *second_encoded = qa_encode_checkpoint_value(second_image, &second_length);
  CHECK(second_length == image_length && memcmp(second_encoded, encoded_image, image_length) == 0);
  free(second_encoded);
  free(encoded_image);
  free(encoded_memory);
  qa_save_value_free(second_image);
  qa_save_value_free(image);
  qa_save_value_free(image_value);
  qa_save_value_free(memory_value);
  qa_bot_memory_capture_destroy(&capture);
  qa_bot_memory_capture_destroy(&second);
  qa_bot_memory_restored_destroy(&allocations);
  qa_weight_config_store_bots_a_destroy(&restored);
  qa_weight_config_store_bots_a_destroy(&weights);
  fixture_close(&g);
  fixture_close(&f);
}

/* ---- libvars ---- */
static int64_t bytes_of(void *ctx, int64_t index) {
  const char *text = ctx;
  return index < (int64_t)strlen(text) ? (uint8_t)text[index] : 0;
}
static const char *text_fn(void *ctx) { return ctx; }
static void test_libvars_oracle(void) {
  CHECK(qa_lib_var_string_value("1.5") == 1.5f);
  CHECK(qa_lib_var_string_value("12a") == 0);
  CHECK(qa_lib_var_string_value("") == 0);
  CHECK(qa_lib_var_string_value(".5") == 0.5f);
  CHECK(qa_lib_var_string_value("1.") == 0);
  CHECK(qa_lib_var_string_value("00012") == 12);
  qa_bot_lib_vars v;
  qa_bot_lib_vars_init(&v);
  CHECK(qa_bot_lib_vars_value(&v, "A", "1.25") == 1.25f);
  CHECK_STR(qa_bot_lib_vars_string(&v, "a", "x"), "1.25");
  CHECK(qa_bot_lib_vars_get_value(&v, "A") == 1.25f);
  CHECK(qa_bot_lib_vars_changed(&v, "a"));
  qa_bot_lib_vars_set(&v, "a", "2");
  CHECK_STR(qa_bot_lib_vars_get_string(&v, "A"), "2");
  CHECK(qa_bot_lib_vars_get_value(&v, "a") == 2);
  qa_bot_lib_vars_set_not_modified(&v, "A");
  CHECK(!qa_bot_lib_vars_changed(&v, "a"));
  CHECK_STR(qa_bot_lib_vars_get_string_by_name_bytes(&v, bytes_of, (void *)"a"), "2");
  CHECK_STR(qa_bot_lib_vars_get_string_by_name_bytes(&v, NULL, NULL), "");
  qa_bot_lib_vars_set_by_name_bytes(&v, bytes_of, (void *)"z", text_fn, (void *)"z", text_fn, (void *)"9");
  CHECK(qa_bot_lib_vars_get_value(&v, "Z") == 9);
  qa_bot_lib_var *z = qa_bot_lib_vars_get(&v, "z");
  CHECK_STR(qa_bot_lib_var_name(z), "z");
  CHECK(qa_bot_lib_vars_resolve(&v, qa_bot_lib_vars_reference(&v, z)) == z);
  qa_bot_lib_vars_clear(&v);
  CHECK(qa_bot_lib_vars_get(&v, "z") == NULL);
  CHECK_ABORTS(qa_bot_lib_var_value(z));
  CHECK(qa_bot_memory_get_live_allocations(&v.owned_memory) == 0);
  qa_bot_lib_vars_destroy(&v);
}
/* checkpoint.test: the variable half of the allocation-reference case. */
static void test_libvars_checkpoint(void) {
  qa_bot_memory memory;
  qa_bot_memory_init(&memory);
  qa_bot_lib_vars variables;
  qa_bot_lib_vars_init_memory(&variables, &memory);
  qa_bot_lib_vars_set(&variables, "sv_gravity", "743.25");
  qa_bot_lib_vars_set_not_modified(&variables, "sv_gravity");
  qa_bot_lib_vars_set(&variables, "other", "1");
  int64_t alias = qa_bot_lib_vars_reference(&variables, qa_bot_lib_vars_get(&variables, "sv_gravity"));
  qa_bot_memory_capture capture;
  CHECK(qa_bot_memory_checkpoint(&memory, &capture));
  qa_save_value *saved = qa_bot_lib_vars_checkpoint(&variables, &capture);
  size_t saved_length = 0, memory_length = 0;
  uint8_t *encoded = qa_encode_checkpoint_value(saved, &saved_length);
  uint8_t *encoded_memory = qa_encode_checkpoint_value(capture.image, &memory_length);
  qa_bot_memory_destroy(&memory);
  qa_bot_memory restored_memory;
  qa_bot_memory_init(&restored_memory);
  qa_save_value *memory_value = qa_decode_checkpoint_value(encoded_memory, memory_length);
  qa_bot_memory_restored allocations;
  qa_bot_memory_restore(&restored_memory, memory_value, &allocations);
  qa_bot_lib_vars restored;
  qa_bot_lib_vars_init_memory(&restored, &restored_memory);
  qa_save_value *value = qa_decode_checkpoint_value(encoded, saved_length);
  qa_bot_lib_vars_restore(&restored, value, &allocations);
  CHECK(!qa_bot_lib_vars_changed(&restored, "sv_gravity"));
  qa_bot_lib_var *gravity = qa_bot_lib_vars_resolve(&restored, alias);
  CHECK(gravity == qa_bot_lib_vars_get(&restored, "sv_gravity"));
  CHECK(qa_bot_lib_var_value(gravity) == 743.25f);
  qa_bot_lib_vars_set(&restored, "sv_gravity", "812");
  CHECK(qa_bot_lib_var_value(gravity) == 812);
  CHECK_STR(qa_bot_lib_vars_get_string(&restored, "other"), "1");
  /* A second owner cannot overlay a populated one. */
  CHECK_ABORTS(qa_bot_lib_vars_restore(&restored, value, &allocations));
  qa_save_value_free(saved);
  qa_save_value_free(value);
  qa_save_value_free(memory_value);
  free(encoded);
  free(encoded_memory);
  qa_bot_memory_capture_destroy(&capture);
  qa_bot_memory_restored_destroy(&allocations);
  qa_bot_lib_vars_destroy(&variables);
  qa_bot_lib_vars_destroy(&restored);
  qa_bot_memory_destroy(&restored_memory);
}
/* log-save.test: BotLog gates its opens on the variable set through BotLibVars. */
static qa_bot_log_io_result log_stream_ok(void *ctx, const uint8_t *bytes, size_t n) {
  int *writes = ctx;
  (void)bytes;
  (void)n;
  (*writes)++;
  return (qa_bot_log_io_result){QA_LOG_IO_OK};
}
static qa_bot_log_io_result log_stream_flush(void *ctx) {
  (void)ctx;
  return (qa_bot_log_io_result){QA_LOG_IO_OK};
}
static qa_bot_log_open_result log_open_file(void *ctx, const char *name) {
  int *writes = ctx;
  (void)name;
  qa_bot_log_open_result result;
  memset(&result, 0, sizeof result);
  result.opened = true;
  result.stream.ctx = writes;
  result.stream.write = log_stream_ok;
  result.stream.flush = log_stream_flush;
  result.stream.close = log_stream_flush;
  return result;
}
static void test_log_variable_gate(void) {
  qa_bot_lib_vars variables;
  qa_bot_lib_vars_init(&variables);
  int writes = 0;
  qa_bot_log log;
  qa_bot_log_init(&log, &variables);
  log.open_file = log_open_file;
  log.open_ctx = &writes;
  (void)qa_bot_log_open(&log, "bot.log");
  CHECK(!qa_bot_log_has_file(&log));
  qa_bot_lib_vars_set(&variables, "log", "1");
  (void)qa_bot_log_open(&log, "bot.log");
  CHECK(qa_bot_log_has_file(&log));
  qa_bot_log_write(&log, "x");
  CHECK(writes > 0);
  (void)qa_bot_log_close(&log);
  qa_bot_log_destroy(&log);
  qa_bot_lib_vars_destroy(&variables);
}

/* ---- structure ---- */
typedef struct write_fixture {
  const void *sentinel;
  char text[4096];
  int fail_after;
} write_fixture;
static int64_t write_capture(void *ctx, const char *text) {
  write_fixture *w = ctx;
  CHECK(w->sentinel == w);
  if (w->fail_after >= 0 && w->fail_after-- == 0) return -1;
  strncat(w->text, text, sizeof w->text - strlen(w->text) - 1);
  return (int64_t)strlen(text);
}
static void write_fixture_init(write_fixture *w) {
  memset(w, 0, sizeof *w);
  w->sentinel = w;
  w->fail_after = -1;
}
typedef struct reader_tokens {
  const void *sentinel;
  qa_ui_script_source_reader *source;
  qa_ui_script_token *unread;
} reader_tokens;
static void run_next(void *ctx);
typedef struct next_call {
  qa_ui_script_source_reader *source;
  qa_ui_script_token_record record;
  bool read;
} next_call;
static void run_next(void *ctx) {
  next_call *call = ctx;
  call->read = qa_ui_script_source_reader_next(call->source, &call->record);
}
static int reader_next(void *ctx, qa_ui_script_token **out) {
  reader_tokens *t = ctx;
  CHECK(t->sentinel == t);
  if (t->unread != NULL) {
    *out = t->unread;
    t->unread = NULL;
    return QA_STRUCTURE_SOURCE_TOKEN;
  }
  next_call call = {t->source, {NULL, 0, 0, 0}, false};
  bool ok = qa_ui_script_source_reader_catch(t->source, run_next, &call);
  if (!ok) return QA_STRUCTURE_SOURCE_FAILURE;
  if (!call.read) return QA_STRUCTURE_SOURCE_END;
  *out = call.record.token;
  return QA_STRUCTURE_SOURCE_TOKEN;
}
static void reader_unread(void *ctx, qa_ui_script_token *token) {
  reader_tokens *t = ctx;
  CHECK(t->sentinel == t);
  t->unread = token;
}
static bool reader_resolve(void *ctx, const qa_ui_script_include_request *request, qa_ui_script_source *out) {
  qa_bot_script_reader *reader = ctx;
  return reader->resolve(reader->ctx, request, out);
}
static const qa_structure_field_definition test_fields[] = {
    {"c", 0, QA_STRUCT_CHAR, 0, 0, 0, NULL},
    {"i", 4, QA_STRUCT_INT, 0, 0, 0, NULL},
    {"f", 8, QA_STRUCT_FLOAT | QA_STRUCT_ARRAY, 2, 0, 0, NULL},
    {"s", 16, QA_STRUCT_STRING, 0, 0, 0, NULL},
};
static const qa_structure_definition test_definition = {100, test_fields, 4};
/* Returns the readStructure result and the last diagnostic message through out. */
static bool read_structure_case(const char *text, uint8_t *bytes, char *last_message, size_t capacity) {
  file_entry file = {"s.c", text};
  fixture_sources f;
  fixture_open(&f, &file, 1);
  qa_ui_script_source source;
  CHECK(f.reader.resolve_root(f.reader.ctx, "s.c", &source));
  qa_ui_script_preprocessor_options options;
  memset(&options, 0, sizeof options);
  options.globals = f.globals;
  qa_ui_script_source_reader *tokens = qa_ui_script_source_reader_open(&source, reader_resolve, &f.reader, &options);
  reader_tokens stream = {&stream, tokens, NULL};
  stream.sentinel = &stream;
  qa_structure_reader reader;
  qa_structure_reader_init(&reader, (qa_structure_token_source){&stream, reader_next, reader_unread}, "s.c", NULL, 0, NULL, NULL);
  bool result = qa_structure_reader_read_structure(&reader, &test_definition, bytes, 100, 0);
  size_t count = 0;
  const qa_ui_script_diagnostic *diagnostics = qa_structure_reader_diagnostics(&reader, &count);
  last_message[0] = '\0';
  if (count > 0) snprintf(last_message, capacity, "%s", diagnostics[count - 1].message);
  qa_structure_reader_destroy(&reader);
  qa_ui_script_source_reader_destroy(tokens);
  fixture_close(&f);
  return result;
}
static void test_structure_oracle(void) {
  write_fixture w;
  struct { double value; const char *text; } floats[] = {{1.5, "1.5"}, {0, "0"}, {-2.25, "-2.25"}, {100, "100"}, {0.1, "0.1"}, {1e10, "10000000000"}};
  for (size_t i = 0; i < 6; i++) {
    write_fixture_init(&w);
    CHECK(qa_write_float_bots_a(write_capture, &w, floats[i].value));
    CHECK_STR(w.text, floats[i].text);
  }
  uint8_t bytes[100];
  memset(bytes, 0, sizeof bytes);
  char message[256];
  CHECK(read_structure_case("{ c -5 i 300 f {1.5, 2} s \"hello\" }", bytes, message, sizeof message));
  static const uint8_t expected[24] = {251, 0, 0, 0, 44, 1, 0, 0, 0, 0, 192, 63, 0, 0, 0, 64, 104, 101, 108, 108, 111, 0, 0, 0};
  CHECK(memcmp(bytes, expected, 24) == 0);
  write_fixture_init(&w);
  CHECK(qa_write_structure_bots_a(write_capture, &w, &test_definition, bytes, 100));
  CHECK_STR(w.text, "{\r\n\tc\t-5\r\n\ti\t300\r\n\tf\t{1.5,2}\r\n\ts\t\"hello\"\r\n}\r\n");
  write_fixture_init(&w);
  w.fail_after = 3;
  CHECK(!qa_write_structure_bots_a(write_capture, &w, &test_definition, bytes, 100));
  struct { const char *text, *message; } failures_table[] = {
      {"{ q 1 }", "unknown structure field q"},
      {"{ i 40000 }", "value 40000 out of range [-32768, 32767]"},
      {"{ i 1.5 }", "unexpected float"},
      {"{ i", "couldn't read expected token"},
      {"{ f {1 2} }", "expected a comma, found 2"},
  };
  for (size_t i = 0; i < 5; i++) {
    memset(bytes, 0, sizeof bytes);
    CHECK(!read_structure_case(failures_table[i].text, bytes, message, sizeof message));
    CHECK_STR(message, failures_table[i].message);
  }
  CHECK(qa_find_structure_field(test_fields, 4, "s") == &test_fields[3]);
  CHECK(qa_find_structure_field(test_fields, 4, "z") == NULL);
  CHECK_ABORTS(qa_write_structure_bots_a(write_capture, &w, &test_definition, bytes, 8));
}

/* ---- bsp entities ---- */
static void bsp_print(void *ctx, int severity, const char *text) { capture_print(ctx, severity, text); }
static bool key_is_n(void *ctx, const char *candidate) {
  int *calls = ctx;
  (*calls)++;
  return strcmp(candidate, "n") == 0;
}
typedef struct write_target {
  const void *sentinel;
  uint8_t bytes[8];
  int64_t cleared;
} write_target;
static void target_set(void *ctx, int64_t index, uint8_t value) {
  write_target *t = ctx;
  CHECK(t->sentinel == t);
  t->bytes[index] = value;
}
static void target_clear(void *ctx, int64_t length) {
  write_target *t = ctx;
  CHECK(t->sentinel == t);
  t->cleared = length;
  memset(t->bytes, 0, (size_t)length);
}
static void test_bsp_oracle(void) {
  text_log log = {{NULL}, 0};
  qa_aas_bsp_entities bsp;
  qa_aas_bsp_entities_init(&bsp);
  qa_aas_bsp_entities_configure(&bsp, bsp_print, &log, NULL);
  CHECK(!qa_aas_bsp_entities_loaded(&bsp));
  qa_aas_bsp_entities_load(&bsp, "{ \"classname\" \"worldspawn\" } { \"classname\" \"a\" \"origin\" \"1.5 -2 3e1\" \"n\" \"42\" \"bad\" \"1 x 3\" \"big\" \" +7xyz\" \"f\" \"0x10\" }");
  CHECK(qa_aas_bsp_entities_loaded(&bsp));
  CHECK(qa_aas_bsp_entities_next_entity(&bsp, 0) == 1);
  CHECK(qa_aas_bsp_entities_next_entity(&bsp, 1) == 2);
  CHECK(qa_aas_bsp_entities_next_entity(&bsp, 2) == 0);
  qa_aas_bsp_vector_result vector = qa_aas_bsp_entities_vector(&bsp, 2, "origin");
  CHECK(vector.found && v3_eq(vector.value, 1.5, -2, 30));
  vector = qa_aas_bsp_entities_vector(&bsp, 2, "bad");
  CHECK(vector.found && v3_eq(vector.value, 1, 0, 0));
  vector = qa_aas_bsp_entities_vector(&bsp, 2, "none");
  CHECK(!vector.found && v3_eq(vector.value, 0, 0, 0));
  qa_aas_bsp_number_result number = qa_aas_bsp_entities_float(&bsp, 2, "n");
  CHECK(number.found && number.value == 42);
  number = qa_aas_bsp_entities_int(&bsp, 2, "n");
  CHECK(number.found && number.value == 42);
  number = qa_aas_bsp_entities_int(&bsp, 2, "big");
  CHECK(number.found && number.value == 7);
  number = qa_aas_bsp_entities_float(&bsp, 2, "f");
  CHECK(number.found && number.value == 16);
  number = qa_aas_bsp_entities_int(&bsp, 9, "n");
  CHECK(!number.found && number.value == 0);
  uint8_t out[4];
  CHECK(qa_aas_bsp_entities_value(&bsp, 2, "classname", out, 4));
  CHECK(out[0] == 97 && out[1] == 0);
  CHECK(qa_aas_bsp_entities_value(&bsp, 2, "origin", out, 4));
  CHECK(memcmp(out, "1.5\0", 4) == 0);
  CHECK_LOG(&log, "1:bsp entity out of range\n");
  log_clear(&log);
  int calls = 0;
  qa_aas_bsp_key key = {key_is_n, &calls};
  number = qa_aas_bsp_entities_int_key(&bsp, 2, &key);
  CHECK(number.found && number.value == 42 && calls > 0);
  write_target target;
  memset(&target, 0xff, sizeof target);
  target.sentinel = &target;
  target.cleared = 0;
  qa_aas_bsp_writes writes = {&target, target_set, target_clear};
  uint8_t unused[4];
  CHECK(qa_aas_bsp_entities_value_ex(&bsp, 2, "n", NULL, unused, 4, 4, &writes));
  CHECK(target.cleared == 4 && memcmp(target.bytes, "42\0\0", 4) == 0);
  CHECK_ABORTS(qa_aas_bsp_entities_next_entity(&bsp, 2147483647));
  CHECK_ABORTS(qa_aas_bsp_entities_value_ex(&bsp, 2, "n", NULL, unused, 4, 9, NULL));

  qa_aas_bsp_entities_load(&bsp, "{ \"a\" }");
  CHECK_LOG(&log, "3:file entdata, line 1: expected a string, found }\n");
  CHECK(qa_aas_bsp_entities_loaded(&bsp) && qa_aas_bsp_entities_next_entity(&bsp, 0) == 0);
  log_clear(&log);
  qa_aas_bsp_entities_load(&bsp, "x");
  CHECK_LOG(&log, "3:file entdata, line 1: invalid x\n\n");
  log_clear(&log);
  qa_aas_bsp_entities_load(&bsp, "{ \"a\" \"b\"");
  CHECK_LOG(&log, "3:file entdata, line 1: missing }\n\n");
  log_clear(&log);
  qa_aas_bsp_entities_load(&bsp, "{ a \"b\" }");
  CHECK_LOG(&log, "3:file entdata, line 1: invalid a\n\n");
  log_clear(&log);

  /* Checkpoint round trip over a fresh owner. */
  qa_aas_bsp_entities_load(&bsp, "{ \"classname\" \"worldspawn\" } { \"k\" \"v\" }");
  qa_bot_memory_capture capture;
  CHECK(qa_bot_memory_checkpoint(bsp.memory, &capture));
  qa_save_value *saved = qa_aas_bsp_entities_checkpoint(&bsp, &capture);
  qa_bot_memory restored_memory;
  qa_bot_memory_init(&restored_memory);
  qa_bot_memory_restored allocations;
  qa_bot_memory_restore(&restored_memory, capture.image, &allocations);
  qa_aas_bsp_entities again;
  qa_aas_bsp_entities_init(&again);
  qa_aas_bsp_entities_configure(&again, bsp_print, &log, &restored_memory);
  qa_aas_bsp_entities_restore(&again, saved, &allocations);
  CHECK(qa_aas_bsp_entities_loaded(&again));
  CHECK(qa_aas_bsp_entities_next_entity(&again, 1) == 2);
  CHECK(qa_aas_bsp_entities_value(&again, 2, "k", out, 4) && out[0] == 'v');
  qa_save_value_free(saved);
  qa_bot_memory_capture_destroy(&capture);
  qa_bot_memory_restored_destroy(&allocations);
  qa_aas_bsp_entities_destroy(&again);
  qa_bot_memory_destroy(&restored_memory);
  qa_aas_bsp_entities_destroy(&bsp);
  log_clear(&log);
}

/* ---- character oracle ---- */
static void character_log_write(void *ctx, const char *text) { log_add(ctx, text); }
typedef struct character_report {
  const void *sentinel;
  text_log lines;
} character_report;
static void character_report_fn(void *ctx, const qa_character_diagnostic *d) {
  character_report *r = ctx;
  CHECK(r->sentinel == r);
  char line[512];
  snprintf(line, sizeof line, "%s|%s|%s|%s", qa_character_severity_name(d->severity), qa_character_code_name(d->code),
           d->source, d->message);
  log_add(&r->lines, line);
}
static const char *character_name_fn(void *ctx) { return ctx; }
static void test_character_oracle(void) {
  static const file_entry files[] = {
      {"bots/default_c.c", "skill 1 { 0 \"Def\" 2 0.1 3 \"defw\" 5 7 } skill 4 { 0 \"Def\" 2 0.5 3 \"defw\" 5 9 } skill 5 { 0 \"Def\" 2 0.9 3 \"defw\" 5 11 }"},
      {"bots/t_c.c", "skill 1 { 0 \"T\" 2 0.25 4 1.5 6 3 } skill 4 { 0 \"T4\" 2 0.75 4 2.5 6 8 }"},
      {"bots/bad_c.c", "skill 1 { 0 \"x\" 0 \"y\" }"},
      {"bots/none_c.c", "skill 9 { 0 \"x\" }"}};
  fixture_sources f;
  fixture_open(&f, FILES(files));
  text_log lines = {{NULL}, 0};
  character_report report;
  memset(&report, 0, sizeof report);
  report.sentinel = &report;
  qa_bot_character_options options;
  memset(&options, 0, sizeof options);
  options.memory = &f.sources.memory;
  options.log_write = character_log_write;
  options.log_ctx = &lines;
  options.report = character_report_fn;
  options.report_ctx = &report;
  qa_bot_character_library *c = malloc(sizeof *c);
  qa_bot_character_library_init(c);
  qa_bot_character_library_configure(c, &f.reader, &options);
  int64_t a = qa_bot_character_load(c, "bots/t_c.c", 1);
  int64_t b = qa_bot_character_load(c, "bots/t_c.c", 2.5);
  int64_t d = qa_bot_character_load(c, "bots/t_c.c", 4);
  CHECK(a == 2 && b == 5 && d == 4);
  CHECK_STR(qa_bot_character_string(c, a, 0), "T");
  CHECK(qa_bot_character_float(c, a, 2) == 0.25f);
  CHECK(qa_bot_character_float(c, b, 2) == 0.5f);
  CHECK(qa_bot_character_float(c, b, 4) == 2);
  CHECK(qa_bot_character_integer(c, b, 6) == 3);
  CHECK_STR(qa_bot_character_string(c, b, 0), "T");
  CHECK_STR(qa_bot_character_string(c, b, 3), "defw");
  CHECK(qa_bot_character_integer(c, a, 5) == 7);
  CHECK(qa_bot_character_float(c, a, 6) == 3);
  CHECK(qa_bot_character_integer(c, b, 2) == 0);
  CHECK((double)qa_bot_character_bounded_float(c, b, 2, 0, 0.1) == 0.10000000149011612);
  CHECK((double)qa_bot_character_bounded_float(c, b, 2, 0.9, 1) == 0.8999999761581421);
  CHECK(qa_bot_character_bounded_integer(c, a, 6, 0, 2) == 2);
  CHECK(qa_bot_character_bounded_float(c, b, 2, 1, 0) == 0);
  CHECK(qa_bot_character_bounded_integer(c, a, 6, 1.5, 2) == 0);
  CHECK(qa_bot_character_float(c, a, 0) == 0);
  CHECK_STR(qa_bot_character_string(c, a, 2), "");
  CHECK(qa_bot_character_float(c, a, 70) == 0);
  CHECK(qa_bot_character_float(c, a, 90) == 0);
  CHECK(qa_bot_character_float(c, 0, 0) == 0);
  CHECK(qa_bot_character_float(c, 99, 0) == 0);
  CHECK_LOG(&report.lines,
            "info|loaded|bots/default_c.c|loaded skill 1 from bots/default_c.c",
            "info|loaded|bots/t_c.c|loaded skill 1 from bots/t_c.c",
            "info|loaded|bots/default_c.c|loaded cached skill 1.000000 from bots/default_c.c",
            "info|loaded|bots/t_c.c|loaded cached skill 1.000000 from bots/t_c.c",
            "info|loaded|bots/default_c.c|loaded skill 4 from bots/default_c.c",
            "info|loaded|bots/t_c.c|loaded skill 4 from bots/t_c.c",
            "info|loaded|bots/default_c.c|loaded cached skill 4.000000 from bots/default_c.c",
            "info|loaded|bots/t_c.c|loaded cached skill 4.000000 from bots/t_c.c",
            "error|invalid-bounds|bots/t_c.c|cannot bound characteristic 2 between 1.000000 and 0.000000",
            "error|invalid-bounds|bots/t_c.c|integer characteristic bounds must be integers",
            "error|wrong-type|bots/t_c.c|characteristic 0 is not a float",
            "error|wrong-type|bots/t_c.c|characteristic 2 is not a string",
            "error|uninitialized|bots/t_c.c|characteristic 70 is not initialized",
            "error|invalid-index|bots/t_c.c|characteristic 90 does not exist",
            "fatal|invalid-handle|<character-library>|character handle 0 out of range",
            "fatal|invalid-handle|<character-library>|character handle 99 out of range");
  CHECK_LOG(&lines, "bots/t_c.c", "{\n", "    0 T\n", "    2 0.500000\n", "    3 defw\n", "    4 2.000000\n", "    5 7\n", "    6 3\n", "}\n");
  log_clear(&report.lines);
  CHECK(qa_bot_character_load(c, "bots/bad_c.c", 1) == 1);
  CHECK(qa_bot_character_load(c, "bots/none_c.c", 1) == 1);
  qa_bot_character_file thunk = {NULL, character_name_fn, (void *)"bots/missing_c.c"};
  CHECK(qa_bot_character_load_file(c, thunk, 1) == 1);
  CHECK_LOG(&report.lines,
            "info|loaded|bots/default_c.c|loaded cached skill 1.000000 from bots/default_c.c",
            "error|parse-error|bots/bad_c.c|characteristic 0 already initialized\n",
            "warning|missing-skill|bots/bad_c.c|couldn't find skill 1 in bots/bad_c.c",
            "info|fallback|bots/bad_c.c|loaded cached default skill 1 from bots/bad_c.c",
            "info|loaded|bots/default_c.c|loaded cached skill 1.000000 from bots/default_c.c",
            "warning|missing-skill|bots/none_c.c|couldn't find skill 1 in bots/none_c.c",
            "info|fallback|bots/none_c.c|loaded cached default skill 1 from bots/none_c.c",
            "info|loaded|bots/default_c.c|loaded cached skill 1.000000 from bots/default_c.c",
            "error|missing-source|bots/missing_c.c|counldn't load bots/missing_c.c",
            "warning|missing-skill|bots/missing_c.c|couldn't find skill 1 in bots/missing_c.c",
            "info|fallback|bots/missing_c.c|loaded cached default skill 1 from bots/missing_c.c");
  size_t diagnostic_count = 0;
  const qa_character_diagnostic *diagnostics = qa_bot_character_diagnostics(c, &diagnostic_count);
  bool saw_location = false;
  for (size_t i = 0; i < diagnostic_count; i++)
    if (diagnostics[i].code == QA_CHARACTER_PARSE_ERROR && diagnostics[i].has_location) {
      saw_location = true;
      CHECK_STR(diagnostics[i].location.path, "bots/bad_c.c");
      CHECK(diagnostics[i].location.line == 1 && diagnostics[i].location.column == 17);
    }
  CHECK(saw_location);
  CHECK_STR(qa_bot_character_string(c, qa_bot_character_load(c, "bots/none_c.c", 1), 0), "Def");
  CHECK_ABORTS(qa_bot_character_load(c, "bots/t_c.c", NAN));

  /* Checkpoint round trip. */
  (void)qa_bot_character_diagnostics(c, &diagnostic_count);
  qa_bot_memory_capture capture;
  CHECK(qa_bot_memory_checkpoint(&f.sources.memory, &capture));
  qa_save_value *saved = qa_bot_character_checkpoint(c, &capture);
  qa_bot_memory restored_memory;
  qa_bot_memory_init(&restored_memory);
  qa_bot_memory_restored allocations;
  qa_bot_memory_restore(&restored_memory, capture.image, &allocations);
  qa_bot_character_options restored_options;
  memset(&restored_options, 0, sizeof restored_options);
  restored_options.memory = &restored_memory;
  qa_bot_character_library *again = malloc(sizeof *again);
  qa_bot_character_library_init(again);
  qa_bot_character_library_configure(again, &f.reader, &restored_options);
  qa_bot_character_restore(again, saved, &allocations);
  CHECK_STR(qa_bot_character_string(again, b, 3), "defw");
  CHECK(qa_bot_character_float(again, b, 2) == 0.5f);
  size_t restored_diagnostics = 0;
  (void)qa_bot_character_diagnostics(again, &restored_diagnostics);
  CHECK(restored_diagnostics == diagnostic_count);
  qa_bot_character_shutdown(again);
  CHECK(qa_bot_memory_get_live_allocations(&restored_memory) < 80);
  qa_save_value_free(saved);
  qa_bot_memory_capture_destroy(&capture);
  qa_bot_memory_restored_destroy(&allocations);
  qa_bot_character_library_destroy(again);
  free(again);
  qa_bot_memory_destroy(&restored_memory);

  size_t before = qa_bot_memory_get_live_allocations(&f.sources.memory);
  qa_bot_character_shutdown(c);
  CHECK(qa_bot_memory_get_live_allocations(&f.sources.memory) < before);
  qa_bot_character_library_destroy(c);
  free(c);
  log_clear(&lines);
  log_clear(&report.lines);
  fixture_close(&f);
}

/* ---- weapons oracle ---- */
typedef struct weapon_report {
  const void *sentinel;
  text_log lines;
} weapon_report;
static void weapon_report_fn(void *ctx, const qa_weapon_diagnostic *d) {
  weapon_report *r = ctx;
  CHECK(r->sentinel == r);
  static const char *const names[] = {"message", "warning", "error", "fatal"};
  char line[512];
  snprintf(line, sizeof line, "%s|%s|%s|%s|%lld|%lld", d->origin_print ? "print" : "source", names[d->severity], d->message,
           d->location.path, (long long)d->location.line, (long long)d->location.column);
  log_add(&r->lines, line);
}
typedef struct dump_sink {
  const void *sentinel;
  char text[8192];
} dump_sink;
static bool dump_has_file(void *ctx) {
  dump_sink *d = ctx;
  CHECK(d->sentinel == d);
  return true;
}
static int64_t dump_write(void *ctx, const char *text) {
  dump_sink *d = ctx;
  CHECK(d->sentinel == d);
  strncat(d->text, text, sizeof d->text - strlen(d->text) - 1);
  return (int64_t)strlen(text);
}
static void dump_flush(void *ctx) { dump_write(ctx, "<F>"); }
#define CHECK_WEAPON_SETUP(path, ...)                                \
  do {                                                               \
    CHECK(qa_weapon_ai_setup(w, path) == QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_CONFIG); \
    CHECK_LOG(&report.lines, __VA_ARGS__);                           \
    log_clear(&report.lines);                                        \
  } while (0)
static void test_weapons_oracle(void) {
  static const file_entry files[] = {
      {"weapons.c", "projectileinfo { name \"p\" damage 50 radius 1.5 model \"mdl\" gravity 2 } weaponinfo { name \"Gun\" number 1 projectile \"p\" speed 900 recoil {1,2} offset {3} level 2 hspread 1.5 } weaponinfo { number 3 name \"G3\" projectile \"p\" }"},
      {"w_w.c", "weight \"Gun\" return 10; weight \"G3\" switch(0) { case 5: return 1; default: return 50; }"},
      {"bad1.c", "weaponinfo { number 99 name \"a\" projectile \"p\" }"},
      {"bad2.c", "weaponinfo { number 1 projectile \"p\" }"},
      {"bad3.c", "weaponinfo { number 1 name \"a\" }"},
      {"bad4.c", "weaponinfo { number 1 name \"a\" projectile \"q\" }"},
      {"bad5.c", "nope"},
      {"bad6.c", "weaponinfo { number 1 nosuch 3 }"},
      {"bad7.c", "projectileinfo { name \"p\" } projectileinfo { name \"q\" }"},
      {"d.c", "projectileinfo { name \"p\" damage 5 model \"m\" } weaponinfo { number 1 name \"G\" projectile \"p\" recoil {1,2,3} }"}};
  fixture_sources f;
  fixture_open(&f, FILES(files));
  qa_weight_config_store_bots_a weights;
  store_with_memory(&weights, &f, &f.sources.memory);
  weapon_report report;
  memset(&report, 0, sizeof report);
  report.sentinel = &report;
  qa_weapon_ai_options options;
  memset(&options, 0, sizeof options);
  options.memory = &f.sources.memory;
  options.max_weapon_info = (qa_weapon_capacity){true, 8, NULL, NULL};
  options.max_projectile_info = (qa_weapon_capacity){true, 1, NULL, NULL};
  options.report = weapon_report_fn;
  options.report_ctx = &report;
  qa_weapon_ai_bots_a *w = malloc(sizeof *w);
  qa_weapon_ai_bots_a_init(w);
  qa_weapon_ai_bots_a_configure(w, &f.reader, &weights, &options);
  CHECK(qa_weapon_ai_setup(w, NULL) == QA_WEAPON_LOAD_NO_ERROR);
  qa_weapon_config_bots_a *config = qa_weapon_ai_config(w);
  CHECK_STR(qa_weapon_config_path(config), "weapons.c");
  CHECK(qa_weapon_config_capacity(config) == 8);
  CHECK(qa_weapon_config_defined_count(config) == 2);
  CHECK(qa_weapon_config_projectile_count(config) == 1);
  qa_weapon_info_bots_a info;
  CHECK(qa_weapon_config_weapon(config, 1, &info));
  CHECK(!qa_weapon_config_weapon(config, 0, &info));
  CHECK(qa_weapon_config_weapon(config, 1, &info));
  CHECK(info.valid && info.number == 1);
  CHECK_STR(info.name, "Gun");
  CHECK_STR(info.model, "");
  CHECK(info.level == 2);
  CHECK_STR(info.projectile, "p");
  CHECK(info.horizontal_spread == 1.5f && info.speed == 900);
  CHECK(v3_eq(info.recoil, 1, 2, 0) && v3_eq(info.offset, 3, 0, 0) && v3_eq(info.angle_offset, 0, 0, 0));
  CHECK_STR(info.projectile_info.name, "p");
  /* The source's WEAPON_OFS(model) writes eight bytes past projectile.model. */
  CHECK_STR(info.projectile_info.model, "");
  CHECK(info.projectile_info.gravity == 2 && info.projectile_info.damage == 50 && info.projectile_info.radius == 1.5f);
  qa_projectile_info_bots_a projectile;
  qa_weapon_config_projectile(config, 0, &projectile);
  CHECK_STR(projectile.name, "p");
  CHECK(projectile.damage == 50 && projectile.gravity == 2);
  CHECK_LOG(&report.lines, "print|message|loaded weapons.c|weapons.c|1|1");
  log_clear(&report.lines);

  int64_t s = qa_weapon_ai_allocate_state(w);
  CHECK(qa_weapon_ai_load_weights(w, s, "w_w.c") == QA_WEAPON_LOAD_NO_ERROR);
  CHECK(qa_weapon_ai_choose_best_fight_weapon(w, s, INV(0)) == 1);
  CHECK(qa_weapon_ai_choose_best_fight_weapon(w, s, INV(5)) == 3);
  float weight = 0;
  CHECK(qa_weapon_ai_evaluate_fight_weapon(w, s, 1, INV(0), &weight) && weight == 10);
  CHECK(!qa_weapon_ai_evaluate_fight_weapon(w, s, 2, INV(0), &weight));
  CHECK(!qa_weapon_ai_get_weapon_info(w, s, 0, &info));
  CHECK(qa_weapon_ai_get_weapon_info(w, s, 3, &info));
  CHECK_STR(info.name, "G3");
  size_t record_length = 0;
  CHECK(qa_weapon_ai_weapon_info_bytes(w, s, 3, &record_length) != NULL && record_length == 552);
  CHECK(!qa_weapon_ai_get_weapon_info(w, 99, 1, &info));
  CHECK(qa_weapon_ai_load_weights(w, s, "missing.c") == QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_WEIGHTS);
  CHECK_LOG(&report.lines, "print|error|weapon number out of range|weapons.c|1|1",
            "print|fatal|move state handle 99 out of range|<weapon-ai>|1|1",
            "print|fatal|couldn't load weapon config missing.c|missing.c|1|1");
  log_clear(&report.lines);
  CHECK_WEAPON_SETUP("bad1.c", "print|error|weapon info number 99 out of range in bad1.c|bad1.c|1|1", "print|fatal|couldn't load the weapon config|bad1.c|1|1");
  CHECK_WEAPON_SETUP("bad2.c", "print|error|weapon 1 has no name in bad2.c|bad2.c|1|1", "print|fatal|couldn't load the weapon config|bad2.c|1|1");
  CHECK_WEAPON_SETUP("bad3.c", "print|error|weapon a has no projectile in bad3.c|bad3.c|1|1", "print|fatal|couldn't load the weapon config|bad3.c|1|1");
  CHECK_WEAPON_SETUP("bad4.c", "print|error|weapon a uses undefined projectile in bad4.c|bad4.c|1|1", "print|fatal|couldn't load the weapon config|bad4.c|1|1");
  CHECK_WEAPON_SETUP("bad5.c", "print|error|unknown definition nope in bad5.c|bad5.c|1|1", "print|fatal|couldn't load the weapon config|bad5.c|1|1");
  CHECK_WEAPON_SETUP("bad6.c", "source|error|unknown structure field nosuch|bad6.c|1|23", "print|fatal|couldn't load the weapon config|bad6.c|1|1");
  CHECK_WEAPON_SETUP("bad7.c", "print|error|more than 1 projectiles defined in bad7.c|bad7.c|1|29", "print|fatal|couldn't load the weapon config|bad7.c|1|1");
  CHECK_WEAPON_SETUP("nothere.c", "print|error|counldn't load nothere.c|nothere.c|1|1", "print|fatal|couldn't load the weapon config|nothere.c|1|1");
  CHECK(qa_weapon_ai_config(w) == NULL);
  CHECK_ABORTS(qa_weapon_ai_setup(w, ""));

  dump_sink dump;
  memset(&dump, 0, sizeof dump);
  dump.sentinel = &dump;
  qa_weapon_ai_options dump_options;
  memset(&dump_options, 0, sizeof dump_options);
  dump_options.memory = &f.sources.memory;
  dump_options.max_weapon_info = (qa_weapon_capacity){true, 2, NULL, NULL};
  dump_options.max_projectile_info = (qa_weapon_capacity){true, 1, NULL, NULL};
  dump_options.debug_has_file = dump_has_file;
  dump_options.debug_file_write = dump_write;
  dump_options.debug_flush = dump_flush;
  dump_options.debug_ctx = &dump;
  qa_weapon_ai_bots_a *w3 = malloc(sizeof *w3);
  qa_weapon_ai_bots_a_init(w3);
  qa_weapon_ai_bots_a_configure(w3, &f.reader, &weights, &dump_options);
  CHECK(qa_weapon_ai_setup(w3, "d.c") == QA_WEAPON_LOAD_NO_ERROR);
  /* TS 2026-11-01: the projectile dump reads model at its own offset 80, while the reader keeps
     be_ai_weap.c's WEAPON_OFS(model) write at 88, so the dumped model is empty. */
  CHECK_STR(dump.text,
            "{\r\n\tname\t\"p\"\r\n\tmodel\t\"\"\r\n\tflags\t0\r\n\tgravity\t0\r\n\tdamage\t5\r\n\tradius\t0\r\n\tvisdamage\t0\r\n\tdamagetype\t0\r\n\thealthinc\t0\r\n\tpush\t0\r\n\tdetonation\t0\r\n\tbounce\t0\r\n\tbouncefric\t0\r\n\tbouncestop\t0\r\n}\r\n<F>"
            "{\r\n\tnumber\t0\r\n\tname\t\"\"\r\n\tlevel\t0\r\n\tmodel\t\"\"\r\n\tweaponindex\t0\r\n\tflags\t0\r\n\tprojectile\t\"\"\r\n\tnumprojectiles\t0\r\n\thspread\t0\r\n\tvspread\t0\r\n\tspeed\t0\r\n\tacceleration\t0\r\n\trecoil\t{0,0,0}\r\n\toffset\t{0,0,0}\r\n\tangleoffset\t{0,0,0}\r\n\textrazvelocity\t0\r\n\tammoamount\t0\r\n\tammoindex\t0\r\n\tactivate\t0\r\n\treload\t0\r\n\tspinup\t0\r\n\tspindown\t0\r\n}\r\n<F>"
            "{\r\n\tnumber\t1\r\n\tname\t\"G\"\r\n\tlevel\t0\r\n\tmodel\t\"\"\r\n\tweaponindex\t0\r\n\tflags\t0\r\n\tprojectile\t\"p\"\r\n\tnumprojectiles\t0\r\n\thspread\t0\r\n\tvspread\t0\r\n\tspeed\t0\r\n\tacceleration\t0\r\n\trecoil\t{1,2,3}\r\n\toffset\t{0,0,0}\r\n\tangleoffset\t{0,0,0}\r\n\textrazvelocity\t0\r\n\tammoamount\t0\r\n\tammoindex\t0\r\n\tactivate\t0\r\n\treload\t0\r\n\tspinup\t0\r\n\tspindown\t0\r\n}\r\n<F>");

  /* Checkpoint round trip restores a live state, its weights and the config. */
  CHECK(qa_weapon_ai_setup(w, "weapons.c") == QA_WEAPON_LOAD_NO_ERROR);
  CHECK(qa_weapon_ai_load_weights(w, s, "w_w.c") == QA_WEAPON_LOAD_NO_ERROR);
  log_clear(&report.lines);
  qa_bot_memory_capture capture;
  CHECK(qa_bot_memory_checkpoint(&f.sources.memory, &capture));
  qa_save_value *saved_weights = qa_weight_config_store_checkpoint(&weights, &capture);
  qa_save_value *saved = qa_weapon_ai_checkpoint(w, &capture);
  qa_bot_memory restored_memory;
  qa_bot_memory_init(&restored_memory);
  qa_bot_memory_restored allocations;
  qa_bot_memory_restore(&restored_memory, capture.image, &allocations);
  qa_weight_config_store_options store_options;
  memset(&store_options, 0, sizeof store_options);
  store_options.memory = &restored_memory;
  qa_weight_config_store_bots_a restored_store;
  qa_weight_config_store_bots_a_init(&restored_store);
  qa_weight_config_store_bots_a_configure(&restored_store, &f.reader, &store_options);
  qa_weight_config_store_restore(&restored_store, saved_weights, &allocations);
  qa_weapon_ai_options restored_options;
  memset(&restored_options, 0, sizeof restored_options);
  restored_options.memory = &restored_memory;
  qa_weapon_ai_bots_a *again = malloc(sizeof *again);
  qa_weapon_ai_bots_a_init(again);
  qa_weapon_ai_bots_a_configure(again, &f.reader, &restored_store, &restored_options);
  qa_weapon_ai_restore(again, saved, &allocations);
  CHECK(qa_weapon_ai_choose_best_fight_weapon(again, s, INV(5)) == 3);
  CHECK(qa_weapon_ai_get_weapon_info(again, s, 1, &info));
  CHECK_STR(info.name, "Gun");
  CHECK(qa_weapon_config_defined_count(qa_weapon_ai_config(again)) == 2);
  qa_save_value_free(saved);
  qa_save_value_free(saved_weights);
  qa_bot_memory_capture_destroy(&capture);
  qa_bot_memory_restored_destroy(&allocations);
  qa_weapon_ai_bots_a_destroy(again);
  free(again);
  qa_weight_config_store_bots_a_destroy(&restored_store);
  qa_bot_memory_destroy(&restored_memory);

  /* Negative static capacity: corrected and reported at construction. */
  weapon_report negative;
  memset(&negative, 0, sizeof negative);
  negative.sentinel = &negative;
  qa_weapon_ai_options negative_options;
  memset(&negative_options, 0, sizeof negative_options);
  negative_options.max_weapon_info = (qa_weapon_capacity){true, -1, NULL, NULL};
  negative_options.report = weapon_report_fn;
  negative_options.report_ctx = &negative;
  qa_weapon_ai_bots_a *wn = malloc(sizeof *wn);
  qa_weapon_ai_bots_a_init(wn);
  qa_weapon_ai_bots_a_configure(wn, &f.reader, &weights, &negative_options);
  CHECK_LOG(&negative.lines, "print|error|max_weaponinfo = -1|<weapon-ai>|1|1");
  log_clear(&negative.lines);
  qa_weapon_ai_bots_a_destroy(wn);
  free(wn);

  qa_weapon_ai_free_state(w, s);
  qa_weapon_ai_free_state(w, s);
  CHECK_LOG(&report.lines, "print|fatal|invalid move state 1|<weapon-ai>|1|1");
  log_clear(&report.lines);
  qa_weapon_ai_shutdown(w);
  qa_weapon_ai_shutdown(w3);
  qa_weapon_ai_bots_a_destroy(w);
  qa_weapon_ai_bots_a_destroy(w3);
  free(w);
  free(w3);
  qa_weight_config_store_bots_a_destroy(&weights);
  fixture_close(&f);
}

/* ---- goal fixtures ---- */
typedef struct goal_fixture {
  const void *sentinel;
  double now;
  int64_t game_type;
  text_log logs;
  text_log reports;
  /* TS entities map */
  qa_goal_entity_info entities[8];
  bool entity_live[8];
  int64_t last_entity;
  /* route trace */
  int64_t routes[64];
  size_t route_count;
  /* pickup model */
  qa_identity_owner identity;
  int64_t generation;
  bool useful;
  bool supply_available;
  int64_t mapped_entity; /* 0: the plain pickup entity 50 */
  bool bottom_anchored;
  qa_source_pickup_goal candidate;
  /* ownership */
  int64_t ownership_calls[8][2];
  size_t ownership_count;
  /* retirement hooks */
  qa_bot_goal_library *library;
  bool retire_during_reach;
  const qa_goal_world *retire_world;
  bool retire_in_owner;
  bool projecting_source;
} goal_fixture;
#define FIXTURE(ctx) goal_fixture *fx = ctx; CHECK(fx->sentinel == fx)
static void goal_log(void *ctx, const char *text) { FIXTURE(ctx); log_add(&fx->logs, text); }
static double goal_clock(void *ctx) { FIXTURE(ctx); return fx->now; }
static double goal_game_type(void *ctx) { FIXTURE(ctx); return (double)fx->game_type; }
static void goal_report(void *ctx, const qa_goal_diagnostic *d) {
  FIXTURE(ctx);
  static const char *const names[] = {"message", "warning", "error", "fatal"};
  char line[512];
  snprintf(line, sizeof line, "%s:%s", names[d->severity], d->message);
  log_add(&fx->reports, line);
}
static double host_trace(void *ctx, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds, int64_t pass, int64_t mask) {
  FIXTURE(ctx);
  (void)start; (void)end; (void)bounds; (void)pass; (void)mask;
  return 1;
}
static int64_t host_contents(void *ctx, qa_vec3 point) { FIXTURE(ctx); (void)point; return 0; }
static int64_t host_next(void *ctx, int64_t after) { FIXTURE(ctx); return after < fx->last_entity ? after + 1 : 0; }
static qa_goal_entity_info host_info(void *ctx, int64_t entity) {
  FIXTURE(ctx);
  if (entity < 0 || entity >= 8 || !fx->entity_live[entity]) {
    fprintf(stderr, "Missing item entity\n");
    abort();
  }
  return fx->entities[entity];
}
static qa_goal_area_info nav_area(void *ctx, int64_t number) { FIXTURE(ctx); (void)number; return (qa_goal_area_info){0, 1}; }
static int64_t nav_jump(void *ctx, qa_vec3 origin, qa_bounds bounds) { FIXTURE(ctx); (void)origin; (void)bounds; return 0; }
static qa_goal_drop_result nav_drop(void *ctx, qa_vec3 origin, qa_bounds bounds) { FIXTURE(ctx); (void)bounds; return (qa_goal_drop_result){origin, true}; }
static qa_goal_reachable_position nav_best(void *ctx, qa_vec3 origin, qa_bounds bounds) {
  FIXTURE(ctx);
  (void)bounds;
  return (qa_goal_reachable_position){origin.x == 100 ? 2 : 3, origin};
}
static int64_t nav_reach(void *ctx, qa_vec3 origin, int64_t client) { FIXTURE(ctx); (void)origin; (void)client; return 1; }
static qa_goal_route_result nav_route(void *ctx, const qa_goal_route_request *request) {
  FIXTURE(ctx);
  if (fx->route_count < 64) fx->routes[fx->route_count++] = request->goal_area;
  return (qa_goal_route_result){true, request->goal_area == 2 ? 100 : 200};
}
/* TS `bestReachableArea` wrappers from the retirement and projection cases. */
static qa_goal_reachable_position nav_best_retiring(void *ctx, qa_vec3 origin, qa_bounds bounds) {
  FIXTURE(ctx);
  if (fx->retire_during_reach) {
    fx->retire_during_reach = false;
    qa_bot_goal_init_level_items(fx->library, fx->retire_world);
  }
  return nav_best(ctx, origin, bounds);
}
static qa_goal_reachable_position nav_best_projecting(void *ctx, qa_vec3 origin, qa_bounds bounds) {
  FIXTURE(ctx);
  if (!fx->projecting_source) return nav_best(ctx, origin, bounds);
  CHECK(v3_eq(origin, 200, 0, 43));
  CHECK(v3_eq(bounds.min, -8, -8, -28) && v3_eq(bounds.max, 8, 8, 28));
  origin.z = 39;
  return (qa_goal_reachable_position){3, origin};
}
static qa_source_pickup_goal pickup_of(goal_fixture *fx) {
  qa_source_pickup_goal goal;
  memset(&goal, 0, sizeof goal);
  goal.actor = qa_identity_owner_actor(&fx->identity, 50, fx->generation);
  goal.entity = fx->mapped_entity != 0 ? fx->mapped_entity : 50;
  goal.origin = v3(200, 0, 15);
  goal.bounds = (qa_bounds){v3(-8, -8, fx->bottom_anchored ? 0 : -8), v3(8, 8, fx->bottom_anchored ? 56 : 8)};
  goal.name = "Shell supply";
  goal.utility = 100;
  return goal;
}
static const qa_source_pickup_goal *pickup_candidates(void *ctx, int64_t client, size_t *out_count) {
  FIXTURE(ctx);
  (void)client;
  if (fx->mapped_entity != 0 && !fx->supply_available) {
    *out_count = 0;
    return NULL;
  }
  fx->candidate = pickup_of(fx);
  *out_count = 1;
  return &fx->candidate;
}
static bool pickup_inspect(void *ctx, int64_t client, qa_actor_id actor, qa_source_pickup_goal *out) {
  FIXTURE(ctx);
  (void)client;
  if (fx->mapped_entity != 0 && !fx->supply_available) return false;
  qa_source_pickup_goal current = pickup_of(fx);
  if (!fx->useful || actor.slot != current.actor.slot || actor.generation != current.actor.generation) return false;
  *out = current;
  return true;
}
static bool pickup_owns(void *ctx, int64_t client, int64_t entity) {
  FIXTURE(ctx);
  if (fx->retire_in_owner) {
    qa_bot_goal_init_level_items(fx->library, fx->retire_world);
    return true;
  }
  if (fx->ownership_count < 8) {
    fx->ownership_calls[fx->ownership_count][0] = client;
    fx->ownership_calls[fx->ownership_count][1] = entity;
    fx->ownership_count++;
  }
  return entity == 2;
}
static int64_t point_area_one(void *ctx, qa_vec3 origin) { FIXTURE(ctx); (void)origin; return 1; }
static qa_goal_world fixture_world(goal_fixture *fx, qa_aas_bsp_entities *bsp, bool pickups) {
  qa_goal_world world;
  memset(&world, 0, sizeof world);
  world.bsp_entities = bsp;
  world.has_navigation = true;
  world.navigation = (qa_goal_navigation){fx, nav_area, nav_jump, nav_drop, nav_best, nav_reach, nav_route};
  world.host = (qa_goal_world_host){fx, host_trace, host_contents, host_next, host_info};
  world.point_area = point_area_one;
  world.point_area_ctx = fx;
  world.has_source_pickups = pickups;
  world.source_pickups = (qa_source_pickup_goals){fx, NULL, pickup_candidates, pickup_inspect};
  return world;
}
static void set_entity(goal_fixture *fx, int64_t entity, double x, int64_t model) {
  qa_vec3 origin = v3(x, 0, 15);
  fx->entities[entity] = (qa_goal_entity_info){2, model, origin, origin, fx->now};
  fx->entity_live[entity] = true;
}
static qa_bot_goal top_goal(qa_bot_goal_library *l, int64_t state) {
  qa_bot_goal goal;
  memset(&goal, 0, sizeof goal);
  CHECK(qa_bot_goal_get_top_goal(l, state, &goal));
  return goal;
}
static void goal_library_open(qa_bot_goal_library *l, goal_fixture *fx, fixture_sources *f, qa_weight_config_store_bots_a *store,
                              qa_bot_random random) {
  qa_bot_goal_options options;
  memset(&options, 0, sizeof options);
  options.memory = &f->sources.memory;
  options.resolver = &f->reader;
  options.weight_store = store;
  options.log_write = goal_log;
  options.log_ctx = fx;
  options.clock = goal_clock;
  options.clock_ctx = fx;
  options.random = random;
  options.game_type = goal_game_type;
  options.game_type_ctx = fx;
  options.report = goal_report;
  options.report_ctx = fx;
  qa_bot_goal_library_init(l);
  qa_bot_goal_library_configure(l, &options);
  fx->library = l;
}

/* source-behavior: item goals combine source weights, borrowed route costs, respawn avoidance. */
static void test_item_goals(void) {
  static const file_entry files[] = {
      {"items.c", "iteminfo \"item_health\" { name \"Health\" modelindex 5 respawntime 35 mins {-15,-15,-15} maxs {15,15,15} }\n"
                  "iteminfo \"item_armor\" { name \"Armor\" modelindex 6 respawntime 25 mins {-15,-15,-15} maxs {15,15,15} }\n"
                  "iteminfo \"weapon_shotgun\" { name \"Shotgun\" modelindex 7 respawntime 30 mins {-15,-15,-15} maxs {15,15,15} }"},
      {"weights.c", "weight \"item_health\" return 10; weight \"item_armor\" return 30; weight \"weapon_shotgun\" return balance(150,100,200);"}};
  fixture_sources f;
  fixture_open(&f, FILES(files));
  qa_weight_config_store_bots_a *store = malloc(sizeof *store);
  store_with_memory(store, &f, &f.sources.memory);
  goal_fixture *fx = calloc(1, sizeof *fx);
  fx->sentinel = fx;
  fx->now = 50;
  fx->last_entity = 2;
  fx->useful = true;
  qa_identity_owner_create(&fx->identity, "source-pickup-goals");
  random_fixture rnd;
  qa_bot_random random = random_of(&rnd, 0);
  qa_bot_goal_library *l = malloc(sizeof *l);
  goal_library_open(l, fx, &f, store, random);
  qa_aas_bsp_entities *bsp = malloc(sizeof *bsp);
  qa_aas_bsp_entities_init(bsp);
  qa_aas_bsp_entities_configure(bsp, NULL, NULL, &f.sources.memory);
  set_entity(fx, 1, 100, 5);
  set_entity(fx, 2, 200, 6);
  qa_vec3 zero = v3(0, 0, 0);
  qa_weight_inventory inventory = INV(0);

  CHECK(qa_bot_goal_setup(l, NULL) == QA_GOAL_ERROR_NONE);
  qa_aas_bsp_entities_load(bsp, "{ \"classname\" \"worldspawn\" } { \"classname\" \"item_health\" \"origin\" \"100 0 15\" } { \"classname\" \"item_armor\" \"origin\" \"200 0 15\" }");
  qa_goal_world plain = fixture_world(fx, bsp, false);
  qa_bot_goal_init_level_items(l, &plain);
  qa_bot_goal_update_entity_items(l);
  int64_t state = qa_bot_goal_alloc_goal_state(l, 0);
  CHECK(qa_bot_goal_load_item_weights(l, state, "weights.c") == QA_GOAL_ERROR_NONE);
  CHECK(qa_bot_goal_choose_ltg_item(l, state, zero, inventory, 0));
  CHECK(top_goal(l, state).entity == 2);
  CHECK(qa_bot_goal_avoid_goal_time(l, state, 2) == 25);
  fx->now = 51;
  CHECK(qa_bot_goal_avoid_goal_time(l, state, 2) == 24);
  CHECK(qa_bot_goal_choose_ltg_item(l, state, zero, inventory, 0));
  CHECK(top_goal(l, state).entity == 1);
  CHECK(qa_bot_goal_avoid_goal_time(l, state, 1) == 35);
  CHECK(fx->route_count == 4 && fx->routes[0] == 3 && fx->routes[1] == 2 && fx->routes[2] == 3 && fx->routes[3] == 2);

  qa_goal_world world = fixture_world(fx, bsp, true);
  qa_bot_goal_init_level_items(l, &world);
  qa_bot_goal_update_entity_items(l);
  CHECK(qa_bot_goal_choose_ltg_item(l, state, zero, inventory, 0));
  qa_bot_goal source = top_goal(l, state);
  CHECK(source.number == 0x7fffffff);
  CHECK(qa_is_source_goal_number((double)source.number));
  CHECK_STR(qa_bot_goal_goal_name(l, source.number), "Shell supply");
  qa_bot_goal found;
  CHECK(qa_bot_goal_get_level_item_goal(l, -1, qa_goal_name_text("Shell supply"), NULL, &found) && found.number == source.number);
  CHECK(!qa_bot_goal_get_level_item_goal(l, source.number, qa_goal_name_text("Shell supply"), NULL, &found));
  CHECK(qa_bot_goal_avoid_goal_time(l, state, source.number) == 0);
  CHECK(qa_bot_goal_source_goal_status(l, 0, &source) == QA_SOURCE_GOAL_AVAILABLE);
  CHECK(!qa_bot_goal_item_goal_in_vis_but_not_visible(l, 0, source.origin, source.origin, &source));
  qa_bot_goal_set_avoid_goal_time(l, state, source.number, 5);
  CHECK(!qa_bot_goal_choose_ltg_item(l, state, zero, inventory, 0));
  qa_bot_goal_remove_from_avoid_goals(l, state, source.number);
  CHECK(qa_bot_goal_choose_nbg_item(l, state, zero, inventory, 0, NULL, 201));
  CHECK(top_goal(l, state).number == source.number);
  fx->useful = false;
  CHECK(qa_bot_goal_source_goal_status(l, 0, &source) == QA_SOURCE_GOAL_UNAVAILABLE);
  CHECK(qa_bot_goal_item_goal_in_vis_but_not_visible(l, 0, source.origin, source.origin, &source));
  fx->useful = true;
  fx->generation++;
  CHECK(qa_bot_goal_source_goal_status(l, 0, &source) == QA_SOURCE_GOAL_UNAVAILABLE);
  CHECK(qa_bot_goal_choose_ltg_item(l, state, zero, inventory, 0));
  qa_bot_goal replacement = top_goal(l, state);
  CHECK(replacement.number == source.number - 1);
  qa_bot_goal_init_level_items(l, &world);
  qa_bot_goal_update_entity_items(l);
  CHECK(qa_bot_goal_source_goal_status(l, 0, &replacement) == QA_SOURCE_GOAL_UNAVAILABLE);
  CHECK(qa_bot_goal_choose_ltg_item(l, state, zero, inventory, 0));
  CHECK(top_goal(l, state).number == replacement.number - 1);

  /* Retirement during the source reachability projection. */
  qa_goal_world retiring = world;
  retiring.navigation.best_reachable_area = nav_best_retiring;
  fx->retire_world = &world;
  fx->retire_during_reach = false;
  qa_bot_goal_init_level_items(l, &retiring);
  qa_bot_goal_update_entity_items(l);
  fx->retire_during_reach = true;
  CHECK(!qa_bot_goal_choose_ltg_item(l, state, zero, inventory, 0));
  CHECK(!qa_bot_goal_get_level_item_goal(l, -1, qa_goal_name_text("Shell supply"), NULL, &found));
  CHECK(qa_bot_goal_choose_ltg_item(l, state, zero, inventory, 0));
  CHECK(top_goal(l, state).number == replacement.number - 2);
  qa_bot_goal_empty_goal_stack(l, state);

  /* A bottom-anchored source pickup projects through its world-space center. */
  qa_goal_world anchored_world = world;
  anchored_world.navigation.best_reachable_area = nav_best_projecting;
  fx->bottom_anchored = true;
  fx->projecting_source = false;
  qa_bot_goal_init_level_items(l, &anchored_world);
  qa_bot_goal_update_entity_items(l);
  fx->projecting_source = true;
  CHECK(qa_bot_goal_choose_ltg_item(l, state, zero, inventory, 0));
  qa_bot_goal anchored = top_goal(l, state);
  CHECK(v3_eq(anchored.origin, 200, 0, 39));
  CHECK(v3_eq(anchored.mins, -8, -8, -24));
  CHECK(v3_eq(anchored.maxs, 8, 8, 32));
  fx->projecting_source = false;
  fx->bottom_anchored = false;

  /* Native weapon goals and mapped-pickup ownership. */
  qa_aas_bsp_entities_load(bsp, "{ \"classname\" \"worldspawn\" } { \"classname\" \"item_health\" \"origin\" \"100 0 15\" } { \"classname\" \"weapon_shotgun\" \"origin\" \"200 0 15\" }");
  fx->entities[2] = (qa_goal_entity_info){2, 7, v3(200, 0, 15), v3(200, 0, 15), fx->now};
  fx->supply_available = false;
  fx->mapped_entity = 2;
  qa_goal_world unowned = world;
  qa_bot_goal_init_level_items(l, &unowned);
  qa_bot_goal_update_entity_items(l);
  qa_bot_goal_reset_goal_state(l, state);
  rnd.calls = 0;
  CHECK(qa_bot_goal_choose_ltg_item(l, state, zero, inventory, 0));
  qa_bot_goal native_weapon = top_goal(l, state);
  CHECK(native_weapon.entity == 2);
  CHECK(!qa_is_source_goal_number((double)native_weapon.number));
  CHECK(rnd.calls == 2);

  qa_goal_world owned = unowned;
  owned.source_pickups.owns_item_goal = pickup_owns;
  qa_bot_goal_init_level_items(l, &owned);
  qa_bot_goal_update_entity_items(l);
  qa_bot_goal_reset_goal_state(l, state);
  rnd.calls = 0;
  fx->ownership_count = 0;
  CHECK(qa_bot_goal_choose_ltg_item(l, state, zero, inventory, 0));
  CHECK(top_goal(l, state).entity == 1);
  CHECK(rnd.calls == 1);
  CHECK(fx->ownership_count == 2 && fx->ownership_calls[0][0] == 0 && fx->ownership_calls[0][1] == 2 &&
        fx->ownership_calls[1][0] == 0 && fx->ownership_calls[1][1] == 1);
  CHECK_STR(qa_bot_goal_goal_name(l, native_weapon.number), "Shotgun");
  CHECK(qa_bot_goal_get_level_item_goal(l, -1, qa_goal_name_text("Shotgun"), NULL, &found) && found.entity == 2);
  fx->supply_available = true;
  qa_bot_goal_reset_goal_state(l, state);
  rnd.calls = 0;
  CHECK(qa_bot_goal_choose_nbg_item(l, state, zero, inventory, 0, NULL, 201));
  qa_bot_goal mapped = top_goal(l, state);
  CHECK(mapped.entity == 2);
  CHECK(qa_is_source_goal_number((double)mapped.number));
  CHECK(rnd.calls == 1);

  /* The ownership callback retires the map mid-loop: no choice, no draw. */
  fx->supply_available = false;
  qa_goal_world retiring_owner = unowned;
  retiring_owner.source_pickups.owns_item_goal = pickup_owns;
  fx->retire_in_owner = false;
  qa_bot_goal_init_level_items(l, &retiring_owner);
  qa_bot_goal_update_entity_items(l);
  qa_bot_goal_reset_goal_state(l, state);
  rnd.calls = 0;
  fx->retire_in_owner = true;
  fx->retire_world = &unowned;
  CHECK(!qa_bot_goal_choose_ltg_item(l, state, zero, inventory, 0));
  CHECK(!qa_bot_goal_get_top_goal(l, state, &found));
  CHECK(rnd.calls == 0);

  qa_bot_goal_shutdown(l);
  qa_weight_config_store_shutdown(store);
  log_clear(&fx->logs);
  log_clear(&fx->reports);
  qa_bot_goal_library_destroy(l);
  qa_weight_config_store_bots_a_destroy(store);
  qa_aas_bsp_entities_destroy(bsp);
  qa_identity_owner_destroy(&fx->identity);
  free(l);
  free(store);
  free(bsp);
  free(fx);
  fixture_close(&f);
}

/* round-restart.test: rebinding retains map allocations and replaces observers. */
typedef struct round_world {
  const void *sentinel;
  qa_identity_owner *identity;
  int64_t generation;
  int64_t reads;
  char name[32];
  qa_source_pickup_goal pickup;
} round_world;
static qa_source_pickup_goal round_pickup(round_world *w) {
  qa_source_pickup_goal goal;
  memset(&goal, 0, sizeof goal);
  goal.actor = qa_identity_owner_actor(w->identity, 50, w->generation);
  goal.entity = 50;
  goal.origin = v3(100, 0, 15);
  goal.bounds = (qa_bounds){v3(-8, -8, -8), v3(8, 8, 8)};
  goal.name = w->name;
  goal.utility = 100;
  return goal;
}
static int64_t round_next(void *ctx, int64_t after) {
  round_world *w = ctx;
  CHECK(w->sentinel == w);
  (void)after;
  w->reads++;
  return 0;
}
static const qa_source_pickup_goal *round_candidates(void *ctx, int64_t client, size_t *out_count) {
  round_world *w = ctx;
  CHECK(w->sentinel == w);
  (void)client;
  w->reads++;
  w->pickup = round_pickup(w);
  *out_count = 1;
  return &w->pickup;
}
static bool round_inspect(void *ctx, int64_t client, qa_actor_id actor, qa_source_pickup_goal *out) {
  round_world *w = ctx;
  CHECK(w->sentinel == w);
  (void)client;
  w->reads++;
  qa_source_pickup_goal goal = round_pickup(w);
  if (actor.slot != goal.actor.slot || actor.generation != goal.actor.generation) return false;
  *out = goal;
  return true;
}
static qa_goal_reachable_position round_best(void *ctx, qa_vec3 origin, qa_bounds bounds) {
  FIXTURE(ctx);
  (void)bounds;
  return (qa_goal_reachable_position){1, origin};
}
static qa_goal_route_result round_route(void *ctx, const qa_goal_route_request *request) {
  FIXTURE(ctx);
  (void)request;
  return (qa_goal_route_result){true, 100};
}
static qa_goal_world round_goal_world(goal_fixture *fx, qa_aas_bsp_entities *bsp, round_world *w) {
  qa_goal_world world = fixture_world(fx, bsp, true);
  world.navigation.best_reachable_area = round_best;
  world.navigation.route = round_route;
  world.host.ctx = w;
  world.host.next_entity = round_next;
  world.source_pickups = (qa_source_pickup_goals){w, NULL, round_candidates, round_inspect};
  return world;
}
static qa_actor_id restore_actor(void *ctx, qa_saved_actor_id saved) {
  return qa_identity_owner_actor(ctx, (int64_t)saved.slot, (int64_t)saved.generation);
}
static void test_round_restart(void) {
  static const file_entry files[] = {
      {"items.c", "iteminfo \"item_health\" { name \"Health\" modelindex 5 respawntime 35 mins {-15,-15,-15} maxs {15,15,15} }"},
      {"weights.c", "weight \"item_health\" return 10;"}};
  fixture_sources f;
  fixture_open(&f, FILES(files));
  qa_weight_config_store_bots_a *store = malloc(sizeof *store);
  store_with_memory(store, &f, &f.sources.memory);
  goal_fixture *fx = calloc(1, sizeof *fx);
  fx->sentinel = fx;
  fx->now = 50;
  qa_identity_owner identity;
  qa_identity_owner_create(&identity, "bot-round-goals");
  random_fixture rnd;
  qa_bot_goal_library *goals = malloc(sizeof *goals);
  goal_library_open(goals, fx, &f, store, random_of(&rnd, 0));
  qa_aas_bsp_entities *bsp = malloc(sizeof *bsp);
  qa_aas_bsp_entities_init(bsp);
  qa_aas_bsp_entities_configure(bsp, NULL, NULL, &f.sources.memory);
  round_world old_round = {&old_round, &identity, 0, 0, "Supply 0", {{{NULL, NULL}, 0, 0, NULL}, 0, {0, 0, 0}, {{0, 0, 0}, {0, 0, 0}}, NULL, 0}};
  round_world new_round = {&new_round, &identity, 1, 0, "Supply 1", {{{NULL, NULL}, 0, 0, NULL}, 0, {0, 0, 0}, {{0, 0, 0}, {0, 0, 0}}, NULL, 0}};
  qa_vec3 zero = v3(0, 0, 0);
  CHECK(qa_bot_goal_setup(goals, NULL) == QA_GOAL_ERROR_NONE);
  qa_aas_bsp_entities_load(bsp, "{ \"classname\" \"worldspawn\" } { \"classname\" \"item_health\" \"origin\" \"100 0 15\" }");
  qa_goal_world old_world = round_goal_world(fx, bsp, &old_round), new_world = round_goal_world(fx, bsp, &new_round);
  qa_bot_goal_init_level_items(goals, &old_world);
  int64_t old_state = qa_bot_goal_alloc_goal_state(goals, 0);
  CHECK(qa_bot_goal_load_item_weights(goals, old_state, "weights.c") == QA_GOAL_ERROR_NONE);
  CHECK(qa_bot_goal_choose_ltg_item(goals, old_state, zero, INV(0), 0));
  qa_bot_goal old_goal = top_goal(goals, old_state);
  CHECK(qa_bot_goal_source_goal_status(goals, 0, &old_goal) == QA_SOURCE_GOAL_AVAILABLE);
  CHECK_ABORTS(qa_bot_goal_rebind_world(goals, &new_world));
  qa_bot_goal_free_goal_state(goals, old_state);
  qa_bot_memory_capture before;
  CHECK(qa_bot_memory_checkpoint(&f.sources.memory, &before));
  size_t before_length = 0;
  uint8_t *before_bytes = qa_encode_checkpoint_value(before.image, &before_length);
  qa_bot_goal native_goal;
  CHECK(qa_bot_goal_get_level_item_goal(goals, -1, qa_goal_name_text("Health"), NULL, &native_goal));
  int64_t reads_before = old_round.reads;
  qa_bot_goal_rebind_world(goals, &new_world);
  qa_bot_memory_capture after;
  CHECK(qa_bot_memory_checkpoint(&f.sources.memory, &after));
  size_t after_length = 0;
  uint8_t *after_bytes = qa_encode_checkpoint_value(after.image, &after_length);
  CHECK(before_length == after_length && memcmp(before_bytes, after_bytes, before_length) == 0);
  qa_bot_goal again;
  CHECK(qa_bot_goal_get_level_item_goal(goals, -1, qa_goal_name_text("Health"), NULL, &again));
  CHECK(memcmp(&again, &native_goal, sizeof again) == 0);
  CHECK(qa_bot_goal_source_goal_status(goals, 0, &old_goal) == QA_SOURCE_GOAL_UNAVAILABLE);
  qa_bot_goal_update_entity_items(goals);
  int64_t state = qa_bot_goal_alloc_goal_state(goals, 0);
  CHECK(qa_bot_goal_load_item_weights(goals, state, "weights.c") == QA_GOAL_ERROR_NONE);
  CHECK(qa_bot_goal_choose_ltg_item(goals, state, zero, INV(0), 0));
  qa_bot_goal new_goal = top_goal(goals, state);
  CHECK(new_goal.number < old_goal.number);
  CHECK_STR(qa_bot_goal_goal_name(goals, new_goal.number), "Supply 1");
  CHECK(qa_bot_goal_source_goal_status(goals, 0, &new_goal) == QA_SOURCE_GOAL_AVAILABLE);
  CHECK(qa_bot_goal_source_goal_status(goals, 0, &old_goal) == QA_SOURCE_GOAL_UNAVAILABLE);
  CHECK(old_round.reads == reads_before);
  CHECK(new_round.reads > 0);

  /* Goal library checkpoint: restored state keeps its goals, avoid table and source binding. */
  qa_bot_goal_set_avoid_goal_time(goals, state, 77, 5);
  qa_bot_memory_capture capture;
  CHECK(qa_bot_memory_checkpoint(&f.sources.memory, &capture));
  qa_save_value *saved_weights = qa_weight_config_store_checkpoint(store, &capture);
  qa_save_value *saved_bsp = qa_aas_bsp_entities_checkpoint(bsp, &capture);
  qa_save_value *saved = qa_bot_goal_checkpoint(goals, &capture);
  qa_bot_memory restored_memory;
  qa_bot_memory_init(&restored_memory);
  qa_bot_memory_restored allocations;
  qa_bot_memory_restore(&restored_memory, capture.image, &allocations);
  qa_weight_config_store_options store_options;
  memset(&store_options, 0, sizeof store_options);
  store_options.memory = &restored_memory;
  qa_weight_config_store_bots_a *restored_store = malloc(sizeof *restored_store);
  qa_weight_config_store_bots_a_init(restored_store);
  qa_weight_config_store_bots_a_configure(restored_store, &f.reader, &store_options);
  qa_weight_config_store_restore(restored_store, saved_weights, &allocations);
  qa_aas_bsp_entities *restored_bsp = malloc(sizeof *restored_bsp);
  qa_aas_bsp_entities_init(restored_bsp);
  qa_aas_bsp_entities_configure(restored_bsp, NULL, NULL, &restored_memory);
  qa_aas_bsp_entities_restore(restored_bsp, saved_bsp, &allocations);
  qa_bot_goal_library *restored = malloc(sizeof *restored);
  qa_bot_goal_options options = goals->options;
  options.memory = &restored_memory;
  options.weight_store = restored_store;
  qa_bot_goal_library_init(restored);
  qa_bot_goal_library_configure(restored, &options);
  qa_goal_world restored_world = round_goal_world(fx, restored_bsp, &new_round);
  qa_bot_goal_restore(restored, saved, &allocations, &restored_world, restore_actor, &identity);
  CHECK(qa_bot_goal_source_goal_status(restored, 0, &new_goal) == QA_SOURCE_GOAL_AVAILABLE);
  CHECK_STR(qa_bot_goal_goal_name(restored, new_goal.number), "Supply 1");
  qa_bot_goal round_trip = top_goal(restored, state);
  CHECK(round_trip.number == new_goal.number);
  CHECK(qa_bot_goal_avoid_goal_time(restored, state, 77) == 5);
  CHECK(qa_bot_goal_get_level_item_goal(restored, -1, qa_goal_name_text("Health"), NULL, &again));
  CHECK(again.number == native_goal.number);
  qa_save_value_free(saved);
  qa_save_value_free(saved_bsp);
  qa_save_value_free(saved_weights);
  qa_bot_memory_capture_destroy(&capture);
  qa_bot_memory_restored_destroy(&allocations);
  qa_bot_goal_library_destroy(restored);
  qa_weight_config_store_bots_a_destroy(restored_store);
  qa_aas_bsp_entities_destroy(restored_bsp);
  qa_bot_memory_destroy(&restored_memory);
  free(restored);
  free(restored_store);
  free(restored_bsp);

  free(before_bytes);
  free(after_bytes);
  qa_bot_memory_capture_destroy(&before);
  qa_bot_memory_capture_destroy(&after);
  qa_bot_goal_shutdown(goals);
  qa_weight_config_store_shutdown(store);
  log_clear(&fx->logs);
  log_clear(&fx->reports);
  qa_bot_goal_library_destroy(goals);
  qa_weight_config_store_bots_a_destroy(store);
  qa_aas_bsp_entities_destroy(bsp);
  qa_identity_owner_destroy(&identity);
  free(goals);
  free(store);
  free(bsp);
  free(fx);
  fixture_close(&f);
}

/* ---- goals oracle: item parsing, stack, avoid table, map info ---- */
typedef struct item_report {
  const void *sentinel;
  text_log lines;
} item_report;
static void item_report_fn(void *ctx, const qa_goal_diagnostic *d) {
  item_report *r = ctx;
  CHECK(r->sentinel == r);
  static const char *const names[] = {"message", "warning", "error", "fatal"};
  char line[512];
  snprintf(line, sizeof line, "%s:%s", names[d->severity], d->message);
  log_add(&r->lines, line);
}
static void item_error_case(const qa_bot_script_reader *reader, qa_bot_memory *memory, const char *path, double maximum,
                            const char *thrown, const char *reported) {
  item_report report;
  memset(&report, 0, sizeof report);
  report.sentinel = &report;
  qa_item_config_options options;
  memset(&options, 0, sizeof options);
  options.memory = memory;
  options.report = item_report_fn;
  options.report_ctx = &report;
  if (maximum >= 0) {
    options.has_max_items = true;
    options.max_items = maximum;
  }
  char *error = NULL;
  CHECK(qa_load_item_config(reader, path, &options, &error) == NULL);
  CHECK_STR(error != NULL ? error : "", thrown);
  CHECK_LOG(&report.lines, reported);
  free(error);
  log_clear(&report.lines);
}
static int64_t oracle_point_area(void *ctx, qa_vec3 origin) { FIXTURE(ctx); return origin.x == 7 ? 0 : 3; }
static qa_goal_reachable_position oracle_best(void *ctx, qa_vec3 origin, qa_bounds bounds) {
  FIXTURE(ctx);
  (void)bounds;
  return (qa_goal_reachable_position){origin.x == 10 ? 0 : 2, origin};
}
static int64_t no_entity(void *ctx, int64_t after) { FIXTURE(ctx); (void)after; return 0; }
static const uint8_t *push_thunk(void *ctx, qa_bot_goal *goal, size_t *out_length) {
  (void)out_length;
  *goal = *(const qa_bot_goal *)ctx;
  goal->number = 8;
  return NULL;
}
static void test_goals_oracle(void) {
  static const file_entry files[] = {
      {"items.c", "iteminfo \"a\" { name \"Alpha\" model \"m\" modelindex 3 type 4 index 5 respawntime 7.5 mins {-1,-2} maxs {1,2,3} } iteminfo \"b\" { name \"Beta\" }"},
      {"e1.c", "iteminfo 5 {}"},
      {"e2.c", "nope"},
      {"e3.c", "iteminfo \"a\" { bad 1 }"},
      {"e4.c", "iteminfo \"a\" {} iteminfo \"b\" {}"},
      {"e5.c", ""}};
  fixture_sources f;
  fixture_open(&f, FILES(files));
  qa_bot_memory *memory = &f.sources.memory;
  item_report report;
  memset(&report, 0, sizeof report);
  report.sentinel = &report;
  qa_item_config_options options;
  memset(&options, 0, sizeof options);
  options.memory = memory;
  options.report = item_report_fn;
  options.report_ctx = &report;
  qa_item_config_bots_a *config = qa_load_item_config(&f.reader, "items.c", &options, NULL);
  CHECK_STR(qa_item_config_path(config), "items.c");
  CHECK(qa_item_config_count(config) == 2);
  qa_item_info_bots_a item;
  qa_item_config_item(config, 0, &item);
  CHECK_STR(item.classname, "a");
  CHECK_STR(item.name, "Alpha");
  CHECK_STR(item.model, "m");
  CHECK(item.model_index == 3 && item.type == 4 && item.index == 5 && item.respawn_time == 7.5f && item.number == 0);
  CHECK(v3_eq(item.mins, -1, -2, 0) && v3_eq(item.maxs, 1, 2, 3));
  qa_item_config_item(config, 1, &item);
  CHECK(item.number == 1);
  CHECK_LOG(&report.lines, "message:loaded items.c\n");
  log_clear(&report.lines);
  qa_item_config_free(config);
  qa_item_config_destroy(config);
  item_error_case(&f.reader, memory, "e1.c", -1, "e1.c:1:10: expected string, found 5", "error:file e1.c, line 1: expected string, found 5\n");
  item_error_case(&f.reader, memory, "e2.c", -1, "e2.c:1:1: unknown definition nope\n", "error:file e2.c, line 1: unknown definition nope\n\n");
  item_error_case(&f.reader, memory, "e3.c", -1, "e3.c:1:16: unknown structure field bad", "error:file e3.c, line 1: unknown structure field bad\n");
  item_error_case(&f.reader, memory, "missing.c", -1, "missing.c:1:1: couldn't load missing.c", "error:counldn't load missing.c\n");
  item_error_case(&f.reader, memory, "e4.c", 1, "e4.c:1:17: more than 1 item info defined\n", "error:file e4.c, line 1: more than 1 item info defined\n\n");
  config = qa_load_item_config(&f.reader, "e5.c", &options, NULL);
  CHECK(qa_item_config_count(config) == 0);
  size_t diagnostic_count = 0;
  const qa_ui_script_diagnostic *diagnostics = qa_item_config_diagnostics(config, &diagnostic_count);
  CHECK(diagnostic_count == 1);
  if (diagnostic_count == 1) CHECK_STR(diagnostics[0].message, "no item info loaded");
  CHECK_LOG(&report.lines, "warning:no item info loaded\n", "message:loaded e5.c\n");
  log_clear(&report.lines);
  qa_item_config_free(config);
  qa_item_config_destroy(config);

  qa_weight_config_store_bots_a *store = malloc(sizeof *store);
  store_with_memory(store, &f, memory);
  goal_fixture *fx = calloc(1, sizeof *fx);
  fx->sentinel = fx;
  fx->now = 10;
  random_fixture rnd;
  qa_bot_goal_library *g = malloc(sizeof *g);
  goal_library_open(g, fx, &f, store, random_of(&rnd, 0));
  CHECK(qa_bot_goal_setup(g, NULL) == QA_GOAL_ERROR_NONE);
  CHECK(qa_bot_goal_game_type(g) == 0);
  CHECK(qa_bot_goal_setup(g, "nope.c") == QA_GOAL_ERROR_CANNOT_LOAD_ITEM_CONFIG);
  CHECK(qa_bot_goal_item_config(g) == NULL);
  CHECK_LOG(&fx->reports, "message:loaded items.c\n", "error:counldn't load nope.c\n", "fatal:couldn't load item config\n");
  log_clear(&fx->reports);
  CHECK(qa_bot_goal_setup(g, NULL) == QA_GOAL_ERROR_NONE);
  int64_t s = qa_bot_goal_alloc_goal_state(g, 1);
  qa_bot_goal goal = {v3(1, 2, 3), 4, v3(-1, -1, -1), v3(1, 1, 1), 5, 6, 1, 7};
  qa_bot_goal_push_goal(g, s, &goal);
  qa_bot_goal_push_goal_fn(g, s, push_thunk, &goal);
  CHECK(top_goal(g, s).number == 8);
  qa_bot_goal second;
  CHECK(qa_bot_goal_get_second_goal(g, s, &second) && second.number == 6);
  uint8_t *bytes = qa_bot_goal_get_top_goal_bytes(g, s);
  CHECK(bytes[44] == 8 && bytes[45] == 0);
  qa_bot_goal dumped[8];
  CHECK(qa_bot_goal_dump_goal_stack(g, s, dumped, 8) == 2 && dumped[0].number == 6 && dumped[1].number == 8);
  qa_bot_goal_pop_goal(g, s);
  CHECK(top_goal(g, s).number == 6);
  CHECK(!qa_bot_goal_get_second_goal(g, s, &second));
  qa_bot_goal_empty_goal_stack(g, s);
  for (int i = 0; i < 9; i++) qa_bot_goal_push_goal(g, s, &goal);
  CHECK_LOG(&fx->reports, "message:loaded items.c\n", "error:goal heap overflow\n", "error:goal heap overflow\n");
  CHECK_LOG(&fx->logs, "1: ", "2: ", "1: ", "2: ", "3: ", "4: ", "5: ", "6: ", "7: ", "1: ", "2: ", "3: ", "4: ", "5: ", "6: ", "7: ");
  log_clear(&fx->reports);
  log_clear(&fx->logs);
  qa_bot_goal_set_avoid_goal_time(g, s, 6, 2.5);
  CHECK(qa_bot_goal_avoid_goal_time(g, s, 6) == 2.5f);
  qa_avoid_goal_dump avoid[4];
  CHECK(qa_bot_goal_dump_avoid_goals(g, s, avoid, 4) == 1 && avoid[0].number == 6 && avoid[0].remaining == 2.5f);
  CHECK_LOG(&fx->logs, "avoid goal , number 6 for 2.500000 seconds");
  log_clear(&fx->logs);
  fx->now = 11;
  CHECK(qa_bot_goal_avoid_goal_time(g, s, 6) == 1.5f);
  qa_bot_goal_remove_from_avoid_goals(g, s, 6);
  CHECK(qa_bot_goal_avoid_goal_time(g, s, 6) == 0);
  qa_bot_goal_reset_goal_state(g, s);
  CHECK(!qa_bot_goal_get_top_goal(g, s, &second));
  CHECK(!qa_bot_goal_get_top_goal(g, 0, &second));
  CHECK(!qa_bot_goal_get_top_goal(g, 99, &second));
  qa_bot_goal_free_goal_state(g, 7);
  CHECK_LOG(&fx->reports, "fatal:goal state handle 0 out of range\n", "fatal:goal state handle 99 out of range\n", "fatal:invalid goal state handle 7\n");
  log_clear(&fx->reports);

  qa_aas_bsp_entities *bsp = malloc(sizeof *bsp);
  qa_aas_bsp_entities_init(bsp);
  qa_aas_bsp_entities_configure(bsp, NULL, NULL, memory);
  qa_aas_bsp_entities_load(bsp, "{ \"classname\" \"worldspawn\" } { \"classname\" \"target_location\" \"origin\" \"1 2 3\" \"message\" \"Lobby\" } { \"classname\" \"info_camp\" \"origin\" \"4 5 6\" \"message\" \"Camp\" \"range\" \"9\" \"weight\" \"2\" \"wait\" \"3\" \"random\" \"1\" } { \"classname\" \"info_camp\" \"origin\" \"7 8 9\" } { \"classname\" \"a\" } { \"classname\" \"nosuch\" \"origin\" \"0 0 0\" } { \"classname\" \"a\" \"origin\" \"10 0 0\" \"notfree\" \"1\" } { \"classname\" \"item_botroam\" \"origin\" \"20 0 0\" }");
  qa_goal_world world = fixture_world(fx, bsp, false);
  world.navigation.best_reachable_area = oracle_best;
  world.host.next_entity = no_entity;
  world.point_area = oracle_point_area;
  qa_bot_goal_init_level_items(g, &world);
  CHECK_LOG(&fx->reports, "message:camp spot at 7.0 8.0 9.0 in solid\n", "error:item a without origin\n",
            "message:a not reachable for bots at (10.0 0.0 0.0)\n", "message:found 1 level items\n");
  CHECK_LOG(&fx->logs, "item b has modelindex 0", "entity worldspawn unknown item\r\n", "entity target_location unknown item\r\n",
            "entity info_camp unknown item\r\n", "entity info_camp unknown item\r\n", "entity nosuch unknown item\r\n",
            "entity item_botroam unknown item\r\n");
  qa_bot_goal out;
  CHECK(qa_bot_goal_get_map_location_goal(g, qa_goal_name_text("LOBBY"), NULL, &out));
  CHECK(v3_eq(out.origin, 1, 2, 3) && out.area == 3 && v3_eq(out.mins, -8, -8, -8) && v3_eq(out.maxs, 8, 8, 8) &&
        out.entity == 0 && out.number == 0 && out.flags == 0 && out.item_info == 0);
  CHECK(!qa_bot_goal_get_map_location_goal(g, qa_goal_name_text("x"), NULL, &out));
  int64_t next = 0;
  CHECK(qa_bot_goal_get_next_camp_spot_goal(g, 0, NULL, &next, &out) && next == 1);
  CHECK(v3_eq(out.origin, 4, 5, 6) && out.area == 3);
  CHECK(!qa_bot_goal_get_next_camp_spot_goal(g, 1, NULL, &next, &out));
  CHECK(!qa_bot_goal_get_level_item_goal(g, -1, qa_goal_name_text("alpha"), NULL, &out));
  CHECK_STR(qa_bot_goal_goal_name(g, 1), "Alpha");
  CHECK_STR(qa_bot_goal_goal_name(g, 99), "");
  qa_bot_goal_shutdown(g);
  qa_weight_config_store_shutdown(store);
  log_clear(&fx->logs);
  log_clear(&fx->reports);
  qa_bot_goal_library_destroy(g);
  qa_weight_config_store_bots_a_destroy(store);
  qa_aas_bsp_entities_destroy(bsp);
  free(g);
  free(store);
  free(bsp);
  free(fx);
  fixture_close(&f);
}

/* TS RetiredGoalSetup: a report that runs shutdown inside setup unwinds it. */
typedef struct retire_fixture {
  const void *sentinel;
  qa_bot_goal_library *library;
  int calls;
} retire_fixture;
static void retire_report(void *ctx, const qa_goal_diagnostic *d) {
  retire_fixture *r = ctx;
  CHECK(r->sentinel == r);
  (void)d;
  r->calls++;
  if (r->calls == 1) qa_bot_goal_shutdown(r->library);
}
static double retire_clock(void *ctx) { (void)ctx; return 0; }
static double retire_type(void *ctx) { (void)ctx; return 0; }
static void test_goal_setup_retirement(void) {
  static const file_entry files[] = {{"items.c", "iteminfo \"a\" { name \"A\" }"}};
  fixture_sources f;
  fixture_open(&f, FILES(files));
  qa_weight_config_store_bots_a *store = malloc(sizeof *store);
  store_with_memory(store, &f, &f.sources.memory);
  retire_fixture r;
  memset(&r, 0, sizeof r);
  r.sentinel = &r;
  qa_bot_goal_library *l = malloc(sizeof *l);
  r.library = l;
  qa_bot_goal_options options;
  memset(&options, 0, sizeof options);
  options.memory = &f.sources.memory;
  options.resolver = &f.reader;
  options.weight_store = store;
  options.clock = retire_clock;
  options.game_type = retire_type;
  options.report = retire_report;
  options.report_ctx = &r;
  qa_bot_goal_library_init(l);
  qa_bot_goal_library_configure(l, &options);
  CHECK(qa_bot_goal_setup(l, NULL) == QA_GOAL_ERROR_CANNOT_LOAD_ITEM_CONFIG);
  CHECK(qa_bot_goal_item_config(l) == NULL);
  CHECK(r.calls == 1);
  qa_bot_goal_library_destroy(l);
  qa_weight_config_store_bots_a_destroy(store);
  free(l);
  free(store);
  fixture_close(&f);
}

/* ---- retail.test: real botfiles out of the product's pk3 archives ---- */
typedef struct retail_file {
  char *path;
  uint8_t *bytes;
  size_t length;
} retail_file;
typedef struct retail_files {
  const void *sentinel;
  retail_file *items;
  size_t count, capacity;
} retail_files;
static const uint8_t *retail_read(void *ctx, const char *path, size_t *out_len) {
  retail_files *files = ctx;
  CHECK(files->sentinel == files);
  /* Later archives override earlier ones (BotAssetFiles.add). */
  for (size_t i = files->count; i-- > 0;) {
    if (strcmp(files->items[i].path, path) == 0) {
      *out_len = files->items[i].length;
      return files->items[i].bytes;
    }
  }
  return NULL;
}
static int compare_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void retail_add_directory(retail_files *files, const char *root, const char *directory) {
  char pattern[1024];
  snprintf(pattern, sizeof pattern, "%s/%s", root, directory);
  char *names[64];
  size_t name_count = 0;
  for (int n = 0; n < 16; n++) {
    char path[1100];
    snprintf(path, sizeof path, "%s/pak%d.pk3", pattern, n);
    if (access(path, R_OK) == 0 && name_count < 64) names[name_count++] = strdup(path);
  }
  qsort(names, name_count, sizeof names[0], compare_names);
  for (size_t a = 0; a < name_count; a++) {
    qa_archive_handle *archive = qa_open_archive(names[a], false, QA_ARCHIVE_FORMAT_PK3);
    for (size_t e = 0; archive != NULL && e < archive->entry_count; e++) {
      const qa_archive_entry *entry = &archive->entries[e];
      if (entry->is_directory || strncmp(entry->path, "botfiles/", 9) != 0) continue;
      size_t length = 0;
      uint8_t *bytes = qa_archive_read_entry_entry(archive, entry, &length);
      if (files->count == files->capacity) {
        files->capacity = files->capacity == 0 ? 256 : files->capacity * 2;
        files->items = realloc(files->items, files->capacity * sizeof *files->items);
      }
      files->items[files->count++] = (retail_file){strdup(entry->path), bytes, length};
    }
    if (archive != NULL) qa_archive_destroy(archive);
    free(names[a]);
  }
}
static void retail_sources(fixture_sources *f, retail_files *files) {
  memset(f, 0, sizeof *f);
  f->sentinel = f;
  f->globals = qa_ui_script_global_defines_create(NULL, NULL, NULL);
  qa_bot_script_sources_init(&f->sources);
  f->sources.globals = f->globals;
  f->sources.read_file = retail_read;
  f->sources.read_ctx = files;
  f->sources.print = fixture_print;
  f->sources.print_ctx = f;
  f->sources.common_print = fixture_common;
  f->sources.common_print_ctx = f;
  f->reader = qa_bot_script_sources_reader(&f->sources);
}
static void retail_sarge(retail_files *files) {
  fixture_sources f;
  retail_sources(&f, files);
  qa_bot_character_options options;
  memset(&options, 0, sizeof options);
  options.memory = &f.sources.memory;
  qa_bot_character_library *characters = malloc(sizeof *characters);
  qa_bot_character_library_init(characters);
  qa_bot_character_library_configure(characters, &f.reader, &options);
  int64_t easy = qa_bot_character_load(characters, "bots/sarge_c.c", 1);
  int64_t medium = qa_bot_character_load(characters, "bots/sarge_c.c", 2.5);
  int64_t hard = qa_bot_character_load(characters, "bots/sarge_c.c", 4);
  CHECK(easy > 0 && medium > 0 && hard > 0);
  CHECK_STR(qa_bot_character_string(characters, medium, QA_CHAR_NAME), "Sarge");
  CHECK(qa_bot_character_float(characters, easy, QA_CHAR_ATTACK_SKILL) == 0.5f);
  CHECK(qa_bot_character_float(characters, hard, QA_CHAR_ATTACK_SKILL) == 0.75f);
  CHECK(qa_bot_character_float(characters, medium, QA_CHAR_ATTACK_SKILL) == 0.625f);
  CHECK(qa_bot_character_float(characters, medium, QA_CHAR_REACTIONTIME) == 1.5f);
  CHECK(qa_bot_character_integer(characters, medium, QA_CHAR_VIEW_MAXCHANGE) == 180);
  CHECK_STR(qa_bot_character_string(characters, medium, QA_CHAR_ITEMWEIGHTS), "bots/sarge_i.c");
  qa_bot_character_shutdown(characters);
  CHECK(qa_bot_memory_get_live_allocations(&f.sources.memory) == 0);
  qa_bot_character_library_destroy(characters);
  free(characters);
  qa_bot_script_sources_destroy(&f.sources);
  qa_ui_script_global_defines_destroy(f.globals);
}
static void retail_weapons(retail_files *files, bool missionpack) {
  fixture_sources f;
  retail_sources(&f, files);
  qa_weight_config_store_bots_a *weights = malloc(sizeof *weights);
  store_with_memory(weights, &f, &f.sources.memory);
  qa_weapon_ai_options options;
  memset(&options, 0, sizeof options);
  options.memory = &f.sources.memory;
  qa_weapon_ai_bots_a *weapons = malloc(sizeof *weapons);
  qa_weapon_ai_bots_a_init(weapons);
  qa_weapon_ai_bots_a_configure(weapons, &f.reader, weights, &options);
  CHECK(qa_weapon_ai_setup(weapons, NULL) == QA_WEAPON_LOAD_NO_ERROR);
  qa_weapon_config_bots_a *config = qa_weapon_ai_config(weapons);
  CHECK(qa_weapon_config_defined_count(config) == 12);
  CHECK(qa_weapon_config_projectile_count(config) == 12);
  qa_weapon_info_bots_a info;
  CHECK(qa_weapon_config_weapon(config, 1, &info) && info.projectile_info.damage == 50);
  CHECK(qa_weapon_config_weapon(config, 5, &info));
  CHECK_STR(info.name, "Rocket Launcher");
  CHECK(info.speed == 900 && info.projectile_info.damage == 100 && info.projectile_info.radius == 120 &&
        info.projectile_info.damage_type == 3);
  if (missionpack) {
    CHECK(qa_weapon_config_weapon(config, 13, &info));
    CHECK_STR(info.name, "Chaingun");
  }
  int64_t state = qa_weapon_ai_allocate_state(weapons);
  CHECK(qa_weapon_ai_load_weights(weapons, state, "bots/anarki_w.c") == QA_WEAPON_LOAD_NO_ERROR);
  double inventory[256];
  for (int i = 0; i < 256; i++) inventory[i] = 1;
  CHECK(qa_weapon_ai_choose_best_fight_weapon(weapons, state, inv(inventory, 256)) == 5);
  inventory[8] = 0;
  CHECK(qa_weapon_ai_choose_best_fight_weapon(weapons, state, inv(inventory, 256)) == 7);
  /* q3-runtime-warnings: the arsenal refresh path reads weapons without warnings; WP_NONE warns. */
  size_t diagnostics_before = 0;
  (void)qa_weapon_ai_diagnostics(weapons, &diagnostics_before);
  for (int refresh = 0; refresh < 4; refresh++) {
    CHECK(qa_weapon_ai_get_weapon_info(weapons, state, 1, &info));
    CHECK_STR(info.name, "Gauntlet");
    CHECK(qa_weapon_ai_get_weapon_info(weapons, state, 5, &info));
    CHECK_STR(info.name, "Rocket Launcher");
  }
  size_t diagnostics_after = 0;
  const qa_weapon_diagnostic *diagnostics = qa_weapon_ai_diagnostics(weapons, &diagnostics_after);
  CHECK(diagnostics_after == diagnostics_before);
  CHECK(!qa_weapon_ai_get_weapon_info(weapons, state, 0, &info));
  diagnostics = qa_weapon_ai_diagnostics(weapons, &diagnostics_after);
  CHECK(diagnostics_after == diagnostics_before + 1);
  CHECK_STR(diagnostics[diagnostics_after - 1].message, "weapon number out of range");
  qa_item_config_options item_options;
  memset(&item_options, 0, sizeof item_options);
  item_options.memory = &f.sources.memory;
  qa_item_config_bots_a *items = qa_load_item_config(&f.reader, "items.c", &item_options, NULL);
  size_t count = qa_item_config_count(items);
  bool saw_health = false;
  for (size_t i = 0; i < count; i++) {
    qa_item_info_bots_a item;
    qa_item_config_item(items, i, &item);
    if (strcmp(item.classname, "item_health") != 0) continue;
    saw_health = true;
    CHECK_STR(item.name, "25 Health");
    CHECK(item.respawn_time == 30);
    break;
  }
  CHECK(saw_health);
  qa_weight_config_bots_a *item_weights = qa_weight_config_store_load(weights, "bots/sarge_i.c", NULL);
  CHECK(qa_weight_config_find(item_weights, "item_health") >= 0);
  CHECK(qa_weight_config_weight_count(item_weights) > 20);
  qa_item_config_free(items);
  qa_item_config_destroy(items);
  qa_weapon_ai_shutdown(weapons);
  qa_weight_config_store_shutdown(weights);
  qa_bot_memory_reset_hunk(&f.sources.memory);
  CHECK(qa_bot_memory_get_live_allocations(&f.sources.memory) == 0);
  qa_weapon_ai_bots_a_destroy(weapons);
  qa_weight_config_store_bots_a_destroy(weights);
  free(weapons);
  free(weights);
  qa_bot_script_sources_destroy(&f.sources);
  qa_ui_script_global_defines_destroy(f.globals);
}
static void retail_files_destroy(retail_files *files) {
  for (size_t i = 0; i < files->count; i++) {
    free(files->items[i].path);
    free(files->items[i].bytes);
  }
  free(files->items);
}
static bool test_retail(void) {
  const char *root = getenv("QA_BOT_RETAIL_ROOT");
  char fallback[1024];
  if (root == NULL) {
    const char *home = getenv("HOME");
    snprintf(fallback, sizeof fallback, "%s/qfiles/Quake 3 Arena", home != NULL ? home : "");
    root = fallback;
  }
  char probe_base[1100], probe_mission[1100];
  snprintf(probe_base, sizeof probe_base, "%s/baseq3/pak0.pk3", root);
  snprintf(probe_mission, sizeof probe_mission, "%s/missionpack/pak0.pk3", root);
  if (access(probe_base, R_OK) != 0 || access(probe_mission, R_OK) != 0) return false;
  for (int product = 0; product < 2; product++) {
    retail_files files;
    memset(&files, 0, sizeof files);
    files.sentinel = &files;
    retail_add_directory(&files, root, "baseq3");
    if (product == 1) retail_add_directory(&files, root, "missionpack");
    retail_sarge(&files);
    retail_weapons(&files, product == 1);
    retail_files_destroy(&files);
  }
  return true;
}

int main(void) {
  test_fuzzy_weights();
  test_weights_oracle();
  test_fuzzy_checkpoint();
  test_libvars_oracle();
  test_libvars_checkpoint();
  test_log_variable_gate();
  test_structure_oracle();
  test_bsp_oracle();
  test_character_oracle();
  test_weapons_oracle();
  test_item_goals();
  test_round_restart();
  test_goals_oracle();
  test_goal_setup_retirement();
  bool retail = test_retail();
  printf("test_library_core: %d checks, %d failures%s\n", checks, failures, retail ? "" : " (retail corpus absent: retail cases skipped)");
  return failures == 0 ? 0 : 1;
}
