/* Port of src/core/common-parse.ts. GPL-2.0-or-later. */
#include "qa/core/common_parse.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

#define MAX_TOKEN_CHARS 1024

static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    if (length > 0) memcpy(out, text, length);
    out[length] = '\0';
  }
  return out;
}

bool qa_common_parse_cursor_init(qa_common_parse_cursor *cursor, const char *source,
                                 size_t length, bool uninitialized_end) {
  cursor->source = dup_range(source, length);
  if (cursor->source == NULL) return false;
  cursor->length = length;
  cursor->terminator = length;
  for (size_t i = 0; i < length; i++)
    if (source[i] == '\0') { cursor->terminator = i; break; }
  cursor->uninitialized_end = uninitialized_end;
  cursor->has_offset = true;
  cursor->offset = 0;
  return true;
}
void qa_common_parse_cursor_destroy(qa_common_parse_cursor *cursor) {
  free(cursor->source);
  cursor->source = NULL;
}
void qa_common_parse_cursor_set_null(qa_common_parse_cursor *cursor) {
  cursor->has_offset = false;
}
void qa_common_parse_cursor_set_offset(qa_common_parse_cursor *cursor, size_t offset) {
  if (offset > cursor->terminator)
    qa_range_error("COM_Parse cursor is outside its C byte string");
  cursor->has_offset = true;
  cursor->offset = offset;
}
static int signed_byte(const qa_common_parse_cursor *cursor, size_t offset) {
  if (offset >= cursor->length) {
    if (cursor->uninitialized_end)
      qa_range_error("COM_Parse reached an uninitialized short-read tail");
    return 0;
  }
  unsigned byte = (unsigned char)cursor->source[offset];
  return byte >= 128 ? (int)byte - 256 : (int)byte;
}
static char source_char(const qa_common_parse_cursor *cursor, size_t offset) {
  return offset < cursor->length ? cursor->source[offset] : '\0';
}

typedef struct text_buffer { char *data; size_t length; size_t capacity; } text_buffer;
static bool text_add_char(text_buffer *b, char c) {
  if (b->length + 2 > b->capacity) {
    size_t cap = b->capacity == 0 ? 256 : b->capacity * 2;
    char *data = (char *)realloc(b->data, cap);
    if (data == NULL) return false;
    b->data = data;
    b->capacity = cap;
  }
  b->data[b->length++] = c;
  b->data[b->length] = '\0';
  return true;
}

char *qa_compress_common_text(const char *source, size_t length,
                              bool uninitialized_end, size_t *out_length) {
  qa_common_parse_cursor cursor;
  if (!qa_common_parse_cursor_init(&cursor, source, length, uninitialized_end))
    return NULL;
  text_buffer out = {NULL, 0, 0};
  size_t offset = 0;
  bool newline = false;
  bool whitespace = false;
  int byte;
  bool oom = false;
  while ((byte = signed_byte(&cursor, offset)) != 0 && !oom) {
    if (byte == 47 && signed_byte(&cursor, offset + 1) == 47) {
      while ((byte = signed_byte(&cursor, offset)) != 0 && byte != 10) offset++;
    } else if (byte == 47 && signed_byte(&cursor, offset + 1) == 42) {
      while (signed_byte(&cursor, offset) != 0 &&
             (signed_byte(&cursor, offset) != 42 || signed_byte(&cursor, offset + 1) != 47))
        offset++;
      if (signed_byte(&cursor, offset) != 0) offset += 2;
    } else if (byte == 10 || byte == 13) {
      newline = true;
      offset++;
    } else if (byte == 32 || byte == 9) {
      whitespace = true;
      offset++;
    } else {
      if (newline) {
        oom = !text_add_char(&out, '\n');
        newline = false;
        whitespace = false;
      }
      if (whitespace && !oom) {
        oom = !text_add_char(&out, ' ');
        whitespace = false;
      }
      if (!oom) oom = !text_add_char(&out, source_char(&cursor, offset));
      offset++;
      if (byte == 34 && !oom) {
        while ((byte = signed_byte(&cursor, offset)) != 0 && byte != 34) {
          if (!text_add_char(&out, source_char(&cursor, offset))) { oom = true; break; }
          offset++;
        }
        if (byte == 34 && !oom) {
          oom = !text_add_char(&out, source_char(&cursor, offset));
          offset++;
        }
      }
    }
  }
  qa_common_parse_cursor_destroy(&cursor);
  if (oom) { free(out.data); return NULL; }
  if (out.data == NULL) out.data = dup_range("", 0);
  if (out_length != NULL) *out_length = out.length;
  return out.data;
}

