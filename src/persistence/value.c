/* Port of src/persistence/value.ts. GPL-2.0-or-later. */
#include "qa/persistence/value.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static noreturn void save_format_errorv(const char *path, const char *format, va_list args) {
  char text[1024];
  vsnprintf(text, sizeof text, format, args);
  qa_raise("SaveFormatError", 0, 0, "%s: %s", path, text);
}
noreturn void qa_save_format_error(const char *path, const char *format, ...) {
  va_list args;
  va_start(args, format);
  save_format_errorv(path, format, args);
}

/* JSON.parse / fatal TextDecoder failures raise a SyntaxError / TypeError, matching
 * the TS runtime error shape; quiet parses (TS try/catch sites) catch them. */
static noreturn void syntax_error_message(const char *message) {
  qa_raise("SyntaxError", 0, 0, "JSON Parse error: %s", message);
}

static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    memcpy(out, text, length);
    out[length] = '\0';
  }
  return out;
}
static char *dup_text(const char *text) { return dup_range(text, strlen(text)); }

static qa_save_value *value_new(qa_save_value_kind kind) {
  qa_save_value *value = (qa_save_value *)calloc(1, sizeof *value);
  if (value != NULL) value->kind = kind;
  return value;
}
/* JS own-key order (Object.keys, JSON.stringify): array-index keys
 * ("0" or [1-9][0-9]* below 2^32 - 1) ascending, then the other keys in
 * insertion order. */
static bool js_array_index_key(const char *key, uint32_t *out) {
  if (key[0] == '\0' || (key[0] == '0' && key[1] != '\0')) return false;
  uint64_t value = 0;
  for (const char *p = key; *p != '\0'; p++) {
    if (*p < '0' || *p > '9') return false;
    value = value * 10 + (uint64_t)(*p - '0');
    if (value > 4294967294u) return false;
  }
  *out = (uint32_t)value;
  return true;
}

static void js_order_entries(qa_save_entry *entries, size_t count) {
  /* Stable insertion sort: index keys first by value; others keep order. */
  for (size_t i = 1; i < count; i++) {
    qa_save_entry entry = entries[i];
    uint32_t value = 0;
    if (!js_array_index_key(entry.key, &value)) continue;
    size_t j = i;
    while (j > 0) {
      uint32_t other = 0;
      bool other_index = js_array_index_key(entries[j - 1].key, &other);
      if (other_index && other <= value) break;
      entries[j] = entries[j - 1];
      j--;
    }
    entries[j] = entry;
  }
}

qa_save_value *qa_save_value_undefined(void) { return value_new(QA_SAVE_UNDEFINED); }
qa_save_value *qa_save_value_null(void) { return value_new(QA_SAVE_NULL); }
qa_save_value *qa_save_value_boolean(bool b) {
  qa_save_value *v = value_new(QA_SAVE_BOOLEAN);
  if (v != NULL) v->boolean = b;
  return v;
}
qa_save_value *qa_save_value_number(double n) {
  qa_save_value *v = value_new(QA_SAVE_NUMBER);
  if (v != NULL) v->number = n;
  return v;
}
qa_save_value *qa_save_value_bigint_text(const char *decimal) {
  qa_save_value *v = value_new(QA_SAVE_BIGINT);
  if (v == NULL) return NULL;
  v->text = dup_text(decimal);
  if (v->text == NULL) { free(v); return NULL; }
  return v;
}
qa_save_value *qa_save_value_bigint_i64(int64_t n) {
  char buffer[32];
  (void)snprintf(buffer, sizeof buffer, "%lld", (long long)n);
  return qa_save_value_bigint_text(buffer);
}
qa_save_value *qa_save_value_string(const char *s) {
  qa_save_value *v = value_new(QA_SAVE_STRING);
  if (v == NULL) return NULL;
  v->text = dup_text(s);
  if (v->text == NULL) { free(v); return NULL; }
  return v;
}
qa_save_value *qa_save_value_bytes(const uint8_t *data, size_t length) {
  qa_save_value *v = value_new(QA_SAVE_BYTES);
  if (v == NULL) return NULL;
  if (length > 0) {
    v->bytes = (uint8_t *)malloc(length);
    if (v->bytes == NULL) { free(v); return NULL; }
    memcpy(v->bytes, data, length);
    v->byte_length = length;
  }
  return v;
}
qa_save_value *qa_save_value_array(void) { return value_new(QA_SAVE_ARRAY); }
qa_save_value *qa_save_value_object(void) { return value_new(QA_SAVE_OBJECT); }

bool qa_save_value_push(qa_save_value *array, qa_save_value *child) {
  qa_save_value **items = (qa_save_value **)realloc(
      array->items, (array->item_count + 1) * sizeof *items);
  if (items == NULL) return false;
  array->items = items;
  array->items[array->item_count++] = child;
  return true;
}
bool qa_save_value_set(qa_save_value *object, const char *key, qa_save_value *child) {
  char *copy = dup_text(key);
  if (copy == NULL) return false;
  qa_save_entry *entries = (qa_save_entry *)realloc(
      object->entries, (object->entry_count + 1) * sizeof *entries);
  if (entries == NULL) { free(copy); return false; }
  object->entries = entries;
  object->entries[object->entry_count].key = copy;
  object->entries[object->entry_count].value = child;
  object->entry_count++;
  return true;
}
void qa_save_value_free(qa_save_value *value) {
  if (value == NULL) return;
  free(value->text);
  free(value->bytes);
  for (size_t i = 0; i < value->item_count; i++) qa_save_value_free(value->items[i]);
  free(value->items);
  for (size_t i = 0; i < value->entry_count; i++) {
    free(value->entries[i].key);
    qa_save_value_free(value->entries[i].value);
  }
  free(value->entries);
  free(value->attachment);
  free(value);
}
bool qa_save_value_attach(qa_save_value *value, const void *data, size_t length) {
  void *copy = malloc(length ? length : 1);
  if (copy == NULL) return false;
  memcpy(copy, data, length);
  free(value->attachment);
  value->attachment = copy;
  value->attachment_length = length;
  return true;
}
const void *qa_save_value_attachment(const qa_save_value *value, size_t length) {
  return value != NULL && value->attachment_length == length ? value->attachment : NULL;
}
const qa_save_value *qa_save_value_get(const qa_save_value *object, const char *key) {
  if (object == NULL || object->kind != QA_SAVE_OBJECT) return NULL;
  for (size_t i = 0; i < object->entry_count; i++)
    if (strcmp(object->entries[i].key, key) == 0) return object->entries[i].value;
  return NULL;
}

