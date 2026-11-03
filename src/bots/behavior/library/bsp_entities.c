/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/bsp_entities.h"
#include "library_internal.h"
#include "qa/core/numeric.h"
#include <ctype.h>
#include <math.h>
#include <strings.h>
void qa_aas_bsp_entities_init(qa_aas_bsp_entities *e) {
  memset(e, 0, sizeof *e);
  qa_bot_memory_init(&e->owned_memory);
  e->memory = &e->owned_memory;
  e->next_allocation = 1;
}
void qa_aas_bsp_entities_configure(qa_aas_bsp_entities *e, qa_aas_bsp_print_fn print, void *print_ctx,
                                   qa_bot_memory *memory) {
  e->print = print;
  e->print_ctx = print_ctx;
  if (memory != NULL) e->memory = memory;
}
void qa_aas_bsp_entities_destroy(qa_aas_bsp_entities *e) {
  free(e->allocations);
  e->allocations = NULL;
  e->allocation_capacity = 0;
  qa_bot_memory_destroy(&e->owned_memory);
}
bool qa_aas_bsp_entities_loaded(const qa_aas_bsp_entities *e) { return e->is_loaded; }
static void print_text(qa_aas_bsp_entities *e, int severity, const char *text) {
  if (e->print != NULL) e->print(e->print_ctx, severity, text);
}
static void check_int32(int64_t value) {
  if (value < INT32_MIN || value > INT32_MAX)
    qa_range_error("BSP entity numbers must be signed 32-bit integers");
}
static int64_t allocate_cell(qa_aas_bsp_entities *e, int64_t size, bool clear) {
  if (e->next_allocation > 0xffffffffLL)
    qa_range_error("BSP allocation IDs exceed release32 pointer cells");
  qa_bot_memory_allocation allocation = lib_mem_alloc(e->memory, size, QA_BOT_MEMORY_HUNK, clear);
  int64_t pointer = e->next_allocation++;
  if ((size_t)pointer >= e->allocation_capacity) {
    size_t next = lib_grow(e->allocation_capacity, (size_t)pointer + 1);
    e->allocations = lib_realloc(e->allocations, next * sizeof *e->allocations);
    memset(e->allocations + e->allocation_capacity, 0,
           (next - e->allocation_capacity) * sizeof *e->allocations);
    e->allocation_capacity = next;
  }
  e->allocations[pointer] = (qa_aas_bsp_allocation_cell){true, allocation};
  return pointer;
}
static qa_bot_memory_allocation *cell(qa_aas_bsp_entities *e, uint32_t pointer) {
  if (pointer >= e->allocation_capacity || !e->allocations[pointer].live)
    qa_range_error("BSP epair pointer does not identify an owned allocation");
  return &e->allocations[pointer].allocation;
}
static uint32_t pointer_at(qa_aas_bsp_entities *e, uint32_t pair, size_t offset) {
  qa_bot_memory_allocation *allocation = cell(e, pair);
  if (offset + 4 > allocation->length) qa_range_error("Offset is outside the bounds of the DataView");
  return lib_u32(allocation->bytes + offset);
}
static void set_pointer(qa_aas_bsp_entities *e, uint32_t pair, size_t offset, uint32_t pointer) {
  lib_set_u32(cell(e, pair)->bytes + offset, pointer);
}
static const char *read_string(qa_bot_memory_allocation *allocation) {
  if (memchr(allocation->bytes, 0, allocation->length) == NULL)
    qa_range_error("BSP string has no terminator within its allocation");
  return (const char *)allocation->bytes;
}
static void free_cell(qa_aas_bsp_entities *e, uint32_t pointer) {
  qa_bot_memory_allocation allocation = *cell(e, pointer);
  qa_bot_memory_free(e->memory, &allocation);
}
static void free_entities(qa_aas_bsp_entities *e) {
  for (int64_t index = 1; index < e->entity_count; index++) {
    for (uint32_t pair = e->pairs[index]; pair != 0;) {
      uint32_t next = pointer_at(e, pair, 8), key = pointer_at(e, pair, 0), value = pointer_at(e, pair, 4);
      if (key != 0) free_cell(e, key);
      if (value != 0) free_cell(e, value);
      free_cell(e, pair);
      pair = next;
    }
  }
  e->entity_count = 0;
}
void qa_aas_bsp_entities_dump(qa_aas_bsp_entities *e) {
  free_entities(e);
  if (e->has_entity_text) qa_bot_memory_free(e->memory, &e->entity_text);
  e->has_entity_text = false;
  e->is_loaded = false;
  memset(e->pairs, 0, sizeof e->pairs);
}
static void print_location(qa_aas_bsp_entities *e, qa_ui_script_lexer *lexer, const char *message) {
  const char *path;
  int64_t line, column;
  qa_ui_script_lexer_current_location(lexer, &path, &line, &column);
  char *text = lib_format("file %s, line %lld: %s\n", path, (long long)line, message);
  print_text(e, 3, text);
  free(text);
}
/* TS read(): NULL for end of script or a printed ScriptLanguageError. */
static qa_ui_script_token *read_token(qa_aas_bsp_entities *e, qa_ui_script_lexer *lexer) {
  qa_ui_script_fail_env *env = qa_ui_script_lexer_fail_env(lexer);
  qa_ui_script_catch block;
  block.prev = env->top;
  env->top = &block;
  qa_ui_script_token *token = NULL;
  if (setjmp(block.buf) == 0) {
    bool read = qa_ui_script_lexer_next(lexer, &token);
    env->top = block.prev;
    return read ? token : NULL;
  }
  env->top = block.prev;
  size_t count = 0;
  const qa_ui_script_diagnostic *issues = qa_ui_script_lexer_diagnostics(lexer, &count);
  for (size_t i = 0; i < count; i++) {
    char *text = lib_format("file %s, line %lld: %s\n", issues[i].location.path,
                            (long long)issues[i].location.line, issues[i].message);
    print_text(e, issues[i].severity == QA_UI_SCRIPT_SEVERITY_ERROR ? 3 : 2, text);
    free(text);
  }
  qa_ui_script_fail_env_clear_pending(env);
  return NULL;
}
static void store_string(qa_aas_bsp_entities *e, uint32_t pointer, const char *value, size_t length) {
  uint8_t *bytes = cell(e, pointer)->bytes;
  memcpy(bytes, value, length);
  bytes[length] = 0;
}
static void parse(qa_aas_bsp_entities *e, qa_ui_script_lexer *lexer) {
  e->entity_count = 1;
  while (true) {
    qa_ui_script_token *opening = read_token(e, lexer);
    if (opening == NULL) return;
    if (!lib_token_is(opening, "{")) {
      char *message = lib_format("invalid %s\n", opening->text);
      print_location(e, lexer, message);
      free(message);
      qa_ui_script_token_release(opening);
      free_entities(e);
      return;
    }
    qa_ui_script_token_release(opening);
    if (e->entity_count >= QA_BSP_MAX_ENTITIES) {
      print_text(e, 1, "too many entities in BSP file\n");
      return;
    }
    int64_t entity = e->entity_count++;
    e->pairs[entity] = 0;
    while (true) {
      qa_ui_script_token *key = read_token(e, lexer);
      if (key == NULL) {
        print_location(e, lexer, "missing }\n");
        free_entities(e);
        return;
      }
      if (lib_token_is(key, "}")) {
        qa_ui_script_token_release(key);
        break;
      }
      uint32_t pair = (uint32_t)allocate_cell(e, 12, true);
      set_pointer(e, pair, 8, e->pairs[entity]);
      e->pairs[entity] = pair;
      if (key->kind != QA_UI_SCRIPT_KIND_STRING) {
        char *message = lib_format("invalid %s\n", key->text);
        print_location(e, lexer, message);
        free(message);
        qa_ui_script_token_release(key);
        free_entities(e);
        return;
      }
      /* Managed quote removal corrects source StripDoubleQuotes' overlapping strcpy. */
      uint32_t key_pointer = (uint32_t)allocate_cell(e, (int64_t)key->value_length + 1, false);
      set_pointer(e, pair, 0, key_pointer);
      store_string(e, key_pointer, key->value, key->value_length);
      qa_ui_script_token_release(key);
      qa_ui_script_token *value = read_token(e, lexer);
      if (value == NULL || value->kind != QA_UI_SCRIPT_KIND_STRING) {
        char *message = value == NULL ? lib_strdup("couldn't read expected token")
                                      : lib_format("expected a string, found %s", value->text);
        print_location(e, lexer, message);
        free(message);
        if (value != NULL) qa_ui_script_token_release(value);
        free_entities(e);
        return;
      }
      uint32_t value_pointer = (uint32_t)allocate_cell(e, (int64_t)value->value_length + 1, false);
      set_pointer(e, pair, 4, value_pointer);
      store_string(e, value_pointer, value->value, value->value_length);
      qa_ui_script_token_release(value);
    }
  }
}
int qa_aas_bsp_entities_load(qa_aas_bsp_entities *e, const char *text) {
  qa_aas_bsp_entities_dump(e);
  size_t length = strlen(text);
  e->entity_text = lib_mem_alloc(e->memory, (int64_t)length + 1, QA_BOT_MEMORY_HUNK, true);
  e->has_entity_text = true;
  memcpy(e->entity_text.bytes, text, length + 1);
  qa_ui_script_memory memory = lib_script_memory(e->memory);
  qa_ui_script_lexer_options options;
  memset(&options, 0, sizeof options);
  options.has_flags = true;
  options.flags = QA_UI_SCRIPT_LEXER_NO_STRING_CONCATENATION | QA_UI_SCRIPT_LEXER_NO_STRING_ESCAPES;
  options.memory = &memory;
  /* TS passes `${text}\0`: the source terminator is part of the lexed bytes. */
  qa_ui_script_lexer *lexer =
      qa_ui_script_lexer_create_text(e->entity_text.bytes, length + 1, "entdata", &options);
  if (lexer == NULL) qa_range_error("out of memory");
  parse(e, lexer);
  qa_ui_script_lexer_dispose(lexer);
  qa_ui_script_lexer_destroy(lexer);
  e->is_loaded = true;
  return 0;
}
int64_t qa_aas_bsp_entities_next_entity(const qa_aas_bsp_entities *e, int64_t after) {
  check_int32(after);
  if (after == 2147483647) qa_range_error("AAS_NextBSPEntity would overflow signed int");
  int64_t next = after + 1;
  return next < 1 || next >= e->entity_count ? 0 : next;
}
bool qa_aas_bsp_entities_value_ex(qa_aas_bsp_entities *e, int64_t entity, const char *key_text,
                                  const qa_aas_bsp_key *key, uint8_t *output, size_t output_length,
                                  int64_t size, const qa_aas_bsp_writes *writes) {
  check_int32(entity);
  if (output_length == 0) qa_range_error("AAS_ValueForBSPEpairKey requires its first output byte");
  if (writes == NULL) output[0] = 0;
  else writes->set_byte(writes->ctx, 0, 0);
  if (entity <= 0 || entity >= e->entity_count) {
    print_text(e, 1, "bsp entity out of range\n");
    return false;
  }
  for (uint32_t pair = e->pairs[entity]; pair != 0; pair = pointer_at(e, pair, 8)) {
    uint32_t key_pointer = pointer_at(e, pair, 0);
    if (key_pointer == 0)
      qa_range_error("BSP epair lookup would consume an uninitialized source key");
    const char *candidate = read_string(cell(e, key_pointer));
    if (!(key_text != NULL ? strcmp(candidate, key_text) == 0 : key->matches(key->ctx, candidate)))
      continue;
    uint32_t value_pointer = pointer_at(e, pair, 4);
    if (value_pointer == 0)
      qa_range_error("BSP epair lookup would consume an uninitialized source value");
    if (size < 1 || (uint64_t)size > output_length)
      qa_range_error("AAS_ValueForBSPEpairKey matched output exceeds its allocation or has an invalid size");
    if (writes == NULL) memset(output, 0, (size_t)size);
    else writes->clear(writes->ctx, size);
    const char *value = read_string(cell(e, value_pointer));
    size_t length = strlen(value);
    if (length > (size_t)size - 1) length = (size_t)size - 1;
    for (size_t index = 0; index < length; index++) {
      if (writes == NULL) output[index] = (uint8_t)value[index];
      else writes->set_byte(writes->ctx, (int64_t)index, (uint8_t)value[index]);
    }
    return true;
  }
  return false;
}
bool qa_aas_bsp_entities_value(qa_aas_bsp_entities *e, int64_t entity, const char *key,
                               uint8_t *output, size_t output_length) {
  return qa_aas_bsp_entities_value_ex(e, entity, key, NULL, output, output_length,
                                      (int64_t)output_length, NULL);
}
static bool numeric_text(qa_aas_bsp_entities *e, int64_t entity, const char *key_text,
                         const qa_aas_bsp_key *key, char *buffer) {
  return qa_aas_bsp_entities_value_ex(e, entity, key_text, key, (uint8_t *)buffer,
                                      QA_BSP_MAX_EPAIRKEY, QA_BSP_MAX_EPAIRKEY, NULL);
}
static bool is_space(char c) { return c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r' || c == ' '; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_hex(char c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
/* C-locale glibc 2.44 %lf consumption, followed by native atof conversion.
 * Unlike atof, an incomplete exponent/infinity/payload fails this conversion. */
static bool scan_float(const char *text, size_t start, double *out_value, size_t *out_end) {
  size_t cursor = start;
  while (text[cursor] != '\0' && is_space(text[cursor])) cursor++;
  size_t beginning = cursor;
  if (text[cursor] == '+' || text[cursor] == '-') cursor++;
  const char *word = text + cursor;
  if (strncasecmp(word, "inf", 3) == 0) {
    if (word[3] == 'i' || word[3] == 'I') {
      if (strncasecmp(word, "infinity", 8) != 0) return false;
      cursor += 8;
    } else {
      cursor += 3;
    }
  } else if (strncasecmp(word, "nan", 3) == 0) {
    cursor += 3;
    if (text[cursor] == '(') {
      cursor++;
      while (isalnum((unsigned char)text[cursor]) || text[cursor] == '_') cursor++;
      if (text[cursor] != ')') return false;
      cursor++;
    }
  } else {
    bool hexadecimal = strncasecmp(word, "0x", 2) == 0;
    if (hexadecimal) cursor += 2;
    size_t digits = 0;
    while (hexadecimal ? is_hex(text[cursor]) : is_digit(text[cursor])) {
      cursor++;
      digits++;
    }
    if (text[cursor] == '.') {
      cursor++;
      while (hexadecimal ? is_hex(text[cursor]) : is_digit(text[cursor])) {
        cursor++;
        digits++;
      }
    }
    if (digits == 0) return false;
    char exponent = (char)tolower((unsigned char)text[cursor]);
    if (exponent != '\0' && exponent == (hexadecimal ? 'p' : 'e')) {
      cursor++;
      if (text[cursor] == '+' || text[cursor] == '-') cursor++;
      size_t exponent_start = cursor;
      while (is_digit(text[cursor])) cursor++;
      if (cursor == exponent_start) return false;
    }
  }
  char *slice = lib_strndup(text + beginning, cursor - beginning);
  *out_value = qa_native_atof(slice);
  free(slice);
  *out_end = cursor;
  return true;
}
static qa_aas_bsp_vector_result vector_of(qa_aas_bsp_entities *e, int64_t entity, const char *key_text,
                                          const qa_aas_bsp_key *key) {
  char text[QA_BSP_MAX_EPAIRKEY];
  qa_aas_bsp_vector_result result = {false, {0, 0, 0}};
  if (!numeric_text(e, entity, key_text, key, text)) return result;
  result.found = true;
  double *axes[3] = {&result.value.x, &result.value.y, &result.value.z};
  size_t cursor = 0;
  for (int axis = 0; axis < 3; axis++) {
    double value;
    size_t end;
    if (!scan_float(text, cursor, &value, &end)) break;
    *axes[axis] = (float)value;
    cursor = end;
  }
  return result;
}
static qa_aas_bsp_number_result float_of(qa_aas_bsp_entities *e, int64_t entity, const char *key_text,
                                         const qa_aas_bsp_key *key) {
  char text[QA_BSP_MAX_EPAIRKEY];
  if (!numeric_text(e, entity, key_text, key, text)) return (qa_aas_bsp_number_result){false, 0};
  return (qa_aas_bsp_number_result){true, (float)qa_native_atof(text)};
}
static qa_aas_bsp_number_result int_of(qa_aas_bsp_entities *e, int64_t entity, const char *key_text,
                                       const qa_aas_bsp_key *key) {
  char text[QA_BSP_MAX_EPAIRKEY];
  if (!numeric_text(e, entity, key_text, key, text)) return (qa_aas_bsp_number_result){false, 0};
  const char *cursor = text;
  while (*cursor != '\0' && is_space(*cursor)) cursor++;
  bool negative = *cursor == '-';
  if (*cursor == '+' || *cursor == '-') cursor++;
  if (is_digit(*cursor)) {
    uint64_t limit = negative ? 2147483648ULL : 2147483647ULL, magnitude = 0;
    for (; is_digit(*cursor); cursor++) {
      magnitude = magnitude * 10 + (uint64_t)(*cursor - '0');
      /* Map text is untrusted and atoi has no failure path; an oversized value saturates. */
      if (magnitude > limit)
        return (qa_aas_bsp_number_result){true, negative ? -2147483648.0 : 2147483647.0};
    }
  }
  return (qa_aas_bsp_number_result){true, qa_native_atoi(text)};
}
qa_aas_bsp_vector_result qa_aas_bsp_entities_vector(qa_aas_bsp_entities *e, int64_t n, const char *k) { return vector_of(e, n, k, NULL); }
qa_aas_bsp_number_result qa_aas_bsp_entities_float(qa_aas_bsp_entities *e, int64_t n, const char *k) { return float_of(e, n, k, NULL); }
qa_aas_bsp_number_result qa_aas_bsp_entities_int(qa_aas_bsp_entities *e, int64_t n, const char *k) { return int_of(e, n, k, NULL); }
qa_aas_bsp_vector_result qa_aas_bsp_entities_vector_key(qa_aas_bsp_entities *e, int64_t n, const qa_aas_bsp_key *k) { return vector_of(e, n, NULL, k); }
qa_aas_bsp_number_result qa_aas_bsp_entities_float_key(qa_aas_bsp_entities *e, int64_t n, const qa_aas_bsp_key *k) { return float_of(e, n, NULL, k); }
qa_aas_bsp_number_result qa_aas_bsp_entities_int_key(qa_aas_bsp_entities *e, int64_t n, const qa_aas_bsp_key *k) { return int_of(e, n, NULL, k); }
qa_save_value *qa_aas_bsp_entities_checkpoint(qa_aas_bsp_entities *e, qa_bot_memory_capture *memory) {
  qa_save_value *object = lib_sv(qa_save_value_object());
  lib_set_num(object, "count", (double)e->entity_count);
  lib_set(object, "loaded", qa_save_value_boolean(e->is_loaded));
  lib_set_num(object, "nextAllocation", (double)e->next_allocation);
  lib_set(object, "entityText",
          e->has_entity_text
              ? qa_save_value_number((double)qa_bot_memory_capture_reference(memory, &e->entity_text))
              : qa_save_value_null());
  qa_save_value *entities = lib_sv(qa_save_value_array());
  for (int i = 0; i < QA_BSP_MAX_ENTITIES; i++) lib_push(entities, qa_save_value_number(e->pairs[i]));
  lib_set(object, "entities", entities);
  qa_save_value *allocations = lib_sv(qa_save_value_array());
  for (size_t pointer = 0; pointer < e->allocation_capacity; pointer++) {
    if (!e->allocations[pointer].live) continue;
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set_num(entry, "pointer", (double)pointer);
    lib_set_num(entry, "allocation",
                (double)qa_bot_memory_capture_reference(memory, &e->allocations[pointer].allocation));
    lib_push(allocations, entry);
  }
  lib_set(object, "allocations", allocations);
  return object;
}
void qa_aas_bsp_entities_restore(qa_aas_bsp_entities *e, const qa_save_value *value,
                                 qa_bot_memory_restored *memory) {
  qa_save_reader reader, entity_text, entities, allocations;
  if (!qa_save_reader_init(&reader, value, "bot.bsp")) qa_range_error("out of memory");
  double count = lib_int_min(&reader, "count", 0);
  bool loaded = lib_read_bool(&reader, "loaded");
  double next = lib_int_min(&reader, "nextAllocation", 1);
  lib_field(&reader, "entityText", &entity_text);
  lib_field(&reader, "entities", &entities);
  lib_field(&reader, "allocations", &allocations);
  bool any = false;
  for (size_t i = 0; i < e->allocation_capacity && !any; i++) any = e->allocations[i].live;
  if (e->is_loaded || any || count < 0 || count > QA_BSP_MAX_ENTITIES ||
      qa_save_reader_list_count(&entities) != QA_BSP_MAX_ENTITIES || next < 1 || next > 4294967296.0)
    qa_ui_script_plain_error("Invalid bot BSP entity restoration");
  size_t allocation_count = qa_save_reader_list_count(&allocations);
  for (size_t i = 0; i < allocation_count; i++) {
    qa_save_reader entry;
    lib_item(&allocations, i, &entry);
    double pointer = lib_int_min(&entry, "pointer", 1);
    double reference = lib_int_min(&entry, "allocation", 0);
    qa_save_reader_destroy(&entry);
    if (pointer < 1 || pointer >= next ||
        ((size_t)pointer < e->allocation_capacity && e->allocations[(size_t)pointer].live))
      qa_ui_script_plain_error("Invalid saved BSP allocation pointer");
    if ((size_t)pointer >= e->allocation_capacity) {
      size_t grown = lib_grow(e->allocation_capacity, (size_t)pointer + 1);
      e->allocations = lib_realloc(e->allocations, grown * sizeof *e->allocations);
      memset(e->allocations + e->allocation_capacity, 0,
             (grown - e->allocation_capacity) * sizeof *e->allocations);
      e->allocation_capacity = grown;
    }
    e->allocations[(size_t)pointer] =
        (qa_aas_bsp_allocation_cell){true, qa_bot_memory_restored_allocation(memory, (int64_t)reference)};
  }
  for (size_t index = 0; index < QA_BSP_MAX_ENTITIES; index++) {
    qa_save_reader entry;
    lib_item(&entities, index, &entry);
    double pair = qa_save_reader_integer_min(&entry, 0);
    qa_save_reader_destroy(&entry);
    size_t seen_count = 0, seen_capacity = 0;
    uint32_t *seen = NULL;
    for (uint32_t pointer = (uint32_t)pair; pointer != 0; pointer = pointer_at(e, pointer, 8)) {
      for (size_t i = 0; i < seen_count; i++)
        if (seen[i] == pointer) qa_ui_script_plain_error("Invalid saved BSP epair list");
      if (cell(e, pointer)->length != 12) qa_ui_script_plain_error("Invalid saved BSP epair list");
      if (seen_count == seen_capacity) {
        seen_capacity = lib_grow(seen_capacity, seen_count + 1);
        seen = lib_realloc(seen, seen_capacity * sizeof *seen);
      }
      seen[seen_count++] = pointer;
      for (size_t offset = 0; offset <= 4; offset += 4) {
        qa_bot_memory_allocation *string = cell(e, pointer_at(e, pointer, offset));
        if (memchr(string->bytes, 0, string->length) == NULL)
          qa_ui_script_plain_error("Saved BSP epair string is unterminated");
      }
    }
    free(seen);
    e->pairs[index] = (uint32_t)pair;
  }
  if (qa_save_reader_is_null(&entity_text)) {
    e->has_entity_text = false;
  } else {
    double reference = qa_save_reader_integer_min(&entity_text, 0);
    e->entity_text = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
    e->has_entity_text = true;
  }
  e->entity_count = (int64_t)count;
  e->next_allocation = (int64_t)next;
  e->is_loaded = loaded;
  qa_save_reader_destroy(&entity_text);
  qa_save_reader_destroy(&entities);
  qa_save_reader_destroy(&allocations);
  qa_save_reader_destroy(&reader);
}
