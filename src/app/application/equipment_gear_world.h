#ifndef QA_APPLICATION_EQUIPMENT_GEAR_WORLD_H
#define QA_APPLICATION_EQUIPMENT_GEAR_WORLD_H

#include "equipment_gear_presentation.h"

typedef struct application_equipment_gear_world_view {
    application_equipment_gear_presentation source;
    qa_body_state player_body, tether_body;
    qa_vec3 player_angles;
    float view_height;
    bool offhand;
} application_equipment_gear_world_view;

/* An actual live hook projects through its gear-owned canonical WORLD body.
 * Offhand and weapon-slot hooks both remain visible. No GAME code runs. */
bool application_equipment_gear_world_read(qa_application *, qa_actor_id,
    application_equipment_gear_world_view *, bool *visible, qa_error *);
bool application_equipment_gear_world_current(qa_application *,
    const application_equipment_gear_world_view *);

#endif
