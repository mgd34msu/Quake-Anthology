#ifndef QA_DISPLAY_SETTINGS_H
#define QA_DISPLAY_SETTINGS_H

#include "qa/display.h"

typedef struct qa_display_surface_ticket qa_display_surface_ticket;
typedef struct qa_display_settings {
    uint32_t width, height;
    qa_display_fullscreen fullscreen;
    int swap_interval;
} qa_display_settings;

/* The active display must remain alive and idle until publish or abort.
 * A failed prepare or abort may retain a ticket requiring checked abort. */
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
/* Pure validation: no native setters, allocation, or context switch. */
bool qa_display_surface_ready(const qa_display_surface_ticket *ticket,
                              qa_error *error);
/* Restore the old native endpoint before renderer/input child rollback.
 * Keeps both windows alive; checked abort releases the candidate afterward. */
bool qa_display_surface_rollback(qa_display_surface_ticket *ticket, qa_error *error);
bool qa_display_surface_abort(qa_display_surface_ticket **ticket, qa_error *error);
/* Call only after all children and the surface are ready. Returns the old
 * window for retirement; performs only ownership and pointer transfers. */
void qa_display_surface_publish(qa_display_surface_ticket **ticket,
                                qa_display **active, qa_display **retired);

#endif
