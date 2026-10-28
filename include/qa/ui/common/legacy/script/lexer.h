/*
 * Port of src/ui/common/legacy/script/lexer.ts: ScriptLexer and its
 * token/diagnostic types (botlib/l_script.c). C accommodations:
 *  - ScriptToken is a refcounted heap struct (retain/release); text
 *    and leadingWhitespace are byte strings with explicit lengths
 *    (escaped NUL payloads survive). `value` mirrors the TS union:
 *    string/literal hold the unquoted text, name/punctuation/
 *    primitive hold the text, number leaves it empty.
 *  - ScriptLanguageError rides a setjmp catch stack (see the
 *    internal section); uncaught failures abort printing the TS
 *    "path:line:column: message" form. isSourceFailure(error)
 *    becomes pointer identity against the pending failure
 *    diagnostic in the reported array.
 *  - The string-source lexer copies its source text (bytes; TS
 *    UTF-16 unit counts equal byte counts for byte input).
 *  - Number subtype/integer fields follow DataView int32/uint32
 *    storage; setters take the C integer types.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_LEGACY_SCRIPT_LEXER_H
#define QA_UI_COMMON_LEGACY_SCRIPT_LEXER_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/memory.h"

#define QA_UI_SCRIPT_DEFAULT_TOKEN_LIMIT 1024

/** TS ScriptTokenType. */
typedef enum qa_ui_script_token_type {
  QA_UI_SCRIPT_TOKEN_PRIMITIVE = 0,
  QA_UI_SCRIPT_TOKEN_STRING = 1,
  QA_UI_SCRIPT_TOKEN_LITERAL = 2,
  QA_UI_SCRIPT_TOKEN_NUMBER = 3,
  QA_UI_SCRIPT_TOKEN_NAME = 4,
  QA_UI_SCRIPT_TOKEN_PUNCTUATION = 5
} qa_ui_script_token_type;

/** TS scriptTokenTypeName ("" for primitive/unknown). */
const char *qa_ui_script_token_type_name(int64_t type);
/** TS scriptNumberSubtypeName; malloc'd, NULL only on OOM. */
char *qa_ui_script_number_subtype_name(int64_t subtype);
/** TS stripDoubleQuotes/stripSingleQuotes; malloc'd, NULL only OOM. */
char *qa_ui_script_strip_double_quotes(const char *text);
char *qa_ui_script_strip_single_quotes(const char *text);

/** TS NumberFlag. */
enum {
  QA_UI_SCRIPT_NUMBER_DECIMAL = 0x0008,
  QA_UI_SCRIPT_NUMBER_HEX = 0x0100,
  QA_UI_SCRIPT_NUMBER_OCTAL = 0x0200,
  QA_UI_SCRIPT_NUMBER_BINARY = 0x0400,
  QA_UI_SCRIPT_NUMBER_FLOAT = 0x0800,
  QA_UI_SCRIPT_NUMBER_INTEGER = 0x1000,
  QA_UI_SCRIPT_NUMBER_LONG = 0x2000,
  QA_UI_SCRIPT_NUMBER_UNSIGNED = 0x4000
};

