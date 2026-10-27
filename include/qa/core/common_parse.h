/*
 * Port of src/core/common-parse.ts: Q3 COM_Parse/COM_Compress, the
 * general Tokenizer and parseEntities, and the Q1/Q2 donor token
 * parsers. C accommodations:
 *  - Source text is ptr+length bytes; the TS Latin-1 charCode checks
 *    are inherent, and offsets/lengths count bytes (UTF-16 units in
 *    TS; identical for Latin-1 input).
 *  - CommonParseCursor.offset null becomes has_offset == false.
 *  - Prints are fn ptr + ctx; CommonError raises through
 *    qa_common_error; TextParseError aborts with its TS string form.
 *  - Token/entity strings are malloc'd (NULL only on OOM where a
 *    bool return says so).
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_COMMON_PARSE_H
#define QA_CORE_COMMON_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "qa/persistence/value.h"

#define QA_INFO_STRING_MAX 1024
#define QA_BIG_INFO_STRING_MAX 8192
#define QA_TOKEN_MAX 1024
#define QA_COMMAND_TOKEN_MAX 1024
#define QA_Q2_TOKEN_MAX 128

typedef void (*qa_common_print_fn)(void *ctx, const char *text);

typedef struct qa_common_parse_cursor {
  char *source; /* owned */
  size_t length;
  size_t terminator;
  bool uninitialized_end; /* TS end: "uninitialized" */
  bool has_offset;        /* false is TS offset === null */
  size_t offset;
} qa_common_parse_cursor;

/* False only on OOM. `uninitialized_end` selects the TS end mode. */
bool qa_common_parse_cursor_init(qa_common_parse_cursor *cursor, const char *source,
                                 size_t length, bool uninitialized_end);
void qa_common_parse_cursor_destroy(qa_common_parse_cursor *cursor);
void qa_common_parse_cursor_set_null(qa_common_parse_cursor *cursor);
void qa_common_parse_cursor_set_offset(qa_common_parse_cursor *cursor, size_t offset);

/* Malloc'd compressed text (NUL-free); *out_length set when given. */
char *qa_compress_common_text(const char *source, size_t length,
                              bool uninitialized_end, size_t *out_length);

typedef struct qa_common_parse_state {
  char token[1025];
  size_t token_length;
  int32_t line;
  char name[1024];
} qa_common_parse_state;

void qa_common_parse_state_init(qa_common_parse_state *state);
/* Owned checkpoint tree {token,line,name}; NULL only on OOM. */
qa_save_value *qa_common_parse_state_capture(const qa_common_parse_state *state);
void qa_common_parse_state_restore(qa_common_parse_state *state, const qa_save_value *value);
void qa_common_parse_state_begin_session(qa_common_parse_state *state, const char *name,
                                         size_t length, qa_common_print_fn print, void *ctx);
void qa_common_parse_state_parse_error(const qa_common_parse_state *state,
                                       const char *message, size_t length,
                                       qa_common_print_fn print, void *ctx);
void qa_common_parse_state_parse_warning(const qa_common_parse_state *state,
                                         const char *message, size_t length,
                                         qa_common_print_fn print, void *ctx);
void qa_common_parse_state_match_token(qa_common_parse_state *state,
                                       qa_common_parse_cursor *cursor,
                                       const char *match, size_t length);
void qa_common_parse_state_skip_braced_section(qa_common_parse_state *state,
                                               qa_common_parse_cursor *cursor);
void qa_common_parse_state_parse_1d_matrix(qa_common_parse_state *state,
                                           qa_common_parse_cursor *cursor, double x,
                                           float *matrix, size_t matrix_length,
                                           double offset);
void qa_common_parse_state_parse_2d_matrix(qa_common_parse_state *state,
                                           qa_common_parse_cursor *cursor, double y,
                                           double x, float *matrix, size_t matrix_length,
                                           double offset);
void qa_common_parse_state_parse_3d_matrix(qa_common_parse_state *state,
                                           qa_common_parse_cursor *cursor, double z,
                                           double y, double x, float *matrix,
                                           size_t matrix_length, double offset);
void qa_common_parse_state_overwrite_token(qa_common_parse_state *state,
                                           const char *text, size_t length);
void qa_common_parse_state_skip_rest_of_line(qa_common_parse_state *state,
                                             qa_common_parse_cursor *cursor);
/* Borrowed shared token (state->token). */
const char *qa_common_parse_state_parse(qa_common_parse_state *state,
                                        qa_common_parse_cursor *cursor,
                                        bool allow_line_breaks);

noreturn void qa_text_parse_error(const char *source_name, int64_t line, int64_t column,
                                  const char *format, ...);

typedef struct qa_text_token {
  char *value; /* owned */
  int64_t line, column;
  bool quoted;
} qa_text_token;

typedef struct qa_tokenizer {
  char *source; /* owned */
  size_t length;
  char *name; /* owned */
  size_t offset;
  int64_t line, column;
} qa_tokenizer;

/* `name` NULL means "<text>". False only on OOM. */
bool qa_tokenizer_init(qa_tokenizer *tokenizer, const char *source, size_t length,
                       const char *name);
void qa_tokenizer_destroy(qa_tokenizer *tokenizer);
/* False is TS undefined (also false on OOM, with out->value NULL). */
bool qa_tokenizer_next(qa_tokenizer *tokenizer, bool allow_line_breaks, qa_text_token *out);

typedef struct qa_entity_pair { char *key; char *value; } qa_entity_pair;
typedef struct qa_entity_record {
  qa_entity_pair *pairs; /* insertion order; later keys replace */
  size_t pair_count;
} qa_entity_record;
typedef struct qa_entities {
  qa_entity_record *records;
  size_t record_count;
} qa_entities;

/* `name` NULL means "<entities>". False only on OOM. */
bool qa_parse_entities(const char *text, size_t length, const char *name, qa_entities *out);
void qa_entities_free(qa_entities *entities);

typedef struct qa_legacy_parse_state {
  const char *data; /* borrowed */
  size_t length;
  size_t index;
} qa_legacy_parse_state;

/* Malloc'd token; NULL is TS null (end of file) or OOM. */
char *qa_parse_q1_token(qa_legacy_parse_state *state, bool quakeworld);
/* Malloc'd token (never NULL except OOM); max is the TS number. */
char *qa_parse_q2_token(qa_legacy_parse_state *state, double max_token_chars);

#endif