/* --- canonical base64 (TS canonicalBase64) --- */
static bool canonical_base64(const char *value, size_t length) {
  if (length % 4 != 0) return false;
  size_t padding = 0;
  if (length >= 2 && value[length - 1] == '=' && value[length - 2] == '=') padding = 2;
  else if (length >= 1 && value[length - 1] == '=') padding = 1;
  size_t end = length - padding;
  int last_digit = 0;
  for (size_t i = 0; i < end; i++) {
    unsigned char code = (unsigned char)value[i];
    if (code >= 65 && code <= 90) last_digit = code - 65;
    else if (code >= 97 && code <= 122) last_digit = code - 71;
    else if (code >= 48 && code <= 57) last_digit = code + 4;
    else if (code == 43) last_digit = 62;
    else if (code == 47) last_digit = 63;
    else return false;
  }
  if (padding == 2) return (last_digit & 15) == 0;
  if (padding == 1) return (last_digit & 3) == 0;
  return true;
}
static const char base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int base64_digit(unsigned char code) {
  if (code >= 'A' && code <= 'Z') return code - 'A';
  if (code >= 'a' && code <= 'z') return code - 'a' + 26;
  if (code >= '0' && code <= '9') return code - '0' + 52;
  if (code == '+') return 62;
  return 63;
}
static char *base64_encode(const uint8_t *data, size_t length) {
  size_t out_length = ((length + 2) / 3) * 4;
  char *out = (char *)malloc(out_length + 1);
  if (out == NULL) return NULL;
  size_t o = 0;
  for (size_t i = 0; i < length; i += 3) {
    size_t remaining = length - i;
    unsigned a = data[i];
    unsigned b = remaining > 1 ? data[i + 1] : 0;
    unsigned c = remaining > 2 ? data[i + 2] : 0;
    out[o++] = base64_alphabet[a >> 2];
    out[o++] = base64_alphabet[((a & 3) << 4) | (b >> 4)];
    out[o++] = remaining > 1 ? base64_alphabet[((b & 15) << 2) | (c >> 6)] : '=';
    out[o++] = remaining > 2 ? base64_alphabet[c & 63] : '=';
  }
  out[o] = '\0';
  return out;
}
/* Caller checked canonical_base64. */
static uint8_t *base64_decode(const char *text, size_t length, size_t *out_length) {
  size_t padding = 0;
  if (length >= 2 && text[length - 1] == '=' && text[length - 2] == '=') padding = 2;
  else if (length >= 1 && text[length - 1] == '=') padding = 1;
  size_t total = (length / 4) * 3 - padding;
  uint8_t *out = total > 0 ? (uint8_t *)malloc(total) : NULL;
  if (total > 0 && out == NULL) return NULL;
  size_t o = 0;
  for (size_t i = 0; i < length; i += 4) {
    int a = base64_digit((unsigned char)text[i]);
    int b = base64_digit((unsigned char)text[i + 1]);
    int c = text[i + 2] == '=' ? 0 : base64_digit((unsigned char)text[i + 2]);
    int d = text[i + 3] == '=' ? 0 : base64_digit((unsigned char)text[i + 3]);
    if (o < total) out[o++] = (uint8_t)((a << 2) | (b >> 4));
    if (o < total) out[o++] = (uint8_t)(((b & 15) << 4) | (c >> 2));
    if (o < total) out[o++] = (uint8_t)(((c & 3) << 6) | d);
  }
  *out_length = total;
  return out;
}

/* --- growable text buffer for JSON output --- */
typedef struct text_buffer { char *data; size_t length; size_t capacity; } text_buffer;
static bool text_reserve(text_buffer *b, size_t extra) {
  if (b->length + extra + 1 <= b->capacity) return true;
  size_t cap = b->capacity == 0 ? 256 : b->capacity;
  while (cap < b->length + extra + 1) cap *= 2;
  char *data = (char *)realloc(b->data, cap);
  if (data == NULL) return false;
  b->data = data;
  b->capacity = cap;
  return true;
}
static bool text_add(text_buffer *b, const char *text, size_t length) {
  if (!text_reserve(b, length)) return false;
  memcpy(b->data + b->length, text, length);
  b->length += length;
  b->data[b->length] = '\0';
  return true;
}
static bool text_add_cstr(text_buffer *b, const char *text) {
  return text_add(b, text, strlen(text));
}
static bool text_add_char(text_buffer *b, char c) { return text_add(b, &c, 1); }

/* JS String(finite number) via shortest round-trip decimal. */
static void format_js_number(double value, char *buffer, size_t size) {
  qa_net_js_number(value, buffer, size);
}

static bool write_json_string(text_buffer *out, const char *text) {
  static const char hex_digits[] = "0123456789abcdef";
  if (!text_add_char(out, '"')) return false;
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    bool ok;
    switch (*p) {
      case '"': ok = text_add_cstr(out, "\\\""); break;
      case '\\': ok = text_add_cstr(out, "\\\\"); break;
      case '\b': ok = text_add_cstr(out, "\\b"); break;
      case '\f': ok = text_add_cstr(out, "\\f"); break;
      case '\n': ok = text_add_cstr(out, "\\n"); break;
      case '\r': ok = text_add_cstr(out, "\\r"); break;
      case '\t': ok = text_add_cstr(out, "\\t"); break;
      default:
        if (*p < 0x20) {
          char escape[7] = {'\\', 'u', '0', '0', hex_digits[*p >> 4], hex_digits[*p & 0xf], '\0'};
          ok = text_add_cstr(out, escape);
        } else {
          ok = text_add_char(out, (char)*p);
        }
        break;
    }
    if (!ok) return false;
  }
  return text_add_char(out, '"');
}

