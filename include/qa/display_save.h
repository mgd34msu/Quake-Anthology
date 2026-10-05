#ifndef QA_DISPLAY_SAVE_H
#define QA_DISPLAY_SAVE_H
#include "qa/display.h"
typedef struct qa_display_restore_guard qa_display_restore_guard;
/* Fresh frontend construction retains the actual completed native display cut
 * and window lease without creating another window or publishing a frame. */
bool qa_display_create_detached(qa_display *,qa_display **,qa_display_restore_guard **,qa_error *);
/* Pure imported display recipe under the actual retained native alias. */
bool qa_display_restore_info(const qa_display_restore_guard *,const qa_display *,qa_display_info *);
bool qa_display_handoff_prepare(qa_display_restore_guard *,qa_error *);
bool qa_display_handoff_ready(const qa_display_restore_guard *,qa_error *);
/* A failed import returns the original native settings and presentation before
 * releasing either retained display owner. Failure requires a cleanup retry. */
bool qa_display_handoff_abort(qa_display_restore_guard *,qa_error *);
/* Retry a refused import rollback retained by this actual display owner. */
bool qa_display_restore_cleanup(qa_display *,qa_error *);
/* Moves ownership of the qualified native window/context without allocation.
 * CPU presentation is already copied to the native backbuffer by prepare. */
void qa_display_handoff(qa_display_restore_guard *);
void qa_display_restore_guard_destroy(qa_display_restore_guard *);
#endif
