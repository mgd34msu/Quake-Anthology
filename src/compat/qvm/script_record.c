/* See include/qa/compat/qvm/script_record.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/script_record.h"

#include <math.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

static void write_token(qa_qvm_writable_view *view, const char *text, int32_t type,
                        double subtype, double integer_value, double float_value) {
  if (view->length < QA_QVM_SCRIPT_TOKEN_BYTES)
    qa_binary_error("QVM pc_token_t", 0, "record requires %d bytes, received %lld",
                    QA_QVM_SCRIPT_TOKEN_BYTES, (long long)view->length);
  /* TS stringLength: C text is bytes, so the >255 check is inherent;
   * embedded NULs cannot occur in a C string. */
  size_t length = strlen(text);
  if (length > 1023) qa_range_error("QVM pc_token_t string exceeds 1023 source bytes");
  if (subtype != trunc(subtype) || subtype < -2147483648.0 || subtype > 2147483647.0)
    qa_range_error("QVM pc_token_t requires an int32 source subtype");
  if (!isfinite(integer_value) || integer_value != trunc(integer_value) ||
      integer_value < -9007199254740991.0 || integer_value > 9007199254740991.0)
    qa_range_error("QVM pc_token_t requires an exactly represented source integer");
  bool leading_quote = type == 1 && length > 0 && (uint8_t)text[0] == 34;
  size_t stripped_length = length - (leading_quote ? 1 : 0);
  if (type == 1 && stripped_length == 0)
    qa_range_error("QVM pc_token_t cannot encode undefined empty StripDoubleQuotes input");
  for (size_t i = 0; i < length; i++)
    qa_qvm_writable_view_set_uint8(view, (int64_t)(16 + i), (uint8_t)text[i]);
  qa_qvm_writable_view_set_uint8(view, (int64_t)(16 + length), 0);
  qa_qvm_writable_view_set_int32(view, 0, type, true);
  qa_qvm_writable_view_set_int32(view, 4, (int32_t)subtype, true);
  qa_qvm_writable_view_set_int32(view, 8, (int32_t)(uint32_t)(int64_t)integer_value, true);
  qa_qvm_writable_view_set_float32(view, 12, float_value, true);
  if (leading_quote) {
    for (size_t i = 0; i < length; i++) {
      uint8_t byte = qa_qvm_writable_view_get_uint8(view, (int64_t)(17 + i));
      qa_qvm_writable_view_set_uint8(view, (int64_t)(16 + i), byte);
    }
  }
  if (type == 1 && qa_qvm_writable_view_get_uint8(view, (int64_t)(16 + stripped_length - 1)) == 34)
    qa_qvm_writable_view_set_uint8(view, (int64_t)(16 + stripped_length - 1), 0);
}

static int32_t token_type(const char *kind) {
  if (strcmp(kind, "primitive") == 0) return 0;
  if (strcmp(kind, "string") == 0) return 1;
  if (strcmp(kind, "literal") == 0) return 2;
  if (strcmp(kind, "number") == 0) return 3;
  if (strcmp(kind, "name") == 0) return 4;
  if (strcmp(kind, "punctuation") == 0) return 5;
  qa_range_error("Unknown script token kind %s", kind);
}

void qa_qvm_write_script_token(qa_qvm_writable_view *view, const qa_qvmy_script_token_record *value) {
  write_token(view, value->token_text, token_type(value->token_kind),
              value->subtype, value->integer_value, value->float_value);
}

void qa_qvm_write_script_token_memory(qa_qvm_writable_view *view, const qa_qvmy_source_token_memory *value) {
  write_token(view, value->string, value->type, value->subtype, value->integer_value, value->float_value);
}