/* TS pack(): writes the packed JSON directly. `path` is built lazily
 * only on the error paths that need it. */
static bool pack_value(const qa_save_value *value, text_buffer *out, char *path, size_t path_cap);
static size_t *js_key_order(const qa_save_value *object);
static void path_child(char *path, size_t cap, const char *suffix, char *out, size_t out_cap) {
  (void)snprintf(out, out_cap, "%s%s", path, suffix);
  (void)cap;
}
static bool js_array_index_key(const char *key, uint32_t *out);
static size_t *js_key_order(const qa_save_value *object) {
  size_t *order = (size_t *)malloc((object->entry_count + 1) * sizeof *order);
  if (order == NULL) return NULL;
  size_t n = 0;
  for (size_t i = 0; i < object->entry_count; i++) order[n++] = i;
  for (size_t i = 1; i < n; i++) {
    size_t entry = order[i];
    uint32_t value = 0;
    if (!js_array_index_key(object->entries[entry].key, &value)) continue;
    size_t j = i;
    while (j > 0) {
      uint32_t other = 0;
      if (js_array_index_key(object->entries[order[j - 1]].key, &other) && other <= value) break;
      order[j] = order[j - 1];
      j--;
    }
    order[j] = entry;
  }
  return order;
}

static bool pack_tagged(text_buffer *out, const char *tag, const char *encoded) {
  return text_add_cstr(out, "{\"$qts\":") && write_json_string(out, tag) &&
         text_add_cstr(out, ",\"value\":") && write_json_string(out, encoded) &&
         text_add_char(out, '}');
}
static bool pack_value(const qa_save_value *value, text_buffer *out, char *path, size_t path_cap) {
  char child[1024];
  switch (value->kind) {
    case QA_SAVE_NULL: return text_add_cstr(out, "null");
    case QA_SAVE_BOOLEAN: return text_add_cstr(out, value->boolean ? "true" : "false");
    case QA_SAVE_STRING: return write_json_string(out, value->text);
    case QA_SAVE_NUMBER: {
      if (isfinite(value->number) && !signbit(value->number) ) {
        char buffer[64];
        format_js_number(value->number, buffer, sizeof buffer);
        return text_add_cstr(out, buffer);
      }
      if (isfinite(value->number) && value->number != 0.0) {
        char buffer[64];
        format_js_number(value->number, buffer, sizeof buffer);
        return text_add_cstr(out, buffer);
      }
      const char *encoded = (value->number == 0.0 && signbit(value->number)) ? "-0"
          : isnan(value->number) ? "NaN" : value->number > 0 ? "Infinity" : "-Infinity";
      return pack_tagged(out, "number", encoded);
    }
    case QA_SAVE_BIGINT: return pack_tagged(out, "bigint", value->text);
    case QA_SAVE_BYTES: {
      char *encoded = base64_encode(value->bytes, value->byte_length);
      if (encoded == NULL && value->byte_length > 0) return false;
      bool ok = pack_tagged(out, "bytes", encoded != NULL ? encoded : "");
      free(encoded);
      return ok;
    }
    case QA_SAVE_ARRAY: {
      if (!text_add_char(out, '[')) return false;
      for (size_t i = 0; i < value->item_count; i++) {
        if (i > 0 && !text_add_char(out, ',')) return false;
        char suffix[32];
        (void)snprintf(suffix, sizeof suffix, "[%zu]", i);
        path_child(path, path_cap, suffix, child, sizeof child);
        if (!pack_value(value->items[i], out, child, sizeof child)) return false;
      }
      return text_add_char(out, ']');
    }
    case QA_SAVE_OBJECT: {
      for (size_t i = 0; i < value->entry_count; i++)
        if (strcmp(value->entries[i].key, "$qts") == 0)
          qa_save_format_error(path, "$qts is reserved by the save envelope");
      if (!text_add_char(out, '{')) return false;
      size_t *order = js_key_order(value);
      if (order == NULL) return false;
      qa_cleanup order_guard;
      qa_cleanup_push(&order_guard, free, order);
      bool ok = true;
      for (size_t n = 0; n < value->entry_count && ok; n++) {
        size_t i = order[n];
        if (n > 0 && !text_add_char(out, ',')) ok = false;
        else if (!write_json_string(out, value->entries[i].key)) ok = false;
        else if (!text_add_char(out, ':')) ok = false;
        else {
          (void)snprintf(child, sizeof child, "%s.%s", path, value->entries[i].key);
          if (!pack_value(value->entries[i].value, out, child, sizeof child)) ok = false;
        }
      }
      qa_cleanup_pop(&order_guard);
      free(order);
      return ok && text_add_char(out, '}');
    }
    default:
      qa_save_format_error(path, "checkpoint contains a non-data value");
  }
}

uint8_t *qa_encode_checkpoint_value(const qa_save_value *value, size_t *out_length) {
  text_buffer out = {NULL, 0, 0};
  char path[] = "checkpoint";
  if (!pack_value(value, &out, path, sizeof path)) { free(out.data); return NULL; }
  *out_length = out.length;
  return (uint8_t *)out.data;
}

/* --- strict JSON parser (JSON.parse over fatal UTF-8) ---
 * Produces a raw tree using only null/boolean/number/string/array/
 * object kinds; unpack() then interprets $qts tags. Strings are
 * stored as UTF-8 bytes; \u escapes (incl. surrogate pairs and lone
 * surrogates as WTF-8) are encoded as UTF-8. */
