#define _POSIX_C_SOURCE 200809L
/* See include/qa/llm/auth.h. GPL-2.0-or-later. */
#include "qa/llm/auth.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

#include "qa/llm/auth_worker.h"

#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- JSON DOM ---- */

static qa_llm_json *json_new(qa_llm_json_kind kind) {
  qa_llm_json *value = calloc(1, sizeof *value);
  if (value == NULL) qa_llm_oom();
  value->kind = kind;
  return value;
}

qa_llm_json *qa_llm_json_null(void) { return json_new(QA_LLM_JSON_NULL); }
qa_llm_json *qa_llm_json_bool(bool value) { qa_llm_json *json = json_new(QA_LLM_JSON_BOOL); json->boolean = value; return json; }
qa_llm_json *qa_llm_json_number(double value) { qa_llm_json *json = json_new(QA_LLM_JSON_NUMBER); json->number = value; return json; }
qa_llm_json *qa_llm_json_string(const char *value) { qa_llm_json *json = json_new(QA_LLM_JSON_STRING); json->string = qa_llm_strdup(value != NULL ? value : ""); return json; }
qa_llm_json *qa_llm_json_array(void) { return json_new(QA_LLM_JSON_ARRAY); }
qa_llm_json *qa_llm_json_object(void) { return json_new(QA_LLM_JSON_OBJECT); }

void qa_llm_json_free(qa_llm_json *value) {
  if (value == NULL) return;
  free(value->string);
  for (size_t i = 0; i < value->count; i++) {
    qa_llm_json_free(value->items[i]);
    if (value->keys != NULL) free(value->keys[i]);
  }
  free(value->items);
  free(value->keys);
  free(value);
}

static void json_grow(qa_llm_json *value) {
  if (value->count == value->capacity) {
    value->capacity = value->capacity == 0 ? 8 : value->capacity * 2;
    value->items = realloc(value->items, value->capacity * sizeof *value->items);
    if (value->items == NULL) qa_llm_oom();
    if (value->kind == QA_LLM_JSON_OBJECT) {
      value->keys = realloc(value->keys, value->capacity * sizeof *value->keys);
      if (value->keys == NULL) qa_llm_oom();
    }
  }
}

void qa_llm_json_array_push(qa_llm_json *array, qa_llm_json *item) {
  json_grow(array);
  array->items[array->count++] = item;
}

void qa_llm_json_object_set(qa_llm_json *object, const char *key, qa_llm_json *value) {
  for (size_t i = 0; i < object->count; i++) {
    if (strcmp(object->keys[i], key) == 0) {
      qa_llm_json_free(object->items[i]);
      object->items[i] = value;
      return;
    }
  }
  json_grow(object);
  object->keys[object->count] = qa_llm_strdup(key);
  object->items[object->count] = value;
  object->count++;
}

qa_llm_json *qa_llm_json_get(const qa_llm_json *object, const char *key) {
  if (object == NULL || object->kind != QA_LLM_JSON_OBJECT) return NULL;
  for (size_t i = 0; i < object->count; i++) {
    if (strcmp(object->keys[i], key) == 0) return object->items[i];
  }
  return NULL;
}

bool qa_llm_json_is_array(const qa_llm_json *value) { return value != NULL && value->kind == QA_LLM_JSON_ARRAY; }
bool qa_llm_json_is_object(const qa_llm_json *value) { return value != NULL && value->kind == QA_LLM_JSON_OBJECT; }

bool qa_llm_record(const qa_llm_json *value, qa_llm_error *error) {
  if (!qa_llm_json_is_object(value)) {
    qa_llm_error_set(error, "Invalid LLM settings data.");
    return false;
  }
  return true;
}

/* JSON.parse */
typedef struct json_parser {
  const char *text;
  size_t length;
  size_t pos;
  int depth;
} json_parser;

static void json_skip_ws(json_parser *parser) {
  while (parser->pos < parser->length) {
    char c = parser->text[parser->pos];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') parser->pos++;
    else break;
  }
}

static bool json_parse_value(json_parser *parser, qa_llm_json **out);

static void utf8_encode(qa_llm_strbuf *out, uint32_t value) {
  char bytes[4];
  if (value < 0x80) {
    qa_llm_strbuf_append_char(out, (char)value);
  } else if (value < 0x800) {
    bytes[0] = (char)(0xc0 | (value >> 6));
    bytes[1] = (char)(0x80 | (value & 0x3f));
    qa_llm_strbuf_append_n(out, bytes, 2);
  } else if (value < 0x10000) {
    bytes[0] = (char)(0xe0 | (value >> 12));
    bytes[1] = (char)(0x80 | ((value >> 6) & 0x3f));
    bytes[2] = (char)(0x80 | (value & 0x3f));
    qa_llm_strbuf_append_n(out, bytes, 3);
  } else {
    bytes[0] = (char)(0xf0 | (value >> 18));
    bytes[1] = (char)(0x80 | ((value >> 12) & 0x3f));
    bytes[2] = (char)(0x80 | ((value >> 6) & 0x3f));
    bytes[3] = (char)(0x80 | (value & 0x3f));
    qa_llm_strbuf_append_n(out, bytes, 4);
  }
}

