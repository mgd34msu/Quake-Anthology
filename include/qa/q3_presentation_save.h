#ifndef QA_Q3_PRESENTATION_SAVE_H
#define QA_Q3_PRESENTATION_SAVE_H
#include "qa/q3_presentation.h"
/* Retained scene/parser state only. Asset handles must already be restored in
 * the existing candidate registry, and its selected world/entity bytes bound.
 * Movies and their prepared source cache are separate media owners. Restore
 * keeps the installed presentation address and dispatches no source callback. */
bool qa_q3_presentation_scene_checkpoint(const qa_q3_presentation *, qa_buffer *, qa_error *);
bool qa_q3_presentation_scene_restore(qa_q3_presentation *, qa_bytes, qa_error *);
#endif
