#ifndef QA_CONSOLE_IO_H
#define QA_CONSOLE_IO_H
#include "qa/console.h"
typedef struct qa_dedicated_console qa_dedicated_console;
qa_dedicated_console *qa_dedicated_console_create(qa_error *);
void qa_dedicated_console_destroy(qa_dedicated_console *);
/* Raw UTF-8 may be split anywhere. Complete lines are decoded at drain time. */
bool qa_dedicated_console_feed(qa_dedicated_console *, qa_bytes, bool eof, qa_error *);
bool qa_dedicated_console_drain(qa_dedicated_console *, qa_console *, const qa_command_context *,
                                size_t *lines, qa_error *);
bool qa_dedicated_console_eof(const qa_dedicated_console *);
bool qa_dedicated_console_ended(const qa_dedicated_console *);
/* The caller owns the descriptor and is its sole reader. Polling never changes
 * its flags or closes it; budget zero selects 64 KiB per owner turn. */
bool qa_dedicated_console_poll(qa_dedicated_console *, int descriptor, size_t byte_budget,
                               qa_error *);
#endif