static bool utf8_valid(const uint8_t *bytes, size_t length) {
  size_t i = 0;
  while (i < length) {
    uint8_t c = bytes[i];
    if (c < 0x80) { i++; continue; }
    size_t extra; uint32_t cp; uint32_t min;
    if ((c & 0xe0) == 0xc0) { extra = 1; cp = c & 0x1f; min = 0x80; }
    else if ((c & 0xf0) == 0xe0) { extra = 2; cp = c & 0x0f; min = 0x800; }
    else if ((c & 0xf8) == 0xf0) { extra = 3; cp = c & 0x07; min = 0x10000; }
    else return false;
    if (i + extra >= length + 0 && i + extra > length - 1) return false;
    for (size_t j = 1; j <= extra; j++) {
      if (i + j >= length || (bytes[i + j] & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (uint32_t)(bytes[i + j] & 0x3f);
    }
    if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    i += extra + 1;
  }
  return true;
}

/* JavaScriptCore LiteralParser (strict JSON), which is what Bun's JSON.parse
 * runs: the lexer stays one token ahead of the parser, and a failure reports
 * "JSON Parse error: " plus the lexer's message when the current token is a
 * lexer error, else the parser's message, else (trailing tokens) "Unable to
 * parse JSON string". Offsets are UTF-8 bytes; the texts JSC prints per UTF-16
 * unit print the whole UTF-8 character here. */
typedef enum json_token {
  JT_LBRACKET, JT_RBRACKET, JT_LPAREN, JT_RPAREN, JT_LBRACE, JT_RBRACE,
  JT_COMMA, JT_COLON, JT_ASSIGN, JT_SEMI, JT_DOT, JT_STRING, JT_NUMBER,
  JT_TRUE, JT_FALSE, JT_NULL, JT_IDENTIFIER, JT_END, JT_ERROR
} json_token;
typedef struct json_parser {
  const char *text;
  size_t offset;
  size_t length;
  int depth;
  json_token token;
  size_t token_start, token_end;
  char *token_string; /* decoded JT_STRING text, owned until taken */
  double token_number;
  char lex_error[160];
} json_parser;
/* JSON.parse throws RangeError on stack exhaustion in TS, and a packet of open brackets
 * reaches this parser from the wire; the recursion here and in unpack_value and
 * qa_save_value_free stays well inside a default thread stack at this depth. */
#define JSON_MAXIMUM_DEPTH 1024
static void cleanup_value(void *arg) { qa_save_value_free(arg); }
static void cleanup_text(void *arg) { free(arg); }
static void cleanup_token(void *arg) {
  json_parser *p = arg;
  free(p->token_string);
  p->token_string = NULL;
}
static bool encode_utf8(text_buffer *out, uint32_t cp) {
  char buf[4]; size_t n;
  if (cp < 0x80) { buf[0] = (char)cp; n = 1; }
  else if (cp < 0x800) { buf[0] = (char)(0xc0 | (cp >> 6)); buf[1] = (char)(0x80 | (cp & 0x3f)); n = 2; }
  else if (cp < 0x10000) { buf[0] = (char)(0xe0 | (cp >> 12)); buf[1] = (char)(0x80 | ((cp >> 6) & 0x3f)); buf[2] = (char)(0x80 | (cp & 0x3f)); n = 3; }
  else { buf[0] = (char)(0xf0 | (cp >> 18)); buf[1] = (char)(0x80 | ((cp >> 12) & 0x3f)); buf[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); buf[3] = (char)(0x80 | (cp & 0x3f)); n = 4; }
  return text_add(out, buf, n);
}
static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
/* Byte length of the UTF-8 character at offset (1 for a stray byte). */
static size_t json_char_length(const json_parser *p, size_t offset) {
  unsigned char c = (unsigned char)p->text[offset];
  size_t n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
  return offset + n <= p->length ? n : p->length - offset;
}
static json_token lex_fail(json_parser *p, const char *message) {
  snprintf(p->lex_error, sizeof p->lex_error, "%s", message);
  p->token = JT_ERROR;
  return JT_ERROR;
}
static json_token lex_fail_char(json_parser *p, const char *prefix, size_t at, const char *suffix) {
  snprintf(p->lex_error, sizeof p->lex_error, "%s%.*s%s", prefix,
           (int)json_char_length(p, at), p->text + at, suffix);
  p->token = JT_ERROR;
  return JT_ERROR;
}
static bool json_digit(const json_parser *p, size_t at) {
  return at < p->length && p->text[at] >= '0' && p->text[at] <= '9';
}
/* -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)? ; an exponent marker not
 * followed by digits ends the number before it. */
static json_token lex_number(json_parser *p) {
  size_t at = p->offset;
  if (at < p->length && p->text[at] == '-') at++;
  if (at < p->length && p->text[at] == '0') at++;
  else if (at < p->length && p->text[at] >= '1' && p->text[at] <= '9') {
    while (json_digit(p, at)) at++;
  } else {
    return lex_fail(p, "Invalid number");
  }
  if (at < p->length && p->text[at] == '.') {
    at++;
    if (!json_digit(p, at)) return lex_fail(p, "Invalid digits after decimal point");
    while (json_digit(p, at)) at++;
  }
  if (at < p->length && (p->text[at] == 'e' || p->text[at] == 'E')) {
    size_t exponent = at + 1;
    if (exponent < p->length && (p->text[exponent] == '+' || p->text[exponent] == '-')) exponent++;
    if (json_digit(p, exponent)) {
      at = exponent;
      while (json_digit(p, at)) at++;
    }
  }
  char *token = dup_range(p->text + p->offset, at - p->offset);
  if (token == NULL) qa_range_error("Out of memory");
  p->token_number = strtod(token, NULL);
  free(token);
  p->offset = at;
  p->token = JT_NUMBER;
  return JT_NUMBER;
}
/* UTF-16 units in the 4-unit window after "\u", as UTF-8 bytes. */
static size_t escape_window(const json_parser *p, size_t at, size_t *units) {
  size_t bytes = 0;
  *units = 0;
  while (*units < 4 && at + bytes < p->length) {
    size_t n = json_char_length(p, at + bytes);
    *units += n == 4 ? 2 : 1;
    bytes += n;
  }
  return bytes;
}
static json_token lex_string(json_parser *p) {
  size_t at = p->offset + 1;
  text_buffer out = {NULL, 0, 0};
  for (;;) {
    size_t run = at;
    while (at < p->length) {
      unsigned char c = (unsigned char)p->text[at];
      if (c < 0x20 || c == '"' || c == '\\') break;
      at++;
    }
    if (at > run && !text_add(&out, p->text + run, at - run)) qa_range_error("Out of memory");
    if (at >= p->length) { free(out.data); return lex_fail(p, "Unterminated string"); }
    char c = p->text[at];
    if (c == '"') { at++; break; }
    if (c != '\\') { free(out.data); return lex_fail(p, "Unterminated string"); }
    at++;
    if (at >= p->length) { free(out.data); return lex_fail(p, "Unterminated string"); }
    char e = p->text[at];
    char simple = e == '"' ? '"' : e == '\\' ? '\\' : e == '/' ? '/' : e == 'b' ? '\b'
                : e == 'f' ? '\f' : e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : 0;
    if (simple != 0) {
      if (!text_add_char(&out, simple)) qa_range_error("Out of memory");
      at++;
      continue;
    }
    if (e != 'u') {
      free(out.data);
      return lex_fail_char(p, "Invalid escape character ", at, "");
    }
    size_t units;
    size_t window = escape_window(p, at + 1, &units);
    if (units < 4) { free(out.data); return lex_fail(p, "\\u must be followed by 4 hex digits"); }
    uint32_t cp = 0;
    bool hex = window == 4;
    for (size_t i = 0; hex && i < 4; i++) {
      int d = hex_value(p->text[at + 1 + i]);
      if (d < 0) hex = false;
      else cp = (cp << 4) | (uint32_t)d;
    }
    if (!hex) {
      free(out.data);
      snprintf(p->lex_error, sizeof p->lex_error, "\"\\u%.*s\" is not a valid unicode escape",
               (int)window, p->text + at + 1);
      p->token = JT_ERROR;
      return JT_ERROR;
    }
    at += 5;
    /* A high surrogate joins a following \u low surrogate; lone halves stay (WTF-8). */
    if (cp >= 0xd800 && cp <= 0xdbff && at + 6 <= p->length && p->text[at] == '\\' &&
        p->text[at + 1] == 'u') {
      uint32_t low = 0;
      bool ok = true;
      for (size_t i = 0; ok && i < 4; i++) {
        int d = hex_value(p->text[at + 2 + i]);
        if (d < 0) ok = false;
        else low = (low << 4) | (uint32_t)d;
      }
      if (ok && low >= 0xdc00 && low <= 0xdfff) {
        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
        at += 6;
      }
    }
    if (!encode_utf8(&out, cp)) qa_range_error("Out of memory");
  }
  p->token_string = out.data != NULL ? out.data : dup_text("");
  if (p->token_string == NULL) qa_range_error("Out of memory");
  p->offset = at;
  p->token = JT_STRING;
  return JT_STRING;
}
static bool json_literal(json_parser *p, const char *word, size_t length) {
  return p->length - p->offset >= length && memcmp(p->text + p->offset, word, length) == 0;
}
static json_token json_lex(json_parser *p) {
  free(p->token_string);
  p->token_string = NULL;
  while (p->offset < p->length) {
    char c = p->text[p->offset];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->offset++;
    else break;
  }
  p->token_start = p->offset;
  if (p->offset >= p->length) { p->token = JT_END; return JT_END; }
  char c = p->text[p->offset];
  json_token single = c == '[' ? JT_LBRACKET : c == ']' ? JT_RBRACKET : c == '(' ? JT_LPAREN
                    : c == ')' ? JT_RPAREN : c == '{' ? JT_LBRACE : c == '}' ? JT_RBRACE
                    : c == ',' ? JT_COMMA : c == ':' ? JT_COLON : c == '=' ? JT_ASSIGN
                    : c == ';' ? JT_SEMI : c == '.' ? JT_DOT : JT_ERROR;
  if (single != JT_ERROR) {
    p->offset++;
    p->token = single;
    return single;
  }
  if (c == '"') return lex_string(p);
  if (c == '-' || (c >= '0' && c <= '9')) return lex_number(p);
  if (c == 't' && json_literal(p, "true", 4)) { p->offset += 4; p->token = JT_TRUE; return JT_TRUE; }
  if (c == 'f' && json_literal(p, "false", 5)) { p->offset += 5; p->token = JT_FALSE; return JT_FALSE; }
  if (c == 'n' && json_literal(p, "null", 4)) { p->offset += 4; p->token = JT_NULL; return JT_NULL; }
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$') {
    while (p->offset < p->length) {
      char d = p->text[p->offset];
      if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || (d >= '0' && d <= '9') || d == '_' || d == '$')
        p->offset++;
      else break;
    }
    p->token_end = p->offset;
    p->token = JT_IDENTIFIER;
    return JT_IDENTIFIER;
  }
  if (c == '\'') return lex_fail(p, "Single quotes (') are not allowed in JSON");
  return lex_fail_char(p, "Unrecognized token '", p->offset, "'");
}
/* LiteralParser::getErrorMessage: the lexer's message wins. */
static noreturn void json_fail(json_parser *p, const char *message) {
  if (p->token == JT_ERROR) syntax_error_message(p->lex_error);
  syntax_error_message(message);
}
static noreturn void json_unexpected(json_parser *p) {
  char message[256];
  char token = 0;
  switch (p->token) {
    case JT_END: json_fail(p, "Unexpected EOF");
    case JT_IDENTIFIER:
      snprintf(message, sizeof message, "Unexpected identifier \"%.*s\"",
               (int)(p->token_end - p->token_start), p->text + p->token_start);
      json_fail(p, message);
    case JT_LBRACKET: token = '['; break;
    case JT_RBRACKET: token = ']'; break;
    case JT_LPAREN: token = '('; break;
    case JT_RPAREN: token = ')'; break;
    case JT_LBRACE: token = '{'; break;
    case JT_RBRACE: token = '}'; break;
    case JT_COMMA: token = ','; break;
    case JT_COLON: token = ':'; break;
    case JT_ASSIGN: token = '='; break;
    case JT_SEMI: token = ';'; break;
    case JT_DOT: token = '.'; break;
    default: json_fail(p, "Unable to parse JSON string");
  }
  snprintf(message, sizeof message, "Unexpected token '%c'", token);
  json_fail(p, message);
}
static qa_save_value *json_value(json_parser *p);
static qa_save_value *json_object_body(json_parser *p, qa_save_value *v) {
  json_lex(p);
  if (p->token == JT_RBRACE) { json_lex(p); return v; }
  if (p->token != JT_STRING) json_fail(p, "Expected '}'");
  for (;;) {
    char *key = p->token_string;
    p->token_string = NULL;
    qa_cleanup key_guard;
    qa_cleanup_push(&key_guard, cleanup_text, key);
    json_lex(p);
    if (p->token != JT_COLON) json_fail(p, "Expected ':' before value in object property definition");
    json_lex(p);
    qa_save_value *child = json_value(p);
    qa_cleanup_pop(&key_guard);
    if (child == NULL) { free(key); return NULL; }
    /* JS objects keep the last duplicate key; replace in place. */
    bool replaced = false;
    for (size_t i = 0; i < v->entry_count; i++) {
      if (strcmp(v->entries[i].key, key) == 0) {
        qa_save_value_free(v->entries[i].value);
        v->entries[i].value = child;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      qa_save_entry *entries = (qa_save_entry *)realloc(v->entries, (v->entry_count + 1) * sizeof *entries);
      if (entries == NULL) { free(key); qa_save_value_free(child); return NULL; }
      v->entries = entries;
      v->entries[v->entry_count].key = key;
      v->entries[v->entry_count].value = child;
      v->entry_count++;
    } else free(key);
    if (p->token == JT_COMMA) {
      json_lex(p);
      if (p->token != JT_STRING) json_fail(p, "Property name must be a string literal");
      continue;
    }
    if (p->token == JT_RBRACE) {
      json_lex(p);
      js_order_entries(v->entries, v->entry_count); /* JSON.parse result key order */
      return v;
    }
    json_fail(p, "Expected '}'");
  }
}
static qa_save_value *json_array_body(json_parser *p, qa_save_value *v) {
  json_lex(p);
  if (p->token == JT_RBRACKET) { json_lex(p); return v; }
  for (;;) {
    qa_save_value *child = json_value(p);
    if (child == NULL) return NULL;
    if (!qa_save_value_push(v, child)) { qa_save_value_free(child); return NULL; }
    if (p->token == JT_COMMA) {
      json_lex(p);
      if (p->token == JT_RBRACKET) json_fail(p, "Unexpected comma at the end of array expression");
      continue;
    }
    if (p->token == JT_RBRACKET) { json_lex(p); return v; }
    json_fail(p, "Expected ']'");
  }
}
/* Parses the value at the current token and leaves the next token current. */
static qa_save_value *json_value(json_parser *p) {
  json_token token = p->token;
  if (token == JT_STRING) {
    qa_save_value *v = value_new(QA_SAVE_STRING);
    if (v == NULL) return NULL;
    v->text = p->token_string;
    p->token_string = NULL;
    qa_cleanup guard;
    qa_cleanup_push(&guard, cleanup_value, v);
    json_lex(p);
    qa_cleanup_pop(&guard);
    return v;
  }
  if (token == JT_LBRACE || token == JT_LBRACKET) {
    if (p->depth >= JSON_MAXIMUM_DEPTH) qa_range_error("Maximum call stack size exceeded.");
    p->depth++;
    qa_save_value *v = value_new(token == JT_LBRACE ? QA_SAVE_OBJECT : QA_SAVE_ARRAY);
    if (v == NULL) { p->depth--; return NULL; }
    qa_cleanup guard;
    qa_cleanup_push(&guard, cleanup_value, v);
    qa_save_value *result = token == JT_LBRACE ? json_object_body(p, v) : json_array_body(p, v);
    qa_cleanup_pop(&guard);
    if (result == NULL) qa_save_value_free(v);
    p->depth--;
    return result;
  }
  qa_save_value *v;
  if (token == JT_NUMBER) v = qa_save_value_number(p->token_number);
  else if (token == JT_TRUE) v = qa_save_value_boolean(true);
  else if (token == JT_FALSE) v = qa_save_value_boolean(false);
  else if (token == JT_NULL) v = value_new(QA_SAVE_NULL);
  else json_unexpected(p);
  if (v == NULL) return NULL;
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_value, v);
  json_lex(p);
  qa_cleanup_pop(&guard);
  return v;
}
/* LiteralParser::tryLiteralParse (StrictJSON): one value, then the end. */
static qa_save_value *json_document(json_parser *p) {
  json_lex(p);
  qa_save_value *parsed = json_value(p);
  if (parsed == NULL) return NULL;
  if (p->token != JT_END) {
    qa_cleanup guard;
    qa_cleanup_push(&guard, cleanup_value, parsed);
    json_fail(p, "Unable to parse JSON string");
  }
  return parsed;
}