/* --- CommonParseState --- */
void qa_common_parse_state_init(qa_common_parse_state *state) {
  memset(state, 0, sizeof *state);
}
qa_save_value *qa_common_parse_state_capture(const qa_common_parse_state *state) {
  qa_save_value *object = qa_save_value_object();
  qa_save_value *token = qa_save_value_string(state->token);
  qa_save_value *line = qa_save_value_number((double)state->line);
  qa_save_value *name = qa_save_value_string(state->name);
  if (object == NULL || token == NULL || line == NULL || name == NULL ||
      !qa_save_value_set(object, "token", token) ||
      !qa_save_value_set(object, "line", line) ||
      !qa_save_value_set(object, "name", name)) {
    qa_save_value_free(object);
    if (token != NULL && (object == NULL || qa_save_value_get(object, "token") == NULL))
      qa_save_value_free(token);
    if (line != NULL && (object == NULL || qa_save_value_get(object, "line") == NULL))
      qa_save_value_free(line);
    if (name != NULL && (object == NULL || qa_save_value_get(object, "name") == NULL))
      qa_save_value_free(name);
    return NULL;
  }
  return object;
}
void qa_common_parse_state_restore(qa_common_parse_state *state, const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "common.parser"))
    qa_range_error("out of memory");
  qa_save_reader token_reader, line_reader, name_reader;
  if (!qa_save_reader_field(&reader, "token", &token_reader) ||
      !qa_save_reader_field(&reader, "line", &line_reader) ||
      !qa_save_reader_field(&reader, "name", &name_reader))
    qa_range_error("out of memory");
  const char *token = qa_save_reader_string(&token_reader);
  double line = qa_save_reader_integer_min(&line_reader, 0);
  const char *name = qa_save_reader_string(&name_reader);
  if (strlen(token) >= MAX_TOKEN_CHARS || strlen(name) >= MAX_TOKEN_CHARS)
    qa_save_reader_fail(&reader, "parser string exceeds source storage");
  strcpy(state->token, token);
  state->token_length = strlen(token);
  state->line = (int32_t)line;
  strcpy(state->name, name);
  qa_save_reader_destroy(&token_reader);
  qa_save_reader_destroy(&line_reader);
  qa_save_reader_destroy(&name_reader);
  qa_save_reader_destroy(&reader);
}
static size_t nul_cut(const char *text, size_t length) {
  for (size_t i = 0; i < length; i++)
    if (text[i] == '\0') return i;
  return length;
}
void qa_common_parse_state_begin_session(qa_common_parse_state *state, const char *name,
                                         size_t length, qa_common_print_fn print, void *ctx) {
  state->line = 0;
  size_t text_length = nul_cut(name, length);
  if (text_length >= 32000)
    qa_common_error(QA_COMMON_ERROR_FATAL, "Com_sprintf: overflowed bigbuffer");
  if (text_length >= MAX_TOKEN_CHARS && print != NULL) {
    char message[128];
    (void)snprintf(message, sizeof message, "Com_sprintf: overflow of %zu in %d\n",
                   text_length, MAX_TOKEN_CHARS);
    print(ctx, message);
  }
  size_t kept = text_length < MAX_TOKEN_CHARS - 1 ? text_length : MAX_TOKEN_CHARS - 1;
  memcpy(state->name, name, kept);
  state->name[kept] = '\0';
}
static void diagnostic(const qa_common_parse_state *state, const char *kind,
                       const char *message, size_t length,
                       qa_common_print_fn print, void *ctx) {
  size_t cut = nul_cut(message, length);
  size_t total = strlen(kind) + strlen(state->name) + cut + 64;
  char *text = (char *)malloc(total);
  if (text == NULL || print == NULL) { free(text); return; }
  (void)snprintf(text, total, "%s: %s, line %d: %.*s\n", kind, state->name,
                 state->line, (int)cut, message);
  print(ctx, text);
  free(text);
}
void qa_common_parse_state_parse_error(const qa_common_parse_state *state,
                                       const char *message, size_t length,
                                       qa_common_print_fn print, void *ctx) {
  diagnostic(state, "ERROR", message, length, print, ctx);
}
void qa_common_parse_state_parse_warning(const qa_common_parse_state *state,
                                         const char *message, size_t length,
                                         qa_common_print_fn print, void *ctx) {
  diagnostic(state, "WARNING", message, length, print, ctx);
}
static void token_append(qa_common_parse_state *state, char c) {
  state->token[state->token_length++] = c;
  state->token[state->token_length] = '\0';
}
const char *qa_common_parse_state_parse(qa_common_parse_state *state,
                                        qa_common_parse_cursor *cursor,
                                        bool allow_line_breaks) {
  state->token[0] = '\0';
  state->token_length = 0;
  if (!cursor->has_offset) return state->token;
  size_t data = cursor->offset;
  bool has_new_lines = false;
  int c;
  for (;;) {
    while ((c = signed_byte(cursor, data)) <= 32) {
      if (c == 0) {
        qa_common_parse_cursor_set_null(cursor);
        return state->token;
      }
      if (c == 10) {
        state->line = (int32_t)((uint32_t)state->line + 1u);
        has_new_lines = true;
      }
      data++;
    }
    if (has_new_lines && !allow_line_breaks) {
      qa_common_parse_cursor_set_offset(cursor, data);
      return state->token;
    }
    if (c == 47 && signed_byte(cursor, data + 1) == 47) {
      data += 2;
      while ((c = signed_byte(cursor, data)) != 0 && c != 10) data++;
    } else if (c == 47 && signed_byte(cursor, data + 1) == 42) {
      data += 2;
      while (signed_byte(cursor, data) != 0 &&
             (signed_byte(cursor, data) != 42 || signed_byte(cursor, data + 1) != 47))
        data++;
      if (signed_byte(cursor, data) != 0) data += 2;
    } else break;
  }
  if (c == 34) {
    data++;
    for (;;) {
      c = signed_byte(cursor, data);
      data++;
      if (c == 34 || c == 0) {
        if (state->token_length == MAX_TOKEN_CHARS)
          qa_range_error("COM_Parse quoted token terminator exceeds 1024-byte storage");
        if (c == 0) qa_common_parse_cursor_set_null(cursor);
        else qa_common_parse_cursor_set_offset(cursor, data);
        return state->token;
      }
      if (state->token_length < MAX_TOKEN_CHARS)
        token_append(state, source_char(cursor, data - 1));
    }
  }
  do {
    if (state->token_length < MAX_TOKEN_CHARS)
      token_append(state, source_char(cursor, data));
    data++;
    c = signed_byte(cursor, data);
    if (c == 10) state->line = (int32_t)((uint32_t)state->line + 1u);
  } while (c > 32);
  if (state->token_length == MAX_TOKEN_CHARS) {
    state->token[0] = '\0';
    state->token_length = 0;
  }
  qa_common_parse_cursor_set_offset(cursor, data);
  return state->token;
}
void qa_common_parse_state_match_token(qa_common_parse_state *state,
                                       qa_common_parse_cursor *cursor,
                                       const char *match, size_t length) {
  const char *token = qa_common_parse_state_parse(state, cursor, true);
  size_t expected_length = nul_cut(match, length);
  if (strlen(token) != expected_length || memcmp(token, match, expected_length) != 0) {
    size_t total = strlen(token) + expected_length + 32;
    char *message = (char *)malloc(total);
    if (message != NULL)
      (void)snprintf(message, total, "MatchToken: %s != %.*s", token,
                     (int)expected_length, match);
    qa_common_error(QA_COMMON_ERROR_DROP, message != NULL ? message : "MatchToken");
  }
}
void qa_common_parse_state_skip_braced_section(qa_common_parse_state *state,
                                               qa_common_parse_cursor *cursor) {
  int depth = 0;
  do {
    const char *token = qa_common_parse_state_parse(state, cursor, true);
    if (strcmp(token, "{") == 0) depth++;
    else if (strcmp(token, "}") == 0) depth--;
  } while (depth != 0 && cursor->has_offset);
}
void qa_common_parse_state_parse_1d_matrix(qa_common_parse_state *state,
                                           qa_common_parse_cursor *cursor, double x,
                                           float *matrix, size_t matrix_length,
                                           double offset) {
  qa_common_parse_state_match_token(state, cursor, "(", 1);
  for (double index = 0; index < x; index++) {
    const char *token = qa_common_parse_state_parse(state, cursor, true);
    double target = offset + index;
    if (!(target == (double)(int64_t)target) || target < 0 ||
        target >= (double)matrix_length)
      qa_range_error("Parse1DMatrix write exceeds its destination");
    matrix[(size_t)target] = (float)qa_native_atof(token);
  }
  qa_common_parse_state_match_token(state, cursor, ")", 1);
}
void qa_common_parse_state_parse_2d_matrix(qa_common_parse_state *state,
                                           qa_common_parse_cursor *cursor, double y,
                                           double x, float *matrix, size_t matrix_length,
                                           double offset) {
  qa_common_parse_state_match_token(state, cursor, "(", 1);
  for (double index = 0; index < y; index++)
    qa_common_parse_state_parse_1d_matrix(state, cursor, x, matrix, matrix_length,
                                          offset + index * x);
  qa_common_parse_state_match_token(state, cursor, ")", 1);
}
void qa_common_parse_state_parse_3d_matrix(qa_common_parse_state *state,
                                           qa_common_parse_cursor *cursor, double z,
                                           double y, double x, float *matrix,
                                           size_t matrix_length, double offset) {
  qa_common_parse_state_match_token(state, cursor, "(", 1);
  for (double index = 0; index < z; index++)
    qa_common_parse_state_parse_2d_matrix(state, cursor, y, x, matrix, matrix_length,
                                          offset + index * x * y);
  qa_common_parse_state_match_token(state, cursor, ")", 1);
}
void qa_common_parse_state_overwrite_token(qa_common_parse_state *state,
                                           const char *text, size_t length) {
  size_t value_length = nul_cut(text, length);
  if (value_length >= MAX_TOKEN_CHARS)
    qa_range_error("COM_Parse token write exceeds its source buffer");
  memcpy(state->token, text, value_length);
  state->token[value_length] = '\0';
  state->token_length = value_length;
}
void qa_common_parse_state_skip_rest_of_line(qa_common_parse_state *state,
                                             qa_common_parse_cursor *cursor) {
  if (!cursor->has_offset)
    qa_range_error("SkipRestOfLine requires a live source cursor");
  size_t data = cursor->offset;
  for (;;) {
    int byte = signed_byte(cursor, data);
    data++;
    if (byte == 0) {
      qa_common_parse_cursor_set_null(cursor);
      return;
    }
    if (byte == 10) {
      state->line = (int32_t)((uint32_t)state->line + 1u);
      qa_common_parse_cursor_set_offset(cursor, data);
      return;
    }
  }
}

