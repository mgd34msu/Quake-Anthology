/* Port of src/text/localization.ts. GPL-2.0-or-later. */
#include "qa/text/localization.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

#define MAX_LOC_KEY 64
#define MAX_LOC_FORMAT 1024
#define MAX_STRING_CHARS 1024

const char *const qa_loc_known_languages[6] = {
    "english", "french", "german", "italian", "russian", "spanish"};

static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  memcpy(out, text, length);
  out[length] = '\0';
  return out;
}
static char *dup_text(const char *text) { return dup_range(text, strlen(text)); }

static void log_warn(const qa_lib_log *log, const char *format, ...) {
  if (log == NULL || log->warn == NULL) return;
  char stack[1200];
  va_list args;
  va_start(args, format);
  int written = vsnprintf(stack, sizeof stack, format, args);
  va_end(args);
  if (written < 0) return;
  if ((size_t)written < sizeof stack) {
    log->warn(log->ctx, stack);
    return;
  }
  char *text = (char *)malloc((size_t)written + 1);
  if (text == NULL) return;
  va_start(args, format);
  vsnprintf(text, (size_t)written + 1, format, args);
  va_end(args);
  log->warn(log->ctx, text);
  free(text);
}

/* TS strlcpy over UTF-8 bytes (see the header note). NULL is OOM. */
static char *loc_strlcpy(const char *src, int64_t size) {
  if (size <= 0) return dup_text("");
  size_t length = strlen(src);
  if ((int64_t)length < size) return dup_text(src);
  int64_t end = size - 1;
  const unsigned char *bytes = (const unsigned char *)src;
  while (end > 0 && (bytes[end] & 0xc0) == 0x80) end--;
  return dup_range(src, (size_t)end);
}
static char *loc_strnlcpy(const char *src, size_t count, int64_t size) {
  size_t length = strlen(src);
  size_t ret = count < length ? count : length;
  char *head = dup_range(src, ret);
  if (head == NULL) return NULL;
  char *out = loc_strlcpy(head, size);
  free(head);
  return out;
}
static char *join_text(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  char *out = (char *)malloc(la + lb + 1);
  if (out == NULL) return NULL;
  memcpy(out, a, la);
  memcpy(out + la, b, lb + 1);
  return out;
}
static char *loc_strlcat(const char *dst, const char *src, int64_t size) {
  char *tail = loc_strlcpy(src, size - (int64_t)strlen(dst));
  if (tail == NULL) return NULL;
  char *out = join_text(dst, tail);
  free(tail);
  return out;
}
static char *loc_strnlcat(const char *dst, const char *src, size_t count, int64_t size) {
  char *tail = loc_strnlcpy(src, count, size - (int64_t)strlen(dst));
  if (tail == NULL) return NULL;
  char *out = join_text(dst, tail);
  free(tail);
  return out;
}

typedef struct loc_parse_result {
  bool ok;
  const char *error;
  qa_loc_arg arguments[QA_LOC_MAX_ARGS];
  size_t count;
} loc_parse_result;

static loc_parse_result loc_parse(const char *format) {
  loc_parse_result result;
  memset(&result, 0, sizeof result);
  int arg_index_state = 0;
  size_t rover = 0;
  size_t length = strlen(format);
  for (;;) {
    if (rover >= length || format[rover] == '\0') break;
    if (format[rover] != '{') {
      rover++;
      continue;
    }
    size_t arg_start = rover;
    rover++;
    if (rover < length && format[rover] == '{') {
      rover++; /* "{{" is a literal brace */
      continue;
    }
    if (result.count == QA_LOC_MAX_ARGS) {
      result.error = "too many arguments";
      return result;
    }
    qa_loc_arg *arg = &result.arguments[result.count++];
    arg->start = arg_start;
    size_t digit_end = rover;
    unsigned value = 0;
    while (digit_end < length && format[digit_end] >= '0' && format[digit_end] <= '9') {
      value = value * 10u + (unsigned)(format[digit_end] - '0');
      digit_end++;
    }
    if (digit_end == rover) {
      if (arg_index_state == -1) {
        result.error = "encountered sequential argument, but has positional args";
        return result;
      }
      arg->arg_index = arg_index_state & 0xff;
      arg_index_state++;
    } else {
      if (arg_index_state > 0) {
        result.error = "encountered positional argument, but has sequential args";
        return result;
      }
      arg->arg_index = (int)(value & 0xffu);
      arg_index_state = -1;
    }
    rover = digit_end - 1;
    for (;;) {
      if (rover >= length || format[rover] == '\0') {
        result.error = "EOF before end of argument found";
        return result;
      }
      rover++;
      if (rover >= length || format[rover] != '}') continue;
      size_t arg_end = rover;
      rover++;
      if (rover < length && format[rover] == '}') continue;
      arg->end = arg_end + 1;
      break;
    }
  }
  result.ok = true;
  return result;
}

