/* See include/qa/ui/common/legacy/team_arena/memory.h.
 * GPL-2.0-or-later. */
#include "qa/ui/common/legacy/team_arena/memory.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/format.h"
#include "qa/foundation/support.h"
#include "qa/ui/common/legacy/script/lexer.h" /* qa_ui_script_plain_error */

typedef struct byte_storage {
  uint8_t *bytes;
  size_t length;
  size_t refs;
} byte_storage;

static byte_storage *storage_new(size_t length) {
  byte_storage *storage = calloc(1, sizeof *storage);
  if (storage == NULL) return NULL;
  storage->bytes = calloc(length == 0 ? 1 : length, 1);
  if (storage->bytes == NULL) {
    free(storage);
    return NULL;
  }
  storage->length = length;
  storage->refs = 1;
  return storage;
}
static byte_storage *storage_retain(byte_storage *storage) {
  if (storage != NULL) storage->refs++;
  return storage;
}
static void storage_release(byte_storage *storage) {
  if (storage == NULL || --storage->refs > 0) return;
  free(storage->bytes);
  free(storage);
}

typedef enum ptr_kind {
  PTR_STRING,
  PTR_ALLOCATION,
  PTR_SCRIPT,
  PTR_ITEM,
  PTR_MENU,
  PTR_RESOURCE
} ptr_kind;

typedef struct ptr_entry {
  int64_t offset; /* absolute byte offset in the storage */
  ptr_kind kind;
  void *value; /* string: retained reference; others borrowed */
  bool has_handle;
  int32_t handle;
} ptr_entry;

typedef struct ptr_map {
  ptr_entry *entries;
  size_t count, cap;
  size_t refs;
} ptr_map;

static ptr_map *map_new(void) {
  ptr_map *map = calloc(1, sizeof *map);
  if (map != NULL) map->refs = 1;
  return map;
}
static ptr_map *map_retain(ptr_map *map) {
  if (map != NULL) map->refs++;
  return map;
}
static void map_release(ptr_map *map);
static ptr_entry *map_get(ptr_map *map, int64_t offset) {
  if (map == NULL) return NULL;
  for (size_t i = 0; i < map->count; i++)
    if (map->entries[i].offset == offset) return &map->entries[i];
  return NULL;
}
static void map_delete(ptr_map *map, int64_t offset) {
  if (map == NULL) return;
  for (size_t i = 0; i < map->count; i++) {
    if (map->entries[i].offset == offset) {
      if (map->entries[i].kind == PTR_STRING)
        qa_ui_string_reference_release(map->entries[i].value);
      memmove(&map->entries[i], &map->entries[i + 1],
              (map->count - i - 1) * sizeof(ptr_entry));
      map->count--;
      return;
    }
  }
}
static void map_set(ptr_map *map, int64_t offset, ptr_kind kind, void *value,
                    bool has_handle, int32_t handle) {
  ptr_entry *entry = map_get(map, offset);
  if (entry == NULL) {
    if (map->count == map->cap) {
      size_t cap = map->cap == 0 ? 8 : map->cap * 2;
      ptr_entry *grown = realloc(map->entries, cap * sizeof *grown);
      if (grown == NULL) qa_range_error("UI memory allocation failed");
      map->entries = grown;
      map->cap = cap;
    }
    entry = &map->entries[map->count++];
    entry->offset = offset;
    entry->value = NULL;
    entry->kind = kind;
  } else if (entry->kind == PTR_STRING) {
    qa_ui_string_reference_release(entry->value);
  }
  entry->kind = kind;
  entry->value = value;
  entry->has_handle = has_handle;
  entry->handle = handle;
  if (kind == PTR_STRING) qa_ui_string_reference_retain(value);
}
static void map_release(ptr_map *map) {
  if (map == NULL || --map->refs > 0) return;
  for (size_t i = 0; i < map->count; i++)
    if (map->entries[i].kind == PTR_STRING)
      qa_ui_string_reference_release(map->entries[i].value);
  free(map->entries);
  free(map);
}

struct qa_ui_string_reference {
  byte_storage *storage;
  int64_t offset;
  ptr_map *pointers;
  size_t refs;
};

