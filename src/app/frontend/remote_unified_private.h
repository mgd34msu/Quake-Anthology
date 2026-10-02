#ifndef QA_FRONTEND_REMOTE_UNIFIED_PRIVATE_H
#define QA_FRONTEND_REMOTE_UNIFIED_PRIVATE_H
#include "internal.h"
#include "remote_unified.h"
#include "qa/strings.h"

typedef struct frontend_unified_identity {
    struct frontend_unified_identity *next;
    qa_saved_actor_id wire;
    qa_actor_id actual;
} frontend_unified_identity;
typedef struct frontend_unified_metadata {
    qa_actor_id actor;
    qa_actor_owner owner, old_owner;
    qa_actor_definition definition, old_definition;
} frontend_unified_metadata;
struct frontend_remote_unified {
    frontend_remote_unified *next;
    qa_frontend *frontend;
    frontend_remote_unified_options options;
    qa_unified_session *session;
    qa_executable_recipe *recipe, *preparing_recipe, *retiring_recipe;
    qa_unified_document *offer, *frame, *prepared_frame, *prediction;
    qa_actor_registry *actors;
    qa_strings *strings;
    frontend_unified_metadata *metadata;
    size_t metadata_count;
    frontend_unified_identity *identities;
    qa_saved_actor_id wire_player;
    qa_saved_actor_id wire_client;
    qa_actor_id player;
    uint32_t epoch, source_entity;
    uint64_t frame_number;
    bool bound, preparing, prepared, admitted, retired, busy, consumers_live, transport_restarted;
};
bool frontend_unified_fail(qa_error *, qa_status, const char *);
bool frontend_unified_clone(const qa_unified_document *, qa_unified_document **, qa_error *);
#endif
