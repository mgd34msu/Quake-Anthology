/* Port of src/audio/music.ts. GPL-2.0-or-later. */
#include "qa/audio/music.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

void qa_music_controls_init(qa_music_controls *controls) {
  controls->enabled = true;
  for (int i = 0; i < 100; i++) controls->remap[i] = i;
}

void qa_music_controls_remapped_tracks(const qa_music_controls *controls,
                                       int out[99]) {
  for (int i = 0; i < 99; i++) out[i] = controls->remap[i + 1];
}

int64_t qa_music_controls_mapped_track(const qa_music_controls *controls,
                                       int64_t track) {
  if (track < 0 || track >= 100) return track;
  return controls->remap[track];
}

void qa_music_controls_set_remap(qa_music_controls *controls,
                                 const int64_t *tracks, size_t count) {
  if (count >= 100) qa_range_error("CD remap exceeds 99 tracks");
  for (size_t i = 0; i < count; i++)
    if (tracks[i] < 0 || tracks[i] > 255) qa_range_error("Invalid CD track");
  for (size_t i = 0; i < count; i++) controls->remap[i + 1] = (int)tracks[i];
}

void qa_music_controls_reset(qa_music_controls *controls) {
  qa_music_controls_init(controls);
}

void qa_music_player_init(qa_music_player *player, int64_t output_rate,
                          qa_sound_family family,
                          qa_music_volume_mode volume_mode,
                          qa_music_controls *controls) {
  memset(player, 0, sizeof *player);
  player->output_rate = output_rate;
  player->family = family;
  player->volume_mode = volume_mode;
  if (controls != NULL) {
    player->controls = controls;
  } else {
    qa_music_controls_init(&player->owned_controls);
    player->controls = &player->owned_controls;
  }
  player->target_volume = 0.25;
  player->smoothed_volume = (float)0.5;
  qa_raw_audio_stream_init(&player->pcm, output_rate);
}

void qa_music_player_destroy(qa_music_player *player) {
  qa_music_player_stop(player);
  qa_raw_audio_stream_destroy(&player->pcm);
}

bool qa_music_player_playing(const qa_music_player *player) {
  return player->stream != NULL;
}

int64_t qa_music_player_source_position(const qa_music_player *player) {
  return qa_raw_audio_stream_source_position(&player->pcm);
}

int64_t qa_music_player_completed_plays(const qa_music_player *player) {
  return player->completions;
}

double qa_music_player_volume(const qa_music_player *player) {
  return player->family == QA_SOUND_FAMILY_Q3 &&
                 player->volume_mode == QA_MUSIC_VOLUME_SOURCE
             ? player->smoothed_volume
             : player->target_volume;
}

void qa_music_player_set_volume(qa_music_player *player, double value) {
  if (!isfinite(value) || value < 0) qa_range_error("Invalid music volume");
  player->target_volume = value;
}

void qa_music_player_update(qa_music_player *player) {
  if (player->family == QA_SOUND_FAMILY_Q3 &&
      player->volume_mode == QA_MUSIC_VOLUME_SOURCE &&
      player->stream != NULL && !player->paused && player->controls->enabled)
    player->smoothed_volume =
        (float)((float)(player->smoothed_volume +
                        (float)((float)player->target_volume * 2)) /
                4);
}

void qa_music_player_start(qa_music_player *player, qa_pcm_stream *intro,
                           qa_pcm_stream *loop) {
  qa_music_player_stop(player);
  player->stream = intro;
  player->loop = loop;
  qa_raw_audio_stream_destroy(&player->pcm);
  qa_raw_audio_stream_init(&player->pcm, player->output_rate);
  player->paused = false;
}

void qa_music_player_stop(qa_music_player *player) {
  qa_pcm_stream *stream = player->stream;
  qa_pcm_stream *loop = player->loop;
  player->stream = NULL;
  player->loop = NULL;
  if (player->has_pending_chunk) {
    qa_pcm_sound_dispose(&player->pending_chunk);
    player->has_pending_chunk = false;
  }
  qa_pcm_stream_close(stream);
  if (loop != stream) qa_pcm_stream_close(loop);
}

