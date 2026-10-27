/* Port of src/ui/settings/language.ts. GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_LANGUAGE_H
#define QA_UI_SETTINGS_LANGUAGE_H

#include <stdint.h>

#include "qa/core/cvars/index.h"

void qa_ui_register_language_settings(qa_cvar_registry *cvars);
/* Malloc'd value (or "english"); NULL only on OOM. */
char *qa_ui_read_seat_language(qa_cvar_registry *cvars, int64_t seat);
/* Aborts (TS Error) unless /^[a-z]+$/. */
void qa_ui_write_seat_language(qa_cvar_registry *cvars, int64_t seat,
                               const char *language);

#endif
