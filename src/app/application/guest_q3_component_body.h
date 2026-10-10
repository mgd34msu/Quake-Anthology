#ifndef QA_APPLICATION_GUEST_Q3_COMPONENT_BODY_H
#define QA_APPLICATION_GUEST_Q3_COMPONENT_BODY_H

#include "guest_q3_body_profile.h"
#include "qa/application_q3_component_body.h"
#include "qa/qvm_save.h"
#include "qa/network_unified_frame_pool.h"

typedef struct application_q3_component_body_source {
    void *context;
    /* The component scene owner supplies its reached source-slot table, with
     * the real ownership bit used by original snapshot admission. */
    bool (*actor)(void *, uint32_t, qa_actor_id *, bool *owned, bool *found, qa_error *);
    /* Live and current are pure observations of that retained scene owner. */
    bool (*live)(void *, qa_actor_id);
    bool (*current)(void *, uint64_t sequence, int32_t time_ms);
} application_q3_component_body_source;
typedef struct application_q3_component_body_options {
    qa_qvm *vm;
    const qa_qvm_image *image;
    /* Exactly one original qvm-scene player/mesh declaration. Its field
     * offset comes from that actual component centity layout. */
    const application_q3_body_profile *profile;
    qa_actor_owner owner;
    qa_q3_presentation_assets *assets;
    qa_unified_frame_pool *storage;
    application_q3_component_body_source source;
} application_q3_component_body_options;

bool application_q3_component_body_create(const application_q3_component_body_options *,
    application_q3_component_body **, qa_error *);
bool application_q3_component_body_destroy(application_q3_component_body *, qa_error *);
bool application_q3_component_body_idle(const application_q3_component_body *);
/* Begin before the actual component advance, after reached context has been
 * written. End after its original calls return, including a failed call. */
bool application_q3_component_body_begin(application_q3_component_body *,
    uint64_t sequence, int32_t time_ms, qa_error *);
bool application_q3_component_body_end(application_q3_component_body *, bool success, qa_error *);
bool application_q3_component_body_source_entity(void *, const qa_qvm_call *, int32_t,
    const qa_q3_ref_entity *, bool *suppress, qa_error *);
bool application_q3_component_body_descriptors(const application_q3_component_body *,
    qa_qvm_saved_function out[2], qa_error *);
void application_q3_component_body_adopt(application_q3_component_body *, const qa_qvm_binding[2]);

/* The enclosing component owns the declaration resource/receipt and merges
 * these two identities with its complete executor inventory before RAM
 * import. Captured frame output is rebuilt by the next genuine advance,
 * matching the original component host checkpoint. */
bool application_q3_component_body_checkpoint(const application_q3_component_body *,
    qa_buffer *, qa_error *);
bool application_q3_component_body_saved_read(const application_q3_body_profile *,
    qa_bytes, qa_qvm_binding[2], qa_error *);

#endif
