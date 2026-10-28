/* See include/qa/ui/common/legacy/script/source_memory.h.
 * GPL-2.0-or-later. */
#include "qa/ui/common/legacy/script/source_memory.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

enum {
  INDENT_BYTES = 16,
  PATH_BYTES = 64,
  OFF_FILENAME = 0,
  OFF_INCLUDE_PATH = 1024,
  OFF_PUNCTUATIONS = 2048,
  OFF_SCRIPT_STACK = 2052,
  OFF_TOKENS = 2056,
  OFF_DEFINE_HASH = 2064,
  OFF_INDENT_STACK = 2068,
  OFF_SKIP = 2072,
  OFF_TOKEN = 2076
};

typedef struct source_indent {
  int64_t id;
  bool heap;
  qa_ui_script_allocation allocation; /* heap backing */
  /* managed backing */
  int64_t type, skip, script, next;
} source_indent;

struct qa_ui_source_record {
  bool heap; /* record backing */
  qa_ui_script_allocation allocation;
  /* managed backing */
  char *filename;
  char *include_path;
  uint32_t m_script, m_tokens, m_indent;
  int32_t m_skip;
  /* hash backing */
  bool hash_heap;
  qa_ui_script_allocation hash_allocation;
  uint32_t heads[QA_UI_SOURCE_DEFINE_HASH_BUCKETS];
  bool hash_freed;
  bool record_freed;
  const qa_ui_script_memory *memory;
  bool has_memory;
  qa_ui_precomp_token *token;
  source_indent **indents; /* index id-1 */
  size_t indent_count, indent_cap;
  int64_t next_indent;
  qa_ui_script_punctuation_spec *punctuations; /* shallow; text owned here */
  char **punctuation_texts;
  size_t punctuation_count;
  bool has_punctuations;
};

static uint32_t rd_u32(const uint8_t *bytes, size_t offset) {
  uint32_t value;
  memcpy(&value, bytes + offset, sizeof value);
  return value;
}
static void wr_u32(uint8_t *bytes, size_t offset, uint32_t value) {
  memcpy(bytes + offset, &value, sizeof value);
}
static char *copy_cstring(const char *text) {
  size_t length = strlen(text);
  char *result = malloc(length + 1);
  if (result != NULL) memcpy(result, text, length + 1);
  return result;
}

static int64_t indent_get(source_indent *indent, size_t offset,
                          int64_t managed) {
  return indent->heap ? (int32_t)rd_u32(indent->allocation.bytes, offset)
                      : managed;
}
static uint32_t indent_get_u(source_indent *indent, size_t offset,
                             int64_t managed) {
  return indent->heap ? rd_u32(indent->allocation.bytes, offset)
                      : (uint32_t)managed;
}
static qa_ui_source_indent_type indent_type(source_indent *indent) {
  int64_t value = indent_get(indent, 0, indent->type);
  switch (value) {
    case QA_UI_SOURCE_INDENT_IF:
    case QA_UI_SOURCE_INDENT_ELSE:
    case QA_UI_SOURCE_INDENT_ELIF:
    case QA_UI_SOURCE_INDENT_IFDEF:
    case QA_UI_SOURCE_INDENT_IFNDEF:
      return (qa_ui_source_indent_type)value;
    default:
      qa_range_error("source indent has an unsupported directive type");
  }
}
static void indent_set_type(source_indent *indent, int64_t value) {
  if (indent->heap)
    wr_u32(indent->allocation.bytes, 0, (uint32_t)(int32_t)value);
  else indent->type = value;
}
static int64_t indent_skip(source_indent *indent) {
  return indent_get(indent, 4, indent->skip);
}
static void indent_set_skip(source_indent *indent, int64_t value) {
  if (indent->heap)
    wr_u32(indent->allocation.bytes, 4, (uint32_t)(int32_t)value);
  else indent->skip = (int32_t)value;
}
static uint32_t indent_script(source_indent *indent) {
  return indent_get_u(indent, 8, indent->script);
}
static void indent_set_script(source_indent *indent, uint32_t value) {
  if (indent->heap) wr_u32(indent->allocation.bytes, 8, value);
  else indent->script = value;
}
static uint32_t indent_next(source_indent *indent) {
  return indent_get_u(indent, 12, indent->next);
}
static void indent_set_next(source_indent *indent, uint32_t value) {
  if (indent->heap) wr_u32(indent->allocation.bytes, 12, value);
  else indent->next = value;
}

