/* See include/qa/ui/common/legacy/script/lexer.h. GPL-2.0-or-later. */
#include "qa/ui/common/legacy/script/lexer.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/ui/common/legacy/script/token_memory.h"

_Noreturn void qa_ui_script_plain_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

static char *copy_text(const char *text, size_t length) {
  char *result = malloc(length + 1);
  if (result == NULL) return NULL;
  memcpy(result, text, length);
  result[length] = '\0';
  return result;
}
static char *copy_cstring(const char *text) {
  return copy_text(text, strlen(text));
}

void qa_ui_script_fail_env_init(qa_ui_script_fail_env *env) {
  memset(env, 0, sizeof *env);
}

void qa_ui_script_fail_env_destroy(qa_ui_script_fail_env *env) {
  if (env->has_pending) qa_ui_script_diagnostic_destroy(&env->pending);
  env->has_pending = false;
  env->pending_identity = NULL;
}

_Noreturn void qa_ui_script_fail_throw(
    qa_ui_script_fail_env *env, qa_ui_script_diagnostic *diagnostic,
    const qa_ui_script_diagnostic *identity) {
  if (env->top == NULL) {
    /* Uncaught ScriptLanguageError: TS message form, then abort. */
    fprintf(stderr, "ScriptLanguageError: %s:%lld:%lld: %s\n",
            diagnostic->location.path,
            (long long)diagnostic->location.line,
            (long long)diagnostic->location.column, diagnostic->message);
    abort();
  }
  if (env->has_pending) qa_ui_script_diagnostic_destroy(&env->pending);
  env->pending = *diagnostic;
  env->has_pending = true;
  env->pending_identity = identity;
  longjmp(env->top->buf, 1);
}

const char *qa_ui_script_token_type_name(int64_t type) {
  switch (type) {
    case QA_UI_SCRIPT_TOKEN_STRING: return "string";
    case QA_UI_SCRIPT_TOKEN_LITERAL: return "literal";
    case QA_UI_SCRIPT_TOKEN_NUMBER: return "number";
    case QA_UI_SCRIPT_TOKEN_NAME: return "name";
    case QA_UI_SCRIPT_TOKEN_PUNCTUATION: return "punctuation";
    default: return "";
  }
}

char *qa_ui_script_number_subtype_name(int64_t subtype) {
  const char *name = NULL;
  if ((subtype & QA_UI_SCRIPT_NUMBER_DECIMAL) != 0) name = "decimal";
  if ((subtype & QA_UI_SCRIPT_NUMBER_HEX) != 0) name = "hex";
  if ((subtype & QA_UI_SCRIPT_NUMBER_OCTAL) != 0) name = "octal";
  if ((subtype & QA_UI_SCRIPT_NUMBER_BINARY) != 0) name = "binary";
  if (name == NULL)
    qa_range_error("expected numeric subtype diagnostic reads an uninitialized source string");
  char buffer[64];
  snprintf(buffer, sizeof buffer, "%s%s%s%s%s", name,
           (subtype & QA_UI_SCRIPT_NUMBER_LONG) != 0 ? " long" : "",
           (subtype & QA_UI_SCRIPT_NUMBER_UNSIGNED) != 0 ? " unsigned" : "",
           (subtype & QA_UI_SCRIPT_NUMBER_FLOAT) != 0 ? " float" : "",
           (subtype & QA_UI_SCRIPT_NUMBER_INTEGER) != 0 ? " integer" : "");
  return copy_cstring(buffer);
}

static char *strip_quotes(const char *text, char quote) {
  size_t length = strlen(text); /* TS cut at NUL is inherent */
  size_t start = 0;
  if (length > 0 && text[0] == quote) start = 1;
  if (length - start == 0)
    qa_range_error("StripQuotes reads before the source string allocation");
  size_t end = length;
  if (end > start && text[end - 1] == quote) end--;
  return copy_text(text + start, end - start);
}

char *qa_ui_script_strip_double_quotes(const char *text) {
  return strip_quotes(text, '"');
}
char *qa_ui_script_strip_single_quotes(const char *text) {
  return strip_quotes(text, '\'');
}

void qa_ui_script_location_destroy(qa_ui_script_location *location) {
  free(location->path);
  location->path = NULL;
}

const char *qa_ui_script_severity_name(qa_ui_script_severity severity) {
  return severity == QA_UI_SCRIPT_SEVERITY_ERROR ? "error" : "warning";
}

void qa_ui_script_diagnostic_destroy(qa_ui_script_diagnostic *diagnostic) {
  free(diagnostic->message);
  diagnostic->message = NULL;
  qa_ui_script_location_destroy(&diagnostic->location);
}

qa_save_value *qa_ui_script_diagnostic_capture(
    const qa_ui_script_diagnostic *diagnostic) {
  qa_save_value *object = qa_save_value_object();
  qa_save_value *location = qa_save_value_object();
  if (object == NULL || location == NULL) goto oom;
  if (!qa_save_value_set(
          location, "path",
          qa_save_value_string(diagnostic->location.path)))
    goto oom;
  if (!qa_save_value_set(
          location, "line",
          qa_save_value_number((double)diagnostic->location.line)))
    goto oom;
  if (!qa_save_value_set(
          location, "column",
          qa_save_value_number((double)diagnostic->location.column)))
    goto oom;
  if (!qa_save_value_set(
          object, "severity",
          qa_save_value_string(
              qa_ui_script_severity_name(diagnostic->severity))))
    goto oom;
  if (!qa_save_value_set(object, "message",
                         qa_save_value_string(diagnostic->message)))
    goto oom;
  if (!qa_save_value_set(object, "location", location)) goto oom;
  return object;
oom:
  qa_save_value_free(object);
  qa_save_value_free(location);
  return NULL;
}

bool qa_ui_script_read_diagnostic(const qa_save_reader *reader,
                                  qa_ui_script_diagnostic *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader field, location;
  const char *choices[2] = {"warning", "error"};
  if (!qa_save_reader_field(reader, "severity", &field)) return false;
  const char *severity = qa_save_reader_choice_string(&field, choices, 2);
  out->severity = strcmp(severity, "error") == 0
                      ? QA_UI_SCRIPT_SEVERITY_ERROR
                      : QA_UI_SCRIPT_SEVERITY_WARNING;
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "message", &field)) return false;
  out->message = copy_cstring(qa_save_reader_string(&field));
  qa_save_reader_destroy(&field);
  if (out->message == NULL) return false;
  if (!qa_save_reader_field(reader, "location", &location)) goto oom;
  if (!qa_save_reader_field(&location, "path", &field)) {
    qa_save_reader_destroy(&location);
    goto oom;
  }
  out->location.path = copy_cstring(qa_save_reader_string(&field));
  qa_save_reader_destroy(&field);
  if (out->location.path == NULL) {
    qa_save_reader_destroy(&location);
    goto oom;
  }
  if (!qa_save_reader_field(&location, "line", &field)) {
    qa_save_reader_destroy(&location);
    goto oom;
  }
  out->location.line = (int64_t)qa_save_reader_integer(&field);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&location, "column", &field)) {
    qa_save_reader_destroy(&location);
    goto oom;
  }
  out->location.column = (int64_t)qa_save_reader_integer(&field);
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&location);
  return true;
oom:
  qa_ui_script_diagnostic_destroy(out);
  return false;
}

const char *qa_ui_script_token_kind_name(qa_ui_script_token_kind kind) {
  switch (kind) {
    case QA_UI_SCRIPT_KIND_PRIMITIVE: return "primitive";
    case QA_UI_SCRIPT_KIND_STRING: return "string";
    case QA_UI_SCRIPT_KIND_LITERAL: return "literal";
    case QA_UI_SCRIPT_KIND_NUMBER: return "number";
    case QA_UI_SCRIPT_KIND_NAME: return "name";
    case QA_UI_SCRIPT_KIND_PUNCTUATION: return "punctuation";
  }
  return "";
}

qa_ui_script_token *qa_ui_script_token_retain(qa_ui_script_token *token) {
  if (token != NULL) token->refs++;
  return token;
}

void qa_ui_script_token_release(qa_ui_script_token *token) {
  if (token == NULL || --token->refs > 0) return;
  free(token->text);
  free(token->value);
  qa_ui_script_location_destroy(&token->location);
  free(token->leading_whitespace);
  free(token->metadata_reason);
  /* token->snapshot is weak; the snapshot owns this token. */
  free(token);
}

qa_ui_script_token *qa_ui_script_token_create(
    qa_ui_script_token_kind kind, const char *text, size_t text_length,
    const qa_ui_script_location *location, const char *leading_whitespace,
    size_t leading_whitespace_length, int64_t lines_crossed) {
  qa_ui_script_token *token = calloc(1, sizeof *token);
  if (token == NULL) return NULL;
  token->kind = kind;
  token->text = copy_text(text, text_length);
  token->value = copy_text("", 0);
  token->location.path = copy_cstring(location->path);
  token->leading_whitespace =
      copy_text(leading_whitespace, leading_whitespace_length);
  if (token->text == NULL || token->value == NULL
      || token->location.path == NULL
      || token->leading_whitespace == NULL) {
    token->refs = 1;
    qa_ui_script_token_release(token);
    return NULL;
  }
  token->text_length = text_length;
  token->location.line = location->line;
  token->location.column = location->column;
  token->leading_whitespace_length = leading_whitespace_length;
  token->lines_crossed = lines_crossed;
  token->refs = 1;
  return token;
}

bool qa_ui_script_token_set_value(qa_ui_script_token *token,
                                  const char *value, size_t length) {
  char *copy = copy_text(value, length);
  if (copy == NULL) return false;
  free(token->value);
  token->value = copy;
  token->value_length = length;
  return true;
}

