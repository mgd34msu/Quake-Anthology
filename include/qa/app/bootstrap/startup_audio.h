/* Port of src/app/bootstrap/startup-audio.ts over the UnifiedAudio engine,
 * SoundBank and ApplicationMusic. Async -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_AUDIO_H
#define QA_APP_BOOTSTRAP_STARTUP_AUDIO_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/audio/music.h"
#include "qa/app/bootstrap/audio/playlist_settings.h"
#include "qa/audio/bank.h"
#include "qa/audio/engine.h"
#include "qa/audio/output.h"
#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
#include "qa/ui/common/controller.h"
/* TS Partial<AudioPreferences> as StartupAudio.open reads it. */
typedef struct qa_app_startup_audio_preferences {
  bool has_output_format; qa_audio_output_format output_format;
  bool has_effects_volume; double effects_volume;
  bool has_music_volume; double music_volume;
  const char *menu_track; /* NULL absent */
} qa_app_startup_audio_preferences;
typedef struct qa_app_startup_audio qa_app_startup_audio;
/* TS StartupAudio.open options. Mounts are borrowed for the audio's life;
 * the audio owns its sound banks (TS new SoundBank(mounts)). */
typedef struct qa_app_startup_audio_options {
  qa_music_controls *music_controls; /* NULL = TS undefined */
  qa_content_mounted *mounts;
  qa_app_music_source source;
  bool has_theme;                    /* false = TS theme: null */
  qa_content_mounted *theme_mounts;
  qa_app_music_source theme_source;
  qa_seat_id seat;
  void (*print)(void *ctx, const char *text); void *print_ctx;
  qa_app_startup_audio_preferences preferences;
} qa_app_startup_audio_options;
/* TS static open(options). A failure closes the audio and raises. */
qa_app_startup_audio *qa_app_startup_audio_open(const qa_app_startup_audio_options *options);
void qa_app_startup_audio_destroy(qa_app_startup_audio *a);
qa_unified_audio *qa_app_startup_audio_engine(qa_app_startup_audio *a);
qa_audio_output_format qa_app_startup_audio_output_format(qa_app_startup_audio *a);
void qa_app_startup_audio_bind_output_cvars(qa_app_startup_audio *a, qa_cvar_registry *cvars);
void qa_app_startup_audio_select_output_format(qa_app_startup_audio *a, qa_audio_output_format format);
void qa_app_startup_audio_restart_output(qa_app_startup_audio *a);
const char *const *qa_app_startup_audio_music_tracks(qa_app_startup_audio *a, size_t *out_count);
/* menu_track owned (caller frees), as qa_app_read_music_settings. */
qa_app_audio_music_preferences qa_app_startup_audio_music_preferences(qa_app_startup_audio *a);
void qa_app_startup_audio_open_output(qa_app_startup_audio *a, const char *device_name, void (*print)(void *ctx, const char *text), void *print_ctx);
void qa_app_startup_audio_cd_command(qa_app_startup_audio *a, const char *const *args, size_t count, void (*print)(void *ctx, const char *text), void *print_ctx);
void qa_app_startup_audio_queue_cd_command(qa_app_startup_audio *a, const char *const *args, size_t count, void (*print)(void *ctx, const char *text), void *print_ctx);
void qa_app_startup_audio_queue_music_command(qa_app_startup_audio *a, const char *const *args, size_t count, void (*print)(void *ctx, const char *text), void *print_ctx);
void qa_app_startup_audio_flush_commands(qa_app_startup_audio *a);
void qa_app_startup_audio_set_volumes(qa_app_startup_audio *a, double effects, double music);
void qa_app_startup_audio_sound(qa_app_startup_audio *a, qa_ui_sound event);
void qa_app_startup_audio_pump(qa_app_startup_audio *a);
void qa_app_startup_audio_close(qa_app_startup_audio *a);
#endif