static void copy_path(qa_ui_source_record *record, size_t offset,
                      const char *path) {
  uint8_t *bytes = record->allocation.bytes;
  bool ended = false;
  size_t length = strlen(path);
  for (size_t index = 0; index < PATH_BYTES; index++) {
    unsigned int character =
        ended || index >= length ? 0 : (uint8_t)path[index];
    bytes[offset + index] = (uint8_t)character;
    if (character == 0) ended = true;
  }
}

static char *read_path(qa_ui_source_record *record, size_t offset) {
  uint8_t *bytes = record->allocation.bytes;
  for (size_t index = 0; index < 1024; index++) {
    if (offset + index >= record->allocation.length)
      qa_range_error("source path is outside its allocation");
    if (bytes[offset + index] == 0) {
      char *result = malloc(index + 1);
      if (result == NULL) return NULL;
      memcpy(result, bytes + offset, index);
      result[index] = '\0';
      return result;
    }
  }
  qa_range_error("source path lacks its terminator");
}

static qa_ui_precomp_token *record_token_for(
    const qa_ui_script_allocation *allocation) {
  qa_ui_script_allocation view;
  view.bytes = allocation->bytes + OFF_TOKEN;
  view.length = QA_UI_SOURCE_RECORD_BYTES - OFF_TOKEN;
  view.record = NULL;
  return qa_ui_precomp_token_create(0, view);
}

qa_ui_source_record *qa_ui_source_record_create(
    const char *filename, int64_t script,
    const qa_ui_script_memory *memory, bool has_memory,
    qa_ui_source_lifetime lifetime) {
  qa_ui_source_record *record = calloc(1, sizeof *record);
  if (record == NULL) return NULL;
  record->memory = memory;
  record->has_memory = has_memory;
  record->next_indent = 1;
  if (has_memory && lifetime == QA_UI_SOURCE_LIFETIME_HEAP) {
    if (!qa_ui_script_memory_allocate(memory, QA_UI_SOURCE_RECORD_BYTES,
                                      false, &record->allocation))
      goto oom;
    memset(record->allocation.bytes, 0, QA_UI_SOURCE_RECORD_BYTES);
    record->heap = true;
    record->token = record_token_for(&record->allocation);
    if (record->token == NULL) goto oom;
    copy_path(record, OFF_FILENAME, filename);
    qa_ui_source_record_set_script(record, (uint32_t)script);
  } else {
    record->filename = copy_cstring(filename);
    record->include_path = copy_cstring("");
    record->m_script = (uint32_t)script;
    record->token = qa_ui_precomp_token_local();
    if (record->filename == NULL || record->include_path == NULL
        || record->token == NULL)
      goto oom;
  }
  if (!has_memory) {
    record->hash_heap = false;
  } else {
    record->hash_heap = true;
    if (!qa_ui_script_memory_allocate(
            memory, QA_UI_SOURCE_DEFINE_HASH_BUCKETS * 4, true,
            &record->hash_allocation))
      goto oom;
    if (record->heap)
      wr_u32(record->allocation.bytes, OFF_DEFINE_HASH, 1);
  }
  return record;
oom:
  qa_ui_source_record_destroy(record);
  return NULL;
}

void qa_ui_source_record_destroy(qa_ui_source_record *record) {
  if (record == NULL) return;
  for (size_t index = 0; index < record->indent_count; index++) {
    source_indent *indent = record->indents[index];
    if (indent != NULL) {
      if (indent->heap)
        qa_ui_script_memory_free(record->memory, &indent->allocation);
      free(indent);
    }
  }
  free(record->indents);
  qa_ui_precomp_token_destroy(record->token);
  if (record->hash_heap && !record->hash_freed)
    qa_ui_script_memory_free(record->memory, &record->hash_allocation);
  if (record->heap && !record->record_freed && record->has_memory)
    qa_ui_script_memory_free(record->memory, &record->allocation);
  free(record->filename);
  free(record->include_path);
  for (size_t index = 0; index < record->punctuation_count; index++)
    free(record->punctuation_texts[index]);
  free(record->punctuation_texts);
  free(record->punctuations);
  free(record);
}

