#ifndef QA_APPLICATION_NATIVE_Q2_WEAPON_STAGE_H
#define QA_APPLICATION_NATIVE_Q2_WEAPON_STAGE_H
#include "native_q2_items.h"
#include "qa/equipment.h"
#include "guest_q3_mod.h"

typedef struct application_native_q2_weapon_stage application_native_q2_weapon_stage;
typedef struct application_native_q2_weapon_stage_options {
    application_native_q2_callbacks *callbacks;
    application_native_q2_items *items;
    qa_session *session;
    qa_inventory *inventory;
    qa_equipment *equipment;
    qa_actor_owner owner;
    void *context;
    bool (*actor_current)(void *, qa_actor_id, qa_error *);
    bool (*source_inputs)(void *, qa_actor_id,
        application_q3_mod_value [Q3_MOD_VALUE_COUNT],
        application_native_callback_inputs *, qa_error *);
} application_native_q2_weapon_stage_options;

/* Layout comes only from this callback document's items.weapons. Original
 * Source RAM owns selection, animation, latch and firing cadence. */
bool application_native_q2_weapon_stage_create(const application_native_q2_weapon_stage_options *,
    application_native_q2_weapon_stage **, qa_error *);
bool application_native_q2_weapon_stage_activate(application_native_q2_weapon_stage *, qa_error *);
bool application_native_q2_weapon_stage_suspend(application_native_q2_weapon_stage *, qa_error *);
bool application_native_q2_weapon_stage_admit(application_native_q2_weapon_stage *, qa_actor_id, qa_error *);
bool application_native_q2_weapon_stage_release(application_native_q2_weapon_stage *, qa_actor_id, qa_error *);
bool application_native_q2_weapon_stage_read(application_native_q2_weapon_stage *, qa_actor_id,
    qa_item_id *active, qa_item_id *pending, qa_error *);
bool application_native_q2_weapon_stage_request(application_native_q2_weapon_stage *, qa_actor_id,
    qa_item_id, uint64_t *, qa_weapon_request_status *, qa_error *);
bool application_native_q2_weapon_stage_status(application_native_q2_weapon_stage *, qa_actor_id,
    uint64_t, qa_item_id, qa_weapon_request_status *, qa_error *);
bool application_native_q2_weapon_stage_cancel(application_native_q2_weapon_stage *, qa_actor_id,
    uint64_t, qa_item_id, qa_error *);
bool application_native_q2_weapon_stage_accepts_attack(application_native_q2_weapon_stage *,
    qa_actor_id, bool *, qa_error *);
bool application_native_q2_weapon_stage_idle(const application_native_q2_weapon_stage *);
bool application_native_q2_weapon_stage_destroy(application_native_q2_weapon_stage **, qa_error *);
bool application_native_q2_weapon_stage_capture(application_native_q2_weapon_stage *, qa_buffer *, qa_error *);
/* Decode only retained request identities into an already admitted isolated
 * candidate. Actual source RAM and actor/item tables are restored by parents. */
bool application_native_q2_weapon_stage_restore(application_native_q2_weapon_stage *, qa_bytes, qa_error *);
#endif