static bool loc_has_arguments(const char *base) {
  size_t length = strlen(base);
  for (size_t i = 0; i < length; i++) {
    if (base[i] != '{') continue;
    i++;
    if (i >= length) return false;
    if (base[i] != '{') return true;
  }
  return false;
}

/* TS unescapeBraces: "{{" is the literal brace. NULL is OOM. */
static char *unescape_braces_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  size_t w = 0;
  for (size_t i = 0; i < length; i++) {
    out[w++] = text[i];
    if (text[i] == '{' && i + 1 < length && text[i + 1] == '{') i++;
  }
  out[w] = '\0';
  return out;
}

#define PARSE_FLAG_ESCAPE 1

typedef struct token_state {
  const uint8_t *data;
  size_t length;
  size_t index;
} token_state;

static int token_cc(const token_state *state, size_t i) {
  return i < state->length ? state->data[i] : 0;
}
static int parse_escape_sequence(token_state *state) {
  int code = token_cc(state, state->index);
  state->index++;
  if (code == 0) return -1;
  if (code == 0x6e) return 0x0a;
  if (code == 0x74) return 0x09;
  if (code == 0x72) return 0x0d;
  return code;
}

typedef struct text_builder {
  char *data;
  size_t length;
  size_t capacity;
} text_builder;

static bool builder_push(text_builder *b, char ch) {
  if (b->length + 2 > b->capacity) {
    size_t cap = b->capacity == 0 ? 64 : b->capacity * 2;
    char *data = (char *)realloc(b->data, cap);
    if (data == NULL) return false;
    b->data = data;
    b->capacity = cap;
  }
  b->data[b->length++] = ch;
  b->data[b->length] = '\0';
  return true;
}

/* TS comParseToken, byte-wise (see header note). Malloc'd, NULL is OOM. */
static char *com_parse_token(token_state *state, int64_t size, int flags) {
  text_builder result = {NULL, 0, 0};
  int64_t len = 0;
  for (;;) {
    int c = token_cc(state, state->index);
    while (c <= 32) {
      if (c == 0) {
        free(result.data);
        return dup_text("");
      }
      state->index++;
      c = token_cc(state, state->index);
    }
    if (c == 0x2f && token_cc(state, state->index + 1) == 0x2f) {
      state->index += 2;
      while (token_cc(state, state->index) != 0 && token_cc(state, state->index) != 0x0a)
        state->index++;
      continue;
    }
    if (c == 0x2f && token_cc(state, state->index + 1) == 0x2a) {
      state->index += 2;
      while (token_cc(state, state->index) != 0) {
        if (token_cc(state, state->index) == 0x2a &&
            token_cc(state, state->index + 1) == 0x2f) {
          state->index += 2;
          break;
        }
        state->index++;
      }
      continue;
    }
    break;
  }
  int c = token_cc(state, state->index);
  if (c == 0x3d) {
    state->index++;
    free(result.data);
    return dup_text("=");
  }
  if (c == 0x22) {
    state->index++;
    for (;;) {
      c = token_cc(state, state->index);
      state->index++;
      if (c == 0x22 || c == 0) {
        char *out = loc_strlcpy(result.data == NULL ? "" : result.data, size);
        free(result.data);
        return out;
      }
      if (c == 0x5c && (flags & PARSE_FLAG_ESCAPE) != 0) {
        int esc = parse_escape_sequence(state);
        if (esc < 0) {
          char *out = loc_strlcpy(result.data == NULL ? "" : result.data, size);
          free(result.data);
          return out;
        }
        c = esc;
      }
      if (len + 1 < size && !builder_push(&result, (char)c)) {
        free(result.data);
        return NULL;
      }
      len++;
    }
  }
  do {
    bool consumed = false;
    if (c == 0x5c && (flags & PARSE_FLAG_ESCAPE) != 0) {
      state->index++;
      int esc = parse_escape_sequence(state);
      if (esc < 0) break;
      c = esc;
      consumed = true;
    }
    if (len + 1 < size && !builder_push(&result, (char)c)) {
      free(result.data);
      return NULL;
    }
    len++;
    if (!consumed) state->index++;
    c = token_cc(state, state->index);
  } while (c > 32 && c != 0x3d);
  char *out = loc_strlcpy(result.data == NULL ? "" : result.data, size);
  free(result.data);
  return out;
}

