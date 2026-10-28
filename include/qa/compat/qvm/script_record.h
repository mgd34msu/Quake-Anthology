/*
 * Port of src/compat/qvm/script-record.ts. The TS single
 * writeQvmScriptToken(view, ScriptTokenRecord | SourceTokenMemory)
 * becomes two entry points over the landed engine types (TS owners
 * src/ui/common/legacy/script/lexer.ts ScriptTokenRecord and
 * token-memory.ts SourceTokenMemory). QA_QVM_SCRIPT_TOKEN_BYTES is
 * this module's own pc_token_t guest layout (1040), not an engine
 * type. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_SCRIPT_RECORD_H
#define QA_COMPAT_QVM_SCRIPT_RECORD_H

#include "qa/compat/qvm/memory_writes.h"
#include "qa/ui/common/legacy/script/lexer.h"
#include "qa/ui/common/legacy/script/token_memory.h"

#define QA_QVM_SCRIPT_TOKEN_BYTES 1040

void qa_qvm_write_script_token(qa_qvm_writable_view *view,
                               const qa_ui_script_token_record *value);
void qa_qvm_write_script_token_memory(qa_qvm_writable_view *view,
                                      qa_ui_source_token_memory *value);

#endif
