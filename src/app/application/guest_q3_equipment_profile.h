#ifndef QA_APPLICATION_GUEST_Q3_EQUIPMENT_PROFILE_H
#define QA_APPLICATION_GUEST_Q3_EQUIPMENT_PROFILE_H

#include "qa/qvm.h"

typedef struct application_q3_equipment_region { uint32_t entry, join; } application_q3_equipment_region;
typedef struct application_q3_equipment_status {
    uint32_t entry, decision;
    bool taken;
    application_q3_equipment_region *ammo;
    size_t ammo_count;
} application_q3_equipment_status;
typedef struct application_q3_equipment_profile {
    uint32_t hud, view_entry, view_decision, warning_entry, warning_state;
    int32_t warning_none, warning_low, warning_empty;
    uint32_t held_entry, gun, parent_argument, state_argument, entity_argument, entity_number_offset;
    application_q3_equipment_status *status;
    size_t status_count;
    bool present, view_taken, status_regions;
} application_q3_equipment_profile;

/* Declaration bytes are the exact artifact-matched compatibility value. With
 * no declaration, only the qualified original cgame layouts supply a boundary. */
bool application_q3_equipment_profile_read(const qa_qvm_image *, qa_qvm_role,
    qa_qvm_abi, qa_bytes, application_q3_equipment_profile *, qa_error *);
void application_q3_equipment_profile_free(application_q3_equipment_profile *);

#endif
