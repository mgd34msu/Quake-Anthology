/*
 * Port of src/bots/behavior/library/libvars.ts (botlib/l_libvar.c).
 * C accommodations: BotLibVars owns a default BotMemory (TS `new BotMemory()`)
 * unless initialized with qa_bot_lib_vars_init_memory; destroy releases only C
 * bookkeeping and the owned memory, because BotMemory owns every allocation
 * (TS GC). qa_bot_lib_var is the TS live BotLibVar view; a handle stays
 * readable until destroy and aborts once its allocation was cleared.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_LIBVARS_H
#define QA_BOTS_BEHAVIOR_LIBRARY_LIBVARS_H
#include "qa/bots/behavior/library/memory.h"
#include "qa/persistence/value.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_bot_lib_var qa_bot_lib_var;
typedef struct qa_bot_lib_string_pointer {
  bool live;
  qa_bot_memory_allocation allocation;
  int64_t offset;
} qa_bot_lib_string_pointer;
typedef struct qa_bot_lib_vars {
  qa_bot_memory owned_memory;
  qa_bot_memory *memory;
  qa_bot_lib_var **records; /* indexed by pointer ID; NULL is absent */
  size_t record_capacity;
  qa_bot_lib_string_pointer *strings; /* indexed by pointer ID */
  size_t string_capacity;
  qa_bot_lib_var **retired; /* cleared views kept readable-as-freed until destroy */
  size_t retired_count, retired_capacity;
  int64_t next_pointer;
  uint32_t head;
} qa_bot_lib_vars;
/** TS (index) => byte reader over a borrowed source name. */
typedef int64_t (*qa_bot_lib_name_byte_fn)(void *ctx, int64_t index);
/** TS () => string reader; the returned text is borrowed for the call. */
typedef const char *(*qa_bot_lib_text_fn)(void *ctx);

float qa_lib_var_string_value(const char *text);
void qa_bot_lib_vars_init(qa_bot_lib_vars *v);
void qa_bot_lib_vars_init_memory(qa_bot_lib_vars *v, qa_bot_memory *memory);
void qa_bot_lib_vars_destroy(qa_bot_lib_vars *v);
const char *qa_bot_lib_var_name(const qa_bot_lib_var *variable);
const char *qa_bot_lib_var_string(const qa_bot_lib_var *variable);
int32_t qa_bot_lib_var_flags(const qa_bot_lib_var *variable);
bool qa_bot_lib_var_modified(const qa_bot_lib_var *variable);
float qa_bot_lib_var_value(const qa_bot_lib_var *variable);
qa_bot_lib_var *qa_bot_lib_vars_get(qa_bot_lib_vars *v, const char *name);
const char *qa_bot_lib_vars_get_string(qa_bot_lib_vars *v, const char *name);
/* read_byte NULL is the TS null reader. */
const char *qa_bot_lib_vars_get_string_by_name_bytes(qa_bot_lib_vars *v,
                                                     qa_bot_lib_name_byte_fn read_byte, void *ctx);
float qa_bot_lib_vars_get_value(qa_bot_lib_vars *v, const char *name);
qa_bot_lib_var *qa_bot_lib_vars_get_or_create(qa_bot_lib_vars *v, const char *name,
                                              const char *def);
const char *qa_bot_lib_vars_string(qa_bot_lib_vars *v, const char *name, const char *def);
float qa_bot_lib_vars_value(qa_bot_lib_vars *v, const char *name, const char *def);
void qa_bot_lib_vars_set(qa_bot_lib_vars *v, const char *name, const char *value);
void qa_bot_lib_vars_set_by_name_bytes(qa_bot_lib_vars *v, qa_bot_lib_name_byte_fn read_byte,
                                       void *byte_ctx, qa_bot_lib_text_fn read_name, void *name_ctx,
                                       qa_bot_lib_text_fn read_value, void *value_ctx);
bool qa_bot_lib_vars_changed(qa_bot_lib_vars *v, const char *name);
void qa_bot_lib_vars_set_not_modified(qa_bot_lib_vars *v, const char *name);
void qa_bot_lib_vars_clear(qa_bot_lib_vars *v);
/* Owned checkpoint tree. */
qa_save_value *qa_bot_lib_vars_checkpoint(qa_bot_lib_vars *v, qa_bot_memory_capture *memory);
void qa_bot_lib_vars_restore(qa_bot_lib_vars *v, const qa_save_value *value,
                             qa_bot_memory_restored *memory);
int64_t qa_bot_lib_vars_reference(qa_bot_lib_vars *v, const qa_bot_lib_var *variable);
qa_bot_lib_var *qa_bot_lib_vars_resolve(qa_bot_lib_vars *v, int64_t pointer);
#endif
