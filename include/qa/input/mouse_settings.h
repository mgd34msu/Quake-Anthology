/*
 * Port of src/input/mouse-settings.ts: the seat registry owns
 * values; menus and profile restore use MouseInput.tuning.
 * qa_mouse_settings_store adapts the settings to the mouse
 * tuning-store shape. GPL-2.0-or-later.
 */
#ifndef QA_INPUT_MOUSE_SETTINGS_H
#define QA_INPUT_MOUSE_SETTINGS_H

#include "qa/core/cvars/index.h"
#include "qa/input/mouse.h"

typedef struct qa_mouse_settings {
  qa_cvar_registry *cvars;
} qa_mouse_settings;

/* Registers and documents the mouse cvars, as the TS constructor. */
void qa_mouse_settings_init(qa_mouse_settings *settings, qa_cvar_registry *cvars);
qa_mouse_tuning qa_mouse_settings_read(const qa_mouse_settings *settings);
void qa_mouse_settings_write(qa_mouse_settings *settings, qa_mouse_tuning value);
qa_mouse_tuning_store qa_mouse_settings_store(qa_mouse_settings *settings);

#endif