static char ascii_lower(char ch) {
  return ch >= 'A' && ch <= 'Z' ? (char)(ch + 32) : ch;
}
static char *lower_dup(const char *text) {
  char *out = dup_text(text);
  if (out == NULL) return NULL;
  for (size_t i = 0; out[i] != '\0'; i++) out[i] = ascii_lower(out[i]);
  return out;
}
static char *strip_platform_tag(const char *token) {
  const char *s = token;
  size_t length = strlen(s);
  if (s[0] == '<') {
    s++;
    length--;
  }
  if (length > 0 && s[length - 1] == '>') length--;
  char *out = dup_range(s, length);
  if (out == NULL) return NULL;
  for (size_t i = 0; out[i] != '\0'; i++) out[i] = ascii_lower(out[i]);
  return out;
}

static bool table_set(qa_localization_table *table, const char *key,
                      const char *format, const qa_loc_arg *args, size_t count) {
  char *format_copy = dup_text(format);
  if (format_copy == NULL) return false;
  for (size_t i = 0; i < table->entry_count; i++) {
    if (strcmp(table->entries[i].key, key) != 0) continue;
    free(table->entries[i].value.format);
    table->entries[i].value.format = format_copy;
    memcpy(table->entries[i].value.arguments, args, count * sizeof *args);
    table->entries[i].value.argument_count = count;
    return true;
  }
  if (table->entry_count == table->entry_capacity) {
    size_t cap = table->entry_capacity == 0 ? 16 : table->entry_capacity * 2;
    qa_loc_entry *entries =
        (qa_loc_entry *)realloc(table->entries, cap * sizeof *entries);
    if (entries == NULL) {
      free(format_copy);
      return false;
    }
    table->entries = entries;
    table->entry_capacity = cap;
  }
  char *key_copy = dup_text(key);
  if (key_copy == NULL) {
    free(format_copy);
    return false;
  }
  qa_loc_entry *entry = &table->entries[table->entry_count++];
  entry->key = key_copy;
  entry->value.format = format_copy;
  memcpy(entry->value.arguments, args, count * sizeof *args);
  entry->value.argument_count = count;
  return true;
}

/* TS Loc_ParseInto. `seen` (merge) is a malloc'd string list or NULL. */
typedef struct seen_keys {
  char **keys;
  size_t count;
  size_t capacity;
} seen_keys;

static bool seen_has(const seen_keys *seen, const char *key) {
  for (size_t i = 0; i < seen->count; i++)
    if (strcmp(seen->keys[i], key) == 0) return true;
  return false;
}
static bool seen_add(seen_keys *seen, const char *key) {
  if (seen_has(seen, key)) return true;
  if (seen->count == seen->capacity) {
    size_t cap = seen->capacity == 0 ? 16 : seen->capacity * 2;
    char **keys = (char **)realloc(seen->keys, cap * sizeof *keys);
    if (keys == NULL) return false;
    seen->keys = keys;
    seen->capacity = cap;
  }
  seen->keys[seen->count] = dup_text(key);
  if (seen->keys[seen->count] == NULL) return false;
  seen->count++;
  return true;
}
static void seen_free(seen_keys *seen) {
  for (size_t i = 0; i < seen->count; i++) free(seen->keys[i]);
  free(seen->keys);
  memset(seen, 0, sizeof *seen);
}

