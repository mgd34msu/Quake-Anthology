#ifndef QA_FRONTEND_EQUIPMENT_SOURCE_H
#define QA_FRONTEND_EQUIPMENT_SOURCE_H

#include "equipment_media.h"
#include "qa/application_q3_equipment_source.h"
#include "qa/application_q3_client.h"

typedef struct frontend_equipment_source frontend_equipment_source;
typedef struct frontend_equipment_source_options {
    qa_frontend *frontend;
    qa_actor_owner receiver;
    uint32_t seat;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    void *lease;
    /* Borrow increments the actual linked CGAME lease operation counters.
     * Current is a pure full-context/lifetime qualification; release closes
     * the successful borrow after all held scopes and view submissions unwind. */
    bool (*borrow)(void *, qa_application_q3_client_context *, qa_error *);
    bool (*current)(void *, const qa_application_q3_client_context *);
    void (*release)(void *);
} frontend_equipment_source_options;

bool frontend_equipment_source_create(const frontend_equipment_source_options *,
    frontend_equipment_source **, qa_error *);
void frontend_equipment_source_services(frontend_equipment_source *,
    qa_application_q3_equipment_services *);
bool frontend_equipment_source_idle(const frontend_equipment_source *);
bool frontend_equipment_source_destroy(frontend_equipment_source *, qa_error *);
/* Called by the real source renderer owner at its current view boundaries. */
bool frontend_equipment_source_prepare_view(frontend_equipment_source *,
    const qa_q3_refdef *, qa_q3_scene_options *, qa_error *);
bool frontend_equipment_source_submit(frontend_equipment_source *,
    const qa_q3_scene_options *, qa_scene_frame *, qa_error *);

#endif
