/* See header. Release32 libvar_t layout: two pointers, int, qboolean, float, next pointer.
 * GPL-2.0-or-later. */
#include "qa/bots/behavior/library/libvars.h"
#include "library_internal.h"
#include <math.h>
#define RECORD_BYTES 24
struct qa_bot_lib_var {
  qa_bot_lib_vars *owner;
  qa_bot_memory_allocation allocation;
  bool retired;
};
static float parse_value(const char *t) {
  int64_t den = 0;
  float val = 0.0f;
  size_t len = strlen(t);
  for (size_t i = 0; i < len; i++) {
    int byte = (uint8_t)t[i];
    if (byte < 48 || byte > 57) {
      if (den != 0 || byte != 46) return 0.0f;
      den = 10;
      i++;
      /* Port policy: a terminal dot is nonnumeric; C would read beyond its owned string. */
      if (i == len) return 0.0f;
      byte = (uint8_t)t[i];
    }
    if (den != 0) {
      /* Native x64 signed-char, binary32 division/addition; the post-dot byte is unchecked. */
      int sb = byte < 128 ? byte : byte - 256;
      val = (float)((double)val + (double)(float)((double)(sb - 48) / (double)den));
      if (den * 10 > 2147483647LL)
        qa_range_error(
            "LibVarStringValue: fractional denominator would overflow the source signed int");
      den *= 10;
    } else {
      /* The source literal 10.0 promotes this expression to double before float assignment. */
      val = (float)((double)val * 10.0 + (double)(byte - 48));
    }
  }
  return val;
}
float qa_lib_var_string_value(const char *text) { return parse_value(text ? text : ""); }

void qa_bot_lib_vars_init_memory(qa_bot_lib_vars *v, qa_bot_memory *memory) {
  memset(v, 0, sizeof *v);
  qa_bot_memory_init(&v->owned_memory);
  v->memory = memory != NULL ? memory : &v->owned_memory;
  v->next_pointer = 1;
}
void qa_bot_lib_vars_init(qa_bot_lib_vars *v) { qa_bot_lib_vars_init_memory(v, NULL); }
void qa_bot_lib_vars_destroy(qa_bot_lib_vars *v) {
  for (size_t i = 0; i < v->record_capacity; i++) free(v->records[i]);
  for (size_t i = 0; i < v->retired_count; i++) free(v->retired[i]);
  free(v->records);
  free(v->strings);
  free(v->retired);
  v->records = NULL;
  v->strings = NULL;
  v->retired = NULL;
  v->record_capacity = v->string_capacity = v->retired_count = v->retired_capacity = 0;
  qa_bot_memory_destroy(&v->owned_memory);
}
static const uint8_t *view_bytes(const qa_bot_lib_var *variable) {
  if (variable->retired) qa_range_error("Bot memory allocation has been freed");
  return variable->allocation.bytes;
}
static qa_bot_lib_string_pointer *string_cell(qa_bot_lib_vars *v, int64_t pointer) {
  if (pointer < 0 || (size_t)pointer >= v->string_capacity) return NULL;
  qa_bot_lib_string_pointer *cell = &v->strings[pointer];
  return cell->live ? cell : NULL;
}
static void set_string_cell(qa_bot_lib_vars *v, int64_t pointer, qa_bot_memory_allocation allocation,
                            int64_t offset) {
  if ((size_t)pointer >= v->string_capacity) {
    size_t next = lib_grow(v->string_capacity, (size_t)pointer + 1);
    v->strings = lib_realloc(v->strings, next * sizeof *v->strings);
    memset(v->strings + v->string_capacity, 0, (next - v->string_capacity) * sizeof *v->strings);
    v->string_capacity = next;
  }
  v->strings[pointer] = (qa_bot_lib_string_pointer){true, allocation, offset};
}
static void set_record_cell(qa_bot_lib_vars *v, int64_t pointer, qa_bot_lib_var *record) {
  if ((size_t)pointer >= v->record_capacity) {
    size_t next = lib_grow(v->record_capacity, (size_t)pointer + 1);
    v->records = lib_realloc(v->records, next * sizeof *v->records);
    memset(v->records + v->record_capacity, 0, (next - v->record_capacity) * sizeof *v->records);
    v->record_capacity = next;
  }
  v->records[pointer] = record;
}
static const char *read_string(qa_bot_lib_vars *v, uint32_t pointer) {
  qa_bot_lib_string_pointer *reference = string_cell(v, pointer);
  if (reference == NULL) qa_ui_script_plain_error("Bot library variable string pointer is null or freed");
  const uint8_t *bytes = reference->allocation.bytes;
  if (memchr(bytes + reference->offset, 0, reference->allocation.length - (size_t)reference->offset) == NULL)
    qa_range_error("Bot library variable string has no allocated terminator");
  return (const char *)bytes + reference->offset;
}
const char *qa_bot_lib_var_name(const qa_bot_lib_var *variable) {
  return read_string(variable->owner, lib_u32(view_bytes(variable)));
}
const char *qa_bot_lib_var_string(const qa_bot_lib_var *variable) {
  return read_string(variable->owner, lib_u32(view_bytes(variable) + 4));
}
int32_t qa_bot_lib_var_flags(const qa_bot_lib_var *variable) { return lib_i32(view_bytes(variable) + 8); }
bool qa_bot_lib_var_modified(const qa_bot_lib_var *variable) { return lib_i32(view_bytes(variable) + 12) != 0; }
float qa_bot_lib_var_value(const qa_bot_lib_var *variable) { return lib_f32(view_bytes(variable) + 16); }