static qa_ui_string_reference *reference_new(byte_storage *storage,
                                             int64_t offset,
                                             ptr_map *pointers) {
  if (offset < 0 || offset >= (int64_t)storage->length)
    qa_range_error("UI string pointer is outside its byte storage");
  qa_ui_string_reference *reference = calloc(1, sizeof *reference);
  if (reference == NULL) return NULL;
  reference->storage = storage_retain(storage);
  reference->offset = offset;
  reference->pointers = map_retain(pointers);
  reference->refs = 1;
  return reference;
}

qa_ui_string_reference *qa_ui_string_reference_literal(const char *text) {
  size_t length = strlen(text);
  byte_storage *storage = storage_new(length + 1);
  if (storage == NULL) return NULL;
  memcpy(storage->bytes, text, length);
  qa_ui_string_reference *reference = reference_new(storage, 0, NULL);
  storage_release(storage);
  return reference;
}

qa_ui_string_reference *qa_ui_string_reference_retain(
    qa_ui_string_reference *reference) {
  if (reference != NULL) reference->refs++;
  return reference;
}

void qa_ui_string_reference_release(qa_ui_string_reference *reference) {
  if (reference == NULL || --reference->refs > 0) return;
  storage_release(reference->storage);
  map_release(reference->pointers);
  free(reference);
}

int64_t qa_ui_string_reference_offset(
    const qa_ui_string_reference *reference) {
  return reference->offset;
}

char *qa_ui_string_reference_read(qa_ui_string_reference *reference) {
  byte_storage *storage = reference->storage;
  for (int64_t index = reference->offset;
       index < (int64_t)storage->length; index++) {
    for (int64_t word = index - 3; word <= index; word++) {
      ptr_entry *pointer = map_get(reference->pointers, word);
      if (pointer != NULL
          && !(pointer->kind == PTR_RESOURCE && pointer->has_handle))
        qa_range_error("UI string byte read requires QVM pointer address bits");
    }
    if (storage->bytes[index] == 0) {
      size_t length = (size_t)(index - reference->offset);
      char *result = malloc(length + 1);
      if (result == NULL) return NULL;
      memcpy(result, storage->bytes + reference->offset, length);
      result[length] = '\0';
      return result;
    }
  }
  qa_range_error("UI string reads beyond its retained byte storage");
}

struct qa_ui_memory_allocation {
  byte_storage *storage;
  ptr_map *pointers;
  int64_t offset;
  int64_t size;
};

static qa_ui_memory_allocation *allocation_new(byte_storage *storage,
                                               ptr_map *pointers,
                                               int64_t offset,
                                               int64_t size) {
  if (offset < 0 || size < 0
      || offset + size > (int64_t)storage->length)
    qa_range_error("UI_Alloc borrow is outside the physical memory pool");
  qa_ui_memory_allocation *allocation = calloc(1, sizeof *allocation);
  if (allocation == NULL) return NULL;
  allocation->storage = storage_retain(storage);
  allocation->pointers = map_retain(pointers);
  allocation->offset = offset;
  allocation->size = size;
  return allocation;
}

qa_ui_memory_allocation *qa_ui_memory_allocation_zeroed(int64_t size) {
  if (size < 0)
    qa_range_error("UI_Alloc borrow is outside the physical memory pool");
  byte_storage *storage = storage_new((size_t)size);
  ptr_map *map = map_new();
  if (storage == NULL || map == NULL) {
    storage_release(storage);
    map_release(map);
    return NULL;
  }
  qa_ui_memory_allocation *allocation =
      allocation_new(storage, map, 0, size);
  storage_release(storage);
  map_release(map);
  return allocation;
}

void qa_ui_memory_allocation_destroy(qa_ui_memory_allocation *allocation) {
  if (allocation == NULL) return;
  storage_release(allocation->storage);
  map_release(allocation->pointers);
  free(allocation);
}

int64_t qa_ui_memory_allocation_offset(
    const qa_ui_memory_allocation *allocation) {
  return allocation->offset;
}
int64_t qa_ui_memory_allocation_size(
    const qa_ui_memory_allocation *allocation) {
  return allocation->size;
}

qa_ui_memory_allocation *qa_ui_memory_allocation_subrecord(
    qa_ui_memory_allocation *allocation, int64_t offset, int64_t size) {
  if (offset < 0 || size < 0 || offset + size > allocation->size)
    qa_range_error("UI record view is outside its retained storage");
  return allocation_new(allocation->storage, allocation->pointers,
                        allocation->offset + offset, size);
}

