#ifndef QA_NATIVE_INTERNAL_H
#define QA_NATIVE_INTERNAL_H

#include "qa/binary.h"
#include "qa/json.h"
#include "qa/native.h"
#include "qa/native_region_scope.h"
#include "qa/native_observe.h"
#include "qa/native_process.h"
#include "guest/abi.h"
#include "qa/filesystem.h"
#include "qa/network_unified_frame_pool.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NATIVE_MAX_ARGUMENTS 32u
#define NATIVE_MAX_STRING (1024u * 1024u)
#define NATIVE_MAX_WATCH_BYTES 65536u

typedef struct native_region_slot native_region_slot;

typedef struct native_type_spec {
    qa_native_type public_type;
} native_type_spec;

typedef struct native_signature_spec {
    const char *name;
    qa_native_signature signature;
    uint32_t slot;
    bool optional;
    qa_native_q2_import_kind dispatch;
} native_signature_spec;

typedef struct native_profile_spec {
    qa_native_profile profile;
    const char *entry_export;
    uint32_t api_version;
    const native_signature_spec *imports;
    size_t import_count;
    const native_signature_spec *entries;
    size_t entry_count;
    size_t import_prefix;
    size_t entity_field_after;
    bool q2_table;
    bool q3_vm;
    bool quake_live;
} native_profile_spec;

typedef struct native_import_binding {
    struct qa_native_instance *instance;
    native_signature_spec spec;
    guest_abi_plan *guest_plan;
    uint64_t guest_id, guest_address;
} native_import_binding;

typedef struct native_entry_binding {
    native_signature_spec spec;
    qa_native_address address;
} native_entry_binding;

typedef struct native_allocation {
    struct native_allocation *next;
    qa_native_address guest_address;
    size_t size;
    int32_t tag;
} native_allocation;

typedef struct native_slot {
    qa_native_slot_kind kind;
    qa_actor_id actor;
    qa_actor_owner owner;
    uint32_t source_slot;
} native_slot;
typedef struct native_process_temporary {
    struct native_process_temporary *next;
    qa_fs_root *root;
    uint64_t resource_root;
} native_process_temporary;

struct qa_native_module {
    size_t references;
    uint8_t *bytes;
    size_t size;
    char *source;
    qa_native_module_info info;
};

struct qa_native_instance {
    qa_native_module *module;
    qa_native_options options;
    const qa_native_declaration *declaration_ref;
    bool has_declaration;
    qa_native_lifecycle lifecycle;
    qa_native_address image_base;
    uint64_t image_bytes;
    void *import_table;
    size_t import_table_bytes;
    qa_native_address export_table;
    native_import_binding *imports;
    size_t import_count;
    native_entry_binding *entries;
    size_t entry_count;
    native_allocation *allocations;
    qa_native_entity_table entities;
    bool entity_views_retired;
    native_slot *slots;
    uint32_t slot_capacity;
    qa_native_sysv_process *sysv_process;
    qa_native_process_resource_services process_resources;
    native_process_temporary *process_temporaries;
    qa_native_windows_process *windows_process;
    qa_native_guest *guest;
    qa_native_process_kind process_kind;
    uint64_t source_id, first_callback, callback_base, import_table_address;
    char *source_library;
    qa_buffer process_host;
    bool process_host_pending;
    native_region_slot *regions;
    size_t region_count;
    qa_native_region_scope *region_scopes;
    qa_native_entry_observer *entry_observers;
    qa_native_entry_observer *pending_entry_observers;
    qa_native_write_observer *write_observers;
    qa_unified_frame_pool *observation_storage;
    uint64_t next_observer_id;
    uint32_t active_depth, callback_depth, region_depth, write_depth;
    const qa_native_write_event *active_write_event;
    struct qa_native_write_scope *write_scope;
    struct qa_native_call_scope *call_scope;
    const qa_native_region_event *active_region_event;
    uint32_t region_callback_depth, region_call_depth;
    qa_native_address region_invocation_entry;
    uint32_t region_invocation_depth;
    bool *invoke_entered;
    bool checkpointing, destroying, unloading, pending_shutdown, pending_restart,
        pending_initialize, restart_original_ready, shutdown_entry, failed, process_observing;
    qa_error failure;
};

