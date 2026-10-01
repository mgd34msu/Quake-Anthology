#ifndef QA_FRONTEND_EQUIPMENT_GEAR_WORLD_OUTPUT_H
#define QA_FRONTEND_EQUIPMENT_GEAR_WORLD_OUTPUT_H

#include "equipment_gear.h"
#include "../application/equipment_gear_world.h"

typedef struct frontend_equipment_gear_world_output frontend_equipment_gear_world_output;
/* Project the actual owned hook and its cable once per entered view. The
 * output retains its private registry until it is discarded or submitted. */
bool frontend_equipment_gear_world_prepare(qa_frontend *,
    const application_equipment_gear_world_view *, qa_actor_id viewer,
    void *, bool (*current)(void *), frontend_equipment_gear_world_output **, qa_error *);
bool frontend_equipment_gear_world_submit(frontend_equipment_gear_world_output *,
    qa_q3_presentation *, const qa_q3_scene_options *, qa_scene_frame *, qa_error *);
void frontend_equipment_gear_world_destroy(frontend_equipment_gear_world_output *);

#endif