void qa_ui_script_token_record_release(qa_ui_script_token_record *record) {
  qa_ui_script_token_release(record->token);
  record->token = NULL;
}

void qa_ui_script_fail_env_clear_pending(qa_ui_script_fail_env *env) {
  if (env->has_pending) qa_ui_script_diagnostic_destroy(&env->pending);
  env->has_pending = false;
  env->pending_identity = NULL;
}

/* ---- punctuation table ---- */

typedef struct owned_spec {
  char *text;
  int64_t punctuation;
  int64_t next; /* link: 1-based index+1, 0 = none */
} owned_spec;

static const qa_ui_script_punctuation_spec DEFAULT_SPECS[] = {
    {">>=", 1}, {"<<=", 2}, {"...", 3}, {"##", 4}, {"&&", 5}, {"||", 6},
    {">=", 7}, {"<=", 8}, {"==", 9}, {"!=", 10}, {"*=", 11}, {"/=", 12},
    {"%=", 13}, {"+=", 14}, {"-=", 15}, {"++", 16}, {"--", 17}, {"&=", 18},
    {"|=", 19}, {"^=", 20}, {">>", 21}, {"<<", 22}, {"->", 23}, {"::", 24},
    {".*", 25}, {"*", 26}, {"/", 27}, {"%", 28}, {"+", 29}, {"-", 30},
    {"=", 31}, {"&", 32}, {"|", 33}, {"^", 34}, {"~", 35}, {"!", 36},
    {">", 37}, {"<", 38}, {".", 39}, {",", 40}, {";", 41}, {":", 42},
    {"?", 43}, {"(", 44}, {")", 45}, {"{", 46}, {"}", 47}, {"[", 48},
    {"]", 49}, {"\\", 50}, {"#", 51}, {"$", 52}};
#define DEFAULT_SPEC_COUNT (sizeof DEFAULT_SPECS / sizeof DEFAULT_SPECS[0])

/* TS punctuationTable(): longest-first insertion per first byte. */
static void build_table(owned_spec *specs, size_t count,
                        uint32_t heads[256]) {
  memset(heads, 0, 256 * sizeof heads[0]);
  for (size_t index = 0; index < count; index++) specs[index].next = 0;
  for (size_t index = 0; index < count; index++) {
    owned_spec *added = &specs[index];
    unsigned int character = (uint8_t)added->text[0];
    if (character > 127)
      qa_range_error("punctuation table index exceeds its source signed-char allocation");
    int64_t pointer = heads[character];
    owned_spec *previous = NULL;
    while (pointer != 0) {
      owned_spec *current = &specs[pointer - 1];
      if (strlen(current->text) < strlen(added->text)) break;
      previous = current;
      pointer = current->next;
    }
    added->next = pointer;
    if (previous == NULL) heads[character] = (uint32_t)(index + 1);
    else previous->next = (int64_t)(index + 1);
  }
}

static bool copy_specs(const qa_ui_script_punctuation_spec *specs,
                       size_t count, owned_spec **out) {
  *out = calloc(count == 0 ? 1 : count, sizeof **out);
  if (*out == NULL) return false;
  for (size_t index = 0; index < count; index++) {
    (*out)[index].text = copy_cstring(specs[index].text);
    if ((*out)[index].text == NULL) {
      for (size_t done = 0; done < index; done++) free((*out)[done].text);
      free(*out);
      *out = NULL;
      return false;
    }
    (*out)[index].punctuation = specs[index].punctuation;
  }
  return true;
}

static void free_specs(owned_spec *specs, size_t count) {
  if (specs == NULL) return;
  for (size_t index = 0; index < count; index++) free(specs[index].text);
  free(specs);
}

qa_ui_source_script_storage *qa_ui_script_allocate_source(
    int64_t length, const char *path, const qa_ui_script_memory *memory) {
  qa_ui_source_script_storage *source =
      qa_ui_source_script_allocate(length, path, memory);
  if (source == NULL) return NULL;
  owned_spec *specs = NULL;
  if (!copy_specs(DEFAULT_SPECS, DEFAULT_SPEC_COUNT, &specs)) {
    qa_ui_source_script_destroy(source);
    return NULL;
  }
  uint32_t heads[256];
  build_table(specs, DEFAULT_SPEC_COUNT, heads);
  free_specs(specs, DEFAULT_SPEC_COUNT);
  qa_ui_source_script_set_default_punctuations(source, heads, 256);
  return source;
}

/* ---- lexer ---- */

struct qa_ui_script_lexer {
  qa_ui_source_script_storage *storage; /* owned; NULL = text source */
  uint8_t *text;
  size_t text_length;
  char *diagnostic_path;
  int64_t diagnostic_flags;
  int64_t max_token_length;
  qa_ui_script_report_fn report;
  void *report_ctx;
  qa_ui_script_diagnostic *reported;
  size_t reported_count, reported_cap;
  int64_t diagnostic_offset, diagnostic_line;
  int64_t diagnostic_last_offset, diagnostic_last_line;
  int64_t diagnostic_whitespace_start, diagnostic_whitespace_end;
  int64_t column;
  bool diagnostic_token_available;
  qa_ui_source_token_memory *diagnostic_token;
  qa_ui_source_token_memory *output;
  qa_ui_source_token_context token_context;
  owned_spec *specs;
  size_t spec_count;
  uint32_t heads[256];
  qa_ui_script_fail_env own_env;
  qa_ui_script_fail_env *env;
};

static bool push_diagnostic(qa_ui_script_lexer *lexer,
                            qa_ui_script_diagnostic *diagnostic) {
  if (lexer->reported_count == lexer->reported_cap) {
    size_t cap = lexer->reported_cap == 0 ? 8 : lexer->reported_cap * 2;
    qa_ui_script_diagnostic *grown =
        realloc(lexer->reported, cap * sizeof *grown);
    if (grown == NULL) return false;
    lexer->reported = grown;
    lexer->reported_cap = cap;
  }
  lexer->reported[lexer->reported_count++] = *diagnostic;
  return true;
}

static int64_t get_offset(qa_ui_script_lexer *lexer) {
  return lexer->storage != NULL
             ? qa_ui_source_script_offset(lexer->storage)
             : lexer->diagnostic_offset;
}
static void set_offset(qa_ui_script_lexer *lexer, int64_t value) {
  if (lexer->storage != NULL) qa_ui_source_script_set_offset(lexer->storage, value);
  else lexer->diagnostic_offset = value;
}
static int64_t get_line(qa_ui_script_lexer *lexer) {
  return lexer->storage != NULL ? qa_ui_source_script_line(lexer->storage)
                                : lexer->diagnostic_line;
}
static void set_line(qa_ui_script_lexer *lexer, int64_t value) {
  if (lexer->storage != NULL)
    qa_ui_source_script_set_line(lexer->storage, (int32_t)value);
  else lexer->diagnostic_line = value;
}
static int64_t get_flags(qa_ui_script_lexer *lexer) {
  return lexer->storage != NULL
             ? qa_ui_source_script_flags(lexer->storage)
             : lexer->diagnostic_flags;
}
static bool get_token_available(qa_ui_script_lexer *lexer) {
  return lexer->storage != NULL
             ? qa_ui_source_script_token_available(lexer->storage)
             : lexer->diagnostic_token_available;
}
static qa_ui_source_token_memory *retained_token(qa_ui_script_lexer *lexer) {
  return lexer->storage != NULL
             ? qa_ui_source_script_token(lexer->storage)
             : lexer->diagnostic_token;
}
static size_t source_length(qa_ui_script_lexer *lexer) {
  if (lexer->storage == NULL) return lexer->text_length;
  size_t length = 0;
  qa_ui_source_script_buffer(lexer->storage, &length);
  return length;
}
static const uint8_t *source_bytes(qa_ui_script_lexer *lexer,
                                   size_t *out_length) {
  if (lexer->storage == NULL) {
    *out_length = lexer->text_length;
    return lexer->text;
  }
  return qa_ui_source_script_buffer(lexer->storage, out_length);
}

/* -1 is TS undefined. */
static int peek_at(qa_ui_script_lexer *lexer, int64_t ahead) {
  size_t length = 0;
  const uint8_t *bytes = source_bytes(lexer, &length);
  int64_t offset = get_offset(lexer) + ahead;
  if (offset < 0 || offset >= (int64_t)length) return -1;
  return bytes[offset];
}

static bool source_starts_with(qa_ui_script_lexer *lexer, const char *text) {
  size_t length = 0;
  const uint8_t *bytes = source_bytes(lexer, &length);
  int64_t start = get_offset(lexer);
  size_t count = strlen(text);
  if (start < 0 || (size_t)start + count > length) return false;
  return memcmp(bytes + start, text, count) == 0;
}

/* Malloc'd slice (NUL-terminated); *out_length is the byte length. */
static char *source_slice(qa_ui_script_lexer *lexer, int64_t start,
                          int64_t end, size_t *out_length) {
  size_t length = 0;
  const uint8_t *bytes = source_bytes(lexer, &length);
  if (start < 0 || end < start || end > (int64_t)length)
    qa_range_error("script token text is outside its allocation");
  *out_length = (size_t)(end - start);
  return copy_text((const char *)bytes + start, *out_length);
}

static char *current_path_copy(qa_ui_script_lexer *lexer) {
  if (lexer->storage == NULL) return copy_cstring(lexer->diagnostic_path);
  return qa_ui_source_script_path(lexer->storage);
}