/* TS unpack(): transforms the raw tree in place where possible. */
static bool valid_bigint_text(const char *text) {
  size_t i = 0;
  if (text[i] == '-') i++;
  if (text[i] == '\0') return false;
  if (text[i] == '0') return text[i + 1] == '\0';
  if (text[i] < '1' || text[i] > '9') return false;
  for (i++; text[i] != '\0'; i++) if (text[i] < '0' || text[i] > '9') return false;
  return true;
}
/* Returns replacement (may free input); NULL only on OOM. */
static qa_save_value *unpack_value(qa_save_value *value, const char *path) {
  char child_path[1024];
  if (value->kind == QA_SAVE_ARRAY) {
    for (size_t i = 0; i < value->item_count; i++) {
      (void)snprintf(child_path, sizeof child_path, "%s[%zu]", path, i);
      qa_save_value *child = unpack_value(value->items[i], child_path);
      if (child == NULL) { qa_save_value_free(value); return NULL; }
      value->items[i] = child;
    }
    return value;
  }
  if (value->kind != QA_SAVE_OBJECT) return value;
  const qa_save_value *tag = qa_save_value_get(value, "$qts");
  if (tag != NULL) {
    const qa_save_value *encoded = qa_save_value_get(value, "value");
    if (encoded == NULL || encoded->kind != QA_SAVE_STRING || value->entry_count != 2)
      qa_save_format_error(path, "invalid tagged checkpoint value");
    const char *tag_text = tag->kind == QA_SAVE_STRING ? tag->text : NULL;
    const char *enc = encoded->text;
    qa_save_value *result;
    if (tag_text != NULL && strcmp(tag_text, "bigint") == 0 && valid_bigint_text(enc))
      result = qa_save_value_bigint_text(enc);
    else if (tag_text != NULL && strcmp(tag_text, "bytes") == 0 && canonical_base64(enc, strlen(enc))) {
      size_t length = 0;
      uint8_t *data = base64_decode(enc, strlen(enc), &length);
      if (length > 0 && data == NULL) { qa_save_value_free(value); return NULL; }
      result = value_new(QA_SAVE_BYTES);
      if (result != NULL) { result->bytes = data; result->byte_length = length; }
      else free(data);
    } else if (tag_text != NULL && strcmp(tag_text, "number") == 0 && strcmp(enc, "-0") == 0)
      result = qa_save_value_number(-0.0);
    else if (tag_text != NULL && strcmp(tag_text, "number") == 0 && strcmp(enc, "NaN") == 0)
      result = qa_save_value_number(NAN);
    else if (tag_text != NULL && strcmp(tag_text, "number") == 0 && strcmp(enc, "Infinity") == 0)
      result = qa_save_value_number(INFINITY);
    else if (tag_text != NULL && strcmp(tag_text, "number") == 0 && strcmp(enc, "-Infinity") == 0)
      result = qa_save_value_number(-INFINITY);
    else
      qa_save_format_error(path, "unknown tagged checkpoint value");
    qa_save_value_free(value);
    return result;
  }
  for (size_t i = 0; i < value->entry_count; i++) {
    (void)snprintf(child_path, sizeof child_path, "%s.%s", path, value->entries[i].key);
    qa_save_value *child = unpack_value(value->entries[i].value, child_path);
    if (child == NULL) { qa_save_value_free(value); return NULL; }
    value->entries[i].value = child;
  }
  return value;
}

