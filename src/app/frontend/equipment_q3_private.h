#ifndef QA_FRONTEND_EQUIPMENT_Q3_PRIVATE_H
#define QA_FRONTEND_EQUIPMENT_Q3_PRIVATE_H
#include "equipment_q3.h"
#include "qa/hud_controls.h"
#include "qa/pool.h"

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
    qa_hud_cvar_handles hud_cvars;
    size_t users, slot;
};
struct equipment_q3_content {
    equipment_q3_content *next;
    qa_frontend *frontend;
    frontend_equipment_q3 *storage_owner;
    size_t slot;
    uint64_t source_generation, source_files_generation;
    qa_vfs *source_files;
    qa_arena actor_storage;
    qa_actor_index actor_index;
    frontend_equipment_q3_owner_view view;
    q3n_weapons *weapons;
    frontend_equipment_q3_presenter *presenters, *tail;
    bool admitting, restoring;
};
struct frontend_equipment_q3 {
    equipment_q3_content *contents, *tail;
    qa_arena storage;
    qa_pool content_records, presenter_records;
    qa_id_index content_index;
    bool admitting;
};
bool frontend_equipment_q3_content_dispose(equipment_q3_content *, qa_error *);

#endif
