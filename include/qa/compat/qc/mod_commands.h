/* Port of src/compat/qc/mod-commands.ts. GPL-2.0-or-later.
 * Result arrays are malloc'd; free with qa_compat_qc_console_call_destroy. String values borrow argv/args_text storage. */
#ifndef QA_COMPAT_QC_MOD_COMMANDS_H
#define QA_COMPAT_QC_MOD_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/mod_callbacks.h"
bool qa_compat_qc_console_call(qa_mod_console_command command, const char *const *argv, size_t argc, const char *args_text, qa_mod_source_call *out, qa_mod_callback_value **out_args, qa_mod_named_value **out_globals);
void qa_compat_qc_console_call_destroy(qa_mod_callback_value *args, qa_mod_named_value *globals);
#endif