static inline void native_entity_notify(qa_native_instance *instance,
    qa_native_entity_change change, uint32_t slot, qa_native_address address, size_t bytes) {
    if (change == QA_NATIVE_ENTITIES_INVALIDATE && !address) instance->entity_views_retired = true;
    else if (change == QA_NATIVE_ENTITIES_TABLE) instance->entity_views_retired = false;
    if (instance->options.entity_changed) {
        const qa_native_entity_event event = {change, slot, address, bytes};
        instance->options.entity_changed(instance->options.context, instance, &event);
    }
}
static inline void native_entity_changed(qa_native_instance *instance,
    qa_native_entity_change change, uint32_t slot) {
    native_entity_notify(instance, change, slot, 0, 0);
}

struct qa_native_write_scope {
    struct qa_native_write_scope *previous;
    qa_native_instance *instance;
    qa_unified_frame_lease *storage;
    const qa_native_write_event *event;
    uint32_t depth, invocation_depth;
};

struct qa_native_entry_observer {
    qa_native_entry_observer *next;
    qa_native_instance *instance;
    uint64_t id;
    qa_native_address address;
    qa_native_signature signature;
    qa_native_entry_observer_fn callback;
    void *context;
    uint32_t active_calls;
    guest_abi_plan *guest_plan;
    uint64_t guest_id;
};

struct qa_native_write_observer {
    qa_native_write_observer *next;
    qa_native_instance *instance;
    uint64_t id;
    qa_native_address address;
    size_t size;
    qa_native_write_observer_fn callback;
    void *context;
    uint32_t active_calls;
    qa_buffer snapshot;
};

struct qa_native_region_binding {
    qa_native_region_binding *previous;
    qa_native_region_binding *next;
    struct qa_native_instance *instance;
    native_region_slot *region;
    qa_native_region_fn callback;
    void *context;
};

struct native_region_slot {
    qa_native_declared_region definition;
    char *path;
    qa_native_region_binding *first;
    qa_native_region_binding *last;
};

struct qa_native_declaration {
    size_t references;
    uint8_t *json;
    size_t json_size;
    size_t primary_offset, primary_size;
    bool callbacks;
    qa_json_document *document;
    qa_json_id primary;
    native_region_slot *regions;
    size_t region_count;
};

extern _Thread_local qa_native_instance *native_active_instance;
bool native_image_soname(qa_bytes, qa_bytes *, qa_error *);

/* Set only at a validated source-call or owned guest dispatch boundary. */
static inline void native_call_started(qa_native_instance *instance) {
    if (instance && instance->invoke_entered) *instance->invoke_entered = true;
    if (instance && instance->pending_shutdown) {
        instance->pending_shutdown = false;
        instance->lifecycle = QA_NATIVE_SHUT_DOWN;
        instance->shutdown_entry = true;
        instance->restart_original_ready = false;
        instance->pending_restart = false;
    }
    if (instance && instance->pending_initialize) {
        instance->pending_initialize = false;
        instance->lifecycle = QA_NATIVE_INITIALIZED;
    }
}

static inline bool native_fail(qa_error *error, qa_status code, size_t offset,
                               const char *message) {
    qa_error_set(error, code, offset, "%s", message);
    return false;
}

static inline size_t native_align(size_t value, size_t alignment) {
    return alignment && value % alignment ? value + alignment - value % alignment : value;
}

static inline bool native_u64_fits_size(uint64_t value) {
#if SIZE_MAX < UINT64_MAX
    return value <= (uint64_t)SIZE_MAX;
#else
    (void)value;
    return true;
#endif
}