qa_ui_memory_allocation *qa_ui_memory_allocation_dereference(
    qa_ui_memory_allocation *allocation, int64_t size) {
  return allocation_new(allocation->storage, allocation->pointers,
                        allocation->offset, size);
}

static void check_word(qa_ui_memory_allocation *allocation, int64_t offset) {
  if (offset < 0 || offset + 4 > allocation->size)
    qa_range_error("UI_Alloc field is outside the borrowed record");
}

static void discard_pointers(qa_ui_memory_allocation *allocation,
                             int64_t offset, int64_t size) {
  if (size == 0) return;
  for (int64_t pointer = allocation->offset + offset - 3;
       pointer < allocation->offset + offset + size; pointer++)
    map_delete(allocation->pointers, pointer);
}

static void check_numeric(qa_ui_memory_allocation *allocation,
                          int64_t offset) {
  check_word(allocation, offset);
  for (int64_t index = allocation->offset + offset - 3;
       index < allocation->offset + offset + 4; index++) {
    ptr_entry *pointer = map_get(allocation->pointers, index);
    if (pointer != NULL
        && !(pointer->kind == PTR_RESOURCE && pointer->has_handle))
      qa_ui_script_plain_error(
          "UI_Alloc numeric read requires QVM pointer address bits");
  }
}

void qa_ui_memory_allocation_clear(qa_ui_memory_allocation *allocation) {
  discard_pointers(allocation, 0, allocation->size);
  memset(allocation->storage->bytes + allocation->offset, 0,
         (size_t)allocation->size);
}

static uint32_t read_u32(qa_ui_memory_allocation *allocation,
                         int64_t offset) {
  uint32_t value;
  memcpy(&value,
         allocation->storage->bytes + allocation->offset + offset,
         sizeof value);
  return value;
}
static void write_u32(qa_ui_memory_allocation *allocation, int64_t offset,
                      uint32_t value) {
  memcpy(allocation->storage->bytes + allocation->offset + offset, &value,
         sizeof value);
}

int32_t qa_ui_memory_allocation_get_int32(
    qa_ui_memory_allocation *allocation, int64_t offset) {
  check_numeric(allocation, offset);
  return (int32_t)read_u32(allocation, offset);
}
double qa_ui_memory_allocation_get_float32(
    qa_ui_memory_allocation *allocation, int64_t offset) {
  check_numeric(allocation, offset);
  float value;
  memcpy(&value, allocation->storage->bytes + allocation->offset + offset,
         sizeof value);
  return value;
}
void qa_ui_memory_allocation_set_int32(qa_ui_memory_allocation *allocation,
                                       int64_t offset, int32_t value) {
  check_word(allocation, offset);
  discard_pointers(allocation, offset, 4);
  write_u32(allocation, offset, (uint32_t)value);
}
void qa_ui_memory_allocation_set_float32(
    qa_ui_memory_allocation *allocation, int64_t offset, double value) {
  check_word(allocation, offset);
  discard_pointers(allocation, offset, 4);
  float stored = (float)value;
  memcpy(allocation->storage->bytes + allocation->offset + offset, &stored,
         sizeof stored);
}

static ptr_entry *pointer_at(qa_ui_memory_allocation *allocation,
                             int64_t offset) {
  check_word(allocation, offset);
  ptr_entry *pointer =
      map_get(allocation->pointers, allocation->offset + offset);
  if (pointer == NULL
      || (pointer->kind == PTR_RESOURCE && pointer->has_handle)) {
    check_numeric(allocation, offset);
    if (read_u32(allocation, offset) != 0)
      qa_ui_script_plain_error("UI pointer read contains non-pointer bytes");
    return NULL;
  }
  return pointer;
}

static void set_pointer(qa_ui_memory_allocation *allocation, int64_t offset,
                        bool has_value, ptr_kind kind, void *value) {
  qa_ui_memory_allocation_set_int32(allocation, offset, 0);
  if (has_value)
    map_set(allocation->pointers, allocation->offset + offset, kind, value,
            false, 0);
}

char *qa_ui_memory_allocation_get_string(qa_ui_memory_allocation *allocation,
                                         int64_t offset) {
  qa_ui_string_reference *reference =
      qa_ui_memory_allocation_get_string_reference(allocation, offset);
  return reference == NULL ? NULL : qa_ui_string_reference_read(reference);
}

