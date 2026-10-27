/*
 * Port of src/input/device-settings.ts: MIDI and source-input
 * cvar declarations. The TS reads process.platform for the
 * joystick profile default; C uses the build target.
 * GPL-2.0-or-later.
 */
#ifndef QA_INPUT_DEVICE_SETTINGS_H
#define QA_INPUT_DEVICE_SETTINGS_H

#include <stddef.h>

#include "qa/core/cvars/index.h"

#define QA_INPUT_DEVICE_CVAR_NAME_COUNT 15
extern const char *const qa_input_device_cvar_names[QA_INPUT_DEVICE_CVAR_NAME_COUNT];

void qa_register_midi_settings(qa_cvar_registry *cvars);
void qa_register_source_input_settings(qa_cvar_registry *cvars);
void qa_register_input_device_cvars(qa_cvar_registry *cvars);

#endif
