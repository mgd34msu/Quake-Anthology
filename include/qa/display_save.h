#ifndef QA_DISPLAY_SAVE_H
#define QA_DISPLAY_SAVE_H
#include "qa/display.h"
typedef struct qa_display_restore_guard qa_display_restore_guard;
bool qa_display_checkpoint(qa_display *,qa_buffer *,qa_error *);
/* Distinct heap owner borrowing the same qualified native window/context.
 * Saved presentation storage stays detached until final native preparation. */
bool qa_display_restore(qa_bytes,const qa_display *,qa_display **,qa_display_restore_guard **,qa_error *);
/* Fresh frontend construction retains the actual completed native display cut
 * and window lease without creating another window or publishing a frame. */
bool qa_display_create_detached(qa_display *,qa_display **,qa_display_restore_guard **,qa_error *);
bool qa_display_restore_checkpoint(const qa_display_restore_guard *,qa_buffer *,qa_error *);
bool qa_display_handoff_prepare(qa_display_restore_guard *,qa_error *);
bool qa_display_handoff_ready(const qa_display_restore_guard *,qa_error *);
/* Moves ownership of the qualified native window/context without allocation.
 * CPU presentation is already copied to the native backbuffer by prepare. */
void qa_display_handoff(qa_display_restore_guard *);
void qa_display_restore_guard_destroy(qa_display_restore_guard *);
#endif