qa_ui_string_reference *qa_ui_memory_allocation_get_string_reference(
    qa_ui_memory_allocation *allocation, int64_t offset) {
  ptr_entry *pointer = pointer_at(allocation, offset);
  if (pointer == NULL) return NULL;
  if (pointer->kind == PTR_STRING) return pointer->value;
  qa_ui_script_plain_error("UI_Alloc string read aliases a non-string pointer");
}

void qa_ui_memory_allocation_set_string_text(
    qa_ui_memory_allocation *allocation, int64_t offset,
    const char *value) {
  if (value == NULL) {
    set_pointer(allocation, offset, false, PTR_STRING, NULL);
    return;
  }
  qa_ui_string_reference *reference =
      qa_ui_string_reference_literal(value);
  if (reference == NULL) qa_range_error("UI memory allocation failed");
  set_pointer(allocation, offset, true, PTR_STRING, reference);
  qa_ui_string_reference_release(reference);
}

void qa_ui_memory_allocation_set_string_reference(
    qa_ui_memory_allocation *allocation, int64_t offset,
    qa_ui_string_reference *value) {
  set_pointer(allocation, offset, value != NULL, PTR_STRING, value);
}

qa_ui_string_reference *qa_ui_memory_allocation_string_reference(
    qa_ui_memory_allocation *allocation) {
  return reference_new(allocation->storage, allocation->offset,
                       allocation->pointers);
}

char *qa_ui_memory_allocation_string_array_get(
    qa_ui_memory_allocation *allocation, int64_t offset, int64_t index) {
  check_word(allocation, offset + index * 4);
  return qa_ui_memory_allocation_get_string(allocation,
                                            offset + index * 4);
}
void qa_ui_memory_allocation_string_array_set(
    qa_ui_memory_allocation *allocation, int64_t offset, int64_t index,
    const char *value) {
  check_word(allocation, offset + index * 4);
  qa_ui_memory_allocation_set_string_text(allocation, offset + index * 4,
                                          value);
}

void qa_ui_memory_allocation_write_string(
    qa_ui_memory_allocation *allocation, const char *text) {
  size_t length = strlen(text);
  if ((int64_t)length >= allocation->size)
    qa_range_error("UI string copy exceeds its allocation");
  discard_pointers(allocation, 0, (int64_t)length + 1);
  memcpy(allocation->storage->bytes + allocation->offset, text, length);
  allocation->storage->bytes[allocation->offset + (int64_t)length] = 0;
}

void qa_ui_memory_allocation_set_allocation_pointer(
    qa_ui_memory_allocation *allocation, int64_t offset,
    qa_ui_memory_allocation *value) {
  set_pointer(allocation, offset, value != NULL, PTR_ALLOCATION, value);
}

bool qa_ui_memory_allocation_is_null_pointer(
    qa_ui_memory_allocation *allocation, int64_t offset) {
  check_word(allocation, offset);
  ptr_entry *pointer =
      map_get(allocation->pointers, allocation->offset + offset);
  if (pointer == NULL
      || (pointer->kind == PTR_RESOURCE && pointer->has_handle)) {
    check_numeric(allocation, offset);
    return read_u32(allocation, offset) == 0;
  }
  return false;
}

qa_ui_memory_allocation *qa_ui_memory_allocation_get_allocation_pointer(
    qa_ui_memory_allocation *allocation, int64_t offset) {
  ptr_entry *pointer = pointer_at(allocation, offset);
  if (pointer == NULL || pointer->kind == PTR_ALLOCATION)
    return pointer == NULL ? NULL : pointer->value;
  qa_ui_script_plain_error("UI allocation read aliases a different typed pointer");
}

static void *get_typed(qa_ui_memory_allocation *allocation, int64_t offset,
                       ptr_kind kind, const char *error) {
  ptr_entry *pointer = pointer_at(allocation, offset);
  if (pointer == NULL || pointer->kind == kind)
    return pointer == NULL ? NULL : pointer->value;
  qa_ui_script_plain_error("%s", error);
}

