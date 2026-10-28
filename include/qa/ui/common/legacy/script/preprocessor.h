/*
 * Port of src/ui/common/legacy/script/preprocessor.ts:
 * ScriptSourceReader, ScriptPreprocessor and ScriptGlobalDefines
 * (botlib/l_precomp.c). C accommodations:
 *  - Includes are synchronous fn+ctx (PORTING.md: async flows
 *    become synchronous); next_async runs the same read with the
 *    TS pending-read guard held during the call and invokes the
 *    validate callback before/after include resolution points.
 *    The "synchronous includes" rejection cannot occur.
 *  - ScriptSource text is bytes with explicit length; a root or
 *    included SourceScriptStorage is passed via the storage field
 *    (ownership moves to the reader, as the TS instanceof flow).
 *  - The `now` callback returns a broken-down local date (TS Date
 *    getters); NULL selects the current local time.
 *  - Uncaught ScriptLanguageError aborts (see lexer.h); the
 *    engine's internal catches (expect/readLine paths) ride the
 *    shared setjmp channel.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_LEGACY_SCRIPT_PREPROCESSOR_H
#define QA_UI_COMMON_LEGACY_SCRIPT_PREPROCESSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/lexer.h"
#include "qa/ui/common/legacy/script/memory.h"
#include "qa/ui/common/legacy/script/precomp_memory.h"
#include "qa/ui/common/legacy/script/token_memory.h"

/** TS ScriptSource; storage (when set) is the loaded source. */
typedef struct qa_ui_script_source {
  const char *path;
  const uint8_t *text;
  size_t text_length;
  qa_ui_source_script_storage *storage; /* owned by the reader once taken */
} qa_ui_script_source;

/** TS IncludeRequest kind. */
typedef enum qa_ui_script_include_kind {
  QA_UI_SCRIPT_INCLUDE_QUOTED,
  QA_UI_SCRIPT_INCLUDE_SYSTEM
} qa_ui_script_include_kind;

/** TS IncludeRequest. include_path NULL is the omitted field. */
typedef struct qa_ui_script_include_request {
  qa_ui_script_include_kind kind;
  const char *from_path;
  const char *requested_path;
  const char *include_path;
} qa_ui_script_include_request;

/** TS IncludeResolver/AsyncIncludeResolver. False is TS undefined;
 * out fields are borrowed only for the call (engine copies), except
 * out->storage whose ownership moves on true. */
typedef bool (*qa_ui_script_resolve_fn)(
    void *ctx, const qa_ui_script_include_request *request,
    qa_ui_script_source *out);

/** Broken-down local date for __DATE__/__TIME__ (TS Date getters). */
typedef struct qa_ui_script_date {
  int64_t year;
  int64_t month; /* 0-11 */
  int64_t day;   /* 1-31 */
  int64_t hour;
  int64_t minute;
  int64_t second;
} qa_ui_script_date;

typedef qa_ui_script_date (*qa_ui_script_now_fn)(void *ctx);
/** TS (text) => void sinks (debugEval, hash-table writes). */
typedef void (*qa_ui_script_write_fn)(void *ctx, const char *text);

typedef struct qa_ui_script_global_defines qa_ui_script_global_defines;

/** TS ScriptGlobalSnapshot. */
typedef struct qa_ui_script_global_snapshot {
  qa_ui_precomp_define **definitions; /* borrowed from the owner */
  size_t count;
  qa_ui_precomp_memory *owner; /* internal copy owner */
} qa_ui_script_global_snapshot;

void qa_ui_script_global_snapshot_destroy(
    qa_ui_script_global_snapshot *snapshot);

/** TS ScriptPreprocessorOptions. Zero/false means TS undefined
 * except where has_* says otherwise. */
