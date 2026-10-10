#ifndef QA_QC_INTERNAL_H
#define QA_QC_INTERNAL_H

#include "qa/binary.h"
#include "qa/qc.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define QC_RESERVED_WORDS 28u
#define QC_RETURN_WORD 1u
#define QC_ARGUMENT_WORD(n) (4u + (n) * 3u)

typedef struct qc_engine_string {
    char *name;
    uint32_t offset, capacity;
} qc_engine_string;

typedef struct qc_strings {
    uint8_t *bytes;
    uint32_t used, capacity;
    qc_engine_string *engines;
    uint32_t engine_count, engine_capacity;
    bool quakeworld;
} qc_strings;

typedef struct qc_slot {
    qa_qc_slot_kind kind;
    qa_actor_id actor;
    qa_actor_owner owner;
    uint32_t source_slot;
} qc_slot;

typedef struct qc_body_context {
    struct qa_qc_instance *instance;
    uint32_t slot;
    qa_entity_body_fields fields;
    bool refreshing;
    uint64_t projection_revision, body_revision;
} qc_body_context;

typedef struct qc_frame {
    uint32_t function, statement, argument_count;
    uint32_t local_base, local_count;
} qc_frame;

typedef struct qc_boundary {
    struct qc_boundary *previous;
    uint64_t invocation;
    uint32_t function, caller, statement, argument_count;
    uint32_t staging[24], result[3];
    uint32_t staged_argument_count;
    bool active, used, executing, cancelled;
    bool result_valid, body_completed;
} qc_boundary;

typedef struct qc_inline_boundary {
    struct qc_inline_boundary *previous;
    uint64_t invocation;
    qa_qc_inline_region region;
    uint32_t frame_index;
    bool active, used, completed, skip;
} qc_inline_boundary;

typedef struct qc_name_index {
    qa_strings *names;
    uint32_t *ordinals;
} qc_name_index;

struct qa_qc_program {
    char *source;
    qa_qc_program_info info;
    qa_qc_statement *statements;
    qa_qc_definition *globals, *fields;
    qa_qc_function *functions;
    uint8_t *strings, *initial_globals;
    uint32_t string_bytes;
    qc_name_index names[3];
    qa_qc_game_fields engine_fields;
    qa_qc_engine_globals engine_globals;
};

struct qa_qc_instance {
    const qa_qc_program *program;
    qa_qc_options options;
    qa_qc_entity_layout layout;
    qa_qc_builtin_binding *bindings;
    char **extensions;
    qa_qc_inline_region *inline_regions;
    uint8_t *globals, *entities;
    qc_slot *slots;
    uint32_t *actor_slots;
    uint32_t actor_capacity;
    qc_body_context *bodies;
    qa_entity_references references;
    uint64_t *projected_words;
    size_t projection_stride;
    uint64_t projection_revision;
    uint64_t *profiles;
    qc_strings strings;
    qa_arena scratch;
    qc_frame *frames;
    uint32_t *locals;
    uint32_t entity_count, frame_count, local_count;
    uint32_t argument_count, execution_depth, callback_depth;
    uint32_t random_state;
    uint64_t statement_budget, next_invocation;
    uint32_t store_observer_depth, entity_access_depth;
    bool trace_enabled, destroying, checkpointing, capturing_checkpoint;
    qc_boundary *boundary;
    qc_boundary *cancelling;
    qc_inline_boundary *inline_boundary;
};

typedef struct qc_saved_slot {
    uint32_t slot, freed_at;
    qa_qc_slot_kind kind;
    qa_actor_owner owner;
    uint32_t source_slot;
    qa_saved_actor_id actor;
    bool has_actor;
} qc_saved_slot;

typedef struct qc_saved_word {
    uint32_t word, value;
} qc_saved_word;

typedef struct qc_saved_bytes {
    uint32_t offset, size;
    uint8_t *data;
} qc_saved_bytes;

struct qa_qc_checkpoint {
    uint16_t program_crc;
    qa_qc_profile profile;
    qa_qc_entity_layout layout;
    uint32_t entity_count, first_dynamic_slot, global_words;
    qc_saved_word *globals, *fields;
    uint32_t global_count, field_count;
    qc_saved_slot *slots;
    uint32_t slot_count;
    qc_saved_bytes *strings;
    uint32_t string_count, string_base, string_used;
    qc_engine_string *engine_strings;
    uint32_t engine_count;
    uint32_t random_state;
    qa_buffer host;
};

static inline bool qc_fail(qa_error *error, qa_status code, size_t offset,
                           const char *message)
{
    qa_error_set(error, code, offset, "%s", message);
    return false;
}

static inline uint32_t qc_load_word(const uint8_t *bytes, uint32_t word)
{
    return qa_load_u32le(bytes + (size_t)word * 4u);
}

