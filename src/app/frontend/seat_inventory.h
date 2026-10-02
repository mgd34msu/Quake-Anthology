#ifndef QA_FRONTEND_SEAT_INVENTORY_H
#define QA_FRONTEND_SEAT_INVENTORY_H
#include "internal.h"
#include "scene_identity.h"

/* These are the actual seat sections of the INPUT and PRESENTATION records.
 * Capture retains the outer frontend/content lease. Candidate import requires
 * restored fonts, images, menus and tools at their genuine stable addresses;
 * input handlers are imported before controller tokens. No item observation,
 * menu lifecycle, console dispatch or source callback runs. */
bool frontend_seats_checkpoint(qa_frontend *, frontend_scene_namespace *,
    qa_buffer *input, qa_buffer *presentation, qa_error *);
bool frontend_seats_restore(qa_frontend *, frontend_scene_namespace *,
    qa_bytes input, qa_bytes presentation, qa_error *);
bool frontend_seats_restore_finish(qa_frontend *, qa_error *);
#endif