/** TS Punctuation. */
typedef enum qa_ui_script_punctuation {
  QA_UI_SCRIPT_PUNCT_RIGHT_SHIFT_ASSIGN = 1,
  QA_UI_SCRIPT_PUNCT_LEFT_SHIFT_ASSIGN = 2,
  QA_UI_SCRIPT_PUNCT_PARAMETERS = 3,
  QA_UI_SCRIPT_PUNCT_PREPROCESSOR_MERGE = 4,
  QA_UI_SCRIPT_PUNCT_LOGICAL_AND = 5,
  QA_UI_SCRIPT_PUNCT_LOGICAL_OR = 6,
  QA_UI_SCRIPT_PUNCT_GREATER_OR_EQUAL = 7,
  QA_UI_SCRIPT_PUNCT_LESS_OR_EQUAL = 8,
  QA_UI_SCRIPT_PUNCT_EQUAL = 9,
  QA_UI_SCRIPT_PUNCT_NOT_EQUAL = 10,
  QA_UI_SCRIPT_PUNCT_MULTIPLY_ASSIGN = 11,
  QA_UI_SCRIPT_PUNCT_DIVIDE_ASSIGN = 12,
  QA_UI_SCRIPT_PUNCT_MODULO_ASSIGN = 13,
  QA_UI_SCRIPT_PUNCT_ADD_ASSIGN = 14,
  QA_UI_SCRIPT_PUNCT_SUBTRACT_ASSIGN = 15,
  QA_UI_SCRIPT_PUNCT_INCREMENT = 16,
  QA_UI_SCRIPT_PUNCT_DECREMENT = 17,
  QA_UI_SCRIPT_PUNCT_BINARY_AND_ASSIGN = 18,
  QA_UI_SCRIPT_PUNCT_BINARY_OR_ASSIGN = 19,
  QA_UI_SCRIPT_PUNCT_BINARY_XOR_ASSIGN = 20,
  QA_UI_SCRIPT_PUNCT_RIGHT_SHIFT = 21,
  QA_UI_SCRIPT_PUNCT_LEFT_SHIFT = 22,
  QA_UI_SCRIPT_PUNCT_POINTER_REFERENCE = 23,
  QA_UI_SCRIPT_PUNCT_SCOPE = 24,
  QA_UI_SCRIPT_PUNCT_POINTER_TO_MEMBER = 25,
  QA_UI_SCRIPT_PUNCT_MULTIPLY = 26,
  QA_UI_SCRIPT_PUNCT_DIVIDE = 27,
  QA_UI_SCRIPT_PUNCT_MODULO = 28,
  QA_UI_SCRIPT_PUNCT_ADD = 29,
  QA_UI_SCRIPT_PUNCT_SUBTRACT = 30,
  QA_UI_SCRIPT_PUNCT_ASSIGN = 31,
  QA_UI_SCRIPT_PUNCT_BINARY_AND = 32,
  QA_UI_SCRIPT_PUNCT_BINARY_OR = 33,
  QA_UI_SCRIPT_PUNCT_BINARY_XOR = 34,
  QA_UI_SCRIPT_PUNCT_BINARY_NOT = 35,
  QA_UI_SCRIPT_PUNCT_LOGICAL_NOT = 36,
  QA_UI_SCRIPT_PUNCT_GREATER = 37,
  QA_UI_SCRIPT_PUNCT_LESS = 38,
  QA_UI_SCRIPT_PUNCT_REFERENCE = 39,
  QA_UI_SCRIPT_PUNCT_COMMA = 40,
  QA_UI_SCRIPT_PUNCT_SEMICOLON = 41,
  QA_UI_SCRIPT_PUNCT_COLON = 42,
  QA_UI_SCRIPT_PUNCT_QUESTION = 43,
  QA_UI_SCRIPT_PUNCT_PARENTHESIS_OPEN = 44,
  QA_UI_SCRIPT_PUNCT_PARENTHESIS_CLOSE = 45,
  QA_UI_SCRIPT_PUNCT_BRACE_OPEN = 46,
  QA_UI_SCRIPT_PUNCT_BRACE_CLOSE = 47,
  QA_UI_SCRIPT_PUNCT_BRACKET_OPEN = 48,
  QA_UI_SCRIPT_PUNCT_BRACKET_CLOSE = 49,
  QA_UI_SCRIPT_PUNCT_BACKSLASH = 50,
  QA_UI_SCRIPT_PUNCT_PREPROCESSOR = 51,
  QA_UI_SCRIPT_PUNCT_DOLLAR = 52
} qa_ui_script_punctuation;

/** TS LexerFlag. */
enum {
  QA_UI_SCRIPT_LEXER_NONE = 0,
  QA_UI_SCRIPT_LEXER_NO_ERRORS = 0x0001,
  QA_UI_SCRIPT_LEXER_NO_WARNINGS = 0x0002,
  QA_UI_SCRIPT_LEXER_NO_STRING_CONCATENATION = 0x0004,
  QA_UI_SCRIPT_LEXER_NO_STRING_ESCAPES = 0x0008,
  QA_UI_SCRIPT_LEXER_PRIMITIVE = 0x0010,
  QA_UI_SCRIPT_LEXER_NO_BINARY_NUMBERS = 0x0020,
  QA_UI_SCRIPT_LEXER_NO_NUMBER_VALUES = 0x0040
};

