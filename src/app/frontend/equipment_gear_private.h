#ifndef QA_FRONTEND_EQUIPMENT_GEAR_PRIVATE_H
#define QA_FRONTEND_EQUIPMENT_GEAR_PRIVATE_H

#include "equipment_gear.h"

typedef struct equipment_gear_content equipment_gear_content;
struct frontend_equipment_gear_presenter {
    frontend_equipment_gear_presenter *next;
    equipment_gear_content *owner;
    qa_actor_id actor;
    q3n_selected_weapon_state state;
    size_t users;
};
struct equipment_gear_content {
    equipment_gear_content *next;
    qa_frontend *frontend;
    frontend_equipment_gear_owner_view view;
    q3n_weapons *weapons;
    frontend_equipment_gear_presenter *presenters, *tail;
    bool admitting, restoring;
};
struct frontend_equipment_gear {
    equipment_gear_content *contents, *tail;
    bool admitting;
};
bool frontend_equipment_gear_source(qa_frontend *, qa_actor_owner,
    qa_application_equipment_content *, const application_q3_grapple_definition **, qa_error *);
bool frontend_equipment_gear_content_create(qa_frontend *,
    const qa_application_equipment_content *, const application_q3_grapple_definition *,
    const frontend_visual_owner_view *, qa_q3_product, equipment_gear_content **, qa_error *);
bool frontend_equipment_gear_quiet(const equipment_gear_content *);
void frontend_equipment_gear_content_dispose(equipment_gear_content *);

#endif
