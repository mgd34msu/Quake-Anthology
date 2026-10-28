/* Port of src/app/bootstrap/startup-audio.ts. ApplicationMusic is
 * owned here (audio/music.h); playlist/output-settings/playlist-
 * settings are called directly. Engine construction stays a host
 * service (audio.ts owner). Async -> synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_AUDIO_H
#define QA_APP_BOOTSTRAP_STARTUP_AUDIO_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/audio/music.h"
#include "qa/app/bootstrap/audio/playlist_settings.h"
#include "qa/audio/engine.h"
#include "qa/audio/output.h"
#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
/* TS Partial<AudioPreferences> (audio-settings.ts mirror subset). */
typedef struct qa_app_startup_audio_preferences {
  bool has_output_format; qa_audio_output_format output_format;
  bool has_effects_volume; double effects_volume;
  bool has_music_volume; double music_volume;
  const char *menu_track; /* NULL absent */
} qa_app_startup_audio_preferences;
typedef struct qa_app_startup_audio_host {
  void *ctx;
  /* Engine/bank/music construction (audio.ts owners): fills engine. */
  qa_unified_audio *(*create_engine)(void *ctx, const qa_app_startup_audio_preferences *preferences);
  void (*destroy_engine)(void *ctx, qa_unified_audio *engine);
  qa_audio_output_format (*engine_output_format)(void *ctx, qa_unified_audio *engine);
  const void *(*engine_selected_output)(void *ctx, qa_unified_audio *engine);
  void (*engine_select_output)(void *ctx, qa_unified_audio *engine, const void *output, qa_audio_output_format format, bool restart);
  void (*engine_set_listeners)(void *ctx, qa_unified_audio *engine, qa_seat_id seat);
  void (*engine_set_effects_volume)(void *ctx, qa_unified_audio *engine, double volume);
  void (*engine_open_device)(void *ctx, qa_unified_audio *engine, const char *device_name, bool *out_failed, const char **out_error);
  void (*engine_play_ui)(void *ctx, qa_unified_audio *engine, const void *sound, const char *family, qa_seat_id seat);
  void (*engine_update_music)(void *ctx, qa_unified_audio *engine);
  void (*engine_pump)(void *ctx, qa_unified_audio *engine);
  void (*engine_close)(void *ctx, qa_unified_audio *engine);
  /* SoundBank register(menuSoundPath(family,event)): NULL = missing. */
  const void *(*bank_register)(void *ctx, const char *family, const char *event);
  void (*bank_clear)(void *ctx);
} qa_app_startup_audio_host;
typedef struct qa_app_startup_audio qa_app_startup_audio;
typedef struct qa_app_startup_audio_options {
  const char *family;             /* source.family */
  qa_app_music_source source;     /* TS options.source */
  qa_sound_bank *bank;            /* TS audio.bank */
  qa_content_mounted *mounts;     /* TS options.mounts */
  qa_music_controls *controls;    /* NULL = TS default controls */
  bool has_theme; const char *theme_family;
  qa_app_music_source theme_source;
  qa_sound_bank *theme_bank;      /* TS new SoundBank(theme.mounts) */
  qa_content_mounted *theme_mounts;
  qa_seat_id seat;
  void (*print)(void *ctx, const char *text); void *print_ctx;
  qa_app_startup_audio_preferences preferences;
} qa_app_startup_audio_options;
qa_app_startup_audio *qa_app_startup_audio_open(qa_app_startup_audio_host host, qa_app_startup_audio_options options);
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
void qa_app_startup_audio_sound(qa_app_startup_audio *a, const char *event);
void qa_app_startup_audio_pump(qa_app_startup_audio *a);
void qa_app_startup_audio_close(qa_app_startup_audio *a);
#endif
