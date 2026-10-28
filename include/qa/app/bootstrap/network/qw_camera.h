/* Port of src/app/bootstrap/network/qw-camera.ts (QW cl_cam
 * spectator camera). Maps become slot arrays (0..31).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_QW_CAMERA_H
#define QA_APP_BOOTSTRAP_NETWORK_QW_CAMERA_H
#include <stdbool.h>
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
typedef struct qa_app_qw_camera_options {
  void *ctx;
  double (*hightrack)(void *ctx);
  double (*chasecam)(void *ctx);
} qa_app_qw_camera_options;
typedef struct qa_app_qw_camera_player {
  const char *name; bool spectator; double frags;
} qa_app_qw_camera_player;
typedef struct qa_app_qw_camera_trace {
  double fraction; qa_vec3 end; bool in_water;
} qa_app_qw_camera_trace;
typedef struct qa_app_qw_camera_frame {
  qa_qw_player_state self;
  bool has_player[32]; qa_qw_player_state players[32];
  bool has_user[32]; qa_app_qw_camera_player users[32];
  double seconds;
} qa_app_qw_camera_frame;
typedef struct qa_app_qw_camera_view {
  qa_vec3 origin, angles; qa_qw_player_state target; bool chase;
} qa_app_qw_camera_view;
typedef struct qa_app_qw_camera_host {
  void *ctx;
  void (*send_command)(void *ctx, const char *text);
  qa_app_qw_camera_trace (*trace)(void *ctx, qa_vec3 start, qa_vec3 end);
  void (*print)(void *ctx, const char *text);
} qa_app_qw_camera_host;
typedef struct qa_app_qw_spectator_camera qa_app_qw_spectator_camera;
qa_app_qw_spectator_camera *qa_app_qw_spectator_camera_create(
    qa_app_qw_camera_options options, qa_app_qw_camera_host host);
void qa_app_qw_spectator_camera_destroy(qa_app_qw_spectator_camera *camera);
/* false = TS null view. */
bool qa_app_qw_spectator_camera_view(const qa_app_qw_spectator_camera *camera, qa_app_qw_camera_view *out);
bool qa_app_qw_spectator_camera_take_teleport(qa_app_qw_spectator_camera *camera, qa_vec3 *out);
void qa_app_qw_spectator_camera_reset(qa_app_qw_spectator_camera *camera);
qa_qw_user_command qa_app_qw_spectator_camera_command(qa_app_qw_spectator_camera *camera,
    qa_qw_user_command command, const qa_app_qw_camera_frame *frame);
#endif
