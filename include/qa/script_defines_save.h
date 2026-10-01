#ifndef QA_SCRIPT_DEFINES_SAVE_H
#define QA_SCRIPT_DEFINES_SAVE_H
#include "qa/script.h"

/* Pure continuation of the actual retained global macro owner. Restore makes
 * a fresh owner without lexing, defining, opening resources or callbacks. */
bool qa_script_defines_save_capture(const qa_script_defines *,qa_buffer *,qa_error *);
bool qa_script_defines_save_restore(qa_bytes,qa_script_defines **,qa_error *);
/* Replace active definitions after isolated decoding while preserving this
 * retained owner's identity and any existing source token aliases. */
bool qa_script_defines_save_restore_into(qa_script_defines *,qa_bytes,qa_error *);
#endif
