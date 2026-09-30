#ifndef QA_UI_ACCOUNT_SAVE_H
#define QA_UI_ACCOUNT_SAVE_H
#include "qa/ui.h"
/* Idle private edit continuation on the actual registered menu/controller.
 * No account request, field reset, open/close hook or provider replay. */
bool qa_ui_rankings_checkpoint(const qa_ui_rankings *, qa_buffer *, qa_error *);
bool qa_ui_rankings_restore(qa_ui_rankings *, qa_bytes, qa_error *);
#endif
