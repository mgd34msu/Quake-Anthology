#ifndef QA_APPLICATION_NATIVE_Q2_PUBLICATION_H
#define QA_APPLICATION_NATIVE_Q2_PUBLICATION_H

#include "network_unified.h"

struct application_native_q2;
typedef struct application_native_q2_publication application_native_q2_publication;
typedef struct application_native_q2_publication_restore application_native_q2_publication_restore;
typedef enum application_native_q2_hud_mode {
    APPLICATION_NATIVE_Q2_HUD_NONE,
    APPLICATION_NATIVE_Q2_HUD_OVERLAY,
    APPLICATION_NATIVE_Q2_HUD_REPLACE
} application_native_q2_hud_mode;
typedef struct application_native_q2_publication_view {
    const application_native_q2_publication *registration;
    const qa_launch_instance *descriptor;
    const qa_catalog_mod *metadata;
    const qa_unified_document *identity;
    qa_actor_owner owner, source_owner;
    uint64_t activation_generation, generation;
    application_native_q2_hud_mode hud;
    bool camera;
} application_native_q2_publication_view;

/* Factory owns this child of an actually admitted native GAME. No selected
 * component declaration means no registration, including stock native GAME. */
bool application_native_q2_publication_create(struct application_native_q2 *,
    application_native_q2_publication **, qa_error *);
bool application_native_q2_publication_activate(application_native_q2_publication *, qa_error *);
bool application_native_q2_publication_retire(application_native_q2_publication *, qa_error *);
void application_native_q2_publication_destroy(application_native_q2_publication **);
bool application_native_q2_publication_read(qa_application *, const application_unified_source *,
    application_native_q2_publication_view *, bool *found, qa_error *);
bool application_native_q2_publication_current(qa_application *, const application_unified_source *,
    const application_native_q2_publication_view *);
/* Pure imported/returned registration read for the actual checkpoint Source
 * lane. It never invokes a HUD observer or native module callback. */
bool application_native_q2_publication_checkpoint_read(qa_application *, const application_unified_source *,
    application_native_q2_publication_view *, bool *found, qa_error *);
bool application_native_q2_publication_capture(struct application_native_q2 *, qa_buffer *, qa_error *);
bool application_native_q2_publication_restore_prepare(struct application_native_q2 *, qa_bytes, bool map_ready,
    application_native_q2_publication_restore **, qa_error *);
void application_native_q2_publication_restore_commit(application_native_q2_publication_restore *);
void application_native_q2_publication_restore_abort(application_native_q2_publication_restore *);

#endif