static int64_t loc_parse_into(qa_localization_table *table, qa_byte_view bytes,
                              const qa_loc_reload_options *opts,
                              qa_loc_duplicate_keys duplicates, seen_keys *seen) {
  const qa_lib_log *log = opts == NULL ? NULL : opts->log;
  char *platform = NULL;
  if (opts != NULL && opts->platform != NULL) platform = lower_dup(opts->platform);
  /* TS TextDecoder("utf-8") replaces invalid sequences; C keeps bytes. */
  token_state state = {bytes.bytes, bytes.length, 0};
  int64_t num_locs = 0;
  for (;;) {
    char *key = com_parse_token(&state, MAX_LOC_KEY, 0);
    if (key == NULL || key[0] == '\0') {
      free(key);
      break;
    }
    char *equals = com_parse_token(&state, MAX_STRING_CHARS, 0);
    bool has_platform = false;
    seen_keys tags = {NULL, 0, 0};
    bool stop = equals == NULL || equals[0] == '\0';
    if (!stop && equals[0] == '<') {
      has_platform = true;
      while (equals != NULL && equals[0] != '\0' &&
             equals[strlen(equals) - 1] != '>') {
        char *tag = strip_platform_tag(equals);
        if (tag != NULL) {
          if (tags.count == tags.capacity) {
            size_t cap = tags.capacity == 0 ? 4 : tags.capacity * 2;
            char **grown = (char **)realloc(tags.keys, cap * sizeof *grown);
            if (grown == NULL) {
              free(tag);
              break;
            }
            tags.keys = grown;
            tags.capacity = cap;
          }
          tags.keys[tags.count++] = tag;
        }
        free(equals);
        equals = com_parse_token(&state, MAX_STRING_CHARS, 0);
      }
      if (equals != NULL && equals[0] != '\0') {
        char *tag = strip_platform_tag(equals);
        if (tag != NULL) {
          if (tags.count == tags.capacity) {
            size_t cap = tags.capacity == 0 ? 4 : tags.capacity * 2;
            char **grown = (char **)realloc(tags.keys, cap * sizeof *grown);
            if (grown == NULL) free(tag);
            else {
              tags.keys = grown;
              tags.capacity = cap;
              tags.keys[tags.count++] = tag;
            }
          } else {
            tags.keys[tags.count++] = tag;
          }
        }
      }
      free(equals);
      equals = com_parse_token(&state, MAX_STRING_CHARS, 0);
    }
    if (!stop && equals != NULL && strcmp(equals, "=") != 0) {
      log_warn(log, "loc parse error (%s): expected \"=\", rest of file ignored", key);
    }
    if (stop || equals == NULL || strcmp(equals, "=") != 0) {
      free(key);
      free(equals);
      seen_free(&tags);
      break;
    }
    free(equals);
    char *format = com_parse_token(&state, MAX_LOC_FORMAT, PARSE_FLAG_ESCAPE);
    if (format == NULL) {
      free(key);
      seen_free(&tags);
      break;
    }
    loc_parse_result parsed = loc_parse(format);
    if (!parsed.ok) {
      log_warn(log, "loc parse error (%s): %s", key, parsed.error);
      free(key);
      free(format);
      seen_free(&tags);
      continue;
    }
    bool sink_has =
        seen != NULL ? seen_has(seen, key) : qa_localization_table_find(table, key) != NULL;
    if (has_platform) {
      if (platform != NULL && seen_has(&tags, platform)) {
        if (seen != NULL) seen_add(seen, key);
        if (table_set(table, key, format, parsed.arguments, parsed.count)) num_locs++;
      }
    } else if (duplicates == QA_LOC_DUPLICATES_LAST || !sink_has) {
      if (seen != NULL) seen_add(seen, key);
      if (table_set(table, key, format, parsed.arguments, parsed.count)) num_locs++;
    }
    free(key);
    free(format);
    seen_free(&tags);
  }
  free(platform);
  return num_locs;
}