static bool json_hex4(json_parser *parser, uint32_t *out) {
  if (parser->pos + 4 > parser->length) return false;
  uint32_t value = 0;
  for (int i = 0; i < 4; i++) {
    char c = parser->text[parser->pos++];
    value <<= 4;
    if (c >= '0' && c <= '9') value |= (uint32_t)(c - '0');
    else if (c >= 'a' && c <= 'f') value |= (uint32_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') value |= (uint32_t)(c - 'A' + 10);
    else return false;
  }
  *out = value;
  return true;
}

static bool json_parse_string_raw(json_parser *parser, char **out) {
  if (parser->pos >= parser->length || parser->text[parser->pos] != '"') return false;
  parser->pos++;
  qa_llm_strbuf text;
  qa_llm_strbuf_init(&text);
  while (parser->pos < parser->length) {
    uint8_t c = (uint8_t)parser->text[parser->pos++];
    if (c == '"') { *out = qa_llm_strbuf_steal(&text); return true; }
    if (c < 0x20) { qa_llm_strbuf_free(&text); return false; }
    if (c != '\\') { qa_llm_strbuf_append_char(&text, (char)c); continue; }
    if (parser->pos >= parser->length) break;
    char escape = parser->text[parser->pos++];
    switch (escape) {
      case '"': qa_llm_strbuf_append_char(&text, '"'); break;
      case '\\': qa_llm_strbuf_append_char(&text, '\\'); break;
      case '/': qa_llm_strbuf_append_char(&text, '/'); break;
      case 'b': qa_llm_strbuf_append_char(&text, '\b'); break;
      case 'f': qa_llm_strbuf_append_char(&text, '\f'); break;
      case 'n': qa_llm_strbuf_append_char(&text, '\n'); break;
      case 'r': qa_llm_strbuf_append_char(&text, '\r'); break;
      case 't': qa_llm_strbuf_append_char(&text, '\t'); break;
      case 'u': {
        uint32_t value;
        if (!json_hex4(parser, &value)) { qa_llm_strbuf_free(&text); return false; }
        if (value >= 0xd800 && value <= 0xdbff) {
          size_t save = parser->pos;
          if (parser->pos + 2 <= parser->length && parser->text[parser->pos] == '\\' && parser->text[parser->pos + 1] == 'u') {
            parser->pos += 2;
            uint32_t low;
            if (json_hex4(parser, &low) && low >= 0xdc00 && low <= 0xdfff) {
              value = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00);
            } else {
              parser->pos = save;
              value = 0xfffd; /* lone surrogate: JS keeps it; C text cannot */
            }
          } else value = 0xfffd;
        } else if (value >= 0xdc00 && value <= 0xdfff) value = 0xfffd;
        utf8_encode(&text, value);
        break;
      }
      default: qa_llm_strbuf_free(&text); return false;
    }
  }
  qa_llm_strbuf_free(&text);
  return false;
}

