#ifndef QA_NATIVE_GUEST_INTERNAL_H
#define QA_NATIVE_GUEST_INTERNAL_H

#include "qa/native_guest.h"
#include "qa/binary.h"
#include "host_child.h"
#include <unicorn/unicorn.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct guest_backing {
    uint64_t id;
    uint8_t *data;
    size_t bytes, references;
    bool file;
    qa_native_guest_file source;
    bool child_owned;
} guest_backing;
typedef struct guest_allocation {
    uint64_t address, mapping;
    size_t bytes;
    int32_t tag;
} guest_allocation;
typedef struct guest_run {
    struct guest_run *parent;
    size_t remaining;
    uint64_t bypass;
    uint64_t instruction;
    qa_native_guest_commit *writes;
    size_t count, capacity;
    size_t prepared;
    uint64_t prepared_address;
    int prepared_size;
    bool recording_failed;
    qa_error failure;
} guest_run;
struct qa_native_guest {
    qa_native_guest_options options;
    uc_engine *cpu;
    guest_host_child *child;
    guest_profile_guard_receipt profile;
    guest_profile_cpu_domain source_domain;
    uc_hook store_hook, fault_hook;
    guest_backing *backings;
    size_t backing_count, backing_capacity, backing_bytes;
    qa_native_guest_mapping *mappings;
    size_t mapping_count, mapping_capacity;
    guest_allocation *allocations;
    size_t allocation_count, allocation_capacity;
    qa_native_guest_callback *callbacks;
    size_t callback_count, callback_capacity;
    uint64_t next_mapping, next_backing, allocation_cursor;
    guest_run *run;
    qa_native_guest_commit_fn observe;
    void *observe_context;
    qa_native_guest_instruction_fn instruction_observer;
    void *instruction_context;
    void (*dispatch_started)(void *);
    void *dispatch_context;
    unsigned callback_depth, publication_depth;
    bool stepping, failed, restoring, faulting;
    bool has_memory_fault;
    qa_native_guest_fault memory_fault;
};

bool guest_fail(qa_error *, qa_status, uint64_t, const char *);
bool guest_uc(qa_native_guest *, uc_err, qa_error *);
bool guest_grow(void **, size_t *, size_t, size_t, qa_error *);
bool guest_ready(const qa_native_guest *, qa_error *);
bool guest_mutable(const qa_native_guest *, qa_error *);
guest_backing *guest_backing_at(const qa_native_guest *, uint64_t);
qa_native_guest_mapping *guest_mapping(const qa_native_guest *, uint64_t);
bool guest_range(const qa_native_guest *, uint64_t, size_t, uint32_t, qa_error *);
bool guest_install_mapping(qa_native_guest *, const qa_native_guest_mapping *, qa_error *);
bool guest_backend_map(qa_native_guest *, const qa_native_guest_mapping *, qa_error *);
bool guest_backend_change(qa_native_guest *, const qa_native_guest_mapping *, uint32_t, bool, qa_error *);
bool guest_file_access(const qa_native_guest *, const qa_native_guest_mapping *,
    uint64_t, size_t, qa_error *);
bool guest_publish(qa_native_guest *, const qa_native_guest_commit *, qa_error *);
bool guest_create(const qa_native_guest_options *, bool, qa_native_guest **, qa_error *);
bool guest_cpu_open(qa_native_guest *, bool, qa_error *);
bool guest_cpu_transfer(qa_native_guest *, qa_native_guest_cpu *, bool, qa_error *);
bool guest_allocation_storage(const qa_native_guest *, const guest_allocation *,
    uint64_t *, size_t *, qa_error *);
bool guest_allocation_transfer(qa_native_guest *, uint64_t, guest_allocation *,
    uint64_t *, qa_error *);
bool guest_native_result(qa_native_guest *, bool, qa_error *);
void guest_dispatch_started(qa_native_guest *);
bool guest_native_map(qa_native_guest *, const qa_native_guest_mapping *, qa_error *);
bool guest_backing_retire(qa_native_guest *, guest_backing *, qa_error *);
bool guest_native_transfer(qa_native_guest *, qa_native_guest_cpu *, bool, qa_error *);
bool guest_native_run_program(qa_native_guest *, uint64_t, uint64_t,
    qa_native_guest_syscall_fn, void *, bool *, qa_error *);

#endif
