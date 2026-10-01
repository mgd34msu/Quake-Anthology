#ifndef QA_UI_PRESENTATION_PREPARE_H
#define QA_UI_PRESENTATION_PREPARE_H
#include "qa/ui.h"

typedef struct qa_ui_presentation_ticket qa_ui_presentation_ticket;
/* A presentation ticket borrows the actual controller and its font owner.
 * Font resources and both fallback spans remain stable through termination;
 * the published desired span keeps the controller's normal borrowed lifetime.
 * Preparation changes no presentation state and runs no callbacks. */
bool qa_ui_presentation_prepare(qa_ui *,const qa_font_selection *,float,
    qa_ui_color_mode,qa_ui_presentation_ticket **,qa_error *);
bool qa_ui_presentation_ready(const qa_ui_presentation_ticket *,qa_error *);
/* Consumes an admitted ticket using only the prepared presentation copy. */
void qa_ui_presentation_publish(qa_ui_presentation_ticket *);
/* Checked abort leaves the actual controller presentation unchanged. */
bool qa_ui_presentation_abort(qa_ui_presentation_ticket *,qa_error *);
/* Lifetime/capture admission is separate from returned UI callbacks. */
bool qa_ui_presentation_idle(const qa_ui *);
#endif
