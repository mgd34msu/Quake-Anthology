#ifndef QA_APPLICATION_EQUIPMENT_H
#define QA_APPLICATION_EQUIPMENT_H

#include "qa/application.h"
#include "qa/application_q3_weapon_models.h"
#include "qa/equipment_weapon_slot.h"

typedef enum qa_application_ammo_warning {
    QA_APPLICATION_AMMO_NONE,
    QA_APPLICATION_AMMO_LOW,
    QA_APPLICATION_AMMO_EMPTY
} qa_application_ammo_warning;

typedef struct qa_application_equipment_view {
    qa_actor_id actor;
    qa_actor_owner provider, primary;
    qa_actor_owner gear_namespace;
    uint64_t gear_service_owner;
    qa_game_family family;
    qa_item_id item, ammo;
    const char *label, *view_model;
    const qa_resource *view_source;
    const qa_vfs *view_content;
    int32_t frame, skin;
    float rate;
    qa_vec3 kick_origin, kick_angles;
    qa_vec3 gun_origin, gun_angles;
    double ammo_count;
    qa_q3_weapon q3_weapon;
    int32_t source_weapon;
    qa_q3_player_state q3_state;
    qa_q3_player q3_source;
    int32_t q3_time_ms;
    qa_q3_fire_stamp q3_fire;
    qa_application_ammo_warning warning;
    bool selected, visible, has_frame, has_skin, has_rate, has_source_gun_pose, has_q3_state, has_q3_source;
    bool has_weapon_status, finite_ammo, has_ammo_to_start, low_ammo, has_start_requirement;
    /* A controller-selected EQUIPMENT slot has its own genuine source
     * namespace and is independent of the actor's ARSENAL binding. */
    bool equipment_slot;
    bool original_qvm;
    bool source_slot;
    qa_weapon_presentation source_binding;
    qa_item_id pending;
    qa_actor_owner pending_provider;
    uint64_t source_generation;
    qa_bytes source_icon, source_held;
} qa_application_equipment_view;

/* Observes the actual active equipment slot, otherwise the selected arsenal,
 * and its physical actor. Model, label and
 * resource pointers borrow that owner until its next mutation or retirement.
 * Missing optional source fields remain explicit. Output is unchanged on error. */
bool qa_application_equipment_read(qa_application *, qa_actor_id,
    qa_application_equipment_view *, qa_error *);
bool qa_application_equipment_source_read(qa_application *,qa_actor_id,
    qa_application_equipment_view *,bool *present,qa_error *);
bool qa_application_equipment_current(qa_application *, const qa_application_equipment_view *);
/* Requests a weapon from its actual admitted provider through the canonical
 * slot handoff. Acceptance comes from that Source binding; declared item
 * USE/DROP calls are independent and are not invoked here. */
bool qa_application_equipment_request_weapon(qa_application *, qa_actor_id,
    qa_actor_owner, qa_item_id, bool *accepted, qa_error *);
/* Pure product qualification for an actual retained equipment registry during
 * capture/import. Reads its real constructed provider; no source call runs. */
bool qa_application_equipment_q3_product_read(const qa_application *, qa_actor_owner,
    qa_q3_product *, qa_error *);
/* Reads only completed registrations in the selected original GAME's matching
 * CG receiver role for this actual launch seat ID. The renderer retains its own current
 * recipient lease. Handles borrow the returned CG registry. Genuine absence
 * clears present; no stock model is inferred from the source weapon number. */
bool qa_application_equipment_q3_models_read(qa_application *,
    const qa_application_equipment_view *, qa_actor_owner recipient, uint32_t launch_seat,
    qa_application_q3_weapon_models *, bool *present, qa_error *);
/* Qualifies the actual entered recipient's matching original CG/GAME model
 * owner before lazy Source registration. The caller retains its whole Draw
 * lease and registry; this runs no Source function and invents no draw refs. */
bool qa_application_equipment_q3_source_draw_match(qa_application *,
    const qa_application_equipment_view *, qa_actor_owner recipient, uint32_t launch_seat,
    const qa_q3_presentation_assets *recipient_assets, bool *matching, qa_error *);

#endif