static void cleanup_value_slot(void *arg) { qa_save_value_free(*(qa_save_value **)arg); }
typedef struct decode_job {
  const uint8_t *bytes;
  size_t length;
  qa_save_value *parsed;
  bool oom;
} decode_job;

static void decode_parse_run(void *raw) {
  decode_job *job = raw;
  char *text = dup_range((const char *)job->bytes, job->length);
  if (text == NULL) { job->oom = true; return; }
  qa_cleanup text_guard;
  qa_cleanup_push(&text_guard, cleanup_text, text);
  json_parser parser;
  memset(&parser, 0, sizeof parser);
  parser.text = text;
  parser.length = job->length;
  qa_cleanup token_guard;
  qa_cleanup_push(&token_guard, cleanup_token, &parser);
  qa_save_value *parsed = json_document(&parser);
  qa_cleanup_pop(&token_guard);
  cleanup_token(&parser);
  if (parsed == NULL) { qa_cleanup_pop(&text_guard); free(text); job->oom = true; return; }
  qa_cleanup_pop(&text_guard);
  free(text);
  job->parsed = parsed;
}

/* TS decodeCheckpointValue wraps the fatal TextDecoder and JSON.parse
 * failures as SaveFormatError("checkpoint", error.message); both texts are
 * Bun's (the parser above reproduces JavaScriptCore's JSON.parse messages). */
