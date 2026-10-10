#ifndef QA_APPLICATION_NATIVE_Q2_EQUIPMENT_H
#define QA_APPLICATION_NATIVE_Q2_EQUIPMENT_H

#include "qa/application.h"
#include "qa/native.h"

typedef struct qa_application_native_q2_equipment_view {
    qa_actor_id actor;
    qa_actor_owner provider;
    qa_native_profile profile;
    uint32_t source_slot;
    qa_item_id item;
    int32_t gun_index, frame, skin, rate;
    qa_vec3 gun_offset, gun_angles, view_kick_angles;
    qa_string_id view_model;
    const qa_vfs *view_content;
    bool visible, has_skin, has_rate;
} qa_application_native_q2_equipment_view;

/* Reads the actual begun client's public state and qualified selected item.
 * MODEL names and content borrow their source owner until its next mutation or
 * retirement. This observation acquires no model resource and runs no source
 * callback. Gun vectors retain their public source conventions; view kick is
 * separate camera state. Classic has no skin/rate fields. Output stays intact
 * on failure. A fully restored, idle persistence candidate is also admitted. */
bool qa_application_native_q2_equipment_read(qa_application *, qa_actor_id,
    qa_application_native_q2_equipment_view *, qa_error *);

#endif
