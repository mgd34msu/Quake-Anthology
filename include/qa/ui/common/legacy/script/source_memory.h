/*
 * Port of src/ui/common/legacy/script/source-memory.ts: SourceRecord
 * and its indent stack (l_precomp.c/h source_t byte layout).
 * C accommodations: borrowed punctuations are copied shallowly
 * (spec text stays borrowed); the heap/managed backings, pointer
 * words and extents mirror the TS exactly. destroy() frees the C
 * structs and any allocations the TS free* calls did not reach.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_LEGACY_SCRIPT_SOURCE_MEMORY_H
#define QA_UI_COMMON_LEGACY_SCRIPT_SOURCE_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/lexer.h"
#include "qa/ui/common/legacy/script/memory.h"
#include "qa/ui/common/legacy/script/precomp_memory.h"

#define QA_UI_SOURCE_RECORD_BYTES 3144
#define QA_UI_SOURCE_DEFINE_HASH_BUCKETS 1024

/** TS SourceIndentType. */
typedef enum qa_ui_source_indent_type {
  QA_UI_SOURCE_INDENT_IF = 1,
  QA_UI_SOURCE_INDENT_ELSE = 2,
  QA_UI_SOURCE_INDENT_ELIF = 4,
  QA_UI_SOURCE_INDENT_IFDEF = 8,
  QA_UI_SOURCE_INDENT_IFNDEF = 16
} qa_ui_source_indent_type;

/** TS SourceRecord lifetime argument. */
typedef enum qa_ui_source_lifetime {
  QA_UI_SOURCE_LIFETIME_HEAP,
  QA_UI_SOURCE_LIFETIME_STACK
} qa_ui_source_lifetime;

typedef struct qa_ui_source_record qa_ui_source_record;

/* NULL memory is TS undefined (managed backings). NULL return: OOM. */
qa_ui_source_record *qa_ui_source_record_create(
    const char *filename, int64_t script,
    const qa_ui_script_memory *memory, bool has_memory,
    qa_ui_source_lifetime lifetime);
qa_ui_source_record *qa_ui_source_record_restore_save_state(
    const qa_save_value *value, const qa_ui_script_memory *memory,
    bool has_memory, const qa_ui_script_memory_restore *restore);
qa_save_value *qa_ui_source_record_capture_save_state(
    qa_ui_source_record *record,
    const qa_ui_script_memory_capture *capture);
void qa_ui_source_record_destroy(qa_ui_source_record *record);

qa_ui_precomp_token *qa_ui_source_record_token(qa_ui_source_record *record);
/* Malloc'd; NULL only on OOM. */
char *qa_ui_source_record_filename(qa_ui_source_record *record);
/* NULL specs is TS null. Spec text pointers stay borrowed. */
void qa_ui_source_record_set_punctuations(
    qa_ui_source_record *record,
    const qa_ui_script_punctuation_spec *specs, size_t count);
/* Borrowed specs; *out_count set. NULL return is TS null. */
const qa_ui_script_punctuation_spec *qa_ui_source_record_punctuations(
    qa_ui_source_record *record, size_t *out_count);
uint32_t qa_ui_source_record_tokens(qa_ui_source_record *record);
void qa_ui_source_record_set_tokens(qa_ui_source_record *record,
                                    uint32_t value);
/* Malloc'd; NULL only on OOM. */
char *qa_ui_source_record_include_path(qa_ui_source_record *record);
void qa_ui_source_record_set_include_path(qa_ui_source_record *record,
                                          const char *path);
uint32_t qa_ui_source_record_script(qa_ui_source_record *record);
void qa_ui_source_record_set_script(qa_ui_source_record *record,
                                    uint32_t value);
int32_t qa_ui_source_record_skip(qa_ui_source_record *record);
void qa_ui_source_record_set_skip(qa_ui_source_record *record, int32_t value);

uint32_t qa_ui_source_record_hash_head(qa_ui_source_record *record,
                                       int64_t bucket);
void qa_ui_source_record_set_hash_head(qa_ui_source_record *record,
                                       int64_t bucket, uint32_t id);

bool qa_ui_source_record_has_current_indent(qa_ui_source_record *record);
void qa_ui_source_record_push_indent(qa_ui_source_record *record,
                                     qa_ui_source_indent_type type,
                                     bool skip);
/* False is TS undefined. */
bool qa_ui_source_record_pop_indent(qa_ui_source_record *record,
                                    qa_ui_source_indent_type *out_type,
                                    int64_t *out_skip);
void qa_ui_source_record_free_indents(qa_ui_source_record *record);
void qa_ui_source_record_free_hash(qa_ui_source_record *record);
void qa_ui_source_record_free_record(qa_ui_source_record *record);

#endif