/* TS nextChunk, driven as the RawAudioStream refill. The pending
 * chunk's samples are borrowed by queue (which copies), then the
 * pending sound is disposed after each refill round in mix. */
static bool next_chunk(void *ctx, qa_stream_pcm *out) {
  qa_music_player *player = ctx;
  if (player->has_pending_chunk) {
    qa_pcm_sound_dispose(&player->pending_chunk);
    player->has_pending_chunk = false;
  }
  qa_pcm_stream *stream = player->stream;
  if (stream == NULL) return false;
  int64_t source_sample = qa_pcm_stream_position_frames(stream);
  bool has = false;
  if (!qa_pcm_stream_read(stream, 16384, &player->pending_chunk, &has))
    abort(); /* OOM has no TS counterpart */
  if (has) {
    player->has_pending_chunk = true;
    memset(out, 0, sizeof *out);
    out->samples_s16 = player->pending_chunk.samples;
    out->sample_count = player->pending_chunk.sample_count;
    out->channels = player->pending_chunk.channels;
    out->sample_rate = player->pending_chunk.sample_rate;
    out->source_sample = source_sample;
    out->reset_stream = false;
    return true;
  }
  if (player->loop == NULL) {
    player->stream = NULL;
    player->completions++;
    qa_pcm_stream_close(stream);
    return false;
  }
  if (player->loop != stream) qa_pcm_stream_close(stream);
  player->stream = player->loop;
  qa_pcm_stream_seek(player->stream, 0);
  if (!qa_pcm_stream_read(player->stream, 16384, &player->pending_chunk, &has))
    abort();
  if (!has) {
    qa_music_player_stop(player);
    return false;
  }
  player->has_pending_chunk = true;
  memset(out, 0, sizeof *out);
  out->samples_s16 = player->pending_chunk.samples;
  out->sample_count = player->pending_chunk.sample_count;
  out->channels = player->pending_chunk.channels;
  out->sample_rate = player->pending_chunk.sample_rate;
  out->source_sample = 0;
  out->reset_stream = true;
  return true;
}

typedef struct music_mix_job {
  qa_music_player *player;
  int64_t frames;
  double *output;
} music_mix_job;
static void music_mix_run(void *ctx) {
  music_mix_job *job = ctx;
  qa_raw_audio_stream_mix(&job->player->pcm, job->frames, qa_music_player_volume(job->player),
                          next_chunk, job->player, job->output);
}

void qa_music_player_mix(qa_music_player *player, int64_t frames,
                         double *output) {
  if (!player->controls->enabled || player->paused ||
      qa_music_player_volume(player) <= 0 || player->stream == NULL) {
    for (int64_t i = 0; i < frames * 2; i++) output[i] = 0;
    return;
  }
  /* A decoder failure stops the damaged track instead of ending the session
     (TS try/catch -> stop(); onFault?.(error); silence). */
  music_mix_job job = {player, frames, output};
  qa_caught_error caught;
  const bool mixed = qa_try(music_mix_run, &job, &caught);
  if (player->has_pending_chunk) {
    qa_pcm_sound_dispose(&player->pending_chunk);
    player->has_pending_chunk = false;
  }
  if (!mixed) {
    qa_music_player_stop(player);
    if (player->on_fault != NULL) player->on_fault(player->on_fault_ctx, &caught);
    for (int64_t i = 0; i < frames * 2; i++) output[i] = 0;
  }
}

void qa_music_player_close(qa_music_player *player) {
  qa_music_player_stop(player);
}

void qa_cd_music_init(qa_cd_music *music, qa_music_player *player,
                      qa_open_music_track_fn open, void *open_ctx) {
  memset(music, 0, sizeof *music);
  music->player = player;
  music->open = open;
  music->open_ctx = open_ctx;
}

