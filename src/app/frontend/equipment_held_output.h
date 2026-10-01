#ifndef QA_FRONTEND_EQUIPMENT_HELD_OUTPUT_H
#define QA_FRONTEND_EQUIPMENT_HELD_OUTPUT_H

#include "equipment_media.h"

typedef struct frontend_equipment_held_output frontend_equipment_held_output;

/* The enclosing real client draw lease holds the parent registry and selected
 * media owner until this output is released. Construction performs no source
 * callback, registration or model acquisition. */
bool frontend_equipment_held_output_create(qa_frontend *,
    const qa_application_equipment_view *, frontend_equipment_media *,
    qa_q3_presentation_assets *, const qa_q3_ref_entity *authored_parent,
    frontend_equipment_held_output **, qa_error *);
/* Parent tags belong to parent_assets; captured shader passes and submission
 * belong to source_assets. Both registries remain held by the entered frame. */
bool frontend_equipment_held_output_create_from(qa_frontend *,
    const qa_application_equipment_view *, frontend_equipment_media *,
    const qa_q3_presentation_assets *parent_assets, qa_q3_presentation_assets *source_assets,
    const qa_q3_ref_entity *, frontend_equipment_held_output **, qa_error *);
bool frontend_equipment_held_output_pass(frontend_equipment_held_output *,
    const qa_q3_ref_entity *, qa_error *);
size_t frontend_equipment_held_output_count(const frontend_equipment_held_output *);
bool frontend_equipment_held_output_submit(frontend_equipment_held_output *,
    qa_q3_presentation *, const qa_q3_scene_options *, uint32_t first_order,
    qa_scene_frame *, qa_error *);
void frontend_equipment_held_output_destroy(frontend_equipment_held_output *);

#endif