static bool json_parse_value(json_parser *parser, qa_llm_json **out) {
  if (++parser->depth > 512) return false;
  json_skip_ws(parser);
  bool ok = false;
  if (parser->pos >= parser->length) goto done;
  char c = parser->text[parser->pos];
  if (c == '{') {
    parser->pos++;
    qa_llm_json *object = qa_llm_json_object();
    json_skip_ws(parser);
    if (parser->pos < parser->length && parser->text[parser->pos] == '}') { parser->pos++; *out = object; ok = true; goto done; }
    for (;;) {
      json_skip_ws(parser);
      char *key = NULL;
      if (!json_parse_string_raw(parser, &key)) { qa_llm_json_free(object); goto done; }
      json_skip_ws(parser);
      if (parser->pos >= parser->length || parser->text[parser->pos] != ':') { free(key); qa_llm_json_free(object); goto done; }
      parser->pos++;
      qa_llm_json *item = NULL;
      if (!json_parse_value(parser, &item)) { free(key); qa_llm_json_free(object); goto done; }
      qa_llm_json_object_set(object, key, item);
      free(key);
      json_skip_ws(parser);
      if (parser->pos < parser->length && parser->text[parser->pos] == ',') { parser->pos++; continue; }
      if (parser->pos < parser->length && parser->text[parser->pos] == '}') { parser->pos++; *out = object; ok = true; }
      else qa_llm_json_free(object);
      goto done;
    }
  }
  if (c == '[') {
    parser->pos++;
    qa_llm_json *array = qa_llm_json_array();
    json_skip_ws(parser);
    if (parser->pos < parser->length && parser->text[parser->pos] == ']') { parser->pos++; *out = array; ok = true; goto done; }
    for (;;) {
      qa_llm_json *item = NULL;
      if (!json_parse_value(parser, &item)) { qa_llm_json_free(array); goto done; }
      qa_llm_json_array_push(array, item);
      json_skip_ws(parser);
      if (parser->pos < parser->length && parser->text[parser->pos] == ',') { parser->pos++; continue; }
      if (parser->pos < parser->length && parser->text[parser->pos] == ']') { parser->pos++; *out = array; ok = true; }
      else qa_llm_json_free(array);
      goto done;
    }
  }
  if (c == '"') {
    char *text = NULL;
    if (!json_parse_string_raw(parser, &text)) goto done;
    qa_llm_json *value = qa_llm_json_string(text);
    free(text);
    *out = value;
    ok = true;
    goto done;
  }
  if (parser->length - parser->pos >= 4 && memcmp(parser->text + parser->pos, "true", 4) == 0) {
    parser->pos += 4; *out = qa_llm_json_bool(true); ok = true; goto done;
  }
  if (parser->length - parser->pos >= 5 && memcmp(parser->text + parser->pos, "false", 5) == 0) {
    parser->pos += 5; *out = qa_llm_json_bool(false); ok = true; goto done;
  }
  if (parser->length - parser->pos >= 4 && memcmp(parser->text + parser->pos, "null", 4) == 0) {
    parser->pos += 4; *out = qa_llm_json_null(); ok = true; goto done;
  }
  {
    /* JSON number grammar, then strtod over the accepted span. */
    size_t start = parser->pos;
    size_t pos = start;
    if (pos < parser->length && parser->text[pos] == '-') pos++;
    if (pos < parser->length && parser->text[pos] == '0') pos++;
    else if (pos < parser->length && parser->text[pos] >= '1' && parser->text[pos] <= '9') {
      while (pos < parser->length && isdigit((uint8_t)parser->text[pos])) pos++;
    } else goto done;
    if (pos < parser->length && parser->text[pos] == '.') {
      pos++;
      if (pos >= parser->length || !isdigit((uint8_t)parser->text[pos])) goto done;
      while (pos < parser->length && isdigit((uint8_t)parser->text[pos])) pos++;
    }
    if (pos < parser->length && (parser->text[pos] == 'e' || parser->text[pos] == 'E')) {
      pos++;
      if (pos < parser->length && (parser->text[pos] == '+' || parser->text[pos] == '-')) pos++;
      if (pos >= parser->length || !isdigit((uint8_t)parser->text[pos])) goto done;
      while (pos < parser->length && isdigit((uint8_t)parser->text[pos])) pos++;
    }
    char *digits = qa_llm_strndup(parser->text + start, pos - start);
    double number = strtod(digits, NULL);
    free(digits);
    parser->pos = pos;
    *out = qa_llm_json_number(number);
    ok = true;
  }
done:
  parser->depth--;
  return ok;
}

bool qa_llm_json_parse(const char *text, size_t length, qa_llm_json **out) {
  json_parser parser = { text, length, 0, 0 };
  *out = NULL;
  if (!json_parse_value(&parser, out)) return false;
  json_skip_ws(&parser);
  if (parser.pos != parser.length) {
    qa_llm_json_free(*out);
    *out = NULL;
    return false;
  }
  return true;
}

static void json_stringify_string(qa_llm_strbuf *out, const char *text) {
  qa_llm_strbuf_append_char(out, '"');
  for (const uint8_t *p = (const uint8_t *)text; *p != '\0'; p++) {
    switch (*p) {
      case '"': qa_llm_strbuf_append(out, "\\\""); break;
      case '\\': qa_llm_strbuf_append(out, "\\\\"); break;
      case '\b': qa_llm_strbuf_append(out, "\\b"); break;
      case '\f': qa_llm_strbuf_append(out, "\\f"); break;
      case '\n': qa_llm_strbuf_append(out, "\\n"); break;
      case '\r': qa_llm_strbuf_append(out, "\\r"); break;
      case '\t': qa_llm_strbuf_append(out, "\\t"); break;
      default:
        if (*p < 0x20) qa_llm_strbuf_appendf(out, "\\u%04x", *p);
        else qa_llm_strbuf_append_char(out, (char)*p);
    }
  }
  qa_llm_strbuf_append_char(out, '"');
}

static void json_stringify_into(qa_llm_strbuf *out, const qa_llm_json *value) {
  switch (value->kind) {
    case QA_LLM_JSON_NULL: qa_llm_strbuf_append(out, "null"); break;
    case QA_LLM_JSON_BOOL: qa_llm_strbuf_append(out, value->boolean ? "true" : "false"); break;
    case QA_LLM_JSON_NUMBER: {
      char buffer[40]; /* JSON.stringify: String(number), non-finite as null */
      qa_net_json_number(value->number, buffer, sizeof buffer);
      qa_llm_strbuf_append(out, buffer);
      break;
    }
    case QA_LLM_JSON_STRING: json_stringify_string(out, value->string); break;
    case QA_LLM_JSON_ARRAY:
      qa_llm_strbuf_append_char(out, '[');
      for (size_t i = 0; i < value->count; i++) {
        if (i > 0) qa_llm_strbuf_append_char(out, ',');
        json_stringify_into(out, value->items[i]);
      }
      qa_llm_strbuf_append_char(out, ']');
      break;
    case QA_LLM_JSON_OBJECT:
      qa_llm_strbuf_append_char(out, '{');
      for (size_t i = 0; i < value->count; i++) {
        if (i > 0) qa_llm_strbuf_append_char(out, ',');
        json_stringify_string(out, value->keys[i]);
        qa_llm_strbuf_append_char(out, ':');
        json_stringify_into(out, value->items[i]);
      }
      qa_llm_strbuf_append_char(out, '}');
      break;
  }
}

