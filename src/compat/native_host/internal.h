#ifndef QA_NATIVE_HOST_INTERNAL_H
#define QA_NATIVE_HOST_INTERNAL_H

#include "qa/binary.h"
#include "qa/native_host.h"
#include "qa/native_host_q2_wire.h"
#include "qa/native_process_resources.h"
#include "qa/network.h"
#include "qa/source_save.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef enum native_host_kind {
    NATIVE_HOST_Q2_GAME,
    NATIVE_HOST_Q2_CGAME,
    NATIVE_HOST_Q3
} native_host_kind;

typedef struct native_host_string {
    char *text;
    qa_native_address address;
    size_t bytes;
    struct native_host_string *next;
} native_host_string;

typedef struct native_host_cvar_record {
    char *name;
    qa_native_address address;
    uint64_t modification;
    qa_cvar_handle handle;
    /* Publication receipt only. Values always come from the common table. */
    uint8_t published[56];
    bool published_valid;
    native_host_string *name_object, *value_object, *latched_object;
    struct native_host_cvar_record *order_previous, *order_next;
    struct native_host_cvar_record *next;
} native_host_cvar_record;

typedef struct native_host_memory_state {
    native_host_string *strings;
    native_host_cvar_record *cvar_shadows;
} native_host_memory_state;
bool native_host_memory_fields(qa_source_save_io *, qa_native_host *,
    native_host_memory_state *, bool qualify_addresses);
void native_host_memory_dispose(native_host_memory_state *);

typedef struct native_host_surface {
    qa_collision_surface surface;
    qa_native_address address;
    struct native_host_surface *next;
} native_host_surface;

typedef struct native_host_q2_lifetime {
    qa_actor_id actor;
    qa_vec3 creation_origin;
    uint64_t creation_frame;
    qa_native_host_q2_origin origins[8];
    bool present, linked, bot_registered;
} native_host_q2_lifetime;

typedef struct native_host_q2_fields {
    qa_native_host *host;
    uint32_t slot, velocity_offset, ground_offset;
    uint64_t body_serial;
    qa_world *bound_world;
    bool extended;
    qa_entity_body_fields body;
    qa_entity_collision_fields collision;
    void *write_context;
    bool (*write)(void *, const qa_body_state *, qa_error *);
} native_host_q2_fields;

typedef struct native_host_edict_layout {
    size_t bytes, inuse, linkcount, area, area2, flags;
    size_t mins, maxs, absmin, absmax, size, solid, clipmask, owner;
    size_t cluster_count, clusters, headnode;
} native_host_edict_layout;

typedef struct native_host_classic_layout {
    native_host_edict_layout edict;
    struct {
        size_t bytes, surface, contents, entity;
    } trace;
    struct {
        size_t bytes, touches, view_angles, view_height, mins, maxs;
        size_t ground, water_type, water_level;
    } pmove;
    struct {
        size_t bytes, string, latched, flags, modified, value, next;
    } cvar;
} native_host_classic_layout;

struct qa_native_host {
    native_host_kind kind;
    qa_native_profile profile;
    size_t pointer_bytes;
    const native_host_edict_layout *edict;
    const native_host_classic_layout *classic;
    qa_native_instance *instance;
    qa_native_process_resources *owned_process;
    qa_native_process_platform *owned_platform;
    qa_native_host_engine_services engine;
    qa_native_host_world_services world;
    /* Borrowed only while a checked completed-boundary observation is active. */
    const qa_source_frame *q2_observation_frame;
    qa_native_host_movement_services movement;
    qa_native_host_q2_application_fn q2_application;
    void *q2_application_context;
    uint32_t q2_seat;
    qa_native_address q2_hud_records;
    bool q2_seat_bound;
    qa_native_host_q3_bridge q3;
    qa_qvm_role q3_role;
    qa_qvm_abi q3_abi;
    qa_cvars *cvars;
    qa_console *console;
    qa_command_context command_context;
    native_host_string *strings;
    native_host_cvar_record *cvar_shadows, *cvar_order;
    uint64_t cvar_view_identity, cvar_revision;
    native_host_surface *surfaces;
    qa_entity_model_fields models;
    qa_entity_model_field *model_entries;
    native_host_q2_lifetime *q2_lifetimes;
    size_t q2_lifetime_capacity;
    native_host_q2_fields *q2_fields;
    uint32_t q2_field_capacity;
    qa_entity_references q2_references;
    bool q2_fields_reported, q2_fields_invalidated;
    uint8_t *message;
    size_t message_size, message_capacity;
    qa_native_host_message_reference *message_references;
    size_t message_reference_count, message_reference_capacity;
    bool message_failed;
    bool *retained_clients;
    size_t retained_capacity;
    size_t maximum_string_bytes;
    unsigned callback_depth, filter_depth;
    bool restoring, destroying;
    qa_native_host_reconstruction *reconstruction;
};