qa_save_value *qa_decode_checkpoint_value(const uint8_t *bytes, size_t length) {
  if (!utf8_valid(bytes, length)) qa_save_format_error("checkpoint", "Invalid byte sequence");
  decode_job job = {bytes, length, NULL, false};
  qa_caught_error caught;
  if (!qa_try(decode_parse_run, &job, &caught)) {
    if (strcmp(caught.name, "SyntaxError") != 0) qa_rethrow(&caught);
    char message[QA_CAUGHT_MESSAGE_CAPACITY];
    snprintf(message, sizeof message, "%s", caught.message);
    qa_save_format_error("checkpoint", "%s", message);
  }
  if (job.oom) return NULL;
  /* unpack_value frees replaced nodes as it goes; a raise in it leaves the
   * tree as it was then, released by this guard. */
  qa_save_value *parsed = job.parsed;
  qa_cleanup parsed_guard;
  qa_cleanup_push(&parsed_guard, cleanup_value_slot, &parsed);
  qa_save_value *result = unpack_value(parsed, "checkpoint");
  qa_cleanup_pop(&parsed_guard);
  return result;
}

typedef struct json_parse_scope { const char *text; size_t length; qa_save_value *parsed; } json_parse_scope;
static void json_parse_body(void *ctx) {
  json_parse_scope *scope = ctx;
  json_parser parser;
  memset(&parser, 0, sizeof parser);
  parser.text = scope->text;
  parser.length = scope->length;
  qa_cleanup token_guard;
  qa_cleanup_push(&token_guard, cleanup_token, &parser);
  qa_save_value *parsed = json_document(&parser);
  qa_cleanup_pop(&token_guard);
  cleanup_token(&parser);
  scope->parsed = parsed;
}
qa_save_value *qa_parse_json_value(const uint8_t *bytes, size_t length, bool quiet) {
  char *text = dup_range((const char *)bytes, length);
  if (text == NULL) return NULL;
  json_parse_scope scope = {text, length, NULL};
  qa_cleanup guard;
  qa_cleanup_push(&guard, cleanup_text, text);
  if (quiet) {
    qa_caught_error caught;
    if (!qa_try(json_parse_body, &scope, &caught)) scope.parsed = NULL;
  } else json_parse_body(&scope);
  qa_cleanup_pop(&guard);
  free(text);
  return scope.parsed;
}

