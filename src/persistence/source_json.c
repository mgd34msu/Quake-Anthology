/* Port of src/persistence/source-json.ts. GPL-2.0-or-later. */
#include "qa/persistence/source_json.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/persistence/value.h"
#include "qa/foundation/js.h"

static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    memcpy(out, text, length);
    out[length] = '\0';
  }
  return out;
}
static char *dup_text(const char *text) { return dup_range(text, strlen(text)); }

static qa_save_json *json_new(qa_save_json_kind kind) {
  qa_save_json *value = (qa_save_json *)calloc(1, sizeof *value);
  if (value != NULL) value->kind = kind;
  return value;
}
qa_save_json *qa_save_json_null(void) { return json_new(QA_SAVE_JSON_NULL); }
qa_save_json *qa_save_json_boolean(bool b) {
  qa_save_json *v = json_new(QA_SAVE_JSON_BOOLEAN);
  if (v != NULL) v->boolean = b;
  return v;
}
qa_save_json *qa_save_json_string(const char *s) {
  qa_save_json *v = json_new(QA_SAVE_JSON_STRING);
  if (v == NULL) return NULL;
  v->text = dup_text(s);
  if (v->text == NULL) { free(v); return NULL; }
  return v;
}
qa_save_json *qa_save_json_array(void) { return json_new(QA_SAVE_JSON_ARRAY); }
qa_save_json *qa_save_json_object(void) { return json_new(QA_SAVE_JSON_OBJECT); }

bool qa_save_json_push(qa_save_json *array, qa_save_json *child) {
  qa_save_json **items = (qa_save_json **)realloc(
      array->items, (array->item_count + 1) * sizeof *items);
  if (items == NULL) return false;
  array->items = items;
  array->items[array->item_count++] = child;
  return true;
}
qa_save_json *qa_save_json_get(qa_save_json *object, const char *key) {
  if (object == NULL || object->kind != QA_SAVE_JSON_OBJECT) return NULL;
  for (size_t i = 0; i < object->entry_count; i++)
    if (strcmp(object->entries[i].key, key) == 0) return object->entries[i].value;
  return NULL;
}
const qa_save_json *qa_save_json_get_const(const qa_save_json *object, const char *key) {
  return qa_save_json_get((qa_save_json *)object, key);
}
bool qa_save_json_set(qa_save_json *object, const char *key, qa_save_json *child) {
  for (size_t i = 0; i < object->entry_count; i++) {
    if (strcmp(object->entries[i].key, key) == 0) {
      qa_save_json_free(object->entries[i].value);
      object->entries[i].value = child;
      return true;
    }
  }
  char *copy = dup_text(key);
  if (copy == NULL) return false;
  qa_save_json_entry *entries = (qa_save_json_entry *)realloc(
      object->entries, (object->entry_count + 1) * sizeof *entries);
  if (entries == NULL) { free(copy); return false; }
  object->entries = entries;
  object->entries[object->entry_count].key = copy;
  object->entries[object->entry_count].value = child;
  object->entry_count++;
  return true;
}
void qa_save_json_free(qa_save_json *value) {
  if (value == NULL) return;
  free(value->text);
  for (size_t i = 0; i < value->item_count; i++) qa_save_json_free(value->items[i]);
  free(value->items);
  for (size_t i = 0; i < value->entry_count; i++) {
    free(value->entries[i].key);
    qa_save_json_free(value->entries[i].value);
  }
  free(value->entries);
  free(value);
}

