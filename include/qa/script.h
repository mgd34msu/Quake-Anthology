#ifndef QA_SCRIPT_H
#define QA_SCRIPT_H

#include "qa/common.h"

typedef enum qa_script_token_kind {
    QA_SCRIPT_PRIMITIVE,
    QA_SCRIPT_STRING,
    QA_SCRIPT_LITERAL,
    QA_SCRIPT_NUMBER,
    QA_SCRIPT_NAME,
    QA_SCRIPT_PUNCTUATION
} qa_script_token_kind;
enum {
    QA_SCRIPT_DECIMAL = 0x0008,
    QA_SCRIPT_HEX = 0x0100,
    QA_SCRIPT_OCTAL = 0x0200,
    QA_SCRIPT_BINARY = 0x0400,
    QA_SCRIPT_FLOAT = 0x0800,
    QA_SCRIPT_INTEGER = 0x1000,
    QA_SCRIPT_LONG = 0x2000,
    QA_SCRIPT_UNSIGNED = 0x4000
};
enum {
    QA_SCRIPT_NO_ERRORS = 1,
    QA_SCRIPT_NO_WARNINGS = 2,
    QA_SCRIPT_NO_STRING_CONCAT = 4,
    QA_SCRIPT_NO_STRING_ESCAPES = 8,
    QA_SCRIPT_PRIMITIVE_TOKENS = 16,
    QA_SCRIPT_NO_BINARY = 32,
    QA_SCRIPT_NO_NUMBER_VALUES = 64,
    /* Opt out of l_script's historical hex and fractional-number grammar. */
    QA_SCRIPT_STRICT_NUMBERS = 128
};
typedef enum qa_script_punctuation_id {
    QA_SCRIPT_RSHIFT_ASSIGN = 1,
    QA_SCRIPT_LSHIFT_ASSIGN,
    QA_SCRIPT_PARAMETERS,
    QA_SCRIPT_MERGE,
    QA_SCRIPT_LOGICAL_AND,
    QA_SCRIPT_LOGICAL_OR,
    QA_SCRIPT_GE,
    QA_SCRIPT_LE,
    QA_SCRIPT_EQ,
    QA_SCRIPT_NE,
    QA_SCRIPT_MUL_ASSIGN,
    QA_SCRIPT_DIV_ASSIGN,
    QA_SCRIPT_MOD_ASSIGN,
    QA_SCRIPT_ADD_ASSIGN,
    QA_SCRIPT_SUB_ASSIGN,
    QA_SCRIPT_INCREMENT,
    QA_SCRIPT_DECREMENT,
    QA_SCRIPT_AND_ASSIGN,
    QA_SCRIPT_OR_ASSIGN,
    QA_SCRIPT_XOR_ASSIGN,
    QA_SCRIPT_RSHIFT,
    QA_SCRIPT_LSHIFT,
    QA_SCRIPT_POINTER,
    QA_SCRIPT_SCOPE,
    QA_SCRIPT_MEMBER,
    QA_SCRIPT_MUL,
    QA_SCRIPT_DIV,
    QA_SCRIPT_MOD,
    QA_SCRIPT_ADD,
    QA_SCRIPT_SUB,
    QA_SCRIPT_ASSIGN,
    QA_SCRIPT_AND,
    QA_SCRIPT_OR,
    QA_SCRIPT_XOR,
    QA_SCRIPT_NOT,
    QA_SCRIPT_LOGICAL_NOT,
    QA_SCRIPT_GT,
    QA_SCRIPT_LT,
    QA_SCRIPT_REFERENCE,
    QA_SCRIPT_COMMA,
    QA_SCRIPT_SEMICOLON,
    QA_SCRIPT_COLON,
    QA_SCRIPT_QUESTION,
    QA_SCRIPT_OPEN_PAREN,
    QA_SCRIPT_CLOSE_PAREN,
    QA_SCRIPT_OPEN_BRACE,
    QA_SCRIPT_CLOSE_BRACE,
    QA_SCRIPT_OPEN_BRACKET,
    QA_SCRIPT_CLOSE_BRACKET,
    QA_SCRIPT_BACKSLASH,
    QA_SCRIPT_HASH,
    QA_SCRIPT_DOLLAR
} qa_script_punctuation_id;
typedef struct qa_script_location {
    const char *path;
    uint32_t line, column;
    size_t offset;
} qa_script_location;
typedef struct qa_script_token {
    qa_script_token_kind kind;
    uint32_t subtype, lines_crossed;
    int32_t integer;
    double number;
    /* Strings/literals include their decoded quotes. All spans and paths stay
     * valid until the owning lexer/source is closed. No per-read ownership. */
    qa_bytes text, leading_whitespace;
    qa_script_location location;
} qa_script_token;
typedef struct qa_script_punctuation {
    const char *text;
    uint32_t id;
} qa_script_punctuation;
typedef enum qa_script_severity {
    QA_SCRIPT_WARNING,
    QA_SCRIPT_ERROR,
    QA_SCRIPT_INFO,
    QA_SCRIPT_FATAL
} qa_script_severity;
typedef struct qa_script_diagnostic {
    qa_script_severity severity;
    qa_script_location location;
    const char *message;
} qa_script_diagnostic;
struct qa_script_memory;
typedef struct qa_script_lexer_options {
    uint32_t flags;
    size_t token_limit; /* Zero selects source MAX_TOKEN 1024, including NUL. */
    void *context;
    void (*diagnostic)(void *, const qa_script_diagnostic *);
    const qa_script_punctuation *punctuations;
    size_t punctuation_count;
    const struct qa_script_memory *memory;
} qa_script_lexer_options;
typedef struct qa_script_lexer qa_script_lexer;
/* Immutable input is copied into the script_t text allocation. */
bool qa_script_lexer_open(const char *path, qa_bytes, const qa_script_lexer_options *,
                          qa_script_lexer **, qa_error *);
