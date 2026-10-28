/* Port of src/app/bootstrap/audio/playlist-settings.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_AUDIO_PLAYLIST_SETTINGS_H
#define QA_APP_BOOTSTRAP_AUDIO_PLAYLIST_SETTINGS_H
#include <stdbool.h>
#include "qa/core/cvars/index.h"
/* TS MusicPreferences. menu_track is malloc'd in _read results
 * (caller frees); the startup_audio.h qa_app_music_preferences
 * mirror of this type is superseded by this header. */
typedef struct qa_app_audio_music_preferences {
  bool music_shuffle;
  char *menu_track; /* owned when returned by read */
} qa_app_audio_music_preferences;
/* TS validMenuTrack. */
bool qa_app_valid_menu_track(const char *value);
/* TS registerMusicSettings. */
void qa_app_register_music_settings(qa_cvar_registry *cvars);
/* TS readMusicSettings (cvars NULL = TS null). */
qa_app_audio_music_preferences qa_app_read_music_settings(qa_cvar_registry *cvars);
#endif
