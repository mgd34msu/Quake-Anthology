#ifndef QA_SCRIPT_DEFINES_SAVE_H
#define QA_SCRIPT_DEFINES_SAVE_H
#include "qa/script.h"

/* Pure continuation of the actual retained global macro owner. Restore makes
 * a fresh owner without lexing, defining, opening resources or callbacks. */
bool qa_script_defines_save_capture(const qa_script_defines *,qa_buffer *,qa_error *);
bool qa_script_defines_save_restore(qa_bytes,qa_script_defines **,qa_error *);
#endif