/* --- Tokenizer --- */
noreturn void qa_text_parse_error(const char *source_name, int64_t line, int64_t column,
                                  const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "TextParseError: %s:%lld:%lld: ", source_name,
          (long long)line, (long long)column);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}
static bool is_whitespace(unsigned char c) { return c <= 32; }
bool qa_tokenizer_init(qa_tokenizer *tokenizer, const char *source, size_t length,
                       const char *name) {
  tokenizer->source = dup_range(source, length);
  tokenizer->name = dup_range(name != NULL ? name : "<text>",
                              strlen(name != NULL ? name : "<text>"));
  if (tokenizer->source == NULL || tokenizer->name == NULL) {
    free(tokenizer->source);
    free(tokenizer->name);
    return false;
  }
  tokenizer->length = length;
  tokenizer->offset = 0;
  tokenizer->line = 1;
  tokenizer->column = 1;
  return true;
}
void qa_tokenizer_destroy(qa_tokenizer *tokenizer) {
  free(tokenizer->source);
  free(tokenizer->name);
  tokenizer->source = NULL;
  tokenizer->name = NULL;
}
static bool tokenizer_starts(const qa_tokenizer *t, const char *prefix) {
  size_t length = strlen(prefix);
  return t->offset + length <= t->length &&
         memcmp(t->source + t->offset, prefix, length) == 0;
}
static void tokenizer_advance(qa_tokenizer *t) {
  if (t->offset >= t->length) return;
  char character = t->source[t->offset];
  if (character == '\r') {
    t->offset++;
    if (t->offset < t->length && t->source[t->offset] == '\n') t->offset++;
    t->line++;
    t->column = 1;
    return;
  }
  t->offset++;
  if (character == '\n') {
    t->line++;
    t->column = 1;
  } else t->column++;
}
bool qa_tokenizer_next(qa_tokenizer *t, bool allow_line_breaks, qa_text_token *out) {
  memset(out, 0, sizeof *out);
  bool crossed_line = false;
  for (;;) {
    while (t->offset < t->length) {
      unsigned char character = (unsigned char)t->source[t->offset];
      if (!is_whitespace(character)) break;
      if (character == '\n' || character == '\r') crossed_line = true;
      tokenizer_advance(t);
    }
    if (tokenizer_starts(t, "//")) {
      tokenizer_advance(t);
      tokenizer_advance(t);
      while (t->offset < t->length) {
        char character = t->source[t->offset];
        if (character == '\n' || character == '\r') break;
        tokenizer_advance(t);
      }
      continue;
    }
    if (tokenizer_starts(t, "/*")) {
      tokenizer_advance(t);
      tokenizer_advance(t);
      while (t->offset < t->length && !tokenizer_starts(t, "*/")) {
        char character = t->source[t->offset];
        if (character == '\n' || character == '\r') crossed_line = true;
        tokenizer_advance(t);
      }
      if (tokenizer_starts(t, "*/")) {
        tokenizer_advance(t);
        tokenizer_advance(t);
      }
      continue;
    }
    break;
  }
  if (!allow_line_breaks && crossed_line) return false;
  if (t->offset >= t->length) return false;
  int64_t line = t->line;
  int64_t column = t->column;
  bool quoted = t->source[t->offset] == '"';
  text_buffer value = {NULL, 0, 0};
  bool oom = false;
  if (quoted) {
    tokenizer_advance(t);
    while (t->offset < t->length && !oom) {
      char character = t->source[t->offset];
      if (character == '"') break;
      oom = !text_add_char(&value, character);
      tokenizer_advance(t);
    }
    if (t->offset < t->length && t->source[t->offset] == '"') tokenizer_advance(t);
  } else {
    while (t->offset < t->length && !oom) {
      unsigned char character = (unsigned char)t->source[t->offset];
      if (is_whitespace(character)) break;
      oom = !text_add_char(&value, (char)character);
      tokenizer_advance(t);
    }
  }
  if (oom) { free(value.data); return false; }
  if (value.length >= QA_TOKEN_MAX) {
    free(value.data);
    qa_text_parse_error(t->name, line, column,
                        "token is limited to %d characters", QA_TOKEN_MAX - 1);
  }
  out->value = value.data != NULL ? value.data : dup_range("", 0);
  if (out->value == NULL) return false;
  out->line = line;
  out->column = column;
  out->quoted = quoted;
  return true;
}

