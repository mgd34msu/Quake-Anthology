/*
 * Port of src/ui/common/legacy/team-arena/memory.ts: UI_Alloc,
 * UI_InitMemory, String_Alloc and String_Report (ui_shared.c).
 * C accommodations:
 *  - The typed-pointer kinds hold the real menu.ts types from
 *    include/qa/ui/common/legacy/menu.h (forward-declared here to
 *    avoid an include cycle) with the same alias checks and error
 *    strings.
 *  - stringArray's live getter/setter array becomes indexed
 *    get/set functions. String reads malloc (NULL only on OOM, or
 *    the TS undefined/null where the function says so).
 *  - Plain TS `Error` alias throws abort printing "Error: <msg>";
 *    RangeErrors abort via qa_range_error.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_LEGACY_TEAM_ARENA_MEMORY_H
#define QA_UI_COMMON_LEGACY_TEAM_ARENA_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** TS UiStringReference. */
typedef struct qa_ui_string_reference qa_ui_string_reference;

/* NULL only on OOM. */
qa_ui_string_reference *qa_ui_string_reference_literal(const char *text);
qa_ui_string_reference *qa_ui_string_reference_retain(
    qa_ui_string_reference *reference);
void qa_ui_string_reference_release(qa_ui_string_reference *reference);
int64_t qa_ui_string_reference_offset(
    const qa_ui_string_reference *reference);
/* Malloc'd read; NULL only on OOM. */
char *qa_ui_string_reference_read(qa_ui_string_reference *reference);

/** TS UiMemoryAllocation (heap handle over shared byte storage). */
typedef struct qa_ui_memory_allocation qa_ui_memory_allocation;

/* NULL only on OOM. */
qa_ui_memory_allocation *qa_ui_memory_allocation_zeroed(int64_t size);
/* Frees the handle only; shared storage outlives handles. */
void qa_ui_memory_allocation_destroy(qa_ui_memory_allocation *allocation);
int64_t qa_ui_memory_allocation_offset(
    const qa_ui_memory_allocation *allocation);
int64_t qa_ui_memory_allocation_size(
    const qa_ui_memory_allocation *allocation);

qa_ui_memory_allocation *qa_ui_memory_allocation_subrecord(
    qa_ui_memory_allocation *allocation, int64_t offset, int64_t size);
qa_ui_memory_allocation *qa_ui_memory_allocation_dereference(
    qa_ui_memory_allocation *allocation, int64_t size);
void qa_ui_memory_allocation_clear(qa_ui_memory_allocation *allocation);

int32_t qa_ui_memory_allocation_get_int32(qa_ui_memory_allocation *allocation,
                                          int64_t offset);
/* DataView float32: stored binary32 read back as double. */
double qa_ui_memory_allocation_get_float32(
    qa_ui_memory_allocation *allocation, int64_t offset);
void qa_ui_memory_allocation_set_int32(qa_ui_memory_allocation *allocation,
                                       int64_t offset, int32_t value);
void qa_ui_memory_allocation_set_float32(qa_ui_memory_allocation *allocation,
                                         int64_t offset, double value);

/* Malloc'd; NULL is OOM or TS undefined (no string pointer). */
char *qa_ui_memory_allocation_get_string(qa_ui_memory_allocation *allocation,
                                         int64_t offset);
/* Borrowed reference; NULL is TS undefined. */
qa_ui_string_reference *qa_ui_memory_allocation_get_string_reference(
    qa_ui_memory_allocation *allocation, int64_t offset);
/* NULL text/reference clears the pointer (TS undefined). */
void qa_ui_memory_allocation_set_string_text(
    qa_ui_memory_allocation *allocation, int64_t offset, const char *value);
void qa_ui_memory_allocation_set_string_reference(
    qa_ui_memory_allocation *allocation, int64_t offset,
    qa_ui_string_reference *value);
/* New reference over this allocation's bytes (caller releases). */
qa_ui_string_reference *qa_ui_memory_allocation_string_reference(
    qa_ui_memory_allocation *allocation);
/* stringArray element access; NULL string is TS null. */
char *qa_ui_memory_allocation_string_array_get(
    qa_ui_memory_allocation *allocation, int64_t offset, int64_t index);
void qa_ui_memory_allocation_string_array_set(
    qa_ui_memory_allocation *allocation, int64_t offset, int64_t index,
    const char *value);
void qa_ui_memory_allocation_write_string(
    qa_ui_memory_allocation *allocation, const char *text);

void qa_ui_memory_allocation_set_allocation_pointer(
    qa_ui_memory_allocation *allocation, int64_t offset,
    qa_ui_memory_allocation *value);
bool qa_ui_memory_allocation_is_null_pointer(
    qa_ui_memory_allocation *allocation, int64_t offset);
