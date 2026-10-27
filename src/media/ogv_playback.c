/* Port of src/media/ogv-playback.ts. GPL-2.0-or-later. */
#include "qa/media/ogv_playback.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* TS plain `throw new Error(msg)`. */
static noreturn void media_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

const char *qa_ogv_playback_status_name(qa_ogv_playback_status status) {
  switch (status) {
    case QA_OGV_PLAYBACK_PLAYING: return "playing";
    case QA_OGV_PLAYBACK_LOOPED: return "looped";
    case QA_OGV_PLAYBACK_HELD: return "held";
    case QA_OGV_PLAYBACK_ENDED: return "ended";
  }
  return "playing";
}

void qa_ogv_playback_tick_dispose(qa_ogv_playback_tick *tick) {
  if (tick->has_frame) qa_cinematic_frame_dispose(&tick->frame);
  memset(tick, 0, sizeof *tick);
}

static bool set_value(qa_save_value *object, const char *key,
                      qa_save_value *child) {
  return child != NULL && qa_save_value_set(object, key, child);
}
static bool set_number(qa_save_value *object, const char *key, double value) {
  return set_value(object, key, qa_save_value_number(value));
}
static bool set_bool(qa_save_value *object, const char *key, bool value) {
  return set_value(object, key, qa_save_value_boolean(value));
}
static void read_field(const qa_save_reader *reader, const char *name,
                       qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) {
    media_error("out of memory");
  }
}
static void reader_init(qa_save_reader *reader, const qa_save_value *value,
                        const char *path) {
  if (!qa_save_reader_init(reader, value, path)) {
    media_error("out of memory");
  }
}

static void make_headers(qa_ogv_playback *playback,
                         qa_platform_ogg_packet out[3]) {
  for (int i = 0; i < 3; i++) {
    out[i].data = playback->movie.video[i].data;
    out[i].length = playback->movie.video[i].length;
    out[i].first = playback->movie.video[i].first;
    out[i].last = playback->movie.video[i].last;
    out[i].granule = playback->movie.video[i].granule;
    out[i].index = playback->movie.video[i].index;
  }
}

static void decoder_init_or_fail(qa_ogv_playback *playback,
                                 qa_theora_decoder *decoder) {
  qa_platform_ogg_packet headers[3];
  make_headers(playback, headers);
  char error[256];
  error[0] = '\0';
  if (!qa_theora_decoder_init(decoder, headers, 3, error, sizeof error)) {
    media_error("%s", error[0] == '\0' ? "Theora decoder failed" : error);
  }
}

static void decode_frame(qa_ogv_playback *playback) {
  int64_t index = playback->next_index + 3;
  if (index < 0 || index >= (int64_t)playback->movie.video_count) {
    media_error("Missing Theora frame packet");
  }
  qa_ogg_packet *packet = &playback->movie.video[index];
  qa_platform_ogg_packet platform_packet;
  platform_packet.data = packet->data;
  platform_packet.length = packet->length;
  platform_packet.first = packet->first;
  platform_packet.last = packet->last;
  platform_packet.granule = packet->granule;
  platform_packet.index = packet->index;
  qa_theora_picture image;
  memset(&image, 0, sizeof image);
  char error[256];
  error[0] = '\0';
  if (!qa_theora_decoder_decode(&playback->decoder, &platform_packet, &image,
                                error, sizeof error)) {
    media_error("%s", error[0] == '\0' ? "Theora decode failed" : error);
  }
  double source_time =
      (double)playback->next_index * playback->decoder.frame_milliseconds;
  if (playback->has_picture) qa_cinematic_frame_dispose(&playback->picture);
  memset(&playback->picture, 0, sizeof playback->picture);
  playback->picture.rgba = image.rgba;
  playback->picture.rgba_length = image.rgba_length;
  playback->picture.width = image.width;
  playback->picture.height = image.height;
  playback->picture.index = playback->next_index++;
  playback->picture.loop = playback->pass;
  playback->picture.source_time = source_time;
  playback->picture.time = playback->epoch + source_time;
  playback->has_picture = true;
}

