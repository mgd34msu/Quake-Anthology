/* Port of src/app/bootstrap/presentation-scene.ts.
 * ApplicationWorldScene groups landed scene model passes behind
 * the host below; seatModelVisible is ported in full.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PRESENTATION_SCENE_H
#define QA_APP_BOOTSTRAP_PRESENTATION_SCENE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
/* viewer NULL = TS null. */
bool qa_app_seat_model_visible(const qa_actor_id *viewer, qa_actor_id actor, bool view_weapon);
typedef struct qa_app_world_scene_host {
  void *ctx;
  const char *(*prepare)(void *ctx, const void *source);
  const char *(*frame)(void *ctx, const void *camera);
  const char *(*close)(void *ctx);
} qa_app_world_scene_host;
typedef struct qa_app_application_world_scene qa_app_application_world_scene;
qa_app_application_world_scene *qa_app_application_world_scene_create(qa_app_world_scene_host host);
const char *qa_app_application_world_scene_prepare(qa_app_application_world_scene *scene, const void *source);
const char *qa_app_application_world_scene_frame(qa_app_application_world_scene *scene, const void *camera);
void qa_app_application_world_scene_close(qa_app_application_world_scene *scene);
#endif
