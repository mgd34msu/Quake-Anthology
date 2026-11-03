/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/log.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static qa_bot_log_io_result ok(void) { return (qa_bot_log_io_result){QA_LOG_IO_OK}; }
static void live(qa_bot_log *l) {
  if (!l->is_open)
    qa_range_error("Bot log source FILE is closed but its pointer is retained");
}
static int32_t src_int(double v) {
  double t = trunc(v);
  if (!(t >= -2147483648.0 && t <= 2147483647.0))
    qa_range_error("Log_WriteTimeStamped: source float-to-int conversion is undefined");
  return (int32_t)t;
}
static int32_t checked(int64_t v) {
  if (v < INT32_MIN || v > INT32_MAX)
    qa_range_error("Log_WriteTimeStamped: source signed arithmetic would overflow");
  return (int32_t)v;
}
static void two(char *b, size_t n, int32_t v) {
  if (v < 0)
    snprintf(b, n, "-%lld", (long long)(-(int64_t)v));
  else
    snprintf(b, n, "%02lld", (long long)v);
}
void qa_bot_log_init(qa_bot_log *l, qa_bot_lib_vars *vars) {
  memset(l, 0, sizeof *l);
  l->variables = vars;
}
void qa_bot_log_destroy(qa_bot_log *l) {
  free(l->opened_filename);
  l->opened_filename = NULL;
}
static const char *stored(qa_bot_log *l) {
  if (strlen(l->filename) == 1024)
    qa_range_error("Log_Open: source stored filename is not NUL-terminated");
  return l->filename;
}
qa_bot_log_io_result qa_bot_log_open(qa_bot_log *l, const char *filename) {
  if (l->variables && qa_bot_lib_vars_value(l->variables, "log", "0") == 0.0f)
    return ok();
  const char *name = filename ? filename : "";
  if (name[0] == '\0') {
    if (l->print)
      l->print(l->print_ctx, 1, "openlog <filename>\n");
    return ok();
  }
  if (l->is_open || l->retained_closed) {
    char b[1200];
    snprintf(b, sizeof b, "log file %s is already opened\n", stored(l));
    if (l->print)
      l->print(l->print_ctx, 3, b);
    return ok();
  }
  qa_bot_log_open_result r = l->open_file(l->open_ctx, name);
  if (!r.opened) {
    char b[1200];
    snprintf(b, sizeof b, "can't open the log file %s\n", name);
    if (l->print)
      l->print(l->print_ctx, 3, b);
    return (qa_bot_log_io_result){QA_LOG_IO_FAILED};
  }
  l->stream = r.stream;
  l->is_open = true;
  snprintf(l->filename, 1025, "%s", name);
  free(l->opened_filename);
  size_t n = strlen(name) + 1;
  l->opened_filename = malloc(n);
  if (l->opened_filename)
    memcpy(l->opened_filename, name, n);
  char b[1200];
  snprintf(b, sizeof b, "Opened log %s\n", stored(l));
  if (l->print)
    l->print(l->print_ctx, 1, b);
  return ok();
}
qa_bot_log_io_result qa_bot_log_close(qa_bot_log *l) {
  if (!l->is_open && !l->retained_closed)
    return ok();
  live(l);
  qa_bot_log_io_result r = l->stream.close(l->stream.ctx);
  l->is_open = false;
  if (r.kind == QA_LOG_IO_OK)
    l->retained_closed = false;
  else
    l->retained_closed = true;
  char b[1200];
  if (r.kind == QA_LOG_IO_FAILED)
    snprintf(b, sizeof b, "can't close log file %s\n", stored(l));
  else
    snprintf(b, sizeof b, "Closed log %s\n", stored(l));
  if (l->print)
    l->print(l->print_ctx, r.kind == QA_LOG_IO_FAILED ? 3 : 1, b);
  return r;
}
qa_bot_log_io_result qa_bot_log_shutdown(qa_bot_log *l) { return qa_bot_log_close(l); }
qa_bot_log_io_result qa_bot_log_dispose_resources(qa_bot_log *l) {
  if (!l->is_open) {
    l->retained_closed = false;
    return ok();
  }
  l->is_open = false;
  l->retained_closed = false;
  return l->stream.close(l->stream.ctx);
}
void qa_bot_log_write(qa_bot_log *l, const char *f) {
  if (!l->is_open && !l->retained_closed)
    return;
  live(l);
  l->stream.write(l->stream.ctx, (const uint8_t *)f, strlen(f));
  l->stream.flush(l->stream.ctx);
}
void qa_bot_log_write_time_stamped(qa_bot_log *l, const char *f) {
  if (!l->is_open && !l->retained_closed)
    return;
  live(l);
  float time = (float)l->time;
  int32_t hours = src_int((double)(float)((double)(float)((double)time / 60.0) / 60.0));
  int32_t minutes = src_int((double)(float)((double)time / 60.0));
  int32_t seconds = src_int(time);
  int32_t centi = src_int((double)(float)((double)time * 100.0));
  int32_t fraction = checked((int64_t)centi - checked((int64_t)seconds * 100));
  char h[32], mi[32], se[32], fr[32], prefix[256];
  two(h, sizeof h, hours);
  two(mi, sizeof mi, minutes);
  two(se, sizeof se, seconds);
  two(fr, sizeof fr, fraction);
  snprintf(prefix, sizeof prefix, "%d   %s:%s:%s:%s   ", l->numwrites, h, mi, se, fr);
  l->stream.write(l->stream.ctx, (const uint8_t *)prefix, strlen(prefix));
  l->stream.write(l->stream.ctx, (const uint8_t *)f, strlen(f));
  const uint8_t crlf[2] = {13, 10};
  l->stream.write(l->stream.ctx, crlf, 2);
  if (l->numwrites == INT32_MAX)
    qa_range_error("Log_WriteTimeStamped: source numwrites increment would overflow");
  l->numwrites++;
  l->stream.flush(l->stream.ctx);
}
void qa_bot_log_flush(qa_bot_log *l) {
  if (l->is_open || l->retained_closed) {
    live(l);
    l->stream.flush(l->stream.ctx);
  }
}
bool qa_bot_log_has_file(const qa_bot_log *l) { return l->is_open || l->retained_closed; }
int64_t qa_bot_log_file_write(qa_bot_log *l, const char *f) {
  live(l);
  qa_bot_log_io_result r = l->stream.write(l->stream.ctx, (const uint8_t *)f, strlen(f));
  return r.kind == QA_LOG_IO_OK ? (int64_t)strlen(f) : -1;
}
qa_save_value *qa_bot_log_capture_save_state(qa_bot_log *l) {
  qa_save_value *state = qa_save_value_object(), *out = qa_save_value_object();
  if (state == NULL || out == NULL)
    qa_range_error("Out of memory capturing the bot log");
  if (!l->is_open && !l->retained_closed)
    qa_save_value_set(state, "kind", qa_save_value_string("closed"));
  else if (!l->is_open)
    qa_save_value_set(state, "kind", qa_save_value_string("closed-retained"));
  else {
    if (!l->stream.has_checkpoint || l->stream.checkpoint_position == NULL) {
      fprintf(stderr, "Active bot log stream does not support continuation checkpoints\n");
      abort();
    }
    int64_t position = l->stream.checkpoint_position(l->stream.ctx);
    if (position < 0 || position > INT64_C(9007199254740991)) {
      fprintf(stderr, "Bot log stream returned an invalid byte position\n");
      abort();
    }
    qa_save_value_set(state, "kind", qa_save_value_string("open"));
    qa_save_value_set(state, "position", qa_save_value_number((double)position));
  }
  qa_save_value_set(out, "filename", qa_save_value_string(l->filename));
  qa_save_value_set(out, "openedFilename",
                    qa_save_value_string(l->opened_filename != NULL ? l->opened_filename : ""));
  qa_save_value_set(out, "numwrites", qa_save_value_number((double)l->numwrites));
  qa_save_value_set(out, "state", state);
  return out;
}
static void log_field(const qa_save_reader *r, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(r, name, out))
    qa_range_error("Out of memory reading the bot log");
}
void qa_bot_log_restore_save_state(qa_bot_log *l, const qa_save_value *value) {
  if (l->is_open || l->retained_closed) {
    fprintf(stderr, "Log restore requires an empty owner\n");
    abort();
  }
  static const char *const KINDS[] = {"closed", "closed-retained", "open"};
  qa_save_reader reader, state, cell;
  if (!qa_save_reader_init(&reader, value, "bot.log"))
    qa_range_error("Out of memory reading the bot log");
  log_field(&reader, "state", &state);
  log_field(&state, "kind", &cell);
  const char *kind = qa_save_reader_choice_string(&cell, KINDS, 3);
  bool retained = strcmp(kind, "closed-retained") == 0, open = strcmp(kind, "open") == 0;
  qa_save_reader_destroy(&cell);
  qa_save_reader filename_reader, opened_reader;
  log_field(&reader, "filename", &filename_reader);
  log_field(&reader, "openedFilename", &opened_reader);
  const char *filename = qa_save_reader_string(&filename_reader);
  const char *opened = qa_save_reader_string(&opened_reader);
  log_field(&reader, "numwrites", &cell);
  double numwrites = qa_save_reader_integer_min(&cell, 0);
  qa_save_reader_destroy(&cell);
  if (strlen(filename) > 1024 || numwrites > 2147483647.0)
    qa_save_reader_fail(&reader, "invalid log state");
  bool active = false;
  qa_bot_log_stream_bots_a stream;
  memset(&stream, 0, sizeof stream);
  if (open) {
    if (opened[0] == '\0' || strlen(filename) != (strlen(opened) < 1024 ? strlen(opened) : 1024) ||
        strncmp(filename, opened, 1024) != 0)
      qa_save_reader_fail(&reader, "invalid active log filename");
    if (l->resume_file == NULL)
      qa_save_reader_fail(&reader, "active log continuation requires a resumable file host");
    log_field(&state, "position", &cell);
    double position = qa_save_reader_integer_min(&cell, 0);
    qa_save_reader_destroy(&cell);
    qa_bot_log_open_result result = l->resume_file(l->resume_ctx, opened, (int64_t)position);
    if (!result.opened) {
      fprintf(stderr, "Bot log resume failed for %s\n", opened);
      abort();
    }
    stream = result.stream;
    active = true;
  }
  l->is_open = active;
  l->retained_closed = retained;
  l->stream = stream;
  snprintf(l->filename, sizeof l->filename, "%s", filename);
  free(l->opened_filename);
  size_t n = strlen(opened) + 1;
  l->opened_filename = malloc(n);
  if (l->opened_filename == NULL)
    qa_range_error("Out of memory restoring the bot log");
  memcpy(l->opened_filename, opened, n);
  l->numwrites = (int32_t)numwrites;
  qa_save_reader_destroy(&filename_reader);
  qa_save_reader_destroy(&opened_reader);
  qa_save_reader_destroy(&state);
  qa_save_reader_destroy(&reader);
}
