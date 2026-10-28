/* Port of src/app/bootstrap/audio/output-settings.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_AUDIO_OUTPUT_SETTINGS_H
#define QA_APP_BOOTSTRAP_AUDIO_OUTPUT_SETTINGS_H
#include <stddef.h>
#include "qa/audio/output.h"
#include "qa/core/cvars/index.h"
/* TS audioOutputCvarNames. */
extern const char *const qa_app_audio_output_cvar_names[];
extern const size_t qa_app_audio_output_cvar_name_count;
/* TS registerAudioOutputCvars (defaults = defaultAudioOutputFormat
 * when defaults is NULL). */
void qa_app_register_audio_output_cvars(qa_cvar_registry *cvars, const qa_audio_output_format *defaults);
/* TS readAudioOutputCvars / writeAudioOutputCvars. */
qa_audio_output_format qa_app_read_audio_output_cvars(qa_cvar_registry *cvars);
void qa_app_write_audio_output_cvars(qa_cvar_registry *cvars, qa_audio_output_format format);
#endif
