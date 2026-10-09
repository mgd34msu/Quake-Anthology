#ifndef QA_FRONTEND_REMOTE_UNIFIED_PRIVATE_H
#define QA_FRONTEND_REMOTE_UNIFIED_PRIVATE_H
#include "internal.h"
#include "remote_unified.h"
#include "qa/strings.h"
#include "q1_sky.h"

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
typedef struct frontend_unified_metadata_cut {
    struct frontend_unified_metadata_cut *next;
    qa_unified_document *document;
} frontend_unified_metadata_cut;
struct frontend_remote_unified {
    frontend_remote_unified *next;
    qa_frontend *frontend;
    frontend_remote_unified_options options;
    frontend_q1_sky_controls sky_controls;
    qa_unified_session *session;
    qa_executable_recipe *recipe, *preparing_recipe, *retiring_recipe;
    qa_unified_document *offer, *frame, *prepared_frame;
    qa_unified_document *source_metadata;
    qa_unified_document *prepared_source_metadata;
    frontend_unified_metadata_cut *metadata_head, *metadata_tail;
    size_t pending_metadata_bytes;
    qa_actor_registry *actors;
    qa_strings *strings;
    frontend_unified_metadata *metadata;
    qa_unified_frame_lease *metadata_lease;
    size_t metadata_count;
    frontend_unified_identity *identities;
    qa_actor_id wire_player;
    qa_saved_actor_id wire_client;
    qa_actor_id player;
    uint32_t epoch, source_entity;
    uint64_t frame_number;
    bool frame_obsolete;
    bool bound, preparing, prepared, admitted, retired, busy, consumers_live, transport_restarted;
    bool restore_pending;
    bool retirement_pending;
};
bool frontend_unified_fail(qa_error *, qa_status, const char *);
bool frontend_unified_document_equal(const qa_unified_document *, const qa_unified_document *);
bool frontend_unified_document_restore_bind(qa_unified_document **, const qa_unified_document *, bool, qa_error *);
#endif
