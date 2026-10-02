#ifndef QA_Q3_HOST_INTERNAL_H
#define QA_Q3_HOST_INTERNAL_H

#include "qa/q3_host.h"
#include "qa/binary.h"
#include "qa/arena.h"
#include "qa/script.h"
#include "qa/common_parse.h"
#include "qa/navigation_asset.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef enum q3_file_kind { Q3_FILE_CLOSED, Q3_FILE_READ, Q3_FILE_WRITE } q3_file_kind;
typedef struct q3_file {
    q3_file_kind kind;
    uint64_t serial;
    qa_resource *resource;
    qa_buffer restored_bytes;
    char *restored_path;
    qa_sha256_digest restored_digest;
    qa_q3_host_write_file *writable;
    uint64_t position;
    bool zip;
} q3_file;

typedef struct q3_script {
    qa_script *reader;
    unsigned operations;
    bool retired;
} q3_script;

typedef struct q3_crossings {
    struct q3_crossings *next;
    qa_nav_crossings storage;
    bool busy;
} q3_crossings;

typedef struct q3_entity_slot {
    qa_q3_host *host;
    qa_actor_id actor;
    uint32_t number, input_motion;
    bool borrowed, input_retired, has_visibility;
    int32_t area, area2, last_cluster, clusters[16];
    uint32_t cluster_count;
} q3_entity_slot;

typedef struct q3_portal_target { uint32_t portal, multiplicity; } q3_portal_target;
typedef struct q3_portal_reference {
    uint32_t first, second, count;
    q3_portal_target *targets;
    size_t target_count;
} q3_portal_reference;

typedef struct q3_game_data {
    uint64_t entities, clients;
    uint32_t entity_count, entity_stride, client_stride;
    q3_entity_slot slots[1024];
    q3_portal_reference *portals;
    size_t portal_count, portal_capacity;
} q3_game_data;

typedef struct q3_cvar_binding {
    qa_q3_host_cvar_namespace reference;
    size_t handle;
    qa_cvars *registry;
    char *name,*previous_value;
    uint64_t previous_modification;
    float previous_number;
    int32_t previous_integer;
    uint64_t revision;
    qa_console_dialect dialect;
    bool read,previous_current;
} q3_cvar_binding;
typedef struct q3_cvar_cache {
    qa_qvm *vm;
    qa_native_instance *native;
    qa_native_host_guest_memory memory;
    int32_t pointer,handle;
    uint64_t address;
} q3_cvar_cache;
typedef struct q3_cvar_status {
    char *previous_value;
    uint64_t previous_modification;
    float previous_number;
    int32_t previous_integer;
    uint32_t revision;
    bool read,previous_visible;
} q3_cvar_status;

struct qa_q3_host {
    qa_q3_host_options options;
    q3_file files[64];
    uint64_t file_serial;
    qa_arena scratch;
    q3_crossings *crossings;
    q3_script *scripts[64];
    bool script_pending[64];
    uint64_t script_generation;
    qa_common_cursor entity_cursor;
    qa_common_parser entity_parser;
    qa_bounds clip_bounds, clip_brush;
    qa_qvm *vm;
    qa_native_instance *native;
    qa_native_host *native_host;
    qa_native_host_guest_memory memory;
    qa_native_profile native_profile;
    q3_game_data *game;
    q3_cvar_binding *cvar_bindings;
    size_t cvar_binding_count;
    q3_cvar_cache *cvar_caches;
    size_t cvar_cache_count;
    q3_cvar_status cvar_status;
    qa_actor_definition slot_definition;
    unsigned calls;
    const struct q3_call *render_call;
    const struct q3_call *system_movie_call;
    bool retired, restore_pending, scripts_reporting, bots_shutdown;
};

typedef struct q3_call {
    qa_q3_host *host;
    qa_native_host_guest_memory memory;
    int32_t service;
    int32_t source_service;
    uint64_t arguments[16];
    size_t count;
    qa_qvm *vm;
    const qa_qvm_call *source_call;
    qa_native_instance *native;
    qa_native_profile native_profile;
    qa_native_host *native_host;
} q3_call;

typedef enum q3_service_result {
    Q3_UNHANDLED, Q3_COMPLETED, Q3_FAILED
} q3_service_result;

typedef q3_service_result (*q3_handler)(q3_call *, int32_t *, qa_error *);
typedef struct q3_signature {
    uint8_t count;
    qa_native_value_type arguments[16];
} q3_signature;

typedef struct q3_record {
    qa_q3_abi_record abi;
    const q3_call *call;
    uint64_t address;
} q3_record;