void qa_localization_table_init(qa_localization_table *table, qa_loc_profile profile) {
  memset(table, 0, sizeof *table);
  table->profile = profile;
}
void qa_localization_table_clear(qa_localization_table *table) {
  for (size_t i = 0; i < table->entry_count; i++) {
    free(table->entries[i].key);
    free(table->entries[i].value.format);
  }
  table->entry_count = 0;
}
void qa_localization_table_destroy(qa_localization_table *table) {
  qa_localization_table_clear(table);
  free(table->entries);
  memset(table, 0, sizeof *table);
}
const char *qa_localization_table_profile_name(const qa_localization_table *table) {
  return table->profile == QA_LOC_Q2_RERELEASE ? "q2-rerelease" : "q1-rerelease";
}
const qa_loc_string *qa_localization_table_find(const qa_localization_table *table,
                                                const char *base) {
  for (size_t i = 0; i < table->entry_count; i++)
    if (strcmp(table->entries[i].key, base) == 0) return &table->entries[i].value;
  return NULL;
}
size_t qa_localization_table_size(const qa_localization_table *table) {
  return table->entry_count;
}

static qa_loc_duplicate_keys resolve_duplicates(const qa_localization_table *table,
                                                const qa_loc_reload_options *opts) {
  if (opts != NULL && opts->duplicate_keys != QA_LOC_DUPLICATES_UNSPECIFIED)
    return opts->duplicate_keys;
  return table->profile == QA_LOC_Q2_RERELEASE ? QA_LOC_DUPLICATES_LAST
                                               : QA_LOC_DUPLICATES_FIRST;
}
static void log_info_count(const qa_loc_reload_options *opts, const char *verb,
                           int64_t count) {
  if (opts == NULL || opts->log == NULL || opts->log->info == NULL) return;
  char text[96];
  snprintf(text, sizeof text, "%s %lld localization strings", verb, (long long)count);
  opts->log->info(opts->log->ctx, text);
}

int64_t qa_localization_table_reload(qa_localization_table *table, qa_byte_view bytes,
                                     const qa_loc_reload_options *options) {
  qa_localization_table_clear(table);
  if (bytes.bytes == NULL) return 0;
  int64_t num = loc_parse_into(table, bytes, options, resolve_duplicates(table, options),
                               NULL);
  log_info_count(options, "Loaded", num);
  return num;
}
int64_t qa_localization_table_merge(qa_localization_table *table, qa_byte_view bytes,
                                    const qa_loc_reload_options *options) {
  if (bytes.bytes == NULL) return 0;
  seen_keys seen = {NULL, 0, 0};
  int64_t num = loc_parse_into(table, bytes, options, resolve_duplicates(table, options),
                               &seen);
  seen_free(&seen);
  log_info_count(options, "Merged", num);
  return num;
}
int64_t qa_localization_table_load_ordered(qa_localization_table *table,
                                           qa_loc_load_tier primary,
                                           qa_loc_load_tier fallback,
                                           const qa_loc_reload_options *options) {
  qa_loc_load_tier tier = primary.has_base ? primary : fallback;
  qa_byte_view base = tier.has_base ? tier.base : (qa_byte_view){NULL, 0};
  qa_localization_table_reload(table, base, options);
  for (size_t i = 0; i < tier.mod_count; i++)
    qa_localization_table_merge(table, tier.mods[i], options);
  return (int64_t)table->entry_count;
}
int64_t qa_localization_table_load(qa_localization_table *table, qa_byte_view bytes,
                                   const qa_loc_reload_options *options) {
  return qa_localization_table_reload(table, bytes, options);
}

