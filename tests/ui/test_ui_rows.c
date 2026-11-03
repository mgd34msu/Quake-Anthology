/* Rows read against src/ui/common/legacy/script/preprocessor.ts:
 * ExpressionParser's debugEval trace for '?' and for failed reductions.
 * GPL-2.0-or-later. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/ui/common/legacy/script/preprocessor.h"

static int failures;
#define CHECK(c)                                             \
  do {                                                       \
    if (!(c)) {                                              \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
      failures++;                                            \
    }                                                        \
  } while (0)

typedef struct trace {
  unsigned sentinel;
  char text[4096];
} trace;
#define SENTINEL 0x51ea7u

static void trace_debug(void *ctx, const char *line) {
  trace *t = ctx;
  CHECK(t->sentinel == SENTINEL);
  strncat(t->text, line, sizeof t->text - strlen(t->text) - 2);
  strcat(t->text, "\n");
}
static void trace_report(void *ctx, const qa_ui_script_diagnostic *d) {
  trace *t = ctx;
  CHECK(t->sentinel == SENTINEL);
  strncat(t->text, "report: ", sizeof t->text - strlen(t->text) - 1);
  strncat(t->text, d->message, sizeof t->text - strlen(t->text) - 2);
}
static bool no_include(void *ctx, const qa_ui_script_include_request *request,
                       qa_ui_script_source *out) {
  (void)ctx; (void)request; (void)out;
  return false;
}

typedef struct read_all {
  qa_ui_script_source_reader *reader;
} read_all;
static void read_all_run(void *ctx) {
  read_all *r = ctx;
  qa_ui_script_token_record record;
  while (qa_ui_script_source_reader_next(r->reader, &record))
    qa_ui_script_token_record_release(&record);
}

static void run_script(const char *text, trace *t, bool *completed) {
  memset(t, 0, sizeof *t);
  t->sentinel = SENTINEL;
  qa_ui_script_source root;
  memset(&root, 0, sizeof root);
  root.path = "rows.menu";
  root.text = (const uint8_t *)text;
  root.text_length = strlen(text);
  qa_ui_script_preprocessor_options options;
  memset(&options, 0, sizeof options);
  options.debug_eval = trace_debug;
  options.debug_eval_ctx = t;
  options.report = trace_report;
  options.report_ctx = t;
  qa_ui_script_source_reader *reader =
      qa_ui_script_source_reader_open(&root, no_include, NULL, &options);
  CHECK(reader != NULL);
  read_all run = {reader};
  *completed = qa_ui_script_source_reader_catch(reader, read_all_run, &run);
  qa_ui_script_source_reader_dispose(reader);
  qa_ui_script_source_reader_destroy(reader);
}

int main(void) {
  trace t;
  bool completed;

  /* '?' unlinks its own value node; TS still logs value.value. */
  run_script("#if 1 ? 2 : 3\n7\n#endif\n", &t, &completed);
  CHECK(completed);
  CHECK(strstr(t.text,
               "operator ?, value1 = 1\nvalue2 = 2\nresult value = 1\n"
               "operator :, value1 = 2\nvalue2 = 3\nresult value = 2\n") !=
        NULL);

  /* A failed reduction reports, then logs the unreduced value. */
  run_script("#if 1 / 0\n7\n#endif\n", &t, &completed);
  CHECK(!completed);
  CHECK(strstr(t.text,
               "operator /, value1 = 1\nvalue2 = 0\n"
               "report: divide by zero in #if/#elif\nresult value = 1\n") !=
        NULL);

  /* Failures outside a reduction add no result line. */
  run_script("#if 1 +\n7\n#endif\n", &t, &completed);
  CHECK(!completed);
  CHECK(strstr(t.text, "trailing operator") != NULL);
  CHECK(strstr(t.text, "result value") == NULL);

  if (failures != 0) {
    fprintf(stderr, "test_ui_rows: %d failure(s)\n%s", failures, t.text);
    return 1;
  }
  puts("test_ui_rows: ok");
  return 0;
}
