#ifndef QA_FRONTEND_CLIENT_REGISTRY_H
#define QA_FRONTEND_CLIENT_REGISTRY_H
#include "internal.h"
#include "qa/persistence_content.h"

typedef struct frontend_client_registry_context {
    void *context;
    bool (*retain)(void *,qa_error *);
    bool (*release)(void *,qa_error *);
} frontend_client_registry_context;
typedef struct frontend_client_registry_view {
    const frontend_client_registry *owner;
    const qa_launch_instance *source;
    qa_cvars *cvars;
    uint32_t launch_seat;
    size_t references;
} frontend_client_registry_view;

/* Takes one actual prepared heap only on success. The descriptor identifies
 * its source constructor; installed CGAME/UI aliases retain this same owner.
 * The context lease keeps the heap's genuine callback user alive. */
bool frontend_client_registry_create(qa_frontend *,const qa_launch_instance *,uint32_t launch_seat,
    qa_cvars **owned,const frontend_client_registry_context *,frontend_client_registry **,qa_error *);
bool frontend_client_registry_retain(frontend_client_registry *,frontend_client_registry **,qa_error *);
/* Failure leaves the caller's reference reachable. The final release retires
 * the real registry before releasing its callback context and metadata. */
bool frontend_client_registry_release(frontend_client_registry **,qa_error *);
/* Manager retirement preflights all its physical rows before releasing any.
 * A client lease still present rejects retirement without dropping a ref. */
bool frontend_client_registry_release_ready(const frontend_client_registry *,qa_error *);
bool frontend_client_registry_acquire(qa_frontend *,const qa_launch_instance *,uint32_t launch_seat,
    frontend_client_registry **,qa_error *);
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
/* This physical prefix is the sole QACV payload for shared client heaps.
 * Decode stages owned bytes before source constructors. The first genuine
 * constructor qualifies its retained source and imports those bytes once;
 * subsequent native/original aliases retain the same actual heap. */
bool frontend_client_registries_checkpoint(const qa_frontend *,const qa_application_content_graph *,qa_buffer *,qa_error *);
bool frontend_client_registries_prepare_restore(qa_frontend *,qa_application_content_graph *,qa_bytes,qa_error *);
bool frontend_client_registries_finish_restore(qa_frontend *,qa_error *);
bool frontend_client_registries_discard_restore(qa_frontend *,qa_error *);
#endif