static void queue_audio(qa_ogv_playback *playback, double elapsed) {
  qa_pcm_stream *audio = playback->audio;
  if (audio == NULL) return;
  int64_t frame_count = qa_pcm_stream_frame_count(audio);
  int64_t rate = qa_pcm_stream_sample_rate(audio);
  double wanted_d = ceil((elapsed + 200.0) * (double)rate / 1000.0);
  int64_t wanted = wanted_d < (double)frame_count ? (int64_t)wanted_d
                                                  : frame_count;
  while (qa_pcm_stream_position_frames(audio) < wanted) {
    int64_t source_sample = qa_pcm_stream_position_frames(audio);
    int64_t amount = wanted - source_sample;
    if (amount > 4096) amount = 4096;
    qa_pcm_sound chunk;
    memset(&chunk, 0, sizeof chunk);
    bool has = false;
    if (!qa_pcm_stream_read(audio, amount, &chunk, &has)) {
      media_error("out of memory");
    }
    if (!has) break;
    double source_time =
        (double)source_sample * 1000.0 / (double)chunk.sample_rate;
    qa_cinematic_audio out;
    memset(&out, 0, sizeof out);
    out.is_u8 = false;
    out.samples_s16 = chunk.samples;
    out.sample_count = chunk.sample_count;
    out.channels = chunk.channels;
    out.sample_rate = chunk.sample_rate;
    out.source_sample = source_sample;
    out.source_time = source_time;
    out.time = playback->epoch + source_time;
    out.loop = playback->pass;
    out.reset_stream = playback->reset_audio;
    playback->options.on_audio(playback->options.on_audio_ctx, &out);
    playback->reset_audio = false;
    qa_pcm_sound_dispose(&chunk);
  }
}

static void restore_checkpoint(qa_ogv_playback *playback,
                               const qa_save_value *value) {
  qa_save_reader r;
  reader_init(&r, value, "ogv-playback");
  qa_save_reader f;
  read_field(&r, "epoch", &f);
  playback->epoch = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  read_field(&r, "pass", &f);
  playback->pass = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(&r, "nextIndex", &f);
  int64_t next_index = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(&r, "picture", &f);
  qa_cinematic_frame picture;
  bool has_picture = qa_read_cinematic_frame(&f, &picture);
  qa_save_reader_destroy(&f);
  if (next_index > (int64_t)playback->movie.video_count - 3) {
    qa_save_reader_fail(&r, "video packet cursor exceeds movie");
  }
  /* Opaque native state is reconstructed from the exact packet
   * prefix, without output callbacks. */
  while (playback->next_index < next_index) decode_frame(playback);
  bool differs = has_picture != playback->has_picture;
  if (!differs && has_picture) {
    const qa_cinematic_frame *actual = &playback->picture;
    differs = picture.index != actual->index ||
              picture.width != actual->width ||
              picture.height != actual->height ||
              picture.rgba_length != actual->rgba_length ||
              (picture.rgba_length != 0 &&
               memcmp(picture.rgba, actual->rgba, picture.rgba_length) != 0);
  }
  if (differs) {
    qa_save_reader_fail(&r,
                        "reconstructed Theora frame differs from checkpoint");
  }
  read_field(&r, "audioPosition", &f);
  bool has_audio_position = !qa_save_reader_is_null(&f);
  int64_t audio_position = 0;
  if (has_audio_position) {
    audio_position = (int64_t)qa_save_reader_integer_min(&f, 0);
  }
  qa_save_reader_destroy(&f);
  if (has_audio_position != (playback->audio != NULL)) {
    qa_save_reader_fail(&r, "Vorbis stream ownership differs");
  }
  if (playback->audio != NULL && has_audio_position) {
    if (audio_position > qa_pcm_stream_frame_count(playback->audio)) {
      qa_save_reader_fail(&r, "Vorbis sample cursor exceeds movie");
    }
    while (qa_pcm_stream_position_frames(playback->audio) < audio_position) {
      int64_t remaining =
          audio_position - qa_pcm_stream_position_frames(playback->audio);
      qa_pcm_sound chunk;
      memset(&chunk, 0, sizeof chunk);
      bool has = false;
      if (!qa_pcm_stream_read(playback->audio,
                              remaining > 4096 ? 4096 : remaining, &chunk,
                              &has)) {
        media_error("out of memory");
      }
      if (!has) qa_save_reader_fail(&r, "Vorbis prefix ended early");
      qa_pcm_sound_dispose(&chunk);
    }
  }
  if (playback->has_picture) qa_cinematic_frame_dispose(&playback->picture);
  playback->picture = picture;
  playback->has_picture = has_picture;
  static const char *const state_choices[3] = {"playing", "held", "ended"};
  read_field(&r, "state", &f);
  const char *state = qa_save_reader_choice_string(&f, state_choices, 3);
  playback->state = strcmp(state, "held") == 0 ? QA_OGV_PLAYBACK_HELD
                    : strcmp(state, "ended") == 0 ? QA_OGV_PLAYBACK_ENDED
                                                  : QA_OGV_PLAYBACK_PLAYING;
  qa_save_reader_destroy(&f);
  read_field(&r, "resetAudio", &f);
  playback->reset_audio = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  read_field(&r, "closed", &f);
  bool closed = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&r);
  if (closed) qa_ogv_playback_close(playback);
}