char *qa_localization_table_localize_source(qa_localization_table *table, const char *base,
                                            bool allow_in_place,
                                            const char *const *args, int64_t num_args,
                                            int64_t output_length,
                                            const qa_lib_log *log) {
  char *working = dup_text(base);
  if (working == NULL) return NULL;
  const qa_loc_string *str = NULL;
  qa_loc_string in_place;
  memset(&in_place, 0, sizeof in_place);
  bool own_format = false;
  char *result = NULL;
  if (!allow_in_place) {
    if (working[0] != '$') {
      result = loc_strlcpy(working, output_length);
      free(working);
      return result;
    }
    memmove(working, working + 1, strlen(working));
    str = qa_localization_table_find(table, working);
  } else if (working[0] == '$') {
    memmove(working, working + 1, strlen(working));
    str = qa_localization_table_find(table, working);
  } else if (loc_has_arguments(working)) {
    char *format = loc_strlcpy(working, MAX_LOC_FORMAT);
    if (format == NULL) {
      free(working);
      return NULL;
    }
    loc_parse_result parsed = loc_parse(format);
    if (!parsed.ok) {
      log_warn(log, "in-place localization of \"%s\" failed: %s", working, parsed.error);
      free(format);
      result = loc_strlcpy(working, output_length);
      free(working);
      return result;
    }
    in_place.format = format;
    memcpy(in_place.arguments, parsed.arguments,
           parsed.count * sizeof in_place.arguments[0]);
    in_place.argument_count = parsed.count;
    str = &in_place;
    own_format = true;
  } else {
    result = loc_strlcpy(working, output_length);
    free(working);
    return result;
  }
  if (str == NULL || str->argument_count == 0) {
    if (str == NULL) {
      result = loc_strlcpy(working, output_length);
    } else {
      char *plain = unescape_braces_range(str->format, strlen(str->format));
      result = plain == NULL ? NULL : loc_strlcpy(plain, output_length);
      free(plain);
    }
    if (own_format) free(in_place.format);
    free(working);
    return result;
  }
  for (size_t i = 0; i < str->argument_count; i++) {
    if ((int64_t)str->arguments[i].arg_index >= num_args) {
      log_warn(log, "Loc_Localize: base \"%s\" localized with too few arguments", working);
      result = loc_strlcpy(working, output_length);
      if (own_format) free(in_place.format);
      free(working);
      return result;
    }
  }
  for (int64_t i = 0; i < num_args; i++) {
    if (args == NULL || args[i] == NULL) {
      log_warn(log, "Loc_Localize: invalid argument at position %lld", (long long)i);
      result = loc_strlcpy(working, output_length);
      if (own_format) free(in_place.format);
      free(working);
      return result;
    }
  }
  /* Copy the format/args: recursion may not mutate the table, but the
   * in-place format must outlive the building loop anyway. */
  const qa_loc_arg *arg = &str->arguments[0];
  char *head = unescape_braces_range(str->format, arg->start);
  char *output = head == NULL ? NULL : loc_strnlcpy(head, arg->start, output_length);
  free(head);
  for (size_t i = 0; output != NULL && i + 1 < str->argument_count; i++) {
    const char *raw = args == NULL ? "" : args[arg->arg_index];
    char *localized = qa_localization_table_localize_source(
        table, raw == NULL ? "" : raw, false, NULL, 0, MAX_STRING_CHARS, log);
    if (localized == NULL) {
      free(output);
      output = NULL;
      break;
    }
    char *next = loc_strlcat(output, localized, output_length);
    free(localized);
    free(output);
    output = next;
    if (output == NULL) break;
    if (i + 1 >= str->argument_count)
      qa_range_error("Missing localization argument record");
    const qa_loc_arg *next_arg = &str->arguments[i + 1];
    char *between = unescape_braces_range(str->format + arg->end, next_arg->start - arg->end);
    next = between == NULL ? NULL
                           : loc_strnlcat(output, between, next_arg->start - arg->end,
                                          output_length);
    free(between);
    free(output);
    output = next;
    arg = next_arg;
  }
  if (output != NULL) {
    const char *raw = args == NULL ? "" : args[arg->arg_index];
    char *localized = qa_localization_table_localize_source(
        table, raw == NULL ? "" : raw, false, NULL, 0, MAX_STRING_CHARS, log);
    if (localized == NULL) {
      free(output);
      output = NULL;
    } else {
      char *next = loc_strlcat(output, localized, output_length);
      free(localized);
      free(output);
      output = next;
      if (output != NULL) {
        char *tail = unescape_braces_range(str->format + arg->end, strlen(str->format + arg->end));
        next = tail == NULL ? NULL : loc_strlcat(output, tail, output_length);
        free(tail);
        free(output);
        output = next;
      }
    }
  }
  if (own_format) free(in_place.format);
  free(working);
  return output;
}

