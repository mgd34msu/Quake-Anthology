/*
 * Port of src/console/log.ts (ConsoleLog): appends console text to
 * a file, creating parent directories, mode 0600. C accommodation:
 * node:fs becomes POSIX open/write (PORTING.md: platform libraries
 * linked directly). The TS plain-Error throws (closed log, write
 * making no progress, fs failures) abort with the same messages,
 * as the throws reach the TS top-level crash handler.
 * Ownership: the log owns a copy of its path and its fd; destroy
 * closes. init returns false only on OOM. GPL-2.0-or-later.
 */
#ifndef QA_CONSOLE_LOG_H
#define QA_CONSOLE_LOG_H

#include <stdbool.h>

typedef struct qa_console_log {
  char *path; /* owned copy */
  int fd;     /* -1 when closed (TS null) */
} qa_console_log;

/* False only on OOM; mkdir/open failures abort as the TS throws. */
bool qa_console_log_init(qa_console_log *log, const char *path, bool append);
void qa_console_log_destroy(qa_console_log *log);
void qa_console_log_write(qa_console_log *log, const char *text);
void qa_console_log_close(qa_console_log *log);

#endif
