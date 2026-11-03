/* See include/qa/ui/common/legacy/script/token_memory.h.
 * GPL-2.0-or-later. */
#include "qa/ui/common/legacy/script/token_memory.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/binary.h"
#include "qa/foundation/support.h"

enum {
  OFF_TYPE = 1024,
  OFF_SUBTYPE = 1028,
  OFF_INTEGER = 1032,
  OFF_FLOAT = 1036,
  OFF_WHITESPACE = 1048,
  OFF_END_WHITESPACE = 1052,
  OFF_LINE = 1056,
  OFF_LINES_CROSSED = 1060,
  OFF_NEXT = 1064,
  STRING_BYTES = 1024
};

struct qa_ui_script_token_snapshot {
  size_t refs;
  qa_ui_source_token_memory *storage; /* owned */
  qa_ui_script_token *token;          /* weak: the token owns us */
  int64_t subtype;
  uint32_t integer_value;
  double float_value;
  qa_ui_source_token_context context; /* owned */
};

struct qa_ui_source_token_memory {
  qa_ui_source_token_borrow_fn borrow;
  void *borrow_ctx;
  uint8_t *local_bytes; /* owned when set (local/view-from-snapshot) */
  bool owns_local;
  bool has_extent;
  int64_t text_extent;
  qa_ui_script_token_snapshot *snapshot; /* ref unless snapshot_weak */
  bool snapshot_weak;
};

void qa_ui_source_token_context_destroy(qa_ui_source_token_context *context) {
  free(context->path);
  free(context->leading_whitespace);
  memset(context, 0, sizeof *context);
  context->column = 1;
}

bool qa_ui_source_token_context_set(qa_ui_source_token_context *context,
                                    const char *path, int64_t column,
                                    const char *leading_whitespace,
                                    size_t leading_whitespace_length) {
  char *new_path = malloc(strlen(path) + 1);
  char *new_ws = malloc(leading_whitespace_length + 1);
  if (new_path == NULL || new_ws == NULL) {
    free(new_path);
    free(new_ws);
    return false;
  }
  strcpy(new_path, path);
  memcpy(new_ws, leading_whitespace, leading_whitespace_length);
  new_ws[leading_whitespace_length] = '\0';
  free(context->path);
  free(context->leading_whitespace);
  context->path = new_path;
  context->column = column;
  context->leading_whitespace = new_ws;
  context->leading_whitespace_length = leading_whitespace_length;
  return true;
}

static uint8_t *span(qa_ui_source_token_memory *token, size_t *out_length) {
  if (token->borrow != NULL) return token->borrow(token->borrow_ctx, out_length);
  *out_length = QA_UI_SOURCE_TOKEN_BYTES;
  return token->local_bytes;
}

uint8_t *qa_ui_source_token_bytes(qa_ui_source_token_memory *token) {
  size_t length = 0;
  uint8_t *bytes = span(token, &length);
  if (length != QA_UI_SOURCE_TOKEN_BYTES)
    qa_range_error("token_t requires exactly 1068 source bytes");
  return bytes;
}

qa_ui_source_token_memory *qa_ui_source_token_memory_create(
    qa_ui_source_token_borrow_fn borrow, void *borrow_ctx) {
  qa_ui_source_token_memory *token = calloc(1, sizeof *token);
  if (token == NULL) return NULL;
  token->borrow = borrow;
  token->borrow_ctx = borrow_ctx;
  return token;
}

qa_ui_source_token_memory *qa_ui_source_token_memory_create_local(void) {
  qa_ui_source_token_memory *token = calloc(1, sizeof *token);
  if (token == NULL) return NULL;
  token->local_bytes = calloc(QA_UI_SOURCE_TOKEN_BYTES, 1);
  if (token->local_bytes == NULL) {
    free(token);
    return NULL;
  }
  token->owns_local = true;
  return token;
}

typedef struct view_span {
  uint8_t *bytes;
  size_t length;
} view_span;

static uint8_t *view_borrow(void *ctx, size_t *out_length) {
  view_span *view = ctx;
  *out_length = view->length;
  return view->bytes;
}