qa_ui_precomp_token *qa_ui_source_record_token(qa_ui_source_record *record) {
  return record->token;
}

char *qa_ui_source_record_filename(qa_ui_source_record *record) {
  return record->heap ? read_path(record, OFF_FILENAME)
                      : copy_cstring(record->filename);
}

void qa_ui_source_record_set_punctuations(
    qa_ui_source_record *record,
    const qa_ui_script_punctuation_spec *specs, size_t count) {
  if (record->heap)
    wr_u32(record->allocation.bytes, OFF_PUNCTUATIONS,
           specs == NULL ? 0 : 1);
  for (size_t index = 0; index < record->punctuation_count; index++)
    free(record->punctuation_texts[index]);
  free(record->punctuation_texts);
  free(record->punctuations);
  record->punctuations = NULL;
  record->punctuation_texts = NULL;
  record->punctuation_count = 0;
  record->has_punctuations = specs != NULL;
  if (specs == NULL) return;
  record->punctuations = calloc(count == 0 ? 1 : count,
                                sizeof *record->punctuations);
  record->punctuation_texts =
      calloc(count == 0 ? 1 : count, sizeof *record->punctuation_texts);
  if (record->punctuations == NULL || record->punctuation_texts == NULL)
    qa_range_error("source record allocation failed");
  record->punctuation_count = count;
  for (size_t index = 0; index < count; index++) {
    record->punctuation_texts[index] = copy_cstring(specs[index].text);
    if (record->punctuation_texts[index] == NULL)
      qa_range_error("source record allocation failed");
    record->punctuations[index].text = record->punctuation_texts[index];
    record->punctuations[index].punctuation = specs[index].punctuation;
  }
}

const qa_ui_script_punctuation_spec *qa_ui_source_record_punctuations(
    qa_ui_source_record *record, size_t *out_count) {
  if (!record->heap) {
    *out_count = record->has_punctuations ? record->punctuation_count : 0;
    return record->has_punctuations ? record->punctuations : NULL;
  }
  uint32_t pointer = rd_u32(record->allocation.bytes, OFF_PUNCTUATIONS);
  if (pointer == 0) {
    *out_count = 0;
    return NULL;
  }
  if (pointer != 1 || !record->has_punctuations)
    qa_range_error("source punctuation pointer does not identify its borrowed table");
  *out_count = record->punctuation_count;
  return record->punctuations;
}

uint32_t qa_ui_source_record_tokens(qa_ui_source_record *record) {
  return record->heap ? rd_u32(record->allocation.bytes, OFF_TOKENS)
                      : record->m_tokens;
}
void qa_ui_source_record_set_tokens(qa_ui_source_record *record,
                                    uint32_t value) {
  if (record->heap) wr_u32(record->allocation.bytes, OFF_TOKENS, value);
  else record->m_tokens = value;
}

char *qa_ui_source_record_include_path(qa_ui_source_record *record) {
  return record->heap ? read_path(record, OFF_INCLUDE_PATH)
                      : copy_cstring(record->include_path);
}

void qa_ui_source_record_set_include_path(qa_ui_source_record *record,
                                          const char *path) {
  if (!record->heap) {
    size_t length = strlen(path);
    if (length > PATH_BYTES) length = PATH_BYTES;
    bool slash = length > 0
                 && (path[length - 1] == '/' || path[length - 1] == '\\');
    char *value = malloc(length + (slash ? 1 : 2));
    if (value == NULL) qa_range_error("source record allocation failed");
    memcpy(value, path, length);
    if (!slash) value[length++] = '/';
    value[length] = '\0';
    free(record->include_path);
    record->include_path = value;
    return;
  }
  copy_path(record, OFF_INCLUDE_PATH, path);
  char *value = qa_ui_source_record_include_path(record);
  if (value == NULL) qa_range_error("source record allocation failed");
  size_t length = strlen(value);
  if (!(length > 0 && (value[length - 1] == '/' || value[length - 1] == '\\'))) {
    if (length + 1 >= 1024) {
      free(value);
      qa_range_error("source include path exceeds its allocation");
    }
    record->allocation.bytes[OFF_INCLUDE_PATH + length] = 47;
    record->allocation.bytes[OFF_INCLUDE_PATH + length + 1] = 0;
  }
  free(value);
}

