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

enum { SCRIPT_LEXER_BYTES=2148, SCRIPT_PUNCTUATION_BYTES=1024, SCRIPT_TOKEN_BYTES=1068,
    SCRIPT_LEXER_BUFFER=1024, SCRIPT_LEXER_POINTER=1028, SCRIPT_LEXER_END=1032,
    SCRIPT_LEXER_LAST_POINTER=1036, SCRIPT_LEXER_WHITESPACE=1040, SCRIPT_LEXER_END_WHITESPACE=1044,
    SCRIPT_LEXER_LENGTH=1048, SCRIPT_LEXER_LINE=1052, SCRIPT_LEXER_LAST_LINE=1056,
    SCRIPT_LEXER_AVAILABLE=1060, SCRIPT_LEXER_FLAGS=1064, SCRIPT_LEXER_PUNCTUATIONS=1068,
    SCRIPT_LEXER_TABLE=1072, SCRIPT_LEXER_TOKEN=1076, SCRIPT_LEXER_NEXT=2144 };
typedef struct script_lexer_allocation {
    qa_script_memory_allocation allocation;
    size_t reference;
    uint8_t *bytes;
    uint32_t size;
    bool detached,retired;
} script_lexer_allocation;
struct qa_script_lexer {
    qa_bytes input;
    qa_script_lexer_options options;
    qa_script_memory memory;
    script_lexer_allocation record,table;
    uint32_t column;
    qa_script_location token_location;
    qa_bytes token_whitespace;
    size_t token_extent;
    const char *path;
    qa_arena arena;
    const qa_script_punctuation *punctuations;
    const int32_t *heads, *next;
    qa_script_punctuation *owned_punctuations;
    int32_t *owned_index;
    size_t punctuation_count;
    bool source_failure,released;
};
typedef struct script_lexer_cursor {size_t offset;uint32_t line,column;} script_lexer_cursor;
static inline size_t script_lexer_offset(const qa_script_lexer *l) {
    return (size_t)qa_load_u32le(l->record.bytes+SCRIPT_LEXER_POINTER)-SCRIPT_LEXER_BYTES;
}
static inline void script_lexer_offset_set(qa_script_lexer *l,size_t value) {
    qa_store_u32le(l->record.bytes+SCRIPT_LEXER_POINTER,(uint32_t)value+SCRIPT_LEXER_BYTES);
}
static inline uint32_t script_lexer_line(const qa_script_lexer *l) {
    return qa_load_u32le(l->record.bytes+SCRIPT_LEXER_LINE);
}
static inline void script_lexer_line_set(qa_script_lexer *l,uint32_t value) {
    qa_store_u32le(l->record.bytes+SCRIPT_LEXER_LINE,value);
}
static inline uint32_t script_lexer_flags(const qa_script_lexer *l) {
    return qa_load_u32le(l->record.bytes+SCRIPT_LEXER_FLAGS);
}
static inline bool script_lexer_available(const qa_script_lexer *l) {
    return qa_load_u32le(l->record.bytes+SCRIPT_LEXER_AVAILABLE)!=0;
}
static inline void script_lexer_available_set(qa_script_lexer *l,bool value) {
    qa_store_u32le(l->record.bytes+SCRIPT_LEXER_AVAILABLE,value);
}
static inline script_lexer_cursor script_lexer_cursor_get(const qa_script_lexer *l) {
    return (script_lexer_cursor){script_lexer_offset(l),script_lexer_line(l),l->column};
}
static inline void script_lexer_cursor_set(qa_script_lexer *l,script_lexer_cursor cursor) {
    script_lexer_offset_set(l,cursor.offset);script_lexer_line_set(l,cursor.line);l->column=cursor.column;
}
bool script_token_store(uint8_t *,const qa_script_token *,uint32_t,uint32_t,qa_error *);
bool script_token_saved_valid(const qa_script_queued_state *,qa_error *);
bool script_token_load(const uint8_t *,size_t,qa_script_location,qa_bytes,qa_arena *,qa_script_token *,qa_error *);
bool script_lexer_memory_bind(qa_script_lexer *,qa_error *);
bool script_lexer_memory_open(qa_script_lexer *,const char *,qa_bytes,qa_error *);
bool script_lexer_punctuation_open(qa_script_lexer *,qa_error *);
void script_lexer_copy_text(qa_script_lexer *,qa_bytes);
void script_lexer_compress(qa_script_lexer *);
bool script_lexer_memory_validate(qa_script_lexer *,qa_error *);
void script_lexer_memory_close(qa_script_lexer *,bool);
void script_lexer_dispose(qa_script_lexer *);
bool script_lexer_retire(qa_script_lexer *,qa_error *);
bool script_lexer_frame_capture(const qa_script_lexer *,qa_script_frame_state *,qa_arena *,qa_error *);
bool script_lexer_frame_valid(const qa_script_frame_state *,qa_error *);
bool script_lexer_frame_restore(const qa_script_lexer_options *,const qa_script_frame_state *,bool,qa_script_lexer **,qa_error *);
bool script_lexer_memory_restore(qa_script_lexer *,const qa_script_frame_state *,bool,qa_error *);
bool script_lexer_adopt(qa_script_lexer *,qa_error *);
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
typedef struct script_expansion script_expansion;
typedef struct script_macro_table script_macro_table;
typedef struct script_queued_token {
    qa_script_token token;
    const script_expansion *expansion;
    uint8_t bytes[SCRIPT_TOKEN_BYTES];
    bool raw;
} script_queued_token;
typedef struct script_token_record {
    script_lexer_allocation record;
    uint32_t pointer;
    qa_script_location location;
    qa_bytes whitespace;
    size_t extent;
    const script_expansion *expansion;
} script_token_record;
typedef struct script_macro {
    struct script_macro *registry_next;
    script_macro_table *owner;
    script_lexer_allocation record;
    uint32_t pointer;
    bool published;
} script_macro;
struct script_macro_table {
    qa_script *source;
    script_macro *records;
    script_lexer_allocation hash;
    script_token_record *queue;
    size_t queue_count,queue_records,queue_capacity;
    uint32_t next_token_pointer,next_define_pointer,first;
    size_t count;
    qa_script_memory memory;
    qa_arena arena;
    bool global,retained,deferred;
};
struct qa_script_defines {
    atomic_uint references;
    script_macro_table table;
};
struct script_expansion {
    uint32_t macro;
    const script_expansion *parent;
};
typedef struct script_frame {
    qa_script_resource resource;
    qa_script_lexer *lexer;
    size_t condition_base, token_count;
    bool active, owned;
} script_frame;
enum { SCRIPT_SOURCE_BYTES=3144, SCRIPT_SOURCE_INCLUDE=1024,
       SCRIPT_SOURCE_HASH=2064, SCRIPT_SOURCE_STACK=2052, SCRIPT_SOURCE_TOKENS=2056, SCRIPT_SOURCE_TOKEN=2076, SCRIPT_SOURCE_INDENT=2068, SCRIPT_SOURCE_SKIP=2072 };
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
enum { SCRIPT_CHECKPOINT_VERSION = 7 };
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
bool script_queue_pop(qa_script *,script_queued_token *,qa_error *);
bool script_queue_snapshot(const script_macro_table *,qa_script_queued_state *,qa_arena *,qa_error *);
bool script_queue_restore(script_macro_table *,const qa_script_checkpoint *,const script_expansion *,qa_error *);
bool script_queue_adopt(script_macro_table *,bool,qa_error *);
void script_queue_close(qa_script *,bool);
script_token_record *script_heap_token(const script_macro_table *,uint32_t);
bool script_heap_copy_token(script_macro_table *,script_queued_token,script_token_record **,qa_error *);
bool script_heap_free_token(script_macro_table *,script_token_record *,qa_error *);
bool script_heap_free_chain(script_macro_table *,uint32_t,qa_error *);
void script_token_float(uint8_t *,double);
bool script_heap_token_bytes(const script_macro_table *,script_token_record *,qa_error *);
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
uint32_t script_macro_hash(qa_bytes);
bool script_read_nested(qa_script *,script_queued_token *,bool *,qa_error *);
bool script_macro_lookup(const script_macro_table *,qa_bytes,script_macro **,qa_error *);
script_macro *script_macro_find(const script_macro_table *, qa_bytes);
bool script_macro_remove(script_macro_table *, qa_bytes, bool *fixed,qa_error *);
bool script_macro_text(script_macro_table *, const char *, size_t, qa_error *);
bool script_globals_import(script_macro_table *, const qa_script_defines *, qa_error *);
bool script_table_clear(script_macro_table *,qa_error *);
bool script_table_open(script_macro_table *,const qa_script_memory *,bool,qa_error *);
void script_table_dispose(script_macro_table *,bool);
bool script_table_adopt(script_macro_table *,bool,qa_error *);
bool script_table_hash_bind(const script_macro_table *,qa_error *);
script_macro *script_macro_resolve(const script_macro_table *,uint32_t);
qa_bytes script_macro_name(const script_macro *);
bool script_macro_bind(script_macro *,qa_error *);
uint32_t script_macro_word(const script_macro *,size_t);
void script_macro_word_set(script_macro *,size_t,uint32_t);
bool script_table_saved_valid(const qa_script_checkpoint *,bool,qa_error *);
bool script_macro_project(const script_macro *,qa_script_macro_state *,qa_arena *,qa_error *);
bool script_macro_publish(script_macro_table *,script_macro *,qa_error *);
bool script_macro_allocate(script_macro_table *,qa_bytes,bool,script_macro **,qa_error *);
bool script_macro_free(script_macro *,qa_error *);
bool script_macro_add_token(script_macro *,size_t,script_queued_token,uint32_t *,qa_error *);
bool script_define_stream(qa_script *,qa_script_location,qa_error *);
bool script_table_capture(script_macro_table *,qa_script_checkpoint *,qa_arena *,qa_error *);
bool script_table_restore(script_macro_table *,const qa_script_checkpoint *,qa_error *);
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