void *qa_ui_memory_allocation_get_script(qa_ui_memory_allocation *allocation,
                                         int64_t offset) {
  return get_typed(allocation, offset, PTR_SCRIPT,
                   "UI script read aliases a different typed pointer");
}
void qa_ui_memory_allocation_set_script(qa_ui_memory_allocation *allocation,
                                        int64_t offset, void *value) {
  set_pointer(allocation, offset, value != NULL, PTR_SCRIPT, value);
}
void *qa_ui_memory_allocation_get_item(qa_ui_memory_allocation *allocation,
                                       int64_t offset) {
  return get_typed(allocation, offset, PTR_ITEM,
                   "UI item read aliases a different typed pointer");
}
void qa_ui_memory_allocation_set_item(qa_ui_memory_allocation *allocation,
                                      int64_t offset, void *value) {
  set_pointer(allocation, offset, value != NULL, PTR_ITEM, value);
}
void *qa_ui_memory_allocation_get_menu(qa_ui_memory_allocation *allocation,
                                       int64_t offset) {
  return get_typed(allocation, offset, PTR_MENU,
                   "UI menu read aliases a different typed pointer");
}
void qa_ui_memory_allocation_set_menu(qa_ui_memory_allocation *allocation,
                                      int64_t offset, void *value) {
  set_pointer(allocation, offset, value != NULL, PTR_MENU, value);
}

void *qa_ui_memory_allocation_get_resource(
    qa_ui_memory_allocation *allocation, int64_t offset) {
  check_word(allocation, offset);
  ptr_entry *pointer =
      map_get(allocation->pointers, allocation->offset + offset);
  if (pointer != NULL && pointer->kind == PTR_RESOURCE)
    return pointer->value;
  if (pointer == NULL) {
    check_numeric(allocation, offset);
    return NULL;
  }
  qa_ui_script_plain_error("UI resource handle read aliases a different typed pointer");
}

bool qa_ui_memory_allocation_get_resource_handle(
    qa_ui_memory_allocation *allocation, int64_t offset, int32_t *out) {
  check_word(allocation, offset);
  ptr_entry *pointer =
      map_get(allocation->pointers, allocation->offset + offset);
  if (pointer != NULL && pointer->kind == PTR_RESOURCE
      && !pointer->has_handle)
    return false;
  *out = qa_ui_memory_allocation_get_int32(allocation, offset);
  return true;
}

void qa_ui_memory_allocation_set_resource(
    qa_ui_memory_allocation *allocation, int64_t offset, void *value,
    bool has_handle, int32_t handle) {
  qa_ui_memory_allocation_set_int32(allocation, offset,
                                    has_handle ? handle : 0);
  map_set(allocation->pointers, allocation->offset + offset, PTR_RESOURCE,
          value, has_handle, handle);
}

/* ---- TeamArenaUiMemory ---- */

#define BUCKET_COUNT 2048

struct qa_ui_team_arena_memory {
  char *profile;
  qa_ui_team_arena_print_fn print;
  void *print_ctx;
  qa_ui_team_arena_module module;
  byte_storage *bytes;
  byte_storage *strings;
  ptr_map *pointers;
  qa_ui_memory_allocation *menus;
  qa_ui_memory_allocation *buckets[BUCKET_COUNT];
  qa_ui_memory_allocation **owned_handles; /* stringDef borrows */
  size_t owned_count, owned_cap;
  qa_ui_string_reference *empty_string;
  int64_t string_point;
  int64_t allocation_point;
  bool exhausted;
};

qa_ui_team_arena_memory *qa_ui_team_arena_memory_create(
    const char *profile, qa_ui_team_arena_print_fn print, void *print_ctx,
    qa_ui_team_arena_module module) {
  qa_ui_team_arena_memory *memory = calloc(1, sizeof *memory);
  if (memory == NULL) return NULL;
  bool cgame = module == QA_UI_TEAM_ARENA_MODULE_CGAME;
  memory->profile = malloc(strlen(profile == NULL ? "qvm32" : profile) + 1);
  if (memory->profile != NULL)
    strcpy(memory->profile, profile == NULL ? "qvm32" : profile);
  memory->print = print;
  memory->print_ctx = print_ctx;
  memory->module = module;
  memory->bytes = storage_new(cgame ? 128 * 1024 : 1024 * 1024);
  memory->strings = storage_new(cgame ? 128 * 1024 : 384 * 1024);
  memory->pointers = map_new();
  memory->menus = qa_ui_memory_allocation_zeroed(64 * 644);
  memory->empty_string = qa_ui_string_reference_literal("");
  if (memory->profile == NULL || memory->bytes == NULL
      || memory->strings == NULL || memory->pointers == NULL
      || memory->menus == NULL || memory->empty_string == NULL) {
    qa_ui_team_arena_memory_destroy(memory);
    return NULL;
  }
  return memory;
}