static bool make_diagnostic(qa_ui_script_lexer *lexer,
                            qa_ui_script_severity severity,
                            const char *message,
                            qa_ui_script_diagnostic *out) {
  memset(out, 0, sizeof *out);
  out->severity = severity;
  out->message = copy_cstring(message);
  out->location.path = current_path_copy(lexer);
  out->location.line = get_line(lexer);
  out->location.column = lexer->column;
  if (out->message == NULL || out->location.path == NULL) {
    qa_ui_script_diagnostic_destroy(out);
    return false;
  }
  return true;
}

static void lexer_error(qa_ui_script_lexer *lexer, const char *message) {
  if ((get_flags(lexer) & QA_UI_SCRIPT_LEXER_NO_ERRORS) != 0) return;
  qa_ui_script_diagnostic diagnostic;
  if (!make_diagnostic(lexer, QA_UI_SCRIPT_SEVERITY_ERROR, message,
                       &diagnostic)
      || !push_diagnostic(lexer, &diagnostic)) {
    qa_ui_script_diagnostic_destroy(&diagnostic);
    qa_range_error("script diagnostic allocation failed");
  }
  if (lexer->report != NULL)
    lexer->report(lexer->report_ctx,
                  &lexer->reported[lexer->reported_count - 1]);
}

static void lexer_warn(qa_ui_script_lexer *lexer, const char *message) {
  if ((get_flags(lexer) & QA_UI_SCRIPT_LEXER_NO_WARNINGS) != 0) return;
  qa_ui_script_diagnostic diagnostic;
  if (!make_diagnostic(lexer, QA_UI_SCRIPT_SEVERITY_WARNING, message,
                       &diagnostic)
      || !push_diagnostic(lexer, &diagnostic)) {
    qa_ui_script_diagnostic_destroy(&diagnostic);
    qa_range_error("script diagnostic allocation failed");
  }
  if (lexer->report != NULL)
    lexer->report(lexer->report_ctx,
                  &lexer->reported[lexer->reported_count - 1]);
}

_Noreturn static void lexer_fail(qa_ui_script_lexer *lexer,
                                 const char *message) {
  qa_ui_script_diagnostic diagnostic;
  if (!make_diagnostic(lexer, QA_UI_SCRIPT_SEVERITY_ERROR, message,
                       &diagnostic))
    qa_range_error("script diagnostic allocation failed");
  const qa_ui_script_diagnostic *identity = NULL;
  if ((get_flags(lexer) & QA_UI_SCRIPT_LEXER_NO_ERRORS) == 0) {
    /* The reported array and the pending copy each own their text. */
    qa_ui_script_diagnostic reported;
    if (!make_diagnostic(lexer, QA_UI_SCRIPT_SEVERITY_ERROR, message,
                         &reported)
        || !push_diagnostic(lexer, &reported)) {
      qa_ui_script_diagnostic_destroy(&reported);
      qa_ui_script_diagnostic_destroy(&diagnostic);
      qa_range_error("script diagnostic allocation failed");
    }
    identity = &lexer->reported[lexer->reported_count - 1];
    if (lexer->report != NULL) lexer->report(lexer->report_ctx, identity);
  }
  qa_ui_script_fail_throw(lexer->env, &diagnostic, identity);
}

static bool is_digit(int character) {
  return character >= '0' && character <= '9';
}
static bool is_name_start(int character) {
  return (character >= 'a' && character <= 'z')
         || (character >= 'A' && character <= 'Z') || character == '_';
}
static bool is_name_character(int character) {
  return is_name_start(character) || is_digit(character);
}
/* TS quirk: only a-f and 'A' count in source numbers. */
static bool is_source_number_hex_digit(int character) {
  return is_digit(character) || (character >= 'a' && character <= 'f')
         || character == 'A';
}
static bool is_source_escape_hex_digit(int character) {
  return is_digit(character) || (character >= 'a' && character <= 'z')
         || (character >= 'A' && character <= 'Z');
}

static void advance(qa_ui_script_lexer *lexer, int64_t count) {
  int64_t length = (int64_t)source_length(lexer);
  for (int64_t moved = 0; moved < count && get_offset(lexer) < length;
       moved++) {
    int character = peek_at(lexer, 0);
    set_offset(lexer, get_offset(lexer) + 1);
    if (character == '\n') {
      set_line(lexer, get_line(lexer) + 1);
      lexer->column = 1;
    } else {
      lexer->column++;
    }
  }
}

static bool at_end(qa_ui_script_lexer *lexer) {
  return get_offset(lexer) >= (int64_t)source_length(lexer)
         || peek_at(lexer, 0) == 0;
}

static void read_whitespace(qa_ui_script_lexer *lexer) {
  while (!at_end(lexer)) {
    int code = peek_at(lexer, 0);
    if (code >= 0 && (code <= 32 || (code >= 128 && code <= 255))) {
      advance(lexer, 1);
      continue;
    }
    if (source_starts_with(lexer, "//")) {
      advance(lexer, 2);
      while (!at_end(lexer) && peek_at(lexer, 0) != '\n') advance(lexer, 1);
      continue;
    }
    if (source_starts_with(lexer, "/*")) {
      advance(lexer, 2);
      while (!at_end(lexer) && !source_starts_with(lexer, "*/"))
        advance(lexer, 1);
      if (source_starts_with(lexer, "*/")) advance(lexer, 2);
      continue;
    }
    return;
  }
}

static int64_t finish_escape_value(qa_ui_script_lexer *lexer,
                                   int64_t value) {
  if (value > 255) {
    set_offset(lexer, get_offset(lexer) - 1);
    lexer->column--;
    lexer_warn(lexer, "too large value in escape character");
    set_offset(lexer, get_offset(lexer) + 1);
    lexer->column++;
    return 255;
  }
  return value;
}

static int64_t decimal_escape_value(qa_ui_script_lexer *lexer,
                                    const char *text, size_t length) {
  int64_t value = 0;
  for (size_t index = 0; index < length; index++) {
    value = value * 10 + ((uint8_t)text[index] - 48);
    if (value > 2147483647)
      lexer_fail(lexer, "decimal escape exceeds the source signed-int range\n");
  }
  return finish_escape_value(lexer, value);
}

static int64_t source_escape_hex_value(qa_ui_script_lexer *lexer,
                                       const char *text, size_t length) {
  int64_t value = 0;
  for (size_t index = 0; index < length; index++) {
    int character = (uint8_t)text[index];
    int64_t digit;
    if (character >= '0' && character <= '9') digit = character - '0';
    else if (character >= 'A' && character <= 'Z')
      digit = character - 'A' + 10;
    else digit = character - 'a' + 10;
    value = value * 16 + digit;
    if (value > 2147483647)
      lexer_fail(lexer, "hex escape exceeds the source signed-int range\n");
  }
  return finish_escape_value(lexer, value);
}

/* Returns the escaped byte (TS readEscape's one-character string). */
static int read_escape(qa_ui_script_lexer *lexer) {
  advance(lexer, 1);
  int character = peek_at(lexer, 0);
  if (character < 0) {
    lexer_error(lexer, "unknown escape char");
    return 0;
  }
  int escaped = -1;
  switch (character) {
    case '\\': escaped = '\\'; break;
    case 'n': escaped = '\n'; break;
    case 'r': escaped = '\r'; break;
    case 't': escaped = '\t'; break;
    case 'v': escaped = '\v'; break;
    case 'b': escaped = '\b'; break;
    case 'f': escaped = '\f'; break;
    case 'a': escaped = 0x07; break;
    case '\'': escaped = '\''; break;
    case '"': escaped = '"'; break;
    case '?': escaped = '?'; break;
    default: break;
  }
  if (escaped >= 0) {
    advance(lexer, 1);
    return escaped;
  }
  if (character == 'x') {
    advance(lexer, 1);
    int64_t start = get_offset(lexer);
    while (is_source_escape_hex_digit(peek_at(lexer, 0))) advance(lexer, 1);
    size_t length = 0;
    char *text = source_slice(lexer, start, get_offset(lexer), &length);
    if (text == NULL) qa_range_error("script token allocation failed");
    int64_t value = source_escape_hex_value(lexer, text, length);
    free(text);
    return (int)value;
  }
  if (is_digit(character)) {
    int64_t start = get_offset(lexer);
    while (is_digit(peek_at(lexer, 0))) advance(lexer, 1);
    size_t length = 0;
    char *text = source_slice(lexer, start, get_offset(lexer), &length);
    if (text == NULL) qa_range_error("script token allocation failed");
    int64_t value = decimal_escape_value(lexer, text, length);
    free(text);
    return (int)value;
  }
  lexer_error(lexer, "unknown escape char");
  return 0;
}

static void fail_length(qa_ui_script_lexer *lexer, const char *label) {
  char message[160];
  snprintf(message, sizeof message, "%s longer than MAX_TOKEN = %lld",
           label, (long long)lexer->max_token_length);
  lexer_fail(lexer, message);
}

static void ensure_token_length(qa_ui_script_lexer *lexer, int64_t start,
                                const char *label, int64_t reserved) {
  if (get_offset(lexer) - start >= lexer->max_token_length - reserved)
    fail_length(lexer, label);
}

/* readQuoted: writes bytes; returns the value length (text is
 * quote+value+quote). */