char *qa_llm_json_stringify(const qa_llm_json *value) {
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  json_stringify_into(&out, value);
  return qa_llm_strbuf_steal(&out);
}

/* ---- subscription credentials and OAuth ---- */

static const char *CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann";

void qa_llm_subscription_credential_free(qa_llm_subscription_credential *credential) {
  if (credential == NULL) return;
  free(credential->access_token);
  free(credential->refresh_token);
  free(credential->token_type);
  for (size_t i = 0; i < credential->scope_count; i++) free(credential->scopes[i]);
  free(credential->scopes);
  memset(credential, 0, sizeof *credential);
}

static bool credential_nonempty(const qa_llm_json *value, char **out, qa_llm_error *error) {
  if (value == NULL || value->kind != QA_LLM_JSON_STRING || qa_llm_trim_is_empty(value->string)) {
    qa_llm_error_set(error, "Invalid subscription credential.");
    return false;
  }
  *out = qa_llm_strdup(value->string);
  return true;
}

bool qa_llm_parse_subscription(const qa_llm_json *value, qa_llm_subscription_credential *out, qa_llm_error *error) {
  memset(out, 0, sizeof *out);
  if (!qa_llm_record(value, error)) return false;
  const qa_llm_json *scopes = qa_llm_json_get(value, "scopes");
  const qa_llm_json *expires = qa_llm_json_get(value, "expiresAt");
  if (expires == NULL || expires->kind != QA_LLM_JSON_NUMBER || !isfinite(expires->number) || !qa_llm_json_is_array(scopes)) {
    qa_llm_error_set(error, "Invalid subscription credential.");
    return false;
  }
  if (!credential_nonempty(qa_llm_json_get(value, "accessToken"), &out->access_token, error) ||
      !credential_nonempty(qa_llm_json_get(value, "refreshToken"), &out->refresh_token, error) ||
      !credential_nonempty(qa_llm_json_get(value, "tokenType"), &out->token_type, error)) {
    qa_llm_subscription_credential_free(out);
    return false;
  }
  out->expires_at = expires->number;
  if (scopes->count > 0) {
    out->scopes = calloc(scopes->count, sizeof *out->scopes);
    if (out->scopes == NULL) qa_llm_oom();
  }
  for (size_t i = 0; i < scopes->count; i++) {
    if (!credential_nonempty(scopes->items[i], &out->scopes[i], error)) {
      out->scope_count = i;
      qa_llm_subscription_credential_free(out);
      return false;
    }
    out->scope_count = i + 1;
  }
  return true;
}

qa_llm_json *qa_llm_subscription_to_json(const qa_llm_subscription_credential *credential) {
  qa_llm_json *object = qa_llm_json_object();
  qa_llm_json_object_set(object, "accessToken", qa_llm_json_string(credential->access_token));
  qa_llm_json_object_set(object, "refreshToken", qa_llm_json_string(credential->refresh_token));
  qa_llm_json_object_set(object, "tokenType", qa_llm_json_string(credential->token_type));
  qa_llm_json_object_set(object, "expiresAt", qa_llm_json_number(credential->expires_at));
  qa_llm_json *scopes = qa_llm_json_array();
  for (size_t i = 0; i < credential->scope_count; i++) qa_llm_json_array_push(scopes, qa_llm_json_string(credential->scopes[i]));
  qa_llm_json_object_set(object, "scopes", scopes);
  return object;
}

void qa_llm_subscription_cancelled(qa_llm_error *error) {
  qa_llm_error_set(error, "Subscription sign-in cancelled.");
}

bool qa_llm_check_abort(qa_llm_abort_signal *signal, qa_llm_error *error) {
  if (qa_llm_signal_is_aborted(signal)) {
    qa_llm_subscription_cancelled(error);
    return false;
  }
  return true;
}

/* URLSearchParams serialization (application/x-www-form-urlencoded). */
static void form_encode(qa_llm_strbuf *out, const char *text) {
  for (const uint8_t *p = (const uint8_t *)text; *p != '\0'; p++) {
    uint8_t c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '*' || c == '-' || c == '.' || c == '_') {
      qa_llm_strbuf_append_char(out, (char)c);
    } else if (c == ' ') {
      qa_llm_strbuf_append_char(out, '+');
    } else {
      qa_llm_strbuf_appendf(out, "%%%02X", c);
    }
  }
}