void qa_entities_free(qa_entities *entities) {
  for (size_t i = 0; i < entities->record_count; i++) {
    for (size_t j = 0; j < entities->records[i].pair_count; j++) {
      free(entities->records[i].pairs[j].key);
      free(entities->records[i].pairs[j].value);
    }
    free(entities->records[i].pairs);
  }
  free(entities->records);
  memset(entities, 0, sizeof *entities);
}
static bool entity_set(qa_entity_record *record, char *key, char *value) {
  for (size_t i = 0; i < record->pair_count; i++) {
    if (strcmp(record->pairs[i].key, key) == 0) {
      free(record->pairs[i].value);
      record->pairs[i].value = value;
      free(key);
      return true;
    }
  }
  qa_entity_pair *pairs = (qa_entity_pair *)realloc(
      record->pairs, (record->pair_count + 1) * sizeof *pairs);
  if (pairs == NULL) return false;
  record->pairs = pairs;
  record->pairs[record->pair_count].key = key;
  record->pairs[record->pair_count].value = value;
  record->pair_count++;
  return true;
}
bool qa_parse_entities(const char *text, size_t length, const char *name, qa_entities *out) {
  memset(out, 0, sizeof *out);
  qa_tokenizer tokenizer;
  if (!qa_tokenizer_init(&tokenizer, text, length, name != NULL ? name : "<entities>"))
    return false;
  for (;;) {
    qa_text_token opening;
    if (!qa_tokenizer_next(&tokenizer, true, &opening)) {
      if (opening.value == NULL && tokenizer.offset < tokenizer.length) {
        /* OOM inside a token is indistinguishable here; TS would have
         * produced a token. Treat as failure. */
        qa_tokenizer_destroy(&tokenizer);
        qa_entities_free(out);
        return false;
      }
      qa_tokenizer_destroy(&tokenizer);
      return true;
    }
    if (strcmp(opening.value, "{") != 0) {
      int64_t line = opening.line;
      int64_t column = opening.column;
      free(opening.value);
      qa_text_parse_error(tokenizer.name, line, column, "expected \"{\"");
    }
    free(opening.value);
    qa_entity_record *records = (qa_entity_record *)realloc(
        out->records, (out->record_count + 1) * sizeof *records);
    if (records == NULL) {
      qa_tokenizer_destroy(&tokenizer);
      qa_entities_free(out);
      return false;
    }
    out->records = records;
    qa_entity_record *entity = &out->records[out->record_count];
    memset(entity, 0, sizeof *entity);
    out->record_count++;
    for (;;) {
      qa_text_token key;
      if (!qa_tokenizer_next(&tokenizer, true, &key))
        qa_text_parse_error(tokenizer.name, tokenizer.line, tokenizer.column,
                            "expected key or \"}\"");
      if (strcmp(key.value, "}") == 0) {
        free(key.value);
        break;
      }
      qa_text_token value;
      if (!qa_tokenizer_next(&tokenizer, false, &value)) {
        qa_text_parse_error(tokenizer.name, tokenizer.line, tokenizer.column,
                            "missing value for entity key \"%s\"", key.value);
      }
      if (strcmp(value.value, "}") == 0) {
        int64_t line = value.line;
        int64_t column = value.column;
        qa_text_parse_error(tokenizer.name, line, column,
                            "missing value for entity key \"%s\"", key.value);
      }
      if (!entity_set(entity, key.value, value.value)) {
        free(key.value);
        free(value.value);
        qa_tokenizer_destroy(&tokenizer);
        qa_entities_free(out);
        return false;
      }
    }
  }
}

