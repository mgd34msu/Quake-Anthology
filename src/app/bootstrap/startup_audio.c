/* Port of src/app/bootstrap/startup-audio.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_audio.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/output_settings.h"
#include "qa/app/bootstrap/audio/playlist.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
typedef struct queued {
  const char *name;
  char **args; size_t arg_count;
  void (*print)(void *ctx, const char *text); void *print_ctx;
} queued;
typedef struct candidate { qa_content_mounted *mounts; qa_app_music_source source; qa_sound_bank *bank; const char *const *names; size_t name_count; } candidate;
struct qa_app_startup_audio {
  qa_app_startup_audio_host host;
  qa_unified_audio *engine;
  qa_app_application_music *music;
  qa_cvar_registry *output_cvars;
  const char *family;
  qa_seat_id seat;
  void (*print)(void *ctx, const char *text); void *print_ctx;
  candidate candidates[2]; size_t candidate_count;
  char *menu_track;
  char **tracks; size_t track_count;
  queued commands[1024]; size_t command_head, command_count;
  struct { const char *event; const void *sound; } sounds[8]; size_t sound_count;
  bool closed;
};
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static void select_menu_track(qa_app_startup_audio *a, const char *value) {
  if (a->closed || (a->menu_track != NULL && strcmp(a->menu_track, value) == 0)) return;
  free(a->menu_track); a->menu_track = xdup(value);
  qa_app_application_music_stop_playback(a->music, "source");
  if (strcmp(value, "0") == 0) return;
  for (size_t ci = 0; ci < a->candidate_count; ci++) {
    bool numeric = value[0] != '\0';
    for (const char *p = value; *p; p++) if (!isdigit((unsigned char)*p)) numeric = false;
    char number_buf[64] = "";
    if (numeric) {
      long n = strtol(value, NULL, 10);
      snprintf(number_buf, sizeof number_buf, "%02ld", n);
    }
    char names_storage[2][512];
    const char *names[4];
    size_t name_count = 0;
    if (strcmp(value, "auto") == 0) {
      for (size_t i = 0; i < a->candidates[ci].name_count && name_count < 4; i++) names[name_count++] = a->candidates[ci].names[i];
    } else if (numeric) {
      snprintf(names_storage[0], sizeof names_storage[0], "music/%s", number_buf);
      snprintf(names_storage[1], sizeof names_storage[1], "music/track%s", number_buf);
      names[name_count++] = names_storage[0];
      names[name_count++] = names_storage[1];
    } else {
      if (strncmp(value, "music/", 6) == 0) names[name_count++] = value;
      else { snprintf(names_storage[0], sizeof names_storage[0], "music/%s", value); names[name_count++] = names_storage[0]; }
    }
    for (size_t ni = 0; ni < name_count; ni++) {
      const char *name = names[ni];
      size_t len = strlen(name);
      bool has_ext = (len > 4 && (strcasecmp(name + len - 4, ".ogg") == 0 || strcasecmp(name + len - 4, ".wav") == 0));
      char paths[2][560];
      const char *path_list[2];
      size_t path_count = 0;
      if (has_ext) path_list[path_count++] = name;
      else {
        snprintf(paths[0], sizeof paths[0], "%s.ogg", name);
        snprintf(paths[1], sizeof paths[1], "%s.wav", name);
        path_list[path_count++] = paths[0];
        path_list[path_count++] = paths[1];
      }
      for (size_t pi = 0; pi < path_count; pi++) {
        const qa_resolved_resource_reference *resolved = NULL;
        if (!qa_content_mounted_resolve(a->candidates[ci].mounts, path_list[pi], &resolved)) continue;
        if (a->closed || a->menu_track == NULL || strcmp(a->menu_track, value) != 0) return;
        char *cue = qa_app_music_file_cue(path_list[pi]);
        qa_app_application_music_play(a->music, a->candidates[ci].source, a->candidates[ci].bank, cue,
                                      (qa_app_open_music_track){NULL, NULL}, NULL);
        free(cue);
        return;
      }
    }
  }
  if (strcmp(value, "auto") != 0) {
    char buf[1024];
    snprintf(buf, sizeof buf, "Menu music unavailable: %s\n", value);
    a->print(a->print_ctx, buf);
  }
}
qa_app_startup_audio *qa_app_startup_audio_open(qa_app_startup_audio_host host, qa_app_startup_audio_options options) {
  qa_app_startup_audio *a = calloc(1, sizeof *a);
  if (!a) return NULL;
  a->host = host;
  a->family = options.family;
  a->seat = options.seat;
  a->print = options.print; a->print_ctx = options.print_ctx;
  a->engine = host.create_engine(host.ctx, &options.preferences);
  a->music = qa_app_application_music_create(a->engine, options.print, options.print_ctx,
                                             QA_MUSIC_VOLUME_IMMEDIATE, options.controls, NULL, NULL);
  qa_app_startup_audio_set_volumes(a, options.preferences.has_effects_volume ? options.preferences.effects_volume : 0.7,
                                   options.preferences.has_music_volume ? options.preferences.music_volume : 0.25);
  host.engine_set_listeners(host.ctx, a->engine, options.seat);
  static const char *const events[] = {"open", "close", "move", "change", "reject"};
  for (int i = 0; i < 5; i++) {
    const void *sound = host.bank_register(host.ctx, options.family, events[i]);
    if (sound != NULL) { a->sounds[a->sound_count].event = events[i]; a->sounds[a->sound_count].sound = sound; a->sound_count++; }
  }
  qa_app_application_music_select(a->music, options.source, options.bank, (qa_app_open_music_track){NULL, NULL});
  static const char *const fallback_q1[] = {"music/track02", "music/02"};
  static const char *const fallback_q2[] = {"music/02", "music/track02"};
  static const char *const fallback_q3[] = {"music/sonic5"};
  static const char *const theme_names[] = {"music/track77"};
  const char *const *fallback = fallback_q3; size_t fallback_count = 1;
  if (strcmp(options.family, "q1") == 0) { fallback = fallback_q1; fallback_count = 2; }
  else if (strcmp(options.family, "q2") == 0) { fallback = fallback_q2; fallback_count = 2; }
  a->candidates[a->candidate_count++] = (candidate){options.mounts, options.source, options.bank, fallback, fallback_count};
  if (options.has_theme) {
    memmove(&a->candidates[1], &a->candidates[0], sizeof(candidate));
    a->candidates[0] = (candidate){options.theme_mounts, options.theme_source, options.theme_bank, theme_names, 1};
    a->candidate_count++;
  }
  /* tracks: sorted unique union over candidates. */
  size_t cap = 0;
  for (size_t ci = 0; ci < a->candidate_count; ci++) {
    size_t count = 0;
    char **list = qa_app_mounted_music_tracks(a->candidates[ci].mounts, &count);
    for (size_t i = 0; i < count; i++) {
      bool dup = false;
      for (size_t j = 0; j < a->track_count; j++) if (strcmp(a->tracks[j], list[i]) == 0) dup = true;
      if (!dup) {
        if (a->track_count == cap) { cap = cap ? cap * 2 : 16; a->tracks = realloc(a->tracks, cap * sizeof *a->tracks); if (!a->tracks) qa_range_error("Out of memory"); }
        a->tracks[a->track_count++] = list[i];
      } else free(list[i]);
    }
    free(list);
  }
  /* localeCompare-free byte sort matches TS sort() for ASCII track names. */
  for (size_t i = 0; i < a->track_count; i++)
    for (size_t j = i + 1; j < a->track_count; j++)
      if (strcmp(a->tracks[j], a->tracks[i]) < 0) { char *tmp = a->tracks[i]; a->tracks[i] = a->tracks[j]; a->tracks[j] = tmp; }
  select_menu_track(a, options.preferences.menu_track ? options.preferences.menu_track : "auto");
  return a;
}
void qa_app_startup_audio_destroy(qa_app_startup_audio *a) {
  if (!a) return;
  qa_app_startup_audio_close(a);
  qa_app_application_music_destroy(a->music);
  for (size_t i = 0; i < a->track_count; i++) free(a->tracks[i]);
  free(a->tracks); free(a->menu_track);
  a->host.destroy_engine(a->host.ctx, a->engine);
  free(a);
}
qa_unified_audio *qa_app_startup_audio_engine(qa_app_startup_audio *a) { return a->engine; }
qa_audio_output_format qa_app_startup_audio_output_format(qa_app_startup_audio *a) {
  return a->host.engine_output_format(a->host.ctx, a->engine);
}
void qa_app_startup_audio_bind_output_cvars(qa_app_startup_audio *a, qa_cvar_registry *cvars) { a->output_cvars = cvars; }
void qa_app_startup_audio_select_output_format(qa_app_startup_audio *a, qa_audio_output_format format) {
  a->host.engine_select_output(a->host.ctx, a->engine, a->host.engine_selected_output(a->host.ctx, a->engine), format, false);
  if (a->output_cvars != NULL) qa_app_write_audio_output_cvars(a->output_cvars, qa_app_startup_audio_output_format(a));
}
void qa_app_startup_audio_restart_output(qa_app_startup_audio *a) {
  qa_audio_output_format format = a->output_cvars == NULL ? qa_app_startup_audio_output_format(a)
                                                           : qa_app_read_audio_output_cvars(a->output_cvars);
  a->host.engine_select_output(a->host.ctx, a->engine, a->host.engine_selected_output(a->host.ctx, a->engine), format, true);
}
const char *const *qa_app_startup_audio_music_tracks(qa_app_startup_audio *a, size_t *out_count) {
  *out_count = a->track_count;
  return (const char *const *)a->tracks;
}
qa_app_audio_music_preferences qa_app_startup_audio_music_preferences(qa_app_startup_audio *a) {
  if (a->output_cvars == NULL) {
    qa_app_audio_music_preferences prefs = {false, xdup(a->menu_track ? a->menu_track : "auto")};
    return prefs;
  }
  return qa_app_read_music_settings(a->output_cvars);
}
void qa_app_startup_audio_open_output(qa_app_startup_audio *a, const char *device_name, void (*print)(void *ctx, const char *text), void *print_ctx) {
  bool failed = false; const char *error = NULL;
  a->host.engine_open_device(a->host.ctx, a->engine, device_name, &failed, &error);
  if (!failed) return;
  if (device_name == NULL) qa_range_error("%s", error ? error : "Audio output unavailable");
  char buf[2048];
  snprintf(buf, sizeof buf, "Audio output %s unavailable: %s. Using system default.\n", device_name, error ? error : "");
  print(print_ctx, buf);
  a->host.engine_open_device(a->host.ctx, a->engine, NULL, &failed, &error);
  if (failed) qa_range_error("%s", error ? error : "Audio output unavailable");
}
void qa_app_startup_audio_cd_command(qa_app_startup_audio *a, const char *const *args, size_t count, void (*print)(void *ctx, const char *text), void *print_ctx) {
  if (!a->closed) qa_app_application_music_cd_command(a->music, args, count, print, print_ctx);
}
static void queue_command(qa_app_startup_audio *a, const char *name, const char *const *args, size_t count, void (*print)(void *ctx, const char *text), void *print_ctx) {
  if (a->closed) return;
  if (a->command_count == 1024) qa_range_error("Event ring capacity exhausted (%zu undrained records); drain before admitting more", (size_t)1024);
  queued *q = &a->commands[(a->command_head + a->command_count) % 1024];
  q->name = name;
  q->args = calloc(count ? count : 1, sizeof *q->args);
  if (!q->args) qa_range_error("Out of memory");
  q->arg_count = count;
  for (size_t i = 0; i < count; i++) q->args[i] = xdup(args[i]);
  q->print = print; q->print_ctx = print_ctx;
  a->command_count++;
}
void qa_app_startup_audio_queue_cd_command(qa_app_startup_audio *a, const char *const *args, size_t count, void (*print)(void *ctx, const char *text), void *print_ctx) {
  queue_command(a, "cd", args, count, print, print_ctx);
}
void qa_app_startup_audio_queue_music_command(qa_app_startup_audio *a, const char *const *args, size_t count, void (*print)(void *ctx, const char *text), void *print_ctx) {
  queue_command(a, "music", args, count, print, print_ctx);
}
void qa_app_startup_audio_flush_commands(qa_app_startup_audio *a) {
  if (a->output_cvars != NULL) {
    qa_app_audio_music_preferences prefs = qa_app_read_music_settings(a->output_cvars);
    select_menu_track(a, prefs.menu_track);
    free(prefs.menu_track);
  }
  while (!a->closed && a->command_count > 0) {
    queued command = a->commands[a->command_head];
    a->command_head = (a->command_head + 1) % 1024;
    a->command_count--;
    if (strcmp(command.name, "cd") == 0) qa_app_startup_audio_cd_command(a, (const char *const *)command.args, command.arg_count, command.print, command.print_ctx);
    else qa_app_application_music_music_command(a->music, (const char *const *)command.args, command.arg_count, command.print, command.print_ctx);
    for (size_t i = 0; i < command.arg_count; i++) free(command.args[i]);
    free(command.args);
  }
}
void qa_app_startup_audio_set_volumes(qa_app_startup_audio *a, double effects, double music) {
  if (a->closed) return;
  a->host.engine_set_effects_volume(a->host.ctx, a->engine, effects);
  qa_app_application_music_set_volume(a->music, music);
}
void qa_app_startup_audio_sound(qa_app_startup_audio *a, const char *event) {
  if (a->closed) return;
  for (size_t i = 0; i < a->sound_count; i++)
    if (strcmp(a->sounds[i].event, event) == 0) a->host.engine_play_ui(a->host.ctx, a->engine, a->sounds[i].sound, a->family, a->seat);
}
void qa_app_startup_audio_pump(qa_app_startup_audio *a) {
  if (a->closed) return;
  a->host.engine_update_music(a->host.ctx, a->engine);
  a->host.engine_pump(a->host.ctx, a->engine);
}
void qa_app_startup_audio_close(qa_app_startup_audio *a) {
  if (a->closed) return;
  a->closed = true;
  while (a->command_count > 0) {
    queued command = a->commands[a->command_head];
    a->command_head = (a->command_head + 1) % 1024;
    a->command_count--;
    for (size_t i = 0; i < command.arg_count; i++) free(command.args[i]);
    free(command.args);
  }
  qa_app_application_music_stop(a->music);
  a->host.engine_close(a->host.ctx, a->engine);
  a->host.bank_clear(a->host.ctx);
  a->sound_count = 0;
}