typedef struct form_fields {
  char **keys;
  char **values;
  size_t count;
  size_t capacity;
} form_fields;

static void form_append(form_fields *form, const char *key, const char *value);
/* URLSearchParams.set: the first pair with the name takes the value and later
 * pairs with the name are removed; otherwise the pair is appended. */
static void form_set(form_fields *form, const char *key, const char *value) {
  bool found = false;
  for (size_t i = 0; i < form->count;) {
    if (strcmp(form->keys[i], key) != 0) { i++; continue; }
    if (!found) {
      free(form->values[i]);
      form->values[i] = qa_llm_strdup(value);
      found = true;
      i++;
      continue;
    }
    free(form->keys[i]);
    free(form->values[i]);
    memmove(form->keys + i, form->keys + i + 1, (form->count - i - 1) * sizeof *form->keys);
    memmove(form->values + i, form->values + i + 1, (form->count - i - 1) * sizeof *form->values);
    form->count--;
  }
  if (!found) form_append(form, key, value);
}
static void form_append(form_fields *form, const char *key, const char *value) {
  if (form->count == form->capacity) {
    form->capacity = form->capacity == 0 ? 8 : form->capacity * 2;
    form->keys = realloc(form->keys, form->capacity * sizeof *form->keys);
    form->values = realloc(form->values, form->capacity * sizeof *form->values);
    if (form->keys == NULL || form->values == NULL) qa_llm_oom();
  }
  form->keys[form->count] = qa_llm_strdup(key);
  form->values[form->count] = qa_llm_strdup(value);
  form->count++;
}

static char *form_to_string(const form_fields *form) {
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  for (size_t i = 0; i < form->count; i++) {
    if (i > 0) qa_llm_strbuf_append_char(&out, '&');
    form_encode(&out, form->keys[i]);
    qa_llm_strbuf_append_char(&out, '=');
    form_encode(&out, form->values[i]);
  }
  return qa_llm_strbuf_steal(&out);
}

static void form_free(form_fields *form) {
  for (size_t i = 0; i < form->count; i++) { free(form->keys[i]); free(form->values[i]); }
  free(form->keys);
  free(form->values);
  memset(form, 0, sizeof *form);
}

/* application/x-www-form-urlencoded parsing of one name or value. */
static void url_component_decode(qa_llm_strbuf *out, const char *text, size_t length) {
  for (size_t i = 0; i < length; i++) {
    char c = text[i];
    if (c == '+') { qa_llm_strbuf_append_char(out, ' '); continue; }
    if (c == '%' && i + 2 < length && isxdigit((unsigned char)text[i + 1]) && isxdigit((unsigned char)text[i + 2])) {
      char hex[3] = {text[i + 1], text[i + 2], '\0'};
      qa_llm_strbuf_append_char(out, (char)strtol(hex, NULL, 16));
      i += 2;
      continue;
    }
    qa_llm_strbuf_append_char(out, c);
  }
}

static bool default_open_browser(void *ctx, const char *url, qa_llm_abort_signal *signal, qa_llm_error *error) {
  (void)ctx;
  (void)url;
  if (!qa_llm_check_abort(signal, error)) return false;
  qa_llm_error_set(error, "Sign-in browser is unavailable.");
  return false;
}