char *native_strdup(const char *text, qa_error *error);
bool native_copy_bytes(qa_bytes source, qa_buffer *out, qa_error *error);
bool native_size_add(size_t left, size_t right, size_t *out);
bool native_size_multiply(size_t left, size_t right, size_t *out);

const native_profile_spec *native_profile(qa_native_profile profile);
bool native_profile_accepts(const native_profile_spec *profile, const qa_native_image_info *image,
                            qa_error *error);
bool native_profile_bind(qa_native_instance *instance, qa_error *error);
bool native_profile_restore_tables(qa_native_instance *instance, qa_error *error);
bool native_profile_prepare_tables(qa_native_instance *instance, qa_error *error);
void native_profile_unbind(qa_native_instance *instance);
bool native_profile_refresh_entities(qa_native_instance *instance, qa_error *error);
const native_entry_binding *native_entry(const qa_native_instance *instance, const char *name);

bool native_import_bind(native_import_binding *binding, qa_native_instance *instance,
                        const native_signature_spec *spec, qa_error *error);
void native_import_unbind(native_import_binding *binding);
bool native_process_open(qa_native_instance *, const qa_native_process_options *, qa_error *);
bool native_process_close(qa_native_instance *, qa_error *);
bool native_process_reload(qa_native_instance *, qa_error *);
bool native_process_export(const qa_native_instance *, const char *, qa_native_address *, qa_error *);
bool native_process_invoke(qa_native_instance *, qa_native_address,
    const qa_native_signature *, const qa_native_value *, size_t, qa_native_value *, qa_error *);
bool native_process_import_bind(native_import_binding *, qa_native_instance *,
    const native_signature_spec *, qa_error *);
bool native_process_q3_bind(qa_native_instance *, qa_native_address *, qa_error *);
bool native_process_restore(qa_native_instance *, const qa_native_process_options *, qa_error *);
bool native_process_publish(qa_native_instance *, qa_native_instance *, qa_error *);
bool native_process_checkpoint_host(qa_native_instance *, qa_bytes, qa_buffer *, qa_error *);
bool native_region_event(qa_native_instance *instance, const qa_native_region_event *event,
                                qa_native_region_decision *decision, qa_error *error);
bool native_region_scopes_instruction(qa_native_instance *, uint64_t, bool *, qa_error *);
struct qa_source_save_io;
bool native_observers_fields(struct qa_source_save_io *, qa_native_instance *);
bool native_observers_resolve(qa_native_instance *, uint64_t, uint64_t,
    qa_native_guest_callback *, qa_error *);
void native_observers_destroy(qa_native_instance *instance);
bool native_process_write_commit(void *, qa_native_guest *, const qa_native_guest_commit *, qa_error *);

bool native_entity_table_store(qa_native_instance *instance, qa_native_entity_table table,
                               qa_error *error);

bool native_regions_copy(qa_native_instance *instance, const qa_native_declaration *declaration,
                         qa_error *error);
void native_regions_destroy(qa_native_instance *instance);
bool native_regions_descriptor(const qa_native_instance *instance, qa_buffer *out, qa_error *error);
bool native_process_region_instruction(void *, qa_native_guest *, uint64_t, qa_error *);
bool native_instance_setup_identity(qa_native_instance *instance, const qa_native_options *options,
                                    qa_error *error);

bool native_call_binding(qa_native_instance *instance, const native_entry_binding *binding,
                         const qa_native_value *arguments, size_t count, qa_native_value *result,
                         qa_error *error);
bool native_invoke_entry(qa_native_instance *, qa_native_address,
                         const qa_native_signature *, const qa_native_value *, size_t,
                         qa_native_value *, qa_error *);
bool native_dispatch_import(qa_native_instance *instance, const native_signature_spec *spec,
                            const qa_native_value *arguments, size_t count,
                            qa_native_value *result);
void native_latch_error(qa_native_instance *instance, const qa_error *error);

#endif
