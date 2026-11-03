/* See include/qa/compat/qvm/script_record.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/script_record.h"

#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

/* TS stringLength over byte text (charCode > 255 cannot occur). */
static size_t string_length(const char *text, size_t extent) {
  for (size_t index = 0; index < extent; index++) {
    if ((uint8_t)text[index] == 0) return index;
    if (index >= 1023)
      qa_range_error("QVM pc_token_t string exceeds 1023 source bytes");
  }
  return extent;
}

static void write_token(qa_qvm_writable_view *view, const char *text,
                        size_t extent, int32_t type, int64_t subtype,
                        uint32_t integer_value, double float_value) {
  if (view->length < QA_QVM_SCRIPT_TOKEN_BYTES)
    qa_binary_error("QVM pc_token_t", 0,
                    "record requires %d bytes, received %lld",
                    QA_QVM_SCRIPT_TOKEN_BYTES, (long long)view->length);
  size_t length = string_length(text, extent);
  /* TS also requires Number.isSafeInteger(integerValue); a uint32
   * source always satisfies it, and setInt32 wraps modulo 2^32. */
  if (subtype < -2147483648LL || subtype > 2147483647LL)
    qa_range_error("QVM pc_token_t requires an int32 source subtype");
  bool leading_quote = type == 1 && length > 0 && (uint8_t)text[0] == 34;
  size_t stripped_length = length - (leading_quote ? 1 : 0);
  if (type == 1 && stripped_length == 0)
    qa_range_error(
        "QVM pc_token_t cannot encode undefined empty StripDoubleQuotes "
        "input");
  for (size_t i = 0; i < length; i++)
    qa_qvm_writable_view_set_uint8(view, (int64_t)(16 + i), (uint8_t)text[i]);
  qa_qvm_writable_view_set_uint8(view, (int64_t)(16 + length), 0);
  qa_qvm_writable_view_set_int32(view, 0, type, true);
  qa_qvm_writable_view_set_int32(view, 4, (int32_t)subtype, true);
  qa_qvm_writable_view_set_int32(view, 8, (int32_t)integer_value, true);
  qa_qvm_writable_view_set_float32(view, 12, float_value, true);
  if (leading_quote) {
    for (size_t i = 0; i < length; i++) {
      uint8_t byte = qa_qvm_writable_view_get_uint8(view, (int64_t)(17 + i));
      qa_qvm_writable_view_set_uint8(view, (int64_t)(16 + i), byte);
    }
  }
  if (type == 1 &&
      qa_qvm_writable_view_get_uint8(view, (int64_t)(16 + stripped_length - 1)) == 34)
    qa_qvm_writable_view_set_uint8(view, (int64_t)(16 + stripped_length - 1), 0);
}

static int32_t token_type(qa_ui_script_token_kind kind) {
  switch (kind) {
    case QA_UI_SCRIPT_KIND_PRIMITIVE: return 0;
    case QA_UI_SCRIPT_KIND_STRING: return 1;
    case QA_UI_SCRIPT_KIND_LITERAL: return 2;
    case QA_UI_SCRIPT_KIND_NUMBER: return 3;
    case QA_UI_SCRIPT_KIND_NAME: return 4;
    case QA_UI_SCRIPT_KIND_PUNCTUATION: return 5;
  }
  return 0;
}

void qa_qvm_write_script_token(qa_qvm_writable_view *view,
                               const qa_ui_script_token_record *value) {
  write_token(view, value->token->text, value->token->text_length,
              token_type(value->token->kind), value->subtype,
              value->integer_value, value->float_value);
}

void qa_qvm_write_script_token_memory(qa_qvm_writable_view *view,
                                      qa_ui_source_token_memory *value) {
  char text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  qa_ui_source_token_string(value, text);
  size_t extent = strlen(text);
  int32_t type = qa_ui_source_token_type(value);
  int64_t subtype = qa_ui_source_token_subtype(value);
  uint32_t integer_value = qa_ui_source_token_integer_value(value);
  double float_value = qa_ui_source_token_float_value(value);
  write_token(view, text, extent, type, subtype, integer_value, float_value);
}
