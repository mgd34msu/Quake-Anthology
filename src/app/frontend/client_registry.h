#ifndef QA_FRONTEND_CLIENT_REGISTRY_H
#define QA_FRONTEND_CLIENT_REGISTRY_H
#include "internal.h"
#include "qa/persistence_content.h"

typedef struct frontend_client_registry_context {
    void *context;
    bool (*retain)(void *,qa_error *);
    bool (*release)(void *,qa_error *);
    bool callbacks_external;
} frontend_client_registry_context;
typedef struct frontend_client_registry_view {
    const frontend_client_registry *owner;
    const qa_launch_instance *source;
    qa_cvars *cvars;
    uint32_t launch_seat;
    size_t references;
    uint64_t view_identity;
    bool source_live;
} frontend_client_registry_view;

/* Takes one retained exact view reference only on success. Source metadata
 * and the view identity distinguish CGAME/UI views of the same source seat.
 * An external callback owner retires its callbacks after its own Shutdown. */
bool frontend_client_registry_create(qa_frontend *,const qa_launch_instance *,uint32_t launch_seat,
    qa_cvars **owned,const frontend_client_registry_context *,frontend_client_registry **,qa_error *);
bool frontend_client_registry_retain(frontend_client_registry *,frontend_client_registry **,qa_error *);
/* Each borrower holds an exact view reference. Failure preserves the slot. */
bool frontend_client_registry_release(frontend_client_registry **,qa_error *);
/* Constructor retirement detaches its owned callbacks and releases the
 * Source context before dropping its reference. Borrowed views may remain. */
bool frontend_client_registry_retire(frontend_client_registry **,qa_error *);
/* Manager retirement preflights all its exact views before retiring any. */
bool frontend_client_registry_release_ready(const frontend_client_registry *,qa_error *);
bool frontend_client_registry_acquire_view(qa_frontend *,const qa_launch_instance *,uint32_t launch_seat,
    const qa_cvars *,frontend_client_registry **,qa_error *);
qa_cvars *frontend_client_registry_cvars(const frontend_client_registry *);
const frontend_client_registry *frontend_client_registry_lookup(const qa_frontend *,const qa_cvars *);
bool frontend_client_registry_source(const frontend_client_registry *,const qa_launch_instance **,uint32_t *launch_seat);
bool frontend_client_registry_matches(const frontend_client_registry *,const qa_launch_instance *,uint32_t launch_seat);
size_t frontend_client_registry_count(const qa_frontend *);
bool frontend_client_registry_read(const qa_frontend *,size_t,frontend_client_registry_view *,qa_error *);
/* All actual manager/client references must have retired before the parent
 * can release its stable callback context. This never drops caller leases. */
bool frontend_client_registries_retired(const qa_frontend *,qa_error *);
bool frontend_client_registries_rebind_ready(const qa_frontend *,const qa_frontend *,qa_error *);
void frontend_client_registries_rebind(qa_frontend *,qa_frontend *);
bool frontend_client_registries_visit(const qa_frontend *,const qa_application_content_visitor *,qa_error *);
/* The prefix records actual typed Source views. The application saves the
 * canonical scalar state once; constructors recreate view declarations. */
bool frontend_client_registries_checkpoint(const qa_frontend *,const qa_application_content_graph *,qa_buffer *,qa_error *);
bool frontend_client_registries_prepare_restore(qa_frontend *,qa_application_content_graph *,qa_bytes,qa_error *);
bool frontend_client_registries_finish_restore(qa_frontend *,qa_error *);
bool frontend_client_registries_discard_restore(qa_frontend *,qa_error *);
#endif
