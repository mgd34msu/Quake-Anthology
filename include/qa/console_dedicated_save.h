#ifndef QA_CONSOLE_DEDICATED_SAVE_H
#define QA_CONSOLE_DEDICATED_SAVE_H
#include "qa/console_io.h"
/* Preserves raw split UTF-8 and EOF without repairing, draining, reading the
 * descriptor, or executing commands. Import requires a genuine empty owner. */
bool qa_dedicated_console_checkpoint(const qa_dedicated_console *, qa_buffer *, qa_error *);
bool qa_dedicated_console_restore(qa_dedicated_console *, qa_bytes, qa_error *);
#endif
