/* Port of src/app/bootstrap/audio-settings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/audio_settings.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/playlist_settings.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
static char *dup(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
/* TS preferences(unknown) over a parsed JSON value; raises the TS Error. */
static qa_app_audio_preferences preferences(const qa_save_value *value) {
  qa_app_audio_preferences out = {0};
  out.output_format = qa_default_audio_output_format;
  if (!value || value->kind != QA_SAVE_OBJECT) qa_raise("Error", 0, 0, "Invalid audio preferences");
  const qa_save_value *version = qa_save_value_get(value, "version");
  const qa_save_value *device = qa_save_value_get(value, "deviceName");
  const qa_save_value *effects = qa_save_value_get(value, "effectsVolume");
  const qa_save_value *music = qa_save_value_get(value, "musicVolume");
  bool device_ok = device && (device->kind == QA_SAVE_NULL ||
      (device->kind == QA_SAVE_STRING && device->text[0] != '\0'));
  if (!version || version->kind != QA_SAVE_NUMBER || version->number != 1 || !device_ok ||
      !effects || effects->kind != QA_SAVE_NUMBER || !isfinite(effects->number) || effects->number < 0 || effects->number > 1 ||
      !music || music->kind != QA_SAVE_NUMBER || !isfinite(music->number) || music->number < 0 || music->number > 1)
    qa_raise("Error", 0, 0, "Invalid audio preferences");
  const qa_save_value *shuffle = qa_save_value_get(value, "musicShuffle");
  const qa_save_value *track = qa_save_value_get(value, "menuTrack");
  bool shuffle_value = shuffle && shuffle->kind == QA_SAVE_BOOLEAN ? shuffle->boolean : false;
  const char *track_value = track && track->kind == QA_SAVE_STRING ? track->text : "auto";
  if ((shuffle && shuffle->kind != QA_SAVE_BOOLEAN) || (track && track->kind != QA_SAVE_STRING) || !qa_app_valid_menu_track(track_value))
    qa_raise("Error", 0, 0, "Invalid music preferences");
  const qa_save_value *format = qa_save_value_get(value, "outputFormat");
  if (format) {
    /* TS audioOutputFormat(unknown): its RangeError reaches loadAudioSettings' catch. */
    const qa_save_value *rate = format->kind == QA_SAVE_OBJECT ? qa_save_value_get(format, "sampleRate") : NULL;
    const qa_save_value *channels = format->kind == QA_SAVE_OBJECT ? qa_save_value_get(format, "channels") : NULL;
    const qa_save_value *bits = format->kind == QA_SAVE_OBJECT ? qa_save_value_get(format, "sampleBits") : NULL;
    if (!rate || rate->kind != QA_SAVE_NUMBER || !(fabs(rate->number) <= 9007199254740991.0) || rate->number != trunc(rate->number) ||
        rate->number < 8000 || rate->number > 192000 || !channels || channels->kind != QA_SAVE_NUMBER ||
        (channels->number != 1 && channels->number != 2) || !bits || bits->kind != QA_SAVE_NUMBER ||
        (bits->number != 8 && bits->number != 16))
      qa_raise("RangeError", 0, 0, "Audio output requires 8000\xe2\x80\x93" "192000 Hz, 1 or 2 channels, and 8 or 16 bits");
    out.output_format.sample_rate = (int64_t)rate->number;
    out.output_format.channels = (int64_t)channels->number;
    out.output_format.sample_bits = (int64_t)bits->number;
    out.has_output_format = true;
  }
  if (shuffle) { out.has_music_shuffle = true; out.music_shuffle = shuffle_value; }
  if (track) { out.has_menu_track = true; out.menu_track = dup(track_value); }
  out.device_name = device->kind == QA_SAVE_NULL ? NULL : dup(device->text);
  out.effects_volume = effects->number;
  out.music_volume = music->number;
  return out;
}
typedef struct preferences_call { const qa_save_value *value; qa_app_audio_preferences out; } preferences_call;
static void preferences_body(void *ctx) {
  preferences_call *call = ctx;
  call->out = preferences(call->value);
}
bool qa_app_load_audio_settings(const qa_config_store *store, qa_app_audio_preferences *out) {
  return qa_app_load_audio_settings_warn(store, out, NULL, NULL);
}
bool qa_app_load_audio_settings_warn(const qa_config_store *store, qa_app_audio_preferences *out,
    void (*warn)(void *ctx, const char *text), void *warn_ctx) {
  size_t length = 0;
  char *text = qa_config_store_load_text(store, "audio.json", &length);
  if (!text) return false;
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, length, true);
  free(text);
  if (!value) return false;
  /* A hand-edited or foreign file falls back to defaults instead of stopping the launch
   * (TS warn defaults to a no-op). */
  preferences_call call = {value, {0}};
  qa_caught_error caught;
  bool valid = qa_try(preferences_body, &call, &caught);
  qa_save_value_free(value);
  if (!valid) {
    if (warn != NULL) {
      char text[QA_CAUGHT_MESSAGE_CAPACITY + 32];
      snprintf(text, sizeof text, "Ignoring audio.json: %s\n", caught.message);
      warn(warn_ctx, text);
    }
    return false;
  }
  *out = call.out;
  return true;
}
static void append_json_string(char *buf, size_t cap, size_t *len, const char *text) {
  *len += (size_t)snprintf(buf + *len, cap - *len, "\"");
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    unsigned char c = *p;
    if (c == '"' || c == '\\') *len += (size_t)snprintf(buf + *len, cap - *len, "\\%c", c);
    else if (c < 0x20) *len += (size_t)snprintf(buf + *len, cap - *len, "\\u%04x", c);
    else *len += (size_t)snprintf(buf + *len, cap - *len, "%c", c);
  }
  *len += (size_t)snprintf(buf + *len, cap - *len, "\"");
}
void qa_app_save_audio_settings(const qa_config_store *store, qa_app_audio_settings_input audio) {
  bool shuffle = false;
  const char *track = "auto";
  qa_app_audio_preferences saved = {0};
  bool have_saved = false;
  if (audio.has_music_preferences) { shuffle = audio.music_shuffle; track = audio.menu_track; }
  else {
    have_saved = qa_app_load_audio_settings(store, &saved);
    if (have_saved) {
      shuffle = saved.has_music_shuffle ? saved.music_shuffle : false;
      track = saved.has_menu_track ? saved.menu_track : "auto";
    }
  }
  /* preferences({...}) validation, mirrored on the constructed fields. */
  if (!qa_app_valid_menu_track(track)) qa_raise("Error", 0, 0, "Invalid music preferences");
  if ((audio.selected_output && audio.selected_output[0] == '\0') ||
      !isfinite(audio.effects_volume) || audio.effects_volume < 0 || audio.effects_volume > 1 ||
      !isfinite(audio.music_volume) || audio.music_volume < 0 || audio.music_volume > 1)
    qa_raise("Error", 0, 0, "Invalid audio preferences");
  qa_audio_output_format format = audio.has_output_format ? audio.output_format : qa_default_audio_output_format;
  format = qa_audio_output_format_validated(format.sample_rate, format.channels, format.sample_bits);
  char buf[4096]; size_t len = 0;
  len += (size_t)snprintf(buf + len, sizeof buf - len, "{\"version\":1,\"musicShuffle\":%s,\"menuTrack\":", shuffle ? "true" : "false");
  append_json_string(buf, sizeof buf, &len, track);
  len += (size_t)snprintf(buf + len, sizeof buf - len, ",\"outputFormat\":{\"sampleRate\":%lld,\"channels\":%lld,\"sampleBits\":%lld},\"deviceName\":",
      (long long)format.sample_rate, (long long)format.channels, (long long)format.sample_bits);
  if (audio.selected_output) append_json_string(buf, sizeof buf, &len, audio.selected_output);
  else len += (size_t)snprintf(buf + len, sizeof buf - len, "null");
  len += (size_t)snprintf(buf + len, sizeof buf - len, ",\"effectsVolume\":%s,\"musicVolume\":%s}\n", QA_JS_NUMBER(audio.effects_volume), QA_JS_NUMBER(audio.music_volume));
  free(saved.menu_track);
  free(saved.device_name);
  if (!qa_config_store_dump(store, "audio.json", buf)) qa_raise("Error", 0, 0, "Could not write audio.json");
}
/* TS saveAudioSettings(store, audio) with an ApplicationAudio: its musicPreferences getter is always
 * defined, so the stored music settings never come from the document. */
void qa_app_save_audio_settings_from(const qa_config_store *store, qa_app_application_audio *audio) {
  qa_app_audio_music_preferences music = qa_app_application_audio_music_preferences(audio);
  qa_app_audio_settings_input input;
  memset(&input, 0, sizeof input);
  input.has_music_preferences = true; input.music_shuffle = music.music_shuffle; input.menu_track = music.menu_track;
  input.has_output_format = true; input.output_format = qa_app_application_audio_output_format(audio);
  input.selected_output = qa_app_application_audio_selected_output(audio);
  input.effects_volume = qa_app_application_audio_effects_volume(audio);
  input.music_volume = qa_app_application_audio_music_volume(audio);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, music.menu_track);
  qa_app_save_audio_settings(store, input);
  qa_cleanup_pop(&cleanup);
  free(music.menu_track);
}
