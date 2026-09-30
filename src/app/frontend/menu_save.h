#ifndef QA_FRONTEND_MENU_SAVE_H
#define QA_FRONTEND_MENU_SAVE_H
#include "internal.h"
/* Genuine private Controls/Settings drafts. The stable seat and registered UI
 * factories retain their identities; no action or factory runs on decode. */
bool frontend_menu_checkpoint(const frontend_seat *, qa_buffer *, qa_error *);
bool frontend_menu_restore(frontend_seat *, qa_bytes, qa_error *);
#endif
