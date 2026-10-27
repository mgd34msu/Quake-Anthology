/*
 * Port of src/compat/qvm/script-record.ts. Mirrored types (TS owners
 * src/ui/common/legacy/script/lexer.ts and token-memory.ts, not yet
 * landed): qa_qvmy_script_token_record mirrors ScriptTokenRecord
 * flattened with its token kind (exact TS strings) and text;
 * qa_qvmy_source_token_memory mirrors the SourceTokenMemory surface
 * this module reads (string/type/subtype/integerValue/floatValue).
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_SCRIPT_RECORD_H
#define QA_COMPAT_QVM_SCRIPT_RECORD_H

#include <stdint.h>

#include "qa/compat/qvm/memory_writes.h"

#define QA_QVM_SCRIPT_TOKEN_BYTES 1040

typedef struct qa_qvmy_script_token_record {
  const char *token_kind; /* primitive|string|literal|number|name|punctuation */
  const char *token_text;
  double subtype;
  double integer_value;
  double float_value;
} qa_qvmy_script_token_record;

typedef struct qa_qvmy_source_token_memory {
  const char *string;
  int32_t type;
  double subtype;
  double integer_value;
  double float_value;
} qa_qvmy_source_token_memory;

void qa_qvm_write_script_token(qa_qvm_writable_view *view, const qa_qvmy_script_token_record *value);
void qa_qvm_write_script_token_memory(qa_qvm_writable_view *view, const qa_qvmy_source_token_memory *value);

#endif