static size_t read_quoted(qa_ui_script_lexer *lexer, int quote,
                          qa_ui_source_token_memory *output) {
  size_t value_length = 0;
  qa_ui_source_token_set_type(output, quote == '"' ? 1 : 2);
  qa_ui_source_token_set_string_byte(output, 0, quote);
  advance(lexer, 1);
  for (;;) {
    if ((int64_t)value_length + 1 >= lexer->max_token_length - 2)
      fail_length(lexer, "string");
    int character = peek_at(lexer, 0);
    if (character < 0 || character == 0) {
      qa_ui_source_token_set_string_byte(output, (int64_t)value_length + 1,
                                         0);
      lexer_fail(lexer, "missing trailing quote");
    }
    if (character == '\n') {
      qa_ui_source_token_set_string_byte(output, (int64_t)value_length + 1,
                                         0);
      uint8_t *bytes = qa_ui_source_token_bytes(output);
      size_t shown = 0;
      while (shown < value_length && bytes[1 + shown] != 0) shown++;
      char *message = malloc(shown + 32);
      if (message == NULL) qa_range_error("script token allocation failed");
      memcpy(message, "newline inside string ", 22);
      message[22] = (char)quote;
      memcpy(message + 23, bytes + 1, shown);
      message[23 + shown] = '\0';
      lexer_fail(lexer, message);
    }
    if (character == '\\'
        && (get_flags(lexer) & QA_UI_SCRIPT_LEXER_NO_STRING_ESCAPES) == 0) {
      int escaped = read_escape(lexer);
      qa_ui_source_token_set_string_byte(output, (int64_t)value_length + 1,
                                         escaped);
      value_length++;
      continue;
    }
    if (character == quote) {
      advance(lexer, 1);
      if ((get_flags(lexer) & QA_UI_SCRIPT_LEXER_NO_STRING_CONCATENATION)
          != 0)
        break;
      int64_t saved_offset = get_offset(lexer);
      int64_t saved_line = get_line(lexer);
      int64_t saved_column = lexer->column;
      read_whitespace(lexer);
      if (peek_at(lexer, 0) == quote) {
        advance(lexer, 1);
        continue;
      }
      set_offset(lexer, saved_offset);
      set_line(lexer, saved_line);
      lexer->column = saved_column;
      break;
    }
    qa_ui_source_token_set_string_byte(output, (int64_t)value_length + 1,
                                       character);
    value_length++;
    advance(lexer, 1);
  }
  qa_ui_source_token_set_string_byte(output, (int64_t)value_length + 1,
                                     quote);
  qa_ui_source_token_set_string_byte(output, (int64_t)value_length + 2, 0);
  qa_ui_source_token_set_subtype(output, (int32_t)(value_length + 2));
  return value_length;
}

static size_t read_name(qa_ui_script_lexer *lexer,
                        qa_ui_source_token_memory *output) {
  int64_t start = get_offset(lexer);
  qa_ui_source_token_set_type(output, 4);
  while (is_name_character(peek_at(lexer, 0))) {
    int character = peek_at(lexer, 0);
    if (character < 0)
      qa_ui_script_plain_error("source name character is missing");
    qa_ui_source_token_set_string_byte(output, get_offset(lexer) - start,
                                       character);
    advance(lexer, 1);
    if (get_offset(lexer) - start >= lexer->max_token_length)
      fail_length(lexer, "name");
  }
  size_t length = (size_t)(get_offset(lexer) - start);
  qa_ui_source_token_set_string_byte(output, (int64_t)length, 0);
  qa_ui_source_token_set_subtype(output, (int32_t)length);
  return length;
}

static void number_values(const char *text, size_t length, int64_t flags,
                          uint32_t *out_integer, double *out_float) {
  double value = 0;
  if ((flags & QA_UI_SCRIPT_NUMBER_FLOAT) != 0) {
    uint32_t divisor = 0;
    size_t index = 0;
    while (index < length) {
      if (text[index] == '.') {
        if (divisor != 0) {
          *out_integer = 0;
          *out_float = value;
          return;
        }
        divisor = 10;
        index++;
      }
      int digit = (index < length ? (uint8_t)text[index] : 0) - 48;
      if (divisor != 0) {
        value += digit / (double)divisor;
        divisor = divisor * 10u; /* uint32 wrap, TS >>> 0 */
      } else {
        value = value * 10 + digit;
      }
      index++;
    }
    /* The C double-to-unsigned-long conversion has no range check; the
       32-bit result wraps (TS Math.trunc(value) >>> 0). */
    *out_integer = qa_js_to_uint32(trunc(value));
    *out_float = value;
    return;
  }
  uint32_t radix = 10;
  size_t index = 0;
  if ((flags & QA_UI_SCRIPT_NUMBER_HEX) != 0) {
    radix = 16;
    index = 2;
  } else if ((flags & QA_UI_SCRIPT_NUMBER_OCTAL) != 0) {
    radix = 8;
    index = 1;
  } else if ((flags & QA_UI_SCRIPT_NUMBER_BINARY) != 0) {
    radix = 2;
    index = 2;
  }
  uint32_t integer = 0;
  for (; index < length; index++) {
    int code = (uint8_t)text[index];
    int64_t digit = code >= 97 && code <= 102 ? code - 87
                    : code >= 65 && code <= 70 ? code - 55
                                               : code - 48;
    integer = integer * radix + (uint32_t)digit;
  }
  *out_integer = integer;
  *out_float = (double)integer;
}

static size_t read_number(qa_ui_script_lexer *lexer,
                          qa_ui_source_token_memory *output) {
  int64_t start = get_offset(lexer);
  int64_t flags = 0;
  qa_ui_source_token_set_type(output, 3);
  /* append(): copy current byte into the token and advance. */
  if (peek_at(lexer, 0) == '0'
      && (peek_at(lexer, 1) == 'x' || peek_at(lexer, 1) == 'X')) {
    for (int count = 0; count < 2; count++) {
      qa_ui_source_token_set_string_byte(output, get_offset(lexer) - start,
                                         peek_at(lexer, 0));
      advance(lexer, 1);
    }
    while (is_source_number_hex_digit(peek_at(lexer, 0))) {
      qa_ui_source_token_set_string_byte(output, get_offset(lexer) - start,
                                         peek_at(lexer, 0));
      advance(lexer, 1);
      ensure_token_length(lexer, start, "hexadecimal number", 0);
    }
    flags |= QA_UI_SCRIPT_NUMBER_HEX;
    qa_ui_source_token_set_subtype(output, (int32_t)flags);
  } else if (peek_at(lexer, 0) == '0'
             && (peek_at(lexer, 1) == 'b' || peek_at(lexer, 1) == 'B')
             && (get_flags(lexer) & QA_UI_SCRIPT_LEXER_NO_BINARY_NUMBERS)
                    == 0) {
    for (int count = 0; count < 2; count++) {
      qa_ui_source_token_set_string_byte(output, get_offset(lexer) - start,
                                         peek_at(lexer, 0));
      advance(lexer, 1);
    }
    while (peek_at(lexer, 0) == '0' || peek_at(lexer, 0) == '1') {
      qa_ui_source_token_set_string_byte(output, get_offset(lexer) - start,
                                         peek_at(lexer, 0));
      advance(lexer, 1);
      ensure_token_length(lexer, start, "binary number", 0);
    }
    flags |= QA_UI_SCRIPT_NUMBER_BINARY;
    qa_ui_source_token_set_subtype(output, (int32_t)flags);
  } else {
    bool octal = peek_at(lexer, 0) == '0';
    int dots = 0;
    for (;;) {
      int character = peek_at(lexer, 0);
      if (character == '.') dots++;
      else if (character == '8' || character == '9') octal = false;
      else if (!is_digit(character)) break;
      qa_ui_source_token_set_string_byte(output, get_offset(lexer) - start,
                                         character);
      advance(lexer, 1);
      ensure_token_length(lexer, start, "number", 1);
    }
    flags |= octal ? QA_UI_SCRIPT_NUMBER_OCTAL : QA_UI_SCRIPT_NUMBER_DECIMAL;
    if (dots > 0) flags |= QA_UI_SCRIPT_NUMBER_FLOAT;
    qa_ui_source_token_set_subtype(output, (int32_t)flags);
  }
  int64_t text_end = get_offset(lexer);
  for (int suffix = 0; suffix < 2; suffix++) {
    int character = peek_at(lexer, 0);
    if ((character == 'l' || character == 'L')
        && (flags & QA_UI_SCRIPT_NUMBER_LONG) == 0) {
      flags |= QA_UI_SCRIPT_NUMBER_LONG;
      advance(lexer, 1);
      qa_ui_source_token_set_subtype(output, (int32_t)flags);
    } else if ((character == 'u' || character == 'U')
               && (flags & QA_UI_SCRIPT_NUMBER_UNSIGNED) == 0
               && (flags & QA_UI_SCRIPT_NUMBER_FLOAT) == 0) {
      flags |= QA_UI_SCRIPT_NUMBER_UNSIGNED;
      advance(lexer, 1);
      qa_ui_source_token_set_subtype(output, (int32_t)flags);
    }
  }
  if ((flags & QA_UI_SCRIPT_NUMBER_FLOAT) == 0)
    flags |= QA_UI_SCRIPT_NUMBER_INTEGER;
  size_t length = 0;
  char *text = source_slice(lexer, start, text_end, &length);
  if (text == NULL) qa_range_error("script token allocation failed");
  qa_ui_source_token_set_string_byte(output, (int64_t)length, 0);
  uint32_t integer_value;
  double float_value;
  number_values(text, length, flags, &integer_value, &float_value);
  free(text);
  qa_ui_source_token_set_integer_value(output, integer_value);
  qa_ui_source_token_set_float_value(output, float_value);
  qa_ui_source_token_set_subtype(output, (int32_t)flags);
  return length;
}

static const owned_spec *read_punctuation(qa_ui_script_lexer *lexer) {
  int character = peek_at(lexer, 0);
  if (character < 0) return NULL;
  uint32_t head = lexer->storage == NULL
                      ? lexer->heads[character]
                      : qa_ui_source_script_punctuation_head(lexer->storage,
                                                             character);
  for (int64_t pointer = head; pointer != 0;) {
    if (pointer < 1 || pointer > (int64_t)lexer->spec_count)
      qa_range_error("script punctuation head does not identify a default record");
    const owned_spec *link = &lexer->specs[pointer - 1];
    if (source_starts_with(lexer, link->text)) return link;
    pointer = link->next;
  }
  return NULL;
}

