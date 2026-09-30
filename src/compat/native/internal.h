#ifndef QA_NATIVE_INTERNAL_H
#define QA_NATIVE_INTERNAL_H

#include "qa/binary.h"
#include "qa/json.h"
#include "qa/native.h"
#include "qa/native_observe.h"
#include "hook_control.h"

#include <ffi.h>

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NATIVE_CHECKPOINT_VERSION 2u
#define NATIVE_MAX_ARGUMENTS 32u
#define NATIVE_MAX_STRING (1024u * 1024u)
#define NATIVE_DEFAULT_MAX_FRAME (256u * 1024u * 1024u)

typedef struct native_runner_connection native_runner_connection;
typedef struct native_region_slot native_region_slot;

typedef struct native_type_spec {
    qa_native_type public_type;
} native_type_spec;

typedef struct native_signature_spec {
    const char *name;
    qa_native_signature signature;
    uint32_t slot;
    bool optional;
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

typedef struct native_owned_ffi_type {
    ffi_type *type;
    ffi_type **elements;
} native_owned_ffi_type;

typedef struct native_ffi_signature {
    ffi_cif cif;
    ffi_type **arguments;
    ffi_type *result;
    native_owned_ffi_type *owned;
    size_t owned_count, owned_capacity;
} native_ffi_signature;

typedef struct native_import_binding {
    struct qa_native_instance *instance;
    native_signature_spec spec;
    native_ffi_signature ffi;
    ffi_closure *closure;
    void *code;
} native_import_binding;

typedef struct native_entry_binding {
    native_signature_spec spec;
    native_ffi_signature ffi;
    qa_native_address address;
} native_entry_binding;

typedef struct native_allocation {
    struct native_allocation *next;
    void *bytes;
    size_t size;
    int32_t tag;
} native_allocation;

typedef struct native_slot {
    qa_native_slot_kind kind;
    qa_actor_id actor;
    qa_actor_owner owner;
    uint32_t source_slot;
} native_slot;

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
    qa_native_backend backend;
    qa_sha256_digest declaration;
    bool has_declaration;
    qa_native_lifecycle lifecycle;
    void *loader_handle;
    char *materialized_directory;
    char *materialized_path;
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
    native_slot *slots;
    uint32_t slot_capacity;
    native_runner_connection *runner;
    native_region_slot *regions;
    size_t region_count;
    qa_native_entry_observer *entry_observers;
    qa_native_write_observer *write_observers;
    uint64_t next_observer_id;
    uint32_t active_depth, callback_depth, region_depth, region_service_depth, write_depth;
    bool checkpointing, destroying, unloading, pending_shutdown, failed;
    qa_error failure;
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
    native_ffi_signature ffi;
    ffi_closure *closure;
    void *code;
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
    qa_sha256_digest digest;
    uint8_t *json;
    size_t json_size;
    size_t primary_offset, primary_size;
    qa_json_document *document;
    qa_json_id primary;
    native_region_slot *regions;
    size_t region_count;
};

extern _Thread_local qa_native_instance *native_active_instance;