static inline int32_t qc_load_int(const uint8_t *bytes, uint32_t word)
{
    return (int32_t)qc_load_word(bytes, word);
}

static inline float qc_load_float(const uint8_t *bytes, uint32_t word)
{
    return qa_load_f32le(bytes + (size_t)word * 4u);
}

static inline void qc_store_word(uint8_t *bytes, uint32_t word, uint32_t value)
{
    qa_store_u32le(bytes + (size_t)word * 4u, value);
}

static inline void qc_store_float(uint8_t *bytes, uint32_t word, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    qc_store_word(bytes, word, bits);
}

char *qc_strdup(const char *text, qa_error *error);
bool qc_span(size_t offset, size_t count, size_t stride, size_t size,
             size_t *bytes, qa_error *error);

bool qc_strings_create(qc_strings *strings, qa_bytes program, bool quakeworld,
                       qa_error *error);
bool qc_strings_reserve(qc_strings *strings, uint32_t length,
                         uint32_t *out, qa_error *error);
void qc_strings_destroy(qc_strings *strings);
bool qc_strings_get(const qc_strings *strings, int32_t id, const char **out,
                    qa_error *error);
bool qc_strings_allocate(qc_strings *strings, const char *text, int32_t *out,
                         qa_error *error);
bool qc_strings_engine(qc_strings *strings, const char *name, const char *text,
                       size_t capacity, int32_t *out, qa_error *error);
bool qc_global_range(const qa_qc_instance *instance, uint32_t word,
                     uint32_t count, qa_error *error);
bool qc_entity_slot(const qa_qc_instance *instance, int32_t reference,
                    uint32_t *slot, qa_error *error);
void qc_actor_slots_rebuild(qa_qc_instance *);
uint8_t *qc_entity_words(qa_qc_instance *instance, uint32_t slot);
const uint8_t *qc_entity_words_const(const qa_qc_instance *instance,
                                     uint32_t slot);
bool qc_entity_range(const qa_qc_instance *instance, uint32_t slot,
                     uint32_t word, uint32_t count, qa_error *error);
bool qc_pointer(const qa_qc_instance *instance, int32_t pointer,
                uint32_t count, uint32_t *slot, uint32_t *word,
                qa_error *error);
bool qc_write_global(qa_qc_instance *instance, uint32_t word,
                     const uint32_t *values, uint32_t count, qa_error *error);
bool qc_write_entity(qa_qc_instance *instance, uint32_t slot, uint32_t word,
                     const uint32_t *values, uint32_t count, qa_error *error);
bool qc_project_entity(qa_qc_instance *instance, uint32_t slot, uint32_t word,
                       const uint32_t *values, uint32_t count,
                       qa_error *error);
bool qc_prepare_entity_access(qa_qc_instance *instance, uint32_t slot,
                              uint32_t word, uint32_t count,
                              qa_qc_entity_access_kind kind,
                              bool retain_projection,
                              qa_error *error);
void qc_projection_invalidate(qa_qc_instance *instance);

const qa_qc_builtin_requirement *qc_builtin_number(qa_qc_profile profile,
                                                    int32_t number);
const qa_qc_builtin_requirement *qc_builtin_name(qa_qc_profile profile,
                                                  const char *name);
const qa_qc_builtin_binding *qc_builtin_binding(const qa_qc_instance *instance,
                                                qa_qc_builtin builtin,
                                                const char *name);
bool qc_builtin_available(const qa_qc_instance *instance,
                          const qa_qc_builtin_requirement *requirement);
bool qc_builtin_call(qa_qc_instance *instance, int32_t number,
                     const char *name, qa_error *error);
bool qc_builtin_is_pure(qa_qc_profile profile, int32_t number);

bool qc_machine_call(qa_qc_instance *instance, uint32_t function,
                     uint32_t argument_count, qa_error *error);
bool qc_machine_body(qa_qc_instance *instance, uint32_t function,
                     uint32_t argument_count, qa_error *error);
bool qc_machine_inline_continue(qa_qc_instance *instance,
                                qc_inline_boundary *boundary,
                                qa_error *error);

bool qc_host_builtin(qa_qc_instance *instance,
                     const qa_qc_builtin_requirement *requirement,
                     qa_error *error);
bool qc_host_spawn(qa_qc_instance *instance, int32_t *reference,
                   qa_error *error);
bool qc_host_remove(qa_qc_instance *instance, int32_t reference,
                    qa_error *error);
bool qc_sync_body_from_fields(qa_qc_instance *instance, uint32_t slot,
                              qa_error *error);
bool qc_refresh_borrowed(qa_qc_instance *instance, uint32_t slot,
                         qa_error *error);

#endif