bool q3_fail(qa_error *, qa_status, size_t, const char *);
bool q3_bot_client_number(const q3_call *, int32_t source, int32_t *, qa_error *);
qa_bot_runtime *q3_bot_runtime(const q3_call *);
bool q3_bot_entity_number(const q3_call *, int32_t source, int32_t *, qa_error *);
bool q3_bot_source_entity(const q3_call *, int32_t canonical, int32_t *, qa_error *);
bool q3_vm_span(qa_qvm *, uint64_t, size_t, qa_bytes *, qa_error *);
bool q3_read(const q3_call *, uint64_t, void *, size_t, qa_error *);
bool q3_write(const q3_call *, uint64_t, qa_bytes, qa_error *);
bool q3_string(const q3_call *, uint64_t, qa_buffer *, qa_error *);
bool q3_write_string(const q3_call *, uint64_t, const char *, int32_t, qa_error *);
bool q3_write_word(const q3_call *, uint64_t, uint32_t, qa_error *);
bool q3_write_float(const q3_call *, uint64_t, float, qa_error *);
int32_t q3_float_bits(float);
int32_t q3_integer(const q3_call *, size_t);
float q3_float(const q3_call *, size_t);
bool q3_vector(const q3_call *, uint64_t, qa_vec3 *, qa_error *);
bool q3_write_vector(const q3_call *, uint64_t, qa_vec3, qa_error *);
bool q3_record_open(const q3_call *, uint64_t, size_t, q3_record *, qa_error *);
bool q3_signature_find(qa_qvm_role, qa_qvm_abi, int32_t, const q3_signature **,
                        int32_t *, qa_error *);
bool q3_ql_service(int32_t source, int32_t *canonical, qa_error *);
q3_service_result q3_common(q3_call *, int32_t *, qa_error *);
q3_service_result q3_cvars(q3_call *, int32_t *, qa_error *);
void q3_cvars_bindings_free(q3_cvar_binding *,size_t);
bool q3_cvars_bindings_capture(const qa_q3_host *,qa_buffer *,qa_error *);
bool q3_cvars_bindings_decode(qa_bytes,q3_cvar_binding **,size_t *,q3_cvar_cache **,size_t *,q3_cvar_status *,qa_error *);
bool q3_cvars_bindings_restore_ready(qa_q3_host *,qa_error *);
q3_service_result q3_files(q3_call *, int32_t *, qa_error *);
q3_service_result q3_information(q3_call *, int32_t *, qa_error *);
q3_service_result q3_client_state(q3_call *, int32_t *, qa_error *);
q3_service_result q3_scripts(q3_call *, int32_t *, qa_error *);
q3_service_result q3_bot_actions(q3_call *, int32_t *, qa_error *);
q3_service_result q3_bot_library(q3_call *, int32_t *, qa_error *);
q3_service_result q3_bot_chat(q3_call *, int32_t *, qa_error *);
q3_service_result q3_bot_goals(q3_call *, int32_t *, qa_error *);
q3_service_result q3_bot_genetic(q3_call *, int32_t *, qa_error *);
q3_service_result q3_bot_weapons(q3_call *, int32_t *, qa_error *);
q3_service_result q3_bot_navigation(q3_call *, int32_t *, qa_error *);
q3_service_result q3_bot_movement(q3_call *, int32_t *, qa_error *);
q3_service_result q3_client_collision(q3_call *, int32_t *, qa_error *);
q3_service_result q3_client_input(q3_call *, int32_t *, qa_error *);
q3_service_result q3_client_keys(q3_call *, int32_t *, qa_error *);
q3_service_result q3_game_records(q3_call *, int32_t *, qa_error *);
q3_service_result q3_game_spatial(q3_call *, int32_t *, qa_error *);
q3_service_result q3_entity_tokens(q3_call *, int32_t *, qa_error *);
q3_service_result q3_presentation(q3_call *, int32_t *, qa_error *);
bool q3_game_begin(qa_q3_host *, q3_call *, qa_error *);
bool q3_game_end(q3_call *, bool);
bool q3_game_entity_record(q3_call *, uint32_t, q3_record *, qa_error *);
bool q3_game_player_record(q3_call *, uint32_t, q3_record *, qa_error *);
bool q3_game_pointer_slot(q3_call *, uint64_t, uint32_t *, qa_error *);
bool q3_game_collision(void *, qa_actor_collision *, qa_error *);
bool q3_game_portal(qa_q3_host *, int32_t first, int32_t second, bool open, qa_error *);
bool q3_game_close_portals(qa_q3_host *, qa_error *);
bool q3_game_bind_restored(qa_q3_host *, qa_error *);
bool q3_game_checkpoint_capture(qa_q3_host *, qa_buffer *, qa_error *);
bool q3_game_checkpoint_decode(qa_q3_host *, qa_bytes, q3_game_data **, qa_error *);
bool q3_game_checkpoint_install(qa_q3_host *, q3_game_data *, qa_error *);
void q3_game_checkpoint_free(q3_game_data *);
bool q3_game_portal_reference(qa_q3_host *, uint32_t first, uint32_t second,
                              q3_portal_reference *, qa_error *);
size_t q3_shared_offset(qa_qvm_abi, size_t modern);
void q3_file_close(q3_file *);
void q3_script_close(q3_script *);
qa_script_services q3_script_services(qa_q3_host *);

#endif