qa_ui_source_token_memory *qa_ui_source_token_memory_create_view(
    uint8_t *bytes, size_t length) {
  view_span *view = malloc(sizeof *view);
  if (view == NULL) return NULL;
  view->bytes = bytes;
  view->length = length;
  qa_ui_source_token_memory *token =
      qa_ui_source_token_memory_create(view_borrow, view);
  if (token == NULL) free(view);
  return token;
}

void qa_ui_source_token_memory_destroy(qa_ui_source_token_memory *token) {
  if (token == NULL) return;
  if (token->borrow == view_borrow) free(token->borrow_ctx);
  if (token->owns_local) free(token->local_bytes);
  if (token->snapshot != NULL && !token->snapshot_weak)
    qa_ui_script_snapshot_release(token->snapshot);
  free(token);
}

qa_ui_script_token_snapshot *qa_ui_script_snapshot_retain(
    qa_ui_script_token_snapshot *snapshot) {
  if (snapshot != NULL) snapshot->refs++;
  return snapshot;
}

void qa_ui_script_snapshot_release(qa_ui_script_token_snapshot *snapshot) {
  if (snapshot == NULL || --snapshot->refs > 0) return;
  /* The snapshot owns its token (TS WeakMap lifetime); a token that
   * outlives it keeps a cleared back-pointer instead. */
  if (snapshot->token != NULL && snapshot->token->snapshot == snapshot)
    snapshot->token->snapshot = NULL;
  qa_ui_script_token_release(snapshot->token);
  qa_ui_source_token_memory_destroy(snapshot->storage);
  qa_ui_source_token_context_destroy(&snapshot->context);
  free(snapshot);
}

static void set_snapshot(qa_ui_source_token_memory *token,
                         qa_ui_script_token_snapshot *snapshot) {
  if (token->snapshot != NULL && !token->snapshot_weak)
    qa_ui_script_snapshot_release(token->snapshot);
  token->snapshot = qa_ui_script_snapshot_retain(snapshot);
  token->snapshot_weak = false;
}

static uint32_t rd_u32(const uint8_t *bytes, size_t offset) {
  uint32_t value;
  memcpy(&value, bytes + offset, sizeof value);
  return value;
}
static void wr_u32(uint8_t *bytes, size_t offset, uint32_t value) {
  memcpy(bytes + offset, &value, sizeof value);
}
static uint64_t rd_u64(const uint8_t *bytes, size_t offset) {
  uint64_t value;
  memcpy(&value, bytes + offset, sizeof value);
  return value;
}
static void wr_u64(uint8_t *bytes, size_t offset, uint64_t value) {
  memcpy(bytes + offset, &value, sizeof value);
}
static uint16_t rd_u16(const uint8_t *bytes, size_t offset) {
  uint16_t value;
  memcpy(&value, bytes + offset, sizeof value);
  return value;
}
static void wr_u16(uint8_t *bytes, size_t offset, uint16_t value) {
  memcpy(bytes + offset, &value, sizeof value);
}

/* x87 80-bit write of a binary64 value (TS writeFloat). */
static void write_float(uint8_t *bytes, size_t offset, double value) {
  uint64_t bits = qa_float64_bits(value);
  uint32_t sign = (uint32_t)(bits >> 63) << 15;
  uint32_t exponent = (uint32_t)((bits >> 52) & 0x7ff);
  uint64_t fraction = bits & 0xfffffffffffffull;
  uint64_t significand;
  uint32_t extended;
  if (exponent == 0 && fraction == 0) {
    significand = 0;
    extended = 0;
  } else if (exponent == 0) {
    int highest = 63 - __builtin_clzll(fraction);
    significand = fraction << (63 - highest);
    extended = (uint32_t)(highest - 1074 + 16383);
  } else {
    significand = (fraction | 0x10000000000000ull) << 11;
    extended = exponent == 0x7ff ? 0x7fff : exponent - 1023 + 16383;
  }
  wr_u64(bytes, offset, significand);
  wr_u16(bytes, offset + 8, (uint16_t)(sign | extended));
}

