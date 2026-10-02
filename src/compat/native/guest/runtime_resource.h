#ifndef QA_NATIVE_GUEST_RUNTIME_RESOURCE_H
#define QA_NATIVE_GUEST_RUNTIME_RESOURCE_H

#include "qa/native_guest.h"

typedef struct guest_runtime_resources guest_runtime_resources;
enum { GUEST_RUNTIME_FILE_READ = 1, GUEST_RUNTIME_FILE_WRITE = 2 };
/* Zero mode is a genuine metadata-only handle, without read/write authority. */
/* Capability identity is external and durable. A resolver borrows the existing
 * capability; it must not reopen a path or replay creation/truncation. Actual
 * completed bytes are reported even when an operation subsequently fails. */
typedef struct guest_runtime_file_capability {
    uint64_t id;
    uint32_t mode;
    bool (*read)(void *, uint64_t, void *, size_t, size_t *, qa_error *);
    bool (*write)(void *, uint64_t, qa_bytes, size_t *, qa_error *);
    bool (*size)(void *, uint64_t *, qa_error *);
    bool (*truncate)(void *, uint64_t, qa_error *);
    bool (*flush)(void *, qa_error *);
    bool (*close)(void *, qa_error *);
    void *context;
} guest_runtime_file_capability;
typedef bool (*guest_runtime_file_resolve_fn)(void *, uint64_t,
    guest_runtime_file_capability *, qa_error *);
typedef struct guest_runtime_file_view {
    uint64_t id, capability, offset;
    uint32_t mode, creation;
    const char *name;
    bool closed;
} guest_runtime_file_view;

bool guest_runtime_resources_create(guest_runtime_resources **, qa_error *);
/* Adds an already opened genuine capability. No filesystem operation occurs. */
bool guest_runtime_resources_file(guest_runtime_resources *, uint64_t,
    const char *, uint32_t, const guest_runtime_file_capability *, qa_error *);
bool guest_runtime_resources_at(const guest_runtime_resources *, size_t,
    guest_runtime_file_view *, qa_error *);
bool guest_runtime_resources_find(const guest_runtime_resources *, uint64_t,
    guest_runtime_file_view *, qa_error *);
size_t guest_runtime_resources_count(const guest_runtime_resources *);
bool guest_runtime_resources_idle(const guest_runtime_resources *);
bool guest_runtime_resources_read(guest_runtime_resources *, uint64_t,
    void *, size_t, size_t *, qa_error *);
bool guest_runtime_resources_write(guest_runtime_resources *, uint64_t,
    qa_bytes, size_t *, qa_error *);
bool guest_runtime_resources_seek(guest_runtime_resources *, uint64_t, uint64_t, qa_error *);
bool guest_runtime_resources_size(guest_runtime_resources *, uint64_t, uint64_t *, qa_error *);
bool guest_runtime_resources_truncate(guest_runtime_resources *, uint64_t, uint64_t, qa_error *);
bool guest_runtime_resources_flush(guest_runtime_resources *, uint64_t, qa_error *);
bool guest_runtime_resources_close(guest_runtime_resources *, uint64_t, qa_error *);
/* Forget only a genuinely completed close receipt. Remaining rows keep their
 * physical order; no external close callback or forced release occurs. */
bool guest_runtime_resources_remove_closed(guest_runtime_resources *, uint64_t, qa_error *);
/* Closes each still-owned live capability once, in registration order. Refusal
 * retains successful close receipts and the remaining owner for a checked retry.
 * A provisional rebound candidate releases only its borrowed bindings. */
bool guest_runtime_resources_destroy(guest_runtime_resources **, qa_error *);
/* Releases only registry storage. Caller retains all external capabilities. */
void guest_runtime_resources_abandon(guest_runtime_resources **);
bool guest_runtime_resources_checkpoint(const guest_runtime_resources *, qa_buffer *, qa_error *);
bool guest_runtime_resources_decode(qa_bytes, guest_runtime_resources **, qa_error *);
/* Rebind saved open IDs to existing capabilities after complete decode. No open,
 * close, seek, size, read, write, flush or truncate callback is invoked. */
bool guest_runtime_resources_rebind(guest_runtime_resources *,
    guest_runtime_file_resolve_fn, void *, qa_error *);
/* Rebound capabilities are provisional: pure inspection/checkpoint is allowed,
 * external operations and close are blocked. At publication, transfer matching
 * actual close owners from previous. NULL previous requires the caller's
 * external resource graph to have transferred exclusive candidate ownership.
 * This changes registry ownership only, with no file callback or allocation. */
bool guest_runtime_resources_adopt(guest_runtime_resources *,
    guest_runtime_resources *, qa_error *);

#endif