static int64_t next_pointer(qa_bot_lib_vars *v) {
  if (v->next_pointer > 0xffffffffLL) qa_range_error("Bot library variable pointer IDs exhausted");
  return v->next_pointer++;
}
static qa_bot_lib_var *record_at(qa_bot_lib_vars *v, int64_t pointer) {
  qa_bot_lib_var *record =
      pointer >= 0 && (size_t)pointer < v->record_capacity ? v->records[pointer] : NULL;
  if (record == NULL) qa_ui_script_plain_error("Bot library variable record pointer is not live");
  return record;
}
static qa_bot_lib_var *bind(qa_bot_lib_vars *v, qa_bot_memory_allocation allocation) {
  qa_bot_lib_var *record = lib_alloc(sizeof *record);
  record->owner = v;
  record->allocation = allocation;
  return record;
}
static qa_bot_lib_var *allocate(qa_bot_lib_vars *v, const char *name) {
  int64_t pointer = next_pointer(v), name_pointer = next_pointer(v);
  size_t length = strlen(name);
  qa_bot_memory_allocation allocation =
      lib_mem_alloc(v->memory, (int64_t)(RECORD_BYTES + length + 1), QA_BOT_MEMORY_HEAP, false);
  memset(allocation.bytes, 0, RECORD_BYTES);
  memcpy(allocation.bytes + RECORD_BYTES, name, length + 1);
  qa_bot_lib_var *record = bind(v, allocation);
  lib_set_u32(allocation.bytes, (uint32_t)name_pointer);
  lib_set_u32(allocation.bytes + 20, v->head);
  set_string_cell(v, name_pointer, allocation, RECORD_BYTES);
  set_record_cell(v, pointer, record);
  v->head = (uint32_t)pointer;
  return record;
}
static void assign(qa_bot_lib_vars *v, qa_bot_lib_var *record, const char *text, float numeric) {
  int64_t pointer = next_pointer(v);
  size_t length = strlen(text);
  qa_bot_memory_allocation allocation =
      lib_mem_alloc(v->memory, (int64_t)length + 1, QA_BOT_MEMORY_HEAP, false);
  set_string_cell(v, pointer, allocation, 0);
  uint8_t *bytes = record->allocation.bytes;
  qa_bot_lib_string_pointer *old = string_cell(v, lib_u32(bytes + 4));
  if (old != NULL) old->live = false;
  lib_set_u32(bytes + 4, (uint32_t)pointer);
  memcpy(allocation.bytes, text, length + 1);
  lib_set_f32(bytes + 16, numeric);
  lib_set_i32(bytes + 12, 1);
}
static void free_string(qa_bot_lib_vars *v, qa_bot_lib_var *record) {
  qa_bot_lib_string_pointer *reference = string_cell(v, lib_u32(record->allocation.bytes + 4));
  if (reference == NULL) qa_ui_script_plain_error("Bot library variable string pointer is not owned");
  qa_bot_memory_allocation allocation = reference->allocation;
  qa_bot_memory_free(v->memory, &allocation);
}
static bool same_name(const uint8_t *name, size_t available, qa_bot_lib_name_byte_fn read_byte,
                      void *ctx) {
  for (int64_t index = 0; index < 99999; index++) {
    int64_t b = read_byte(ctx, index);
    if ((size_t)index >= available || b < 0 || b > 255)
      qa_range_error("Source name read requires an allocated byte");
    int a = name[index];
    int left = a >= 97 && a <= 122 ? a - 32 : a;
    int right = b >= 97 && b <= 122 ? (int)b - 32 : (int)b;
    if (left != right) return false;
    if (a == 0) return true;
  }
  return true;
}
/* LibVarAlloc prepends new variables; updating an existing variable leaves its position alone. */
static qa_bot_lib_var *get_by_name_bytes(qa_bot_lib_vars *v, qa_bot_lib_name_byte_fn read_byte,
                                         void *ctx) {
  if (read_byte == NULL) return NULL;
  for (int64_t pointer = v->head; pointer != 0;) {
    qa_bot_lib_var *variable = record_at(v, pointer);
    if (same_name(variable->allocation.bytes + RECORD_BYTES,
                  variable->allocation.length - RECORD_BYTES, read_byte, ctx))
      return variable;
    pointer = lib_u32(variable->allocation.bytes + 20);
  }
  return NULL;
}
typedef struct text_name {
  const char *text;
  size_t length;
} text_name;
static int64_t text_byte(void *ctx, int64_t index) {
  text_name *name = ctx;
  return (size_t)index < name->length ? (uint8_t)name->text[index] : 0;
}
static qa_bot_lib_var *get_text(qa_bot_lib_vars *v, const char *name) {
  text_name query = {name, strlen(name)};
  return get_by_name_bytes(v, text_byte, &query);
}
qa_bot_lib_var *qa_bot_lib_vars_get(qa_bot_lib_vars *v, const char *name) { return get_text(v, name); }
const char *qa_bot_lib_vars_get_string(qa_bot_lib_vars *v, const char *n) {
  qa_bot_lib_var *r = get_text(v, n);
  return r ? qa_bot_lib_var_string(r) : "";
}
const char *qa_bot_lib_vars_get_string_by_name_bytes(qa_bot_lib_vars *v,
                                                     qa_bot_lib_name_byte_fn read_byte, void *ctx) {
  qa_bot_lib_var *r = get_by_name_bytes(v, read_byte, ctx);
  return r ? qa_bot_lib_var_string(r) : "";
}
float qa_bot_lib_vars_get_value(qa_bot_lib_vars *v, const char *n) {
  qa_bot_lib_var *r = get_text(v, n);
  return r ? qa_bot_lib_var_value(r) : 0.0f;
}
qa_bot_lib_var *qa_bot_lib_vars_get_or_create(qa_bot_lib_vars *v, const char *n, const char *d) {
  qa_bot_lib_var *existing = get_text(v, n);
  if (existing != NULL) return existing;
  float numeric = parse_value(d);
  qa_bot_lib_var *variable = allocate(v, n);
  assign(v, variable, d, numeric);
  return variable;
}
const char *qa_bot_lib_vars_string(qa_bot_lib_vars *v, const char *n, const char *d) {
  return qa_bot_lib_var_string(qa_bot_lib_vars_get_or_create(v, n, d));
}
float qa_bot_lib_vars_value(qa_bot_lib_vars *v, const char *n, const char *d) {
  return qa_bot_lib_var_value(qa_bot_lib_vars_get_or_create(v, n, d));
}
void qa_bot_lib_vars_set(qa_bot_lib_vars *v, const char *n, const char *val) {
  float numeric = parse_value(val);
  /* The new value may alias the freed string (a borrowed view of the old one). */
  char *text = lib_strdup(val);
  qa_bot_lib_var *existing = get_text(v, n);
  if (existing != NULL) free_string(v, existing);
  qa_bot_lib_var *variable = existing != NULL ? existing : allocate(v, n);
  assign(v, variable, text, numeric);
  free(text);
}
void qa_bot_lib_vars_set_by_name_bytes(qa_bot_lib_vars *v, qa_bot_lib_name_byte_fn read_byte,
                                       void *byte_ctx, qa_bot_lib_text_fn read_name, void *name_ctx,
                                       qa_bot_lib_text_fn read_value, void *value_ctx) {
  qa_bot_lib_var *existing = get_by_name_bytes(v, read_byte, byte_ctx);
  char *name = lib_strdup(existing == NULL ? read_name(name_ctx) : qa_bot_lib_var_name(existing));
  qa_bot_lib_vars_set(v, name, read_value(value_ctx));
  free(name);
}
bool qa_bot_lib_vars_changed(qa_bot_lib_vars *v, const char *n) {
  qa_bot_lib_var *r = get_text(v, n);
  return r ? qa_bot_lib_var_modified(r) : false;
}
void qa_bot_lib_vars_set_not_modified(qa_bot_lib_vars *v, const char *n) {
  qa_bot_lib_var *r = get_text(v, n);
  if (r) lib_set_i32(r->allocation.bytes + 12, 0);
}
void qa_bot_lib_vars_clear(qa_bot_lib_vars *v) {
  while (v->head != 0) {
    int64_t pointer = v->head;
    qa_bot_lib_var *variable = record_at(v, pointer);
    const uint8_t *bytes = variable->allocation.bytes;
    v->head = lib_u32(bytes + 20);
    uint32_t value_pointer = lib_u32(bytes + 4), name_pointer = lib_u32(bytes);
    if (value_pointer != 0) free_string(v, variable);
    qa_bot_lib_string_pointer *cell = string_cell(v, value_pointer);
    if (cell != NULL) cell->live = false;
    cell = string_cell(v, name_pointer);
    if (cell != NULL) cell->live = false;
    qa_bot_memory_free(v->memory, &variable->allocation);
    variable->retired = true;
    v->records[pointer] = NULL;
    /* Retained handles belong to the old botlib lifetime and read as freed. */
    if (v->retired_count == v->retired_capacity) {
      v->retired_capacity = lib_grow(v->retired_capacity, v->retired_count + 1);
      v->retired = lib_realloc(v->retired, v->retired_capacity * sizeof *v->retired);
    }
    v->retired[v->retired_count++] = variable;
  }
}
qa_save_value *qa_bot_lib_vars_checkpoint(qa_bot_lib_vars *v, qa_bot_memory_capture *memory) {
  qa_save_value *object = lib_sv(qa_save_value_object());
  lib_set_num(object, "nextPointer", (double)v->next_pointer);
  lib_set_num(object, "head", v->head);
  qa_save_value *records = lib_sv(qa_save_value_array());
  for (size_t pointer = 0; pointer < v->record_capacity; pointer++) {
    if (v->records[pointer] == NULL) continue;
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set_num(entry, "pointer", (double)pointer);
    lib_set_num(entry, "allocation",
                (double)qa_bot_memory_capture_reference(memory, &v->records[pointer]->allocation));
    lib_push(records, entry);
  }
  lib_set(object, "records", records);
  qa_save_value *strings = lib_sv(qa_save_value_array());
  for (size_t pointer = 0; pointer < v->string_capacity; pointer++) {
    if (!v->strings[pointer].live) continue;
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set_num(entry, "pointer", (double)pointer);
    lib_set_num(entry, "allocation",
                (double)qa_bot_memory_capture_reference(memory, &v->strings[pointer].allocation));
    lib_set_num(entry, "offset", (double)v->strings[pointer].offset);
    lib_push(strings, entry);
  }
  lib_set(object, "strings", strings);
  return object;
}
void qa_bot_lib_vars_restore(qa_bot_lib_vars *v, const qa_save_value *value,
                             qa_bot_memory_restored *memory) {
  qa_save_reader reader, records, strings;
  if (!qa_save_reader_init(&reader, value, "bot.variables")) qa_range_error("out of memory");
  double next = lib_int_min(&reader, "nextPointer", 1);
  double head = lib_int_min(&reader, "head", 0);
  lib_field(&reader, "records", &records);
  lib_field(&reader, "strings", &strings);
  bool live = false;
  for (size_t i = 0; i < v->record_capacity && !live; i++) live = v->records[i] != NULL;
  for (size_t i = 0; i < v->string_capacity && !live; i++) live = v->strings[i].live;
  if (live || next < 1 || next > 4294967296.0)
    qa_ui_script_plain_error("Invalid bot library variable restoration");
  size_t cells = (size_t)next + 1;
  bool *pointers = lib_alloc(cells);
  qa_bot_lib_string_pointer *new_strings = lib_alloc(cells * sizeof *new_strings);
  qa_bot_lib_var **new_records = lib_alloc(cells * sizeof *new_records);
  size_t string_count = qa_save_reader_list_count(&strings);
  for (size_t i = 0; i < string_count; i++) {
    qa_save_reader entry;
    lib_item(&strings, i, &entry);
    double pointer = lib_int_min(&entry, "pointer", 1);
    double reference = lib_int_min(&entry, "allocation", 0);
    double offset = lib_int_min(&entry, "offset", 0);
    qa_save_reader_destroy(&entry);
    if (pointer < 1 || pointer >= next || pointers[(size_t)pointer])
      qa_ui_script_plain_error("Invalid saved bot variable pointer");
    pointers[(size_t)pointer] = true;
    qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
    if (offset < 0 || offset >= (double)allocation.length ||
        memchr(allocation.bytes + (size_t)offset, 0, allocation.length - (size_t)offset) == NULL)
      qa_ui_script_plain_error("Invalid saved bot variable string");
    new_strings[(size_t)pointer] = (qa_bot_lib_string_pointer){true, allocation, (int64_t)offset};
  }
  size_t record_count = qa_save_reader_list_count(&records);
  for (size_t i = 0; i < record_count; i++) {
    qa_save_reader entry;
    lib_item(&records, i, &entry);
    double pointer = lib_int_min(&entry, "pointer", 1);
    double reference = lib_int_min(&entry, "allocation", 0);
    qa_save_reader_destroy(&entry);
    if (pointer < 1 || pointer >= next || pointers[(size_t)pointer])
      qa_ui_script_plain_error("Invalid saved bot variable pointer");
    pointers[(size_t)pointer] = true;
    qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
    if (allocation.length < RECORD_BYTES + 1)
      qa_ui_script_plain_error("Saved bot variable record is truncated");
    uint32_t a = lib_u32(allocation.bytes), b = lib_u32(allocation.bytes + 4);
    if (a >= cells || b >= cells || !new_strings[a].live || !new_strings[b].live)
      qa_ui_script_plain_error("Saved bot variable string reference is missing");
    new_records[(size_t)pointer] = bind(v, allocation);
  }
  bool *visited = lib_alloc(cells);
  size_t visited_count = 0;
  for (double pointer = head; pointer != 0;) {
    qa_bot_lib_var *record = pointer < (double)cells ? new_records[(size_t)pointer] : NULL;
    if (record == NULL || visited[(size_t)pointer])
      qa_ui_script_plain_error("Invalid saved bot variable list");
    visited[(size_t)pointer] = true;
    visited_count++;
    pointer = lib_u32(record->allocation.bytes + 20);
  }
  if (visited_count != record_count)
    qa_ui_script_plain_error("Saved bot variable list has unreachable records");
  free(v->records);
  free(v->strings);
  v->records = new_records;
  v->record_capacity = cells;
  v->strings = new_strings;
  v->string_capacity = cells;
  v->head = (uint32_t)head;
  v->next_pointer = (int64_t)next;
  free(pointers);
  free(visited);
  qa_save_reader_destroy(&records);
  qa_save_reader_destroy(&strings);
  qa_save_reader_destroy(&reader);
}
int64_t qa_bot_lib_vars_reference(qa_bot_lib_vars *v, const qa_bot_lib_var *variable) {
  for (size_t pointer = 0; pointer < v->record_capacity; pointer++)
    if (v->records[pointer] == variable) return (int64_t)pointer;
  qa_ui_script_plain_error("Bot library variable reference belongs to another owner");
}
qa_bot_lib_var *qa_bot_lib_vars_resolve(qa_bot_lib_vars *v, int64_t pointer) {
  return record_at(v, pointer);
}
