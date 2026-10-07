#ifndef QA_FRONTEND_EQUIPMENT_Q3_PRIVATE_H
#define QA_FRONTEND_EQUIPMENT_Q3_PRIVATE_H
#include "equipment_q3.h"

typedef struct equipment_q3_content equipment_q3_content;
struct frontend_equipment_q3_presenter {
    struct frontend_equipment_q3_presenter *next;
    equipment_q3_content *owner;
    qa_actor_id actor;
    q3n_selected_weapon_state state;
    qa_q3_presentation *recipient;
    uint32_t physical_seat;
    int32_t source_time_ms;
    qa_cvars *cvars;
    size_t users;
};
struct equipment_q3_content {
    equipment_q3_content *next;
    qa_frontend *frontend;
    frontend_equipment_q3_owner_view view;
    q3n_weapons *weapons;
    frontend_equipment_q3_presenter *presenters, *tail;
    bool admitting, restoring;
};
struct frontend_equipment_q3 {
    equipment_q3_content *contents, *tail;
    bool admitting;
};
void frontend_equipment_q3_content_dispose(equipment_q3_content *);

#endif