static double read_float(const uint8_t *bytes) {
  uint64_t significand = rd_u64(bytes, OFF_FLOAT);
  uint16_t word = rd_u16(bytes, OFF_FLOAT + 8);
  uint32_t exponent = word & 0x7fff;
  bool negative = (word & 0x8000) != 0;
  if (exponent == 0 && significand == 0) return negative ? -0.0 : 0.0;
  if (exponent == 0x7fff && significand == 0x8000000000000000ull)
    return negative ? -INFINITY : INFINITY;
  if (exponent == 0x7fff && significand > 0x8000000000000000ull) return NAN;
  int power = (int)exponent - 16383;
  double fraction = (double)significand / 9223372036854775808.0;
  double magnitude = power < -1022
                         ? ldexp(ldexp(fraction, power + 1022), -1022)
                         : ldexp(fraction, power);
  double value = negative ? -magnitude : magnitude;
  uint8_t check[10] = {0};
  write_float(check, 0, value);
  if (rd_u64(check, 0) != significand || rd_u16(check, 8) != word)
    qa_range_error("token_t long double exceeds the supported binary64 numeric profile");
  return value;
}

qa_save_value *qa_ui_source_token_capture_save_state(
    qa_ui_source_token_memory *token) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  if (!qa_save_value_set(object, "bytes",
                         qa_save_value_bytes(
                             qa_ui_source_token_bytes(token),
                             QA_UI_SOURCE_TOKEN_BYTES)))
    goto oom;
  if (token->has_extent) {
    if (!qa_save_value_set(object, "textExtent",
                           qa_save_value_number((double)token->text_extent)))
      goto oom;
  } else if (!qa_save_value_set(object, "textExtent", qa_save_value_null())) {
    goto oom;
  }
  return object;
oom:
  qa_save_value_free(object);
  return NULL;
}

void qa_ui_source_token_restore_save_state(qa_ui_source_token_memory *token,
                                           const qa_save_value *value,
                                           bool verify_bytes) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "script.token"))
    qa_range_error("script token restore allocation failed");
  qa_save_reader field;
  if (!qa_save_reader_field(&reader, "bytes", &field))
    qa_range_error("script token restore allocation failed");
  size_t length = 0;
  const uint8_t *bytes = qa_save_reader_bytes(&field, &length);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&reader, "textExtent", &field))
    qa_range_error("script token restore allocation failed");
  bool has_extent = !qa_save_reader_is_null(&field);
  int64_t extent = has_extent
                       ? (int64_t)qa_save_reader_integer_min(&field, 0)
                       : 0;
  qa_save_reader_destroy(&field);
  if (length != QA_UI_SOURCE_TOKEN_BYTES || (has_extent && extent > 1024))
    qa_save_reader_fail(&reader, "invalid token extent");
  uint8_t *mine = qa_ui_source_token_bytes(token);
  if (verify_bytes && memcmp(bytes, mine, QA_UI_SOURCE_TOKEN_BYTES) != 0)
    qa_save_reader_fail(&reader, "token bytes disagree with restored allocation");
  if (!verify_bytes) memcpy(mine, bytes, QA_UI_SOURCE_TOKEN_BYTES);
  token->has_extent = has_extent;
  token->text_extent = extent;
  set_snapshot(token, NULL);
  qa_save_reader_destroy(&reader);
}

void qa_ui_source_token_clear(qa_ui_source_token_memory *token) {
  memset(qa_ui_source_token_bytes(token), 0, QA_UI_SOURCE_TOKEN_BYTES);
  token->has_extent = false;
  set_snapshot(token, NULL);
}

void qa_ui_source_token_copy_from(qa_ui_source_token_memory *token,
                                  qa_ui_source_token_memory *other) {
  void * destination = qa_ui_source_token_bytes(token);
  const void * source_value = qa_ui_source_token_bytes(other);
  memcpy(destination, source_value,
         QA_UI_SOURCE_TOKEN_BYTES);
  token->has_extent = other->has_extent;
  token->text_extent = other->text_extent;
  set_snapshot(token, other->snapshot);
}

#define FIELD32(name, offset)                                                \
  int32_t qa_ui_source_token_##name(qa_ui_source_token_memory *token) {      \
    return (int32_t)rd_u32(qa_ui_source_token_bytes(token), offset);         \
  }                                                                          \
  void qa_ui_source_token_set_##name(qa_ui_source_token_memory *token,       \
                                     int32_t value) {                        \
    wr_u32(qa_ui_source_token_bytes(token), offset, (uint32_t)value);        \
  }