/* --- SaveReader --- */
bool qa_save_reader_init(qa_save_reader *reader, const qa_save_value *value, const char *path) {
  reader->value = value;
  const char *text = path != NULL ? path : "save";
  size_t length = strlen(text);
  if (length >= sizeof reader->path) length = sizeof reader->path - 1;
  memcpy(reader->path, text, length);
  reader->path[length] = '\0';
  return true;
}
void qa_save_reader_destroy(qa_save_reader *reader) {
  reader->path[0] = '\0';
  reader->value = NULL;
}
noreturn void qa_save_reader_fail(const qa_save_reader *reader, const char *format, ...) {
  va_list args;
  va_start(args, format);
  save_format_errorv(reader->path, format, args);
}
static const qa_save_value *reader_value(const qa_save_reader *reader) { return reader->value; }
bool qa_save_reader_is_undefined(const qa_save_reader *reader) {
  return reader->value == NULL || reader->value->kind == QA_SAVE_UNDEFINED;
}
bool qa_save_reader_is_null(const qa_save_reader *reader) {
  return reader->value != NULL && reader->value->kind == QA_SAVE_NULL;
}
bool qa_save_reader_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_OBJECT)
    qa_save_reader_fail(reader, "expected a record");
  char path[1024];
  (void)snprintf(path, sizeof path, "%s.%s", reader->path, name);
  return qa_save_reader_init(out, qa_save_value_get(value, name), path);
}
size_t qa_save_reader_list_count(const qa_save_reader *reader) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_ARRAY)
    qa_save_reader_fail(reader, "expected an array");
  return value->item_count;
}
bool qa_save_reader_item(const qa_save_reader *reader, size_t index, qa_save_reader *out) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_ARRAY)
    qa_save_reader_fail(reader, "expected an array");
  char path[1024];
  (void)snprintf(path, sizeof path, "%s[%zu]", reader->path, index);
  return qa_save_reader_init(out, index < value->item_count ? value->items[index] : NULL, path);
}
const char *qa_save_reader_string(const qa_save_reader *reader) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_STRING)
    qa_save_reader_fail(reader, "expected a string");
  return value->text;
}
bool qa_save_reader_boolean(const qa_save_reader *reader) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_BOOLEAN)
    qa_save_reader_fail(reader, "expected a boolean");
  return value->boolean;
}
double qa_save_reader_number(const qa_save_reader *reader) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_NUMBER)
    qa_save_reader_fail(reader, "expected a number");
  return value->number;
}
double qa_save_reader_finite(const qa_save_reader *reader) {
  double value = qa_save_reader_number(reader);
  if (!isfinite(value)) qa_save_reader_fail(reader, "expected a finite number");
  return value;
}
double qa_save_reader_integer_min(const qa_save_reader *reader, double minimum) {
  double value = qa_save_reader_number(reader);
  if (!(value == trunc(value) && fabs(value) <= 9007199254740991.0) || value < minimum)
    qa_save_reader_fail(reader, "expected an integer in range");
  return value;
}
double qa_save_reader_integer(const qa_save_reader *reader) {
  return qa_save_reader_integer_min(reader, -9007199254740991.0);
}
const char *qa_save_reader_bigint_text(const qa_save_reader *reader) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_BIGINT)
    qa_save_reader_fail(reader, "expected a bigint");
  return value->text;
}
int64_t qa_save_reader_bigint(const qa_save_reader *reader) {
  return (int64_t)strtoll(qa_save_reader_bigint_text(reader), NULL, 10);
}
const uint8_t *qa_save_reader_bytes(const qa_save_reader *reader, size_t *out_length) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_BYTES)
    qa_save_reader_fail(reader, "expected raw checkpoint bytes");
  if (out_length != NULL) *out_length = value->byte_length;
  return value->bytes;
}
const char *qa_save_reader_literal_string(const qa_save_reader *reader, const char *expected) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_STRING || strcmp(value->text, expected) != 0)
    qa_save_reader_fail(reader, "expected %s", expected);
  return expected;
}
double qa_save_reader_literal_number(const qa_save_reader *reader, double expected) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_NUMBER || value->number != expected) {
    char buffer[64];
    format_js_number(expected, buffer, sizeof buffer);
    qa_save_reader_fail(reader, "expected %s", buffer);
  }
  return expected;
}
bool qa_save_reader_literal_boolean(const qa_save_reader *reader, bool expected) {
  const qa_save_value *value = reader_value(reader);
  if (value == NULL || value->kind != QA_SAVE_BOOLEAN || value->boolean != expected)
    qa_save_reader_fail(reader, "expected %s", expected ? "true" : "false");
  return expected;
}
const char *qa_save_reader_choice_string(const qa_save_reader *reader,
                                         const char *const *choices, size_t count) {
  const qa_save_value *value = reader_value(reader);
  if (value != NULL && value->kind == QA_SAVE_STRING)
    for (size_t i = 0; i < count; i++)
      if (strcmp(value->text, choices[i]) == 0) return choices[i];
  /* Build the TS "a or b or c" message; the buffer is released before the raise. */
  text_buffer message = {NULL, 0, 0};
  (void)text_add_cstr(&message, "expected ");
  for (size_t i = 0; i < count; i++) {
    if (i > 0) (void)text_add_cstr(&message, " or ");
    (void)text_add_cstr(&message, choices[i]);
  }
  char text[1024];
  snprintf(text, sizeof text, "%s", message.data != NULL ? message.data : "expected");
  free(message.data);
  qa_save_reader_fail(reader, "%s", text);
}
double qa_save_reader_choice_number(const qa_save_reader *reader,
                                    const double *choices, size_t count) {
  const qa_save_value *value = reader_value(reader);
  if (value != NULL && value->kind == QA_SAVE_NUMBER)
    for (size_t i = 0; i < count; i++)
      if (value->number == choices[i]) return choices[i];
  text_buffer message = {NULL, 0, 0};
  (void)text_add_cstr(&message, "expected ");
  for (size_t i = 0; i < count; i++) {
    char buffer[64];
    format_js_number(choices[i], buffer, sizeof buffer);
    if (i > 0) (void)text_add_cstr(&message, " or ");
    (void)text_add_cstr(&message, buffer);
  }
  char text[1024];
  snprintf(text, sizeof text, "%s", message.data != NULL ? message.data : "expected");
  free(message.data);
  qa_save_reader_fail(reader, "%s", text);
}
const char *qa_namespaced(const qa_save_reader *reader) {
  const char *value = qa_save_reader_string(reader);
  const char *colon = strchr(value, ':');
  if (colon == NULL || colon == value || colon[1] == '\0')
    qa_save_reader_fail(reader, "expected a namespaced identity");
  return value;
}



