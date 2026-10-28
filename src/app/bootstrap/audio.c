/* Port of src/app/bootstrap/audio.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
struct qa_app_application_audio {
  qa_app_loaded_content *content;
  qa_app_audio_host host;
  qa_cvar_registry *volume_cvars, *output_cvars;
  double volume, music_volume;
  bool closed;
};
static double clamp01(double value) { return fmax(0, fmin(1, value)); }
qa_app_application_audio *qa_app_application_audio_create(qa_app_loaded_content *content, qa_app_audio_host host, qa_app_audio_options options) {
  qa_app_application_audio *audio = calloc(1, sizeof *audio);
  if (!audio) return NULL;
  audio->content = content;
  audio->host = host;
  audio->volume = options.has_effects_volume ? options.effects_volume : 1;
  audio->music_volume = options.has_music_volume ? options.music_volume : 1;
  if (options.has_output_format) host.select_output_format(host.ctx, options.output_format);
  if (options.device_name) host.select_output(host.ctx, options.device_name);
  host.set_effects_volume(host.ctx, audio->volume);
  host.set_music_volume(host.ctx, audio->music_volume);
  return audio;
}
void qa_app_application_audio_destroy(qa_app_application_audio *audio) {
  if (!audio) return;
  qa_app_application_audio_close(audio);
  free(audio);
}
void qa_app_application_audio_bind_volume_cvars(qa_app_application_audio *audio, qa_cvar_registry *cvars) { audio->volume_cvars = cvars; }
void qa_app_application_audio_bind_output_cvars(qa_app_application_audio *audio, qa_cvar_registry *cvars) {
  audio->output_cvars = cvars;
  audio->host.bind_output_cvars(audio->host.ctx, cvars);
}
qa_app_audio_music_preferences qa_app_application_audio_music_preferences(qa_app_application_audio *audio) {
  return qa_app_read_music_settings(audio->volume_cvars);
}
double qa_app_application_audio_effects_volume(const qa_app_application_audio *audio) {
  return audio->volume_cvars == NULL ? audio->volume
      : clamp01(qa_cvar_registry_variable_value((qa_cvar_registry *)audio->volume_cvars, "volume"));
}
static void set_cvar_number(qa_cvar_registry *cvars, const char *name, double value) {
  if (!cvars) return;
  char text[64];
  snprintf(text, sizeof text, "%.17g", value);
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, name, text, false, &snapshot)) qa_cvar_read_free(&snapshot);
}
void qa_app_application_audio_set_effects_volume(qa_app_application_audio *audio, double value) {
  set_cvar_number(audio->volume_cvars, "volume", value);
  audio->host.set_effects_volume(audio->host.ctx, value);
  audio->volume = value;
}
double qa_app_application_audio_music_volume_value(const qa_app_application_audio *audio) {
  return audio->volume_cvars == NULL ? audio->music_volume
      : clamp01(qa_cvar_registry_variable_value((qa_cvar_registry *)audio->volume_cvars, "bgmvolume"));
}
void qa_app_application_audio_set_music_volume(qa_app_application_audio *audio, double value) {
  set_cvar_number(audio->volume_cvars, "bgmvolume", value);
  audio->host.set_music_volume(audio->host.ctx, value);
  audio->music_volume = value;
}
qa_audio_output_format qa_app_application_audio_output_format(const qa_app_application_audio *audio) { return audio->host.output_format(audio->host.ctx); }
void qa_app_application_audio_select_output_format(qa_app_application_audio *audio, qa_audio_output_format format) { audio->host.select_output_format(audio->host.ctx, format); }
void qa_app_application_audio_restart_output(qa_app_application_audio *audio) { audio->host.restart_output(audio->host.ctx); }
const char *qa_app_application_audio_selected_output(const qa_app_application_audio *audio) { return audio->host.selected_output(audio->host.ctx); }
const char *const *qa_app_application_audio_output_device_names(qa_app_application_audio *audio, size_t *out_count) { return audio->host.output_device_names(audio->host.ctx, out_count); }
void qa_app_application_audio_select_output(qa_app_application_audio *audio, const char *device_name) { audio->host.select_output(audio->host.ctx, device_name); }
void qa_app_application_audio_detach_output(qa_app_application_audio *audio) { audio->host.detach_output(audio->host.ctx); }
void qa_app_application_audio_prepare_output_transfer(qa_app_application_audio *audio, qa_app_application_audio *next) { audio->host.prepare_output_transfer(audio->host.ctx, next); }
void qa_app_application_audio_ui_sound(qa_app_application_audio *audio, const char *sound, qa_seat_id seat) { audio->host.ui_sound(audio->host.ctx, sound, seat); }
const char *qa_app_application_audio_prepare_environment(qa_app_application_audio *audio, const void *scene) { return audio->host.prepare_environment(audio->host.ctx, scene); }
const char *qa_app_application_audio_command(qa_app_application_audio *audio, qa_app_audio_command_request request, bool *handled) { return audio->host.command(audio->host.ctx, request, handled); }
const char *qa_app_application_audio_preload_sound(qa_app_application_audio *audio, qa_content_id content, const char *path, const char *selected_model) { return audio->host.preload_sound(audio->host.ctx, content, path, selected_model); }
const char *qa_app_application_audio_preload_character_footsteps(qa_app_application_audio *audio) { return audio->host.preload_footsteps(audio->host.ctx); }
const char *qa_app_application_audio_play_music(qa_app_application_audio *audio, qa_content_id content, const char *track, bool has_owner, const void *owner) { return audio->host.play_music(audio->host.ctx, content, track, has_owner, owner); }
const char *qa_app_application_audio_play_component_media(qa_app_application_audio *audio, const void *request) { return audio->host.play_component_media(audio->host.ctx, request); }
const char *qa_app_application_audio_start_world_music(qa_app_application_audio *audio) { return audio->host.start_world_music(audio->host.ctx); }
const char *qa_app_application_audio_receive(qa_app_application_audio *audio, const void *events, size_t count, bool seat_audience, qa_seat_id seat, bool music) { return audio->host.receive(audio->host.ctx, events, count, seat_audience, seat, music); }
const char *qa_app_application_audio_frame(qa_app_application_audio *audio, const void *snapshot, const void *events, size_t count, double frame_started_at) { return audio->host.frame(audio->host.ctx, snapshot, events, count, frame_started_at); }
void qa_app_application_audio_reset_round(qa_app_application_audio *audio) { audio->host.reset_round(audio->host.ctx); }
void qa_app_application_audio_close(qa_app_application_audio *audio) {
  if (!audio || audio->closed) return;
  audio->closed = true;
  audio->host.close_audio(audio->host.ctx);
}
