/*
 * Port of src/ui/common/legacy/script/memory.ts: ScriptMemory,
 * ScriptMemoryAllocation, capture/restore interfaces and
 * SourceScriptStorage (botlib/l_script.c script_t byte layout).
 * C accommodations:
 *  - ScriptMemory is a fn+ctx vtable; `kind` is always "heap" in the
 *    TS type, so it is not passed. A NULL memory selects malloc/free
 *    (the TS `new Uint8Array` fallback owners use).
 *  - Allocation results are returned by value; false is OOM only.
 *    Invalid arguments abort via qa_range_error with the TS message;
 *    plain TS `Error` throws abort printing "Error: <message>".
 *  - Capture/restore use qa_save_value trees (persistence/value.h);
 *    capture's reference() and restore's allocation() are fn+ctx.
 *  - String getters malloc (NULL only on OOM); text is bytes (the TS
 *    charCode > 255 rejections are inherent to C byte strings).
 *  - dispose() mirrors the TS frees; destroy() also frees the C
 *    struct (disposing first if needed, where TS relies on GC).
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_LEGACY_SCRIPT_MEMORY_H
#define QA_UI_COMMON_LEGACY_SCRIPT_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/persistence/value.h"

#define QA_UI_SOURCE_SCRIPT_BYTES 2148
#define QA_UI_SOURCE_PUNCTUATION_TABLE_BYTES (256 * 4)

/** TS ScriptMemoryAllocation. `record` is the owner's bookkeeping. */
typedef struct qa_ui_script_allocation {
  uint8_t *bytes;
  size_t length;
  void *record;
} qa_ui_script_allocation;

/** TS ScriptMemory (fn+ctx). allocate returns false only on OOM. */
typedef struct qa_ui_script_memory {
  void *ctx;
  bool (*allocate)(void *ctx, size_t size, bool clear,
                   qa_ui_script_allocation *out);
  void (*free_allocation)(void *ctx, qa_ui_script_allocation *allocation);
} qa_ui_script_memory;

/** TS ScriptMemoryCapture. */
typedef struct qa_ui_script_memory_capture {
  void *ctx;
  int64_t (*reference)(void *ctx, qa_ui_script_allocation *allocation);
} qa_ui_script_memory_capture;

/** TS ScriptMemoryRestore. */
typedef struct qa_ui_script_memory_restore {
  void *ctx;
  qa_ui_script_allocation (*allocation)(void *ctx, int64_t id);
} qa_ui_script_memory_restore;

/* NULL memory selects malloc/calloc. False only on OOM. */
bool qa_ui_script_memory_allocate(const qa_ui_script_memory *memory,
                                  size_t size, bool clear,
                                  qa_ui_script_allocation *out);
void qa_ui_script_memory_free(const qa_ui_script_memory *memory,
                              qa_ui_script_allocation *allocation);

typedef struct qa_ui_source_token_memory qa_ui_source_token_memory;
typedef struct qa_ui_source_script_storage qa_ui_source_script_storage;

/* TS SourceScriptStorage.allocate; NULL only on OOM. `path` is bytes. */
qa_ui_source_script_storage *qa_ui_source_script_allocate(
    int64_t length, const char *path, const qa_ui_script_memory *memory);
qa_ui_source_script_storage *qa_ui_source_script_restore_save_state(
    const qa_save_value *value, const qa_ui_script_memory *memory,
    const qa_ui_script_memory_restore *restore);
/* Owned checkpoint tree; NULL only on OOM. */
qa_save_value *qa_ui_source_script_capture_save_state(
    qa_ui_source_script_storage *script,
    const qa_ui_script_memory_capture *capture);
void qa_ui_source_script_dispose(qa_ui_source_script_storage *script);
void qa_ui_source_script_destroy(qa_ui_source_script_storage *script);

const qa_ui_script_memory *qa_ui_source_script_memory(
    const qa_ui_source_script_storage *script);
qa_ui_source_token_memory *qa_ui_source_script_token(
    qa_ui_source_script_storage *script);
/* The full allocation bytes (TS private bytes; module-internal use). */
uint8_t *qa_ui_source_script_bytes(qa_ui_source_script_storage *script,
                                   size_t *out_length);

void qa_ui_source_script_set_default_punctuations(
    qa_ui_source_script_storage *script, const uint32_t *heads,
    size_t count);
/* Malloc'd; NULL only on OOM. */
char *qa_ui_source_script_path(qa_ui_source_script_storage *script);
char *qa_ui_source_script_text(qa_ui_source_script_storage *script);
/* TS buffer getter: validated slice; the NUL byte is excluded. */
uint8_t *qa_ui_source_script_buffer(qa_ui_source_script_storage *script,
                                    size_t *out_length);

int32_t qa_ui_source_script_length(qa_ui_source_script_storage *script);
uint32_t qa_ui_source_script_next_script(qa_ui_source_script_storage *script);
void qa_ui_source_script_set_next_script(qa_ui_source_script_storage *script,
                                         uint32_t value);
int64_t qa_ui_source_script_offset(qa_ui_source_script_storage *script);
void qa_ui_source_script_set_offset(qa_ui_source_script_storage *script,
                                    int64_t value);
int64_t qa_ui_source_script_last_offset(qa_ui_source_script_storage *script);
int64_t qa_ui_source_script_lines_crossed(qa_ui_source_script_storage *script);
int64_t qa_ui_source_script_next_whitespace_char(
    qa_ui_source_script_storage *script);
int32_t qa_ui_source_script_line(qa_ui_source_script_storage *script);
void qa_ui_source_script_set_line(qa_ui_source_script_storage *script,
                                  int32_t value);
int32_t qa_ui_source_script_flags(qa_ui_source_script_storage *script);
void qa_ui_source_script_set_flags(qa_ui_source_script_storage *script,
                                   int32_t value);
bool qa_ui_source_script_token_available(qa_ui_source_script_storage *script);
void qa_ui_source_script_set_token_available(
    qa_ui_source_script_storage *script, bool value);

void qa_ui_source_script_begin_token(qa_ui_source_script_storage *script);
void qa_ui_source_script_end_whitespace(qa_ui_source_script_storage *script);
uint32_t qa_ui_source_script_punctuation_head(
    qa_ui_source_script_storage *script, int64_t character);
/* `text` length must equal the allocated buffer length. */
void qa_ui_source_script_copy_text(qa_ui_source_script_storage *script,
                                   const uint8_t *text, size_t length);
void qa_ui_source_script_compress(qa_ui_source_script_storage *script);
void qa_ui_source_script_reset(qa_ui_source_script_storage *script);

#endif
