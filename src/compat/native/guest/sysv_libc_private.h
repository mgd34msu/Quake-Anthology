#ifndef QA_NATIVE_GUEST_SYSV_LIBC_PRIVATE_H
#define QA_NATIVE_GUEST_SYSV_LIBC_PRIVATE_H

#include "sysv_runtime.h"
#include "internal.h"
#include "qa/source_save.h"
#include <math.h>

enum { SYSV_MAX_ALLOCATION = 0x10000000, SYSV_MAX_STRING = 0x100000 };
typedef enum sysv_group {
    SYSV_RUNTIME, SYSV_LIBC, SYSV_STDIO, SYSV_CXX, SYSV_LOCALE, SYSV_IOSTREAM, SYSV_FORMAT, SYSV_PLATFORM
} sysv_group;
typedef struct sysv_service {
    guest_sysv_runtime *runtime;
    uint64_t id, address, a, b, c;
    uint32_t operation;
    sysv_group group;
    char *library, *name, *version, *detail;
    guest_abi_layout parameters[8], result;
    size_t parameter_count;
} sysv_service;
typedef struct sysv_heap { uint64_t address, bytes; } sysv_heap;
typedef struct sysv_file {
    uint64_t address, lock, wide, buffer, handle, capability;
    uint32_t descriptor, mode;
    bool append, closing;
} sysv_file;
typedef struct sysv_destructor { uint64_t target, argument, dso; bool called; } sysv_destructor;
typedef struct sysv_unique { char *name; uint64_t address; } sysv_unique;
typedef struct sysv_object {
    uint32_t kind;
    char *name;
    uint64_t address, bytes, values[8];
} sysv_object;
typedef struct sysv_image {
    guest_sysv_provider provider;
    guest_sysv_tls tls;
    guest_sysv_lifecycle lifecycle;
} sysv_image;
struct guest_sysv_load {
    guest_sysv_runtime *runtime;
    uint64_t provider, next_used;
    guest_sysv_tls tls;
    bool has_tls, replacing;
};
struct guest_sysv_runtime {
    qa_native_guest *guest;
    qa_native_target target;
    qa_native_guest_backend execution;
    guest_sysv_options options;
    guest_runtime_imports *imports;
    uint64_t thread_area, thread_pointer, dtv_base, dtv, errno_address;
    uint64_t argv, envp, empty_string, strtok_slot, tls_used, next_service, trap_used;
    uint64_t files[3], wide_files[3];
    uint64_t file_tables[2];
    uint32_t next_file;
    sysv_file *open_files;
    size_t open_file_count, open_file_capacity;
    uint64_t classic_locale, iostream_refcount, iostream_sync;
    sysv_service **services;
    size_t service_count, service_capacity;
    sysv_heap *heap;
    size_t heap_count, heap_capacity;
    sysv_image *images;
    size_t image_count, image_capacity;
    sysv_destructor *destructors;
    size_t destructor_count, destructor_capacity;
    sysv_unique *unique;
    size_t unique_count, unique_capacity;
    sysv_object *objects;
    size_t object_count, object_capacity;
    guest_sysv_trace *trace;
    size_t trace_count, trace_capacity;
    guest_sysv_load *loading;
    size_t budget;
    unsigned calls;
    bool clock_present;
    uint8_t stream_shapes[3];
    bool file_resources, file_opener;
    bool failed, retired, detached;
};

bool sysv_fail(qa_error *, qa_status, const char *);
bool sysv_current(guest_sysv_runtime *, qa_error *);
bool sysv_copy_text(const char *, char **, qa_error *);
bool sysv_equal_text(const char *, const char *);
bool sysv_read(guest_sysv_runtime *, uint64_t, void *, size_t, qa_error *);
bool sysv_write(guest_sysv_runtime *, uint64_t, const void *, size_t, qa_error *);
bool sysv_unsigned(guest_sysv_runtime *, uint64_t, size_t, uint64_t *, qa_error *);
bool sysv_store(guest_sysv_runtime *, uint64_t, size_t, uint64_t, qa_error *);
bool sysv_pointer(guest_sysv_runtime *, uint64_t, uint64_t *, qa_error *);
bool sysv_put_pointer(guest_sysv_runtime *, uint64_t, uint64_t, qa_error *);
bool sysv_string(guest_sysv_runtime *, uint64_t, size_t, qa_buffer *, bool *, qa_error *);
bool sysv_string_new(guest_sysv_runtime *, const char *, uint64_t *, qa_error *);
bool sysv_allocate(guest_sysv_runtime *, uint64_t, bool, uint64_t *, qa_error *);
bool sysv_free(guest_sysv_runtime *, uint64_t, qa_error *);
bool sysv_allocation_size(guest_sysv_runtime *, uint64_t, uint64_t *, qa_error *);
bool sysv_errno(guest_sysv_runtime *, int, qa_error *);
guest_abi_layout sysv_layout(const guest_sysv_runtime *, qa_native_value_type);
qa_native_value_type sysv_size_type(const guest_sysv_runtime *);
qa_native_value_type sysv_signed_type(const guest_sysv_runtime *);
uint64_t sysv_integer(const qa_native_value *);
bool sysv_service_add(guest_sysv_runtime *, sysv_group, uint32_t,
    uint64_t, uint64_t, uint64_t, const char *, const char *,
    const char *const *, size_t, const qa_native_value_type *, size_t,
    qa_native_value_type, const char *, uint64_t *, qa_error *);