enum {
    NATIVE_Q2_RR_EDICT_BYTES = 1472,
    NATIVE_Q2_RR_CLIENT = 120,
    NATIVE_Q2_RR_SV = 128,
    NATIVE_Q2_RR_INUSE = 1376,
    NATIVE_Q2_RR_LINKED = 1377,
    NATIVE_Q2_RR_LINKCOUNT = 1380,
    NATIVE_Q2_RR_AREANUM = 1384,
    NATIVE_Q2_RR_AREANUM2 = 1388,
    NATIVE_Q2_RR_SVFLAGS = 1392,
    NATIVE_Q2_RR_MINS = 1396,
    NATIVE_Q2_RR_MAXS = 1408,
    NATIVE_Q2_RR_ABSMIN = 1420,
    NATIVE_Q2_RR_ABSMAX = 1432,
    NATIVE_Q2_RR_SIZE = 1444,
    NATIVE_Q2_RR_SOLID = 1456,
    NATIVE_Q2_RR_CLIPMASK = 1460,
    NATIVE_Q2_RR_OWNER = 1464
};

bool native_host_fail(qa_error *, qa_status, size_t, const char *);
bool native_host_import(void *, qa_native_instance *, const qa_native_import_call *,
                        qa_native_value *, qa_error *);
bool native_host_describe_syscall(void *, int32_t, const qa_native_value_type **,
                                  size_t *, qa_error *);
bool native_host_syscall(void *, qa_native_instance *, int32_t, const qa_native_value *,
                         size_t, intptr_t *, qa_error *);
bool native_host_capture(void *, qa_buffer *, qa_error *);
bool native_host_apply(void *, qa_bytes, qa_error *);

bool native_host_q2_import(qa_native_host *, const qa_native_import_call *,
                           qa_native_value *, qa_error *);
bool native_host_q3_import(qa_native_host *, const qa_native_import_call *,
                           qa_native_value *, qa_error *);
bool native_host_q3_dispatch(qa_native_host *, int32_t, const qa_native_value *, size_t,
                             qa_native_value *, qa_error *);

bool native_host_read(qa_native_host *, qa_native_address, void *, size_t, qa_error *);
bool native_host_write(qa_native_host *, qa_native_address, const void *, size_t, qa_error *);
bool native_host_store_pointer(const qa_native_host *, uint8_t *, qa_native_address,
                               qa_error *);
bool native_host_read_u8(qa_native_host *, qa_native_address, uint8_t *, qa_error *);
bool native_host_read_u32(qa_native_host *, qa_native_address, uint32_t *, qa_error *);
bool native_host_read_i32(qa_native_host *, qa_native_address, int32_t *, qa_error *);
bool native_host_write_u8(qa_native_host *, qa_native_address, uint8_t, qa_error *);
bool native_host_write_u32(qa_native_host *, qa_native_address, uint32_t, qa_error *);
bool native_host_write_i32(qa_native_host *, qa_native_address, int32_t, qa_error *);
bool native_host_read_vec3(qa_native_host *, qa_native_address, qa_vec3 *, qa_error *);
bool native_host_write_vec3(qa_native_host *, qa_native_address, qa_vec3, qa_error *);
bool native_host_string_read(qa_native_host *, qa_native_address, qa_buffer *, qa_error *);
bool native_host_string_address(qa_native_host *, const char *, qa_native_address *, qa_error *);
bool native_host_temporary_string(qa_native_host *, const char *, qa_native_address *, qa_error *);
void native_host_temporary_free(qa_native_host *, qa_native_address);
bool native_host_cvar(qa_native_host *, const char *, const char *, uint32_t, bool,
                      qa_native_address *, qa_error *);
bool native_host_refresh_cvars(qa_native_host *, qa_error *);
bool native_host_restore_cvars(qa_native_host *, qa_error *);

bool native_host_actor_for_address(qa_native_host *, qa_native_address, bool,
                                   qa_actor_id *, uint32_t *, qa_error *);
bool native_host_address_for_actor(qa_native_host *, qa_actor_id, qa_native_address *,
                                   qa_error *);
bool native_host_reconcile(qa_native_host *, qa_error *);
bool native_host_fields_refresh(qa_native_host *, qa_error *);
bool native_host_fields_bind(qa_native_host *, uint32_t, qa_error *);
bool native_host_link(qa_native_host *, qa_native_address, qa_error *);
bool native_host_q2_bot_register(qa_native_host *, qa_native_address, bool, qa_error *);
bool native_host_unlink(qa_native_host *, qa_native_address, qa_error *);
bool native_host_trace(qa_native_host *, const qa_native_import_call *, qa_native_value *,
                       bool, qa_error *);
bool native_host_box_edicts(qa_native_host *, const qa_native_import_call *,
                            qa_native_value *, qa_error *);
bool native_host_pmove(qa_native_host *, qa_native_address, qa_error *);

bool native_host_message_write(qa_native_host *, const qa_native_import_call *, qa_error *);
bool native_host_message_send(qa_native_host *, const qa_native_import_call *, qa_error *);
void native_host_message_clear(qa_native_host *);

static inline qa_native_address native_argument_address(const qa_native_import_call *call,
                                                        size_t index)
{
    return call->arguments[index].as.address;
}

static inline int32_t native_argument_i32(const qa_native_import_call *call, size_t index)
{
    return call->arguments[index].as.i32;
}

static inline uint32_t native_argument_u32(const qa_native_import_call *call, size_t index)
{
    return call->arguments[index].as.u32;
}

static inline float native_argument_f32(const qa_native_import_call *call, size_t index)
{
    return call->arguments[index].as.f32;
}

#endif
