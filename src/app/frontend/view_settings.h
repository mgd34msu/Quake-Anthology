#ifndef QA_FRONTEND_VIEW_SETTINGS_H
#define QA_FRONTEND_VIEW_SETTINGS_H
#include "shared_storage.h"
#include "qa/console_cvars_prepare.h"
#include "qa/application_startup_prepare.h"
#include "qa/application_engine_shutdown.h"
#include "qa/application_client_prepare.h"

typedef struct frontend_view_settings frontend_view_settings;
typedef struct frontend_view_preparation frontend_view_preparation;
typedef enum frontend_view_transition {
    FRONTEND_VIEW_INITIAL, FRONTEND_VIEW_REPLACEMENT, FRONTEND_VIEW_BORROWED
} frontend_view_transition;
/* The real frontend recipient applies the preference to ordinary source
 * players and the qualified source-client cg_fov owner. Game cameras retain
 * their own field of view. The installed slot outlives this owner. */
bool frontend_view_settings_create(qa_frontend *,qa_cvars *,void *context,
    bool (*changed)(void *,double,qa_error *),frontend_view_settings **,qa_error *);
bool frontend_view_settings_parent_is(const frontend_view_settings *,const qa_frontend *,const qa_cvars *);
/* Pure published preference. present reports the retained explicit override;
 * field_of_view always supplies the current ordinary-view fallback. */
bool frontend_view_settings_read(const frontend_view_settings *,double *field_of_view,bool *explicit_override);
bool frontend_view_settings_has_published(const frontend_view_settings *,bool *);
bool frontend_view_settings_preferences(const frontend_view_settings *,frontend_shared_view_preferences *);
typedef struct frontend_q1_view_settings {
    double size,back,up,right;
    bool overlay_status,chase;
} frontend_q1_view_settings;
typedef struct frontend_q1_motion_settings {
    float bob, bob_cycle, bob_up, roll_speed, roll_angle;
    float idle_scale, kick_time, kick_roll, kick_pitch, cshift_percent;
    bool contents_blend;
    qa_vec3 idle_cycle, idle_level, offset;
} frontend_q1_motion_settings;
typedef struct frontend_q1_motion_input {
    qa_vec3 origin, angles, entity_angles, velocity, punch;
    double seconds, frame_seconds;
    float view_height, view_size;
    bool quakeworld, grounded, spectator, dead, intermission;
} frontend_q1_motion_input;
bool frontend_view_settings_q1_motion_register(qa_cvars *,uint64_t owner,bool quakeworld,qa_error *);
bool frontend_view_settings_q1_motion_owns(const char *,bool quakeworld);
bool frontend_view_settings_q1_motion_sample(const qa_cvars *,bool quakeworld,
    frontend_q1_motion_settings *,qa_error *);
void frontend_view_q1_damage(const frontend_q1_motion_settings *,qa_vec3 origin,qa_vec3 angles,
    uint8_t armor,uint8_t blood,qa_vec3 from,double seconds,frontend_q1_view_motion *);
qa_scene_vec4 frontend_view_q1_blend(const frontend_q1_motion_settings *,
    const frontend_q1_view_motion *,int32_t contents,bool quakeworld);
const char *frontend_view_q1_face(int32_t health,uint32_t items,double seconds,
    const frontend_q1_view_motion *);
bool frontend_view_q1_damage_origin(uint8_t armor,uint8_t blood,const double from[3],qa_vec3 *,qa_error *);
bool frontend_view_q1_local_damage(qa_frontend *,qa_actor_id,uint8_t armor,uint8_t blood,
    qa_vec3 from,qa_error *);
bool frontend_view_q1_chase(const frontend_q1_view_settings *,qa_collision_geometry *,
    qa_vec3 eye,qa_vec3 aim_angles,qa_vec3 *origin,qa_vec3 *angles,qa_error *);
/* One original Q1/QW refdef kernel; history belongs to the actual CLIENT seat
 * or received source and is rebuilt by fresh/load constructors. */
void frontend_view_q1_motion(const frontend_q1_motion_settings *,const frontend_q1_motion_input *,
    frontend_q1_view_motion *,frontend_q1_view_pose *);
bool frontend_remote_q1_view_pose_read(struct frontend_remote_q1 *,frontend_q1_view_pose *,qa_error *);
/* Samples published canonical Q1/QW settings and applies the donor viewsize
 * clamp to that registry before returning. Candidate tickets remain fenced. */
bool frontend_view_settings_q1_sample(frontend_view_settings *,qa_console_dialect,
    frontend_q1_view_settings *,qa_error *);
/* The caller supplies its genuine initial/replacement/borrowed publication
 * receipt. Preparation retains the actual previous and candidate scalar. */
bool frontend_view_settings_prepare(frontend_view_settings *,const qa_launch_snapshot *,
    const qa_cvars_edit *,frontend_view_transition,frontend_view_preparation **,qa_error *);
bool frontend_view_settings_prepare_client(frontend_view_settings *,
    const qa_application_client_preparation *,const qa_cvars_edit *,frontend_view_transition,
    frontend_view_preparation **,qa_error *);
bool frontend_view_settings_ready_is(const frontend_view_preparation *);
void frontend_view_settings_publish(frontend_view_preparation *);
/* Apply only after scalar publication and native/resource lease release.
 * Keep this child through the actual scalar observer drain, then finish. */
bool frontend_view_settings_apply(frontend_view_preparation *,qa_error *);
bool frontend_view_settings_finish(frontend_view_preparation **,qa_error *);
bool frontend_view_settings_abort(frontend_view_preparation **,qa_error *);
bool frontend_view_settings_checkpoint(const frontend_view_settings *,qa_buffer *,qa_error *);
bool frontend_view_settings_restore(frontend_view_settings *,qa_bytes,qa_error *);
bool frontend_view_settings_destroy(frontend_view_settings **,qa_error *);
/* Detach callbacks from the same real ENGINE registry after its returned loan
 * and canonical cancellation. The loan retains the registry until finish. */
bool frontend_view_settings_shutdown(frontend_view_settings **,
    const qa_application_engine_shutdown *,qa_error *);
bool frontend_view_settings_rebind(frontend_view_settings *,qa_frontend *,
    frontend_view_settings **actual_slot,void *actual_context,qa_error *);
#endif