uint32_t qa_ui_source_record_script(qa_ui_source_record *record) {
  return record->heap ? rd_u32(record->allocation.bytes, OFF_SCRIPT_STACK)
                      : record->m_script;
}
void qa_ui_source_record_set_script(qa_ui_source_record *record,
                                    uint32_t value) {
  if (record->heap) wr_u32(record->allocation.bytes, OFF_SCRIPT_STACK, value);
  else record->m_script = value;
}
int32_t qa_ui_source_record_skip(qa_ui_source_record *record) {
  return record->heap
             ? (int32_t)rd_u32(record->allocation.bytes, OFF_SKIP)
             : record->m_skip;
}
void qa_ui_source_record_set_skip(qa_ui_source_record *record,
                                  int32_t value) {
  if (record->heap)
    wr_u32(record->allocation.bytes, OFF_SKIP, (uint32_t)value);
  else record->m_skip = value;
}

static void check_hash_pointer(qa_ui_source_record *record) {
  if (record->heap
      && rd_u32(record->allocation.bytes, OFF_DEFINE_HASH) != 1)
    qa_range_error("source define hash pointer does not identify its allocation");
}

uint32_t qa_ui_source_record_hash_head(qa_ui_source_record *record,
                                       int64_t bucket) {
  if (!record->hash_heap) {
    if (bucket < 0 || bucket >= QA_UI_SOURCE_DEFINE_HASH_BUCKETS) return 0;
    return record->heads[bucket];
  }
  check_hash_pointer(record);
  return rd_u32(record->hash_allocation.bytes, (size_t)bucket * 4);
}
void qa_ui_source_record_set_hash_head(qa_ui_source_record *record,
                                       int64_t bucket, uint32_t id) {
  if (!record->hash_heap) {
    if (bucket >= 0 && bucket < QA_UI_SOURCE_DEFINE_HASH_BUCKETS)
      record->heads[bucket] = id;
    return;
  }
  check_hash_pointer(record);
  wr_u32(record->hash_allocation.bytes, (size_t)bucket * 4, id);
}

static uint32_t get_indent_head(qa_ui_source_record *record) {
  return record->heap ? rd_u32(record->allocation.bytes, OFF_INDENT_STACK)
                      : record->m_indent;
}
static void set_indent_head(qa_ui_source_record *record, uint32_t value) {
  if (record->heap)
    wr_u32(record->allocation.bytes, OFF_INDENT_STACK, value);
  else record->m_indent = value;
}
static source_indent *top_indent(qa_ui_source_record *record) {
  uint32_t id = get_indent_head(record);
  if (id == 0) return NULL;
  if (id > record->indent_count || record->indents[id - 1] == NULL)
    qa_range_error("source indent pointer does not identify a live allocation");
  return record->indents[id - 1];
}

bool qa_ui_source_record_has_current_indent(qa_ui_source_record *record) {
  source_indent *indent = top_indent(record);
  return indent != NULL
         && indent_script(indent) == qa_ui_source_record_script(record);
}

static bool grow_indents(qa_ui_source_record *record, size_t need) {
  if (need <= record->indent_cap) return true;
  size_t cap = record->indent_cap == 0 ? 4 : record->indent_cap;
  while (cap < need) cap *= 2;
  source_indent **grown = realloc(record->indents, cap * sizeof *grown);
  if (grown == NULL) return false;
  for (size_t index = record->indent_cap; index < cap; index++)
    grown[index] = NULL;
  record->indents = grown;
  record->indent_cap = cap;
  return true;
}

void qa_ui_source_record_push_indent(qa_ui_source_record *record,
                                     qa_ui_source_indent_type type,
                                     bool skip) {
  source_indent *indent = calloc(1, sizeof *indent);
  if (indent == NULL) qa_range_error("source record allocation failed");
  indent->id = record->next_indent++;
  indent->heap = record->has_memory;
  if (indent->heap
      && !qa_ui_script_memory_allocate(record->memory, INDENT_BYTES, false,
                                       &indent->allocation))
    qa_range_error("source record allocation failed");
  if (!grow_indents(record, (size_t)indent->id))
    qa_range_error("source record allocation failed");
  record->indents[indent->id - 1] = indent;
  if ((size_t)indent->id > record->indent_count)
    record->indent_count = (size_t)indent->id;
  indent_set_type(indent, type);
  indent_set_script(indent, qa_ui_source_record_script(record));
  indent_set_skip(indent, skip ? 1 : 0);
  qa_ui_source_record_set_skip(
      record, qa_ui_source_record_skip(record) + (int32_t)indent_skip(indent));
  indent_set_next(indent, get_indent_head(record));
  set_indent_head(record, (uint32_t)indent->id);
}