bool qa_cd_music_enabled(const qa_cd_music *music) {
  return music->player->controls->enabled;
}

void qa_cd_music_set_enabled(qa_cd_music *music, bool value) {
  music->player->controls->enabled = value;
}

bool qa_cd_music_playing_track(const qa_cd_music *music, int64_t *out) {
  if (!music->has_track) return false;
  *out = music->track;
  return true;
}

void qa_cd_music_remapped_tracks(const qa_cd_music *music, int out[99]) {
  qa_music_controls_remapped_tracks(music->player->controls, out);
}

void qa_cd_music_set_remap(qa_cd_music *music, const int64_t *tracks,
                           size_t count) {
  qa_music_controls_set_remap(music->player->controls, tracks, count);
}

void qa_cd_music_reset(qa_cd_music *music) {
  qa_cd_music_stop(music);
  qa_music_controls_reset(music->player->controls);
}

bool qa_cd_music_play(qa_cd_music *music, int64_t track, bool looping) {
  if (!qa_cd_music_enabled(music)) return false;
  if (track < 0 || track > 255) qa_range_error("Invalid CD track");
  int64_t mapped =
      qa_music_controls_mapped_track(music->player->controls, track);
  if (mapped < 1) return false;
  if (music->has_track && music->track == mapped &&
      qa_music_player_playing(music->player))
    return true;
  int64_t request = ++music->request;
  qa_music_player_stop(music->player);
  music->has_track = false;
  char number[24];
  snprintf(number, sizeof number, "%02lld", (long long)mapped);
  char candidates[4][64];
  snprintf(candidates[0], 64, "music/%s.ogg", number);
  snprintf(candidates[1], 64, "music/track%s.ogg", number);
  snprintf(candidates[2], 64, "music/%s.wav", number);
  snprintf(candidates[3], 64, "music/track%s.wav", number);
  for (int i = 0; i < 4; i++) {
    qa_pcm_stream *stream = music->open(candidates[i], music->open_ctx);
    if (request != music->request || !qa_cd_music_enabled(music)) {
      qa_pcm_stream_close(stream);
      return false;
    }
    if (stream == NULL) continue;
    qa_music_player_start(music->player, stream, looping ? stream : NULL);
    music->track = mapped;
    music->has_track = true;
    return true;
  }
  return false;
}

void qa_cd_music_pause(qa_cd_music *music) { music->player->paused = true; }

void qa_cd_music_resume(qa_cd_music *music) {
  if (qa_cd_music_enabled(music)) music->player->paused = false;
}

void qa_cd_music_invalidate_pending(qa_cd_music *music) { music->request++; }

void qa_cd_music_stop(qa_cd_music *music) {
  music->request++;
  qa_music_player_stop(music->player);
  music->has_track = false;
}

void qa_cd_music_close(qa_cd_music *music) { qa_cd_music_stop(music); }

static bool equals_lower(const char *value, const char *lower) {
  while (*value != '\0' && *lower != '\0') {
    char c = *value >= 'A' && *value <= 'Z' ? (char)(*value + 32) : *value;
    if (c != *lower) return false;
    value++;
    lower++;
  }
  return *value == *lower;
}

int64_t qa_remap_q2_music_track(int64_t track,
                                qa_q2_soundtrack_profile profile) {
  static const int xatrix_tracks[10] = {9, 13, 14, 7, 16, 2, 15, 3, 4, 18};
  if (profile.kind == QA_Q2_SOUNDTRACK_DISC || track < 2 || track > 11)
    return track;
  if (equals_lower(profile.campaign, "rogue")) return track + 10;
  if (equals_lower(profile.campaign, "xatrix")) {
    if (track - 2 < 0 || track - 2 >= 10)
      qa_range_error("Expansion soundtrack index outside CD range");
    return xatrix_tracks[track - 2];
  }
  return track;
}