static void require_output_size(int64_t output_bytes) {
  if (output_bytes < 0 || output_bytes > QA_MAX_SAFE_INTEGER)
    qa_range_error("Invalid localization output size");
}

char *qa_localization_table_localize(qa_localization_table *table, const char *base,
                                     const char *const *args, size_t arg_count,
                                     bool allow_in_place, int64_t output_bytes,
                                     const qa_lib_log *log) {
  require_output_size(output_bytes);
  return qa_localization_table_localize_source(table, base, allow_in_place, args,
                                               (int64_t)arg_count, output_bytes, log);
}

uint8_t *qa_localization_table_localize_bytes(qa_localization_table *table,
                                              const char *base,
                                              const char *const *args, size_t arg_count,
                                              bool allow_in_place, int64_t output_bytes,
                                              size_t *out_length) {
  require_output_size(output_bytes);
  char *text = qa_localization_table_localize_source(
      table, base, allow_in_place, args, (int64_t)arg_count, QA_MAX_SAFE_INTEGER, NULL);
  if (text == NULL) {
    *out_length = 0;
    return NULL;
  }
  size_t length = strlen(text);
  size_t keep = output_bytes == 0
                    ? 0
                    : (length < (size_t)(output_bytes - 1) ? length
                                                           : (size_t)(output_bytes - 1));
  uint8_t *out = (uint8_t *)malloc(keep + 1);
  if (out == NULL) {
    free(text);
    *out_length = 0;
    return NULL;
  }
  memcpy(out, text, keep);
  out[keep] = '\0';
  free(text);
  *out_length = keep;
  return out;
}

char *qa_localization_table_lookup(qa_localization_table *table, const char *key,
                                   const char *const *args, size_t arg_count) {
  const char *name = key[0] == '$' ? key + 1 : key;
  if (qa_localization_table_find(table, name) == NULL) return NULL;
  char *dollar = join_text("$", name);
  if (dollar == NULL) return NULL;
  char *out = qa_localization_table_localize(table, dollar, args, arg_count, true,
                                             MAX_STRING_CHARS, NULL);
  free(dollar);
  return out;
}

const char *qa_loc_language_from_locale(const char *tag) {
  if (tag == NULL || tag[0] == '\0') return "english";
  size_t end = 0;
  while (tag[end] != '\0' && tag[end] != '.' && tag[end] != '@') end++;
  size_t primary_end = 0;
  while (primary_end < end && tag[primary_end] != '-' && tag[primary_end] != '_')
    primary_end++;
  static const char *const codes[6] = {"en", "fr", "de", "it", "ru", "es"};
  for (size_t i = 0; i < 6; i++) {
    if (strlen(codes[i]) != primary_end) continue;
    bool match = true;
    for (size_t j = 0; j < primary_end; j++)
      if (ascii_lower(tag[j]) != codes[i][j]) match = false;
    if (match) return qa_loc_known_languages[i];
  }
  return "english";
}

void qa_localization_catalog_init(qa_localization_catalog *catalog, qa_seat_id seat,
                                  qa_loc_profile profile) {
  qa_localization_table_init(&catalog->table, profile);
  catalog->seat = seat;
}
void qa_localization_catalog_destroy(qa_localization_catalog *catalog) {
  qa_localization_table_destroy(&catalog->table);
}