bool qa_ui_script_lexer_next_into(qa_ui_script_lexer *lexer,
                                  qa_ui_source_token_memory *output,
                                  qa_ui_script_token_record *out) {
  memset(out, 0, sizeof *out);
  if (get_token_available(lexer)) {
    if (lexer->storage != NULL)
      qa_ui_source_script_set_token_available(lexer->storage, false);
    else lexer->diagnostic_token_available = false;
    qa_ui_source_token_copy_from(output, retained_token(lexer));
    if (!qa_ui_source_token_read_record(output, &lexer->token_context,
                                        false, 0, out))
      qa_range_error("script token allocation failed");
    return true;
  }
  if (lexer->storage != NULL) qa_ui_source_script_begin_token(lexer->storage);
  else {
    lexer->diagnostic_last_offset = get_offset(lexer);
    lexer->diagnostic_last_line = get_line(lexer);
    lexer->diagnostic_whitespace_start = get_offset(lexer);
  }
  qa_ui_source_token_clear(output);
  qa_ui_source_token_set_whitespace_start(
      output, (uint32_t)(QA_UI_SOURCE_SCRIPT_BYTES + get_offset(lexer)));
  int64_t whitespace_start = get_offset(lexer);
  int64_t line_before = get_line(lexer);
  read_whitespace(lexer);
  if (at_end(lexer)) return false;
  if (lexer->storage != NULL)
    qa_ui_source_script_end_whitespace(lexer->storage);
  else lexer->diagnostic_whitespace_end = get_offset(lexer);
  qa_ui_source_token_set_whitespace_end(
      output, (uint32_t)(QA_UI_SOURCE_SCRIPT_BYTES + get_offset(lexer)));
  qa_ui_source_token_set_line(output, (int32_t)get_line(lexer));
  int64_t lines_crossed = get_line(lexer) - line_before;
  qa_ui_source_token_set_lines_crossed(output, (int32_t)lines_crossed);
  size_t leading_length = 0;
  char *leading =
      source_slice(lexer, whitespace_start, get_offset(lexer), &leading_length);
  if (leading == NULL) qa_range_error("script token allocation failed");
  int64_t token_column = lexer->column;
  int character = peek_at(lexer, 0);
  size_t text_extent = 0;
  bool primitive = false;
  if (character == '"' || character == '\'') {
    text_extent = read_quoted(lexer, character, output) + 2;
  } else if (is_digit(character)
             || (character == '.' && is_digit(peek_at(lexer, 1)))) {
    text_extent = read_number(lexer, output);
  } else if ((get_flags(lexer) & QA_UI_SCRIPT_LEXER_PRIMITIVE) != 0) {
    primitive = true;
    int64_t length = 0;
    for (;;) {
      int current = peek_at(lexer, 0);
      if (current < 0 || current >= 128 || current <= ' ' || current == ';')
        break;
      if (length >= lexer->max_token_length)
        fail_length(lexer, "primitive token");
      qa_ui_source_token_set_string_byte(output, length++, current);
      advance(lexer, 1);
    }
    qa_ui_source_token_set_string_byte(output, length, 0);
    text_extent = (size_t)length;
  } else if (is_name_start(character)) {
    text_extent = read_name(lexer, output);
  } else {
    const owned_spec *spec = read_punctuation(lexer);
    if (spec == NULL) {
      free(leading);
      lexer_fail(lexer, "can't read token");
    }
    size_t spec_length = strlen(spec->text);
    qa_ui_source_token_write_string(output, spec->text, spec_length);
    advance(lexer, (int64_t)spec_length);
    qa_ui_source_token_set_type(output, 5);
    qa_ui_source_token_set_subtype(output, (int32_t)spec->punctuation);
    text_extent = spec_length;
  }
  char *path = current_path_copy(lexer);
  if (path == NULL) {
    free(leading);
    qa_range_error("script token allocation failed");
  }
  qa_ui_source_token_context context;
  memset(&context, 0, sizeof context);
  bool context_ok = qa_ui_source_token_context_set(
      &context, path, token_column, leading, leading_length);
  free(path);
  free(leading);
  if (!context_ok) qa_range_error("script token allocation failed");
  bool read_ok = qa_ui_source_token_read_record(
      output, &context, true, (int64_t)text_extent, out);
  if (!read_ok) {
    qa_ui_source_token_context_destroy(&context);
    qa_range_error("script token allocation failed");
  }
  if (primitive) {
    qa_ui_source_token_context_destroy(&context);
    return true;
  }
  qa_ui_source_token_copy_from(retained_token(lexer), output);
  qa_ui_source_token_context_destroy(&lexer->token_context);
  lexer->token_context = context;
  return true;
}

bool qa_ui_script_lexer_next(qa_ui_script_lexer *lexer,
                             qa_ui_script_token **out) {
  qa_ui_script_token_record record;
  if (!qa_ui_script_lexer_next_into(lexer, lexer->output, &record))
    return false;
  *out = record.token;
  return true;
}

/* TS readExpectedInto: catch a source failure as false. */
static bool read_expected_into(qa_ui_script_lexer *lexer,
                               qa_ui_source_token_memory *output) {
  qa_ui_script_catch catch_block;
  catch_block.prev = lexer->env->top;
  lexer->env->top = &catch_block;
  bool result;
  if (setjmp(catch_block.buf) == 0) {
    qa_ui_script_token_record record;
    result = qa_ui_script_lexer_next_into(lexer, output, &record);
    if (result) qa_ui_script_token_record_release(&record);
    lexer->env->top = catch_block.prev;
  } else {
    lexer->env->top = catch_block.prev;
    qa_ui_script_fail_env_clear_pending(lexer->env);
    result = false;
  }
  return result;
}

static qa_ui_script_lexer *lexer_alloc(const char *path,
                                       const qa_ui_script_lexer_options *options,
                                       qa_ui_script_fail_env *env) {
  qa_ui_script_lexer *lexer = calloc(1, sizeof *lexer);
  if (lexer == NULL) return NULL;
  lexer->diagnostic_path = copy_cstring(path);
  lexer->diagnostic_flags =
      options != NULL && options->has_flags ? options->flags : 0;
  lexer->max_token_length = options != NULL && options->has_max_token_length
                                ? options->max_token_length
                                : QA_UI_SCRIPT_DEFAULT_TOKEN_LIMIT;
  if (options != NULL) {
    lexer->report = options->report;
    lexer->report_ctx = options->report_ctx;
  }
  lexer->diagnostic_line = 1;
  lexer->diagnostic_last_line = 1;
  lexer->column = 1;
  qa_ui_script_fail_env_init(&lexer->own_env);
  lexer->env = env != NULL ? env : &lexer->own_env;
  lexer->diagnostic_token = qa_ui_source_token_memory_create_local();
  lexer->output = qa_ui_source_token_memory_create_local();
  bool context_ok = qa_ui_source_token_context_set(&lexer->token_context,
                                                   path, 1, "", 0);
  owned_spec *specs = NULL;
  bool specs_ok = context_ok
                  && copy_specs(DEFAULT_SPECS, DEFAULT_SPEC_COUNT, &specs);
  if (lexer->diagnostic_path == NULL || lexer->diagnostic_token == NULL
      || lexer->output == NULL || !specs_ok) {
    free_specs(specs, DEFAULT_SPEC_COUNT);
    qa_ui_script_lexer_destroy(lexer);
    return NULL;
  }
  lexer->specs = specs;
  lexer->spec_count = DEFAULT_SPEC_COUNT;
  build_table(lexer->specs, lexer->spec_count, lexer->heads);
  if (lexer->max_token_length < 4)
    qa_range_error("maxTokenLength must be an integer of at least 4");
  return lexer;
}

qa_ui_script_lexer *qa_ui_script_lexer_create_text_env(
    const uint8_t *text, size_t length, const char *path,
    const qa_ui_script_lexer_options *options,
    qa_ui_script_fail_env *env) {
  qa_ui_script_lexer *lexer = lexer_alloc(path, options, env);
  if (lexer == NULL) return NULL;
  if (options != NULL && options->memory != NULL) {
    qa_ui_source_script_storage *stored =
        qa_ui_script_allocate_source((int64_t)length, path, options->memory);
    if (stored == NULL) {
      qa_ui_script_lexer_destroy(lexer);
      return NULL;
    }
    qa_ui_source_script_copy_text(stored, text, length);
    lexer->storage = stored;
  } else {
    lexer->text = malloc(length == 0 ? 1 : length);
    if (lexer->text == NULL) {
      qa_ui_script_lexer_destroy(lexer);
      return NULL;
    }
    memcpy(lexer->text, text, length);
    lexer->text_length = length;
  }
  if (lexer->storage != NULL && options != NULL && options->has_flags)
    qa_ui_source_script_set_flags(lexer->storage, (int32_t)options->flags);
  return lexer;
}

qa_ui_script_lexer *qa_ui_script_lexer_create_text(
    const uint8_t *text, size_t length, const char *path,
    const qa_ui_script_lexer_options *options) {
  return qa_ui_script_lexer_create_text_env(text, length, path, options,
                                            NULL);
}

qa_ui_script_lexer *qa_ui_script_lexer_create_storage_env(
    qa_ui_source_script_storage *storage, const char *path,
    const qa_ui_script_lexer_options *options,
    qa_ui_script_fail_env *env) {
  qa_ui_script_lexer *lexer = lexer_alloc(path, options, env);
  if (lexer == NULL) return NULL;
  lexer->storage = storage;
  if (options != NULL && options->has_flags)
    qa_ui_source_script_set_flags(storage, (int32_t)options->flags);
  return lexer;
}