void qa_script_lexer_close(qa_script_lexer *);
/* Valid reads always publish the current token, including cleared EOF output
 * and partially written tokens on failure. */
bool qa_script_lexer_next(qa_script_lexer *, qa_script_token *, bool *found, qa_error *);
bool qa_script_lexer_unread(qa_script_lexer *, const qa_script_token *, qa_error *);
void qa_script_lexer_reset(qa_script_lexer *);
qa_script_location qa_script_lexer_position(const qa_script_lexer *);
qa_bytes qa_script_token_value(const qa_script_token *);
bool qa_script_token_is(const qa_script_token *, const char *);
typedef struct qa_script_lexer_state {
    size_t offset;
    uint32_t line, column;
    bool unread;
    qa_script_token token;
} qa_script_lexer_state;
bool qa_script_lexer_capture(const qa_script_lexer *, qa_script_lexer_state *, qa_error *);
bool qa_script_lexer_restore(qa_script_lexer *, const qa_script_lexer_state *, qa_error *);

typedef enum qa_script_include_kind {
    QA_SCRIPT_ROOT,
    QA_SCRIPT_INCLUDE_QUOTED,
    QA_SCRIPT_INCLUDE_SYSTEM
} qa_script_include_kind;
typedef struct qa_script_include {
    qa_script_include_kind kind;
    const char *from_path, *requested_path, *include_path;
} qa_script_include;
typedef struct qa_script_resource {
    const char *path;
    qa_bytes bytes;
    void *lease;
} qa_script_resource;
typedef struct qa_script_memory_allocation {
    uint64_t owner,generation;
    uint32_t slot;
} qa_script_memory_allocation;
typedef struct qa_script_memory_span { uint8_t *data; uint32_t size; } qa_script_memory_span;
typedef struct qa_script_memory {
    void *context;
    bool (*retain)(void *,qa_error *);
    void (*release)(void *);
    bool (*allocate)(void *,uint32_t,bool,qa_script_memory_allocation *,qa_error *);
    bool (*bytes)(void *,qa_script_memory_allocation,qa_script_memory_span *,qa_error *);
    bool (*free)(void *,qa_script_memory_allocation,qa_error *);
    bool (*reference)(void *,qa_script_memory_allocation,size_t *,qa_error *);
    bool (*resolve)(void *,size_t,qa_script_memory_allocation *,qa_error *);
    bool (*resolve_history)(void *,size_t,qa_script_memory_allocation *,qa_error *);
} qa_script_memory;
typedef struct qa_script_services {
    void *context;
    /* Callbacks must not close or mutate the active source. Handle owners may
     * retire a handle immediately and defer source close until its call ends. */
    bool (*read)(void *, const qa_script_include *, qa_script_resource *, bool *found, qa_error *);
    void (*release)(void *, qa_script_resource *);
    void (*diagnostic)(void *, const qa_script_diagnostic *);
    /* Captured source-format __DATE__/__TIME__, for deterministic processing. */
    const char *date, *time;
    const qa_script_memory *memory;
    /* LoadScriptFile compresses the loaded text in place before scanning. */
    bool file_text;
} qa_script_services;
typedef struct qa_script_defines qa_script_defines;
typedef struct qa_script qa_script;
typedef struct qa_script_options {
    uint32_t lexer_flags;
    size_t token_limit, maximum_include_depth, maximum_expansions, maximum_queued_tokens,
        maximum_output_tokens, maximum_defines, maximum_expression_tokens, maximum_source_tokens;
    bool builtins;
    const char *include_path;
    const qa_script_defines *globals;
} qa_script_options;
bool qa_script_defines_create(qa_script_defines **, qa_error *);
bool qa_script_defines_bind_memory(qa_script_defines *,const qa_script_memory *,qa_error *);
const qa_script_memory *qa_script_defines_memory(const qa_script_defines *);
void qa_script_defines_retain(qa_script_defines *);
void qa_script_defines_release(qa_script_defines *);
bool qa_script_defines_add(qa_script_defines *, const char *definition, qa_error *);
bool qa_script_defines_remove(qa_script_defines *, const char *name, qa_error *);
bool qa_script_defines_clear(qa_script_defines *,qa_error *);
bool qa_script_open(const char *path, const qa_script_services *, const qa_script_options *,
                    qa_script **, qa_error *);
