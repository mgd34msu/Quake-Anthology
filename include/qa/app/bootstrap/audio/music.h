/* Port of src/app/bootstrap/audio/music.ts. Async flows are
 * synchronous (landed music opens are synchronous); the request
 * counter keeps its TS invalidation positions. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_AUDIO_MUSIC_H
#define QA_APP_BOOTSTRAP_AUDIO_MUSIC_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/bank.h"
#include "qa/audio/engine.h"
#include "qa/audio/music.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
/* TS MusicSource. Strings borrowed. */
typedef struct qa_app_music_source {
  qa_content_id content;
  qa_game_family family;
  const char *edition;
  const char *campaign;
} qa_app_music_source;
/* TS ReadonlyMap<string,string> world spawn keys. */
typedef struct qa_app_world_key_value { const char *key; const char *value; } qa_app_world_key_value;
/* TS worldMusicTrack: returns a borrowed value or "". */
const char *qa_app_world_music_track(const qa_app_world_key_value *world, size_t world_count,
                                     bool has_world, qa_game_family family, const char *edition);
/* TS q1MusicFallback / musicFallback: false is the TS null;
 * *out borrows the catalog product id. */
bool qa_app_q1_music_fallback(qa_content_id content, const qa_content_catalog *catalog, const char **out);
bool qa_app_music_fallback(qa_content_id content, const qa_content_catalog *catalog, const char **out);
/* TS OpenMusicTrack fallback (fn+ctx; fn NULL = TS null). */
typedef struct qa_app_open_music_track {
  qa_pcm_stream *(*open)(void *ctx, const char *path);
  void *ctx;
} qa_app_open_music_track;
/* TS ApplicationMusic playlist argument. */
typedef struct qa_app_music_playlist {
  bool shuffle;
  const char *const *tracks;
  size_t track_count;
} qa_app_music_playlist;
typedef struct qa_app_application_music qa_app_application_music;
typedef void (*qa_app_music_print_fn)(void *ctx, const char *text);
/* controls NULL = TS default new MusicControls (owned here);
 * volume_mode is the TS MusicVolumeMode; random NULL = Math.random. */
qa_app_application_music *qa_app_application_music_create(
    qa_unified_audio *engine, qa_app_music_print_fn print, void *print_ctx,
    qa_music_volume_mode volume_mode, qa_music_controls *controls,
    double (*random)(void *ctx), void *random_ctx);
void qa_app_application_music_destroy(qa_app_application_music *music);
/* TS controls field. */
qa_music_controls *qa_app_application_music_controls(qa_app_application_music *music);
double qa_app_application_music_volume(const qa_app_application_music *music);
/* Aborts with "Invalid music volume" (TS RangeError). */
void qa_app_application_music_set_volume(qa_app_application_music *music, double value);
/* fallback.open NULL = TS null. */
void qa_app_application_music_select(qa_app_application_music *music, qa_app_music_source source,
                                     qa_sound_bank *bank, qa_app_open_music_track fallback);
/* print NULL uses the constructor print. */
void qa_app_application_music_music_command(qa_app_application_music *music,
    const char *const *args, size_t argc, qa_app_music_print_fn print, void *print_ctx);
void qa_app_application_music_cd_command(qa_app_application_music *music,
    const char *const *args, size_t argc, qa_app_music_print_fn print, void *print_ctx);
/* reason: "manual" | "source". */
void qa_app_application_music_stop_playback(qa_app_application_music *music, const char *reason);
void qa_app_application_music_invalidate_pending(qa_app_application_music *music);
void qa_app_application_music_stop(qa_app_application_music *music);
/* playlist NULL = TS null. */
void qa_app_application_music_play(qa_app_application_music *music, qa_app_music_source source,
    qa_sound_bank *bank, const char *track, qa_app_open_music_track fallback,
    const qa_app_music_playlist *playlist);
/* current: TS () => boolean liveness probe. */
void qa_app_application_music_play_tracks(qa_app_application_music *music, qa_app_music_source source,
    qa_sound_bank *bank, const char *intro, const char *loop,
    bool (*current)(void *ctx), void *current_ctx);
void qa_app_application_music_update_automatic(qa_app_application_music *music, bool shuffle);
#endif