FIELD32(type, OFF_TYPE)
FIELD32(subtype, OFF_SUBTYPE)
FIELD32(line, OFF_LINE)
FIELD32(lines_crossed, OFF_LINES_CROSSED)
#undef FIELD32

#define FIELDU32(name, offset)                                               \
  uint32_t qa_ui_source_token_##name(qa_ui_source_token_memory *token) {     \
    return rd_u32(qa_ui_source_token_bytes(token), offset);                  \
  }                                                                          \
  void qa_ui_source_token_set_##name(qa_ui_source_token_memory *token,       \
                                     uint32_t value) {                       \
    wr_u32(qa_ui_source_token_bytes(token), offset, value);                  \
  }
FIELDU32(integer_value, OFF_INTEGER)
FIELDU32(whitespace_start, OFF_WHITESPACE)
FIELDU32(whitespace_end, OFF_END_WHITESPACE)
FIELDU32(next, OFF_NEXT)
#undef FIELDU32

double qa_ui_source_token_float_value(qa_ui_source_token_memory *token) {
  return read_float(qa_ui_source_token_bytes(token));
}
void qa_ui_source_token_set_float_value(qa_ui_source_token_memory *token,
                                        double value) {
  write_float(qa_ui_source_token_bytes(token), OFF_FLOAT, value);
}
bool qa_ui_source_token_whitespace_before(qa_ui_source_token_memory *token) {
  uint32_t left = qa_ui_source_token_whitespace_end(token);
  uint32_t right = qa_ui_source_token_whitespace_start(token);
  return left
         > right;
}
void qa_ui_source_token_clear_whitespace(qa_ui_source_token_memory *token) {
  qa_ui_source_token_set_whitespace_start(token, 0);
  qa_ui_source_token_set_whitespace_end(token, 0);
  qa_ui_source_token_set_lines_crossed(token, 0);
}

void qa_ui_source_token_string(qa_ui_source_token_memory *token,
                               char out[QA_UI_SOURCE_TOKEN_STRING_BYTES]) {
  uint8_t *bytes = qa_ui_source_token_bytes(token);
  for (size_t index = 0; index < STRING_BYTES; index++) {
    if (bytes[index] == 0) {
      memcpy(out, bytes, index);
      out[index] = '\0';
      return;
    }
  }
  qa_range_error("token_t string lacks a terminator within its 1024-byte field");
}

