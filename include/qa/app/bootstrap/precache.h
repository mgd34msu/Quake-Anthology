/* Port of src/app/bootstrap/precache.ts over the real SharedSimulation sources (TS PrecacheSource is a Pick
 * of SharedSimulation), ApplicationAudio and ApplicationEffects. TS async preloads are synchronous; a
 * failed preload is a raise caught here (TS catch). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PRECACHE_H
#define QA_APP_BOOTSTRAP_PRECACHE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/catalog/equipment.h"
#include "qa/content/catalog/index.h"
#include "qa/content/monsters/definitions.h"
#include "qa/contracts/content.h"
/* TS ResourceContent. */
typedef struct qa_app_precache_content {
  const qa_content_catalog *catalog;
  const qa_executable_recipe *recipe;
} qa_app_precache_content;
/* Owned resource request list (paths owned). */
typedef struct qa_app_resource_requests {
  qa_resource_request *items; size_t count;
  char **owned_paths; size_t owned_count;
  qa_catalog_resource_list backing[4]; size_t backing_count;
} qa_app_resource_requests;
void qa_app_resource_requests_destroy(qa_app_resource_requests *requests);
qa_app_resource_requests qa_app_character_resource_requests(qa_app_precache_content content);
/* TS nativeQ2MonsterResources(content, source, entities). source NULL = TS undefined. */
struct qa_q2_entity;
qa_app_resource_requests qa_app_native_q2_monster_resources(qa_content_id content,
    const qa_monster_source_definition *source, struct qa_q2_entity *const *entities, size_t entity_count);
struct qa_app_shared_simulation;
struct qa_app_application_audio;
struct qa_app_application_effects;
/* TS applicationResourceRequests(content, simulation). */
qa_app_resource_requests qa_app_application_resource_requests(qa_app_precache_content content,
    struct qa_app_shared_simulation *simulation);
/* TS prepareApplicationResources({ content, simulation, audio, effects, progress, print }). */
void qa_app_prepare_application_resources(qa_app_precache_content content, struct qa_app_shared_simulation *simulation,
    struct qa_app_application_audio *audio, struct qa_app_application_effects *effects,
    void (*progress)(void *ctx, const char *message), void *progress_ctx,
    void (*print)(void *ctx, const char *message), void *print_ctx);
struct qa_app_application_assets;
struct qa_application_loading;
/* Application's call (TS application.ts): prepareApplicationResources({ content: assets.content, simulation,
 * audio, effects, progress: message => host.loading?.stage(message), print }). loading NULL = TS undefined. */
void qa_app_prepare_world_resources(struct qa_app_application_assets *assets, struct qa_app_shared_simulation *simulation,
    struct qa_app_application_audio *audio, struct qa_app_application_effects *effects, const struct qa_application_loading *loading,
    void (*print)(void *ctx, const char *text), void *print_ctx);
#endif
