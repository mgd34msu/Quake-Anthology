/* Port of src/app/bootstrap/q1-client-settings.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q1_CLIENT_SETTINGS_H
#define QA_APP_BOOTSTRAP_Q1_CLIENT_SETTINGS_H
#include <stdbool.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
typedef struct qa_app_q1_view_commands qa_app_q1_view_commands;
qa_app_q1_view_commands *qa_app_register_q1_view_commands(qa_command_buffer *commands);
void qa_app_q1_view_commands_release(qa_app_q1_view_commands *registration);
/* profile_all selects the TS "all"; otherwise profile is used.
 * Pass profile_all=false and the registry dialect for the TS default. */
void qa_app_register_q1_client_settings(qa_cvar_registry *cvars, qa_command_dialect profile, bool profile_all);
typedef struct qa_app_q1_chase_settings { double back, up, right; } qa_app_q1_chase_settings;
typedef struct qa_app_q1_view_settings {
  double size;
  bool overlay_status;
  bool has_chase;
  qa_app_q1_chase_settings chase;
} qa_app_q1_view_settings;
/* Returns false for the TS null. has_profile=false uses cvars dialect. */
bool qa_app_read_q1_view_settings(qa_cvar_registry *cvars, qa_command_dialect profile,
    bool has_profile, qa_app_q1_view_settings *out);
qa_scene_camera qa_app_q1_chase_camera(qa_scene_camera camera, qa_vec3 angles,
    qa_app_q1_chase_settings settings, const qa_scene_queries *scene,
    qa_numeric_profile numeric, qa_actor_id actor);
qa_rect qa_app_q1_view_rectangle(qa_rect area, double viewsize, bool intermission, bool overlay_status);
qa_scene_camera qa_app_q1_view_camera(qa_scene_camera camera, qa_rect area,
    qa_app_q1_view_settings settings, bool intermission);
#endif
