#ifndef APPLICATION_WEAPON_TRAJECTORIES_H
#define APPLICATION_WEAPON_TRAJECTORIES_H

#include "qa/application.h"
#include "qa/builtin.h"
#include "qa/launch.h"
#include "qa/persistence_content.h"

bool application_trajectories_configure(qa_application *, const qa_launch_snapshot *, qa_error *);
bool application_trajectories_destroy(qa_application *, qa_error *);
bool application_trajectories_actor_released(qa_application *, qa_actor_record, qa_error *);
bool application_trajectory_launch(void *, const qa_builtin_weapon_launch *,
    qa_builtin_trajectory_update *, bool *, qa_error *);
bool application_trajectory_controls(void *, qa_actor_id);
bool application_trajectory_step(void *, qa_actor_id, const qa_body_state *, uint64_t,
    qa_builtin_trajectory_update *, bool *, qa_error *);
bool application_trajectories_capture(qa_application *, qa_buffer *, qa_error *);
bool application_trajectories_content_collect(qa_application *,
    qa_application_content_graph *, qa_error *);
bool application_trajectories_restore_saved(qa_application *, qa_bytes, qa_error *);
bool application_trajectories_restore_finish(qa_application *, qa_error *);

#endif
