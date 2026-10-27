/* Port of src/persistence/value.ts. GPL-2.0-or-later. */
#include "qa/persistence/value.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

noreturn void qa_save_format_error(const char *path, const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "SaveFormatError: %s: ", path);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

/* JSON.parse / fatal TextDecoder failures abort with a SyntaxError /
 * TypeError class name, matching the TS runtime error shape. Quiet
 * parses (TS try/catch sites) longjmp back instead. */
#include <setjmp.h>
static jmp_buf quiet_jump;
static bool quiet_active = false;
static noreturn void syntax_error(const char *message) {
  if (quiet_active) longjmp(quiet_jump, 1);
  fprintf(stderr, "SyntaxError: %s\n", message);
  abort();
}
static noreturn void type_error(const char *message) {
  fprintf(stderr, "TypeError: %s\n", message);
  abort();
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
  free(value);
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
  if (value == 0.0) { (void)snprintf(buffer, size, "0"); return; }
  if (value == trunc(value) && fabs(value) <= 9007199254740991.0) {
    (void)snprintf(buffer, size, "%lld", (long long)value);
    return;
  }
  for (int precision = 15; precision <= 17; precision++) {
    (void)snprintf(buffer, size, "%.*g", precision, value);
    if (strtod(buffer, NULL) == value) return;
  }
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
static void path_child(char *path, size_t cap, const char *suffix, char *out, size_t out_cap) {
  (void)snprintf(out, out_cap, "%s%s", path, suffix);
  (void)cap;
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
      for (size_t i = 0; i < value->entry_count; i++) {
        if (i > 0 && !text_add_char(out, ',')) return false;
        if (!write_json_string(out, value->entries[i].key)) return false;
        if (!text_add_char(out, ':')) return false;
        (void)snprintf(child, sizeof child, "%s.%s", path, value->entries[i].key);
        if (!pack_value(value->entries[i].value, out, child, sizeof child)) return false;
      }
      return text_add_char(out, '}');
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

typedef struct json_parser { const char *text; size_t offset; size_t length; } json_parser;
static qa_save_value *json_value(json_parser *p);
static void json_ws(json_parser *p) {
  while (p->offset < p->length) {
    char c = p->text[p->offset];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->offset++;
    else break;
  }
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
static uint32_t json_hex4(json_parser *p) {
  if (p->offset + 4 > p->length) syntax_error("Invalid Unicode escape");
  uint32_t value = 0;
  for (int i = 0; i < 4; i++) {
    int d = hex_value(p->text[p->offset++]);
    if (d < 0) syntax_error("Invalid Unicode escape");
    value = (value << 4) | (uint32_t)d;
  }
  return value;
}
/* Malloc'd decoded string; NULL only on OOM. Assumes current char '"'. */
static char *json_string(json_parser *p) {
  p->offset++;
  text_buffer out = {NULL, 0, 0};
  for (;;) {
    if (p->offset >= p->length) { free(out.data); syntax_error("Unterminated string"); }
    unsigned char c = (unsigned char)p->text[p->offset++];
    if (c == '"') break;
    if (c < 0x20) { free(out.data); syntax_error("Bad control character in string"); }
    if (c == '\\') {
      if (p->offset >= p->length) { free(out.data); syntax_error("Unterminated string"); }
      char e = p->text[p->offset++];
      bool ok = true;
      switch (e) {
        case '"': ok = text_add_char(&out, '"'); break;
        case '\\': ok = text_add_char(&out, '\\'); break;
        case '/': ok = text_add_char(&out, '/'); break;
        case 'b': ok = text_add_char(&out, '\b'); break;
        case 'f': ok = text_add_char(&out, '\f'); break;
        case 'n': ok = text_add_char(&out, '\n'); break;
        case 'r': ok = text_add_char(&out, '\r'); break;
        case 't': ok = text_add_char(&out, '\t'); break;
        case 'u': {
          uint32_t cp = json_hex4(p);
          if (cp >= 0xd800 && cp <= 0xdbff && p->offset + 1 < p->length &&
              p->text[p->offset] == '\\' && p->text[p->offset + 1] == 'u') {
            size_t save = p->offset;
            p->offset += 2;
            uint32_t lo = json_hex4(p);
            if (lo >= 0xdc00 && lo <= 0xdfff) cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
            else p->offset = save;
          }
          ok = encode_utf8(&out, cp);
          break;
        }
        default: free(out.data); syntax_error("Bad escape in string");
      }
      if (!ok) { free(out.data); return NULL; }
    } else if (!text_add_char(&out, (char)c)) {
      free(out.data);
      return NULL;
    }
  }
  if (out.data == NULL) return dup_text("");
  return out.data;
}
static qa_save_value *json_value(json_parser *p) {
  json_ws(p);
  if (p->offset >= p->length) syntax_error("Unexpected end of JSON input");
  char c = p->text[p->offset];
  if (c == '"') {
    char *s = json_string(p);
    if (s == NULL) return NULL;
    qa_save_value *v = value_new(QA_SAVE_STRING);
    if (v == NULL) { free(s); return NULL; }
    v->text = s;
    return v;
  }
  if (c == '{') {
    p->offset++;
    qa_save_value *v = value_new(QA_SAVE_OBJECT);
    if (v == NULL) return NULL;
    json_ws(p);
    if (p->offset < p->length && p->text[p->offset] == '}') { p->offset++; return v; }
    for (;;) {
      json_ws(p);
      if (p->offset >= p->length || p->text[p->offset] != '"') { qa_save_value_free(v); syntax_error("Expected object key"); }
      char *key = json_string(p);
      if (key == NULL) { qa_save_value_free(v); return NULL; }
      json_ws(p);
      if (p->offset >= p->length || p->text[p->offset] != ':') { free(key); qa_save_value_free(v); syntax_error("Expected ':' in object"); }
      p->offset++;
      qa_save_value *child = json_value(p);
      if (child == NULL) { free(key); qa_save_value_free(v); return NULL; }
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
        if (entries == NULL) { free(key); qa_save_value_free(child); qa_save_value_free(v); return NULL; }
        v->entries = entries;
        v->entries[v->entry_count].key = key;
        v->entries[v->entry_count].value = child;
        v->entry_count++;
      } else free(key);
      json_ws(p);
      if (p->offset < p->length && p->text[p->offset] == ',') { p->offset++; continue; }
      if (p->offset < p->length && p->text[p->offset] == '}') { p->offset++; return v; }
      qa_save_value_free(v);
      syntax_error("Expected ',' or '}' in object");
    }
  }
  if (c == '[') {
    p->offset++;
    qa_save_value *v = value_new(QA_SAVE_ARRAY);
    if (v == NULL) return NULL;
    json_ws(p);
    if (p->offset < p->length && p->text[p->offset] == ']') { p->offset++; return v; }
    for (;;) {
      qa_save_value *child = json_value(p);
      if (child == NULL || !qa_save_value_push(v, child)) {
        qa_save_value_free(child);
        qa_save_value_free(v);
        return NULL;
      }
      json_ws(p);
      if (p->offset < p->length && p->text[p->offset] == ',') { p->offset++; continue; }
      if (p->offset < p->length && p->text[p->offset] == ']') { p->offset++; return v; }
      qa_save_value_free(v);
      syntax_error("Expected ',' or ']' in array");
    }
  }
  if (p->offset + 4 <= p->length && memcmp(p->text + p->offset, "null", 4) == 0) {
    p->offset += 4;
    return value_new(QA_SAVE_NULL);
  }
  if (p->offset + 4 <= p->length && memcmp(p->text + p->offset, "true", 4) == 0) {
    p->offset += 4;
    return qa_save_value_boolean(true);
  }
  if (p->offset + 5 <= p->length && memcmp(p->text + p->offset, "false", 5) == 0) {
    p->offset += 5;
    return qa_save_value_boolean(false);
  }
  /* JSON number grammar: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)? */
  size_t start = p->offset;
  if (p->offset < p->length && p->text[p->offset] == '-') p->offset++;
  if (p->offset < p->length && p->text[p->offset] == '0') p->offset++;
  else {
    if (p->offset >= p->length || p->text[p->offset] < '1' || p->text[p->offset] > '9')
      syntax_error("Unexpected token in JSON");
    while (p->offset < p->length && p->text[p->offset] >= '0' && p->text[p->offset] <= '9') p->offset++;
  }
  if (p->offset < p->length && p->text[p->offset] == '.') {
    p->offset++;
    if (p->offset >= p->length || p->text[p->offset] < '0' || p->text[p->offset] > '9')
      syntax_error("Invalid JSON number");
    while (p->offset < p->length && p->text[p->offset] >= '0' && p->text[p->offset] <= '9') p->offset++;
  }
  if (p->offset < p->length && (p->text[p->offset] == 'e' || p->text[p->offset] == 'E')) {
    p->offset++;
    if (p->offset < p->length && (p->text[p->offset] == '+' || p->text[p->offset] == '-')) p->offset++;
    if (p->offset >= p->length || p->text[p->offset] < '0' || p->text[p->offset] > '9')
      syntax_error("Invalid JSON number");
    while (p->offset < p->length && p->text[p->offset] >= '0' && p->text[p->offset] <= '9') p->offset++;
  }
  char *token = dup_range(p->text + start, p->offset - start);
  if (token == NULL) return NULL;
  double number = strtod(token, NULL);
  free(token);
  return qa_save_value_number(number);
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

qa_save_value *qa_decode_checkpoint_value(const uint8_t *bytes, size_t length) {
  if (!utf8_valid(bytes, length))
    type_error("The encoded data was not valid for encoding utf-8");
  char *text = dup_range((const char *)bytes, length);
  if (text == NULL) return NULL;
  json_parser parser = {text, 0, length};
  qa_save_value *parsed = json_value(&parser);
  if (parsed == NULL) { free(text); return NULL; }
  json_ws(&parser);
  if (parser.offset != parser.length) {
    qa_save_value_free(parsed);
    free(text);
    syntax_error("Unexpected non-whitespace character after JSON");
  }
  free(text);
  return unpack_value(parsed, "checkpoint");
}

qa_save_value *qa_parse_json_value(const uint8_t *bytes, size_t length, bool quiet) {
  char *text = dup_range((const char *)bytes, length);
  if (text == NULL) return NULL;
  qa_save_value *parsed = NULL;
  bool failed = false;
  if (quiet) {
    quiet_active = true;
    if (setjmp(quiet_jump) != 0) failed = true;
  }
  if (!failed) {
    json_parser parser = {text, 0, length};
    parsed = json_value(&parser);
    if (parsed != NULL) {
      json_ws(&parser);
      if (parser.offset != parser.length) {
        if (!quiet) {
          qa_save_value_free(parsed);
          free(text);
          syntax_error("Unexpected non-whitespace character after JSON");
        }
        qa_save_value_free(parsed);
        parsed = NULL;
      }
    }
  }
  quiet_active = false;
  free(text);
  if (failed) return NULL;
  return parsed;
}

/* --- SaveReader --- */
bool qa_save_reader_init(qa_save_reader *reader, const qa_save_value *value, const char *path) {
  reader->value = value;
  reader->path = dup_text(path != NULL ? path : "save");
  return reader->path != NULL;
}
void qa_save_reader_destroy(qa_save_reader *reader) {
  free(reader->path);
  reader->path = NULL;
  reader->value = NULL;
}
noreturn void qa_save_reader_fail(const qa_save_reader *reader, const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "SaveFormatError: %s: ", reader->path);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
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
  /* Build the TS "a or b or c" message. */
  text_buffer message = {NULL, 0, 0};
  (void)text_add_cstr(&message, "expected ");
  for (size_t i = 0; i < count; i++) {
    if (i > 0) (void)text_add_cstr(&message, " or ");
    (void)text_add_cstr(&message, choices[i]);
  }
  qa_save_reader_fail(reader, "%s", message.data != NULL ? message.data : "expected");
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
  qa_save_reader_fail(reader, "%s", message.data != NULL ? message.data : "expected");
}
const char *qa_namespaced(const qa_save_reader *reader) {
  const char *value = qa_save_reader_string(reader);
  const char *colon = strchr(value, ':');
  if (colon == NULL || colon == value || colon[1] == '\0')
    qa_save_reader_fail(reader, "expected a namespaced identity");
  return value;
}



