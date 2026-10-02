#ifndef QA_CORE_SCRIPT_INTERNAL_H
#define QA_CORE_SCRIPT_INTERNAL_H
#include "qa/arena.h"
#include "qa/binary.h"
#include "qa/script.h"
#include "qa/text.h"
#include <limits.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct qa_script_lexer {
    qa_bytes input;
    qa_script_lexer_options options;
    qa_script_lexer_state state;
    const char *path;
    qa_arena arena;
    const qa_script_punctuation *punctuations;
    const int32_t *heads, *next;
    qa_script_punctuation *owned_punctuations;
    int32_t *owned_index;
    size_t punctuation_count;
    bool source_failure;
};
bool script_grow(void **, size_t *, size_t, size_t, qa_error *);
char *script_string(qa_arena *, const void *, size_t, qa_error *);
bool script_error(qa_script_lexer *, const char *, qa_error *);
void script_report_error(qa_script_lexer *, const char *);
bool script_unsupported(qa_script_lexer *, const char *, qa_error *);
void script_warning(qa_script_lexer *, const char *);
bool script_number(qa_script_lexer *, qa_script_token *, qa_error *);
bool script_quoted(qa_script_lexer *, qa_script_token *, qa_error *);
void script_whitespace(qa_script_lexer *);
uint8_t script_peek(const qa_script_lexer *, size_t);
void script_advance(qa_script_lexer *, size_t);
static inline bool script_digit(uint8_t c) { return c >= '0' && c <= '9'; }
static inline bool script_alpha(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static inline bool script_name(uint8_t c) { return script_alpha(c) || script_digit(c); }
static inline bool script_hex(uint8_t c) {
    return script_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
typedef struct script_macro {
    struct script_macro *next;
    qa_bytes name, *parameters;
    qa_script_token *tokens;
    size_t parameter_count, token_count;
    unsigned builtin;
    bool function, fixed;
} script_macro;
typedef struct script_macro_table {
    script_macro *buckets[1024];
    size_t count;
    qa_arena arena;
} script_macro_table;
struct qa_script_defines {
    atomic_uint references;
    script_macro_table table;
    script_macro *first;
    struct qa_script_defines *retired;
};
typedef struct script_expansion {
    const script_macro *macro;
    const struct script_expansion *parent;
} script_expansion;
typedef struct script_queued_token {
    qa_script_token token;
    const script_expansion *expansion;
} script_queued_token;
typedef struct script_frame {
    qa_script_resource resource;
    qa_script_lexer *lexer;
    size_t condition_base, token_count;
    bool active, owned;
} script_frame;
enum { SCRIPT_SOURCE_BYTES=3144, SCRIPT_SOURCE_INCLUDE=1024,
       SCRIPT_SOURCE_STACK=2052, SCRIPT_SOURCE_INDENT=2068, SCRIPT_SOURCE_SKIP=2072 };
typedef struct script_source_record {
    qa_script_memory_allocation allocation;
    size_t memory_reference;
    uint8_t *bytes;
    bool detached;
} script_source_record;
typedef struct script_condition_record {
    qa_script_memory_allocation allocation;
    uint32_t pointer;
    size_t memory_reference;
    uint8_t *bytes;
    bool detached;
} script_condition_record;
typedef struct script_condition {
    bool skip, was_else;
    size_t frame;
} script_condition;
struct qa_script {
    qa_script_services services;
    qa_script_options options;
    script_macro_table macros;
    qa_script_defines *globals;
    qa_arena arena;
    script_frame *frames;
    size_t frame_count, frame_capacity, *stack, stack_count, stack_capacity;
    script_queued_token *queue;
    size_t queue_count, queue_capacity;
    qa_script_memory memory;
    script_source_record source_record;
    script_condition_record *conditions;
    size_t condition_records,condition_count,condition_capacity;
    uint32_t next_condition_pointer;
    size_t expansions, outputs;
    bool empty_expansion, memory_deferred;
    qa_script_location last_location;
    qa_script_token raw_token;
    script_queued_token *reads;
    size_t read_count, read_capacity;
    bool source_failure;
};
typedef struct script_checkpoint_storage {
    qa_arena arena;
} script_checkpoint_storage;
enum { SCRIPT_CHECKPOINT_VERSION = 4 };
bool script_source_create(qa_script *,qa_error *);
void script_source_stack(qa_script *);
bool script_source_capture(const qa_script *,qa_script_checkpoint *,qa_arena *,qa_error *);
bool script_source_restore(qa_script *,const qa_script_checkpoint *,qa_error *);
bool script_source_adopt(qa_script *,qa_error *);
void script_source_close(qa_script *,bool);
static inline uint32_t script_source_pointer(const qa_script *s) {
    return s->source_record.bytes?qa_load_u32le(s->source_record.bytes+SCRIPT_SOURCE_STACK):
        (s->stack_count?(uint32_t)s->stack[s->stack_count-1]+1:0);
}
static inline size_t script_current_frame(const qa_script *s) {
    return (size_t)script_source_pointer(s)-1;
}
static inline uint32_t script_skipping(const qa_script *s) {
    return s->source_record.bytes?qa_load_u32le(s->source_record.bytes+SCRIPT_SOURCE_SKIP):0;
}
static inline uint32_t script_indent_head(const qa_script *s) {
    return qa_load_u32le(s->source_record.bytes+SCRIPT_SOURCE_INDENT);
}
static inline void script_indent_head_set(qa_script *s,uint32_t value) {
    qa_store_u32le(s->source_record.bytes+SCRIPT_SOURCE_INDENT,value);
}
static inline void script_skipping_set(qa_script *s,uint32_t value) {
    qa_store_u32le(s->source_record.bytes+SCRIPT_SOURCE_SKIP,value);
}
bool script_memory_bind(qa_script *,qa_error *);
bool script_memory_enter(qa_script *,qa_error *);
bool script_condition_top(qa_script *,script_condition *,qa_error *);
bool script_condition_push(qa_script *,uint32_t,bool,size_t,qa_error *);
bool script_condition_pop(qa_script *,qa_error *);
bool script_conditions_capture(const qa_script *,qa_script_condition_state *,qa_error *);
bool script_conditions_restore(qa_script *,const qa_script_checkpoint *,qa_error *);
void script_conditions_close(qa_script *,bool);
bool script_checkpoint_valid(const qa_script_checkpoint *, qa_error *);
typedef struct script_eval_value {
    int32_t integer;
    double number;
} script_eval_value;
script_macro *script_macro_find(const script_macro_table *, qa_bytes);
bool script_macro_remove(script_macro_table *, qa_bytes, bool *fixed);
bool script_macro_parse(script_macro_table *, const qa_script_token *, size_t, size_t, qa_error *);
bool script_macro_text(script_macro_table *, const char *, size_t, qa_error *);
bool script_macro_copy(script_macro_table *, const qa_script_macro_state *, script_macro **,
                       qa_error *);
bool script_globals_import(script_macro_table *, const qa_script_defines *, qa_error *);
void script_table_clear(script_macro_table *);
bool script_fail(qa_script *, qa_script_location, const char *, qa_error *);
void script_warn(qa_script *, qa_script_location, const char *);
bool script_raw(qa_script *, script_queued_token *, bool *, qa_error *);
bool script_push(qa_script *, script_queued_token, qa_error *);
bool script_expand(qa_script *, script_queued_token, script_macro *, qa_error *);
bool script_expression(qa_script *, const qa_script_token *, size_t, bool, script_eval_value *,
                       qa_error *);
bool script_directive(qa_script *, script_queued_token, qa_error *);
bool script_include(qa_script *, const qa_script_include *, qa_error *);
bool script_line(qa_script *, qa_script_token **, size_t *, qa_error *);
bool script_line_token(qa_script *, script_queued_token *, bool *, qa_error *);
bool script_eval_directive(qa_script *, qa_script_location, bool, bool, qa_error *);
bool script_evaluate_stream(qa_script *, qa_script_location, bool, bool, script_eval_value *,
                            qa_error *);
static inline qa_bytes script_bytes(const char *s) {
    return (qa_bytes){(const uint8_t *)s, strlen(s)};
}
static inline bool script_bytes_equal(qa_bytes a, qa_bytes b) {
    return a.size == b.size && (a.size == 0 || memcmp(a.data, b.data, a.size) == 0);
}
#endif
