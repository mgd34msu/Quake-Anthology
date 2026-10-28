/* Port of src/app/bootstrap/audio-settings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/audio_settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/playlist_settings.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
static char *dup(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
/* TS preferences(unknown) over a parsed JSON value; aborts exactly. */
static qa_app_audio_preferences preferences(const qa_save_value *value) {
  qa_app_audio_preferences out = {0};
  out.output_format = qa_default_audio_output_format;
  if (!value || value->kind != QA_SAVE_OBJECT) qa_range_error("Invalid audio preferences");
  const qa_save_value *version = qa_save_value_get(value, "version");
  const qa_save_value *device = qa_save_value_get(value, "deviceName");
  const qa_save_value *effects = qa_save_value_get(value, "effectsVolume");
  const qa_save_value *music = qa_save_value_get(value, "musicVolume");
  bool device_ok = device && (device->kind == QA_SAVE_NULL ||
      (device->kind == QA_SAVE_STRING && device->text[0] != '\0'));
  if (!version || version->kind != QA_SAVE_NUMBER || version->number != 1 || !device_ok ||
      !effects || effects->kind != QA_SAVE_NUMBER || !isfinite(effects->number) || effects->number < 0 || effects->number > 1 ||
      !music || music->kind != QA_SAVE_NUMBER || !isfinite(music->number) || music->number < 0 || music->number > 1)
    qa_range_error("Invalid audio preferences");
  const qa_save_value *shuffle = qa_save_value_get(value, "musicShuffle");
  const qa_save_value *track = qa_save_value_get(value, "menuTrack");
  bool shuffle_value = shuffle && shuffle->kind == QA_SAVE_BOOLEAN ? shuffle->boolean : false;
  const char *track_value = track && track->kind == QA_SAVE_STRING ? track->text : "auto";
  if ((shuffle && shuffle->kind != QA_SAVE_BOOLEAN) || (track && track->kind != QA_SAVE_STRING) || !qa_app_valid_menu_track(track_value))
    qa_range_error("Invalid music preferences");
  if (shuffle) { out.has_music_shuffle = true; out.music_shuffle = shuffle_value; }
  if (track) { out.has_menu_track = true; out.menu_track = dup(track_value); }
  const qa_save_value *format = qa_save_value_get(value, "outputFormat");
  if (format) {
    /* audioOutputFormat(unknown) landed as field validation. */
    const qa_save_value *rate = qa_save_value_get(format, "sampleRate");
    const qa_save_value *channels = qa_save_value_get(format, "channels");
    const qa_save_value *bits = qa_save_value_get(format, "sampleBits");
    out.output_format = qa_audio_output_format_validated(
        rate && rate->kind == QA_SAVE_NUMBER ? (int64_t)rate->number : 0,
        channels && channels->kind == QA_SAVE_NUMBER ? (int64_t)channels->number : 0,
        bits && bits->kind == QA_SAVE_NUMBER ? (int64_t)bits->number : 0);
  }
  out.device_name = device->kind == QA_SAVE_NULL ? NULL : dup(device->text);
  out.effects_volume = effects->number;
  out.music_volume = music->number;
  return out;
}
bool qa_app_load_audio_settings(const qa_config_store *store, qa_app_audio_preferences *out) {
  size_t length = 0;
  char *text = qa_config_store_load_text(store, "audio.json", &length);
  if (!text) return false;
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, length, true);
  free(text);
  if (!value) return false;
  *out = preferences(value);
  qa_save_value_free(value);
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
  if (!qa_app_valid_menu_track(track)) qa_range_error("Invalid music preferences");
  if ((audio.selected_output && audio.selected_output[0] == '\0') ||
      !isfinite(audio.effects_volume) || audio.effects_volume < 0 || audio.effects_volume > 1 ||
      !isfinite(audio.music_volume) || audio.music_volume < 0 || audio.music_volume > 1)
    qa_range_error("Invalid audio preferences");
  qa_audio_output_format format = audio.has_output_format ? audio.output_format : qa_default_audio_output_format;
  format = qa_audio_output_format_validated(format.sample_rate, format.channels, format.sample_bits);
  char buf[4096]; size_t len = 0;
  len += (size_t)snprintf(buf + len, sizeof buf - len, "{\"version\":1,\"musicShuffle\":%s,\"menuTrack\":", shuffle ? "true" : "false");
  append_json_string(buf, sizeof buf, &len, track);
  len += (size_t)snprintf(buf + len, sizeof buf - len, ",\"outputFormat\":{\"sampleRate\":%lld,\"channels\":%lld,\"sampleBits\":%lld},\"deviceName\":",
      (long long)format.sample_rate, (long long)format.channels, (long long)format.sample_bits);
  if (audio.selected_output) append_json_string(buf, sizeof buf, &len, audio.selected_output);
  else len += (size_t)snprintf(buf + len, sizeof buf - len, "null");
  len += (size_t)snprintf(buf + len, sizeof buf - len, ",\"effectsVolume\":%.17g,\"musicVolume\":%.17g}\n", audio.effects_volume, audio.music_volume);
  qa_config_store_dump(store, "audio.json", buf);
  free(saved.menu_track);
  free(saved.device_name);
}