/* ^(?:-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?|NaN|-?Infinity)$ */
static bool valid_source_number(const char *text, size_t length) {
  if (length == 3 && memcmp(text, "NaN", 3) == 0) return true;
  size_t i = 0;
  bool negative = false;
  if (i < length && text[i] == '-') { negative = true; i++; }
  if (length - i == 8 && memcmp(text + i, "Infinity", 8) == 0) return true;
  (void)negative;
  if (i >= length) return false;
  if (text[i] == '0') i++;
  else if (text[i] >= '1' && text[i] <= '9') {
    while (i < length && text[i] >= '0' && text[i] <= '9') i++;
  } else return false;
  if (i < length && text[i] == '.') {
    i++;
    if (i >= length || text[i] < '0' || text[i] > '9') return false;
    while (i < length && text[i] >= '0' && text[i] <= '9') i++;
  }
  if (i < length && (text[i] == 'e' || text[i] == 'E')) {
    i++;
    if (i < length && (text[i] == '+' || text[i] == '-')) i++;
    if (i >= length || text[i] < '0' || text[i] > '9') return false;
    while (i < length && text[i] >= '0' && text[i] <= '9') i++;
  }
  return i == length;
}
static qa_save_json *number_from_range(const char *text, size_t length) {
  if (!valid_source_number(text, length))
    qa_save_format_error("json", "invalid source number");
  qa_save_json *v = json_new(QA_SAVE_JSON_NUMBER);
  if (v == NULL) return NULL;
  v->text = dup_range(text, length);
  if (v->text == NULL) { free(v); return NULL; }
  return v;
}
qa_save_json *qa_save_number_create(const char *text) {
  return number_from_range(text, strlen(text));
}
static void format_js_number(double value, char *buffer, size_t size) {
  qa_net_js_number(value, buffer, size);
}
qa_save_json *qa_save_number_from_double(double value) {
  char buffer[64];
  if (value == 0.0 && signbit(value)) return qa_save_number_create("-0.0");
  if (isnan(value)) return qa_save_number_create("NaN");
  if (isinf(value)) return qa_save_number_create(value > 0 ? "Infinity" : "-Infinity");
  format_js_number(value, buffer, sizeof buffer);
  return qa_save_number_create(buffer);
}
qa_save_json *qa_save_number_from_bigint_text(const char *decimal) {
  return qa_save_number_create(decimal);
}
double qa_save_number_number(const qa_save_json *number) {
  return qa_js_string_to_number(number->text); /* Number(this.text) */
}
const char *qa_save_number_bigint_text(const qa_save_json *number) {
  /* TS BigInt(text) accepts only integer text; the source grammar's
   * fractional/exponent forms throw a SyntaxError there. */
  const char *text = number->text;
  size_t i = 0;
  if (text[i] == '-') i++;
  bool digits = false;
  for (; text[i] != '\0'; i++) {
    if (text[i] < '0' || text[i] > '9') {
      fprintf(stderr, "SyntaxError: Cannot convert %s to a BigInt\n", text);
      abort();
    }
    digits = true;
  }
  if (!digits) {
    fprintf(stderr, "SyntaxError: Cannot convert %s to a BigInt\n", text);
    abort();
  }
  return text;
}
uint64_t qa_save_number_bigint_u64(const qa_save_json *number) {
  return (uint64_t)strtoull(qa_save_number_bigint_text(number), NULL, 10);
}
int64_t qa_save_number_bigint_i64(const qa_save_json *number) {
  return (int64_t)strtoll(qa_save_number_bigint_text(number), NULL, 10);
}

