#ifndef QA_APPLICATION_UNIFIED_PLAYER_H
#define QA_APPLICATION_UNIFIED_PLAYER_H

#include "network_unified.h"
#include "qa/application_qc_presentation.h"

typedef struct application_unified_player_camera {
    qa_application_qc_message_source source;
    qa_actor_id recipient, view_entity;
    uint32_t source_slot, intermission;
    qa_vec3 angles;
    bool has_angles;
} application_unified_player_camera;

/* The actual local decoder supplies its full-generation camera and optional
 * received STAT3 ammo override. current proves that retained decoder receipt;
 * declared weapons and timers are read from the physical Source below. */
typedef struct application_unified_player_external {
    void *context;
    bool (*current)(void *, qa_application *, const application_unified_source *,
        qa_net_client_id, const qa_unified_session_player *);
    const application_unified_player_camera *camera;
    int32_t qc_ammo;
    bool has_qc_ammo;
} application_unified_player_external;

/* Owned CHECKPOINT {view,ui}, observed for the exact physical Source recipient.
 * The caller retains the real Source frame while assembling its parent output.
 * Failure leaves *out unchanged. */
bool application_unified_player_values(qa_application *,
    const application_unified_source *, qa_net_client_id,
    const qa_unified_session_player *, const application_unified_player_external *,
    qa_unified_document **out, qa_error *);

#endif
