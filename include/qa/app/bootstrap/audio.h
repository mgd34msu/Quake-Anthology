/* Port of src/app/bootstrap/audio.ts. The unified engine, sound
 * banks, playlists and per-seat presentation audio are landed/host
 * services wired below; volume cvar mirroring, option storage and
 * output selection guards are kept here. Async -> error strings.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_AUDIO_H
#define QA_APP_BOOTSTRAP_AUDIO_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/audio/playlist_settings.h"
#include "qa/app/bootstrap/content.h"
#include "qa/audio/output.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
/* TS ApplicationAudioCommand / ApplicationAudioOptions subsets. */
typedef struct qa_app_audio_command_request {
  const char *name;
  const char *const *args; size_t arg_count;
  bool has_seat; qa_seat_id seat;
} qa_app_audio_command_request;
typedef struct qa_app_audio_options {
  bool has_output_format; qa_audio_output_format output_format;
  bool defer_output;
  const char *device_name; /* NULL = absent/null */
  bool has_effects_volume; double effects_volume;
  bool has_music_volume; double music_volume;
} qa_app_audio_options;
typedef struct qa_app_application_audio qa_app_application_audio;
typedef struct qa_app_audio_host {
  void *ctx;
  bool (*q3_team_game)(void *ctx);
  /* Engine surface. */
  qa_audio_output_format (*output_format)(void *ctx);
  void (*select_output_format)(void *ctx, qa_audio_output_format format);
  void (*restart_output)(void *ctx);
  const char *(*selected_output)(void *ctx);
  const char *const *(*output_device_names)(void *ctx, size_t *out_count);
  void (*select_output)(void *ctx, const char *device_name);
  void (*detach_output)(void *ctx);
  void (*set_effects_volume)(void *ctx, double value);
  void (*set_music_volume)(void *ctx, double value);
  void (*bind_output_cvars)(void *ctx, qa_cvar_registry *cvars);
  void (*prepare_output_transfer)(void *ctx, qa_app_application_audio *next);
  /* Presentation/audio work; error text or NULL. */
  const char *(*prepare_environment)(void *ctx, const void *scene);
  const char *(*command)(void *ctx, qa_app_audio_command_request request, bool *handled);
  const char *(*preload_sound)(void *ctx, qa_content_id content, const char *path, const char *selected_model);
  const char *(*preload_footsteps)(void *ctx);
  const char *(*play_music)(void *ctx, qa_content_id content, const char *track, bool has_owner, const void *owner);
  const char *(*play_component_media)(void *ctx, const void *request);
  const char *(*start_world_music)(void *ctx);
  const char *(*receive)(void *ctx, const void *events, size_t count, bool seat_audience, qa_seat_id seat, bool music);
  const char *(*frame)(void *ctx, const void *snapshot, const void *events, size_t count, double frame_started_at);
  void (*ui_sound)(void *ctx, const char *sound, qa_seat_id seat);
  void (*reset_round)(void *ctx);
  void (*close_audio)(void *ctx);
} qa_app_audio_host;
qa_app_application_audio *qa_app_application_audio_create(qa_app_loaded_content *content, qa_app_audio_host host, qa_app_audio_options options);
void qa_app_application_audio_destroy(qa_app_application_audio *audio);
void qa_app_application_audio_bind_volume_cvars(qa_app_application_audio *audio, qa_cvar_registry *cvars);
void qa_app_application_audio_bind_output_cvars(qa_app_application_audio *audio, qa_cvar_registry *cvars);
qa_app_audio_music_preferences qa_app_application_audio_music_preferences(qa_app_application_audio *audio);
double qa_app_application_audio_effects_volume(const qa_app_application_audio *audio);
void qa_app_application_audio_set_effects_volume(qa_app_application_audio *audio, double value);
double qa_app_application_audio_music_volume_value(const qa_app_application_audio *audio);
void qa_app_application_audio_set_music_volume(qa_app_application_audio *audio, double value);
qa_audio_output_format qa_app_application_audio_output_format(const qa_app_application_audio *audio);
void qa_app_application_audio_select_output_format(qa_app_application_audio *audio, qa_audio_output_format format);
void qa_app_application_audio_restart_output(qa_app_application_audio *audio);
const char *qa_app_application_audio_selected_output(const qa_app_application_audio *audio);
const char *const *qa_app_application_audio_output_device_names(qa_app_application_audio *audio, size_t *out_count);
void qa_app_application_audio_select_output(qa_app_application_audio *audio, const char *device_name);
void qa_app_application_audio_detach_output(qa_app_application_audio *audio);
void qa_app_application_audio_prepare_output_transfer(qa_app_application_audio *audio, qa_app_application_audio *next);
void qa_app_application_audio_ui_sound(qa_app_application_audio *audio, const char *sound, qa_seat_id seat);
const char *qa_app_application_audio_prepare_environment(qa_app_application_audio *audio, const void *scene);
const char *qa_app_application_audio_command(qa_app_application_audio *audio, qa_app_audio_command_request request, bool *handled);
const char *qa_app_application_audio_preload_sound(qa_app_application_audio *audio, qa_content_id content, const char *path, const char *selected_model);
const char *qa_app_application_audio_preload_character_footsteps(qa_app_application_audio *audio);
const char *qa_app_application_audio_play_music(qa_app_application_audio *audio, qa_content_id content, const char *track, bool has_owner, const void *owner);
const char *qa_app_application_audio_play_component_media(qa_app_application_audio *audio, const void *request);
const char *qa_app_application_audio_start_world_music(qa_app_application_audio *audio);
const char *qa_app_application_audio_receive(qa_app_application_audio *audio, const void *events, size_t count, bool seat_audience, qa_seat_id seat, bool music);
const char *qa_app_application_audio_frame(qa_app_application_audio *audio, const void *snapshot, const void *events, size_t count, double frame_started_at);
void qa_app_application_audio_reset_round(qa_app_application_audio *audio);
void qa_app_application_audio_close(qa_app_application_audio *audio);
#endif
