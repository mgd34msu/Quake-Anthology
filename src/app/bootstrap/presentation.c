/* Port of src/app/bootstrap/presentation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/presentation.h"
#include <math.h>
#include <stdlib.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
qa_rect qa_app_seat_viewport(double index, double count, double width, double height) {
  if (index != trunc(index) || index < 0 || index >= count || count < 1 || count > 4)
    qa_range_error("Invalid local seat layout");
  if (count == 1) { qa_rect out = {0, 0, width, height}; return out; }
  double columns = count == 2 ? 1 : 2, rows = 2;
  double column = fmod(index, columns), row = trunc(index / columns);
  double x = trunc(column * width / columns), y = trunc(row * height / rows);
  qa_rect out = {x, y, trunc((column + 1) * width / columns) - x,
      trunc((row + 1) * height / rows) - y};
  return out;
}
static qa_vec3 rotate(const qa_axis *axis, qa_vec3 v) {
  qa_vec3 out = {
      axis->vectors[0].x * v.x + axis->vectors[1].x * v.y + axis->vectors[2].x * v.z,
      axis->vectors[0].y * v.x + axis->vectors[1].y * v.y + axis->vectors[2].y * v.z,
      axis->vectors[0].z * v.x + axis->vectors[1].z * v.y + axis->vectors[2].z * v.z};
  return out;
}
qa_scene_camera qa_app_camera_with_kick(qa_scene_camera camera, qa_vec3 kick) {
  if (kick.x == 0 && kick.y == 0 && kick.z == 0) return camera;
  qa_axis local = qa_angles_to_axis(kick);
  qa_scene_camera out = camera;
  for (size_t i = 0; i < 3; i++) out.axis.vectors[i] = rotate(&camera.axis, local.vectors[i]);
  return out;
}
qa_scene_camera qa_app_camera_with_client_offset(qa_scene_camera camera, qa_app_player_view player) {
  if (!player.has_client_view_offset_delta || camera.clip_is_portal) return camera;
  qa_scene_camera out = camera;
  out.origin.x += player.client_view_offset_delta.x;
  out.origin.y += player.client_view_offset_delta.y;
  out.origin.z += player.client_view_offset_delta.z;
  return out;
}
qa_scene_camera qa_app_camera_with_character_death(qa_scene_camera camera, qa_app_player_view player) {
  if (!player.foreign_character_death || camera.clip_is_portal) return camera;
  qa_scene_camera out = camera;
  out.origin = player.origin;
  out.origin.z = player.origin.z + player.view_height;
  out.axis = qa_angles_to_axis(player.angles);
  return out;
}
struct qa_app_world_seat_presentation { qa_app_world_seat_host host; bool closed; };
qa_app_world_seat_presentation *qa_app_world_seat_presentation_create(qa_app_world_seat_host host) {
  qa_app_world_seat_presentation *presentation = calloc(1, sizeof *presentation);
  if (presentation == NULL) qa_range_error("Out of memory");
  presentation->host = host;
  return presentation;
}
double qa_app_world_seat_presentation_alpha(const qa_app_world_seat_presentation *presentation) {
  return presentation->host.alpha;
}
const char *qa_app_world_seat_presentation_prepare(qa_app_world_seat_presentation *presentation) {
  return presentation->host.prepare(presentation->host.ctx);
}
const char *qa_app_world_seat_presentation_receive(qa_app_world_seat_presentation *presentation,
    const void *events, size_t count) {
  return presentation->host.receive(presentation->host.ctx, events, count);
}
const char *qa_app_world_seat_presentation_frame(qa_app_world_seat_presentation *presentation, double sequence) {
  return presentation->host.frame(presentation->host.ctx, sequence);
}
void qa_app_world_seat_presentation_close(qa_app_world_seat_presentation *presentation) {
  if (presentation == NULL || presentation->closed) return;
  presentation->closed = true;
  /* TS close aggregates collaborator failures; the host returns the
   * aggregate message which aborts here like the TS throw. */
  const char *error = presentation->host.close(presentation->host.ctx);
  void *host_ctx = presentation->host.ctx;
  (void)host_ctx;
  free(presentation);
  if (error != NULL) qa_range_error("%s", error);
}