void qa_ui_team_arena_memory_destroy(qa_ui_team_arena_memory *memory) {
  if (memory == NULL) return;
  for (size_t i = 0; i < memory->owned_count; i++)
    qa_ui_memory_allocation_destroy(memory->owned_handles[i]);
  free(memory->owned_handles);
  qa_ui_memory_allocation_destroy(memory->menus);
  qa_ui_string_reference_release(memory->empty_string);
  storage_release(memory->bytes);
  storage_release(memory->strings);
  map_release(memory->pointers);
  free(memory->profile);
  free(memory);
}

const char *qa_ui_team_arena_memory_profile(
    const qa_ui_team_arena_memory *memory) {
  return memory->profile;
}
qa_ui_team_arena_module qa_ui_team_arena_memory_module(
    const qa_ui_team_arena_memory *memory) {
  return memory->module;
}
int64_t qa_ui_team_arena_memory_string_bytes(
    const qa_ui_team_arena_memory *memory) {
  return memory->string_point;
}
int64_t qa_ui_team_arena_memory_allocated_bytes(
    const qa_ui_team_arena_memory *memory) {
  return memory->allocation_point;
}
bool qa_ui_team_arena_memory_out_of_memory(
    const qa_ui_team_arena_memory *memory) {
  return memory->exhausted;
}

void qa_ui_team_arena_memory_initialize_memory(
    qa_ui_team_arena_memory *memory) {
  memory->allocation_point = 0;
  memory->exhausted = false;
}

qa_ui_memory_allocation *qa_ui_team_arena_memory_borrow(
    qa_ui_team_arena_memory *memory, int64_t offset, int64_t size) {
  return allocation_new(memory->bytes, memory->pointers, offset, size);
}

qa_ui_memory_allocation *qa_ui_team_arena_memory_menu_record(
    qa_ui_team_arena_memory *memory, int64_t index) {
  if (index < 0 || index >= 64)
    qa_range_error("UI menu index exceeds the source static array");
  return qa_ui_memory_allocation_subrecord(memory->menus, index * 644, 644);
}

static void print_text(qa_ui_team_arena_memory *memory, const char *text) {
  memory->print(memory->print_ctx, text);
}

void qa_ui_team_arena_memory_report(qa_ui_team_arena_memory *memory) {
  print_text(memory, "Memory/String Pool Info\n");
  print_text(memory, "----------------\n");
  /* Math.fround staging from the TS. */
  float string_percent =
      (float)((float)((float)memory->string_point
                      / (double)memory->strings->length)
              * 100.0);
  qa_q3_game_format_arg string_args[3] = {
      qa_q3_game_format_number(string_percent),
      qa_q3_game_format_number((double)memory->string_point),
      qa_q3_game_format_number((double)memory->strings->length)};
  char *line = qa_q3_game_format(
      "String Pool is %.1f%% full, %i bytes out of %i used.\n", string_args,
      3, 32000);
  if (line == NULL) qa_range_error("UI memory allocation failed");
  print_text(memory, line);
  free(line);
  float memory_percent =
      (float)((float)((float)memory->allocation_point
                      / (double)memory->bytes->length)
              * 100.0);
  qa_q3_game_format_arg memory_args[3] = {
      qa_q3_game_format_number(memory_percent),
      qa_q3_game_format_number((double)memory->allocation_point),
      qa_q3_game_format_number((double)memory->bytes->length)};
  line = qa_q3_game_format(
      "Memory Pool is %.1f%% full, %i bytes out of %i used.\n", memory_args,
      3, 32000);
  if (line == NULL) qa_range_error("UI memory allocation failed");
  print_text(memory, line);
  free(line);
}

void qa_ui_team_arena_memory_initialize_strings(
    qa_ui_team_arena_memory *memory) {
  for (int i = 0; i < BUCKET_COUNT; i++) memory->buckets[i] = NULL;
  memory->string_point = 0;
  qa_ui_team_arena_memory_initialize_memory(memory);
}

