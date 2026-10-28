/* Port of src/app/bootstrap/shared-setting-cvars.ts. Audio output /
 * music setting registration lives in app/bootstrap/audio (sibling
 * slice, unlanded): mirrored as optional host fn+ctx. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SHARED_SETTING_CVARS_H
#define QA_APP_BOOTSTRAP_SHARED_SETTING_CVARS_H
#include <stdint.h>
#include "qa/audio/output.h"
#include "qa/contracts/common.h"
#include "qa/core/cvars/index.h"
#include "qa/input/user_command.h"
/* Mirror: TS registerAudioOutputCvars/readAudioOutputCvars/registerMusicSettings
 * (src/app/bootstrap/audio/output-settings.ts, playlist-settings.ts). */
typedef struct qa_app_shared_audio_settings_host {
  void *ctx;
  void (*register_audio_output_cvars)(void *ctx, qa_cvar_registry *cvars, qa_audio_output_format format);
  qa_audio_output_format (*read_audio_output_cvars)(void *ctx, qa_cvar_registry *cvars);
  void (*register_music_settings)(void *ctx, qa_cvar_registry *cvars);
} qa_app_shared_audio_settings_host;
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
void qa_app_register_shared_client_settings(qa_cvar_registry *cvars, qa_audio_output_format output_format, qa_app_shared_audio_settings_host host);
void qa_app_apply_audio_output_settings(qa_cvar_registry *cvars, qa_app_audio_output_target audio, qa_app_shared_audio_settings_host host);
#endif
