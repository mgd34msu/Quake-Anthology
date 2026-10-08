#ifndef QA_CONSOLE_IO_H
#define QA_CONSOLE_IO_H
#include "qa/platform_events.h"
typedef struct qa_dedicated_console qa_dedicated_console;
qa_dedicated_console *qa_dedicated_console_create(qa_error *);
void qa_dedicated_console_destroy(qa_dedicated_console *);
/* Raw UTF-8 may be split anywhere. Complete lines are decoded when read. */
bool qa_dedicated_console_feed(qa_dedicated_console *, qa_bytes, bool eof, qa_error *);
/* A present line includes its trailing newline and NUL. Bytes are borrowed
 * until the next line_next, feed or destroy. An incomplete line stays pending;
 * EOF makes the final unterminated line available. */
bool qa_dedicated_console_line_next(qa_dedicated_console *, qa_bytes *, bool *present, qa_error *);
bool qa_dedicated_console_eof(const qa_dedicated_console *);
bool qa_dedicated_console_ended(const qa_dedicated_console *);
/* Publishes completed lines to the common queue, then reads the owned descriptor
 * without blocking. The caller is its sole reader. Flags and lifetime stay with
 * the caller; budget zero selects 64 KiB per turn. Restored pending lines and
 * the final line at EOF are published too. */
bool qa_platform_console_pump(qa_dedicated_console *, qa_platform_events *, int descriptor,
    size_t byte_budget, uint64_t time_ns, qa_error *);
#endif
