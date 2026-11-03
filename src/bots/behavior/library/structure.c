/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/structure.h"
#include "library_internal.h"
#include "qa/core/numeric.h"
#include <math.h>
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
#define SOURCE_INT_MIN (-32768)
#define SOURCE_INT_MAX 32767
const qa_structure_field_definition *qa_find_structure_field(const qa_structure_field_definition *f,
                                                             size_t n, const char *name) {
  for (size_t i = 0; i < n; i++)
    if (strcmp(f[i].name, name) == 0) return &f[i];
  return NULL;
}
void qa_structure_field_release(qa_structure_field *field) {
  if (field->token != NULL) qa_ui_script_token_release(field->token);
  *field = (qa_structure_field){NULL, NULL, NULL};
}
void qa_structure_reader_init(qa_structure_reader *r, qa_structure_token_source source,
                              const char *path, const qa_ui_script_diagnostic *prior,
                              size_t prior_count, qa_ui_script_report_fn report, void *report_ctx) {
  if (path[0] == '\0') qa_range_error("structure source path cannot be empty");
  memset(r, 0, sizeof *r);
  r->source = source;
  r->path = lib_strdup(path);
  r->report = report;
  r->report_ctx = report_ctx;
  lib_diagnostics list = {NULL, 0, 0};
  for (size_t i = 0; i < prior_count; i++) lib_diagnostics_copy(&list, &prior[i]);
  r->reported = list.items;
  r->reported_count = list.count;
  r->reported_capacity = list.capacity;
}
void qa_structure_reader_destroy(qa_structure_reader *r) {
  lib_diagnostics list = {r->reported, r->reported_count, r->reported_capacity};
  lib_diagnostics_destroy(&list);
  free(r->path);
  free(r->range_error);
  memset(r, 0, sizeof *r);
}
const qa_ui_script_diagnostic *qa_structure_reader_diagnostics(const qa_structure_reader *r,
                                                               size_t *out_count) {
  *out_count = r->reported_count;
  return r->reported;
}
__attribute__((format(printf, 3, 4))) static bool fail(qa_structure_reader *r,
                                                       const qa_ui_script_location *at,
                                                       const char *format, ...) {
  va_list args, copy;
  va_start(args, format);
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  char *message = lib_alloc((size_t)length + 1);
  vsnprintf(message, (size_t)length + 1, format, args);
  va_end(args);
  lib_diagnostics list = {r->reported, r->reported_count, r->reported_capacity};
  lib_diagnostics_push(&list, QA_UI_SCRIPT_SEVERITY_ERROR, message, at->path, at->line, at->column);
  r->reported = list.items;
  r->reported_count = list.count;
  r->reported_capacity = list.capacity;
  free(message);
  r->failed = true;
  if (r->report != NULL) r->report(r->report_ctx, &r->reported[r->reported_count - 1]);
  return false;
}
/* TS expectAny: false on a failure; *out holds one reference on true. */
static bool expect_any(qa_structure_reader *r, const char *message, qa_ui_script_token **out) {
  int status = r->source.next(r->source.ctx, out);
  if (status == QA_STRUCTURE_SOURCE_FAILURE) {
    r->failed = true;
    return false;
  }
  if (status == QA_STRUCTURE_SOURCE_END) {
    qa_ui_script_location at = {r->path, 1, 1};
    return fail(r, &at, "%s", message);
  }
  return true;
}
static bool is_punctuation(const qa_ui_script_token *token, int64_t punctuation) {
  return token->kind == QA_UI_SCRIPT_KIND_PUNCTUATION && token->punctuation == punctuation;
}
static bool expect_text(qa_structure_reader *r, const char *text) {
  qa_ui_script_token *token;
  char message[32];
  snprintf(message, sizeof message, "expected %s", text);
  if (!expect_any(r, message, &token)) return false;
  bool same = lib_token_is(token, text);
  if (!same) fail(r, &token->location, "expected %s, found %s", text, token->text);
  qa_ui_script_token_release(token);
  return same;
}
bool qa_structure_reader_begin(qa_structure_reader *r) { return expect_text(r, "{"); }
bool qa_structure_reader_next_field(qa_structure_reader *r, qa_structure_field *out, bool *out_found) {
  qa_ui_script_token *token;
  if (!expect_any(r, "expected structure field or closing brace", &token)) return false;
  if (is_punctuation(token, QA_UI_SCRIPT_PUNCT_BRACE_CLOSE)) {
    qa_ui_script_token_release(token);
    *out_found = false;
    return true;
  }
  *out = (qa_structure_field){token->text, &token->location, token};
  *out_found = true;
  return true;
}
bool qa_structure_reader_reject_field(qa_structure_reader *r, const qa_structure_field *field) {
  return fail(r, field->location, "unknown structure field %s", field->name);
}
/* structureFloat with its RangeError routed per capture_range_errors. */
static bool structure_float(qa_structure_reader *r, double value, float *out) {
  float result = (float)value;
  if (isfinite(value) && !isfinite(result)) {
    const char *message = "structure value exceeds the source float conversion range";
    if (r == NULL || !r->capture_range_errors) qa_range_error("%s", message);
    free(r->range_error);
    r->range_error = lib_strdup(message);
    r->failed = true;
    return false;
  }
  *out = result;
  return true;
}
/* TS readNumber: *out_token keeps a reference for the location. */
static bool read_number(qa_structure_reader *r, bool float_field, double *out_value,
                        qa_ui_script_token **out_token) {
  qa_ui_script_token *token;
  if (!expect_any(r, "expected number", &token)) return false;
  bool negative = false;
  if (token->kind == QA_UI_SCRIPT_KIND_PUNCTUATION) {
    if (token->punctuation != QA_UI_SCRIPT_PUNCT_SUBTRACT) {
      fail(r, &token->location, "unexpected punctuation %s", token->text);
      qa_ui_script_token_release(token);
      return false;
    }
    negative = true;
    qa_ui_script_token_release(token);
    if (!expect_any(r, "expected number after minus sign", &token)) return false;
  }
  if (token->kind != QA_UI_SCRIPT_KIND_NUMBER) {
    fail(r, &token->location, "expected number, found %s", token->text);
    qa_ui_script_token_release(token);
    return false;
  }
  bool is_float = (token->flags & QA_UI_SCRIPT_NUMBER_FLOAT) != 0;
  if (!float_field && is_float) {
    fail(r, &token->location, "unexpected float");
    qa_ui_script_token_release(token);
    return false;
  }
  double value = is_float ? token->float_value : (double)(int32_t)token->integer_value;
  if (!is_float && negative && value == -2147483648.0)
    qa_range_error("structure negation exceeds the source signed-long range");
  *out_value = negative ? -value : value;
  *out_token = token;
  return true;
}
bool qa_structure_reader_read_int(qa_structure_reader *r, int32_t *out) {
  double value;
  qa_ui_script_token *token;
  if (!read_number(r, false, &value, &token)) return false;
  if (trunc(value) != value || value < SOURCE_INT_MIN || value > SOURCE_INT_MAX) {
    fail(r, &token->location, "value %s out of range [%d, %d]", QA_JS_NUMBER(value), SOURCE_INT_MIN, /* `${value}`: 1.5, 1e21 */
         SOURCE_INT_MAX);
    qa_ui_script_token_release(token);
    return false;
  }
  qa_ui_script_token_release(token);
  *out = (int32_t)value;
  return true;
}
bool qa_structure_reader_read_float(qa_structure_reader *r, float *out) {
  double value;
  qa_ui_script_token *token;
  if (!read_number(r, true, &value, &token)) return false;
  qa_ui_script_token_release(token);
  return structure_float(r, value, out);
}
/* byteString: bytes before NUL, cut to the maximum. */
static size_t byte_length(const char *value, size_t length, int64_t maximum) {
  const char *zero = memchr(value, 0, length);
  size_t end = zero != NULL ? (size_t)(zero - value) : length;
  return (int64_t)end < maximum ? end : (size_t)maximum;
}
bool qa_structure_reader_read_string(qa_structure_reader *r, int64_t maximum_bytes, char **out) {
  if (maximum_bytes < 1) qa_range_error("maximum string bytes must be a positive integer");
  qa_ui_script_token *token;
  if (!expect_any(r, "expected string", &token)) return false;
  if (token->kind != QA_UI_SCRIPT_KIND_STRING) {
    fail(r, &token->location, "expected string, found %s", token->text);
    qa_ui_script_token_release(token);
    return false;
  }
  *out = lib_strndup(token->value, byte_length(token->value, token->value_length, maximum_bytes));
  qa_ui_script_token_release(token);
  return true;
}
bool qa_structure_reader_read_float_array(qa_structure_reader *r, int64_t maximum_values, float *out,
                                          size_t *out_count) {
  if (maximum_values < 1) qa_range_error("maximum array values must be a positive integer");
  if (!expect_text(r, "{")) return false;
  size_t count = 0;
  while ((int64_t)count < maximum_values) {
    qa_ui_script_token *token;
    if (!expect_any(r, "expected array value or closing brace", &token)) return false;
    if (is_punctuation(token, QA_UI_SCRIPT_PUNCT_BRACE_CLOSE)) {
      qa_ui_script_token_release(token);
      *out_count = count;
      return true;
    }
    r->source.unread(r->source.ctx, token);
    if (!qa_structure_reader_read_float(r, &out[count])) return false;
    count++;
    qa_ui_script_token *separator;
    if (!expect_any(r, "expected comma or closing brace", &separator)) return false;
    bool closed = is_punctuation(separator, QA_UI_SCRIPT_PUNCT_BRACE_CLOSE);
    if (!closed && !is_punctuation(separator, QA_UI_SCRIPT_PUNCT_COMMA)) {
      fail(r, &separator->location, "expected a comma, found %s", separator->text);
      qa_ui_script_token_release(separator);
      return false;
    }
    qa_ui_script_token_release(separator);
    if (closed) {
      *out_count = count;
      return true;
    }
  }
  *out_count = count;
  return true;
}
static uint8_t *field_view(uint8_t *bytes, size_t length, int64_t offset, int64_t size) {
  if (offset < 0 || (uint64_t)(offset + size) > length)
    qa_range_error("structure field exceeds its source allocation");
  return bytes + offset;
}
/* sprintf %f on a promoted binary32 with source default rounding. */
static void structure_decimal(double value, char *out, size_t capacity) {
  float single = (float)value;
  uint32_t bits;
  memcpy(&bits, &single, 4);
  const char *sign = bits >> 31 == 0 ? "" : "-";
  if (((bits >> 23) & 255) == 255) {
    snprintf(out, capacity, "%s%s", sign, (bits & 0x7fffff) == 0 ? "inf" : "nan");
    return;
  }
  snprintf(out, capacity, "%.6f", (double)single);
}
static bool read_number_field(qa_structure_reader *r, const qa_structure_field_definition *field,
                              uint8_t *bytes, size_t length, int64_t offset) {
  qa_ui_script_token *token;
  if (!expect_any(r, "couldn't read expected token", &token)) return false;
  bool is_unsigned = (field->type & QA_STRUCT_UNSIGNED) != 0;
  bool bounded = (field->type & QA_STRUCT_BOUNDED) != 0;
  int64_t type = field->type & QA_STRUCT_TYPE;
  bool negative = false, ok = true;
  if (token->kind == QA_UI_SCRIPT_KIND_PUNCTUATION) {
    if (is_unsigned) ok = fail(r, &token->location, "expected unsigned value, found %s", token->text);
    else if (!lib_token_is(token, "-")) ok = fail(r, &token->location, "unexpected punctuation %s", token->text);
    if (!ok) {
      qa_ui_script_token_release(token);
      return false;
    }
    negative = true;
    qa_ui_script_token_release(token);
    if (!expect_any(r, "couldn't read expected token", &token)) return false;
  }
  if (token->kind != QA_UI_SCRIPT_KIND_NUMBER) {
    fail(r, &token->location, "expected number, found %s", token->text);
    qa_ui_script_token_release(token);
    return false;
  }
  bool is_float = (token->flags & QA_UI_SCRIPT_NUMBER_FLOAT) != 0;
  if (is_float && type != QA_STRUCT_FLOAT) {
    fail(r, &token->location, "unexpected float");
    qa_ui_script_token_release(token);
    return false;
  }
  double raw = is_float ? token->float_value : (double)(int32_t)token->integer_value;
  if (!is_float && negative && raw == -2147483648.0)
    qa_range_error("structure negation exceeds the source signed-long range");
  double value = negative ? -raw : raw;
  double minimum = (float)field->floatmin, maximum = (float)field->floatmax;
  if (type == QA_STRUCT_CHAR || type == QA_STRUCT_INT) {
    double low = is_unsigned ? 0 : type == QA_STRUCT_CHAR ? -128 : -32768;
    double high = type == QA_STRUCT_CHAR ? (is_unsigned ? 255 : 127) : (is_unsigned ? 65535 : 32767);
    if (bounded) {
      low = trunc(qa_js_max(low, minimum));
      high = trunc(qa_js_min(high, maximum));
    }
    if (value < low || value > high) {
      fail(r, &token->location, "value %s out of range [%s, %s]", QA_JS_NUMBER(value),
           QA_JS_NUMBER(low), QA_JS_NUMBER(high));
      qa_ui_script_token_release(token);
      return false;
    }
  } else if (bounded && (value < minimum || value > maximum)) {
    char low[400], high[400];
    structure_decimal(minimum, low, sizeof low);
    structure_decimal(maximum, high, sizeof high);
    if (is_float) fail(r, &token->location, "float out of range [%s, %s]", low, high);
    else fail(r, &token->location, "value %s out of range [%s, %s]", QA_JS_NUMBER(value), low, high);
    qa_ui_script_token_release(token);
    return false;
  }
  qa_ui_script_token_release(token);
  uint8_t *target = field_view(bytes, length, offset, type == QA_STRUCT_CHAR ? 1 : 4);
  if (type == QA_STRUCT_CHAR) *target = (uint8_t)qa_uint32(value);
  else if (type == QA_STRUCT_INT) lib_set_i32(target, qa_int32(value));
  else if (type == QA_STRUCT_FLOAT) {
    float stored;
    if (!structure_float(r, value, &stored)) return false;
    lib_set_f32(target, stored);
  }
  return true;
}
static bool read_char_field(qa_structure_reader *r, const qa_structure_field_definition *field,
                            uint8_t *bytes, size_t length, int64_t offset) {
  qa_ui_script_token *token;
  if (!expect_any(r, "couldn't read expected token", &token)) return false;
  if (token->kind == QA_UI_SCRIPT_KIND_LITERAL) {
    size_t used = byte_length(token->value, token->value_length, 1);
    *field_view(bytes, length, offset, 1) = used == 0 ? 0 : (uint8_t)token->value[0];
    qa_ui_script_token_release(token);
    return true;
  }
  r->source.unread(r->source.ctx, token);
  return read_number_field(r, field, bytes, length, offset);
}
/* The failed flag distinguishes a fail() from clean control flow. */
static bool read_structure_body(qa_structure_reader *r, const qa_structure_definition *definition,
                                uint8_t *bytes, size_t length, int64_t base) {
  if (!expect_text(r, "{")) return false;
  while (true) {
    qa_ui_script_token *token;
    if (!expect_any(r, "couldn't read expected token", &token)) return false;
    if (lib_token_is(token, "}")) {
      qa_ui_script_token_release(token);
      return true;
    }
    const qa_structure_field_definition *field =
        qa_find_structure_field(definition->fields, definition->field_count, token->text);
    if (field == NULL) {
      fail(r, &token->location, "unknown structure field %s", token->text);
      qa_ui_script_token_release(token);
      return false;
    }
    bool array = (field->type & QA_STRUCT_ARRAY) != 0;
    if (array && !expect_text(r, "{")) {
      qa_ui_script_token_release(token);
      return false;
    }
    int64_t offset = base + field->offset;
    for (int64_t remaining = array ? field->maxarray : 1; remaining-- > 0;) {
      if (array) { /* TS checkText("}"): an absent token reads on. */
        qa_ui_script_token *check;
        int status = r->source.next(r->source.ctx, &check);
        if (status == QA_STRUCTURE_SOURCE_FAILURE) {
          r->failed = true;
          qa_ui_script_token_release(token);
          return false;
        }
        if (status == QA_STRUCTURE_SOURCE_TOKEN) {
          if (lib_token_is(check, "}")) {
            qa_ui_script_token_release(check);
            break;
          }
          r->source.unread(r->source.ctx, check);
        }
      }
      bool ok = true;
      switch (field->type & QA_STRUCT_TYPE) {
        case QA_STRUCT_CHAR:
          ok = read_char_field(r, field, bytes, length, offset);
          offset++;
          break;
        case QA_STRUCT_INT:
        case QA_STRUCT_FLOAT:
          ok = read_number_field(r, field, bytes, length, offset);
          offset += 4;
          break;
        case QA_STRUCT_STRING: {
          char *value = NULL;
          ok = qa_structure_reader_read_string(r, QA_STRUCTURE_DEFAULT_STRING_BYTES, &value);
          if (ok) {
            uint8_t *target = field_view(bytes, length, offset, QA_MAX_STRINGFIELD);
            memset(target, 0, QA_MAX_STRINGFIELD);
            memcpy(target, value, strlen(value));
            free(value);
          }
          offset += QA_MAX_STRINGFIELD;
          break;
        }
        case QA_STRUCT_STRUCT:
          if (field->substruct == NULL) {
            ok = fail(r, &token->location, "BUG: no sub structure defined");
            break;
          }
          /* The source intentionally ignores this return value. */
          (void)qa_structure_reader_read_structure(r, field->substruct, bytes, length, offset);
          offset += field->substruct->size;
          break;
        default:
          break;
      }
      if (!ok) {
        qa_ui_script_token_release(token);
        return false;
      }
      if (array) {
        qa_ui_script_token *separator;
        if (!expect_any(r, "couldn't read expected token", &separator)) {
          qa_ui_script_token_release(token);
          return false;
        }
        bool closed = lib_token_is(separator, "}");
        if (!closed && !lib_token_is(separator, ",")) {
          fail(r, &separator->location, "expected a comma, found %s", separator->text);
          qa_ui_script_token_release(separator);
          qa_ui_script_token_release(token);
          return false;
        }
        qa_ui_script_token_release(separator);
        if (closed) break;
      }
    }
    qa_ui_script_token_release(token);
  }
}
bool qa_structure_reader_read_structure(qa_structure_reader *r,
                                        const qa_structure_definition *definition, uint8_t *bytes,
                                        size_t length, int64_t base) {
  bool result = read_structure_body(r, definition, bytes, length, base);
  /* Handled here as the TS catch does; the caller sees only the false return. */
  if (!result) r->failed = false;
  return result;
}
bool qa_write_indent_bots_a(qa_structure_write_fn w, void *c, int64_t indent) {
  for (int64_t remaining = indent; remaining-- > 0;)
    if (w(c, "\t") < 0) return false;
  return true;
}
bool qa_write_float_bots_a(qa_structure_write_fn w, void *c, double v) {
  char text[400];
  structure_decimal(v, text, sizeof text);
  for (size_t index = strlen(text) - 1; index > 0; index--) {
    char character = text[index];
    if (character != '0' && character != '.') break;
    text[index] = '\0';
    if (character == '.') break;
  }
  return w(c, text) >= 0;
}
bool qa_write_struct_with_indent_bots_a(qa_structure_write_fn w, void *c,
                                        const qa_structure_definition *d, const uint8_t *bytes,
                                        size_t len, int64_t indent, int64_t base) {
  uint8_t *data = (uint8_t *)(uintptr_t)bytes; /* field_view is read-only here */
  if (!qa_write_indent_bots_a(w, c, indent) || w(c, "{\r\n") < 0) return false;
  indent++;
  for (size_t i = 0; i < d->field_count; i++) {
    const qa_structure_field_definition *field = &d->fields[i];
    char *label = lib_format("%s\t", field->name);
    bool wrote = qa_write_indent_bots_a(w, c, indent) && w(c, label) >= 0;
    free(label);
    if (!wrote) return false;
    int64_t offset = base + field->offset;
    bool array = (field->type & QA_STRUCT_ARRAY) != 0;
    if (array && w(c, "{") < 0) return false;
    for (int64_t remaining = array ? field->maxarray : 1; remaining-- > 0;) {
      char number[32];
      switch (field->type & QA_STRUCT_TYPE) {
        case QA_STRUCT_CHAR:
          snprintf(number, sizeof number, "%d", (int)(int8_t)*field_view(data, len, offset, 1));
          if (w(c, number) < 0) return false;
          offset++;
          break;
        case QA_STRUCT_INT:
          snprintf(number, sizeof number, "%d", lib_i32(field_view(data, len, offset, 4)));
          if (w(c, number) < 0) return false;
          offset += 4;
          break;
        case QA_STRUCT_FLOAT:
          if (!qa_write_float_bots_a(w, c, lib_f32(field_view(data, len, offset, 4)))) return false;
          offset += 4;
          break;
        case QA_STRUCT_STRING: {
          int64_t end = offset;
          while (*field_view(data, len, end, 1) != 0) end++;
          char *text = lib_format("\"%.*s\"", (int)(end - offset), (const char *)data + offset);
          bool ok = w(c, text) >= 0;
          free(text);
          if (!ok) return false;
          offset += QA_MAX_STRINGFIELD;
          break;
        }
        case QA_STRUCT_STRUCT:
          if (field->substruct == NULL)
            qa_range_error("WriteStructWithIndent: null source substructure");
          /* l_struct.c passes structure, not p, including every array element. */
          if (!qa_write_struct_with_indent_bots_a(w, c, field->substruct, bytes, len, indent, base))
            return false;
          offset += field->substruct->size;
          break;
        default:
          break;
      }
      if (array && w(c, remaining > 0 ? "," : "}") < 0) return false;
    }
    if (w(c, "\r\n") < 0) return false;
  }
  return qa_write_indent_bots_a(w, c, indent - 1) && w(c, "}\r\n") >= 0;
}
bool qa_write_structure_bots_a(qa_structure_write_fn w, void *c, const qa_structure_definition *d,
                               const uint8_t *b, size_t n) {
  return qa_write_struct_with_indent_bots_a(w, c, d, b, n, 0, 0);
}
