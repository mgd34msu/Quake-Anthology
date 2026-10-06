#ifndef QA_FRONTEND_REMOTE_Q2_EFFECTS_BRIDGE_H
#define QA_FRONTEND_REMOTE_Q2_EFFECTS_BRIDGE_H
#include "remote_q2_client.h"
#include "remote_q2_effects.h"
bool frontend_remote_q2_effects_records(frontend_remote_q2 *, const qa_q2_server_record *, size_t, qa_error *);
bool remote_q2_effects_create(frontend_remote_q2 *, qa_error *);
bool remote_q2_effects_frame(frontend_remote_q2 *, qa_error *);
bool remote_q2_hit_marker_sample(frontend_remote_q2 *, qa_error *);
bool remote_q2_effect_sound(void *, const char *, qa_vec3, qa_actor_id, double,
    int32_t, float, float, double, qa_error *);
bool remote_q2_effects_source_read(frontend_remote_q2 *, frontend_remote_q2_effects_source *, qa_error *);
const qa_scene_image *frontend_remote_q2_effects_image(const frontend_remote_q2 *);
bool remote_q2_effects_sample_prepare(frontend_remote_q2 *, const qa_scene_view *, float player_fov, qa_vec3 viewer_origin, qa_vec3 gun_offset,
    int32_t viewer_number, frontend_remote_q2_effects_sample *,
    const qa_scene_light **, size_t *, qa_error *);
#endif
