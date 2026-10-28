/* Port of src/app/bootstrap/audio-settings.ts. Partial<> becomes
 * has_* flags. Async store I/O is synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_AUDIO_SETTINGS_H
#define QA_APP_BOOTSTRAP_AUDIO_SETTINGS_H
#include <stdbool.h>
#include "qa/audio/output.h"
#include "qa/settings/config.h"
/* TS AudioPreferences (Partial<MusicPreferences> + required fields).
 * device_name/menu_track owned in load results; free them. */
typedef struct qa_app_audio_preferences {
  bool has_music_shuffle, music_shuffle;
  bool has_menu_track; char *menu_track;
  char *device_name; /* NULL = TS null */
  qa_audio_output_format output_format;
  double effects_volume, music_volume;
} qa_app_audio_preferences;
/* TS saveAudioSettings audio argument. has_music_preferences false
 * loads saved music preferences first. */
typedef struct qa_app_audio_settings_input {
  bool has_music_preferences, music_shuffle;
  const char *menu_track; /* required when has_music_preferences */
  bool has_output_format; qa_audio_output_format output_format;
  const char *selected_output; /* NULL = TS null */
  double effects_volume, music_volume;
} qa_app_audio_settings_input;
/* False = TS {} (absent or non-JSON document). */
bool qa_app_load_audio_settings(const qa_config_store *store, qa_app_audio_preferences *out);
void qa_app_save_audio_settings(const qa_config_store *store, qa_app_audio_settings_input audio);
#endif