qa_ui_script_lexer *qa_ui_script_lexer_create_storage(
    qa_ui_source_script_storage *storage, const char *path,
    const qa_ui_script_lexer_options *options) {
  return qa_ui_script_lexer_create_storage_env(storage, path, options, NULL);
}

void qa_ui_script_lexer_dispose(qa_ui_script_lexer *lexer) {
  if (lexer->storage != NULL) qa_ui_source_script_dispose(lexer->storage);
}

void qa_ui_script_lexer_destroy(qa_ui_script_lexer *lexer) {
  if (lexer == NULL) return;
  if (lexer->storage != NULL) qa_ui_source_script_destroy(lexer->storage);
  free(lexer->text);
  free(lexer->diagnostic_path);
  for (size_t index = 0; index < lexer->reported_count; index++)
    qa_ui_script_diagnostic_destroy(&lexer->reported[index]);
  free(lexer->reported);
  qa_ui_source_token_memory_destroy(lexer->diagnostic_token);
  qa_ui_source_token_memory_destroy(lexer->output);
  qa_ui_source_token_context_destroy(&lexer->token_context);
  free_specs(lexer->specs, lexer->spec_count);
  qa_ui_script_fail_env_destroy(&lexer->own_env);
  free(lexer);
}

qa_ui_source_script_storage *qa_ui_script_lexer_source_storage(
    qa_ui_script_lexer *lexer) {
  return lexer->storage;
}

const qa_ui_script_diagnostic *qa_ui_script_lexer_diagnostics(
    const qa_ui_script_lexer *lexer, size_t *out_count) {
  *out_count = lexer->reported_count;
  return lexer->reported;
}

bool qa_ui_script_lexer_is_source_failure(
    const qa_ui_script_lexer *lexer,
    const qa_ui_script_diagnostic *candidate) {
  return lexer->env->has_pending
         && candidate != NULL && candidate == lexer->env->pending_identity;
}

qa_ui_script_fail_env *qa_ui_script_lexer_fail_env(qa_ui_script_lexer *lexer) {
  return lexer->env;
}

qa_ui_source_token_memory *qa_ui_script_lexer_output(
    qa_ui_script_lexer *lexer) {
  return lexer->output;
}

void qa_ui_script_lexer_current_location(const qa_ui_script_lexer *lexer,
                                         const char **out_path,
                                         int64_t *out_line,
                                         int64_t *out_column) {
  /* The storage path lives in its allocation; the text path is ours.
   * Both are stable while the lexer lives: storage filename bytes
   * are NUL-terminated within the allocation. */
  if (lexer->storage != NULL) {
    size_t length = 0;
    *out_path = (const char *)qa_ui_source_script_bytes(
        lexer->storage, &length);
    *out_line = qa_ui_source_script_line(lexer->storage);
  } else {
    *out_path = lexer->diagnostic_path;
    *out_line = lexer->diagnostic_line;
  }
  *out_column = lexer->column;
}

bool qa_ui_script_lexer_end_of_script(qa_ui_script_lexer *lexer) {
  return get_offset(lexer) >= (int64_t)source_length(lexer);
}

int64_t qa_ui_script_lexer_get_flags(qa_ui_script_lexer *lexer) {
  return get_flags(lexer);
}
void qa_ui_script_lexer_set_flags(qa_ui_script_lexer *lexer, int64_t flags) {
  if (lexer->storage != NULL)
    qa_ui_source_script_set_flags(lexer->storage, (int32_t)flags);
  else lexer->diagnostic_flags = (int32_t)flags;
}

const char *qa_ui_script_lexer_punctuation_from_num(
    qa_ui_script_lexer *lexer, int64_t number) {
  for (size_t index = 0; index < lexer->spec_count; index++)
    if (lexer->specs[index].punctuation == number)
      return lexer->specs[index].text;
  return "unkown punctuation";
}

void qa_ui_script_lexer_set_punctuations(
    qa_ui_script_lexer *lexer, const qa_ui_script_punctuation_spec *specs,
    size_t count) {
  const qa_ui_script_punctuation_spec *values = specs;
  size_t value_count = count;
  if (values == NULL) {
    values = DEFAULT_SPECS;
    value_count = DEFAULT_SPEC_COUNT;
  }
  owned_spec *copied = NULL;
  if (!copy_specs(values, value_count, &copied))
    qa_range_error("script punctuation allocation failed");
  uint32_t heads[256];
  build_table(copied, value_count, heads);
  if (lexer->storage != NULL)
    qa_ui_source_script_set_default_punctuations(lexer->storage, heads, 256);
  free_specs(lexer->specs, lexer->spec_count);
  lexer->specs = copied;
  lexer->spec_count = value_count;
  memcpy(lexer->heads, heads, sizeof heads);
}

int64_t qa_ui_script_lexer_num_lines_crossed(qa_ui_script_lexer *lexer) {
  return lexer->storage != NULL
             ? qa_ui_source_script_lines_crossed(lexer->storage)
             : get_line(lexer) - lexer->diagnostic_last_line;
}

int64_t qa_ui_script_lexer_next_whitespace_char(qa_ui_script_lexer *lexer) {
  if (lexer->storage != NULL)
    return qa_ui_source_script_next_whitespace_char(lexer->storage);
  if (lexer->diagnostic_whitespace_start
      == lexer->diagnostic_whitespace_end)
    return 0;
  int64_t index = lexer->diagnostic_whitespace_start++;
  if (index < 0 || index >= (int64_t)lexer->text_length)
    qa_range_error("script whitespace pointer exceeds its source allocation");
  int byte = lexer->text[index];
  return byte < 128 ? byte : byte - 256;
}

void qa_ui_script_lexer_unread_last(qa_ui_script_lexer *lexer) {
  if (lexer->storage != NULL)
    qa_ui_source_script_set_token_available(lexer->storage, true);
  else lexer->diagnostic_token_available = true;
}

void qa_ui_script_lexer_unread(qa_ui_script_lexer *lexer,
                               qa_ui_script_token *token) {
  qa_ui_source_token_write_token(retained_token(lexer), token);
  qa_ui_source_token_context context;
  memset(&context, 0, sizeof context);
  if (!qa_ui_source_token_context_set(
          &context, token->location.path, token->location.column,
          token->leading_whitespace, token->leading_whitespace_length))
    qa_range_error("script token allocation failed");
  qa_ui_source_token_context_destroy(&lexer->token_context);
  lexer->token_context = context;
  qa_ui_script_lexer_unread_last(lexer);
}

static void output_string(qa_ui_script_lexer *lexer,
                          char out[QA_UI_SOURCE_TOKEN_STRING_BYTES]) {
  qa_ui_source_token_string(lexer->output, out);
}