/** TS SourceLocation (owned path). */
typedef struct qa_ui_script_location {
  char *path;
  int64_t line;
  int64_t column;
} qa_ui_script_location;

void qa_ui_script_location_destroy(qa_ui_script_location *location);

/** TS ScriptDiagnostic severity. */
typedef enum qa_ui_script_severity {
  QA_UI_SCRIPT_SEVERITY_WARNING,
  QA_UI_SCRIPT_SEVERITY_ERROR
} qa_ui_script_severity;

const char *qa_ui_script_severity_name(qa_ui_script_severity severity);

/** TS ScriptDiagnostic (owned message/location). */
typedef struct qa_ui_script_diagnostic {
  qa_ui_script_severity severity;
  char *message;
  qa_ui_script_location location;
} qa_ui_script_diagnostic;

void qa_ui_script_diagnostic_destroy(qa_ui_script_diagnostic *diagnostic);
/* Owned checkpoint value for one diagnostic; NULL only on OOM. */
qa_save_value *qa_ui_script_diagnostic_capture(
    const qa_ui_script_diagnostic *diagnostic);
/** TS readScriptDiagnostic; false only on OOM (out left zeroed). */
bool qa_ui_script_read_diagnostic(const qa_save_reader *reader,
                                  qa_ui_script_diagnostic *out);

/** TS ScriptToken kind (exact TS strings via the name function). */
typedef enum qa_ui_script_token_kind {
  QA_UI_SCRIPT_KIND_PRIMITIVE,
  QA_UI_SCRIPT_KIND_STRING,
  QA_UI_SCRIPT_KIND_LITERAL,
  QA_UI_SCRIPT_KIND_NUMBER,
  QA_UI_SCRIPT_KIND_NAME,
  QA_UI_SCRIPT_KIND_PUNCTUATION
} qa_ui_script_token_kind;

const char *qa_ui_script_token_kind_name(qa_ui_script_token_kind kind);

typedef struct qa_ui_script_token_snapshot qa_ui_script_token_snapshot;

/** TS ScriptToken. Refcounted; fields are read-only to callers. */
typedef struct qa_ui_script_token {
  qa_ui_script_token_kind kind;
  char *text; /* owned bytes, text_length long (may embed NUL) */
  size_t text_length;
  char *value; /* owned bytes; empty for number tokens */
  size_t value_length;
  int64_t length;      /* string/literal/name */
  int64_t flags;       /* number */
  uint32_t integer_value; /* number */
  double float_value;     /* number */
  int64_t punctuation;    /* punctuation */
  qa_ui_script_location location;
  char *leading_whitespace; /* owned bytes */
  size_t leading_whitespace_length;
  int64_t lines_crossed;
  /* token-memory snapshot (TS WeakMap entry); owned reference. */
  qa_ui_script_token_snapshot *snapshot;
  /* Preprocessor TokenMetadata (TS WeakMap entry). */
  bool has_metadata;
  bool metadata_uninitialized;
  int64_t metadata_subtype;
  uint32_t metadata_integer_value;
  double metadata_float_value;
  char *metadata_reason; /* owned; uninitialized reason */
  size_t refs;
} qa_ui_script_token;

qa_ui_script_token *qa_ui_script_token_retain(qa_ui_script_token *token);
void qa_ui_script_token_release(qa_ui_script_token *token);
/* NULL only on OOM. Copies text/location/whitespace. */
qa_ui_script_token *qa_ui_script_token_create(
    qa_ui_script_token_kind kind, const char *text, size_t text_length,
    const qa_ui_script_location *location, const char *leading_whitespace,
    size_t leading_whitespace_length, int64_t lines_crossed);
/* Copies value bytes into the token; false only on OOM. */
bool qa_ui_script_token_set_value(qa_ui_script_token *token,
                                  const char *value, size_t length);

/** TS ScriptTokenRecord. Holds one token reference. */
typedef struct qa_ui_script_token_record {
  qa_ui_script_token *token;
  int64_t subtype;
  uint32_t integer_value;
  double float_value;
} qa_ui_script_token_record;

