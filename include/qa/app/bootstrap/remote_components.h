/* Port of src/app/bootstrap/remote-components.ts. ApplicationModPresentations
 * / component-media / presentation types (sibling bootstrap slices,
 * unlanded) are mirrored as host fn+ctx. Async -> synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_COMPONENTS_H
#define QA_APP_BOOTSTRAP_REMOTE_COMPONENTS_H
#include <stdbool.h>
#include <stddef.h>
/* Mirrors: ComponentClientCommandRequest / SimulationPresentationEvent /
 * WorldSeatPresentation stay opaque borrowed pointers. */
typedef struct qa_app_remote_component_host {
  void *ctx;
  bool (*clients_pending_commands)(void *ctx);
  void (*clients_dispatch_command)(void *ctx, const void *request);
  void (*clients_prepare)(void *ctx, const void *presentation, const void *const *events, size_t event_count, double frame);
  void (*clients_close)(void *ctx);
  void (*prepare_audio)(void *ctx, const void *const *events, size_t event_count);
  void (*prepare_shaders)(void *ctx);
  /* TS event.kind === "presentation-owner" test for the music=false filter. */
  bool (*event_is_presentation_owner)(void *ctx, const void *event);
} qa_app_remote_component_host;
typedef struct qa_app_remote_component_view qa_app_remote_component_view;
qa_app_remote_component_view *qa_app_remote_component_view_create(qa_app_remote_component_host host);
void qa_app_remote_component_view_destroy(qa_app_remote_component_view *v);
/* TS queueCommand closure handed to ApplicationModPresentations. */
void qa_app_remote_component_view_queue_command(qa_app_remote_component_view *v, const void *request);
bool qa_app_remote_component_view_pending_commands(qa_app_remote_component_view *v);
void qa_app_remote_component_view_prepare_media(qa_app_remote_component_view *v, const void *const *events, size_t event_count, bool music);
void qa_app_remote_component_view_prepare(qa_app_remote_component_view *v, const void *presentation, const void *const *events, size_t event_count, double frame);
void qa_app_remote_component_view_close(qa_app_remote_component_view *v);
#endif
