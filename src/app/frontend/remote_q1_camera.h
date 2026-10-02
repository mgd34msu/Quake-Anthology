#ifndef QA_FRONTEND_REMOTE_Q1_CAMERA_H
#define QA_FRONTEND_REMOTE_Q1_CAMERA_H
#include "remote_q1_client.h"
#include "qa/source_save.h"
#include "view_settings.h"
typedef struct remote_q1_camera_view {
    qa_vec3 origin, angles;
    uint32_t target_flags, target_weapon_frame;
    uint8_t target_slot;
    bool chase;
} remote_q1_camera_view;
typedef struct remote_q1_camera {
    bool tracking, locked, has_view, has_teleport, self_present;
    uint8_t slot, old_buttons;
    qa_vec3 desired, teleport, self_origin;
    double last_view_seconds;
    remote_q1_camera_view view;
    struct qa_collision_geometry *geometry;
    qa_resource *map;
} remote_q1_camera;
bool remote_q1_camera_command(frontend_remote_q1 *, qa_qw_command *, uint64_t now_ns, qa_error *);
const remote_q1_camera_view *remote_q1_camera_read(const frontend_remote_q1 *);
bool remote_q1_camera_take_teleport(frontend_remote_q1 *, qa_vec3 *, bool *, qa_error *);
void remote_q1_camera_reset(frontend_remote_q1 *);
bool remote_q1_camera_fields(frontend_remote_q1 *, qa_source_save_io *);
bool frontend_remote_q1_chase_camera(frontend_remote_q1 *, const frontend_q1_view_settings *,
    qa_vec3 eye, qa_vec3 aim_angles, qa_vec3 *origin, qa_vec3 *angles, qa_error *);
bool remote_q1_camera_contents_blend(frontend_remote_q1 *,const qa_scene_view *,qa_scene_vec4 *,qa_error *);
#endif