/* tokenRequest: any failure collapses to the two TS messages. */
static bool token_request(const qa_llm_subscription_auth_options *options, const char *form_body,
                          qa_llm_abort_signal *signal, const qa_llm_subscription_credential *previous,
                          qa_llm_subscription_credential *out, qa_llm_error *error) {
  qa_llm_abort_signal controller;
  qa_llm_signal_init(&controller);
  qa_llm_signal_set_timeout(&controller, options->has_fetch_timeout ? options->fetch_timeout_ms : 30000);
  controller.parent = signal; /* signal.addEventListener("abort", stop) */
  if (qa_llm_signal_is_aborted(signal)) controller.aborted = true;

  qa_llm_header headers[] = { { "Content-Type", "application/x-www-form-urlencoded" } };
  qa_llm_http_init init;
  memset(&init, 0, sizeof init);
  init.method = "POST";
  init.headers = headers;
  init.header_count = 1;
  init.body = form_body;
  init.body_length = strlen(form_body);
  init.signal = &controller;
  init.redirect_error = true;
  const char *url = options->token_url != NULL ? options->token_url : "https://auth.openai.com/oauth/token";

  bool ok = false;
  qa_llm_http_response response;
  qa_llm_error inner;
  qa_llm_error_clear(&inner);
  qa_llm_bytes body;
  qa_llm_bytes_init(&body);
  qa_llm_json *value = NULL;
  memset(out, 0, sizeof *out);
  if (!qa_llm_fetch_invoke(options->fetch, url, &init, &response, &inner)) goto done;
  if (!qa_llm_response_ok(&response)) { qa_llm_response_dispose(&response); goto done; }
  {
    bool read_ok = response.has_body && !response.invalid_body &&
                   qa_llm_response_read_all(&response, &controller, &body);
    qa_llm_response_dispose(&response);
    if (!read_ok) goto done;
  }
  {
    /* response.json(): UTF-8 decode (BOM stripped, invalid bytes become U+FFFD), then JSON.parse. */
    size_t skip = body.length >= 3 && body.data[0] == 0xef && body.data[1] == 0xbb && body.data[2] == 0xbf ? 3 : 0;
    qa_llm_bytes text;
    qa_llm_bytes_init(&text);
    qa_llm_utf8_decode_lossy(body.data + skip, body.length - skip, &text);
    bool parsed = qa_llm_json_parse((const char *)text.data, text.length, &value);
    qa_llm_bytes_free(&text);
    if (!parsed) goto done;
  }
  if (!qa_llm_record(value, &inner)) goto done;
  {
    const qa_llm_json *expires = qa_llm_json_get(value, "expires_in");
    if (expires == NULL || expires->kind != QA_LLM_JSON_NUMBER || !isfinite(expires->number) || expires->number <= 0) goto done;
    double expires_at = (double)qa_llm_now_ms() + expires->number * 1000.0;
    if (!isfinite(expires_at)) goto done;
    const qa_llm_json *token_type = qa_llm_json_get(value, "token_type");
    qa_llm_json *bearer = NULL;
    if (token_type == NULL || token_type->kind == QA_LLM_JSON_NULL) {
      bearer = qa_llm_json_string("Bearer");
      token_type = bearer;
    }
    /* refreshToken: previous !== undefined && item["refresh_token"] === undefined
     * ? previous.refreshToken : nonempty(item["refresh_token"]) */
    const qa_llm_json *refresh = qa_llm_json_get(value, "refresh_token");
    bool fields_ok = credential_nonempty(qa_llm_json_get(value, "access_token"), &out->access_token, &inner);
    if (fields_ok && previous != NULL && refresh == NULL) out->refresh_token = qa_llm_strdup(previous->refresh_token);
    else fields_ok = fields_ok && credential_nonempty(refresh, &out->refresh_token, &inner);
    fields_ok = fields_ok && credential_nonempty(token_type, &out->token_type, &inner);
    qa_llm_json_free(bearer);
    if (!fields_ok) { qa_llm_subscription_credential_free(out); goto done; }
    out->expires_at = expires_at;
    const qa_llm_json *scope = qa_llm_json_get(value, "scope");
    if (scope != NULL && scope->kind == QA_LLM_JSON_STRING) {
      /* scope.split(/\s+/).filter(Boolean) */
      const char *text = scope->string;
      size_t capacity = 0;
      /* \s is every JS WhiteSpace and LineTerminator code point. */
      size_t space;
      while (*text != '\0') {
        while ((space = qa_js_white_space_length(text)) != 0) text += space;
        if (*text == '\0') break;
        const char *begin = text;
        while (*text != '\0' && qa_js_white_space_length(text) == 0) text++;
        if (out->scope_count == capacity) {
          capacity = capacity == 0 ? 4 : capacity * 2;
          out->scopes = realloc(out->scopes, capacity * sizeof *out->scopes);
          if (out->scopes == NULL) qa_llm_oom();
        }
        out->scopes[out->scope_count++] = qa_llm_strndup(begin, (size_t)(text - begin));
      }
    } else if (previous != NULL) {
      if (previous->scope_count > 0) {
        out->scopes = calloc(previous->scope_count, sizeof *out->scopes);
        if (out->scopes == NULL) qa_llm_oom();
        for (size_t i = 0; i < previous->scope_count; i++) out->scopes[i] = qa_llm_strdup(previous->scopes[i]);
        out->scope_count = previous->scope_count;
      }
    }
    ok = true;
  }
done:
  qa_llm_json_free(value);
  qa_llm_bytes_free(&body);
  if (!ok) {
    qa_llm_subscription_credential_free(out);
    if (qa_llm_signal_is_aborted(signal)) qa_llm_subscription_cancelled(error);
    else qa_llm_error_set(error, "Subscription token request failed. Try signing in again.");
  }
  return ok;
}

bool qa_llm_refresh_subscription(const qa_llm_subscription_auth_options *options,
                                 const qa_llm_subscription_credential *credential, qa_llm_abort_signal *signal,
                                 qa_llm_subscription_credential *out, qa_llm_error *error) {
  form_fields form;
  memset(&form, 0, sizeof form);
  form_set(&form, "grant_type", "refresh_token");
  form_set(&form, "client_id", CLIENT_ID);
  form_set(&form, "refresh_token", credential->refresh_token);
  char *body = form_to_string(&form);
  form_free(&form);
  bool ok = token_request(options, body, signal, credential, out, error);
  free(body);
  return ok;
}