bool qa_ogv_playback_init(qa_ogv_playback *playback, const uint8_t *bytes,
                          size_t length,
                          const qa_ogv_playback_options *options,
                          const qa_save_value *checkpoint) {
  memset(playback, 0, sizeof *playback);
  playback->options = *options;
  playback->state = QA_OGV_PLAYBACK_PLAYING;
  playback->reset_audio = true;
  if (!qa_decode_ogg_movie(bytes, length, &playback->movie)) return false;
  decoder_init_or_fail(playback, &playback->decoder);
  if (playback->movie.audio != NULL && !options->silent) {
    playback->audio = qa_vorbis_pcm_stream_from_bytes(
        playback->movie.audio, playback->movie.audio_length);
    if (playback->audio == NULL) {
      qa_theora_decoder_close(&playback->decoder);
      qa_ogg_movie_dispose(&playback->movie);
      return false;
    }
  }
  playback->epoch = options->clock.sample(options->clock.ctx);
  if (checkpoint == NULL) {
    decode_frame(playback);
    queue_audio(playback, 0);
  } else {
    restore_checkpoint(playback, checkpoint);
  }
  return true;
}

void qa_ogv_playback_destroy(qa_ogv_playback *playback) {
  qa_ogv_playback_close(playback);
  if (playback->has_picture) qa_cinematic_frame_dispose(&playback->picture);
  qa_ogg_movie_dispose(&playback->movie);
  memset(playback, 0, sizeof *playback);
}