/* Set only at a validated source-call or encoded runner handoff boundary. */
static inline void native_call_started(qa_native_instance *instance) {
    if (instance && instance->pending_shutdown) {
        instance->pending_shutdown = false;
        instance->lifecycle = QA_NATIVE_SHUT_DOWN;
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
bool native_profile_prepare_remote(qa_native_instance *instance, qa_error *error);
void native_profile_unbind(qa_native_instance *instance);
bool native_profile_refresh_entities(qa_native_instance *instance, qa_error *error);
const native_entry_binding *native_entry(const qa_native_instance *instance, const char *name);

bool native_ffi_prepare(native_ffi_signature *out, const qa_native_signature *signature,
                        qa_error *error);
void native_ffi_destroy(native_ffi_signature *signature);
bool native_ffi_call(qa_native_instance *instance, qa_native_address address,
                     const qa_native_signature *signature, native_ffi_signature *prepared,
                     const qa_native_value *arguments, size_t argument_count,
                     qa_native_value *result, qa_error *error);
bool native_import_bind(native_import_binding *binding, qa_native_instance *instance,
                        const native_signature_spec *spec, qa_error *error);
void native_import_unbind(native_import_binding *binding);
void native_import_dispatch(ffi_cif *cif, void *result, void **arguments, void *context);
void native_observer_dispatch(ffi_cif *cif, void *result, void **arguments, void *context);

bool native_direct_open(qa_native_instance *instance, qa_error *error);
void native_direct_close(qa_native_instance *instance);
bool native_direct_export(const qa_native_instance *instance, const char *name,
                          qa_native_address *out, qa_error *error);
bool native_direct_read(qa_native_address address, void *out, size_t bytes, qa_error *error);
bool native_direct_write(qa_native_address address, const void *bytes, size_t size,
                         qa_error *error);

bool native_runner_open(qa_native_instance *instance, const qa_native_runner_config *config,
                        qa_error *error);
bool native_runner_close(qa_native_instance *instance, qa_error *error);
bool native_runner_call(qa_native_instance *instance, const char *entry,
                        const qa_native_value *arguments, size_t count, qa_native_value *result,
                        qa_error *error);
bool native_runner_invoke(qa_native_instance *instance, qa_native_address address,
                          const qa_native_signature *signature, const qa_native_value *arguments,
                          size_t count, qa_native_value *result, qa_error *error);
bool native_runner_export(qa_native_instance *instance, const char *name, qa_native_address *out,
                          qa_error *error);
bool native_runner_entry_address(qa_native_instance *, const char *, qa_native_address *, qa_error *);
bool native_runner_read(qa_native_instance *instance, qa_native_address source, void *out,
                        size_t bytes, qa_error *error);
bool native_runner_write(qa_native_instance *instance, qa_native_address destination,
                         qa_bytes bytes, qa_error *error);
bool native_runner_allocate(qa_native_instance *instance, size_t bytes, int32_t tag,
                            qa_native_address *out, qa_error *error);
bool native_runner_allocation_query(qa_native_instance *, qa_native_address,
                                    qa_native_allocation_info *, qa_error *);
bool native_runner_free(qa_native_instance *instance, qa_native_address address, qa_error *error);
bool native_runner_free_tag(qa_native_instance *instance, int32_t tag, qa_error *error);
bool native_runner_entity_get(qa_native_instance *instance, qa_native_entity_table *out,
                              qa_error *error);
bool native_runner_entity_set(qa_native_instance *instance, qa_native_entity_table table,
                              qa_error *error);
bool native_runner_checkpoint_capture(qa_native_instance *instance,
                                      qa_native_checkpoint_request request,
                                      qa_native_checkpoint *out, qa_error *error);
bool native_runner_checkpoint_restore(qa_native_instance *instance,
                                      const qa_native_checkpoint *checkpoint,
                                      qa_native_restore_part part, qa_error *error);
bool native_runner_region_event(qa_native_instance *instance, const qa_native_region_event *event,
                                qa_native_region_decision *decision, qa_error *error);
bool native_runner_observer_entry_add(qa_native_entry_observer *binding, qa_error *error);
bool native_runner_observer_entry_remove(qa_native_entry_observer *binding, qa_error *error);
bool native_runner_observer_original(qa_native_entry_observer *binding,
                                     const qa_native_value *arguments, size_t count,
                                     qa_native_value *result, qa_error *error);
bool native_runner_observer_control(qa_native_instance *instance, native_hook_control control,
                                    qa_error *error);
void native_observers_destroy(qa_native_instance *instance);

bool native_entity_table_store(qa_native_instance *instance, qa_native_entity_table table,
                               qa_error *error);

bool native_regions_copy(qa_native_instance *instance, const qa_native_declaration *declaration,
                         qa_error *error);
void native_regions_destroy(qa_native_instance *instance);
bool native_regions_descriptor(const qa_native_instance *instance, qa_buffer *out, qa_error *error);
bool native_instance_setup_identity(qa_native_instance *instance, const qa_native_options *options,
                                    qa_error *error);

bool native_call_binding(qa_native_instance *instance, const native_entry_binding *binding,
                         const qa_native_value *arguments, size_t count, qa_native_value *result,
                         qa_error *error);
bool native_dispatch_import(qa_native_instance *instance, const native_signature_spec *spec,
                            const qa_native_value *arguments, size_t count,
                            qa_native_value *result);
void native_latch_error(qa_native_instance *instance, const qa_error *error);
/* Returns for ordinary direct owners; an actual runner child never resumes a
 * rejected original source callback. */
void native_runner_child_failure(qa_native_instance *instance, const qa_error *error);
bool native_dispatch_formatted(qa_native_instance *instance, uint32_t slot, const char *name,
                               const qa_native_value *prefix, size_t prefix_count,
                               const char *format, va_list values, qa_error *error);
intptr_t native_q3_syscall(int32_t service, ...);
void *native_variadic_import(qa_native_profile profile, uint32_t slot);

bool native_temp_directory(char **path, qa_error *error);
bool native_temp_file(const char *directory, const char *name, qa_bytes bytes, char **path,
                      qa_error *error);
bool native_read_file(const char *path, qa_buffer *out, qa_error *error);
bool native_write_file(const char *path, qa_bytes bytes, qa_error *error);
void native_remove_tree(const char *path);

#endif
