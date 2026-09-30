#ifndef QA_BOT_LIBRARY_SAVE_H
#define QA_BOT_LIBRARY_SAVE_H

#include "qa/bot_library.h"

/* The detached library must have no borrowers of its variable records. Restore
 * replaces only this table after complete validation; options and asset caches
 * remain owned by the library. No source callbacks or numeric parsing run. */
bool qa_bot_library_variables_capture(const qa_bot_library *, qa_buffer *, qa_error *);
bool qa_bot_library_variables_restore(qa_bot_library *, qa_bytes, qa_error *);

#endif