void qa_ui_script_token_record_release(qa_ui_script_token_record *record);

/** TS (diagnostic) => void report callback. */
typedef void (*qa_ui_script_report_fn)(
    void *ctx, const qa_ui_script_diagnostic *diagnostic);

/** TS ScriptPunctuation. */
typedef struct qa_ui_script_punctuation_spec {
  const char *text;
  int64_t punctuation;
} qa_ui_script_punctuation_spec;

/** TS ScriptLexerOptions. NULL report/memory mean TS undefined. */
typedef struct qa_ui_script_lexer_options {
  bool has_flags;
  int64_t flags;
  bool has_max_token_length;
  int64_t max_token_length;
  qa_ui_script_report_fn report;
  void *report_ctx;
  const qa_ui_script_memory *memory;
} qa_ui_script_lexer_options;

typedef struct qa_ui_script_lexer qa_ui_script_lexer;

/** allocateScriptSource(length, path, memory); NULL only on OOM. */
qa_ui_source_script_storage *qa_ui_script_allocate_source(
    int64_t length, const char *path, const qa_ui_script_memory *memory);

/* String source (copied). NULL only on OOM. Options may be NULL. */
qa_ui_script_lexer *qa_ui_script_lexer_create_text(
    const uint8_t *text, size_t length, const char *path,
    const qa_ui_script_lexer_options *options);
/* Storage source; takes ownership of `storage`. NULL only on OOM. */
qa_ui_script_lexer *qa_ui_script_lexer_create_storage(
    qa_ui_source_script_storage *storage, const char *path,
    const qa_ui_script_lexer_options *options);
qa_ui_script_lexer *qa_ui_script_lexer_restore_save_state(
    const qa_save_value *value, const qa_ui_script_memory *memory,
    bool has_memory, const qa_ui_script_memory_restore *restore,
    qa_ui_script_report_fn report, void *report_ctx);
qa_save_value *qa_ui_script_lexer_capture_save_state(
    qa_ui_script_lexer *lexer, const qa_ui_script_memory_capture *capture);
void qa_ui_script_lexer_dispose(qa_ui_script_lexer *lexer);
void qa_ui_script_lexer_destroy(qa_ui_script_lexer *lexer);

qa_ui_source_script_storage *qa_ui_script_lexer_source_storage(
    qa_ui_script_lexer *lexer);
/* Borrowed reported array. */
const qa_ui_script_diagnostic *qa_ui_script_lexer_diagnostics(
    const qa_ui_script_lexer *lexer, size_t *out_count);
bool qa_ui_script_lexer_is_source_failure(
    const qa_ui_script_lexer *lexer,
    const qa_ui_script_diagnostic *candidate);
/* Writes a borrowed-path view; path valid while the lexer lives. */
void qa_ui_script_lexer_current_location(const qa_ui_script_lexer *lexer,
                                         const char **out_path,
                                         int64_t *out_line,
                                         int64_t *out_column);
bool qa_ui_script_lexer_end_of_script(qa_ui_script_lexer *lexer);
/* False is TS undefined; *out takes one token reference. */
bool qa_ui_script_lexer_next(qa_ui_script_lexer *lexer,
                             qa_ui_script_token **out);
bool qa_ui_script_lexer_next_into(qa_ui_script_lexer *lexer,
                                  qa_ui_source_token_memory *output,
                                  qa_ui_script_token_record *out);
int64_t qa_ui_script_lexer_get_flags(qa_ui_script_lexer *lexer);
void qa_ui_script_lexer_set_flags(qa_ui_script_lexer *lexer, int64_t flags);
const char *qa_ui_script_lexer_punctuation_from_num(
    qa_ui_script_lexer *lexer, int64_t number);
/* NULL specs restores the defaults (TS null). */
void qa_ui_script_lexer_set_punctuations(
    qa_ui_script_lexer *lexer, const qa_ui_script_punctuation_spec *specs,
    size_t count);
