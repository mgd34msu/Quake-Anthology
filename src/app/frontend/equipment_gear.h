#ifndef QA_FRONTEND_EQUIPMENT_GEAR_H
#define QA_FRONTEND_EQUIPMENT_GEAR_H

#include "visual_access.h"
#include "../application/equipment_gear_presentation.h"
#include "qa/application_equipment_content.h"
#include "qa/application_equipment.h"
#include "../../presentation/q3_native/selected_authored_media.h"

typedef struct frontend_equipment_gear_presenter frontend_equipment_gear_presenter;
typedef struct frontend_equipment_gear_owner_view {
    qa_application_equipment_content source;
    const application_q3_grapple_definition *definition;
    qa_q3_product product;
    frontend_visual_owner_view content;
    qa_q3_presentation_assets *assets;
    q3n_selected_authored_media *media;
} frontend_equipment_gear_owner_view;

/* The private gear namespace owns a separate registry. Each full actor owns
 * its authentic selected weapon continuation across view and held draws. */
bool frontend_equipment_gear_prepare(qa_frontend *,
    const application_equipment_gear_presentation *, bool view,
    void *context, bool (*current)(void *), frontend_equipment_gear_presenter **, qa_error *);
bool frontend_equipment_gear_retain(frontend_equipment_gear_presenter *, qa_error *);
void frontend_equipment_gear_release(frontend_equipment_gear_presenter *);
bool frontend_equipment_gear_idle(const qa_frontend *);
bool frontend_equipment_gear_retire(qa_frontend *, qa_error *);
void frontend_equipment_gear_destroy(qa_frontend *);
size_t frontend_equipment_gear_count(const qa_frontend *);
bool frontend_equipment_gear_at(const qa_frontend *, size_t,
    frontend_equipment_gear_owner_view *, qa_error *);
bool frontend_equipment_gear_content_visit(const qa_frontend *,
    const qa_application_content_visitor *, qa_error *);

#endif
