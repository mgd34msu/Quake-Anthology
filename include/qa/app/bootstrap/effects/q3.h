/* Port of src/app/bootstrap/effects/q3.ts: source cgame effect producers
 * (ClientEffects, LocalEntitySystem, ImpactMarkSystem) joined to the shared
 * assets, collision queries and drawing. Async create/prepare are
 * synchronous. GPL-2.0-or-later.
 *
 * C accommodations:
 *  - A TS SceneOperation list mixes submissions.ts SceneOperations and
 *    render/scene/models SceneModelGroups; C keeps those two types apart, so
 *    one entry is a qa_app_effect_operation.
 *  - Arrays the TS hands back as GC values (frame operations, batches,
 *    geometry) are owned by a qa_app_effect_storage released with the frame.
 *  - The TS ad-hoc cgame state {time, product, snap, predictedPlayerState} is
 *    the real qa_q3_pres_client_game_state (client 0, snapshot 0, snap NULL).
 *  - ClientEffects stores options.hardware; the TS getter re-reads the bound
 *    hardware, so it is refreshed before every call into the effect system. */
#ifndef QA_APP_BOOTSTRAP_EFFECTS_Q3_H
#define QA_APP_BOOTSTRAP_EFFECTS_Q3_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/presentation/local_entities.h"
#include "qa/content/q3/presentation/scene.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/materials/q3_lighting.h"
#include "qa/render/q3_hardware.h"
#include "qa/render/scene/models/renderer.h"
#include "qa/render/scene/models/types.h"
#include "qa/render/scene/submissions.h"

/* TS SourceEffectSound. playback_kind: "once"|"actor"|"loop"; path is
 * borrowed text that lives as long as the producing effects object. */
typedef struct qa_app_source_effect_sound {
  qa_content_id content;
  const char *path;
  qa_vec3 origin;
  double channel;
  double volume;
  double seconds;
  const char *playback_kind;
  qa_actor_id actor;     /* actor / loop */
  qa_vec3 velocity;      /* loop */
} qa_app_source_effect_sound;

/* Owned values released together (the GC lifetime of one TS frame). */
typedef struct qa_app_effect_storage_entry { void (*release)(void *value); void *value; } qa_app_effect_storage_entry;
typedef struct qa_app_effect_storage {
  qa_app_effect_storage_entry *entries; size_t count, capacity;
} qa_app_effect_storage;
/* Takes ownership of value; release(value) runs at qa_app_effect_storage_release. */
void qa_app_effect_storage_add(qa_app_effect_storage *storage, void (*release)(void *value), void *value);
/* Releases newest first and empties the storage. */
void qa_app_effect_storage_release(qa_app_effect_storage *storage);

/* One TS SceneOperation. model_group points into frame storage. */
typedef struct qa_app_effect_operation {
  bool is_model_group;
  qa_scene_operation scene;                /* !is_model_group */
  const qa_scene_model_group *model_group; /* is_model_group */
} qa_app_effect_operation;
typedef struct qa_app_effect_operations {
  qa_app_effect_operation *items; size_t count, capacity;
} qa_app_effect_operations;
void qa_app_effect_operations_push(qa_app_effect_operations *list, qa_app_effect_operation operation);
/* Appends every group of a renderer prepare result; the groups array is
 * taken into storage. */
void qa_app_effect_operations_push_model_groups(qa_app_effect_operations *list, qa_app_effect_storage *storage,
    qa_scene_model_group *groups, size_t count);

/* TS Q3ApplicationEffects.frame result. admission and q3_lights stay valid
 * until the next prepare/resetRound/close of the producing effects;
 * operations live in storage. */
typedef struct qa_app_q3_effect_frame {
  const qa_q3_pres_scene_admission *admission;
  qa_app_effect_operations operations;
  const qa_q3_dynamic_light *q3_lights; size_t q3_light_count;
  qa_app_effect_storage storage;
} qa_app_q3_effect_frame;
void qa_app_q3_effect_frame_destroy(qa_app_q3_effect_frame *frame);

typedef struct qa_app_q3_application_effects qa_app_q3_application_effects;
/* TS Q3ApplicationEffects.create. preload_character is the TS
 * preload === "character"; read_hardware NULL is the TS default ("generic").
 * Failures raise the TS Error through qa_raise (callers catch with qa_try). */
qa_app_q3_application_effects *qa_app_q3_application_effects_create(qa_app_application_assets *assets,
    qa_scene_queries queries, qa_content_id content, bool preload_character,
    qa_q3_hardware_read_fn read_hardware, void *read_hardware_ctx);
void qa_app_q3_application_effects_destroy(qa_app_q3_application_effects *effects);
qa_content_id qa_app_q3_application_effects_content(const qa_app_q3_application_effects *effects);
double qa_app_q3_application_effects_time(const qa_app_q3_application_effects *effects);
qa_q3_base_product qa_app_q3_application_effects_product(const qa_app_q3_application_effects *effects);
/* TS readonly members, for owners and tests. */
qa_scene_model_renderer *qa_app_q3_application_effects_renderer(qa_app_q3_application_effects *effects);
qa_q3_pres_local_entity_pool *qa_app_q3_application_effects_pool(qa_app_q3_application_effects *effects);
/* TS shaders map: false when name was never registered. */
bool qa_app_q3_application_effects_shader(const qa_app_q3_application_effects *effects, const char *name,
    qa_registered_scene_material *out);
size_t qa_app_q3_application_effects_shader_count(const qa_app_q3_application_effects *effects);
/* TS event(): true when consumed. */
bool qa_app_q3_application_effects_event(qa_app_q3_application_effects *effects,
    const qa_app_sim_q3_character_presentation_event *event, qa_vec3 origin);
void qa_app_q3_application_effects_prepare(qa_app_q3_application_effects *effects,
    double time_milliseconds, double elapsed_milliseconds);
/* TS frame(camera, source, viewer?, q1Fog?). q1_fog NULL is TS undefined. */
void qa_app_q3_application_effects_frame(qa_app_q3_application_effects *effects, qa_scene_camera camera,
    qa_source_scene_order *source, const qa_actor_id *viewer, const qa_scene_fog *q1_fog,
    qa_app_q3_effect_frame *out);
/* TS drainSounds(): malloc'd array (caller frees the array). */
qa_app_source_effect_sound *qa_app_q3_application_effects_drain_sounds(
    qa_app_q3_application_effects *effects, size_t *out_count);
void qa_app_q3_application_effects_reset_round(qa_app_q3_application_effects *effects);
void qa_app_q3_application_effects_close(qa_app_q3_application_effects *effects);
#endif