size_t qa_ui_source_token_string_length(qa_ui_source_token_memory *token) {
  char buffer[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  qa_ui_source_token_string(token, buffer);
  return strlen(buffer);
}

void qa_ui_source_token_set_string_byte(qa_ui_source_token_memory *token,
                                        int64_t index, int64_t value) {
  if (index < 0 || index >= STRING_BYTES)
    qa_range_error("token_t string byte is outside its field");
  if (value < 0 || value > 255)
    qa_range_error("token_t string requires source bytes");
  qa_ui_source_token_bytes(token)[index] = (uint8_t)value;
}

void qa_ui_source_token_write_string(qa_ui_source_token_memory *token,
                                     const char *text, size_t length) {
  if (length >= STRING_BYTES)
    qa_range_error("token_t string exceeds its 1024-byte field");
  for (size_t index = 0; index < length; index++)
    qa_ui_source_token_set_string_byte(token, (int64_t)index,
                                       (uint8_t)text[index]);
  qa_ui_source_token_set_string_byte(token, (int64_t)length, 0);
  token->has_extent = memchr(text, '\0', length) != NULL;
  token->text_extent = (int64_t)length;
}

static int64_t token_type_of(const qa_ui_script_token *token) {
  switch (token->kind) {
    case QA_UI_SCRIPT_KIND_PRIMITIVE: return 0;
    case QA_UI_SCRIPT_KIND_STRING: return 1;
    case QA_UI_SCRIPT_KIND_LITERAL: return 2;
    case QA_UI_SCRIPT_KIND_NUMBER: return 3;
    case QA_UI_SCRIPT_KIND_NAME: return 4;
    case QA_UI_SCRIPT_KIND_PUNCTUATION: return 5;
  }
  return 0;
}

static int64_t token_subtype_of(const qa_ui_script_token *token) {
  switch (token->kind) {
    case QA_UI_SCRIPT_KIND_PRIMITIVE: return 0;
    case QA_UI_SCRIPT_KIND_STRING:
    case QA_UI_SCRIPT_KIND_LITERAL:
    case QA_UI_SCRIPT_KIND_NAME: return token->length;
    case QA_UI_SCRIPT_KIND_NUMBER: return token->flags;
    case QA_UI_SCRIPT_KIND_PUNCTUATION: return token->punctuation;
  }
  return 0;
}

void qa_ui_source_token_write_record(
    qa_ui_source_token_memory *token,
    const qa_ui_script_token_record *record) {
  qa_ui_script_token_snapshot *captured = record->token->snapshot;
  if (captured == NULL) qa_ui_source_token_clear(token);
  else qa_ui_source_token_copy_from(token, captured->storage);
  qa_ui_source_token_write_string(token, record->token->text,
                                  record->token->text_length);
  qa_ui_source_token_set_type(token, (int32_t)token_type_of(record->token));
  qa_ui_source_token_set_subtype(token, (int32_t)record->subtype);
  qa_ui_source_token_set_integer_value(token, record->integer_value);
  qa_ui_source_token_set_float_value(token, record->float_value);
  qa_ui_source_token_set_line(token,
                              (int32_t)record->token->location.line);
  qa_ui_source_token_set_lines_crossed(
      token, (int32_t)record->token->lines_crossed);
}

void qa_ui_source_token_write_token(qa_ui_source_token_memory *token,
                                    qa_ui_script_token *value) {
  if (value->snapshot != NULL) {
    qa_ui_script_token_record record = {value, value->snapshot->subtype,
                                        value->snapshot->integer_value,
                                        value->snapshot->float_value};
    qa_ui_source_token_write_record(token, &record);
    return;
  }
  qa_ui_script_token_record record = {
      value, token_subtype_of(value),
      value->kind == QA_UI_SCRIPT_KIND_NUMBER ? value->integer_value : 0,
      value->kind == QA_UI_SCRIPT_KIND_NUMBER ? value->float_value : 0};
  qa_ui_source_token_write_record(token, &record);
}

static bool same_value(qa_ui_source_token_memory *token,
                       qa_ui_source_token_memory *other) {
  if (token->has_extent != other->has_extent) return false;
  if (token->has_extent && token->text_extent != other->text_extent)
    return false;
  const void * first = qa_ui_source_token_bytes(token);
  const void * second = qa_ui_source_token_bytes(other);
  return memcmp(first,
                second,
                QA_UI_SOURCE_TOKEN_BYTES) == 0;
}

bool qa_ui_source_token_read_record(
    qa_ui_source_token_memory *token,
    const qa_ui_source_token_context *context, bool has_text_extent,
    int64_t text_extent, qa_ui_script_token_record *out) {
  memset(out, 0, sizeof *out);
  if (has_text_extent) {
    if (text_extent < 0 || text_extent >= STRING_BYTES)
      qa_range_error("typed token text extent exceeds its source string field");
    token->has_extent =
        qa_ui_source_token_string_length(token) < (size_t)text_extent;
    token->text_extent = text_extent;
  }
  qa_ui_script_token_snapshot *previous = token->snapshot;
  if (previous != NULL && strcmp(previous->context.path, context->path) == 0
      && previous->context.column == context->column
      && previous->context.leading_whitespace_length
             == context->leading_whitespace_length
      && memcmp(previous->context.leading_whitespace,
                context->leading_whitespace,
                context->leading_whitespace_length) == 0
      && same_value(token, previous->storage)) {
    out->token = qa_ui_script_token_retain(previous->token);
    out->subtype = previous->subtype;
    out->integer_value = previous->integer_value;
    out->float_value = previous->float_value;
    return out->token != NULL;
  }
  char string_buffer[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  qa_ui_source_token_string(token, string_buffer);
  size_t string_length = strlen(string_buffer);
  const char *text = string_buffer;
  size_t text_length = string_length;
  char extent_buffer[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  if (token->has_extent && token->text_extent > (int64_t)string_length) {
    if (token->text_extent < 0 || token->text_extent >= STRING_BYTES)
      qa_range_error("typed token text extent exceeds its source string field");
    memcpy(extent_buffer, qa_ui_source_token_bytes(token),
           (size_t)token->text_extent);
    text = extent_buffer;
    text_length = (size_t)token->text_extent;
  }
  qa_ui_script_location location = {context->path,
                                    qa_ui_source_token_line(token),
                                    context->column};
  int64_t subtype = qa_ui_source_token_subtype(token);
  uint32_t integer_value = qa_ui_source_token_integer_value(token);
  double float_value = qa_ui_source_token_float_value(token);
  qa_ui_script_token_kind kind;
  switch (qa_ui_source_token_type(token)) {
    case 0: kind = QA_UI_SCRIPT_KIND_PRIMITIVE; break;
    case 1: kind = QA_UI_SCRIPT_KIND_STRING; break;
    case 2: kind = QA_UI_SCRIPT_KIND_LITERAL; break;
    case 3: kind = QA_UI_SCRIPT_KIND_NUMBER; break;
    case 4: kind = QA_UI_SCRIPT_KIND_NAME; break;
    case 5: kind = QA_UI_SCRIPT_KIND_PUNCTUATION; break;
    default: qa_range_error("token_t has no completed typed token");
  }
  qa_ui_script_token *value = qa_ui_script_token_create(
      kind, text, text_length, &location, context->leading_whitespace,
      context->leading_whitespace_length,
      qa_ui_source_token_lines_crossed(token));
  if (value == NULL) return false;
  bool ok = true;
  if (kind == QA_UI_SCRIPT_KIND_STRING || kind == QA_UI_SCRIPT_KIND_LITERAL) {
    /* TS text.slice(1, -1). */
    size_t inner_length = text_length >= 2 ? text_length - 2 : 0;
    ok = qa_ui_script_token_set_value(value, text_length >= 1 ? text + 1 : text,
                                      inner_length);
    value->length = (int64_t)text_length;
  } else if (kind == QA_UI_SCRIPT_KIND_NUMBER) {
    value->flags = subtype;
    value->integer_value = integer_value;
    value->float_value = float_value;
  } else if (kind == QA_UI_SCRIPT_KIND_NAME) {
    ok = qa_ui_script_token_set_value(value, text, text_length);
    value->length = (int64_t)text_length;
  } else if (kind == QA_UI_SCRIPT_KIND_PUNCTUATION) {
    ok = qa_ui_script_token_set_value(value, text, text_length);
    value->punctuation = subtype;
  } else {
    ok = qa_ui_script_token_set_value(value, text, text_length);
  }
  if (!ok) {
    qa_ui_script_token_release(value);
    return false;
  }
  qa_ui_script_token_snapshot *snapshot = calloc(1, sizeof *snapshot);
  qa_ui_source_token_memory *storage =
      qa_ui_source_token_memory_create_local();
  if (snapshot == NULL || storage == NULL) {
    free(snapshot);
    qa_ui_source_token_memory_destroy(storage);
    qa_ui_script_token_release(value);
    return false;
  }
  snapshot->refs = 1;
  snapshot->storage = storage;
  qa_ui_source_token_copy_from(storage, token);
  snapshot->token = value;
  snapshot->subtype = subtype;
  snapshot->integer_value = integer_value;
  snapshot->float_value = float_value;
  if (!qa_ui_source_token_context_set(&snapshot->context, context->path,
                                      context->column,
                                      context->leading_whitespace,
                                      context->leading_whitespace_length)) {
    qa_ui_script_snapshot_release(snapshot);
    qa_ui_script_token_release(value);
    return false;
  }
  /* copy_from retained the source token's previous snapshot on storage;
   * the storage's link is the weak back-pointer to its own snapshot, so
   * drop that strong reference before replacing it (otherwise every
   * re-snapshotted token, as #define expansion makes, pins the chain). */
  set_snapshot(storage, NULL);
  storage->snapshot = snapshot;
  storage->snapshot_weak = true;
  /* The token's creation reference belongs to the snapshot; the
   * returned record takes one more. */
  value->snapshot = snapshot;
  set_snapshot(token, snapshot);
  qa_ui_script_snapshot_release(snapshot); /* drop the creation ref */
  out->token = qa_ui_script_token_retain(value);
  out->subtype = subtype;
  out->integer_value = integer_value;
  out->float_value = float_value;
  return true;
}