qa_save_value *qa_ogv_playback_capture_checkpoint(qa_ogv_playback *playback) {
  qa_save_value *picture = qa_cinematic_frame_to_save(
      playback->has_picture ? &playback->picture : NULL);
  qa_save_value *object = qa_save_value_object();
  if (picture == NULL || object == NULL) {
    qa_save_value_free(picture);
    qa_save_value_free(object);
    return NULL;
  }
  qa_save_value *audio_position;
  if (playback->audio == NULL && !playback->closed) {
    audio_position = qa_save_value_null();
  } else if (playback->closed) {
    audio_position = playback->has_closed_audio_position
                         ? qa_save_value_number(
                               (double)playback->closed_audio_position)
                         : qa_save_value_null();
  } else {
    audio_position = qa_save_value_number(
        (double)qa_pcm_stream_position_frames(playback->audio));
  }
  const char *state = playback->state == QA_OGV_PLAYBACK_HELD ? "held"
                      : playback->state == QA_OGV_PLAYBACK_ENDED ? "ended"
                                                                  : "playing";
  bool ok = audio_position != NULL &&
            set_number(object, "epoch", playback->epoch) &&
            set_number(object, "pass", (double)playback->pass) &&
            set_number(object, "nextIndex", (double)playback->next_index) &&
            set_value(object, "picture", picture) &&
            set_value(object, "state", qa_save_value_string(state)) &&
            set_bool(object, "closed", playback->closed) &&
            set_bool(object, "resetAudio", playback->reset_audio) &&
            set_value(object, "audioPosition", audio_position);
  if (!ok) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

void qa_ogv_playback_dimensions(const qa_ogv_playback *playback,
                                int64_t *width, int64_t *height) {
  *width = playback->decoder.width;
  *height = playback->decoder.height;
}

const qa_cinematic_frame *qa_ogv_playback_current_frame(
    const qa_ogv_playback *playback) {
  return playback->has_picture ? &playback->picture : NULL;
}

bool qa_ogv_playback_run(qa_ogv_playback *playback, qa_media_clock clock,
                         qa_ogv_playback_tick *out) {
  memset(out, 0, sizeof *out);
  if (playback->closed || playback->state != QA_OGV_PLAYBACK_PLAYING) {
    out->status = playback->state;
    return true;
  }
  double now = clock.sample(clock.ctx);
  if (!isfinite(now) || now < playback->epoch) {
    qa_range_error("OGV clock must be finite and monotonic");
  }
  int64_t count = (int64_t)playback->movie.video_count - 3;
  double duration = (double)count * playback->decoder.frame_milliseconds;
  double elapsed = now - playback->epoch;
  bool looped = false;
  if (elapsed >= duration) {
    if (playback->options.hold) {
      while (playback->next_index < count) decode_frame(playback);
      playback->state = QA_OGV_PLAYBACK_HELD;
      out->status = QA_OGV_PLAYBACK_HELD;
      out->is_frame = true;
      out->has_frame =
          qa_copy_cinematic_frame(
              playback->has_picture ? &playback->picture : NULL,
              &out->frame);
      return playback->has_picture ? out->has_frame : true;
    }
    if (!playback->options.loop) {
      playback->state = QA_OGV_PLAYBACK_ENDED;
      out->status = QA_OGV_PLAYBACK_ENDED;
      return true;
    }
    int64_t passes = (int64_t)floor(elapsed / duration);
    qa_theora_decoder next;
    memset(&next, 0, sizeof next);
    decoder_init_or_fail(playback, &next);
    qa_theora_decoder_close(&playback->decoder);
    playback->decoder = next;
    if (playback->audio != NULL) qa_pcm_stream_seek(playback->audio, 0);
    playback->pass += passes;
    playback->epoch += (double)passes * duration;
    elapsed = now - playback->epoch;
    playback->next_index = 0;
    playback->reset_audio = true;
    looped = true;
  }
  int64_t target = (int64_t)floor(elapsed /
                                  playback->decoder.frame_milliseconds);
  if (target > count - 1) target = count - 1;
  bool changed = false;
  while (playback->next_index <= target) {
    decode_frame(playback);
    changed = true;
  }
  queue_audio(playback, elapsed);
  out->status = looped ? QA_OGV_PLAYBACK_LOOPED : QA_OGV_PLAYBACK_PLAYING;
  out->is_frame = changed;
  if (changed) {
    out->has_frame = qa_copy_cinematic_frame(
        playback->has_picture ? &playback->picture : NULL, &out->frame);
    if (playback->has_picture && !out->has_frame) return false;
  }
  return true;
}

void qa_ogv_playback_close(qa_ogv_playback *playback) {
  if (playback->closed) return;
  playback->has_closed_audio_position = playback->audio != NULL;
  if (playback->audio != NULL) {
    playback->closed_audio_position =
        qa_pcm_stream_position_frames(playback->audio);
  }
  playback->closed = true;
  playback->state = QA_OGV_PLAYBACK_ENDED;
  if (playback->audio != NULL) {
    qa_pcm_stream_close(playback->audio);
    playback->audio = NULL;
  }
  qa_theora_decoder_close(&playback->decoder);
}
