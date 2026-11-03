/* Port of src/app/bootstrap/effects.ts: application joins for Quake
 * cl_tent/r_part and Quake II cl_tent/cl_fx. A world owns one event stream;
 * preparation advances it once and seat frames only sample it.
 * Copyright (C) 1996-2005 Id Software, Inc. GPL-2.0-or-later.
 *
 * C accommodations:
 *  - Async prepare/preload are synchronous.
 *  - Received events are retained until prepare, as TS retains the objects:
 *    each is an owned deep copy (qa_app_sim_presentation_event_copy). Text the
 *    effects keep past that point (content, sound paths, flashlight hand) is
 *    interned for the life of the root effects object.
 *  - Frame results own their arrays in a qa_app_effect_storage (q3.h). */
#ifndef QA_APP_BOOTSTRAP_EFFECTS_H
#define QA_APP_BOOTSTRAP_EFFECTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/effects/q2_view.h"
#include "qa/app/bootstrap/effects/q3.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q3/foundation/presentation.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/session.h"
#include "qa/materials/lighting.h"
#include "qa/materials/q3_lighting.h"
#include "qa/render/q3_hardware.h"
#include "qa/render/scene/submissions.h"

/* TS UnhandledApplicationEffect. source is an owned copy of the event
 * (qa_app_sim_presentation_event_copy); the row frees it. */
typedef struct qa_app_unhandled_effect {
  qa_app_sim_presentation_event source;
  char *reason;
} qa_app_unhandled_effect;
void qa_app_unhandled_effects_free(qa_app_unhandled_effect *rows, size_t count);

/* TS ApplicationEffectFrame. q3_admissions borrow the Q3 owners' admissions
 * (valid until their next prepare); the rest lives in storage. */
typedef struct qa_app_application_effect_frame {
  const qa_q3_pres_scene_admission **q3_admissions; size_t q3_admission_count;
  qa_app_effect_operations operations;
  qa_surface_dynamic_light *lights; size_t light_count;
  qa_q3_dynamic_light *q3_lights; size_t q3_light_count;
  qa_app_effect_storage storage;
} qa_app_application_effect_frame;
void qa_app_application_effect_frame_destroy(qa_app_application_effect_frame *frame);

/* One drainRecipientSounds row: sounds is malloc'd (caller frees). */
typedef struct qa_app_recipient_effect_sounds {
  qa_actor_id recipient;
  qa_app_source_effect_sound *sounds; size_t sound_count;
} qa_app_recipient_effect_sounds;

/* TS preloadTransientResources failure row (text malloc'd). */
typedef struct qa_app_effect_preload_failure { qa_content_id content; char *path; char *error; } qa_app_effect_preload_failure;
void qa_app_effect_preload_failures_free(qa_app_effect_preload_failure *rows, size_t count);

/* TS isPlayer constructor argument. */
typedef bool (*qa_app_effects_is_player_fn)(void *ctx, qa_actor_id actor);

typedef struct qa_app_application_effects qa_app_application_effects;
/* TS new ApplicationEffects(assets, queries, isPlayer, seed = 1). assets is
 * borrowed and must outlive the effects. */
qa_app_application_effects *qa_app_application_effects_create(qa_app_application_assets *assets,
    qa_scene_queries queries, qa_app_effects_is_player_fn is_player, void *is_player_ctx, double seed);
void qa_app_application_effects_destroy(qa_app_application_effects *effects);
qa_app_application_assets *qa_app_application_effects_assets(const qa_app_application_effects *effects);
/* TS readonly queries. */
qa_scene_queries qa_app_application_effects_queries(const qa_app_application_effects *effects);
void qa_app_application_effects_bind_renderer_hardware(qa_app_application_effects *effects,
    qa_q3_hardware_read_fn read, void *ctx);
void qa_app_application_effects_receive(qa_app_application_effects *effects,
    const qa_app_sim_presentation_event *events, size_t count);
/* Owned rows: free with qa_app_unhandled_effects_free. */
qa_app_unhandled_effect *qa_app_application_effects_drain_unhandled(qa_app_application_effects *effects, size_t *out_count);
/* Owned rows: free each sounds array, then the rows. */
qa_app_recipient_effect_sounds *qa_app_application_effects_drain_recipient_sounds(qa_app_application_effects *effects,
    size_t *out_count);
/* TS drainSounds(): malloc'd array (caller frees). */
qa_app_source_effect_sound *qa_app_application_effects_drain_sounds(qa_app_application_effects *effects, size_t *out_count);
qa_app_q2_effect_player_view qa_app_application_effects_player_view(qa_app_application_effects *effects,
    qa_actor_id actor, qa_scene_camera camera);
/* Owned rows: free with qa_app_effect_preload_failures_free. */
qa_app_effect_preload_failure *qa_app_application_effects_preload_transient_resources(
    qa_app_application_effects *effects, size_t *out_count);
void qa_app_application_effects_preload_model(qa_app_application_effects *effects, qa_content_id content, const char *path);
void qa_app_application_effects_prepare(qa_app_application_effects *effects, const qa_world_snapshot *snapshot,
    const qa_app_simulation_presentation *presentations, size_t presentation_count,
    const qa_q3_character_view *characters, size_t character_count);
/* TS frame(camera, source, viewer = null, q1Fog?). viewer/q1_fog NULL are TS
 * null/undefined. */
void qa_app_application_effects_frame(qa_app_application_effects *effects, qa_scene_camera camera,
    qa_source_scene_order *source, const qa_actor_id *viewer, const qa_scene_fog *q1_fog,
    qa_app_application_effect_frame *out);
/* TS shadowSceneLights(camera, style, viewer = null): malloc'd array. */
qa_scene_light *qa_app_application_effects_shadow_scene_lights(qa_app_application_effects *effects,
    qa_scene_camera camera, double (*style)(void *ctx, double index), void *style_ctx,
    const qa_actor_id *viewer, size_t *out_count);
void qa_app_application_effects_reset_round(qa_app_application_effects *effects);
void qa_app_application_effects_close(qa_app_application_effects *effects);
/* Q3 effect owner for content, or NULL (TS private q3 map, for owners/tests). */
qa_app_q3_application_effects *qa_app_application_effects_q3(qa_app_application_effects *effects, qa_content_id content);
#endif