bool qa_ui_script_lexer_expect_token_string(qa_ui_script_lexer *lexer,
                                            const char *text) {
  char message[1200];
  if (!read_expected_into(lexer, lexer->output)) {
    snprintf(message, sizeof message, "couldn't find expected %s", text);
    lexer_error(lexer, message);
    return false;
  }
  char found[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  output_string(lexer, found);
  if (strcmp(found, text) != 0) {
    snprintf(message, sizeof message, "expected %s, found %s", text, found);
    lexer_error(lexer, message);
    return false;
  }
  return true;
}

bool qa_ui_script_lexer_expect_any_token(
    qa_ui_script_lexer *lexer, qa_ui_source_token_memory *output) {
  if (read_expected_into(lexer, output)) return true;
  lexer_error(lexer, "couldn't read expected token");
  return false;
}

bool qa_ui_script_lexer_expect_token_type(
    qa_ui_script_lexer *lexer, int64_t type, int64_t subtype,
    qa_ui_source_token_memory *output) {
  if (!qa_ui_script_lexer_expect_any_token(lexer, output)) return false;
  char message[1200];
  char found[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  qa_ui_source_token_string(output, found);
  if (qa_ui_source_token_type(output) != type) {
    const char *name = qa_ui_script_token_type_name(type);
    if (name[0] == '\0')
      qa_range_error("PS_ExpectTokenType diagnostic reads an uninitialized source string");
    snprintf(message, sizeof message, "expected a %s, found %s", name,
             found);
    lexer_error(lexer, message);
    return false;
  }
  if (type == QA_UI_SCRIPT_TOKEN_NUMBER
      && (qa_ui_source_token_subtype(output) & subtype) != subtype) {
    char *name = qa_ui_script_number_subtype_name(subtype);
    if (name == NULL) qa_range_error("script token allocation failed");
    snprintf(message, sizeof message, "expected %s, found %s", name, found);
    free(name);
    lexer_error(lexer, message);
    return false;
  }
  if (type == QA_UI_SCRIPT_TOKEN_PUNCTUATION) {
    if (subtype < 0) {
      lexer_error(lexer, "BUG: wrong punctuation subtype");
      return false;
    }
    if (qa_ui_source_token_subtype(output) != subtype)
      qa_range_error("PS_ExpectTokenType passes a punctuation struct to source %s");
  }
  return true;
}

static void rewind_checked_token(qa_ui_script_lexer *lexer) {
  set_offset(lexer, lexer->storage != NULL
                        ? qa_ui_source_script_last_offset(lexer->storage)
                        : lexer->diagnostic_last_offset);
}

bool qa_ui_script_lexer_check_token_string(qa_ui_script_lexer *lexer,
                                           const char *text) {
  if (!read_expected_into(lexer, lexer->output)) return false;
  char found[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  output_string(lexer, found);
  if (strcmp(found, text) == 0) return true;
  rewind_checked_token(lexer);
  return false;
}

bool qa_ui_script_lexer_check_token_type(
    qa_ui_script_lexer *lexer, int64_t type, int64_t subtype,
    qa_ui_source_token_memory *output) {
  if (!read_expected_into(lexer, lexer->output)) return false;
  if (qa_ui_source_token_type(lexer->output) == type
      && (qa_ui_source_token_subtype(lexer->output) & subtype) == subtype) {
    qa_ui_source_token_copy_from(output, lexer->output);
    return true;
  }
  rewind_checked_token(lexer);
  return false;
}

bool qa_ui_script_lexer_skip_until_string(qa_ui_script_lexer *lexer,
                                          const char *text) {
  char found[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  while (read_expected_into(lexer, lexer->output)) {
    output_string(lexer, found);
    if (strcmp(found, text) == 0) return true;
  }
  return false;
}

bool qa_ui_script_lexer_script_skip_to(qa_ui_script_lexer *lexer,
                                       const char *text) {
  for (;;) {
    read_whitespace(lexer);
    if (at_end(lexer)) return false;
    if (source_starts_with(lexer, text)) return true;
    advance(lexer, 1);
  }
}

double qa_ui_script_lexer_read_signed_float(qa_ui_script_lexer *lexer) {
  qa_ui_script_lexer_expect_any_token(lexer, lexer->output);
  int sign = 1;
  char found[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  output_string(lexer, found);
  if (strcmp(found, "-") == 0) {
    sign = -1;
    qa_ui_script_lexer_expect_token_type(lexer, QA_UI_SCRIPT_TOKEN_NUMBER,
                                         0, lexer->output);
  } else if (qa_ui_source_token_type(lexer->output)
             != QA_UI_SCRIPT_TOKEN_NUMBER) {
    char message[1200];
    snprintf(message, sizeof message, "expected float value, found %s\n",
             found);
    lexer_error(lexer, message);
  }
  return sign * qa_ui_source_token_float_value(lexer->output);
}

int64_t qa_ui_script_lexer_read_signed_int(qa_ui_script_lexer *lexer) {
  qa_ui_script_lexer_expect_any_token(lexer, lexer->output);
  int32_t sign = 1;
  char found[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  output_string(lexer, found);
  if (strcmp(found, "-") == 0) {
    sign = -1;
    qa_ui_script_lexer_expect_token_type(
        lexer, QA_UI_SCRIPT_TOKEN_NUMBER, QA_UI_SCRIPT_NUMBER_INTEGER,
        lexer->output);
  } else if (qa_ui_source_token_type(lexer->output)
                 != QA_UI_SCRIPT_TOKEN_NUMBER
             || qa_ui_source_token_subtype(lexer->output)
                    == QA_UI_SCRIPT_NUMBER_FLOAT) {
    char message[1200];
    snprintf(message, sizeof message, "expected integer value, found %s\n",
             found);
    lexer_error(lexer, message);
  }
  /* TS Math.imul(sign, integerValue). */
 uint32_t product =
      (uint32_t)sign * qa_ui_source_token_integer_value(lexer->output);
  return (int32_t)product;
}

bool qa_ui_script_lexer_read_literal_into(
    qa_ui_script_lexer *lexer, qa_ui_source_token_memory *output) {
  qa_ui_script_catch catch_block;
  catch_block.prev = lexer->env->top;
  lexer->env->top = &catch_block;
  if (setjmp(catch_block.buf) != 0) {
    lexer->env->top = catch_block.prev;
    qa_ui_script_fail_env_clear_pending(lexer->env);
    return false;
  }
  /* byte()/take() work on offset/column directly. */
  qa_ui_source_token_set_type(output, QA_UI_SCRIPT_TOKEN_LITERAL);
  int64_t length = (int64_t)source_length(lexer);
  int first = get_offset(lexer) == length ? 0 : peek_at(lexer, 0);
  if (first < 0 && get_offset(lexer) != length)
    qa_range_error("PS_ReadLiteral reads outside its source allocation");
  set_offset(lexer, get_offset(lexer) + 1);
  lexer->column++;
  qa_ui_source_token_set_string_byte(output, 0, first);
  int next = get_offset(lexer) == length ? 0 : peek_at(lexer, 0);
  if (next == 0) {
    lexer_error(lexer, "end of file before trailing '");
    lexer->env->top = catch_block.prev;
    return false;
  }
  int value;
  if (next == 92) {
    value = read_escape(lexer);
  } else {
    value = next;
    set_offset(lexer, get_offset(lexer) + 1);
    lexer->column++;
  }
  qa_ui_source_token_set_string_byte(output, 1, value);
  int current = get_offset(lexer) == length ? 0 : peek_at(lexer, 0);
  if (current != 39) {
    lexer_warn(lexer, "too many characters in literal, ignored");
    while (current != 0 && current != 39 && current != 10) {
      set_offset(lexer, get_offset(lexer) + 1);
      lexer->column++;
      current = get_offset(lexer) == length ? 0 : peek_at(lexer, 0);
    }
    if (current == 39) {
      set_offset(lexer, get_offset(lexer) + 1);
      lexer->column++;
    }
  }
  int trailing = get_offset(lexer) == length ? 0 : peek_at(lexer, 0);
  set_offset(lexer, get_offset(lexer) + 1);
  lexer->column++;
  qa_ui_source_token_set_string_byte(output, 2, trailing);
  qa_ui_source_token_set_string_byte(output, 3, 0);
  qa_ui_source_token_set_subtype(output, value < 128 ? value : value - 256);
  lexer->env->top = catch_block.prev;
  return true;
}

void qa_ui_script_lexer_reset(qa_ui_script_lexer *lexer) {
  if (lexer->storage != NULL) qa_ui_source_script_reset(lexer->storage);
  else {
    set_offset(lexer, 0);
    set_line(lexer, 1);
    lexer->diagnostic_last_offset = 0;
    lexer->diagnostic_last_line = 1;
    lexer->diagnostic_whitespace_start = 0;
    lexer->diagnostic_whitespace_end = 0;
  }
  lexer->column = 1;
  lexer->diagnostic_token_available = false;
  qa_ui_source_token_clear(lexer->diagnostic_token);
  for (size_t index = 0; index < lexer->reported_count; index++)
    qa_ui_script_diagnostic_destroy(&lexer->reported[index]);
  lexer->reported_count = 0;
}

/* ---- checkpoint ---- */

static bool set_number(qa_save_value *object, const char *key, double value) {
  return qa_save_value_set(object, key, qa_save_value_number(value));
}

qa_save_value *qa_ui_script_lexer_capture_save_state(
    qa_ui_script_lexer *lexer, const qa_ui_script_memory_capture *capture) {
  qa_save_value *object = qa_save_value_object();
  qa_save_value *source = qa_save_value_object();
  qa_save_value *context = qa_save_value_object();
  qa_save_value *punctuations = qa_save_value_array();
  qa_save_value *reported = qa_save_value_array();
  qa_save_value *diag_token =
      qa_ui_source_token_capture_save_state(lexer->diagnostic_token);
  qa_save_value *output =
      qa_ui_source_token_capture_save_state(lexer->output);
  if (object == NULL || source == NULL || context == NULL
      || punctuations == NULL || reported == NULL || diag_token == NULL
      || output == NULL)
    goto oom;
  if (lexer->storage == NULL) {
    if (!qa_save_value_set(source, "kind", qa_save_value_string("text")))
      goto oom;
    /* Byte text as a checkpoint string (cuts at an embedded NUL). */
    char *text = copy_text((const char *)lexer->text, lexer->text_length);
    if (text == NULL) goto oom;
    bool ok = qa_save_value_set(source, "text", qa_save_value_string(text));
    free(text);
    if (!ok) goto oom;
  } else {
    qa_save_value *state =
        qa_ui_source_script_capture_save_state(lexer->storage, capture);
    if (state == NULL) goto oom;
    if (!qa_save_value_set(source, "kind", qa_save_value_string("storage"))
        || !qa_save_value_set(source, "state", state)) {
      qa_save_value_free(state);
      goto oom;
    }
  }
  if (!qa_save_value_set(object, "source", source)) goto oom;
  source = NULL;
  if (!qa_save_value_set(object, "diagnosticPath",
                         qa_save_value_string(lexer->diagnostic_path)))
    goto oom;
  if (!set_number(object, "maxTokenLength", (double)lexer->max_token_length))
    goto oom;
  if (!set_number(object, "diagnosticFlags",
                  (double)lexer->diagnostic_flags))
    goto oom;
  if (!set_number(object, "diagnosticOffset",
                  (double)lexer->diagnostic_offset))
    goto oom;
  if (!set_number(object, "diagnosticLine", (double)lexer->diagnostic_line))
    goto oom;
  if (!set_number(object, "diagnosticLastOffset",
                  (double)lexer->diagnostic_last_offset))
    goto oom;
  if (!set_number(object, "diagnosticLastLine",
                  (double)lexer->diagnostic_last_line))
    goto oom;
  if (!set_number(object, "diagnosticWhitespaceStart",
                  (double)lexer->diagnostic_whitespace_start))
    goto oom;
  if (!set_number(object, "diagnosticWhitespaceEnd",
                  (double)lexer->diagnostic_whitespace_end))
    goto oom;
  if (!set_number(object, "column", (double)lexer->column)) goto oom;
  if (!qa_save_value_set(
          object, "diagnosticTokenAvailable",
          qa_save_value_boolean(lexer->diagnostic_token_available)))
    goto oom;
  if (!qa_save_value_set(object, "diagnosticToken", diag_token)) goto oom;
  diag_token = NULL;
  if (!qa_save_value_set(object, "output", output)) goto oom;
  output = NULL;
  if (!qa_save_value_set(context, "path",
                         qa_save_value_string(lexer->token_context.path)))
    goto oom;
  if (!set_number(context, "column", (double)lexer->token_context.column))
    goto oom;
  {
    char *ws = copy_text(lexer->token_context.leading_whitespace,
                         lexer->token_context.leading_whitespace_length);
    if (ws == NULL) goto oom;
    bool ok = qa_save_value_set(context, "leadingWhitespace",
                                qa_save_value_string(ws));
    free(ws);
    if (!ok) goto oom;
  }
  if (!qa_save_value_set(object, "tokenContext", context)) goto oom;
  context = NULL;
  for (size_t index = 0; index < lexer->spec_count; index++) {
    qa_save_value *entry = qa_save_value_object();
    if (entry == NULL) goto oom;
    if (!qa_save_value_set(entry, "text",
                           qa_save_value_string(lexer->specs[index].text))
        || !set_number(entry, "punctuation",
                       (double)lexer->specs[index].punctuation)
        || !qa_save_value_push(punctuations, entry)) {
      qa_save_value_free(entry);
      goto oom;
    }
  }
  if (!qa_save_value_set(object, "punctuations", punctuations)) goto oom;
  punctuations = NULL;
  for (size_t index = 0; index < lexer->reported_count; index++) {
    qa_save_value *entry =
        qa_ui_script_diagnostic_capture(&lexer->reported[index]);
    if (entry == NULL || !qa_save_value_push(reported, entry)) {
      qa_save_value_free(entry);
      goto oom;
    }
  }
  if (!qa_save_value_set(object, "reported", reported)) goto oom;
  return object;
oom:
  qa_save_value_free(object);
  qa_save_value_free(source);
  qa_save_value_free(context);
  qa_save_value_free(punctuations);
  qa_save_value_free(reported);
  qa_save_value_free(diag_token);
  qa_save_value_free(output);
  return NULL;
}

static bool field_reader(const qa_save_reader *reader, const char *name,
                         qa_save_reader *out) {
  return qa_save_reader_field(reader, name, out);
}
static int64_t read_int_field(const qa_save_reader *reader,
                              const char *name) {
  qa_save_reader field;
  if (!field_reader(reader, name, &field))
    qa_range_error("script lexer restore allocation failed");
  int64_t value = (int64_t)qa_save_reader_integer(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static int64_t read_int_field_min(const qa_save_reader *reader,
                                  const char *name, double minimum) {
  qa_save_reader field;
  if (!field_reader(reader, name, &field))
    qa_range_error("script lexer restore allocation failed");
  int64_t value = (int64_t)qa_save_reader_integer_min(&field, minimum);
  qa_save_reader_destroy(&field);
  return value;
}
static char *read_string_field(const qa_save_reader *reader,
                               const char *name) {
  qa_save_reader field;
  if (!field_reader(reader, name, &field)) return NULL;
  char *value = copy_cstring(qa_save_reader_string(&field));
  qa_save_reader_destroy(&field);
  return value;
}

qa_ui_script_lexer *qa_ui_script_lexer_restore_save_state_env(
    const qa_save_value *value, const qa_ui_script_memory *memory,
    bool has_memory, const qa_ui_script_memory_restore *restore,
    qa_ui_script_report_fn report, void *report_ctx,
    qa_ui_script_fail_env *env) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "script.lexer")) return NULL;
  qa_ui_script_lexer *lexer = NULL;
  qa_save_reader saved_source, field;
  if (!field_reader(&reader, "source", &saved_source)) goto oom;
  if (!field_reader(&saved_source, "kind", &field)) {
    qa_save_reader_destroy(&saved_source);
    goto oom;
  }
  const char *kinds[2] = {"text", "storage"};
  const char *kind = qa_save_reader_choice_string(&field, kinds, 2);
  bool is_text = strcmp(kind, "text") == 0;
  qa_save_reader_destroy(&field);
  char *path = read_string_field(&reader, "diagnosticPath");
  qa_ui_script_lexer_options options;
  memset(&options, 0, sizeof options);
  options.has_max_token_length = true;
  options.max_token_length = read_int_field_min(&reader, "maxTokenLength", 4);
  options.report = report;
  options.report_ctx = report_ctx;
  if (path == NULL) {
    qa_save_reader_destroy(&saved_source);
    goto oom;
  }
  if (is_text) {
    char *text = read_string_field(&saved_source, "text");
    if (text == NULL) {
      free(path);
      qa_save_reader_destroy(&saved_source);
      goto oom;
    }
    lexer = qa_ui_script_lexer_create_text_env(
        (const uint8_t *)text, strlen(text), path, &options, env);
    free(text);
  } else {
    if (!has_memory)
      qa_save_reader_fail(&reader, "stored lexer requires memory owner");
    if (!field_reader(&saved_source, "state", &field)) {
      free(path);
      qa_save_reader_destroy(&saved_source);
      goto oom;
    }
    qa_ui_source_script_storage *storage =
        qa_ui_source_script_restore_save_state(field.value, memory, restore);
    qa_save_reader_destroy(&field);
    if (storage == NULL) {
      free(path);
      qa_save_reader_destroy(&saved_source);
      goto oom;
    }
    lexer = qa_ui_script_lexer_create_storage_env(storage, path, &options,
                                                  env);
    if (lexer == NULL) qa_ui_source_script_destroy(storage);
  }
  free(path);
  qa_save_reader_destroy(&saved_source);
  if (lexer == NULL) goto oom;
  lexer->diagnostic_flags = read_int_field(&reader, "diagnosticFlags");
  lexer->diagnostic_offset = read_int_field(&reader, "diagnosticOffset");
  lexer->diagnostic_line = read_int_field(&reader, "diagnosticLine");
  lexer->diagnostic_last_offset =
      read_int_field(&reader, "diagnosticLastOffset");
  lexer->diagnostic_last_line = read_int_field(&reader, "diagnosticLastLine");
  lexer->diagnostic_whitespace_start =
      read_int_field(&reader, "diagnosticWhitespaceStart");
  lexer->diagnostic_whitespace_end =
      read_int_field(&reader, "diagnosticWhitespaceEnd");
  lexer->column = read_int_field(&reader, "column");
  if (!field_reader(&reader, "diagnosticTokenAvailable", &field)) goto oom;
  lexer->diagnostic_token_available = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  if (!field_reader(&reader, "diagnosticToken", &field)) goto oom;
  qa_ui_source_token_restore_save_state(lexer->diagnostic_token, field.value,
                                        false);
  qa_save_reader_destroy(&field);
  if (!field_reader(&reader, "output", &field)) goto oom;
  qa_ui_source_token_restore_save_state(lexer->output, field.value, false);
  qa_save_reader_destroy(&field);
  if (!field_reader(&reader, "tokenContext", &saved_source)) goto oom;
  {
    char *context_path = read_string_field(&saved_source, "path");
    int64_t column = read_int_field(&saved_source, "column");
    char *ws = read_string_field(&saved_source, "leadingWhitespace");
    qa_save_reader_destroy(&saved_source);
    if (context_path == NULL || ws == NULL
        || !qa_ui_source_token_context_set(&lexer->token_context,
                                           context_path, column, ws,
                                           strlen(ws))) {
      free(context_path);
      free(ws);
      goto oom;
    }
    free(context_path);
    free(ws);
  }
  if (!field_reader(&reader, "punctuations", &saved_source)) goto oom;
  {
    size_t count = qa_save_reader_list_count(&saved_source);
    owned_spec *specs = calloc(count == 0 ? 1 : count, sizeof *specs);
    if (specs == NULL) {
      qa_save_reader_destroy(&saved_source);
      goto oom;
    }
    bool ok = true;
    for (size_t index = 0; index < count && ok; index++) {
      qa_save_reader entry;
      ok = qa_save_reader_item(&saved_source, index, &entry);
      if (ok) {
        specs[index].text = read_string_field(&entry, "text");
        specs[index].punctuation = read_int_field(&entry, "punctuation");
        ok = specs[index].text != NULL;
        qa_save_reader_destroy(&entry);
      }
    }
    qa_save_reader_destroy(&saved_source);
    if (!ok) {
      free_specs(specs, count);
      goto oom;
    }
    free_specs(lexer->specs, lexer->spec_count);
    lexer->specs = specs;
    lexer->spec_count = count;
    build_table(lexer->specs, count, lexer->heads);
  }
  if (!field_reader(&reader, "reported", &saved_source)) goto oom;
  {
    size_t count = qa_save_reader_list_count(&saved_source);
    bool ok = true;
    for (size_t index = 0; index < count && ok; index++) {
      qa_save_reader entry;
      ok = qa_save_reader_item(&saved_source, index, &entry);
      if (ok) {
        qa_ui_script_diagnostic diagnostic;
        ok = qa_ui_script_read_diagnostic(&entry, &diagnostic)
             && push_diagnostic(lexer, &diagnostic);
        if (!ok) qa_ui_script_diagnostic_destroy(&diagnostic);
        qa_save_reader_destroy(&entry);
      }
    }
    qa_save_reader_destroy(&saved_source);
    if (!ok) goto oom;
  }
  qa_save_reader_destroy(&reader);
  return lexer;
oom:
  qa_save_reader_destroy(&reader);
  qa_ui_script_lexer_destroy(lexer);
  return NULL;
}

qa_ui_script_lexer *qa_ui_script_lexer_restore_save_state(
    const qa_save_value *value, const qa_ui_script_memory *memory,
    bool has_memory, const qa_ui_script_memory_restore *restore,
    qa_ui_script_report_fn report, void *report_ctx) {
  return qa_ui_script_lexer_restore_save_state_env(
      value, memory, has_memory, restore, report, report_ctx, NULL);
}
