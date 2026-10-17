#ifndef QA_FRONTEND_EQUIPMENT_GEAR_OUTPUT_H
#define QA_FRONTEND_EQUIPMENT_GEAR_OUTPUT_H

#include "equipment_gear.h"
#include "equipment_output.h"

bool frontend_equipment_gear_view(frontend_equipment_gear_presenter *,
    const application_equipment_gear_presentation *, const q3n_selected_weapon_view *, float field_of_view,
    void *, bool (*current)(void *), frontend_equipment_output **, bool *submitted, qa_error *);
bool frontend_equipment_gear_held(frontend_equipment_gear_presenter *,
    const application_equipment_gear_presentation *, const q3n_selected_weapon_held *, bool reduced_flashes,
    void *, bool (*current)(void *), frontend_equipment_output **, bool *submitted, qa_error *);

#endif
