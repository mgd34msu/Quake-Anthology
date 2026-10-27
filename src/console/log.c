/* Port of src/console/log.ts. GPL-2.0-or-later. */
#include "qa/console/log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* TS plain `throw new Error(msg)`: same message, then abort (the TS
 * throw reaches the top-level crash handler). */
static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

/* node mkdirSync(dirname(path), { recursive: true }). */
static void make_parent_directories(const char *path) {
  const char *slash = strrchr(path, '/');
  if (slash == NULL || slash == path) return; /* "." or "/" parents */
  size_t length = (size_t)(slash - path);
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) return; /* open below reports the failure path */
  memcpy(copy, path, length);
  copy[length] = '\0';
  for (size_t i = 1; i <= length; i++) {
    if (copy[i] == '/' || copy[i] == '\0') {
      copy[i] = '\0';
      if (mkdir(copy, 0777) != 0 && errno != EEXIST)
        qa_error("%s", strerror(errno));
      copy[i] = '/';
    }
  }
  free(copy);
}

bool qa_console_log_init(qa_console_log *log, const char *path, bool append) {
  log->path = NULL;
  log->fd = -1;
  size_t length = strlen(path);
  log->path = (char *)malloc(length + 1);
  if (log->path == NULL) return false;
  memcpy(log->path, path, length + 1);
  make_parent_directories(path);
  int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
  log->fd = open(path, flags, 0600);
  if (log->fd < 0) qa_error("%s", strerror(errno));
  return true;
}

void qa_console_log_destroy(qa_console_log *log) {
  qa_console_log_close(log);
  free(log->path);
  log->path = NULL;
}

void qa_console_log_write(qa_console_log *log, const char *text) {
  if (log->fd < 0) qa_error("Console log is closed");
  size_t length = strlen(text);
  size_t offset = 0;
  while (offset < length) {
    ssize_t written = write(log->fd, text + offset, length - offset);
    if (written < 0) qa_error("%s", strerror(errno));
    if (written == 0) qa_error("Console log write made no progress");
    offset += (size_t)written;
  }
}

void qa_console_log_close(qa_console_log *log) {
  if (log->fd >= 0) {
    int fd = log->fd;
    log->fd = -1;
    close(fd);
  }
}
