/* See header. Release32 profile: filename[64], float skill, then 81 char/padding/union records.
 * String words are managed pointer identities. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/character.h"
#include "library_internal.h"
#include "qa/core/numeric.h"
#include <math.h>
#include "qa/foundation/js.h"
#define DEFAULT_CHARACTER "bots/default_c.c"
#define MAX_CHARACTERISTICS 80
/* The source allocates c[1] plus MAX_CHARACTERISTICS extra records. Its loader
 * accepts index 80, while defaulting, interpolation and getters only visit 0..79. */
#define STORED_CHARACTERISTICS (MAX_CHARACTERISTICS + 1)
#define MAX_QPATH 64
#define CHARACTERISTIC_BYTES 8
#define CHARACTERISTICS_OFFSET (MAX_QPATH + 4)
#define PROFILE_BYTES (CHARACTERISTICS_OFFSET + STORED_CHARACTERISTICS * CHARACTERISTIC_BYTES)
#define KIND_NONE 0
#define KIND_INTEGER 1
#define KIND_FLOAT 2
#define KIND_STRING 3
const char *qa_character_severity_name(qa_character_severity severity) {
  static const char *const names[] = {"info", "warning", "error", "fatal"};
  return names[severity];
}
const char *qa_character_code_name(qa_character_code code) {
  static const char *const names[] = {"loaded", "missing-source", "missing-skill", "fallback",
                                      "parse-error", "invalid-input", "invalid-handle", "invalid-index",
                                      "uninitialized", "wrong-type", "invalid-bounds",
                                      "unsupported-log-format"};
  return names[code];
}
/* %f on a binary32 value matches the TS toFixed(6) path with its explicit tie-to-even branch. */
static char *character_float_text(double value) {
  if (isnan(value)) return lib_strdup("nan");
  if (isinf(value)) return lib_strdup(value < 0 ? "-inf" : "inf");
  return lib_format("%.6f", value);
}
void qa_bot_character_library_init(qa_bot_character_library *l) {
  memset(l, 0, sizeof *l);
  qa_bot_memory_init(&l->owned_memory);
  l->memory = &l->owned_memory;
  l->next_pointer = 1;
}
void qa_bot_character_library_configure(qa_bot_character_library *l, const qa_bot_script_reader *reader,
                                        const qa_bot_character_options *options) {
  l->reader = *reader;
  l->has_reader = true;
  memset(&l->options, 0, sizeof l->options);
  if (options != NULL) l->options = *options;
  if (l->options.memory != NULL) l->memory = l->options.memory;
}
static void destroy_diagnostic(qa_character_diagnostic *d) {
  free(d->source);
  free(d->message);
  if (d->has_location) qa_ui_script_location_destroy(&d->location);
}
void qa_bot_character_library_destroy(qa_bot_character_library *l) {
  for (size_t i = 0; i < l->reported_count; i++) destroy_diagnostic(&l->reported[i]);
  free(l->reported);
  free(l->strings);
  l->reported = NULL;
  l->strings = NULL;
  l->reported_count = l->reported_capacity = l->string_capacity = 0;
  qa_bot_memory_destroy(&l->owned_memory);
}
const qa_character_diagnostic *qa_bot_character_diagnostics(const qa_bot_character_library *l,
                                                            size_t *out_count) {
  *out_count = l->reported_count;
  return l->reported;
}
/* ---- diagnostics ---- */
static void record(qa_bot_character_library *l, qa_character_severity severity, qa_character_code code,
                   const char *source, const char *message, const qa_ui_script_location *location) {
  if (l->reported_count == l->reported_capacity) {
    l->reported_capacity = lib_grow(l->reported_capacity, l->reported_count + 1);
    l->reported = lib_realloc(l->reported, l->reported_capacity * sizeof *l->reported);
  }
  qa_character_diagnostic *d = &l->reported[l->reported_count++];
  char * source2 = lib_strdup(source);
  char * message2 = lib_strdup(message);
  *d = (qa_character_diagnostic){severity, code, source2, message2, location != NULL,
                                 {NULL, 0, 0}};
  if (location != NULL) d->location = lib_location(location->path, location->line, location->column);
  if (l->options.report != NULL) {
    /* A reentrant report may grow the list; hand it a stable copy. */
    qa_character_diagnostic copy = *d;
    l->options.report(l->options.report_ctx, &copy);
  }
}
__attribute__((format(printf, 5, 6))) static void emit(qa_bot_character_library *l,
                                                       qa_character_severity severity,
                                                       qa_character_code code, const char *source,
                                                       const char *format, ...) {
  va_list args;
  va_start(args, format);
  char *text;
  if (vasprintf(&text, format, args) < 0) qa_range_error("out of memory");
  va_end(args);
  record(l, severity, code, source, text, NULL);
  free(text);
}
/* ---- profile cells ---- */
static uint8_t *cell_at(const qa_bot_memory_allocation *profile, int64_t index) {
  return profile->bytes + CHARACTERISTICS_OFFSET + index * CHARACTERISTIC_BYTES;
}
static int cell_kind(const uint8_t *cell) {
  if (cell[0] > KIND_STRING) qa_ui_script_plain_error("Invalid bot characteristic type byte");
  return cell[0];
}
static float profile_skill(const qa_bot_memory_allocation *profile) { return lib_f32(profile->bytes + MAX_QPATH); }
static const char *profile_filename(const qa_bot_memory_allocation *profile) {
  return (const char *)profile->bytes;
}
static qa_bot_memory_allocation new_profile(qa_bot_character_library *l) {
  return lib_mem_alloc(l->memory, PROFILE_BYTES, QA_BOT_MEMORY_HEAP, true);
}
static void set_filename(qa_bot_memory_allocation *profile, const char *filename) {
  size_t length = strlen(filename);
  memcpy(profile->bytes, filename, length);
  profile->bytes[length] = 0;
}
static qa_character_string_cell *string_cell(qa_bot_character_library *l, uint32_t pointer) {
  if (pointer >= l->string_capacity || !l->strings[pointer].live)
    qa_ui_script_plain_error("Invalid bot characteristic string pointer");
  return &l->strings[pointer];
}
static const char *cell_string(qa_bot_character_library *l, const uint8_t *cell) {
  return (const char *)string_cell(l, lib_u32(cell + 4))->allocation.bytes;
}
static void set_string(qa_bot_character_library *l, qa_bot_memory_allocation *profile, int64_t index,
                       const char *value, size_t length, bool type_before_allocation) {
  uint8_t *cell = cell_at(profile, index);
  if (type_before_allocation) cell[0] = KIND_STRING;
  qa_bot_memory_allocation allocation =
      lib_mem_alloc(l->memory, (int64_t)length + 1, QA_BOT_MEMORY_HEAP, false);
  int64_t pointer = l->next_pointer++;
  if (pointer > 0xffffffffLL) qa_range_error("Bot character string pointer space exhausted");
  if ((size_t)pointer >= l->string_capacity) {
    size_t next = lib_grow(l->string_capacity, (size_t)pointer + 1);
    l->strings = lib_realloc(l->strings, next * sizeof *l->strings);
    memset(l->strings + l->string_capacity, 0, (next - l->string_capacity) * sizeof *l->strings);
    l->string_capacity = next;
  }
  l->strings[pointer] = (qa_character_string_cell){true, allocation};
  lib_set_u32(cell + 4, (uint32_t)pointer);
  memcpy(allocation.bytes, value, length);
  allocation.bytes[length] = 0;
  cell[0] = KIND_STRING;
}
/* TS setValue(index, value, true) with a value read from another profile. */
static void copy_value(qa_bot_character_library *l, qa_bot_memory_allocation *profile, int64_t index,
                       const uint8_t *source, int kind) {
  uint8_t *cell = cell_at(profile, index);
  if (kind == KIND_STRING) {
    char *text = lib_strdup(cell_string(l, source));
    set_string(l, profile, index, text, strlen(text), true);
    free(text);
    return;
  }
  memcpy(cell + 4, source + 4, 4);
  cell[0] = (uint8_t)kind;
}
static void profile_free(qa_bot_character_library *l, qa_bot_memory_allocation *profile) {
  /* The source loader accepts 80, but its string-free loop stops at 79. */
  for (int64_t index = 0; index < MAX_CHARACTERISTICS; index++) {
    const uint8_t *cell = cell_at(profile, index);
    if (cell[0] != KIND_STRING) continue;
    uint32_t pointer = lib_u32(cell + 4);
    if (pointer == 0) continue;
    qa_character_string_cell *string = string_cell(l, pointer);
    qa_bot_memory_free(l->memory, &string->allocation);
    string->live = false;
  }
  qa_bot_memory_free(l->memory, profile);
}
/* ---- file arguments ---- */
static char *file_name(qa_bot_character_file file) {
  return lib_strdup(file.text != NULL ? file.text : file.read(file.ctx));
}
static qa_bot_character_file literal_file(const char *text) {
  return (qa_bot_character_file){text, NULL, NULL};
}
/* ---- loading ---- */
typedef enum attempt_kind { ATTEMPT_LOADED, ATTEMPT_MISSING_SOURCE, ATTEMPT_MISSING_SKILL, ATTEMPT_PARSE_ERROR } attempt_kind;
typedef struct load_report {
  qa_bot_character_library *library;
  const char *filename;
  int64_t generation;
} load_report;
static void load_report_fn(void *ctx, const qa_ui_script_diagnostic *item) {
  load_report *r = ctx;
  if (r->generation != r->library->generation) return;
  record(r->library,
         item->severity == QA_UI_SCRIPT_SEVERITY_WARNING ? QA_CHARACTER_WARNING : QA_CHARACTER_ERROR,
         QA_CHARACTER_PARSE_ERROR, r->filename, item->message, &item->location);
}
static bool load_resolve(void *ctx, const qa_ui_script_include_request *request, qa_ui_script_source *out) {
  qa_bot_character_library *l = ctx;
  return l->reader.resolve(l->reader.ctx, request, out);
}
__attribute__((format(printf, 6, 7))) static attempt_kind parse_error(
    qa_bot_character_library *l, const char *filename, qa_ui_script_source_reader *tokens,
    int64_t generation, const qa_ui_script_token *token, const char *format, ...) {
  if (generation == l->generation) {
    va_list args;
    va_start(args, format);
    char *text;
    if (vasprintf(&text, format, args) < 0) qa_range_error("out of memory");
    va_end(args);
    qa_ui_script_location at = lib_source_location(tokens, token != NULL ? token->location.column : 1);
    record(l, QA_CHARACTER_ERROR, QA_CHARACTER_PARSE_ERROR, filename, text, &at);
    qa_ui_script_location_destroy(&at);
    free(text);
  }
  return ATTEMPT_PARSE_ERROR;
}
static bool is_punctuation(const qa_ui_script_token *token, int64_t punctuation) {
  return token != NULL && token->kind == QA_UI_SCRIPT_KIND_PUNCTUATION && token->punctuation == punctuation;
}
static void release(qa_ui_script_token *token) {
  if (token != NULL) qa_ui_script_token_release(token);
}
static attempt_kind parse_tokens(qa_bot_character_library *l, const char *filename,
                                 qa_bot_memory_allocation *profile, double desired_skill,
                                 qa_ui_script_source_reader *tokens, int64_t generation) {
  while (true) {
    qa_ui_script_token *definition = lib_source_token(tokens);
    if (generation != l->generation) {
      release(definition);
      return ATTEMPT_PARSE_ERROR;
    }
    if (definition == NULL) return ATTEMPT_MISSING_SKILL;
    if (definition->kind != QA_UI_SCRIPT_KIND_NAME || strcmp(definition->value, "skill") != 0) {
      attempt_kind result = parse_error(l, filename, tokens, generation, definition,
                                        "unknown definition %s\n", definition->text);
      release(definition);
      return result;
    }
    release(definition);
    qa_ui_script_token *skill_token = lib_source_token(tokens);
    if (generation != l->generation) {
      release(skill_token);
      return ATTEMPT_PARSE_ERROR;
    }
    if (skill_token == NULL)
      return parse_error(l, filename, tokens, generation, NULL, "couldn't read expected token");
    if (skill_token->kind != QA_UI_SCRIPT_KIND_NUMBER) {
      attempt_kind result = parse_error(l, filename, tokens, generation, skill_token,
                                        "expected a number, found %s", skill_token->text);
      release(skill_token);
      return result;
    }
    uint32_t skill_value = skill_token->integer_value;
    release(skill_token);
    qa_ui_script_token *open = lib_source_token(tokens);
    if (generation != l->generation) {
      release(open);
      return ATTEMPT_PARSE_ERROR;
    }
    if (!is_punctuation(open, QA_UI_SCRIPT_PUNCT_BRACE_OPEN)) {
      attempt_kind result =
          open == NULL ? parse_error(l, filename, tokens, generation, NULL, "couldn't find expected {")
                       : parse_error(l, filename, tokens, generation, open, "expected {, found %s", open->text);
      release(open);
      return result;
    }
    release(open);
    if (desired_skill < 0 || (double)skill_value == desired_skill) {
      lib_set_f32(profile->bytes + MAX_QPATH, (float)skill_value);
      while (true) {
        qa_ui_script_token *index_token = lib_source_token(tokens);
        if (generation != l->generation) {
          release(index_token);
          return ATTEMPT_PARSE_ERROR;
        }
        if (is_punctuation(index_token, QA_UI_SCRIPT_PUNCT_BRACE_CLOSE)) {
          release(index_token);
          return ATTEMPT_LOADED;
        }
        if (index_token == NULL) {
          /* PC_ExpectAnyToken reports EOF, but this source loop still returns ch. */
          parse_error(l, filename, tokens, generation, NULL, "couldn't read expected token");
          return generation == l->generation ? ATTEMPT_LOADED : ATTEMPT_PARSE_ERROR;
        }
        if (index_token->kind != QA_UI_SCRIPT_KIND_NUMBER ||
            (index_token->flags & QA_UI_SCRIPT_NUMBER_INTEGER) == 0) {
          attempt_kind result = parse_error(l, filename, tokens, generation, index_token,
                                            "expected integer index, found %s\n", index_token->text);
          release(index_token);
          return result;
        }
        uint32_t index = index_token->integer_value;
        if (index > MAX_CHARACTERISTICS) {
          attempt_kind result = parse_error(l, filename, tokens, generation, index_token,
                                            "characteristic index out of range [0, %d]\n", MAX_CHARACTERISTICS);
          release(index_token);
          return result;
        }
        if (cell_kind(cell_at(profile, index)) != KIND_NONE) {
          attempt_kind result = parse_error(l, filename, tokens, generation, index_token,
                                            "characteristic %u already initialized\n", index);
          release(index_token);
          return result;
        }
        release(index_token);
        qa_ui_script_token *value_token = lib_source_token(tokens);
        if (generation != l->generation) {
          release(value_token);
          return ATTEMPT_PARSE_ERROR;
        }
        if (value_token == NULL)
          return parse_error(l, filename, tokens, generation, NULL, "couldn't read expected token");
        uint8_t *cell = cell_at(profile, index);
        if (value_token->kind == QA_UI_SCRIPT_KIND_NUMBER) {
          if ((value_token->flags & QA_UI_SCRIPT_NUMBER_FLOAT) != 0) {
            float value = (float)value_token->float_value;
            if (!isfinite(value)) {
              attempt_kind result = parse_error(l, filename, tokens, generation, value_token,
                                                "non-finite characteristic float");
              release(value_token);
              return result;
            }
            lib_set_f32(cell + 4, value);
            cell[0] = KIND_FLOAT;
          } else {
            lib_set_i32(cell + 4, (int32_t)value_token->integer_value);
            cell[0] = KIND_INTEGER;
          }
        } else if (value_token->kind == QA_UI_SCRIPT_KIND_STRING) {
          const char *zero = memchr(value_token->value, 0, value_token->value_length);
          size_t length = zero != NULL ? (size_t)(zero - value_token->value) : value_token->value_length;
          set_string(l, profile, index, value_token->value, length, false);
        } else {
          attempt_kind result = parse_error(l, filename, tokens, generation, value_token,
                                            "expected integer, float or string, found %s\n", value_token->text);
          release(value_token);
          return result;
        }
        release(value_token);
      }
    }
    int64_t depth = 1;
    while (depth > 0) {
      qa_ui_script_token *token = lib_source_token(tokens);
      if (generation != l->generation) {
        release(token);
        return ATTEMPT_PARSE_ERROR;
      }
      if (token == NULL)
        return parse_error(l, filename, tokens, generation, NULL, "couldn't read expected token");
      if (is_punctuation(token, QA_UI_SCRIPT_PUNCT_BRACE_OPEN)) depth++;
      else if (is_punctuation(token, QA_UI_SCRIPT_PUNCT_BRACE_CLOSE)) depth--;
      release(token);
    }
  }
}
static attempt_kind load_from_file(qa_bot_character_library *l, qa_bot_character_file file,
                                   double desired_skill, int64_t generation,
                                   qa_bot_memory_allocation *out_profile) {
  if (generation != l->generation) return ATTEMPT_PARSE_ERROR;
  char *filename = file_name(file);
  if (generation != l->generation) {
    free(filename);
    return ATTEMPT_PARSE_ERROR;
  }
  if (!l->has_reader) qa_range_error("BotCharacterLibrary has no script reader");
  qa_ui_script_source source;
  if (!l->reader.resolve_root(l->reader.ctx, filename, &source)) {
    if (generation == l->generation) {
      char *current = file_name(file);
      emit(l, QA_CHARACTER_ERROR, QA_CHARACTER_MISSING_SOURCE, current, "counldn't load %s", current);
      free(current);
    }
    free(filename);
    return ATTEMPT_MISSING_SOURCE;
  }
  if (generation != l->generation) {
    free(filename);
    return ATTEMPT_PARSE_ERROR;
  }
  char *stored = file_name(file);
  if (generation != l->generation) {
    free(stored);
    free(filename);
    return ATTEMPT_PARSE_ERROR;
  }
  load_report report = {l, filename, generation};
  qa_ui_script_preprocessor_options options;
  memset(&options, 0, sizeof options);
  options.globals = l->reader.globals;
  if (l->reader.debug_eval != NULL) {
    options.debug_eval = l->reader.debug_eval;
    options.debug_eval_ctx = l->reader.debug_eval_ctx;
  }
  options.report = load_report_fn;
  options.report_ctx = &report;
  qa_ui_script_source_reader *tokens = qa_ui_script_source_reader_open(&source, load_resolve, l, &options);
  if (tokens == NULL) qa_range_error("out of memory");
  qa_bot_memory_allocation profile = new_profile(l);
  if (strlen(stored) >= MAX_QPATH) {
    char *message = lib_strdup("character filename copy exceeds MAX_QPATH destination");
    record(l, QA_CHARACTER_ERROR, QA_CHARACTER_INVALID_INPUT, stored, message, NULL);
    qa_ui_script_plain_error("%s", message);
  }
  set_filename(&profile, stored);
  free(stored);
  attempt_kind result = parse_tokens(l, filename, &profile, desired_skill, tokens, generation);
  qa_ui_script_source_reader_dispose(tokens);
  qa_ui_script_source_reader_destroy(tokens);
  free(filename);
  if (result != ATTEMPT_LOADED) profile_free(l, &profile);
  else *out_profile = profile;
  return result;
}
static int64_t free_handle(const qa_bot_character_library *l) {
  for (int64_t handle = 1; handle <= QA_MAX_BOT_CHARACTERS; handle++)
    if (!l->used[handle]) return handle;
  return 0;
}
static int64_t find_cached(qa_bot_character_library *l, qa_bot_character_file file, double skill) {
  int64_t generation = l->generation;
  for (int64_t handle = 1; handle <= QA_MAX_BOT_CHARACTERS; handle++) {
    if (!l->used[handle]) continue;
    uint8_t *identity = l->profiles[handle].bytes;
    char *filename = file_name(file);
    bool stale = generation != l->generation;
    bool retired = !stale && (!l->used[handle] || l->profiles[handle].bytes != identity);
    bool same = !stale && !retired && strcmp(profile_filename(&l->profiles[handle]), filename) == 0 &&
                (skill < 0 || fabs((double)profile_skill(&l->profiles[handle]) - skill) < 0.01);
    free(filename);
    if (stale) return 0;
    if (same) return handle;
  }
  return 0;
}
static int64_t store_profile(qa_bot_character_library *l, qa_bot_memory_allocation profile, int64_t handle,
                             qa_character_code code, qa_bot_character_file file, const char *message) {
  l->profiles[handle] = profile;
  l->used[handle] = true;
  char *filename = file_name(file);
  emit(l, QA_CHARACTER_INFO, code, filename, "%s from %s", message, filename);
  free(filename);
  return handle;
}
static void emit_cached(qa_bot_character_library *l, qa_character_code code, qa_bot_character_file file,
                        const char *label, double skill) {
  char *filename = file_name(file), *text = character_float_text(skill);
  emit(l, QA_CHARACTER_INFO, code, filename, "%s %s from %s", label, text, filename);
  free(text);
  free(filename);
}
static int64_t store_float_message(qa_bot_character_library *l, qa_bot_memory_allocation profile,
                                   int64_t handle, qa_bot_character_file file, const char *label) {
  char *text = character_float_text(profile_skill(&profile));
  char *message = lib_format("%s %s", label, text);
  int64_t result = store_profile(l, profile, handle, QA_CHARACTER_FALLBACK, file, message);
  free(message);
  free(text);
  return result;
}
static int64_t load_cached(qa_bot_character_library *l, qa_bot_character_file file, double skill, bool reload,
                           int64_t generation) {
  qa_bot_character_file default_file = literal_file(DEFAULT_CHARACTER);
  bool debug = l->options.debug_milliseconds != NULL;
  double start = debug ? l->options.debug_milliseconds(l->options.debug_ctx) : 0;
  if (generation != l->generation) return 0;
  int64_t handle = free_handle(l);
  if (handle == 0) return 0;
  if (!reload) {
    int64_t cached = find_cached(l, file, skill);
    if (cached != 0) {
      emit_cached(l, QA_CHARACTER_LOADED, file, "loaded cached skill", skill);
      return cached;
    }
  }
  int64_t integer_skill = (int64_t)trunc(skill + 0.5);
  qa_bot_memory_allocation profile;
  attempt_kind exact = load_from_file(l, file, (double)integer_skill, generation, &profile);
  if (generation != l->generation) return 0;
  if (exact == ATTEMPT_LOADED) {
    char *message = lib_format("loaded skill %lld", (long long)integer_skill);
    int64_t result = store_profile(l, profile, handle, QA_CHARACTER_LOADED, file, message);
    free(message);
    if (generation != l->generation) return 0;
    if (debug && l->options.debug_developer != NULL && l->options.debug_developer(l->options.debug_ctx)) {
      if (generation != l->generation) return 0;
      int32_t elapsed = qa_js_to_int32(l->options.debug_milliseconds(l->options.debug_ctx) - start); /* (now - start) | 0 */
      if (generation != l->generation) return 0;
      char *filename = file_name(file);
      emit(l, QA_CHARACTER_INFO, QA_CHARACTER_LOADED, filename, "skill %lld loaded in %d msec from %s",
           (long long)integer_skill, elapsed, filename);
      free(filename);
    }
    return result;
  }
  char *missing = file_name(file);
  emit(l, QA_CHARACTER_WARNING, QA_CHARACTER_MISSING_SKILL, missing, "couldn't find skill %lld in %s",
       (long long)integer_skill, missing);
  free(missing);
  if (generation != l->generation) return 0;
  if (!reload) {
    int64_t cached_default = find_cached(l, default_file, skill);
    if (cached_default != 0) {
      char *filename = file_name(file);
      emit(l, QA_CHARACTER_INFO, QA_CHARACTER_FALLBACK, filename, "loaded cached default skill %lld from %s",
           (long long)integer_skill, filename);
      free(filename);
      return cached_default;
    }
  }
  attempt_kind exact_default = load_from_file(l, default_file, (double)integer_skill, generation, &profile);
  if (generation != l->generation) return 0;
  if (exact_default == ATTEMPT_LOADED) {
    char *message = lib_format("loaded default skill %lld", (long long)integer_skill);
    int64_t result = store_profile(l, profile, handle, QA_CHARACTER_FALLBACK, file, message);
    free(message);
    return result;
  }
  if (!reload) {
    int64_t cached_any = find_cached(l, file, -1);
    if (cached_any != 0) {
      emit_cached(l, QA_CHARACTER_FALLBACK, file, "loaded cached skill", profile_skill(&l->profiles[cached_any]));
      return cached_any;
    }
  }
  attempt_kind arbitrary = load_from_file(l, file, -1, generation, &profile);
  if (generation != l->generation) return 0;
  if (arbitrary == ATTEMPT_LOADED) return store_float_message(l, profile, handle, file, "loaded skill");
  if (!reload) {
    int64_t cached_default_any = find_cached(l, default_file, -1);
    if (cached_default_any != 0) {
      emit_cached(l, QA_CHARACTER_FALLBACK, file, "loaded cached default skill",
                  profile_skill(&l->profiles[cached_default_any]));
      return cached_default_any;
    }
  }
  attempt_kind default_any = load_from_file(l, default_file, -1, generation, &profile);
  if (generation != l->generation) return 0;
  if (default_any == ATTEMPT_LOADED) return store_float_message(l, profile, handle, file, "loaded default skill");
  char *filename = file_name(file);
  emit(l, QA_CHARACTER_WARNING, QA_CHARACTER_FALLBACK, filename, "couldn't load any skill from %s", filename);
  free(filename);
  return 0;
}
static bool reload_characters(qa_bot_character_library *l) {
  return l->options.reload_characters != NULL && l->options.reload_characters(l->options.reload_ctx);
}
static void with_defaults(qa_bot_character_library *l, int64_t handle, int64_t default_handle) {
  for (int64_t index = 0; index < MAX_CHARACTERISTICS; index++) {
    if (cell_kind(cell_at(&l->profiles[handle], index)) != KIND_NONE) continue;
    const uint8_t *source = cell_at(&l->profiles[default_handle], index);
    int kind = cell_kind(source);
    if (kind != KIND_NONE) copy_value(l, &l->profiles[handle], index, source, kind);
  }
}
static int64_t load_skill(qa_bot_character_library *l, qa_bot_character_file file, double skill, int64_t generation) {
  int64_t default_handle = load_cached(l, literal_file(DEFAULT_CHARACTER), skill, false, generation);
  if (generation != l->generation) return 0;
  int64_t handle = load_cached(l, file, skill, reload_characters(l), generation);
  if (generation != l->generation) return 0;
  if (default_handle != 0 && handle != 0 && l->used[handle] && l->used[default_handle])
    with_defaults(l, handle, default_handle);
  return handle;
}
static int64_t interpolate(qa_bot_character_library *l, int64_t first_handle, int64_t second_handle, float desired) {
  if (!l->used[first_handle] || !l->used[second_handle]) return 0;
  int64_t handle = free_handle(l);
  if (handle == 0) return 0;
  qa_bot_memory_allocation profile = new_profile(l);
  lib_set_f32(profile.bytes + MAX_QPATH, desired);
  set_filename(&profile, profile_filename(&l->profiles[first_handle]));
  l->profiles[handle] = profile;
  l->used[handle] = true;
  float first_skill = profile_skill(&l->profiles[first_handle]);
  float second_skill = profile_skill(&l->profiles[second_handle]);
  float scale = (float)((double)(float)((double)desired - (double)first_skill) /
                        (double)(float)((double)second_skill - (double)first_skill));
  for (int64_t index = 0; index < MAX_CHARACTERISTICS; index++) {
    const uint8_t *lower = cell_at(&l->profiles[first_handle], index);
    const uint8_t *upper = cell_at(&l->profiles[second_handle], index);
    int lower_kind = cell_kind(lower), upper_kind = cell_kind(upper);
    if (lower_kind == KIND_FLOAT && upper_kind == KIND_FLOAT) {
      float low = lib_f32(lower + 4);
      float delta = (float)((double)lib_f32(upper + 4) - (double)low);
      uint8_t *cell = cell_at(&l->profiles[handle], index);
      lib_set_f32(cell + 4, (float)((double)low + (double)(float)((double)delta * (double)scale)));
      cell[0] = KIND_FLOAT;
    } else if (lower_kind == KIND_INTEGER || lower_kind == KIND_STRING) {
      copy_value(l, &l->profiles[handle], index, lower, lower_kind);
    }
  }
  return handle;
}
static const qa_bot_memory_allocation *profile_of(qa_bot_character_library *l, int64_t handle) {
  if (handle <= 0 || handle > QA_MAX_BOT_CHARACTERS) {
    emit(l, QA_CHARACTER_FATAL, QA_CHARACTER_INVALID_HANDLE, "<character-library>",
         "character handle %lld out of range", (long long)handle);
    return NULL;
  }
  if (!l->used[handle]) {
    emit(l, QA_CHARACTER_FATAL, QA_CHARACTER_INVALID_HANDLE, "<character-library>", "invalid character %lld",
         (long long)handle);
    return NULL;
  }
  return &l->profiles[handle];
}
void qa_bot_character_dump(qa_bot_character_library *l, int64_t handle) {
  const qa_bot_memory_allocation *profile = profile_of(l, handle);
  if (profile == NULL) return;
  bool has_log = l->options.log_write != NULL;
  void *ctx = l->options.log_ctx;
  if (has_log) l->options.log_write(ctx, profile_filename(profile));
  if (has_log && l->options.log_has_file != NULL && l->options.log_has_file(ctx))
    emit(l, QA_CHARACTER_WARNING, QA_CHARACTER_UNSUPPORTED_LOG_FORMAT, profile_filename(profile),
         "BotDumpCharacter: omitted undefined skill log format (%%d receives a promoted float)");
  if (has_log) l->options.log_write(ctx, "{\n");
  for (int64_t index = 0; index < MAX_CHARACTERISTICS; index++) {
    const uint8_t *cell = cell_at(profile, index);
    int kind = cell_kind(cell);
    if (kind == KIND_NONE) continue;
    char *value = kind == KIND_FLOAT   ? character_float_text(lib_f32(cell + 4))
                  : kind == KIND_INTEGER ? lib_format("%d", lib_i32(cell + 4))
                                         : lib_strdup(cell_string(l, cell));
    char *line = lib_format(" %4lld %s\n", (long long)index, value);
    if (has_log) l->options.log_write(ctx, line);
    free(line);
    free(value);
  }
  if (has_log) l->options.log_write(ctx, "}\n");
}
static int64_t load_transaction(qa_bot_character_library *l, qa_bot_character_file file, float desired,
                                int64_t generation) {
  if (desired == 1 || desired == 4 || desired == 5) return load_skill(l, file, desired, generation);
  int64_t cached = find_cached(l, file, desired);
  if (cached != 0) {
    emit_cached(l, QA_CHARACTER_LOADED, file, "loaded cached skill", desired);
    return generation == l->generation ? cached : 0;
  }
  int64_t first, second;
  if (desired < 4) {
    first = load_skill(l, file, 1, generation);
    if (first == 0) return 0;
    second = load_skill(l, file, 4, generation);
  } else {
    first = load_skill(l, file, 4, generation);
    if (first == 0) return 0;
    second = load_skill(l, file, 5, generation);
  }
  if (generation != l->generation) return 0;
  if (second == 0) return first;
  int64_t handle = interpolate(l, first, second, desired);
  if (handle != 0 && generation == l->generation) qa_bot_character_dump(l, handle);
  return generation == l->generation ? handle : 0;
}
int64_t qa_bot_character_load_file(qa_bot_character_library *l, qa_bot_character_file file, double skill) {
  float desired = (float)skill;
  if (desired < 1) desired = 1;
  else if (desired > 5) desired = 5;
  if (!isfinite(desired)) {
    char *filename = file_name(file);
    const char *message = "character skill must be finite";
    record(l, QA_CHARACTER_ERROR, QA_CHARACTER_INVALID_INPUT, filename, message, NULL);
    free(filename);
    qa_ui_script_plain_error("%s", message);
  }
  return load_transaction(l, file, desired, l->generation);
}
int64_t qa_bot_character_load(qa_bot_character_library *l, const char *file, double skill) {
  return qa_bot_character_load_file(l, literal_file(file), skill);
}
void qa_bot_character_free(qa_bot_character_library *l, int64_t handle) {
  if (!reload_characters(l)) return;
  if (profile_of(l, handle) == NULL) return;
  profile_free(l, &l->profiles[handle]);
  l->used[handle] = false;
}
void qa_bot_character_shutdown(qa_bot_character_library *l) {
  l->generation += 1;
  for (int64_t handle = 1; handle <= QA_MAX_BOT_CHARACTERS; handle++) {
    if (l->used[handle]) profile_free(l, &l->profiles[handle]);
    l->used[handle] = false;
  }
}
static const char *profile_source(const qa_bot_character_library *l, int64_t handle) {
  return handle > 0 && handle <= QA_MAX_BOT_CHARACTERS && l->used[handle]
             ? profile_filename(&l->profiles[handle])
             : "<character-library>";
}
/* TS value(): NULL is undefined; *out_kind is the record type. */
static const uint8_t *value_cell(qa_bot_character_library *l, int64_t handle, int64_t index, int *out_kind) {
  const qa_bot_memory_allocation *profile = profile_of(l, handle);
  if (profile == NULL) return NULL;
  if (index < 0 || index >= MAX_CHARACTERISTICS) {
    emit(l, QA_CHARACTER_ERROR, QA_CHARACTER_INVALID_INDEX, profile_filename(profile),
         "characteristic %lld does not exist", (long long)index);
    return NULL;
  }
  const uint8_t *cell = cell_at(profile, index);
  int kind = cell_kind(cell);
  if (kind == KIND_NONE) {
    emit(l, QA_CHARACTER_ERROR, QA_CHARACTER_UNINITIALIZED, profile_filename(profile),
         "characteristic %lld is not initialized", (long long)index);
    return NULL;
  }
  *out_kind = kind;
  return cell;
}
float qa_bot_character_float(qa_bot_character_library *l, int64_t handle, int64_t index) {
  int kind = KIND_NONE;
  const uint8_t *cell = value_cell(l, handle, index, &kind);
  if (cell == NULL) return 0;
  if (kind == KIND_INTEGER) return (float)lib_i32(cell + 4);
  if (kind == KIND_FLOAT) return lib_f32(cell + 4);
  emit(l, QA_CHARACTER_ERROR, QA_CHARACTER_WRONG_TYPE, profile_source(l, handle),
       "characteristic %lld is not a float", (long long)index);
  return 0;
}
float qa_bot_character_bounded_float(qa_bot_character_library *l, int64_t handle, int64_t index,
                                     double minimum, double maximum) {
  if (profile_of(l, handle) == NULL) return 0;
  float min = (float)minimum, max = (float)maximum;
  if (min > max) {
    char *low = character_float_text(min), *high = character_float_text(max);
    emit(l, QA_CHARACTER_ERROR, QA_CHARACTER_INVALID_BOUNDS, profile_source(l, handle),
         "cannot bound characteristic %lld between %s and %s", (long long)index, low, high);
    free(low);
    free(high);
    return 0;
  }
  float value = qa_bot_character_float(l, handle, index);
  if (value < min) return min;
  if (value > max) return max;
  return value;
}
int32_t qa_bot_character_integer(qa_bot_character_library *l, int64_t handle, int64_t index) {
  int kind = KIND_NONE;
  const uint8_t *cell = value_cell(l, handle, index, &kind);
  if (cell == NULL) return 0;
  if (kind == KIND_INTEGER) return lib_i32(cell + 4);
  if (kind == KIND_FLOAT) return qa_int32(trunc((double)lib_f32(cell + 4)));
  emit(l, QA_CHARACTER_ERROR, QA_CHARACTER_WRONG_TYPE, profile_source(l, handle),
       "characteristic %lld is not a integer", (long long)index);
  return 0;
}
int32_t qa_bot_character_bounded_integer(qa_bot_character_library *l, int64_t handle, int64_t index,
                                         double minimum, double maximum) {
  if (profile_of(l, handle) == NULL) return 0;
  if (trunc(minimum) != minimum || trunc(maximum) != maximum) {
    emit(l, QA_CHARACTER_ERROR, QA_CHARACTER_INVALID_BOUNDS, profile_source(l, handle),
         "integer characteristic bounds must be integers");
    return 0;
  }
  int32_t min = qa_int32(minimum), max = qa_int32(maximum);
  if (min > max) {
    emit(l, QA_CHARACTER_ERROR, QA_CHARACTER_INVALID_BOUNDS, profile_source(l, handle),
         "cannot bound characteristic %lld between %d and %d", (long long)index, min, max);
    return 0;
  }
  int32_t value = qa_bot_character_integer(l, handle, index);
  if (value < min) return min;
  if (value > max) return max;
  return value;
}
const char *qa_bot_character_string(qa_bot_character_library *l, int64_t handle, int64_t index) {
  int kind = KIND_NONE;
  const uint8_t *cell = value_cell(l, handle, index, &kind);
  if (cell == NULL) return "";
  if (kind == KIND_STRING) return cell_string(l, cell);
  emit(l, QA_CHARACTER_ERROR, QA_CHARACTER_WRONG_TYPE, profile_source(l, handle),
       "characteristic %lld is not a string", (long long)index);
  return "";
}
void qa_bot_character_write_string(qa_bot_character_library *l, int64_t handle, int64_t index,
                                   void (*write)(void *ctx, const char *text), void *ctx) {
  int kind = KIND_NONE;
  const uint8_t *cell = value_cell(l, handle, index, &kind);
  if (cell == NULL) return;
  if (kind == KIND_STRING) {
    write(ctx, cell_string(l, cell));
    return;
  }
  emit(l, QA_CHARACTER_ERROR, QA_CHARACTER_WRONG_TYPE, profile_source(l, handle),
       "characteristic %lld is not a string", (long long)index);
}
qa_save_value *qa_bot_character_checkpoint(qa_bot_character_library *l, qa_bot_memory_capture *memory) {
  qa_save_value *object = lib_sv(qa_save_value_object());
  lib_set_num(object, "generation", (double)l->generation);
  qa_save_value *reported = lib_sv(qa_save_value_array());
  for (size_t i = 0; i < l->reported_count; i++) {
    const qa_character_diagnostic *d = &l->reported[i];
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set(entry, "severity", qa_save_value_string(qa_character_severity_name(d->severity)));
    lib_set(entry, "code", qa_save_value_string(qa_character_code_name(d->code)));
    lib_set(entry, "source", qa_save_value_string(d->source));
    lib_set(entry, "message", qa_save_value_string(d->message));
    if (d->has_location) {
      qa_save_value *location = lib_sv(qa_save_value_object());
      lib_set(location, "path", qa_save_value_string(d->location.path));
      lib_set_num(location, "line", (double)d->location.line);
      lib_set_num(location, "column", (double)d->location.column);
      lib_set(entry, "location", location);
    } else {
      lib_set(entry, "location", qa_save_value_null());
    }
    lib_push(reported, entry);
  }
  lib_set(object, "reported", reported);
  lib_set_num(object, "nextPointer", (double)l->next_pointer);
  qa_save_value *strings = lib_sv(qa_save_value_array());
  for (size_t pointer = 0; pointer < l->string_capacity; pointer++) {
    if (!l->strings[pointer].live) continue;
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set_num(entry, "pointer", (double)pointer);
    lib_set_num(entry, "allocation", (double)qa_bot_memory_capture_reference(memory, &l->strings[pointer].allocation));
    lib_push(strings, entry);
  }
  lib_set(object, "strings", strings);
  qa_save_value *profiles = lib_sv(qa_save_value_array());
  for (int64_t handle = 0; handle <= QA_MAX_BOT_CHARACTERS; handle++)
    lib_push(profiles, l->used[handle] ? qa_save_value_number((double)qa_bot_memory_capture_reference(memory, &l->profiles[handle]))
                                       : qa_save_value_null());
  lib_set(object, "profiles", profiles);
  return object;
}
void qa_bot_character_restore(qa_bot_character_library *l, const qa_save_value *value,
                              qa_bot_memory_restored *memory) {
  static const char *const severities[] = {"info", "warning", "error", "fatal"};
  static const char *const codes[] = {"loaded", "missing-source", "missing-skill", "fallback",
                                      "parse-error", "invalid-input", "invalid-handle", "invalid-index",
                                      "uninitialized", "wrong-type", "invalid-bounds", "unsupported-log-format"};
  qa_save_reader reader, reported, strings, profiles;
  if (!qa_save_reader_init(&reader, value, "bot.characters")) qa_range_error("out of memory");
  double generation = lib_int_min(&reader, "generation", 0);
  double next = lib_int_min(&reader, "nextPointer", 1);
  lib_field(&reader, "reported", &reported);
  lib_field(&reader, "strings", &strings);
  lib_field(&reader, "profiles", &profiles);
  bool any = false;
  for (int64_t handle = 0; handle <= QA_MAX_BOT_CHARACTERS && !any; handle++) any = l->used[handle];
  for (size_t i = 0; i < l->string_capacity && !any; i++) any = l->strings[i].live;
  bool first_null = false;
  if (qa_save_reader_list_count(&profiles) == QA_MAX_BOT_CHARACTERS + 1) {
    qa_save_reader first;
    lib_item(&profiles, 0, &first);
    first_null = qa_save_reader_is_null(&first);
    qa_save_reader_destroy(&first);
  }
  if (any || !first_null || generation < 0 || next < 1 || next > 4294967296.0)
    qa_ui_script_plain_error("Invalid bot character restoration");
  size_t string_count = qa_save_reader_list_count(&strings);
  for (size_t i = 0; i < string_count; i++) {
    qa_save_reader entry;
    lib_item(&strings, i, &entry);
    double pointer = lib_int_min(&entry, "pointer", 1);
    double reference = lib_int_min(&entry, "allocation", 0);
    qa_save_reader_destroy(&entry);
    if (pointer < 1 || pointer >= next ||
        ((size_t)pointer < l->string_capacity && l->strings[(size_t)pointer].live))
      qa_ui_script_plain_error("Invalid saved bot character string pointer");
    qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
    if (memchr(allocation.bytes, 0, allocation.length) == NULL)
      qa_ui_script_plain_error("Saved bot character string is unterminated");
    if ((size_t)pointer >= l->string_capacity) {
      size_t grown = lib_grow(l->string_capacity, (size_t)pointer + 1);
      l->strings = lib_realloc(l->strings, grown * sizeof *l->strings);
      memset(l->strings + l->string_capacity, 0, (grown - l->string_capacity) * sizeof *l->strings);
      l->string_capacity = grown;
    }
    l->strings[(size_t)pointer] = (qa_character_string_cell){true, allocation};
  }
  for (size_t index = 0; index <= QA_MAX_BOT_CHARACTERS; index++) {
    qa_save_reader entry;
    lib_item(&profiles, index, &entry);
    if (!qa_save_reader_is_null(&entry)) {
      double reference = qa_save_reader_integer_min(&entry, 0);
      qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
      if (allocation.length != PROFILE_BYTES)
        qa_ui_script_plain_error("Saved bot character allocation size mismatch");
      for (int64_t characteristic = 0; characteristic < STORED_CHARACTERISTICS; characteristic++) {
        const uint8_t *cell = cell_at(&allocation, characteristic);
        if (cell_kind(cell) == KIND_STRING) (void)cell_string(l, cell);
      }
      l->profiles[index] = allocation;
      l->used[index] = true;
    }
    qa_save_reader_destroy(&entry);
  }
  l->next_pointer = (int64_t)next;
  l->generation = (int64_t)generation;
  size_t reported_count = qa_save_reader_list_count(&reported);
  for (size_t i = 0; i < reported_count; i++) {
    qa_save_reader entry, severity_reader, code_reader, location_reader;
    lib_item(&reported, i, &entry);
    lib_field(&entry, "severity", &severity_reader);
    const char *severity = qa_save_reader_choice_string(&severity_reader, severities, 4);
    lib_field(&entry, "code", &code_reader);
    const char *code = qa_save_reader_choice_string(&code_reader, codes, 12);
    char *source = lib_read_string(&entry, "source");
    char *message = lib_read_string(&entry, "message");
    lib_field(&entry, "location", &location_reader);
    qa_character_diagnostic d = {QA_CHARACTER_INFO, QA_CHARACTER_LOADED, source, message, false, {NULL, 0, 0}};
    for (int s = 0; s < 4; s++) if (strcmp(severity, severities[s]) == 0) d.severity = (qa_character_severity)s;
    for (int c = 0; c < 12; c++) if (strcmp(code, codes[c]) == 0) d.code = (qa_character_code)c;
    if (!qa_save_reader_is_null(&location_reader)) {
      d.has_location = true;
      d.location.path = lib_read_string(&location_reader, "path");
      d.location.line = (int64_t)lib_int(&location_reader, "line");
      d.location.column = (int64_t)lib_int(&location_reader, "column");
    }
    if (l->reported_count == l->reported_capacity) {
      l->reported_capacity = lib_grow(l->reported_capacity, l->reported_count + 1);
      l->reported = lib_realloc(l->reported, l->reported_capacity * sizeof *l->reported);
    }
    l->reported[l->reported_count++] = d;
    qa_save_reader_destroy(&location_reader);
    qa_save_reader_destroy(&code_reader);
    qa_save_reader_destroy(&severity_reader);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&reported);
  qa_save_reader_destroy(&strings);
  qa_save_reader_destroy(&profiles);
  qa_save_reader_destroy(&reader);
}
