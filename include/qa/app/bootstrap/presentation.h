/* Port of src/app/bootstrap/presentation.ts. WorldSeatPresentation
 * wires landed seat services (effects/HUD/text/native renderer)
 * behind the host below; the viewport/camera helpers are ported in
 * full. PlayerView (simulation/types.ts) rides the view below.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PRESENTATION_H
#define QA_APP_BOOTSTRAP_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
/* TS PlayerView fields read by the camera helpers. */
typedef struct qa_app_player_view {
  bool has_client_view_offset_delta;
  qa_vec3 client_view_offset_delta;
  bool foreign_character_death;
  qa_vec3 origin;
  double view_height;
  qa_vec3 angles;
} qa_app_player_view;
/* Aborts (RangeError) on an invalid layout. */
qa_rect qa_app_seat_viewport(double index, double count, double width, double height);
qa_scene_camera qa_app_camera_with_kick(qa_scene_camera camera, qa_vec3 kick);
qa_scene_camera qa_app_camera_with_client_offset(qa_scene_camera camera, qa_app_player_view player);
qa_scene_camera qa_app_camera_with_character_death(qa_scene_camera camera, qa_app_player_view player);
/* TS WorldSeatPresentation host: one callback per TS method the
 * class delegates to its landed collaborators. Error strings are
 * borrowed; NULL = success unless noted. */
typedef struct qa_app_world_seat_host {
  void *ctx;
  const char *(*prepare)(void *ctx);
  const char *(*receive)(void *ctx, const void *events, size_t count);
  const char *(*frame)(void *ctx, double sequence);
  const char *(*replace)(void *ctx, void *candidate);
  const char *(*close)(void *ctx);
  double alpha;
} qa_app_world_seat_host;
typedef struct qa_app_world_seat_presentation qa_app_world_seat_presentation;
qa_app_world_seat_presentation *qa_app_world_seat_presentation_create(qa_app_world_seat_host host);
double qa_app_world_seat_presentation_alpha(const qa_app_world_seat_presentation *presentation);
const char *qa_app_world_seat_presentation_prepare(qa_app_world_seat_presentation *presentation);
const char *qa_app_world_seat_presentation_receive(qa_app_world_seat_presentation *presentation,
    const void *events, size_t count);
const char *qa_app_world_seat_presentation_frame(qa_app_world_seat_presentation *presentation, double sequence);
void qa_app_world_seat_presentation_close(qa_app_world_seat_presentation *presentation);
#endif
