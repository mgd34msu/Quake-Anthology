/* Port of src/app/bootstrap/q3-client/services.ts.
 * ApplicationAudio (../audio.ts, sibling slice) and the assets
 * world surface are host mirrors; scene/resources/audio/draw/
 * cinematics construction and the Q3ClientSound adapter are ported
 * here over landed pres classes. Creation is synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_SERVICES_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_SERVICES_H
#include <stdbool.h>
#include "qa/app/bootstrap/audio/q3.h"
#include "qa/app/bootstrap/q3_client/assets.h"
#include "qa/app/bootstrap/q3_client/cinematics.h"
#include "qa/app/bootstrap/q3_client/collision.h"
#include "qa/audio/types.h"
#include "qa/content/q3/presentation/audio.h"
#include "qa/content/q3/presentation/client.h"
#include "qa/content/q3/presentation/resources.h"
#include "qa/content/q3/presentation/scene.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/media/audio.h"
#include "qa/text/draw2d.h"
#include "qa/world/collision/index.h"
#include "qa/world/collision/q3/settings.h"
/* TS ApplicationAudio surface used here (../audio.ts mirror). */
typedef struct qa_app_q3_application_audio {
  void *ctx;
  qa_cinematic_mixer engine; /* cinematics audio.engine */
  void (*play_music)(void *ctx, qa_content_id content, const char *track);
} qa_app_q3_application_audio;
/* TS assets.world surface used here (../assets.ts mirror). */
typedef struct qa_app_q3_services_world {
  void *ctx;
  const qa_q3_pres_fog_selection *(*fog_selections)(void *ctx, size_t *out_count);
  /* content.world: decoded map kind text + q3 geometry. */
  const char *map_kind; /* "q3-bsp" | ... */
  const qa_q3_world_geometry *map_geometry;
} qa_app_q3_services_world;
/* TS ApplicationQ3ServiceOptions. */
typedef struct qa_app_application_q3_service_options {
  bool has_owner;
  qa_provider_id owner;
  const char *resource_handles; /* NULL = TS undefined */
  /* remapShader?: NULL fn = TS undefined. */
  void (*remap_shader)(void *ctx, const char *original, const char *replacement, const char *offset);
  void *remap_ctx;
  const qa_app_system_cinematic_host *system_cinematics; /* NULL = undefined */
  qa_collision_map_settings *collision_settings;
  qa_app_application_q3_assets *media;
  qa_app_q3_services_world world;
  qa_app_q3_application_audio audio;
  qa_seat_id seat;
  /* TS viewport: the caller's Rect object, shared by the scene recorder
   * and the text sink (both read it where TS reads it). Borrowed; must
   * outlive the services. */
  const qa_rect *viewport;
  qa_scene_queries queries;
  qa_shared_scene_queries *shared_queries; /* NULL = not SharedSceneQueries */
  bool (*actor_at)(void *ctx, double number, qa_actor_id *out);
  void *actor_ctx;
  double (*now)(void *ctx); void *now_ctx;
  double (*frame_number)(void *ctx); void *frame_ctx;
  /* output.* */
  void *output_ctx;
  void (*output_scene)(void *ctx, qa_q3_pres_presented_scene *scene);
  void (*output_command)(void *ctx, const qa_render_command *command);
  void (*output_text)(void *ctx, const qa_material_text_draw *draw);
  void (*output_audio)(void *ctx, qa_app_q3_seat_audio_operation operation);
  void (*output_listener)(void *ctx, qa_vec3 origin, qa_axis axis);
} qa_app_application_q3_service_options;
/* TS ApplicationQ3Services record. */
typedef struct qa_app_application_q3_services qa_app_application_q3_services;
qa_app_application_q3_services *qa_app_create_application_q3_services(qa_app_application_q3_service_options options);
void qa_app_application_q3_services_destroy(qa_app_application_q3_services *services);
qa_q3_pres_scene_recorder *qa_app_application_q3_services_scene(qa_app_application_q3_services *services);
qa_q3_pres_q3_renderer_resources *qa_app_application_q3_services_resources(qa_app_application_q3_services *services);
/* TS services.sound: the content Q3ClientSound over these services. */
qa_q3_pres_client_sound qa_app_application_q3_services_sound(qa_app_application_q3_services *services);
qa_draw2d *qa_app_application_q3_services_draw(qa_app_application_q3_services *services);
qa_q3_pres_collision_world qa_app_application_q3_services_collision(qa_app_application_q3_services *services);
qa_app_application_q3_cinematics *qa_app_application_q3_services_cinematics(qa_app_application_q3_services *services);
#endif
