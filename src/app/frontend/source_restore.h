#ifndef QA_FRONTEND_SOURCE_RESTORE_H
#define QA_FRONTEND_SOURCE_RESTORE_H
#include "qa/frontend.h"
#include "qa/q3_host.h"
#include "qa/application_startup_prepare.h"
struct frontend_video_guests;

typedef struct frontend_key_profile frontend_key_profile;

typedef struct frontend_source_role_identity {
    qa_qvm_role role;
    uint64_t service_owner;
} frontend_source_role_identity;
/* Candidate teardown calls this after guest leases have retired. */
bool frontend_source_discard_unbound(qa_frontend *, qa_error *);
/* Entered hosted CLIENT retarget: drain only the exact retired old namespace
 * before its configuration owner consumes the held registry/key profile. */
bool frontend_source_retire_client_configuration(qa_frontend *, qa_application *,
    const qa_application_startup_source *, const frontend_key_profile *, qa_error *);
/* Includes admitted groups before their real provider factories bind. */
bool frontend_source_identity_used(const qa_frontend *, uint64_t);
bool frontend_source_group_role_read(const qa_frontend *, size_t group,
    size_t role, frontend_source_role_identity *);
bool frontend_source_group_role_video_read(const qa_frontend *, size_t group,
    size_t role, frontend_source_role_identity *, const struct frontend_video_guests *);
/* The world dictionary qualifies source map/resource/heaps and sole root
 * destructor authority before the nofail ownership transfer. */
bool frontend_source_world_adopt_ready(qa_frontend *,size_t,qa_scene_world *,qa_error *);
void frontend_source_world_adopt(qa_frontend *,size_t,qa_scene_world *);
#endif
