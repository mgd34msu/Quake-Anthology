#ifndef QA_FRONTEND_SOURCE_RESTORE_H
#define QA_FRONTEND_SOURCE_RESTORE_H
#include "qa/frontend.h"
#include "qa/q3_host.h"

typedef struct frontend_source_role_identity {
    qa_qvm_role role;
    uint64_t service_owner;
} frontend_source_role_identity;
typedef struct frontend_source_group_plan {
    qa_actor_owner owner;
    uint32_t seat;
    uint64_t identity;
    const frontend_source_role_identity *roles;
    size_t role_count;
} frontend_source_group_plan;
/* Plans follow the saved physical group order. Actual provider factories bind
 * their admitted service owners and mount views to these stable heap groups.
 * No content loading or source call runs during admission. */
bool frontend_source_prepare_groups(qa_frontend *, uint64_t next_source_id,
    const frontend_source_group_plan *, size_t, qa_error *);
bool frontend_source_complete_groups(const qa_frontend *, qa_error *);
/* After qualification and private guest import, removes constructor policy
 * only. Installed groups, leases and their service heaps retain their address. */
void frontend_source_finish_groups(qa_frontend *);
/* Candidate teardown calls this after guest leases have retired. */
bool frontend_source_discard_unbound(qa_frontend *, qa_error *);
bool frontend_source_group_role_read(const qa_frontend *, size_t group,
    size_t role, frontend_source_role_identity *);
#endif
