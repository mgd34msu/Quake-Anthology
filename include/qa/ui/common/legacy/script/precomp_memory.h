/*
 * Port of src/ui/common/legacy/script/precomp-memory.ts:
 * PrecompToken, PrecompDefine and PrecompMemory (l_precomp.c/h
 * token/define ownership). C accommodations: owner-local IDs index
 * growable arrays (TS Maps); the chain() generator becomes a
 * visit callback with the same cycle guard. destroy() frees any
 * tokens/defines still live (TS relies on explicit frees + GC).
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_LEGACY_SCRIPT_PRECOMP_MEMORY_H
#define QA_UI_COMMON_LEGACY_SCRIPT_PRECOMP_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/memory.h"
#include "qa/ui/common/legacy/script/token_memory.h"

#define QA_UI_SOURCE_DEFINE_BYTES 32

typedef struct qa_ui_precomp_token qa_ui_precomp_token;
typedef struct qa_ui_precomp_define qa_ui_precomp_define;
typedef struct qa_ui_precomp_memory qa_ui_precomp_memory;

/* Takes `allocation` by value (its bytes back the token). */
qa_ui_precomp_token *qa_ui_precomp_token_create(
    int64_t id, qa_ui_script_allocation allocation);
/** TS localToken(). */
qa_ui_precomp_token *qa_ui_precomp_token_local(void);
void qa_ui_precomp_token_destroy(qa_ui_precomp_token *token);

int64_t qa_ui_precomp_token_id(const qa_ui_precomp_token *token);
qa_ui_source_token_memory *qa_ui_precomp_token_memory(
    qa_ui_precomp_token *token);
const qa_ui_script_allocation *qa_ui_precomp_token_allocation(
    const qa_ui_precomp_token *token);
const qa_ui_source_token_context *qa_ui_precomp_token_context(
    const qa_ui_precomp_token *token);
/* Copies; false only on OOM. */
bool qa_ui_precomp_token_set_context(
    qa_ui_precomp_token *token, const qa_ui_source_token_context *context);
const char *qa_ui_precomp_token_unsupported(
    const qa_ui_precomp_token *token);
/* Copies; NULL clears (TS undefined). False only on OOM. */
bool qa_ui_precomp_token_set_unsupported(qa_ui_precomp_token *token,
                                         const char *reason);
void qa_ui_precomp_token_copy_from(qa_ui_precomp_token *token,
                                   qa_ui_precomp_token *other);
void qa_ui_precomp_token_clear_whitespace(qa_ui_precomp_token *token);
qa_save_value *qa_ui_precomp_token_capture_save_state(
    qa_ui_precomp_token *token);
void qa_ui_precomp_token_restore_save_state(qa_ui_precomp_token *token,
                                            const qa_save_value *value,
                                            bool verify_bytes);

int64_t qa_ui_precomp_define_id(const qa_ui_precomp_define *define);
/* Malloc'd define name; NULL only on OOM. */
char *qa_ui_precomp_define_name(qa_ui_precomp_define *define);
/* Name copied into a caller buffer (truncation is a RangeError
 * only via the TS terminator checks); returns its length. */
size_t qa_ui_precomp_define_name_into(qa_ui_precomp_define *define,
                                      char *out, size_t capacity);
qa_ui_precomp_memory *qa_ui_precomp_define_owner(
    qa_ui_precomp_define *define);
const qa_ui_script_allocation *qa_ui_precomp_define_allocation(
    const qa_ui_precomp_define *define);
int32_t qa_ui_precomp_define_flags(qa_ui_precomp_define *define);
void qa_ui_precomp_define_set_flags(qa_ui_precomp_define *define,
                                    int32_t value);
int32_t qa_ui_precomp_define_builtin(qa_ui_precomp_define *define);
void qa_ui_precomp_define_set_builtin(qa_ui_precomp_define *define,
                                      int32_t value);
int32_t qa_ui_precomp_define_numparms(qa_ui_precomp_define *define);
void qa_ui_precomp_define_set_numparms(qa_ui_precomp_define *define,
                                       int32_t value);
uint32_t qa_ui_precomp_define_parms(qa_ui_precomp_define *define);
void qa_ui_precomp_define_set_parms(qa_ui_precomp_define *define,
                                    uint32_t value);
uint32_t qa_ui_precomp_define_tokens(qa_ui_precomp_define *define);
void qa_ui_precomp_define_set_tokens(qa_ui_precomp_define *define,
                                     uint32_t value);
uint32_t qa_ui_precomp_define_next(qa_ui_precomp_define *define);
void qa_ui_precomp_define_set_next(qa_ui_precomp_define *define,
                                   uint32_t value);
uint32_t qa_ui_precomp_define_hashnext(qa_ui_precomp_define *define);
void qa_ui_precomp_define_set_hashnext(qa_ui_precomp_define *define,
                                       uint32_t value);
bool qa_ui_precomp_define_fixed(qa_ui_precomp_define *define);
int64_t qa_ui_precomp_define_parameter_index(qa_ui_precomp_define *define,
                                             const char *name);

/** TS PrecompMemory. NULL memory selects malloc (TS undefined). */
qa_ui_precomp_memory *qa_ui_precomp_memory_create(
    const qa_ui_script_memory *memory);
void qa_ui_precomp_memory_destroy(qa_ui_precomp_memory *memory);
size_t qa_ui_precomp_memory_token_count(const qa_ui_precomp_memory *memory);
qa_save_value *qa_ui_precomp_memory_capture_save_state(
    qa_ui_precomp_memory *memory,
    const qa_ui_script_memory_capture *capture);
void qa_ui_precomp_memory_restore_save_state(
    qa_ui_precomp_memory *memory, const qa_save_value *value,
    const qa_ui_script_memory_restore *restore);

qa_ui_precomp_token *qa_ui_precomp_memory_copy_token(
    qa_ui_precomp_memory *memory, qa_ui_precomp_token *token);
qa_ui_precomp_token *qa_ui_precomp_memory_token(qa_ui_precomp_memory *memory,
                                                int64_t id);
/* TS chain(): visit each token; false from visit stops early. */
typedef bool (*qa_ui_precomp_chain_fn)(void *ctx,
                                       qa_ui_precomp_token *token);
void qa_ui_precomp_memory_chain(qa_ui_precomp_memory *memory, int64_t head,
                                qa_ui_precomp_chain_fn visit, void *ctx);
void qa_ui_precomp_memory_free_token(qa_ui_precomp_memory *memory,
                                     qa_ui_precomp_token *token);
void qa_ui_precomp_memory_free_tokens(qa_ui_precomp_memory *memory,
                                      int64_t head);
qa_ui_precomp_define *qa_ui_precomp_memory_allocate_define(
    qa_ui_precomp_memory *memory, const char *name, size_t name_length,
    bool clear);
qa_ui_precomp_define *qa_ui_precomp_memory_define(
    qa_ui_precomp_memory *memory, int64_t id);
qa_ui_precomp_define *qa_ui_precomp_memory_copy_define(
    qa_ui_precomp_memory *memory, qa_ui_precomp_define *define);
void qa_ui_precomp_memory_free_define(qa_ui_precomp_memory *memory,
                                      qa_ui_precomp_define *define);

#endif
