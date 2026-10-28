/* Port of src/app/bootstrap/presentation-scene.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/presentation_scene.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
bool qa_app_seat_model_visible(const qa_actor_id *viewer, qa_actor_id actor, bool view_weapon) {
  bool first_person = viewer != NULL && qa_same_actor(*viewer, actor);
  return view_weapon ? first_person : !first_person;
}
struct qa_app_application_world_scene { qa_app_world_scene_host host; };
qa_app_application_world_scene *qa_app_application_world_scene_create(qa_app_world_scene_host host) {
  qa_app_application_world_scene *scene = calloc(1, sizeof *scene);
  if (scene == NULL) qa_range_error("Out of memory");
  scene->host = host;
  return scene;
}
const char *qa_app_application_world_scene_prepare(qa_app_application_world_scene *scene, const void *source) {
  return scene->host.prepare(scene->host.ctx, source);
}
const char *qa_app_application_world_scene_frame(qa_app_application_world_scene *scene, const void *camera) {
  return scene->host.frame(scene->host.ctx, camera);
}
void qa_app_application_world_scene_close(qa_app_application_world_scene *scene) {
  if (scene == NULL) return;
  const char *error = scene->host.close(scene->host.ctx);
  free(scene);
  if (error != NULL) qa_range_error("%s", error);
}