/* login worker message sink, mirroring auth.ts's promise resolvers. */
typedef struct login_state {
  qa_llm_error pending_error;
  bool has_error;
  bool has_code;
  char *code;
  bool ready_resolved;
  int ready_port;
  bool stopped;
} login_state;

static void login_reject(login_state *state, const char *message) {
  if (state->has_code || state->has_error) return;
  qa_llm_error_set(&state->pending_error, message);
  state->has_error = true;
}

static void login_post(void *ctx, const qa_llm_auth_worker_message *message) {
  login_state *state = ctx;
  switch (message->kind) {
    case QA_LLM_AUTH_WORKER_READY:
      if (message->port > 0 && message->port <= 65535) {
        state->ready_resolved = true;
        state->ready_port = message->port;
      } else {
        login_reject(state, "Could not open the local sign-in callback. Close other sign-in windows or applications using port 1455, then retry.");
      }
      break;
    case QA_LLM_AUTH_WORKER_LISTEN_ERROR:
      login_reject(state, "Could not open the local sign-in callback. Close other sign-in windows or applications using port 1455, then retry.");
      break;
    case QA_LLM_AUTH_WORKER_CODE:
      if (!state->has_code && !state->has_error) {
        if (message->code != NULL && message->code[0] != '\0') {
          state->code = qa_llm_strdup(message->code);
          state->has_code = true;
        } else login_reject(state, "Subscription sign-in was not authorized.");
      }
      break;
    case QA_LLM_AUTH_WORKER_STATE_MISMATCH:
      login_reject(state, "Subscription sign-in state mismatch.");
      break;
    case QA_LLM_AUTH_WORKER_DENIED:
      login_reject(state, "Subscription sign-in was not authorized.");
      break;
    case QA_LLM_AUTH_WORKER_STOPPED:
      state->stopped = true;
      break;
  }
}

