/* Port of src/app/bootstrap/remote-components.ts (RemoteComponentView): a remote seat runs the same
 * ApplicationModPresentations collection and retained component media as a local seat, over the unified
 * replica's PresentationState (remote.media) and its component sources.
 *
 * C accommodations:
 *  - TS async members are synchronous; a TS rejection raises.
 *  - The options are TS ApplicationModPresentationsOptions; the view replaces presentation_media and
 *    queue_command with its own (TS `{ ...options, presentationMedia, queueCommand }`), so its ctx becomes the
 *    view's and the caller's print / next_frame / system_cinematics are forwarded with the caller's ctx.
 *  - Queued command requests are deep copies (TS keeps the request objects) released after dispatch.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_COMPONENTS_H
#define QA_APP_BOOTSTRAP_REMOTE_COMPONENTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/mod_presentations.h"
#include "qa/app/bootstrap/network/remote_unified.h"
#include "qa/app/bootstrap/presentation.h"
#include "qa/app/bootstrap/simulation/types.h"

typedef struct qa_app_remote_component_view qa_app_remote_component_view;
/* new RemoteComponentView(remote, options): options and remote are borrowed and outlive the view. */
qa_app_remote_component_view *qa_app_remote_component_view_create(qa_app_unified_remote *remote,
    const qa_app_mod_presentations_options *options);
/* Closes (TS close()) and frees. */
void qa_app_remote_component_view_destroy(qa_app_remote_component_view *view);
/* TS get pendingCommands. */
bool qa_app_remote_component_view_pending_commands(qa_app_remote_component_view *view);
/* TS prepareMedia(events, music). */
void qa_app_remote_component_view_prepare_media(qa_app_remote_component_view *view,
    const qa_app_sim_presentation_event *events, size_t event_count, bool music);
/* TS prepare(presentation, events, frame). */
void qa_app_remote_component_view_prepare(qa_app_remote_component_view *view, qa_app_world_seat_presentation *presentation,
    const qa_app_sim_presentation_event *events, size_t event_count, double frame);
void qa_app_remote_component_view_close(qa_app_remote_component_view *view);
#endif