static void free_indent(qa_ui_source_record *record, source_indent *indent) {
  record->indents[indent->id - 1] = NULL;
  if (indent->heap)
    qa_ui_script_memory_free(record->memory, &indent->allocation);
  free(indent);
}

bool qa_ui_source_record_pop_indent(qa_ui_source_record *record,
                                    qa_ui_source_indent_type *out_type,
                                    int64_t *out_skip) {
  source_indent *indent = top_indent(record);
  if (indent == NULL
      || indent_script(indent) != qa_ui_source_record_script(record))
    return false;
  *out_type = indent_type(indent);
  *out_skip = indent_skip(indent);
  set_indent_head(record, indent_next(indent));
  qa_ui_source_record_set_skip(
      record,
      qa_ui_source_record_skip(record) - (int32_t)indent_skip(indent));
  free_indent(record, indent);
  return true;
}

void qa_ui_source_record_free_indents(qa_ui_source_record *record) {
  source_indent *indent;
  while ((indent = top_indent(record)) != NULL) {
    set_indent_head(record, indent_next(indent));
    free_indent(record, indent);
  }
}

void qa_ui_source_record_free_hash(qa_ui_source_record *record) {
  if (!record->hash_heap || record->hash_freed) return;
  if (record->heap) {
    uint32_t pointer = rd_u32(record->allocation.bytes, OFF_DEFINE_HASH);
    if (pointer == 0) return;
    if (pointer != 1)
      qa_range_error("source define hash pointer does not identify its allocation");
  }
  qa_ui_script_memory_free(record->memory, &record->hash_allocation);
  record->hash_freed = true;
}

void qa_ui_source_record_free_record(qa_ui_source_record *record) {
  if (record->heap && record->has_memory && !record->record_freed) {
    qa_ui_script_memory_free(record->memory, &record->allocation);
    record->record_freed = true;
  }
}

/* ---- checkpoint ---- */

static bool set_num(qa_save_value *object, const char *key, double value) {
  return qa_save_value_set(object, key, qa_save_value_number(value));
}
static bool set_str(qa_save_value *object, const char *key,
                    const char *value) {
  return qa_save_value_set(object, key, qa_save_value_string(value));
}

static qa_save_value *indent_capture(
    source_indent *indent, const qa_ui_script_memory_capture *capture) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  bool ok;
  if (indent->heap) {
    ok = set_str(object, "kind", "heap")
         && set_num(object, "allocation",
                    (double)capture->reference(capture->ctx,
                                               &indent->allocation));
  } else {
    ok = set_str(object, "kind", "managed")
         && set_num(object, "type", (double)indent->type)
         && set_num(object, "skip", (double)indent->skip)
         && set_num(object, "script", (double)indent->script)
         && set_num(object, "next", (double)indent->next);
  }
  if (!ok) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