/* --- parseSourceJson --- */
typedef struct source_parser { const char *text; size_t offset; size_t length; } source_parser;
static noreturn void source_fail(const source_parser *p, const char *message) {
  char path[64];
  (void)snprintf(path, sizeof path, "json:%zu", p->offset);
  qa_save_format_error(path, "%s", message);
}
static bool is_js_space(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
static void source_ws(source_parser *p) {
  while (p->offset < p->length && is_js_space((unsigned char)p->text[p->offset])) p->offset++;
}
static char char_at(const source_parser *p, size_t offset) {
  return offset < p->length ? p->text[offset] : '\0';
}

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
static bool text_add_char(text_buffer *b, char c) { return text_add(b, &c, 1); }
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
/* Decodes a raw JSON string token (including quotes), mirroring the
 * TS JSON.parse slice. Returns NULL on OOM or invalid string. */
static char *decode_json_token(const char *token, size_t length) {
  text_buffer out = {NULL, 0, 0};
  size_t i = 1;
  while (i + 1 < length) {
    unsigned char c = (unsigned char)token[i++];
    if (c < 0x20) { free(out.data); return NULL; }
    if (c == '\\') {
      if (i >= length) { free(out.data); return NULL; }
      char e = token[i++];
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
          if (i + 4 > length) { free(out.data); return NULL; }
          uint32_t cp = 0;
          for (int k = 0; k < 4; k++) {
            int d = hex_value(token[i++]);
            if (d < 0) { free(out.data); return NULL; }
            cp = (cp << 4) | (uint32_t)d;
          }
          if (cp >= 0xd800 && cp <= 0xdbff && i + 6 <= length &&
              token[i] == '\\' && token[i + 1] == 'u') {
            uint32_t lo = 0;
            bool valid = true;
            for (int k = 0; k < 4; k++) {
              int d = hex_value(token[i + 2 + (size_t)k]);
              if (d < 0) { valid = false; break; }
              lo = (lo << 4) | (uint32_t)d;
            }
            if (valid && lo >= 0xdc00 && lo <= 0xdfff) {
              cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
              i += 6;
            }
          }
          ok = encode_utf8(&out, cp);
          break;
        }
        default: free(out.data); return NULL;
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

static char *source_string(source_parser *p) {
  if (char_at(p, p->offset) != '"') source_fail(p, "expected a string");
  size_t start = p->offset++;
  for (;;) {
    char character = char_at(p, p->offset);
    p->offset++;
    if (character == '\0' && p->offset > p->length) source_fail(p, "unterminated string");
    if (character == '\0' && p->offset - 1 >= p->length) source_fail(p, "unterminated string");
    if (character == '\\') { p->offset++; continue; }
    if (character == '"') break;
    if (character == '\0') source_fail(p, "unterminated string");
  }
  char *decoded = decode_json_token(p->text + start, p->offset - start);
  if (decoded == NULL) source_fail(p, "invalid string");
  return decoded;
}

static qa_save_json *source_value(source_parser *p);
static qa_save_json *source_value(source_parser *p) {
  source_ws(p);
  char character = char_at(p, p->offset);
  if (character == '"') {
    char *s = source_string(p);
    qa_save_json *v = json_new(QA_SAVE_JSON_STRING);
    if (v == NULL) { free(s); return NULL; }
    v->text = s;
    return v;
  }
  if (character == '{' || character == '[') {
    bool is_object = character == '{';
    p->offset++;
    source_ws(p);
    char end = is_object ? '}' : ']';
    qa_save_json *v = json_new(is_object ? QA_SAVE_JSON_OBJECT : QA_SAVE_JSON_ARRAY);
    if (v == NULL) return NULL;
    if (char_at(p, p->offset) != end) {
      for (;;) {
        if (is_object) {
          source_ws(p);
          char *key = source_string(p);
          source_ws(p);
          if (char_at(p, p->offset) != ':') { free(key); qa_save_json_free(v); p->offset++; source_fail(p, "expected ':'"); }
          p->offset++;
          qa_save_json *child = source_value(p);
          if (child == NULL || !qa_save_json_set(v, key, child)) {
            free(key);
            qa_save_json_free(child);
            qa_save_json_free(v);
            return NULL;
          }
          free(key);
        } else {
          qa_save_json *child = source_value(p);
          if (child == NULL || !qa_save_json_push(v, child)) {
            qa_save_json_free(child);
            qa_save_json_free(v);
            return NULL;
          }
        }
        source_ws(p);
        if (char_at(p, p->offset) != ',') break;
        p->offset++;
        source_ws(p);
      }
    }
    if (char_at(p, p->offset) != end) {
      qa_save_json_free(v);
      p->offset++;
      char message[32];
      (void)snprintf(message, sizeof message, "expected '%c'", end);
      source_fail(p, message);
    }
    p->offset++;
    return v;
  }
  static const struct { const char *literal; size_t length; } literals[] = {
      {"null", 4}, {"true", 4}, {"false", 5}};
  for (size_t i = 0; i < 3; i++) {
    if (p->offset + literals[i].length <= p->length &&
        memcmp(p->text + p->offset, literals[i].literal, literals[i].length) == 0) {
      p->offset += literals[i].length;
      if (i == 0) return json_new(QA_SAVE_JSON_NULL);
      return qa_save_json_boolean(i == 1);
    }
  }
  /* /^(?:NaN|-?Infinity|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)/ */
  size_t start = p->offset;
  size_t i = start;
  size_t matched = 0;
  if (i + 3 <= p->length && memcmp(p->text + i, "NaN", 3) == 0) matched = 3;
  else {
    if (i < p->length && p->text[i] == '-') i++;
    if (i + 8 <= p->length && memcmp(p->text + i, "Infinity", 8) == 0) matched = (i + 8) - start;
    else {
      size_t j = i;
      bool ok = false;
      if (j < p->length && p->text[j] == '0') { j++; ok = true; }
      else if (j < p->length && p->text[j] >= '1' && p->text[j] <= '9') {
        while (j < p->length && p->text[j] >= '0' && p->text[j] <= '9') j++;
        ok = true;
      }
      if (ok) {
        if (j < p->length && p->text[j] == '.') {
          size_t k = j + 1;
          if (k < p->length && p->text[k] >= '0' && p->text[k] <= '9') {
            while (k < p->length && p->text[k] >= '0' && p->text[k] <= '9') k++;
            j = k;
          }
        }
        if (j < p->length && (p->text[j] == 'e' || p->text[j] == 'E')) {
          size_t k = j + 1;
          if (k < p->length && (p->text[k] == '+' || p->text[k] == '-')) k++;
          if (k < p->length && p->text[k] >= '0' && p->text[k] <= '9') {
            while (k < p->length && p->text[k] >= '0' && p->text[k] <= '9') k++;
            j = k;
          }
        }
        matched = j - start;
      }
    }
  }
  if (matched == 0) source_fail(p, "expected a value");
  p->offset += matched;
  return number_from_range(p->text + start, matched);
}

qa_save_json *qa_parse_source_json(const char *text) {
  source_parser parser = {text, 0, strlen(text)};
  qa_save_json *root = source_value(&parser);
  if (root == NULL) return NULL;
  source_ws(&parser);
  if (parser.offset != parser.length) {
    qa_save_json_free(root);
    source_fail(&parser, "trailing save data");
  }
  return root;
}

/* --- writeSourceJson --- */
static bool write_json_string(text_buffer *out, const char *text) {
  static const char hex_digits[] = "0123456789abcdef";
  if (!text_add_char(out, '"')) return false;
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    bool ok;
    switch (*p) {
      case '"': ok = text_add(out, "\\\"", 2); break;
      case '\\': ok = text_add(out, "\\\\", 2); break;
      case '\b': ok = text_add(out, "\\b", 2); break;
      case '\f': ok = text_add(out, "\\f", 2); break;
      case '\n': ok = text_add(out, "\\n", 2); break;
      case '\r': ok = text_add(out, "\\r", 2); break;
      case '\t': ok = text_add(out, "\\t", 2); break;
      default:
        if (*p < 0x20) {
          char escape[7] = {'\\', 'u', '0', '0', hex_digits[*p >> 4], hex_digits[*p & 0xf], '\0'};
          ok = text_add(out, escape, 6);
        } else ok = text_add_char(out, (char)*p);
        break;
    }
    if (!ok) return false;
  }
  return text_add_char(out, '"');
}
/* JS array-index key: canonical decimal of a uint32 below 2^32-1. */
static bool array_index_key(const char *key, uint32_t *out) {
  if (key[0] == '\0') return false;
  uint64_t value = 0;
  for (size_t i = 0; key[i] != '\0'; i++) {
    if (key[i] < '0' || key[i] > '9') return false;
    if (i > 0 && key[0] == '0') return false;
    value = value * 10 + (uint64_t)(key[i] - '0');
    if (value >= 4294967295ULL) return false;
  }
  *out = (uint32_t)value;
  return true;
}
static bool write_value(text_buffer *out, const qa_save_json *value);
static bool write_value(text_buffer *out, const qa_save_json *value) {
  switch (value->kind) {
    case QA_SAVE_JSON_NULL: return text_add(out, "null", 4);
    case QA_SAVE_JSON_BOOLEAN: return value->boolean ? text_add(out, "true", 4) : text_add(out, "false", 5);
    case QA_SAVE_JSON_STRING: return write_json_string(out, value->text);
    case QA_SAVE_JSON_NUMBER: return text_add(out, value->text, strlen(value->text));
    case QA_SAVE_JSON_ARRAY: {
      if (!text_add_char(out, '[')) return false;
      for (size_t i = 0; i < value->item_count; i++) {
        if (i > 0 && !text_add_char(out, ',')) return false;
        if (!write_value(out, value->items[i])) return false;
      }
      return text_add_char(out, ']');
    }
    case QA_SAVE_JSON_OBJECT: {
      if (!text_add_char(out, '{')) return false;
      bool first = true;
      /* Pass 1: array-index keys ascending; pass 2: insertion order. */
      for (int pass = 0; pass < 2; pass++) {
        size_t count = value->entry_count;
        for (size_t n = 0; n < count; n++) {
          size_t index = n;
          if (pass == 0) {
            /* Selection by ascending index value. */
            uint32_t best_value = 0;
            size_t best = count;
            for (size_t i = 0; i < count; i++) {
              uint32_t v;
              if (!array_index_key(value->entries[i].key, &v)) continue;
              size_t rank = 0;
              for (size_t j = 0; j < count; j++) {
                uint32_t other;
                if (array_index_key(value->entries[j].key, &other) && other < v) rank++;
              }
              if (rank == n) { best = i; best_value = v; break; }
            }
            if (best == count) break;
            (void)best_value;
            index = best;
          } else {
            uint32_t v;
            if (array_index_key(value->entries[index].key, &v)) continue;
          }
          const qa_save_json *field = value->entries[index].value;
          if (field == NULL || field->kind == QA_SAVE_JSON_UNDEFINED)
            qa_save_format_error(value->entries[index].key, "undefined source save field");
          if (!first && !text_add_char(out, ',')) return false;
          first = false;
          if (!write_json_string(out, value->entries[index].key)) return false;
          if (!text_add_char(out, ':')) return false;
          if (!write_value(out, field)) return false;
        }
      }
      return text_add_char(out, '}');
    }
    default: return false;
  }
}
char *qa_write_source_json(const qa_save_json *value) {
  text_buffer out = {NULL, 0, 0};
  if (!write_value(&out, value)) { free(out.data); return NULL; }
  if (out.data == NULL) return dup_text("");
  return out.data;
}

qa_save_json *qa_source_object(qa_save_json *value, const char *path) {
  if (value == NULL || value->kind != QA_SAVE_JSON_OBJECT)
    qa_save_format_error(path, "expected an object");
  return value;
}
const qa_save_json *qa_source_object_const(const qa_save_json *value, const char *path) {
  return qa_source_object((qa_save_json *)value, path);
}
double qa_source_number_fallback(const qa_save_json *value, const char *path, double fallback) {
  if (value == NULL || value->kind == QA_SAVE_JSON_UNDEFINED) return fallback;
  if (value->kind != QA_SAVE_JSON_NUMBER)
    qa_save_format_error(path, "expected a numeric literal");
  return qa_save_number_number(value);
}
double qa_source_number(const qa_save_json *value, const char *path) {
  return qa_source_number_fallback(value, path, 0);
}