bool sysv_data(guest_sysv_runtime *, const char *, const char *, const char *,
    uint64_t, size_t, qa_error *);
bool sysv_object_add(guest_sysv_runtime *, uint32_t, const char *, uint64_t,
    uint64_t, const uint64_t *, size_t, qa_error *);
sysv_object *sysv_object_find(guest_sysv_runtime *, uint32_t, const char *);
bool sysv_invoke(guest_sysv_runtime *, uint64_t, const qa_native_value_type *,
    size_t, qa_native_value_type, const qa_native_value *, qa_native_value *, qa_error *);
bool sysv_service_valid(const sysv_service *, qa_error *);

bool sysv_platform_install(guest_sysv_runtime *, qa_error *);
bool sysv_platform_call(sysv_service *, const qa_native_value *, qa_native_value *, qa_error *);
bool sysv_platform_valid(const sysv_service *, qa_error *);
bool sysv_libc_install(guest_sysv_runtime *, qa_error *);
bool sysv_libc_call(sysv_service *, const qa_native_value *, qa_native_value *, qa_error *);
bool sysv_stdio_install(guest_sysv_runtime *, qa_error *);
bool sysv_stdio_call(sysv_service *, const qa_native_value *, qa_native_value *, qa_error *);
bool sysv_stdio_flush(guest_sysv_runtime *, uint64_t, int32_t *, qa_error *);
bool sysv_stdio_get(guest_sysv_runtime *, uint64_t, bool, int32_t *, qa_error *);
bool sysv_stdio_put(guest_sysv_runtime *, uint64_t, bool, uint32_t, int32_t *, qa_error *);
bool sysv_stdio_unget(guest_sysv_runtime *, uint64_t, bool, uint32_t, int32_t *, qa_error *);
bool sysv_stdio_fields(qa_source_save_io *, guest_sysv_runtime *);
bool sysv_stdio_valid(const guest_sysv_runtime *, qa_error *);
bool sysv_stdio_lower_valid(guest_sysv_runtime *, const qa_native_guest *, qa_error *);
bool sysv_stdio_retire(guest_sysv_runtime *, qa_error *);
bool sysv_cxx_install(guest_sysv_runtime *, qa_error *);
bool sysv_cxx_call(sysv_service *, const qa_native_value *, qa_native_value *, qa_error *);
bool sysv_cxx_data_install(guest_sysv_runtime *, qa_error *);
bool sysv_cxx_data(guest_sysv_runtime *, const char *, const char *, size_t, uint64_t *, qa_error *);
bool sysv_cxx_text(guest_sysv_runtime *, const char *, uint64_t *, qa_error *);
bool sysv_cxx_unsupported(guest_sysv_runtime *, const char *, uint64_t *, qa_error *);
bool sysv_name(const char *, const char *, const char *, char **, qa_error *);
bool sysv_locale_install(guest_sysv_runtime *, qa_error *);
bool sysv_locale_call(sysv_service *, const qa_native_value *, qa_native_value *, qa_error *);
bool sysv_iostream_install(guest_sysv_runtime *, qa_error *);
bool sysv_iostream_call(sysv_service *, const qa_native_value *, qa_native_value *, qa_error *);
bool sysv_cxx_type(guest_sysv_runtime *, const char *, const uint64_t *,
    const int64_t *, const uint32_t *, size_t, uint64_t *, qa_error *);
bool sysv_cxx_vtable(guest_sysv_runtime *, const char *, uint64_t,
    const uint64_t *, size_t, uint64_t *, qa_error *);
bool sysv_format_install(guest_sysv_runtime *, qa_error *);
bool sysv_format_call(sysv_service *, const qa_native_value *, qa_native_value *, qa_error *);

#endif