qa_save_value *qa_ui_source_record_capture_save_state(
    qa_ui_source_record *record,
    const qa_ui_script_memory_capture *capture) {
  qa_save_value *object = qa_save_value_object();
  qa_save_value *backing = qa_save_value_object();
  qa_save_value *hash = qa_save_value_object();
  qa_save_value *indents = qa_save_value_array();
  qa_save_value *token = qa_ui_precomp_token_capture_save_state(record->token);
  if (object == NULL || backing == NULL || hash == NULL || indents == NULL
      || token == NULL)
    goto oom;
  if (record->heap) {
    if (!set_str(backing, "kind", "heap")
        || !set_num(backing, "allocation",
                    (double)capture->reference(capture->ctx,
                                               &record->allocation)))
      goto oom;
  } else {
    if (!set_str(backing, "kind", "managed")
        || !set_str(backing, "filename", record->filename)
        || !set_str(backing, "includePath", record->include_path)
        || !set_num(backing, "script", (double)record->m_script)
        || !set_num(backing, "tokens", (double)record->m_tokens)
        || !set_num(backing, "indent", (double)record->m_indent)
        || !set_num(backing, "skip", (double)record->m_skip))
      goto oom;
  }
  if (!qa_save_value_set(object, "backing", backing)) goto oom;
  backing = NULL;
  if (record->hash_heap) {
    if (!set_str(hash, "kind", "heap")
        || !set_num(hash, "allocation",
                    (double)capture->reference(capture->ctx,
                                               &record->hash_allocation)))
      goto oom;
  } else {
    qa_save_value *heads = qa_save_value_array();
    if (heads == NULL) goto oom;
    bool ok = set_str(hash, "kind", "managed");
    for (int64_t bucket = 0;
         ok && bucket < QA_UI_SOURCE_DEFINE_HASH_BUCKETS; bucket++) {
      if (record->heads[bucket] == 0) continue;
      qa_save_value *entry = qa_save_value_object();
      ok = entry != NULL && set_num(entry, "bucket", (double)bucket)
           && set_num(entry, "id", (double)record->heads[bucket])
           && qa_save_value_push(heads, entry);
      if (!ok) qa_save_value_free(entry);
    }
    ok = ok && qa_save_value_set(hash, "heads", heads);
    if (!ok) {
      qa_save_value_free(heads);
      goto oom;
    }
  }
  if (!qa_save_value_set(object, "hash", hash)) goto oom;
  hash = NULL;
  if (!set_num(object, "nextIndent", (double)record->next_indent)) goto oom;
  for (size_t index = 0; index < record->indent_count; index++) {
    source_indent *indent = record->indents[index];
    if (indent == NULL) continue;
    qa_save_value *entry = qa_save_value_object();
    qa_save_value *state = indent_capture(indent, capture);
    bool ok = entry != NULL && state != NULL
              && set_num(entry, "id", (double)indent->id)
              && qa_save_value_set(entry, "state", state);
    if (ok) state = NULL;
    ok = ok && qa_save_value_push(indents, entry);
    if (!ok) {
      qa_save_value_free(entry);
      qa_save_value_free(state);
      goto oom;
    }
  }
  if (!qa_save_value_set(object, "indents", indents)) goto oom;
  indents = NULL;
  if (!qa_save_value_set(object, "token", token)) goto oom;
  token = NULL;
  if (!record->has_punctuations) {
    if (!qa_save_value_set(object, "punctuations", qa_save_value_null()))
      goto oom;
  } else {
    qa_save_value *list = qa_save_value_array();
    if (list == NULL) goto oom;
    bool ok = true;
    for (size_t index = 0; ok && index < record->punctuation_count;
         index++) {
      qa_save_value *entry = qa_save_value_object();
      ok = entry != NULL
           && set_str(entry, "text", record->punctuations[index].text)
           && set_num(entry, "punctuation",
                      (double)record->punctuations[index].punctuation)
           && qa_save_value_push(list, entry);
      if (!ok) qa_save_value_free(entry);
    }
    ok = ok && qa_save_value_set(object, "punctuations", list);
    if (!ok) {
      qa_save_value_free(list);
      goto oom;
    }
  }
  return object;
oom:
  qa_save_value_free(object);
  qa_save_value_free(backing);
  qa_save_value_free(hash);
  qa_save_value_free(indents);
  qa_save_value_free(token);
  return NULL;
}

static bool fld(const qa_save_reader *reader, const char *name,
                qa_save_reader *out) {
  return qa_save_reader_field(reader, name, out);
}
static int64_t fld_int(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!fld(reader, name, &field))
    qa_range_error("source record restore allocation failed");
  int64_t value = (int64_t)qa_save_reader_integer(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static int64_t fld_int_min(const qa_save_reader *reader, const char *name,
                           double minimum) {
  qa_save_reader field;
  if (!fld(reader, name, &field))
    qa_range_error("source record restore allocation failed");
  int64_t value = (int64_t)qa_save_reader_integer_min(&field, minimum);
  qa_save_reader_destroy(&field);
  return value;
}
static char *fld_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!fld(reader, name, &field)) return NULL;
  char *value = copy_cstring(qa_save_reader_string(&field));
  qa_save_reader_destroy(&field);
  return value;
}
static bool fld_is_heap(const qa_save_reader *reader) {
  qa_save_reader field;
  if (!fld(reader, "kind", &field))
    qa_range_error("source record restore allocation failed");
  const char *choices[2] = {"heap", "managed"};
  const char *kind = qa_save_reader_choice_string(&field, choices, 2);
  bool heap = strcmp(kind, "heap") == 0;
  qa_save_reader_destroy(&field);
  return heap;
}

