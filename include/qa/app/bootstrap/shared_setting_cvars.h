/* Port of src/app/bootstrap/shared-setting-cvars.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SHARED_SETTING_CVARS_H
#define QA_APP_BOOTSTRAP_SHARED_SETTING_CVARS_H
#include <stdint.h>
#include "qa/audio/output.h"
#include "qa/contracts/common.h"
#include "qa/core/cvars/index.h"
#include "qa/input/user_command.h"
typedef struct qa_app_audio_output_target {
  void *ctx;
  qa_audio_output_format (*output_format)(void *ctx);
  void (*select_output_format)(void *ctx, qa_audio_output_format format);
} qa_app_audio_output_target;
/* Returns static error string or NULL (TS string|null). */
const char *qa_app_validate_field_of_view(const char *text);
void qa_app_register_run_cvar(qa_cvar_registry *cvars, qa_command_dialect movement_dialect);
/* Returns binding handle to unbind (TS returned remove closure). */
int64_t qa_app_bind_run_cvar(qa_cvar_registry *cvars, qa_input_command_builder *builder);
/* output_format NULL is the TS default (defaultAudioOutputFormat). */
void qa_app_register_shared_client_settings(qa_cvar_registry *cvars, const qa_audio_output_format *output_format);
void qa_app_apply_audio_output_settings(qa_cvar_registry *cvars, qa_app_audio_output_target audio);
#endif
