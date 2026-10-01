#ifndef QA_APPLICATION_EQUIPMENT_H
#define QA_APPLICATION_EQUIPMENT_H

#include "qa/application.h"

typedef enum qa_application_ammo_warning {
    QA_APPLICATION_AMMO_NONE,
    QA_APPLICATION_AMMO_LOW,
    QA_APPLICATION_AMMO_EMPTY
} qa_application_ammo_warning;

typedef struct qa_application_equipment_view {
    qa_actor_id actor;
    qa_actor_owner provider, primary;
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
    qa_q3_player_state q3_state;
    qa_q3_player q3_source;
    int32_t q3_time_ms;
    qa_q3_fire_stamp q3_fire;
    qa_application_ammo_warning warning;
    bool selected, visible, has_frame, has_skin, has_rate, has_source_gun_pose, has_q3_state, has_q3_source;
    bool has_weapon_status, finite_ammo, has_ammo_to_start, low_ammo, has_start_requirement;
} qa_application_equipment_view;

/* Observes the actual selected arsenal and physical actor. Model, label and
 * resource pointers borrow that owner until its next mutation or retirement.
 * Missing optional source fields remain explicit. Output is unchanged on error. */
bool qa_application_equipment_read(qa_application *, qa_actor_id,
    qa_application_equipment_view *, qa_error *);
bool qa_application_equipment_current(qa_application *, const qa_application_equipment_view *);
/* Pure product qualification for an actual retained equipment registry during
 * capture/import. Reads its real constructed provider; no source call runs. */
bool qa_application_equipment_q3_product_read(const qa_application *, qa_actor_owner,
    qa_q3_product *, qa_error *);

#endif
