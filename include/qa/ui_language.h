#ifndef QA_UI_LANGUAGE_H
#define QA_UI_LANGUAGE_H
#include "qa/console.h"
bool qa_ui_language_name(uint32_t physical_seat, char out[64], qa_error *);
/* The result borrows the actual ENGINE cvar until its registry mutates. */
bool qa_ui_language_read(const qa_cvars *, uint32_t physical_seat, const char **, qa_error *);
#endif
