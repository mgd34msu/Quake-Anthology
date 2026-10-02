#ifndef QA_FRONTEND_REMOTE_UNIFIED_COMPONENTS_SAVE_H
#define QA_FRONTEND_REMOTE_UNIFIED_COMPONENTS_SAVE_H
#include "remote_unified_components.h"
#include "qa/persistence_content.h"

typedef struct frontend_unified_components_refs {
    qa_application_content_graph *content;
    void *context;
    bool (*scene_current)(void *,const void *actual_frontend_owner,uint64_t frontend_identity,qa_error *);
} frontend_unified_components_refs;
bool frontend_unified_components_checkpoint(frontend_unified_components *,
    const frontend_unified_components_refs *,qa_buffer *,qa_error *);
/* The recipe, private replica identity ledger and content holders precede this
 * prefix. Private renderer dictionaries must import before finish. */
bool frontend_unified_components_restore_prepare(qa_frontend *,frontend_remote_unified *,frontend_unified_media *,
    const frontend_unified_components_refs *,qa_bytes,frontend_unified_components **,qa_error *);
bool frontend_unified_components_restore_finish(frontend_unified_components *,const frontend_unified_components_refs *,qa_error *);
bool frontend_unified_components_prepared(frontend_unified_components *,frontend_unified_component_frame **,
    const qa_unified_document **,qa_error *);
#endif
