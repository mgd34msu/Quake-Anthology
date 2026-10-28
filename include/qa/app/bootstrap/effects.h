/* Port of src/app/bootstrap/effects.ts. Per-family effect engines
 * (effects/q1|q2|q3 in sibling slices) drain through the host; the
 * unhandled/sound queues and reset/close bookkeeping are kept here.
 * SourceEffectSound stays with its effects/q3 owner.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_EFFECTS_H
#define QA_APP_BOOTSTRAP_EFFECTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/render.h"
/* TS UnhandledApplicationEffect (source kept as borrowed event token). */
typedef struct qa_app_unhandled_effect { const void *source; char *reason; } qa_app_unhandled_effect;
/* TS ApplicationEffectFrame fields flattened. */
typedef struct qa_app_effect_frame {
  const void *entities; size_t entity_count;
  const void *lights; size_t light_count;
} qa_app_effect_frame;
typedef struct qa_app_application_effects qa_app_application_effects;
typedef struct qa_app_effects_host {
  void *ctx;
  bool (*is_player)(void *ctx, qa_actor_id actor);
  /* Family engines consume events; returns unhandled reason or NULL
   * per event through the reasons array (malloc'd entries or NULL). */
  void (*receive)(void *ctx, const void *events, size_t count, const char **reasons);
  size_t (*drain_sounds)(void *ctx, const void **out);
  const char *(*preload_transient)(void *ctx, char **out_errors_json);
  const char *(*preload_model)(void *ctx, qa_content_id content, const char *path);
  const char *(*prepare)(void *ctx, const void *snapshot);
  qa_app_effect_frame (*frame)(void *ctx, qa_scene_camera camera, qa_actor_id viewer, bool has_viewer);
  const void *(*shadow_lights)(void *ctx, qa_scene_camera camera, size_t *out_count);
  void (*bind_hardware)(void *ctx);
  void (*reset_round)(void *ctx);
  void (*close_effects)(void *ctx);
} qa_app_effects_host;
qa_app_application_effects *qa_app_application_effects_create(qa_app_effects_host host, const void *assets, const void *queries, double seed);
void qa_app_application_effects_destroy(qa_app_application_effects *effects);
void qa_app_application_effects_bind_renderer_hardware(qa_app_application_effects *effects);
void qa_app_application_effects_receive(qa_app_application_effects *effects, const void *const *events, size_t count);
/* Owned rows; free each reason + array. */
qa_app_unhandled_effect *qa_app_application_effects_drain_unhandled(qa_app_application_effects *effects, size_t *out_count);
size_t qa_app_application_effects_drain_sounds(qa_app_application_effects *effects, const void **out);
const char *qa_app_application_effects_preload_transient_resources(qa_app_application_effects *effects, char **out_errors_json);
const char *qa_app_application_effects_preload_model(qa_app_application_effects *effects, qa_content_id content, const char *path);
const char *qa_app_application_effects_prepare(qa_app_application_effects *effects, const void *snapshot);
qa_app_effect_frame qa_app_application_effects_frame(qa_app_application_effects *effects, qa_scene_camera camera, bool has_viewer, qa_actor_id viewer);
const void *qa_app_application_effects_shadow_scene_lights(qa_app_application_effects *effects, qa_scene_camera camera, size_t *out_count);
void qa_app_application_effects_reset_round(qa_app_application_effects *effects);
void qa_app_application_effects_close(qa_app_application_effects *effects);
#endif
