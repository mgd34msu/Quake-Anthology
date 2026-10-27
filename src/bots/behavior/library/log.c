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