typedef struct qa_ui_script_preprocessor_options {
  const qa_ui_script_memory *memory;
  const char *const *initial_defines;
  size_t initial_define_count;
  qa_ui_script_global_defines *globals;
  const qa_ui_script_global_snapshot *global_defines;
  bool install_builtins;
  qa_ui_script_now_fn now;
  void *now_ctx;
  int64_t max_include_depth;   /* 0 = TS undefined */
  int64_t max_macro_expansions;
  int64_t max_queued_tokens;
  int64_t max_output_tokens;
  int64_t max_source_tokens;
  int64_t max_defines;
  int64_t max_expression_tokens;
  qa_ui_script_report_fn report;
  void *report_ctx;
  qa_ui_script_write_fn debug_eval;
  void *debug_eval_ctx;
} qa_ui_script_preprocessor_options;

/** TS ScriptGlobalDefines. */
qa_ui_script_global_defines *qa_ui_script_global_defines_create(
    qa_ui_script_report_fn report, void *report_ctx,
    const qa_ui_script_memory *memory);
void qa_ui_script_global_defines_destroy(
    qa_ui_script_global_defines *globals);
qa_save_value *qa_ui_script_global_defines_capture_owned_save_state(
    qa_ui_script_global_defines *globals);
void qa_ui_script_global_defines_restore_owned_save_state(
    qa_ui_script_global_defines *globals, const qa_save_value *value);
qa_save_value *qa_ui_script_global_defines_capture_save_state(
    qa_ui_script_global_defines *globals,
    const qa_ui_script_memory_capture *capture);
void qa_ui_script_global_defines_restore_save_state(
    qa_ui_script_global_defines *globals, const qa_save_value *value,
    const qa_ui_script_memory_restore *restore);
const qa_ui_script_diagnostic *qa_ui_script_global_defines_diagnostics(
    const qa_ui_script_global_defines *globals, size_t *out_count);
bool qa_ui_script_global_defines_add(qa_ui_script_global_defines *globals,
                                     const char *text);
bool qa_ui_script_global_defines_remove(
    qa_ui_script_global_defines *globals, const char *name);
/* False only on OOM; destroy with the snapshot destroy above. */
bool qa_ui_script_global_defines_snapshot(
    qa_ui_script_global_defines *globals,
    qa_ui_script_global_snapshot *out);
/** TS copyTo publish callback. */
typedef void (*qa_ui_script_publish_define_fn)(
    void *ctx, qa_ui_precomp_define *define);
void qa_ui_script_global_defines_copy_to(
    qa_ui_script_global_defines *globals, qa_ui_precomp_memory *memory,
    qa_ui_script_publish_define_fn publish, void *publish_ctx);
void qa_ui_script_global_defines_clear(
    qa_ui_script_global_defines *globals);

typedef struct qa_ui_script_source_reader qa_ui_script_source_reader;

/* NULL only on OOM. resolver may be NULL (nothing resolves). */
qa_ui_script_source_reader *qa_ui_script_source_reader_open(
    const qa_ui_script_source *root, qa_ui_script_resolve_fn resolve,
    void *resolve_ctx, const qa_ui_script_preprocessor_options *options);
qa_ui_script_source_reader *qa_ui_script_source_reader_restore_save_state(
    const qa_save_value *value, qa_ui_script_resolve_fn resolve,
    void *resolve_ctx, const qa_ui_script_preprocessor_options *options,
    const qa_ui_script_memory_restore *restore);
qa_save_value *qa_ui_script_source_reader_capture_save_state(
    qa_ui_script_source_reader *reader,
    const qa_ui_script_memory_capture *capture);
void qa_ui_script_source_reader_dispose(qa_ui_script_source_reader *reader);
void qa_ui_script_source_reader_dispose_record_only(
    qa_ui_script_source_reader *reader);
void qa_ui_script_source_reader_destroy(qa_ui_script_source_reader *reader);

/* False is TS undefined; out holds one token reference. */
bool qa_ui_script_source_reader_next(qa_ui_script_source_reader *reader,
                                     qa_ui_script_token_record *out);
/** TS nextAsync(validate); validate may be NULL. */
typedef void (*qa_ui_script_validate_fn)(void *ctx);
bool qa_ui_script_source_reader_next_async(
    qa_ui_script_source_reader *reader, qa_ui_script_validate_fn validate,
    void *validate_ctx, qa_ui_script_token_record *out);
