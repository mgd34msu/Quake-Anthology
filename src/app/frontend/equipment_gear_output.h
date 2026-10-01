#ifndef QA_FRONTEND_EQUIPMENT_GEAR_OUTPUT_H
#define QA_FRONTEND_EQUIPMENT_GEAR_OUTPUT_H

#include "equipment_gear.h"

typedef struct frontend_equipment_gear_output frontend_equipment_gear_output;
bool frontend_equipment_gear_view(frontend_equipment_gear_presenter *,
    const application_equipment_gear_presentation *, const q3n_selected_weapon_view *, float field_of_view,
    void *, bool (*current)(void *), frontend_equipment_gear_output **, bool *submitted, qa_error *);
bool frontend_equipment_gear_held(frontend_equipment_gear_presenter *,
    const application_equipment_gear_presentation *, const q3n_selected_weapon_held *, bool reduced_flashes,
    void *, bool (*current)(void *), frontend_equipment_gear_output **, bool *submitted, qa_error *);
size_t frontend_equipment_gear_output_count(const frontend_equipment_gear_output *);
bool frontend_equipment_gear_output_source_style(frontend_equipment_gear_output *,
    qa_q3_presentation_assets *primary_assets, const qa_q3_ref_entity *, qa_error *);
bool frontend_equipment_gear_output_source_pass(frontend_equipment_gear_output *,
    const qa_q3_ref_entity *, qa_error *);
bool frontend_equipment_gear_output_submit(frontend_equipment_gear_output *, qa_q3_presentation *,
    qa_vec3 offset, const qa_q3_scene_options *, uint32_t first_order, qa_scene_frame *, qa_error *);
void frontend_equipment_gear_output_destroy(frontend_equipment_gear_output *);

#endif