int64_t qa_ui_script_lexer_num_lines_crossed(qa_ui_script_lexer *lexer);
int64_t qa_ui_script_lexer_next_whitespace_char(qa_ui_script_lexer *lexer);
void qa_ui_script_lexer_unread_last(qa_ui_script_lexer *lexer);
void qa_ui_script_lexer_unread(qa_ui_script_lexer *lexer,
                               qa_ui_script_token *token);
bool qa_ui_script_lexer_expect_token_string(qa_ui_script_lexer *lexer,
                                            const char *text);
bool qa_ui_script_lexer_expect_any_token(
    qa_ui_script_lexer *lexer, qa_ui_source_token_memory *output);
bool qa_ui_script_lexer_expect_token_type(
    qa_ui_script_lexer *lexer, int64_t type, int64_t subtype,
    qa_ui_source_token_memory *output);
bool qa_ui_script_lexer_check_token_string(qa_ui_script_lexer *lexer,
                                           const char *text);
bool qa_ui_script_lexer_check_token_type(
    qa_ui_script_lexer *lexer, int64_t type, int64_t subtype,
    qa_ui_source_token_memory *output);
bool qa_ui_script_lexer_skip_until_string(qa_ui_script_lexer *lexer,
                                          const char *text);
bool qa_ui_script_lexer_script_skip_to(qa_ui_script_lexer *lexer,
                                       const char *text);
double qa_ui_script_lexer_read_signed_float(qa_ui_script_lexer *lexer);
int64_t qa_ui_script_lexer_read_signed_int(qa_ui_script_lexer *lexer);
bool qa_ui_script_lexer_read_literal_into(
    qa_ui_script_lexer *lexer, qa_ui_source_token_memory *output);
void qa_ui_script_lexer_reset(qa_ui_script_lexer *lexer);

/* ---- Internal to the script modules (lexer/token/preprocessor) ---- */

typedef struct qa_ui_script_catch {
  jmp_buf buf;
  struct qa_ui_script_catch *prev;
} qa_ui_script_catch;

/** Shared ScriptLanguageError channel (catch stack + pending). */
typedef struct qa_ui_script_fail_env {
  qa_ui_script_catch *top;
  qa_ui_script_diagnostic pending; /* moved in on throw */
  bool has_pending;
  /* Identity of pending within an owner's reported array, or NULL. */
  const qa_ui_script_diagnostic *pending_identity;
} qa_ui_script_fail_env;

void qa_ui_script_fail_env_init(qa_ui_script_fail_env *env);
void qa_ui_script_fail_env_destroy(qa_ui_script_fail_env *env);
/* Drop a caught pending failure (after a setjmp catch handled it). */
void qa_ui_script_fail_env_clear_pending(qa_ui_script_fail_env *env);
/* Takes ownership of `diagnostic` contents; never returns. */
_Noreturn void qa_ui_script_fail_throw(
    qa_ui_script_fail_env *env, qa_ui_script_diagnostic *diagnostic,
    const qa_ui_script_diagnostic *identity);
/* Plain TS `Error` throw: prints "Error: <msg>" and aborts. */
_Noreturn void qa_ui_script_plain_error(const char *format, ...);

/* Create a lexer sharing an engine fail env (NULL env: private). */
qa_ui_script_lexer *qa_ui_script_lexer_create_text_env(
    const uint8_t *text, size_t length, const char *path,
    const qa_ui_script_lexer_options *options,
    qa_ui_script_fail_env *env);
qa_ui_script_lexer *qa_ui_script_lexer_create_storage_env(
    qa_ui_source_script_storage *storage, const char *path,
    const qa_ui_script_lexer_options *options,
    qa_ui_script_fail_env *env);
qa_ui_script_lexer *qa_ui_script_lexer_restore_save_state_env(
    const qa_save_value *value, const qa_ui_script_memory *memory,
    bool has_memory, const qa_ui_script_memory_restore *restore,
    qa_ui_script_report_fn report, void *report_ctx,
    qa_ui_script_fail_env *env);
qa_ui_script_fail_env *qa_ui_script_lexer_fail_env(qa_ui_script_lexer *lexer);
/* TS lexer output token (expect* helpers read it); borrowed. */
qa_ui_source_token_memory *qa_ui_script_lexer_output(
    qa_ui_script_lexer *lexer);

#endif
