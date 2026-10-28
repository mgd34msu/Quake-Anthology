/* Port of src/app/bootstrap/component-media.ts. PresentationState /
 * audio / asset operations are host callbacks over the event view
 * below; chronology, acknowledgement and shader-drain rules are kept
 * here. Async -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_COMPONENT_MEDIA_H
#define QA_APP_BOOTSTRAP_COMPONENT_MEDIA_H
#include <stdbool.h>
#include <stddef.h>
/* SimulationPresentationEvent fields read here. event_kind is the
 * nested event kind for q1-level/q2/local-media events. */
typedef struct qa_app_media_event {
  const char *kind;
  const char *event_kind;
  double sequence;
  const char *original, *replacement; /* shader-remap */
  double time_offset;
  const char *content;
} qa_app_media_event;
typedef struct qa_app_media_seat_events {
  double seat;
  const qa_app_media_event *events; size_t event_count;
  bool music;
} qa_app_media_seat_events;
typedef struct qa_app_component_media_host {
  void *ctx;
  /* PresentationState surface. */
  const qa_app_media_event *(*pending_local_media)(void *ctx, size_t *out_count);
  double (*applied_media_sequence)(void *ctx);
  bool (*local_media_current)(void *ctx, const qa_app_media_event *request);
  void (*acknowledge_local_media)(void *ctx, const qa_app_media_event *request, bool committed);
  void (*acknowledge_local_media_default)(void *ctx, const qa_app_media_event *request);
  void (*applied_media)(void *ctx, double sequence);
  void (*applied_shader)(void *ctx, const char *original, double sequence);
  const qa_app_media_event *(*resolve_shader_replay)(void *ctx, const qa_app_media_event *pending);
  void (*enable_local_media)(void *ctx);
  void (*publish_local_media)(void *ctx, const qa_app_media_event *request, bool initializing);
  /* Audio / assets surface; error or NULL. */
  const char *(*play_component_media)(void *ctx, const qa_app_media_event *request);
  const char *(*audio_receive)(void *ctx, const qa_app_media_event *events, size_t count, bool has_seat, double seat, bool music);
  /* Returns "committed"|"stale" (world remapShader with provider pick). */
  const char *(*remap_shader)(void *ctx, const qa_app_media_event *request);
} qa_app_component_media_host;
bool qa_app_presentation_audio_control(const qa_app_media_event *source);
const char *qa_app_prepare_presentation_audio(qa_app_component_media_host host,
                                              const qa_app_media_event *source, size_t source_count,
                                              const qa_app_media_seat_events *seats, size_t seat_count);
const char *qa_app_prepare_presentation_shaders(qa_app_component_media_host host);
/* TS primaryShaderControl / componentMediaControl factories become
 * direct calls with the same current() gating (host supplies it). */
const char *qa_app_primary_shader_control(qa_app_component_media_host host, const char *original, const char *replacement,
                                          const char *offset, bool initializing, bool current, const char *content);
const char *qa_app_component_media_control(qa_app_component_media_host host, const qa_app_media_event *request, bool initializing, bool current);
#endif
