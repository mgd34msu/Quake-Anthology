/* The C-only accessors the typed remote presentations in app/bootstrap/network
 * take where TS reads LoadedApplicationContent, the identity owner and the scene
 * queries directly (remote-world.ts createSceneQueries(content.world),
 * content.recipe, identity.actor/ownedActor, content.mounts, scene.link/trace).
 * Shared by RemoteSeatSource and RemoteApplication, which build the same five
 * presentation classes.
 *
 * Every accessor taking `void *ctx` expects a qa_app_remote_access: the owner
 * keeps one as the first member of the struct it passes as the remote's ctx.
 * `void *loaded` is a qa_app_loaded_content; `void *scene` is the
 * qa_shared_scene_queries the world host creates. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_ACCESS_H
#define QA_APP_BOOTSTRAP_REMOTE_ACCESS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/network/q2_downloads.h"
#include "qa/app/bootstrap/network/q3_downloads.h"
#include "qa/app/bootstrap/network/qw_camera.h"
#include "qa/app/bootstrap/network/remote_q1.h"
#include "qa/app/bootstrap/network/remote_world.h"
#include "qa/app/bootstrap/network/unified_content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/scene.h"
#include "qa/world/collision/index.h"
#include "qa/world/session/mods.h"

typedef struct qa_app_remote_q2_mounts qa_app_remote_q2_mounts;
typedef struct qa_app_remote_access {
  const qa_identity_owner *identity;
  qa_session_id session;
  /* the latest world the remote's load hook returned (TS this.world.content) */
  qa_app_loaded_content *loaded;
  /* storage of the last content_world / content_read result */
  qa_scene_snapshot_world world;
  uint8_t *read; size_t read_length;
  /* Q2 RemoteContentMounts views handed to the Q2 presentation */
  qa_app_remote_q2_mounts **q2_mounts; size_t q2_mount_count;
} qa_app_remote_access;
void qa_app_remote_access_init(qa_app_remote_access *access, const qa_identity_owner *identity);
void qa_app_remote_access_release(qa_app_remote_access *access);

/* TS createSceneQueries(content.world) and the collision-settings binding of
 * RemoteWorldContent (ctx unused). */
qa_app_remote_world_host qa_app_remote_access_world_host(void);
/* The decoded world of a loaded content (TS content.world). */
qa_decoded_world qa_app_remote_decoded_world(const qa_app_loaded_content *content);

/* TS nextActorGeneration(session.session, slot), identity.actor/client/ownedActor. */
double qa_app_remote_access_next_generation(void *ctx, double slot);
qa_actor_id qa_app_remote_access_identity_actor(void *ctx, double slot, double generation);
qa_client_id qa_app_remote_access_identity_client(void *ctx, double slot, double generation);
/* provider is the qa_provider_reference of recipe.map.entities. */
qa_owned_actor qa_app_remote_access_owned_actor(void *ctx, qa_actor_id actor, const void *provider);
/* TS content.recipe / { resource: recipe.map.geometry, geometry: content.world }. */
const qa_executable_recipe *qa_app_remote_access_content_recipe(void *ctx, void *loaded);
const qa_scene_snapshot_world *qa_app_remote_access_content_world(void *ctx, void *loaded);
/* TS content.mounts.read(reference): bytes owned by the access until the next read. */
const uint8_t *qa_app_remote_access_content_read(void *ctx, void *loaded,
    const qa_resolved_resource_reference *reference, size_t *out_length);
/* TS scene.link / scene.unlink / scene.modelBounds / linkedActor(actor)?.collision.shape.kind === "model". */
void qa_app_remote_access_scene_link(void *ctx, void *scene, qa_linked_body body, qa_actor_collision collision);
void qa_app_remote_access_scene_unlink(void *ctx, void *scene, qa_actor_id actor);
qa_bounds qa_app_remote_access_scene_model_bounds(void *ctx, void *scene, double model);
bool qa_app_remote_access_scene_actor_is_model(void *ctx, void *scene, qa_actor_id actor);
/* TS QwRemotePresentation camera trace over this.scene with the movement profile
 * of the current world (raises "QW camera requires Q1 collision"). */
qa_app_qw_camera_trace qa_app_remote_access_qw_camera_trace(void *ctx, void *scene, qa_vec3 start, qa_vec3 end,
    bool has_pass_actor, qa_actor_id pass_actor);
/* TS (await content.mounts.resolve(path)) !== null. */
bool qa_app_remote_access_resolve_sound(void *ctx, void *loaded, const char *path);
/* TS performance.now(). */
double qa_app_remote_access_now_milliseconds(void *ctx);
/* TS UnifiedRemotePresentation readers: content.preparedMods, the presentation test,
 * recipe.map.entities, resolveUnifiedResource(content, key), recipe.map.geometry. */
const qa_prepared_mod *qa_app_remote_access_prepared_mods(void *ctx, void *loaded, size_t *out_count);
bool qa_app_remote_access_mod_has_presentation(void *ctx, void *loaded);
const void *qa_app_remote_access_map_entities_provider(void *ctx, void *loaded);
qa_resolved_resource_reference qa_app_remote_access_resolve_unified_resource(void *ctx, void *loaded,
    const qa_app_unified_resource_key *key);
qa_resolved_resource_reference qa_app_remote_access_world_resource(void *ctx, void *loaded);

/* The RemoteContentMounts fields the Q2 download receiver reads, as a view the
 * access keeps until it is released. */
const qa_app_q2_remote_mounts *qa_app_remote_access_q2_mounts(qa_app_remote_access *access,
    qa_app_remote_content_mounts *owner);
/* TS Q2RemotePresentation rebind of its download owner to a loaded world's catalog and mounts. */
const qa_app_q2_remote_mounts *qa_app_remote_access_rebind_q2_mounts(void *ctx, const qa_app_q2_remote_mounts *owner,
    void *loaded);

/* The plan mounts of content (catalog + mounts) in defaultOrder, as
 * Q3ApplicationPackages.open reads them. Malloc'd; *out_count rows. */
qa_app_q3_package_mount *qa_app_q3_package_mounts_of(const qa_content_catalog *catalog,
    const qa_content_mounted *mounts, size_t *out_count);
#endif