qa_ui_source_record *qa_ui_source_record_restore_save_state(
    const qa_save_value *value, const qa_ui_script_memory *memory,
    bool has_memory, const qa_ui_script_memory_restore *restore) {
  qa_save_reader reader, backing, hash;
  if (!qa_save_reader_init(&reader, value, "script.source")) return NULL;
  qa_ui_source_record *record = calloc(1, sizeof *record);
  if (record == NULL) {
    qa_save_reader_destroy(&reader);
    return NULL;
  }
  record->memory = memory;
  record->has_memory = has_memory;
  record->next_indent = 1;
  if (!fld(&reader, "backing", &backing) || !fld(&reader, "hash", &hash)) {
    qa_save_reader_destroy(&reader);
    goto oom;
  }
  if (fld_is_heap(&backing)) {
    record->allocation = restore->allocation(
        restore->ctx, fld_int_min(&backing, "allocation", 0));
    if (record->allocation.length != QA_UI_SOURCE_RECORD_BYTES)
      qa_save_reader_fail(&reader, "invalid source extent");
    record->heap = true;
    record->token = record_token_for(&record->allocation);
  } else {
    record->filename = fld_string(&backing, "filename");
    record->include_path = fld_string(&backing, "includePath");
    record->m_script = (uint32_t)fld_int_min(&backing, "script", 0);
    record->m_tokens = (uint32_t)fld_int_min(&backing, "tokens", 0);
    record->m_indent = (uint32_t)fld_int_min(&backing, "indent", 0);
    record->m_skip = (int32_t)fld_int(&backing, "skip");
    record->token = qa_ui_precomp_token_local();
  }
  qa_save_reader_destroy(&backing);
  if (record->token == NULL) {
    qa_save_reader_destroy(&hash);
    qa_save_reader_destroy(&reader);
    goto oom;
  }
  if (fld_is_heap(&hash)) {
    if (!has_memory)
      qa_save_reader_fail(&reader, "heap hash requires memory owner");
    record->hash_allocation = restore->allocation(
        restore->ctx, fld_int_min(&hash, "allocation", 0));
    if (record->hash_allocation.length
        != (size_t)QA_UI_SOURCE_DEFINE_HASH_BUCKETS * 4)
      qa_save_reader_fail(&reader, "invalid define hash extent");
    record->hash_heap = true;
  } else {
    qa_save_reader heads;
    if (!fld(&hash, "heads", &heads)) {
      qa_save_reader_destroy(&hash);
      qa_save_reader_destroy(&reader);
      goto oom;
    }
    size_t count = qa_save_reader_list_count(&heads);
    for (size_t index = 0; index < count; index++) {
      qa_save_reader entry;
      if (!qa_save_reader_item(&heads, index, &entry)) {
        qa_save_reader_destroy(&heads);
        qa_save_reader_destroy(&hash);
        qa_save_reader_destroy(&reader);
        goto oom;
      }
      int64_t bucket = fld_int_min(&entry, "bucket", 0);
      if (bucket >= QA_UI_SOURCE_DEFINE_HASH_BUCKETS
          || record->heads[bucket] != 0)
        qa_save_reader_fail(&entry, "invalid hash bucket");
      record->heads[bucket] = (uint32_t)fld_int_min(&entry, "id", 0);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&heads);
  }
  qa_save_reader_destroy(&hash);
  record->next_indent = fld_int_min(&reader, "nextIndent", 1);
  {
    qa_save_reader indents;
    if (!fld(&reader, "indents", &indents)) {
      qa_save_reader_destroy(&reader);
      goto oom;
    }
    size_t count = qa_save_reader_list_count(&indents);
    for (size_t index = 0; index < count; index++) {
      qa_save_reader entry;
      if (!qa_save_reader_item(&indents, index, &entry)) {
        qa_save_reader_destroy(&indents);
        qa_save_reader_destroy(&reader);
        goto oom;
      }
      int64_t id = fld_int_min(&entry, "id", 1);
      if (id >= record->next_indent
          || (id <= (int64_t)record->indent_count
              && record->indents[id - 1] != NULL))
        qa_save_reader_fail(&entry, "invalid indent identity");
      qa_save_reader state;
      if (!fld(&entry, "state", &state)) {
        qa_save_reader_destroy(&entry);
        qa_save_reader_destroy(&indents);
        qa_save_reader_destroy(&reader);
        goto oom;
      }
      source_indent *indent = calloc(1, sizeof *indent);
      if (indent == NULL) {
        qa_save_reader_destroy(&state);
        qa_save_reader_destroy(&entry);
        qa_save_reader_destroy(&indents);
        qa_save_reader_destroy(&reader);
        goto oom;
      }
      indent->id = id;
      qa_save_reader indent_reader;
      if (!qa_save_reader_init(&indent_reader, state.value,
                               "script.indent")) {
        free(indent);
        qa_save_reader_destroy(&state);
        qa_save_reader_destroy(&entry);
        qa_save_reader_destroy(&indents);
        qa_save_reader_destroy(&reader);
        goto oom;
      }
      if (fld_is_heap(&indent_reader)) {
        if (!has_memory)
          qa_save_reader_fail(&indent_reader,
                              "heap indent requires memory owner");
        indent->allocation = restore->allocation(
            restore->ctx, fld_int_min(&indent_reader, "allocation", 0));
        if (indent->allocation.length != INDENT_BYTES)
          qa_save_reader_fail(&indent_reader, "invalid indent extent");
        indent->heap = true;
      } else {
        indent->type = fld_int(&indent_reader, "type");
        indent->skip = fld_int(&indent_reader, "skip");
        indent->script = fld_int_min(&indent_reader, "script", 0);
        indent->next = fld_int_min(&indent_reader, "next", 0);
      }
      qa_save_reader_destroy(&indent_reader);
      qa_save_reader_destroy(&state);
      if (!grow_indents(record, (size_t)id)) {
        free(indent);
        qa_save_reader_destroy(&entry);
        qa_save_reader_destroy(&indents);
        qa_save_reader_destroy(&reader);
        goto oom;
      }
      record->indents[id - 1] = indent;
      if ((size_t)id > record->indent_count)
        record->indent_count = (size_t)id;
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&indents);
  }
  {
    qa_save_reader field;
    if (!fld(&reader, "token", &field)) {
      qa_save_reader_destroy(&reader);
      goto oom;
    }
    qa_ui_precomp_token_restore_save_state(record->token, field.value,
                                           record->heap);
    qa_save_reader_destroy(&field);
    if (!fld(&reader, "punctuations", &field)) {
      qa_save_reader_destroy(&reader);
      goto oom;
    }
    if (!qa_save_reader_is_null(&field)) {
      size_t count = qa_save_reader_list_count(&field);
      qa_ui_script_punctuation_spec *specs =
          calloc(count == 0 ? 1 : count, sizeof *specs);
      char **texts = calloc(count == 0 ? 1 : count, sizeof *texts);
      bool ok = specs != NULL && texts != NULL;
      for (size_t index = 0; ok && index < count; index++) {
        qa_save_reader entry;
        ok = qa_save_reader_item(&field, index, &entry);
        if (ok) {
          texts[index] = fld_string(&entry, "text");
          specs[index].text = texts[index];
          specs[index].punctuation = fld_int(&entry, "punctuation");
          ok = texts[index] != NULL;
          qa_save_reader_destroy(&entry);
        }
      }
      if (!ok) {
        if (texts != NULL)
          for (size_t index = 0; index < count; index++) free(texts[index]);
        free(texts);
        free(specs);
        qa_save_reader_destroy(&field);
        qa_save_reader_destroy(&reader);
        goto oom;
      }
      record->punctuations = specs;
      record->punctuation_texts = texts;
      record->punctuation_count = count;
      record->has_punctuations = true;
      if (record->heap)
        wr_u32(record->allocation.bytes, OFF_PUNCTUATIONS, 1);
    }
    qa_save_reader_destroy(&field);
  }
  qa_save_reader_destroy(&reader);
  return record;
oom:
  qa_ui_source_record_destroy(record);
  return NULL;
}
