/* Typed views of the engine fragment of TS ModHostServices (mods.ts:87).
 * qa_session_mod_engine_services keeps these members as const void *:
 *   scene   -> const qa_session_mod_engine_scene *
 *   physics -> const qa_session_mod_engine_physics *
 *   clients_visibility -> const qa_q1_client_visibility_scene *
 *   clients_at returns const qa_q1_visibility_client *
 * GPL-2.0-or-later. */
#ifndef QA_WORLD_SESSION_MOD_ENGINE_H
#define QA_WORLD_SESSION_MOD_ENGINE_H
#include <stdbool.h>
#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/scene.h"
#include "qa/world/collision/index.h"

/* TS Pick<SceneQueries,"trace"|"pointContents"> &
 * Partial<Pick<SharedSceneQueries, ...>>. queries.box_leaves and
 * queries.areas_connected are the optional members (NULL = TS
 * undefined). shared non-NULL means geometry, leafArea,
 * adjustAreaPortal* and nativeQ3ClipModels are all present. */
typedef struct qa_session_mod_engine_scene {
  qa_scene_queries queries;
  qa_shared_scene_queries *shared;
} qa_session_mod_engine_scene;

/* TS Pick<SharedPhysics, "bindSource"|"setCollision"|"step"|
 * "touchTriggers"|"q1PusherServices"|"readQ1Pusher"|"writeQ1Pusher">.
 * bind_source returns a handle for unbind_source (TS unbind closure). */
typedef struct qa_session_mod_engine_physics {
  void *ctx;
  int (*bind_source)(void *ctx, qa_owned_actor actor, qa_app_sim_source_body_physics source);
  void (*unbind_source)(void *ctx, int handle);
  void (*set_collision)(void *ctx, qa_owned_actor actor, qa_actor_collision collision);
  const char *(*step)(void *ctx, qa_owned_actor actor, double elapsed);
  void (*touch_triggers)(void *ctx, qa_owned_actor actor);
  /* env is caller-owned and must outlive the services; the
   * implementation fills env->physics with its own SharedPhysics. */
  qa_q1_pusher_services (*q1_pusher_services)(void *ctx, qa_app_sim_q1_pusher_env *env);
  bool (*read_q1_pusher)(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out);
  void (*write_q1_pusher)(void *ctx, qa_q1_physics_entity entity);
} qa_session_mod_engine_physics;

/* TS QuakeCLocalMessageHost.camera. */
typedef struct qa_session_mod_engine_camera {
  void *ctx;
  void (*read)(void *ctx, qa_actor_id actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
} qa_session_mod_engine_camera;

/* TS events.emit(content, event, time?, recipient?). event.content and
 * event.recipient/has_recipient carry the content and recipient arguments;
 * the rest of event is the TS SourcePresentationEvent (types.h), borrowed
 * for the call. */
typedef struct qa_session_mod_emit {
  qa_app_sim_presentation_event event;
  bool has_time;
  qa_source_time time;
} qa_session_mod_emit;

/* TS events.registerResource(content, path, resource). */
typedef struct qa_session_mod_resource_registration {
  qa_content_id content;
  const char *path;
  qa_resolved_resource_reference resource;
} qa_session_mod_resource_registration;
#endif