qa_ui_source_token_memory *qa_ui_script_source_reader_raw_token(
    qa_ui_script_source_reader *reader);
void qa_ui_script_source_reader_current_record(
    qa_ui_script_source_reader *reader, qa_ui_script_token_record *out);
void qa_ui_script_source_reader_unread(
    qa_ui_script_source_reader *reader,
    const qa_ui_script_token_record *record);
void qa_ui_script_source_reader_unread_last(
    qa_ui_script_source_reader *reader);
void qa_ui_script_source_reader_set_include_path(
    qa_ui_script_source_reader *reader, const char *path);
void qa_ui_script_source_reader_set_punctuations(
    qa_ui_script_source_reader *reader,
    const qa_ui_script_punctuation_spec *specs, size_t count);
bool qa_ui_script_source_reader_add_define(
    qa_ui_script_source_reader *reader, const char *text);
void qa_ui_script_source_reader_add_builtin_defines(
    qa_ui_script_source_reader *reader);
void qa_ui_script_source_reader_print_define_hash_table(
    qa_ui_script_source_reader *reader, qa_ui_script_write_fn write,
    void *write_ctx);
bool qa_ui_script_source_reader_expect_token_string(
    qa_ui_script_source_reader *reader, const char *text);
bool qa_ui_script_source_reader_expect_any_token(
    qa_ui_script_source_reader *reader, qa_ui_source_token_memory *output);
bool qa_ui_script_source_reader_expect_token_type(
    qa_ui_script_source_reader *reader, int64_t type, int64_t subtype,
    qa_ui_source_token_memory *output);
bool qa_ui_script_source_reader_check_token_string(
    qa_ui_script_source_reader *reader, const char *text);
bool qa_ui_script_source_reader_check_token_type(
    qa_ui_script_source_reader *reader, int64_t type, int64_t subtype,
    qa_ui_source_token_memory *output);
bool qa_ui_script_source_reader_skip_until_string(
    qa_ui_script_source_reader *reader, const char *text);
bool qa_ui_script_source_reader_read_line_into(
    qa_ui_script_source_reader *reader, qa_ui_source_token_memory *output);
bool qa_ui_script_source_reader_is_source_failure(
    const qa_ui_script_source_reader *reader,
    const qa_ui_script_diagnostic *candidate);
/* TS position: filename copied into the buffer. */
void qa_ui_script_source_reader_position(qa_ui_script_source_reader *reader,
                                         char *filename, size_t capacity,
                                         int64_t *out_line);
void qa_ui_script_source_reader_current_filename(
    qa_ui_script_source_reader *reader, char *filename, size_t capacity);
const qa_ui_script_diagnostic *qa_ui_script_source_reader_diagnostics(
    const qa_ui_script_source_reader *reader, size_t *out_count);

typedef struct qa_ui_script_preprocessor qa_ui_script_preprocessor;

/** TS ScriptPreprocessor.create (eager). NULL only on OOM. */
qa_ui_script_preprocessor *qa_ui_script_preprocessor_create(
    const qa_ui_script_source *root, qa_ui_script_resolve_fn resolve,
    void *resolve_ctx, const qa_ui_script_preprocessor_options *options);
void qa_ui_script_preprocessor_dispose(qa_ui_script_preprocessor *pre);
void qa_ui_script_preprocessor_destroy(qa_ui_script_preprocessor *pre);
const qa_ui_script_diagnostic *qa_ui_script_preprocessor_diagnostics(
    const qa_ui_script_preprocessor *pre, size_t *out_count);
/* False is TS undefined; *out takes one token reference. */
bool qa_ui_script_preprocessor_next(qa_ui_script_preprocessor *pre,
                                    qa_ui_script_token **out);
void qa_ui_script_preprocessor_unread(qa_ui_script_preprocessor *pre,
                                      qa_ui_script_token *token);
void qa_ui_script_preprocessor_reset(qa_ui_script_preprocessor *pre);
/* Borrowed token array (TS all()). */
qa_ui_script_token *const *qa_ui_script_preprocessor_all(
    const qa_ui_script_preprocessor *pre, size_t *out_count);

#endif
