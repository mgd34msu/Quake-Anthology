/* Port of src/app/bootstrap/component-media.ts. The TS functions take a
 * PresentationState, an ApplicationAudio pick and an ApplicationAssets pick;
 * here they are the members of qa_app_component_media_host, typed as TS
 * types them. Async -> synchronous: a TS rejection is a returned error
 * (borrowed, NULL on success). TS closures are fn+ctx pairs.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_COMPONENT_MEDIA_H
#define QA_APP_BOOTSTRAP_COMPONENT_MEDIA_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/presentation_state.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/audio/types.h"
#include "qa/contracts/presentation.h"
#include "qa/render/scene/shaders.h"
#include "qa/render/scene/world.h"

struct qa_active_mod_presentation;

/* TS () => boolean (the scene's current callback type). */
typedef qa_scene_current_fn qa_app_media_current_fn;

typedef struct qa_app_component_media_host {
  void *ctx;
  /* PresentationState. pending_local_media returns the retained requests
   * sorted by sequence (pointers into the state's own storage; each stays
   * valid until that request is acknowledged or replaced). Request
   * identity is pointer identity, as TS compares objects. */
  const qa_app_retained_presentation *const *(*pending_local_media)(void *ctx, size_t *out_count);
  double (*applied_media_sequence)(void *ctx);
  bool (*local_media_current)(void *ctx, const qa_app_retained_presentation *request);
  void (*acknowledge_local_media)(void *ctx, const qa_app_retained_presentation *request, bool committed);
  void (*applied_media)(void *ctx, double sequence);
  void (*applied_shader)(void *ctx, const char *original, double sequence);
  /* TS resolveShaderReplay(LocalPresentationMedia): LocalPresentationMedia
   * | null. Takes and returns local requests (local set); NULL is TS null. */
  const qa_app_retained_presentation *(*resolve_shader_replay)(void *ctx, const qa_app_retained_presentation *request);
  void (*enable_local_media)(void *ctx);
  /* owner NULL is TS undefined; current NULL is TS undefined. The state
   * keeps current with the delivery (TS closure lifetime) and calls
   * release_current(current_ctx), when not NULL, once it drops that
   * delivery; with release_current NULL the caller keeps current_ctx alive
   * while the request is pending. event's strings are copied. */
  void (*publish_local_media)(void *ctx, const qa_presentation_owner *owner, qa_content_id content,
                              const qa_component_presentation_media_request *event, bool initializing,
                              qa_app_media_current_fn current, void *current_ctx, void (*release_current)(void *current_ctx));
  /* ApplicationAudio.playComponentMedia(source, active): source.event is
   * never a shader remap. */
  const char *(*play_component_media)(void *ctx, const qa_app_local_presentation_media *source,
                                      qa_app_media_current_fn active, void *active_ctx);
  /* ApplicationAudio.receive(events, audience, music). */
  const char *(*audio_receive)(void *ctx, const qa_app_sim_presentation_event *events, size_t count,
                               qa_audio_audience audience, bool music);
  /* The ApplicationAssets pick: assets.world, and
   * (await assets.provider(content)).shaders, which returns NULL with
   * *out_error set where the TS promise rejects. */
  qa_world_scene *(*world)(void *ctx);
  qa_scene_shader_registry *(*provider_shaders)(void *ctx, qa_content_id content, const char **out_error);
} qa_app_component_media_host;

bool qa_app_presentation_audio_control(const qa_app_sim_presentation_event *source);
/* TS preparePresentationAudio(events, audio, source = [], seats = []). */
const char *qa_app_prepare_presentation_audio(qa_app_component_media_host host,
                                              const qa_app_sim_presentation_event *source, size_t source_count,
                                              const qa_app_audio_seat_events *seats, size_t seat_count);
const char *qa_app_prepare_presentation_shaders(qa_app_component_media_host host);
/* TS primaryShaderControl(events, assets, sourceCurrent) returns a closure;
 * qa_app_primary_shader_control_enable is the factory's eager
 * events.enableLocalMedia() and qa_app_primary_shader_control the closure
 * call. content is assets.content.recipe.engineBehavior.content. */
void qa_app_primary_shader_control_enable(qa_app_component_media_host host);
const char *qa_app_primary_shader_control(qa_app_component_media_host host, qa_content_id content,
                                          qa_app_media_current_fn source_current, void *source_ctx,
                                          const char *original, const char *replacement, const char *offset,
                                          bool initializing, qa_app_media_current_fn consumer_current, void *consumer_ctx);
/* TS componentMediaControl(events, audio, assets): the factory's eager
 * events.enableLocalMedia() and the closure call. */
void qa_app_component_media_control_enable(qa_app_component_media_host host);
const char *qa_app_component_media_control(qa_app_component_media_host host, const struct qa_active_mod_presentation *source,
                                           const qa_component_presentation_media_request *request, bool initializing,
                                           qa_app_media_current_fn current, void *current_ctx);
#endif
