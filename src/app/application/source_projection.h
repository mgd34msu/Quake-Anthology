#ifndef QA_APPLICATION_SOURCE_PROJECTION_H
#define QA_APPLICATION_SOURCE_PROJECTION_H

#include "qa/q3_abi.h"
#include "qa/q3_host.h"

typedef struct application_source_projection {
    qa_q3_host *host;
    qa_qvm_abi abi;
    uint32_t entity_stride, inuse, health, takedamage, allocate, free;
    bool can_take_damage, has_allocate, has_free;
    void *context;
    bool (*span)(void *, uint64_t address, size_t, qa_bytes *, qa_error *);
    bool (*write)(void *, uint64_t address, qa_bytes, bool private_fields, qa_error *);
    bool (*invoke)(void *, const qa_qvm_call *, uint32_t entry,
        const qa_native_value *, size_t, qa_native_value *, qa_error *);
} application_source_projection;

bool application_source_projection_pointer(const application_source_projection *,
    uint32_t slot, uint64_t *, qa_error *);
bool application_source_projection_slot(const application_source_projection *,
    uint64_t address, uint32_t *, qa_error *);
bool application_source_projection_borrow(const application_source_projection *,
    const qa_qvm_call *, qa_actor_id, uint32_t *, bool *created, qa_error *);
bool application_source_projection_publish(const application_source_projection *,
    uint32_t slot, const qa_body_state *, const int32_t *health, uint64_t time_ns, qa_error *);
bool application_source_projection_keep(const application_source_projection *,
    uint32_t slot, qa_error *);
bool application_source_projection_release(const application_source_projection *,
    const qa_qvm_call *, uint32_t slot, qa_error *);
bool q3g_project_actor(void *, const qa_qvm_call *, qa_actor_id, uint32_t *, qa_error *);

#endif
