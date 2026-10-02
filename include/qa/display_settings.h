#ifndef QA_DISPLAY_SETTINGS_H
#define QA_DISPLAY_SETTINGS_H

#include "qa/display.h"

typedef struct qa_display_surface_ticket qa_display_surface_ticket;
typedef struct qa_display_settings {
    uint32_t width, height;
    qa_display_fullscreen fullscreen;
    int swap_interval;
} qa_display_settings;
/* Borrowed native identities under the owner's retained dispatch boundary.
 * Reads and comparisons are pure; native readiness is checked separately. */
typedef struct qa_display_endpoint {
    const qa_display *owner;
    const void *lease, *window, *context, *renderer, *texture;
    const void *dispatch;
    uint64_t revision, native_revision;
    qa_display_backend backend;
} qa_display_endpoint;
bool qa_display_endpoint_read(const qa_display *, qa_display_endpoint *);
bool qa_display_endpoint_is(const qa_display *, const qa_display_endpoint *);

/* The active display must remain alive and idle until publish or abort.
 * A failed prepare or abort may retain a ticket requiring checked abort.
 * Destruction requested while a ticket is retained keeps the native parents
 * alive, excludes readiness/publication, and requires destruction retry after
 * checked abort releases their association. */
bool qa_display_surface_prepare(qa_display *active,
                                const qa_display_settings *settings,
                                qa_display_surface_ticket **out,
                                qa_error *error);
qa_display *qa_display_surface_candidate(const qa_display_surface_ticket *ticket);
/* Borrowed exact owner; use surface_ready to qualify its native state. */
const qa_display *qa_display_surface_active(const qa_display_surface_ticket *ticket);
/* Enter actual candidate native settings before preparing renderer/input
 * children. The old window and, for GL, the same context remain retained. */
bool qa_display_surface_stage(qa_display_surface_ticket *ticket, qa_error *error);
/* Checks genuine native readiness and retains its owner/resource receipt.
 * No native setters, allocation, or context switch occur here. */
bool qa_display_surface_ready(const qa_display_surface_ticket *ticket,
                              qa_error *error);
/* Pure witness of successful native readiness for these exact retained owners.
 * Native mutation, rollback or destruction requests invalidate the receipt. */
bool qa_display_surface_ready_is(const qa_display_surface_ticket *,
    const qa_display *active, const qa_display *candidate);
/* Restore the old native endpoint before renderer/input child rollback.
 * Keeps both windows alive; checked abort releases the candidate afterward. */
bool qa_display_surface_rollback(qa_display_surface_ticket *ticket, qa_error *error);
bool qa_display_surface_abort(qa_display_surface_ticket **ticket, qa_error *error);
/* Call only after all children and the surface are ready. Returns the old
 * window for retirement; performs only ownership and pointer transfers. */
void qa_display_surface_publish(qa_display_surface_ticket **ticket,
                                qa_display **active, qa_display **retired);

#endif