bool qa_llm_login_subscription(const qa_llm_subscription_auth_options *options, qa_llm_abort_signal *signal,
                               qa_llm_subscription_credential *out, qa_llm_error *error) {
  memset(out, 0, sizeof *out);
  if (!qa_llm_check_abort(signal, error)) return false;
  uint8_t random_state[32], random_verifier[32];
  qa_llm_random_bytes(random_state, sizeof random_state);
  qa_llm_random_bytes(random_verifier, sizeof random_verifier);
  char *state_text = qa_llm_base64url_encode(random_state, sizeof random_state);
  char *verifier = qa_llm_base64url_encode(random_verifier, sizeof random_verifier);

  login_state state;
  memset(&state, 0, sizeof state);
  qa_llm_error_clear(&state.pending_error);
  int requested_port = options->has_callback_port ? options->callback_port : 1455;
  qa_llm_auth_worker *worker = qa_llm_auth_worker_start(requested_port, state_text, login_post, &state);

  bool ok = false;
  bool timed_out = false;
  qa_llm_abort_signal controller;
  qa_llm_signal_init(&controller);
  controller.parent = signal;
  double callback_timeout = options->has_callback_timeout ? options->callback_timeout_ms : 180000;
  /* setTimeout under Bun: NaN, below 1 or above 2^31 - 1 fires after 1 ms. */
  if (!(callback_timeout >= 1 && callback_timeout <= 2147483647.0)) callback_timeout = 1;
  int64_t deadline = qa_llm_monotonic_ms() + (int64_t)callback_timeout;
  char *redirect = NULL;
  char *authorize = NULL;

  if (worker == NULL) {
    /* listen-error already posted into state. */
    if (state.has_error) *error = state.pending_error;
    else qa_llm_error_set(error, "Sign-in failed.");
    goto done;
  }
  if (!state.ready_resolved) {
    if (state.has_error) *error = state.pending_error;
    else qa_llm_error_set(error, "Sign-in failed.");
    goto done;
  }
  {
    qa_llm_strbuf buffer;
    qa_llm_strbuf_init(&buffer);
    qa_llm_strbuf_appendf(&buffer, "http://localhost:%d/auth/callback", state.ready_port);
    redirect = qa_llm_strbuf_steal(&buffer);
  }
  {
    /* new URL(authorizeUrl) + searchParams.set for each entry. */
    const char *base = options->authorize_url != NULL ? options->authorize_url : "https://auth.openai.com/oauth/authorize";
    const char *query = strchr(base, '?');
    form_fields params;
    memset(&params, 0, sizeof params);
    if (query != NULL) {
      /* Preserve pre-existing params (URLSearchParams parse: '+' is a space,
       * %XX decodes; the set re-encodes them). */
      const char *p = query + 1;
      while (*p != '\0' && *p != '#') {
        const char *amp = strchr(p, '&');
        size_t span = amp != NULL ? (size_t)(amp - p) : strlen(p);
        const char *eq = memchr(p, '=', span);
        qa_llm_strbuf key, value;
        qa_llm_strbuf_init(&key);
        qa_llm_strbuf_init(&value);
        size_t key_len = eq != NULL ? (size_t)(eq - p) : span;
        const char *hash = memchr(p, '#', span);
        if (hash != NULL) span = (size_t)(hash - p);
        if (eq != NULL && (size_t)(eq - p) > span) eq = NULL;
        key_len = eq != NULL ? (size_t)(eq - p) : span;
        url_component_decode(&key, p, key_len);
        if (eq != NULL) url_component_decode(&value, eq + 1, span - key_len - 1);
        if (span > 0) form_append(&params, qa_llm_strbuf_text(&key), qa_llm_strbuf_text(&value)); /* empty sequences are skipped */
        qa_llm_strbuf_free(&key);
        qa_llm_strbuf_free(&value);
        if (amp == NULL || hash != NULL) break;
        p = amp + 1;
      }
    }
    char *challenge = qa_llm_sha256_base64url(verifier, strlen(verifier));
    form_set(&params, "response_type", "code");
    form_set(&params, "client_id", CLIENT_ID);
    form_set(&params, "redirect_uri", redirect);
    form_set(&params, "scope", "openid profile email offline_access");
    form_set(&params, "state", state_text);
    form_set(&params, "code_challenge", challenge);
    form_set(&params, "code_challenge_method", "S256");
    form_set(&params, "id_token_add_organizations", "true");
    form_set(&params, "codex_cli_simplified_flow", "true");
    form_set(&params, "originator", "pi");
    free(challenge);
    char *encoded = form_to_string(&params);
    form_free(&params);
    qa_llm_strbuf url;
    qa_llm_strbuf_init(&url);
    const char *fragment = strchr(base, '#');
    if (query != NULL && fragment != NULL && fragment < query) query = NULL;
    const char *path_end = query != NULL ? query : fragment != NULL ? fragment : base + strlen(base);
    qa_llm_strbuf_append_n(&url, base, (size_t)(path_end - base));
    qa_llm_strbuf_append_char(&url, '?');
    qa_llm_strbuf_append(&url, encoded);
    if (fragment != NULL) qa_llm_strbuf_append(&url, fragment); /* URL keeps its hash */
    free(encoded);
    authorize = qa_llm_strbuf_steal(&url);
  }
  {
    qa_llm_open_browser_fn open_browser = options->open_browser != NULL ? options->open_browser : default_open_browser;
    qa_llm_error browser_error;
    qa_llm_error_clear(&browser_error);
    bool launched = open_browser(options->open_browser_ctx, authorize, &controller, &browser_error);
    if (!launched && !state.has_code && !state.has_error) {
      /* Promise.race: a browser failure before any code rejects. */
      *error = browser_error;
      goto done;
    }
    if (state.has_error) { *error = state.pending_error; goto done; }
  }
  /* Wait for the callback, pumping the listener like the worker did. */
  while (!state.has_code && !state.has_error) {
    if (qa_llm_signal_is_aborted(signal)) break;
    if (qa_llm_monotonic_ms() >= deadline) { timed_out = true; break; }
    qa_llm_auth_worker_serve_once(worker, 20);
  }
  if (qa_llm_signal_is_aborted(signal)) {
    qa_llm_subscription_cancelled(error);
    goto done;
  }
  if (timed_out) {
    qa_llm_error_set(error, "Subscription sign-in timed out. Try again.");
    goto done;
  }
  if (state.has_error) { *error = state.pending_error; goto done; }
  {
    /* worker.postMessage({kind:"stop"}); wait briefly for stopped. */
    qa_llm_json *stop = qa_llm_json_object();
    qa_llm_json_object_set(stop, "kind", qa_llm_json_string("stop"));
    qa_llm_auth_worker_handle_message(worker, stop);
    qa_llm_json_free(stop);
  }
  if (!qa_llm_check_abort(signal, error)) goto done;
  {
    form_fields form;
    memset(&form, 0, sizeof form);
    form_set(&form, "grant_type", "authorization_code");
    form_set(&form, "client_id", CLIENT_ID);
    form_set(&form, "redirect_uri", redirect);
    form_set(&form, "code", state.code);
    form_set(&form, "code_verifier", verifier);
    char *body = form_to_string(&form);
    form_free(&form);
    ok = token_request(options, body, signal, NULL, out, error);
    free(body);
  }
done:
  if (worker != NULL) {
    /* finally: stop(force) + terminate; destroy frees without a
     * duplicate stopped when one was already posted. */
    qa_llm_auth_worker_destroy(worker, !state.stopped);
  }
  if (!ok) {
    if (qa_llm_signal_is_aborted(signal)) qa_llm_subscription_cancelled(error);
    else if (timed_out) qa_llm_error_set(error, "Subscription sign-in timed out. Try again.");
    else if (error->message[0] == '\0') qa_llm_error_set(error, "Subscription sign-in failed. Try again.");
    /* LlmSettingsError messages pass through unchanged (all ours are). */
  }
  free(redirect);
  free(authorize);
  free(state.code);
  free(state_text);
  free(verifier);
  return ok;
}
