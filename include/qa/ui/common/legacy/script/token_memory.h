/*
 * Port of src/ui/common/legacy/script/token-memory.ts:
 * SourceTokenMemory over retained token_t bytes (l_script.h),
 * including the x87 long-double float profile. C accommodations:
 * the TS borrow closure is fn+ctx returning the byte span; a local
 * variant owns its 1068 bytes. Snapshot identity (TS WeakMap) is a
 * refcounted snapshot struct shared with the token it produced.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_LEGACY_SCRIPT_TOKEN_MEMORY_H
#define QA_UI_COMMON_LEGACY_SCRIPT_TOKEN_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/lexer.h"

#define QA_UI_SOURCE_TOKEN_BYTES 1068
#define QA_UI_SOURCE_TOKEN_STRING_BYTES 1024

/** TS SourceTokenContext (owned strings). */
typedef struct qa_ui_source_token_context {
  char *path;
  int64_t column;
  char *leading_whitespace;
  size_t leading_whitespace_length;
} qa_ui_source_token_context;

void qa_ui_source_token_context_destroy(qa_ui_source_token_context *context);
/* Copies; false only on OOM. */
bool qa_ui_source_token_context_set(qa_ui_source_token_context *context,
                                    const char *path, int64_t column,
                                    const char *leading_whitespace,
                                    size_t leading_whitespace_length);

/** TS () => Uint8Array borrow. */
typedef uint8_t *(*qa_ui_source_token_borrow_fn)(void *ctx,
                                                 size_t *out_length);

typedef struct qa_ui_source_token_memory qa_ui_source_token_memory;

/* NULL only on OOM. */
qa_ui_source_token_memory *qa_ui_source_token_memory_create(
    qa_ui_source_token_borrow_fn borrow, void *borrow_ctx);
qa_ui_source_token_memory *qa_ui_source_token_memory_create_local(void);
/* Fixed-span view (does not own `bytes`). */
qa_ui_source_token_memory *qa_ui_source_token_memory_create_view(
    uint8_t *bytes, size_t length);
void qa_ui_source_token_memory_destroy(qa_ui_source_token_memory *token);

qa_save_value *qa_ui_source_token_capture_save_state(
    qa_ui_source_token_memory *token);
void qa_ui_source_token_restore_save_state(qa_ui_source_token_memory *token,
                                           const qa_save_value *value,
                                           bool verify_bytes);
/* Validated 1068-byte span (TS bytes getter). */
uint8_t *qa_ui_source_token_bytes(qa_ui_source_token_memory *token);

void qa_ui_source_token_clear(qa_ui_source_token_memory *token);
void qa_ui_source_token_copy_from(qa_ui_source_token_memory *token,
                                  qa_ui_source_token_memory *other);

int32_t qa_ui_source_token_type(qa_ui_source_token_memory *token);
void qa_ui_source_token_set_type(qa_ui_source_token_memory *token,
                                 int32_t value);
int32_t qa_ui_source_token_subtype(qa_ui_source_token_memory *token);
void qa_ui_source_token_set_subtype(qa_ui_source_token_memory *token,
                                    int32_t value);
uint32_t qa_ui_source_token_integer_value(qa_ui_source_token_memory *token);
void qa_ui_source_token_set_integer_value(qa_ui_source_token_memory *token,
                                          uint32_t value);
double qa_ui_source_token_float_value(qa_ui_source_token_memory *token);
void qa_ui_source_token_set_float_value(qa_ui_source_token_memory *token,
                                        double value);
uint32_t qa_ui_source_token_whitespace_start(qa_ui_source_token_memory *token);
void qa_ui_source_token_set_whitespace_start(
    qa_ui_source_token_memory *token, uint32_t value);
uint32_t qa_ui_source_token_whitespace_end(qa_ui_source_token_memory *token);
void qa_ui_source_token_set_whitespace_end(qa_ui_source_token_memory *token,
                                           uint32_t value);
int32_t qa_ui_source_token_line(qa_ui_source_token_memory *token);
void qa_ui_source_token_set_line(qa_ui_source_token_memory *token,
                                 int32_t value);
int32_t qa_ui_source_token_lines_crossed(qa_ui_source_token_memory *token);
void qa_ui_source_token_set_lines_crossed(qa_ui_source_token_memory *token,
                                          int32_t value);
bool qa_ui_source_token_whitespace_before(qa_ui_source_token_memory *token);
void qa_ui_source_token_clear_whitespace(qa_ui_source_token_memory *token);
uint32_t qa_ui_source_token_next(qa_ui_source_token_memory *token);
void qa_ui_source_token_set_next(qa_ui_source_token_memory *token,
                                 uint32_t value);

/* TS string getter: NUL-terminated copy into out[1024]. */
void qa_ui_source_token_string(qa_ui_source_token_memory *token,
                               char out[QA_UI_SOURCE_TOKEN_STRING_BYTES]);
size_t qa_ui_source_token_string_length(qa_ui_source_token_memory *token);
/* Byte text (may embed NUL, setting the typed text extent). */
void qa_ui_source_token_write_string(qa_ui_source_token_memory *token,
                                     const char *text, size_t length);
void qa_ui_source_token_set_string_byte(qa_ui_source_token_memory *token,
                                        int64_t index, int64_t value);

void qa_ui_source_token_write_record(
    qa_ui_source_token_memory *token,
    const qa_ui_script_token_record *record);
void qa_ui_source_token_write_token(qa_ui_source_token_memory *token,
                                    qa_ui_script_token *value);
/* False only on OOM. `has_text_extent` is TS's optional argument. */
bool qa_ui_source_token_read_record(qa_ui_source_token_memory *token,
                                    const qa_ui_source_token_context *context,
                                    bool has_text_extent, int64_t text_extent,
                                    qa_ui_script_token_record *out);

/* Snapshot shared between a token memory and its produced token. */
qa_ui_script_token_snapshot *qa_ui_script_snapshot_retain(
    qa_ui_script_token_snapshot *snapshot);
void qa_ui_script_snapshot_release(qa_ui_script_token_snapshot *snapshot);

#endif
