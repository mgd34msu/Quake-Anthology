#ifndef QA_FRONTEND_SELECTED_CHARACTER_LIFETIME_H
#define QA_FRONTEND_SELECTED_CHARACTER_LIFETIME_H
#include "selected_character.h"
/* Discard stale published declarations only after every actual frame output
 * has returned. Same-publication poses and registries retain continuation. */
bool frontend_selected_character_refresh(qa_frontend *, qa_error *);
#endif