/* Borrowed; NULL is TS undefined. */
qa_ui_memory_allocation *qa_ui_memory_allocation_get_allocation_pointer(
    qa_ui_memory_allocation *allocation, int64_t offset);

/* menu.ts typed pointers (real types in legacy/menu.h). */
typedef struct qa_ui_menu_script qa_ui_menu_script;
typedef struct qa_ui_item_definition qa_ui_item_definition;
typedef struct qa_ui_menu_definition qa_ui_menu_definition;
typedef struct qa_ui_resource_reference qa_ui_resource_reference;

qa_ui_menu_script *qa_ui_memory_allocation_get_script(
    qa_ui_memory_allocation *allocation, int64_t offset);
void qa_ui_memory_allocation_set_script(qa_ui_memory_allocation *allocation,
                                        int64_t offset,
                                        qa_ui_menu_script *value);
qa_ui_item_definition *qa_ui_memory_allocation_get_item(
    qa_ui_memory_allocation *allocation, int64_t offset);
void qa_ui_memory_allocation_set_item(qa_ui_memory_allocation *allocation,
                                      int64_t offset,
                                      qa_ui_item_definition *value);
qa_ui_menu_definition *qa_ui_memory_allocation_get_menu(
    qa_ui_memory_allocation *allocation, int64_t offset);
void qa_ui_memory_allocation_set_menu(qa_ui_memory_allocation *allocation,
                                      int64_t offset,
                                      qa_ui_menu_definition *value);
qa_ui_resource_reference *qa_ui_memory_allocation_get_resource(
    qa_ui_memory_allocation *allocation, int64_t offset);
/* False is TS undefined handle. */
bool qa_ui_memory_allocation_get_resource_handle(
    qa_ui_memory_allocation *allocation, int64_t offset, int32_t *out);
void qa_ui_memory_allocation_set_resource(
    qa_ui_memory_allocation *allocation, int64_t offset,
    qa_ui_resource_reference *value, bool has_handle, int32_t handle);

/** TS (text) => void print callback. */
typedef void (*qa_ui_team_arena_print_fn)(void *ctx, const char *text);

/** TS TeamArenaUiMemory module selector. */
typedef enum qa_ui_team_arena_module {
  QA_UI_TEAM_ARENA_MODULE_UI,
  QA_UI_TEAM_ARENA_MODULE_CGAME
} qa_ui_team_arena_module;

typedef struct qa_ui_team_arena_memory qa_ui_team_arena_memory;

/* `profile` is the TS "qvm32" literal (stored, unused). NULL: OOM. */
qa_ui_team_arena_memory *qa_ui_team_arena_memory_create(
    const char *profile, qa_ui_team_arena_print_fn print, void *print_ctx,
    qa_ui_team_arena_module module);
void qa_ui_team_arena_memory_destroy(qa_ui_team_arena_memory *memory);
const char *qa_ui_team_arena_memory_profile(
    const qa_ui_team_arena_memory *memory);
qa_ui_team_arena_module qa_ui_team_arena_memory_module(
    const qa_ui_team_arena_memory *memory);

int64_t qa_ui_team_arena_memory_string_bytes(
    const qa_ui_team_arena_memory *memory);
int64_t qa_ui_team_arena_memory_allocated_bytes(
    const qa_ui_team_arena_memory *memory);
bool qa_ui_team_arena_memory_out_of_memory(
    const qa_ui_team_arena_memory *memory);

void qa_ui_team_arena_memory_initialize_memory(
    qa_ui_team_arena_memory *memory);
/* Borrow handle over the pool; destroy with the allocation destroy. */
qa_ui_memory_allocation *qa_ui_team_arena_memory_borrow(
    qa_ui_team_arena_memory *memory, int64_t offset, int64_t size);
qa_ui_memory_allocation *qa_ui_team_arena_memory_menu_record(
    qa_ui_team_arena_memory *memory, int64_t index);
void qa_ui_team_arena_memory_report(qa_ui_team_arena_memory *memory);
void qa_ui_team_arena_memory_initialize_strings(
    qa_ui_team_arena_memory *memory);
/* False is TS null (out of memory); *out_offset set on true. */
bool qa_ui_team_arena_memory_allocate(qa_ui_team_arena_memory *memory,
                                      int64_t size, int64_t *out_offset);
/* Malloc'd; NULL is TS null or OOM. NULL input is TS null. */
char *qa_ui_team_arena_memory_string_alloc(qa_ui_team_arena_memory *memory,
                                           const char *input);
/* Borrowed reference; NULL is TS null. NULL input is TS null. */
qa_ui_string_reference *qa_ui_team_arena_memory_string_alloc_reference(
    qa_ui_team_arena_memory *memory, const char *input);

#endif