void qa_script_close(qa_script *);
/* Release native continuation metadata without replaying source MEMORY frees. */
void qa_script_dispose(qa_script *);
/* Publish a detached history reader after its MEMORY restore has committed. */
bool qa_script_adopt_memory(qa_script *,qa_error *);
bool qa_script_next(qa_script *, qa_script_token *, bool *found, qa_error *);
/* Every qa_script_next publishes its current token, including partial failure and the
 * cleared EOF token. Text is borrowed through source close. Raw text may fill
 * token_limit bytes after an overflow; a source ABI must reject that missing
 * terminator rather than silently truncate it. */
bool qa_script_raw_token(const qa_script *, qa_script_token *);
/* Distinguishes recognized source-language failure from service, allocation,
 * or unsupported-profile failure. Inspect after qa_script_next returns false. */
bool qa_script_source_failure(const qa_script *);
bool qa_script_unread(qa_script *, const qa_script_token *, qa_error *);
bool qa_script_define(qa_script *, const char *definition, qa_error *);
bool qa_script_undefine(qa_script *, const char *name, qa_error *);
bool qa_script_is_defined(const qa_script *, const char *name);
qa_script_location qa_script_position(const qa_script *);
qa_script_location qa_script_source_position(const qa_script *);
bool qa_script_expect(qa_script *, const char *, qa_error *);
bool qa_script_check(qa_script *, const char *, bool *matched, qa_error *);
bool qa_script_skip_until(qa_script *, const char *, bool *found, qa_error *);
bool qa_script_read_line(qa_script *, qa_script_token *, bool *found, qa_error *);

/* Checkpoints own their byte spans. Index references use SIZE_MAX for none.
 * Serializers write the named fields; they never persist native pointers. */
typedef struct qa_script_macro_state {
    qa_bytes name;
    const qa_bytes *parameters;
    const qa_script_token *tokens;
    size_t parameter_count, token_count;
    unsigned builtin;
    bool function, fixed, active;
    uint32_t pointer;
    size_t memory_reference;
    qa_bytes record;
} qa_script_macro_state;
typedef struct qa_script_frame_state {
    const char *path;
    qa_bytes source;
    qa_script_lexer_state lexer;
    qa_bytes script_record,punctuation_record;
    size_t script_reference,punctuation_reference;
    bool script_released,punctuation_released;
    size_t condition_base, token_count;
    bool active;
} qa_script_frame_state;
typedef struct qa_script_expansion_state {
    size_t macro, parent;
} qa_script_expansion_state;
typedef struct qa_script_queued_state {
    qa_script_token token;
    size_t expansion;
    uint32_t pointer;
    size_t memory_reference, text_extent;
    uint8_t bytes[1068];
} qa_script_queued_state;
typedef struct qa_script_condition_state {
    size_t frame;
    bool skip, was_else;
    uint32_t pointer;
    size_t memory_reference;
    uint8_t bytes[16];
} qa_script_condition_state;
typedef struct qa_script_checkpoint {
    uint32_t version;
    qa_script_options options; /* globals is always NULL; macros are captured. */
    const char *date, *time;
    const qa_script_macro_state *macros;
    const qa_script_frame_state *frames;
    const size_t *stack;
    const qa_script_expansion_state *expansion_states;
    const qa_script_queued_state *queue;
    const qa_script_condition_state *conditions;
    size_t macro_count, frame_count, stack_count, expansion_count, queue_count, condition_count;
    size_t expansions, outputs;
    uint32_t next_condition_pointer, next_token_pointer, next_define_pointer, define_first;
    qa_bytes define_hash;
    size_t hash_reference;
    qa_bytes source_record;
    size_t source_reference;
    bool empty_expansion;
    qa_script_location last_location;
    qa_script_token raw_token;
    bool source_failure, file_text;
    void *storage;
} qa_script_checkpoint;
bool qa_script_capture(const qa_script *, qa_script_checkpoint *, qa_error *);
bool qa_script_restore(const qa_script_services *, const qa_script_checkpoint *, qa_script **,
                       qa_error *);
/* History readers adopt committed MEMORY on their first source read. */
bool qa_script_restore_detached(const qa_script_services *, const qa_script_checkpoint *,
                                qa_script **, qa_error *);
void qa_script_checkpoint_free(qa_script_checkpoint *);
/* Canonical, versioned little-endian encoding. Both leave output unchanged on
 * failure. Release existing output before success replaces it. Decoded spans
 * belong to the checkpoint, independently of the encoded input. */
bool qa_script_checkpoint_encode(const qa_script_checkpoint *, qa_buffer *, qa_error *);
bool qa_script_checkpoint_decode(qa_bytes, qa_script_checkpoint *, qa_error *);

#endif
