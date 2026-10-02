#ifndef QA_NATIVE_GUEST_HOST_MEMORY_H
#define QA_NATIVE_GUEST_HOST_MEMORY_H

#include "qa/native_guest.h"

typedef struct guest_host_memory guest_host_memory;
typedef struct guest_host_backing_view {
    uint64_t id;
    qa_bytes bytes;
    bool file;
    qa_native_guest_file source;
} guest_host_backing_view;

/* Controller backing addresses are ordinary host mappings. Fixed guest
 * addresses are installed only in the separately owned child. */
bool guest_host_memory_create(guest_host_memory **, qa_error *);
bool guest_host_memory_backing(guest_host_memory *, const guest_host_backing_view *,
    qa_error *);
bool guest_host_memory_remove_backing(guest_host_memory *, uint64_t, qa_error *);
bool guest_host_memory_backing_at(const guest_host_memory *, size_t,
    guest_host_backing_view *, int *, qa_error *);
size_t guest_host_memory_backing_count(const guest_host_memory *);
bool guest_host_memory_map(guest_host_memory *, const qa_native_guest_mapping *, qa_error *);
bool guest_host_memory_change(guest_host_memory *, const qa_native_guest_mapping *,
    uint32_t, bool, qa_error *);
size_t guest_host_memory_mapping_count(const guest_host_memory *);
bool guest_host_memory_mapping_at(const guest_host_memory *, size_t,
    qa_native_guest_mapping *, qa_error *);
bool guest_host_memory_read(const guest_host_memory *, uint64_t, void *, size_t, qa_error *);
bool guest_host_memory_write(guest_host_memory *, uint64_t, qa_bytes, qa_error *);
bool guest_host_memory_check(const guest_host_memory *, uint64_t, size_t, uint32_t, qa_error *);
bool guest_host_memory_fault(const guest_host_memory *, uint64_t, uint32_t,
    qa_native_guest_fault *, qa_error *);
bool guest_host_memory_destroy(guest_host_memory **, qa_error *);

/* These operations run in the actual child. The descriptor owns the immutable
 * snapshot backing; MAP_SHARED preserves every guest alias. No parent address
 * space is overwritten, and occupied fixed addresses are rejected. */
bool guest_host_memory_child_backing(int, const guest_host_backing_view *, qa_error *);
bool guest_host_memory_child_map(int, const qa_native_guest_mapping *, qa_error *);
bool guest_host_memory_child_change(const qa_native_guest_mapping *, uint32_t,
    bool, qa_error *);

#endif
