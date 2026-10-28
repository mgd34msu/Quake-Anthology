/* Port of src/app/bootstrap/precache.ts. SharedSimulation source
 * accessors (simulation/runtime.ts, other slice) are the host view
 * below; catalog resource lists are landed. GPL-2.0-or-later. */
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
/* TS nativeQ2MonsterResources. source NULL = TS undefined. Entity
 * spawn values are (key,value) text pairs for medic reinforcements. */
typedef struct qa_app_q2_precache_entity {
  const char *classname;
  const char *model, *model2, *model3, *model4, *noise, *sound;
  const char *spawn_text; /* borrowed spawn fields text, may be NULL */
} qa_app_q2_precache_entity;
qa_app_resource_requests qa_app_native_q2_monster_resources(qa_content_id content,
    const qa_monster_source_definition *source,
    const qa_app_q2_precache_entity *entities, size_t entity_count);
/* TS PrecacheSource view of SharedSimulation. */
typedef struct qa_app_precache_source {
  void *ctx;
  /* Each fills borrowed name arrays; false = TS null source. */
  bool (*q1_precaches)(void *ctx, const char *const **models, size_t *model_count,
      const char *const **sounds, size_t *sound_count, const void **game_out);
  bool (*quakec_precaches)(void *ctx, const char *const **models, size_t *model_count,
      const char *const **sounds, size_t *sound_count);
  bool (*q1_weapon_precaches)(void *ctx, const char *const **models, size_t *model_count,
      const char *const **sounds, size_t *sound_count, const void **game_out);
  bool (*q2_entities)(void *ctx, const qa_app_q2_precache_entity **entities, size_t *count,
      const char *const **item_paths, size_t *item_path_count, bool *rerelease_out,
      const char *const **skins, size_t *skin_count);
  /* q2RegisteredWeaponResources inputs live in the q2 source. */
  bool (*q2_weapon_paths)(void *ctx, bool rerelease, const char *const **paths, size_t *count);
} qa_app_precache_source;
qa_app_resource_requests qa_app_application_resource_requests(qa_app_precache_content content,
    const qa_app_precache_source *simulation);
typedef struct qa_app_prepare_resources_host {
  void *ctx;
  /* Borrowed error string or NULL. voice NULL = no voice argument. */
  const char *(*preload_sound)(void *ctx, qa_content_id content, const char *path, const char *voice);
  const char *(*preload_model)(void *ctx, qa_content_id content, const char *path);
  const char *(*preload_footsteps)(void *ctx);
  void (*progress)(void *ctx, const char *message);
  void (*print)(void *ctx, const char *message);
  /* q2 player skins for *-prefixed sounds. */
  size_t (*q2_skins)(void *ctx, const char *const **skins);
} qa_app_prepare_resources_host;
void qa_app_prepare_application_resources(qa_app_precache_content content,
    const qa_app_precache_source *simulation, const qa_app_prepare_resources_host *host);
#endif
