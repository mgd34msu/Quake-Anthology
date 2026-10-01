#ifndef QA_Q3_NATIVE_BODY_H
#define QA_Q3_NATIVE_BODY_H

#include "client_info.h"
#include "pose.h"

typedef struct q3n_frame q3n_frame;
typedef struct q3n_remote_entity q3n_remote_entity;

typedef struct q3n_body_options {
    int32_t time, frame_milliseconds, local_view_client, shadow_mode;
    float swing_speed, shadow_plane;
    bool no_player_animations, animations_disabled;
    bool third_person, camera_mode, shadow_visible;
} q3n_body_options;
typedef struct q3n_player_body {
    qa_q3_ref_entity parts[3];
    uint32_t count;
} q3n_player_body;

/* Real media replacement and physical actor generation changes reset this
 * private cgame pose. The reset retains the donor's discarded lerp timing. */
void q3n_player_reset(q3n_player_pose *, qa_vec3 source_angles);
/* Produces lower/upper/head geometry from actual client handles and source S.
 * Event/effect consumers apply passes and attachments to these authored refs. */
bool q3n_player_body_build(qa_q3_presentation_assets *, q3n_player_pose *,
    const q3n_client_info *, const qa_q3_entity *, qa_vec3 origin, qa_vec3 angles,
    const q3n_body_options *, q3n_player_body *, qa_error *);
/* The actual remote row owns its private pose. Client handles and animations
 * retain the reached client-info revisions through both tag callbacks. */
bool q3n_player_body_build_remote(const q3n_frame *, const q3n_remote_entity *,
    const q3n_client_info *, const q3n_body_options *, q3n_player_body *, qa_error *);

#endif