bool qa_ui_team_arena_memory_allocate(qa_ui_team_arena_memory *memory,
                                      int64_t size, int64_t *out_offset) {
  if (size < 0 || size > 0x7fffffff - 15)
    qa_range_error("UI_Alloc size is outside the source allocation range");
  if (memory->allocation_point + size > (int64_t)memory->bytes->length) {
    memory->exhausted = true;
    print_text(memory, "UI_Alloc: Failure. Out of memory!\n");
    return false;
  }
  *out_offset = memory->allocation_point;
  memory->allocation_point += (size + 15) & ~15LL;
  return true;
}

char *qa_ui_team_arena_memory_string_alloc(qa_ui_team_arena_memory *memory,
                                           const char *input) {
  qa_ui_string_reference *reference =
      qa_ui_team_arena_memory_string_alloc_reference(memory, input);
  return reference == NULL ? NULL : qa_ui_string_reference_read(reference);
}

qa_ui_string_reference *qa_ui_team_arena_memory_string_alloc_reference(
    qa_ui_team_arena_memory *memory, const char *input) {
  if (input == NULL) return NULL;
  size_t length = strlen(input);
  if (length == 0) return memory->empty_string;
  int32_t hash = 0;
  for (size_t index = 0; index < length; index++) {
    int32_t byte = (uint8_t)input[index];
    if (byte >= 65 && byte <= 90) byte += 32;
    if (byte >= 128) byte -= 256;
    hash = (int32_t)((uint32_t)hash
                     + (uint32_t)byte * (uint32_t)(index + 119));
  }
  hash &= 2047;
  qa_ui_memory_allocation *first = memory->buckets[hash];
  qa_ui_memory_allocation *current = first;
  while (current != NULL) {
    qa_ui_string_reference *reference =
        qa_ui_memory_allocation_get_string_reference(current, 4);
    if (reference == NULL)
      qa_range_error("String_Alloc compares a NULL stringDef_t string");
    char *existing = qa_ui_string_reference_read(reference);
    bool same = existing != NULL && strcmp(existing, input) == 0;
    free(existing);
    if (same) return reference;
    current = qa_ui_memory_allocation_get_allocation_pointer(current, 0);
  }
  if ((int64_t)length + memory->string_point + 1
      >= (int64_t)memory->strings->length)
    return NULL;
  qa_ui_string_reference *reference =
      reference_new(memory->strings, memory->string_point, NULL);
  if (reference == NULL) qa_range_error("UI memory allocation failed");
  memcpy(memory->strings->bytes + memory->string_point, input, length);
  memory->strings->bytes[memory->string_point + (int64_t)length] = 0;
  memory->string_point += (int64_t)length + 1;
  qa_ui_memory_allocation *last = first;
  current = first;
  while (current != NULL
         && qa_ui_memory_allocation_get_allocation_pointer(current, 0)
                != NULL) {
    last = current;
    current = qa_ui_memory_allocation_get_allocation_pointer(current, 0);
  }
  int64_t offset;
  if (!qa_ui_team_arena_memory_allocate(memory, 8, &offset))
    qa_range_error("String_Alloc dereferences a failed UI_Alloc stringDef_t");
  qa_ui_memory_allocation *allocation =
      qa_ui_team_arena_memory_borrow(memory, offset, 8);
  if (allocation == NULL) qa_range_error("UI memory allocation failed");
  if (memory->owned_count == memory->owned_cap) {
    size_t cap = memory->owned_cap == 0 ? 8 : memory->owned_cap * 2;
    qa_ui_memory_allocation **grown =
        realloc(memory->owned_handles, cap * sizeof *grown);
    if (grown == NULL) qa_range_error("UI memory allocation failed");
    memory->owned_handles = grown;
    memory->owned_cap = cap;
  }
  memory->owned_handles[memory->owned_count++] = allocation;
  qa_ui_memory_allocation_set_allocation_pointer(allocation, 0, NULL);
  qa_ui_memory_allocation_set_string_reference(allocation, 4, reference);
  qa_ui_string_reference_release(reference); /* the map retains it */
  qa_ui_string_reference *stored =
      qa_ui_memory_allocation_get_string_reference(allocation, 4);
  /* The source tracks the penultimate node here. */
  if (last != NULL)
    qa_ui_memory_allocation_set_allocation_pointer(last, 0, allocation);
  else memory->buckets[hash] = allocation;
  return stored;
}