/* --- Q1/Q2 donor parsers --- */
static int byte_at(const qa_legacy_parse_state *state, size_t i) {
  return i < state->length ? (unsigned char)state->data[i] : 0;
}
static int signed_byte_at(const qa_legacy_parse_state *state, size_t i) {
  int b = byte_at(state, i);
  return b >= 128 ? b - 256 : b;
}
static bool is_single_char_token(int c) {
  return c == 123 || c == 125 || c == 41 || c == 40 || c == 39 || c == 58;
}
char *qa_parse_q1_token(qa_legacy_parse_state *state, bool quakeworld) {
  size_t i = state->index;
  for (;;) {
    int c = signed_byte_at(state, i);
    while (c <= 32) {
      if (c == 0) {
        state->index = i;
        return NULL;
      }
      i++;
      c = signed_byte_at(state, i);
    }
    if (byte_at(state, i) == 47 && byte_at(state, i + 1) == 47) {
      while (byte_at(state, i) != 0 && byte_at(state, i) != 10) i++;
      continue;
    }
    break;
  }
  int c0 = byte_at(state, i);
  text_buffer token = {NULL, 0, 0};
  if (c0 == 34) {
    i++;
    for (;;) {
      int c = byte_at(state, i);
      i++;
      if (c == 34 || c == 0) {
        state->index = i;
        if (token.data == NULL) return dup_range("", 0);
        return token.data;
      }
      if (!text_add_char(&token, (char)c)) { free(token.data); return NULL; }
    }
  }
  if (!quakeworld && is_single_char_token(c0)) {
    state->index = i + 1;
    char single[2] = {(char)c0, '\0'};
    return dup_range(single, 1);
  }
  int c = c0;
  do {
    if (!text_add_char(&token, (char)c)) { free(token.data); return NULL; }
    i++;
    c = byte_at(state, i);
    if (!quakeworld && is_single_char_token(c)) break;
  } while (signed_byte_at(state, i) > 32);
  state->index = i;
  if (token.data == NULL) return dup_range("", 0);
  return token.data;
}
static int char_at_legacy(const qa_legacy_parse_state *state, size_t i) {
  return i < state->length ? (unsigned char)state->data[i] : 0;
}
char *qa_parse_q2_token(qa_legacy_parse_state *state, double max_token_chars) {
  size_t idx = state->index;
  double len = 0;
  text_buffer token = {NULL, 0, 0};
  for (;;) {
    int c = char_at_legacy(state, idx);
    while (c <= 32) {
      if (c == 0) {
        state->index = idx;
        free(token.data);
        return dup_range("", 0);
      }
      idx++;
      c = char_at_legacy(state, idx);
    }
    if (c == 47 && char_at_legacy(state, idx + 1) == 47) {
      while (char_at_legacy(state, idx) != 0 && char_at_legacy(state, idx) != 10) idx++;
      continue;
    }
    break;
  }
  int c = char_at_legacy(state, idx);
  if (c == 34) {
    idx++;
    for (;;) {
      c = char_at_legacy(state, idx);
      idx++;
      if (c == 34 || c == 0) {
        state->index = idx;
        if (token.data == NULL) return dup_range("", 0);
        return token.data;
      }
      if (len < max_token_chars) {
        if (!text_add_char(&token, (char)c)) { free(token.data); return NULL; }
        len++;
      }
    }
  }
  do {
    if (len < max_token_chars) {
      if (!text_add_char(&token, (char)c)) { free(token.data); return NULL; }
      len++;
    }
    idx++;
    c = char_at_legacy(state, idx);
  } while (c > 32);
  if (len == max_token_chars) {
    free(token.data);
    token.data = dup_range("", 0);
    if (token.data == NULL) return NULL;
  }
  state->index = idx;
  if (token.data == NULL) return dup_range("", 0);
  return token.data;
}
