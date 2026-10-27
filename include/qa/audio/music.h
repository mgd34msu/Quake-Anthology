/*
 * Port of src/audio/music.ts. MusicPlayer owns its streams (closed
 * on stop/start transitions as in TS) and its RawAudioStream by
 * value; mix writes frames*2 doubles into a caller buffer. CdMusic's
 * async open becomes a synchronous fn+ctx; the request counter and
 * enabled checks keep their TS positions. MusicControls is shared
 * by pointer when supplied, else owned (controls_owned). Volume
 * mode "source" is the non-immediate mode. GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_MUSIC_H
#define QA_AUDIO_MUSIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/audio/streams.h"
#include "qa/audio/types.h"

typedef struct qa_music_controls {
  bool enabled;
  int remap[100];
} qa_music_controls;

void qa_music_controls_init(qa_music_controls *controls);
/* Copies remap[1..99] (TS remappedTracks slice) into out. */
void qa_music_controls_remapped_tracks(const qa_music_controls *controls,
                                       int out[99]);
int64_t qa_music_controls_mapped_track(const qa_music_controls *controls,
                                       int64_t track);
void qa_music_controls_set_remap(qa_music_controls *controls,
                                 const int64_t *tracks, size_t count);
void qa_music_controls_reset(qa_music_controls *controls);

typedef enum qa_music_volume_mode {
  QA_MUSIC_VOLUME_SOURCE,
  QA_MUSIC_VOLUME_IMMEDIATE
} qa_music_volume_mode;

typedef struct qa_music_player {
  int64_t output_rate;
  qa_sound_family family;
  qa_music_volume_mode volume_mode;
  qa_music_controls *controls;
  qa_music_controls owned_controls;
  qa_pcm_stream *stream;
  qa_pcm_stream *loop;
  qa_raw_audio_stream pcm;
  double target_volume;
  float smoothed_volume;
  bool paused;
  int64_t completions;
  /* Borrowed chunk handed to RawAudioStream refill (nextChunk). */
  qa_pcm_sound pending_chunk;
  bool has_pending_chunk;
} qa_music_player;

/* controls NULL creates an owned MusicControls (TS default). */
void qa_music_player_init(qa_music_player *player, int64_t output_rate,
                          qa_sound_family family,
                          qa_music_volume_mode volume_mode,
                          qa_music_controls *controls);
void qa_music_player_destroy(qa_music_player *player);
bool qa_music_player_playing(const qa_music_player *player);
int64_t qa_music_player_source_position(const qa_music_player *player);
int64_t qa_music_player_completed_plays(const qa_music_player *player);
double qa_music_player_volume(const qa_music_player *player);
void qa_music_player_set_volume(qa_music_player *player, double value);
void qa_music_player_update(qa_music_player *player);
/* Takes ownership of intro (and loop when non-NULL). */
void qa_music_player_start(qa_music_player *player, qa_pcm_stream *intro,
                           qa_pcm_stream *loop);
void qa_music_player_stop(qa_music_player *player);
void qa_music_player_mix(qa_music_player *player, int64_t frames,
                         double *output);
void qa_music_player_close(qa_music_player *player);

/* TS OpenMusicTrack: NULL stream means not found. */
typedef qa_pcm_stream *(*qa_open_music_track_fn)(const char *path, void *ctx);

typedef struct qa_cd_music {
  qa_music_player *player; /* borrowed */
  qa_open_music_track_fn open;
  void *open_ctx;
  bool has_track;
  int64_t track;
  int64_t request;
} qa_cd_music;

void qa_cd_music_init(qa_cd_music *music, qa_music_player *player,
                      qa_open_music_track_fn open, void *open_ctx);
bool qa_cd_music_enabled(const qa_cd_music *music);
void qa_cd_music_set_enabled(qa_cd_music *music, bool value);
bool qa_cd_music_playing_track(const qa_cd_music *music, int64_t *out);
void qa_cd_music_remapped_tracks(const qa_cd_music *music, int out[99]);
void qa_cd_music_set_remap(qa_cd_music *music, const int64_t *tracks,
                           size_t count);
void qa_cd_music_reset(qa_cd_music *music);
bool qa_cd_music_play(qa_cd_music *music, int64_t track, bool looping);
void qa_cd_music_pause(qa_cd_music *music);
void qa_cd_music_resume(qa_cd_music *music);
void qa_cd_music_invalidate_pending(qa_cd_music *music);
void qa_cd_music_stop(qa_cd_music *music);
void qa_cd_music_close(qa_cd_music *music);

typedef enum qa_q2_soundtrack_profile_kind {
  QA_Q2_SOUNDTRACK_DISC,
  QA_Q2_SOUNDTRACK_REMASTERED
} qa_q2_soundtrack_profile_kind;
typedef struct qa_q2_soundtrack_profile {
  qa_q2_soundtrack_profile_kind kind;
  const char *campaign; /* remastered */
} qa_q2_soundtrack_profile;

int64_t qa_remap_q2_music_track(int64_t track,
                                qa_q2_soundtrack_profile profile);

#endif
