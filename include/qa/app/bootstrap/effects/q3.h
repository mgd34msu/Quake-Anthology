/* Port of src/app/bootstrap/effects/q3.ts. ApplicationAssets
 * (../assets.ts) and the presentation ClientEffects /
 * LocalEntitySystem / ImpactMarkSystem / SceneModelRenderer
 * construction inside create() are host fn+ctx mirrors (their C
 * owners are sibling slices); the event routing, sound ring,
 * prepare/frame/reset bookkeeping of this file is ported here.
 * Async create/prepare are synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_EFFECTS_Q3_H
#define QA_APP_BOOTSTRAP_EFFECTS_Q3_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
/* TS SourceEffectSound. playback_kind: "once"|"actor"|"loop". */
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
/* TS Q3CharacterPresentationEvent fields read here (simulation/types.ts mirror). */
typedef struct qa_app_q3_character_presentation_event {
  double event; /* EntityEvent value */
  double time_milliseconds;
} qa_app_q3_character_presentation_event;
/* Host mirrors: ClientEffects / LocalEntitySystem / marks /
 * renderer / admission / operations (pres + assets owners). */
typedef struct qa_app_q3_effects_host {
  void *ctx;
  /* ClientEffects calls from event(). */
  void (*spawn_effect)(void *ctx, qa_vec3 origin);
  void (*smoke_puff)(void *ctx, qa_vec3 origin, qa_vec3 velocity, double radius,
                     qa_vec4 color, double duration, double start_time, double fade_in_time,
                     double flags, const char *shader_name);
  void (*gib_player)(void *ctx, qa_vec3 origin);
  /* prepare(): system.addEntities capture + admission snapshot +
   * model descriptor build + renderer.preload, host-side. */
  void (*prepare)(void *ctx, double time_milliseconds, double elapsed_milliseconds);
  /* frame(): builds operations; returns them + q3 lights (borrowed). */
  void (*frame)(void *ctx, qa_scene_camera camera, const void *source_order,
                bool has_viewer, qa_actor_id viewer, bool has_q1_fog, qa_scene_fog q1_fog,
                const void **out_admission, const void **out_operations, size_t *out_operation_count,
                const void **out_lights, size_t *out_light_count);
  /* resetRound() host parts: blood owners, pool, marks. */
  void (*clear_blood_owners)(void *ctx);
  void (*pool_initialize)(void *ctx);
  void (*marks_reset)(void *ctx);
} qa_app_q3_effects_host;
typedef struct qa_app_q3_application_effects qa_app_q3_application_effects;
/* TS Q3ApplicationEffects.create (synchronous). product is the TS
 * Product ("baseq3"|"missionpack") resolved from the catalog
 * campaign by the host; preload_character is the TS preload arg. */
qa_app_q3_application_effects *qa_app_q3_application_effects_create(
    qa_app_q3_effects_host host, qa_content_id content, const char *product);
void qa_app_q3_application_effects_destroy(qa_app_q3_application_effects *effects);
qa_content_id qa_app_q3_application_effects_content(const qa_app_q3_application_effects *effects);
double qa_app_q3_application_effects_time(const qa_app_q3_application_effects *effects);
const char *qa_app_q3_application_effects_product(const qa_app_q3_application_effects *effects);
/* TS event(): true when consumed. */
bool qa_app_q3_application_effects_event(qa_app_q3_application_effects *effects,
                                         qa_app_q3_character_presentation_event event, qa_vec3 origin);
/* Admits one SourceEffectSound (the create()-internal sourceSound
 * sink, exposed for the host's sound callbacks). */
void qa_app_q3_application_effects_admit_sound(qa_app_q3_application_effects *effects,
                                               qa_app_source_effect_sound sound);
void qa_app_q3_application_effects_prepare(qa_app_q3_application_effects *effects,
                                           double time_milliseconds, double elapsed_milliseconds);
void qa_app_q3_application_effects_frame(qa_app_q3_application_effects *effects,
    qa_scene_camera camera, const void *source_order, bool has_viewer, qa_actor_id viewer,
    bool has_q1_fog, qa_scene_fog q1_fog,
    const void **out_admission, const void **out_operations, size_t *out_operation_count,
    const void **out_lights, size_t *out_light_count);
/* TS drainSounds(): malloc'd array (caller frees the array). */
qa_app_source_effect_sound *qa_app_q3_application_effects_drain_sounds(
    qa_app_q3_application_effects *effects, size_t *out_count);
void qa_app_q3_application_effects_reset_round(qa_app_q3_application_effects *effects);
void qa_app_q3_application_effects_close(qa_app_q3_application_effects *effects);
#endif
