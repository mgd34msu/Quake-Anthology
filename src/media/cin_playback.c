/* Port of src/media/cin-playback.ts. GPL-2.0-or-later. */
#include "qa/media/cin_playback.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static noreturn void media_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

const char *qa_cin_playback_status_name(qa_cin_playback_status status) {
  switch (status) {
    case QA_CIN_PLAYBACK_PLAYING: return "playing";
    case QA_CIN_PLAYBACK_HELD: return "held";
    case QA_CIN_PLAYBACK_ENDED: return "ended";
    case QA_CIN_PLAYBACK_LOOPED: return "looped";
  }
  return "playing";
}

void qa_cin_playback_tick_dispose(qa_cin_playback_tick *tick) {
  if (tick->has_frame) qa_cinematic_frame_dispose(&tick->frame);
  memset(tick, 0, sizeof *tick);
}

static int64_t milliseconds(qa_media_clock clock) {
  double time = clock.sample(clock.ctx);
  if (!isfinite(time) || time < 0 || time > 0x7fffffff) {
    qa_range_error("CIN clock requires nonnegative signed milliseconds");
  }
  return (int64_t)trunc(time);
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

static qa_save_value *save_frame(bool has, const qa_cin_frame *frame) {
  if (!has) return qa_save_value_null();
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  qa_save_value *audio = NULL;
  bool ok = true;
  if (!frame->has_audio) {
    audio = qa_save_value_null();
    ok = audio != NULL;
  } else {
    audio = qa_save_value_object();
    qa_save_value *samples = qa_save_value_array();
    ok = audio != NULL && samples != NULL;
    for (size_t i = 0; ok && i < frame->audio.sample_count; i++) {
      double sample = frame->audio.is_s16
                          ? frame->audio.samples_s16[i]
                          : frame->audio.samples_u8[i];
      qa_save_value *child = qa_save_value_number(sample);
      ok = child != NULL && qa_save_value_push(samples, child);
    }
    ok = ok && set_bool(audio, "signed", frame->audio.is_s16) &&
         set_number(audio, "channels", frame->audio.channels) &&
         set_value(audio, "samples", samples) &&
         set_number(audio, "sampleRate",
                    (double)frame->audio.sample_rate) &&
         set_number(audio, "sourceSample",
                    (double)frame->audio.source_sample);
  }
  ok = ok && set_value(object, "kind", qa_save_value_string("frame")) &&
       set_number(object, "index", (double)frame->index) &&
       set_number(object, "time", frame->time) &&
       set_value(object, "pixels",
                 qa_save_value_bytes(frame->pixels, frame->pixels_length)) &&
       set_value(object, "palette", qa_save_value_bytes(frame->palette, 768)) &&
       set_value(object, "audio", audio);
  if (!ok) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

/* False when the value is null; on true *out owns its buffers. */
static bool read_frame(const qa_save_reader *reader, int64_t pixels,
                       qa_cin_frame *out) {
  memset(out, 0, sizeof *out);
  if (qa_save_reader_is_null(reader)) return false;
  qa_save_reader f;
  qa_cin_frame frame;
  memset(&frame, 0, sizeof frame);
  read_field(reader, "pixels", &f);
  size_t pixels_length = 0;
  const uint8_t *image = qa_save_reader_bytes(&f, &pixels_length);
  frame.pixels = (uint8_t *)malloc(pixels_length == 0 ? 1 : pixels_length);
  if (frame.pixels == NULL) media_error("out of memory");
  memcpy(frame.pixels, image, pixels_length);
  frame.pixels_length = pixels_length;
  qa_save_reader_destroy(&f);
  read_field(reader, "palette", &f);
  size_t palette_length = 0;
  const uint8_t *palette = qa_save_reader_bytes(&f, &palette_length);
  uint8_t palette_copy[768] = {0};
  if (palette_length == 768) memcpy(palette_copy, palette, 768);
  qa_save_reader_destroy(&f);
  if ((int64_t)pixels_length != pixels || palette_length != 768) {
    qa_save_reader_fail(reader, "CIN frame size differs");
  }
  memcpy(frame.palette, palette_copy, 768);
  read_field(reader, "index", &f);
  frame.index = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(reader, "time", &f);
  frame.time = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  read_field(reader, "audio", &f);
  if (!qa_save_reader_is_null(&f)) {
    qa_save_reader g;
    read_field(&f, "signed", &g);
    bool is_signed = qa_save_reader_boolean(&g);
    qa_save_reader_destroy(&g);
    static const double channel_choices[2] = {1, 2};
    read_field(&f, "channels", &g);
    int channels = (int)qa_save_reader_choice_number(&g, channel_choices, 2);
    qa_save_reader_destroy(&g);
    read_field(&f, "samples", &g);
    size_t count = qa_save_reader_list_count(&g);
    double *values = (double *)malloc((count == 0 ? 1 : count) * sizeof(double));
    if (values == NULL) media_error("out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&g, i, &item)) media_error("out of memory");
      double sample =
          qa_save_reader_integer_min(&item, is_signed ? -32768 : 0);
      if (sample > (is_signed ? 32767 : 255)) {
        qa_save_reader_fail(&item, "PCM sample outside range");
      }
      values[i] = sample;
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&g);
    if (count % (size_t)channels != 0) {
      qa_save_reader_fail(&f, "partial PCM frame");
    }
    frame.has_audio = true;
    frame.audio.is_s16 = is_signed;
    frame.audio.channels = channels;
    frame.audio.sample_count = count;
    if (is_signed) {
      frame.audio.samples_s16 =
          (int16_t *)malloc((count == 0 ? 1 : count) * sizeof(int16_t));
      if (frame.audio.samples_s16 == NULL) media_error("out of memory");
      for (size_t i = 0; i < count; i++) {
        frame.audio.samples_s16[i] = (int16_t)values[i];
      }
    } else {
      frame.audio.samples_u8 =
          (uint8_t *)malloc(count == 0 ? 1 : count);
      if (frame.audio.samples_u8 == NULL) media_error("out of memory");
      for (size_t i = 0; i < count; i++) {
        frame.audio.samples_u8[i] = (uint8_t)values[i];
      }
    }
    free(values);
    read_field(&f, "sampleRate", &g);
    frame.audio.sample_rate = (int64_t)qa_save_reader_integer_min(&g, 1);
    qa_save_reader_destroy(&g);
    read_field(&f, "sourceSample", &g);
    frame.audio.source_sample = (int64_t)qa_save_reader_integer_min(&g, 0);
    qa_save_reader_destroy(&g);
  }
  qa_save_reader_destroy(&f);
  *out = frame;
  return true;
}

/* Reads the next decoder frame, emitting its audio. Sets *has. */
static bool playback_read(qa_cin_playback *playback, qa_cin_frame *out,
                          bool *has) {
  qa_cin_frame frame;
  if (!qa_cin_decoder_next(&playback->decoder, &frame)) return false;
  if (frame.is_end) {
    *has = false;
    memset(out, 0, sizeof *out);
    return true;
  }
  if (frame.has_audio && !playback->options.silent) {
    double source_time = (double)frame.audio.source_sample * 1000.0 /
                         (double)frame.audio.sample_rate;
    qa_cinematic_audio audio;
    memset(&audio, 0, sizeof audio);
    audio.is_u8 = !frame.audio.is_s16;
    audio.samples_s16 = frame.audio.samples_s16;
    audio.samples_u8 = frame.audio.samples_u8;
    audio.sample_count = frame.audio.sample_count;
    audio.channels = frame.audio.channels;
    audio.sample_rate = frame.audio.sample_rate;
    audio.source_sample = frame.audio.source_sample;
    audio.source_time = source_time;
    audio.time = (double)playback->epoch + source_time;
    audio.loop = playback->pass;
    audio.reset_stream = frame.audio.source_sample == 0;
    playback->options.on_audio(playback->options.on_audio_ctx, &audio);
  }
  *out = frame;
  *has = true;
  return true;
}

static void restore_checkpoint(qa_cin_playback *playback,
                               const qa_save_value *value) {
  qa_save_reader r;
  reader_init(&r, value, "cin-playback");
  qa_save_reader f;
  read_field(&r, "decoder", &f);
  qa_cin_decoder_restore_checkpoint(&playback->decoder, f.value);
  qa_save_reader_destroy(&f);
  read_field(&r, "epoch", &f);
  playback->epoch = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(&r, "pass", &f);
  playback->pass = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  static const char *const state_choices[4] = {"playing", "held", "ended",
                                               "looped"};
  read_field(&r, "state", &f);
  const char *state = qa_save_reader_choice_string(&f, state_choices, 4);
  playback->state = strcmp(state, "held") == 0 ? QA_CIN_PLAYBACK_HELD
                    : strcmp(state, "ended") == 0 ? QA_CIN_PLAYBACK_ENDED
                    : strcmp(state, "looped") == 0 ? QA_CIN_PLAYBACK_LOOPED
                                                   : QA_CIN_PLAYBACK_PLAYING;
  qa_save_reader_destroy(&f);
  int64_t pixels = playback->decoder.width * playback->decoder.height;
  read_field(&r, "picture", &f);
  playback->has_picture = read_frame(&f, pixels, &playback->picture);
  qa_save_reader_destroy(&f);
  read_field(&r, "pending", &f);
  playback->has_pending = read_frame(&f, pixels, &playback->pending);
  qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&r);
}

static bool playback_start(qa_cin_playback *playback,
                           const qa_save_value *checkpoint) {
  playback->epoch = milliseconds(playback->options.clock);
  if (checkpoint != NULL) {
    restore_checkpoint(playback, checkpoint);
  } else {
    if (!playback_read(playback, &playback->picture,
                       &playback->has_picture)) {
      return false;
    }
    if (!playback->has_picture) playback->state = QA_CIN_PLAYBACK_ENDED;
  }
  return true;
}

bool qa_cin_playback_init(qa_cin_playback *playback, qa_media_input *input,
                          const qa_cin_playback_options *options,
                          const qa_save_value *checkpoint) {
  memset(playback, 0, sizeof *playback);
  playback->options = *options;
  playback->state = QA_CIN_PLAYBACK_PLAYING;
  if (!qa_cin_decoder_init(&playback->decoder, input, options->source)) {
    return false;
  }
  if (!playback_start(playback, checkpoint)) {
    qa_cin_decoder_close(&playback->decoder);
    return false;
  }
  return true;
}

bool qa_cin_playback_init_bytes(qa_cin_playback *playback,
                                const uint8_t *data, size_t length,
                                const qa_cin_playback_options *options,
                                const qa_save_value *checkpoint) {
  memset(playback, 0, sizeof *playback);
  playback->options = *options;
  playback->state = QA_CIN_PLAYBACK_PLAYING;
  if (!qa_cin_decoder_init_bytes(&playback->decoder, data, length,
                                 options->source)) {
    return false;
  }
  if (!playback_start(playback, checkpoint)) {
    qa_cin_decoder_close(&playback->decoder);
    return false;
  }
  return true;
}

void qa_cin_playback_destroy(qa_cin_playback *playback) {
  if (playback->has_picture) qa_cin_frame_dispose(&playback->picture);
  if (playback->has_pending) qa_cin_frame_dispose(&playback->pending);
  qa_cin_decoder_destroy(&playback->decoder);
  memset(playback, 0, sizeof *playback);
}

qa_save_value *qa_cin_playback_capture_checkpoint(
    qa_cin_playback *playback) {
  qa_save_value *decoder =
      qa_cin_decoder_capture_checkpoint(&playback->decoder);
  qa_save_value *picture =
      save_frame(playback->has_picture, &playback->picture);
  qa_save_value *pending =
      save_frame(playback->has_pending, &playback->pending);
  qa_save_value *object = qa_save_value_object();
  if (decoder == NULL || picture == NULL || pending == NULL ||
      object == NULL) {
    qa_save_value_free(decoder);
    qa_save_value_free(picture);
    qa_save_value_free(pending);
    qa_save_value_free(object);
    return NULL;
  }
  bool ok = set_value(object, "decoder", decoder) &&
            set_number(object, "epoch", (double)playback->epoch) &&
            set_value(object, "picture", picture) &&
            set_value(object, "pending", pending) &&
            set_value(object, "state",
                      qa_save_value_string(
                          qa_cin_playback_status_name(playback->state))) &&
            set_number(object, "pass", (double)playback->pass);
  if (!ok) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

void qa_cin_playback_dimensions(const qa_cin_playback *playback,
                                int64_t *width, int64_t *height) {
  *width = playback->decoder.width;
  *height = playback->decoder.height;
}

bool qa_cin_playback_current_frame(qa_cin_playback *playback,
                                   qa_cinematic_frame *out) {
  memset(out, 0, sizeof *out);
  if (!playback->has_picture) return false;
  /* Q2 updates its global palette while prefetching, before
   * drawing the current indices. */
  uint8_t palette[768];
  qa_cin_decoder_palette(&playback->decoder, palette);
  uint8_t *rgba = qa_cin_rgba(playback->picture.pixels,
                              playback->picture.pixels_length, palette, 768);
  if (rgba == NULL) return false;
  out->rgba = rgba;
  out->rgba_length = playback->picture.pixels_length * 4;
  out->width = playback->decoder.width;
  out->height = playback->decoder.height;
  out->index = playback->picture.index;
  out->source_time = playback->picture.time;
  out->time = (double)playback->epoch + playback->picture.time;
  out->loop = playback->pass;
  return true;
}

bool qa_cin_playback_restart(qa_cin_playback *playback,
                             qa_media_clock clock) {
  qa_cin_decoder_rewind(&playback->decoder);
  playback->epoch = milliseconds(clock);
  if (playback->has_pending) {
    qa_cin_frame_dispose(&playback->pending);
    playback->has_pending = false;
  }
  playback->state = QA_CIN_PLAYBACK_PLAYING;
  if (playback->has_picture) {
    qa_cin_frame_dispose(&playback->picture);
    playback->has_picture = false;
  }
  if (!playback_read(playback, &playback->picture, &playback->has_picture)) {
    return false;
  }
  if (!playback->has_picture) playback->state = QA_CIN_PLAYBACK_ENDED;
  return true;
}

bool qa_cin_playback_run(qa_cin_playback *playback, qa_media_clock clock,
                         qa_media_focus focus, qa_cin_playback_tick *out) {
  memset(out, 0, sizeof *out);
  if (playback->state == QA_CIN_PLAYBACK_ENDED ||
      playback->state == QA_CIN_PLAYBACK_HELD) {
    out->status = playback->state;
    out->update_kind = QA_CIN_UPDATE_UNCHANGED;
    return true;
  }
  int64_t time = milliseconds(clock);
  int64_t decoded = qa_cin_decoder_next_frame_index(&playback->decoder);
  if (focus != QA_MEDIA_FOCUS_GAME) {
    playback->epoch = time - decoded * 1000 / 14;
    out->status = playback->state;
    out->update_kind = QA_CIN_UPDATE_UNCHANGED;
    return true;
  }
  int64_t frame = (time - playback->epoch) * 14 / 1000;
  if (frame <= decoded) {
    out->status = playback->state;
    out->update_kind = QA_CIN_UPDATE_UNCHANGED;
    return true;
  }
  if (frame > decoded + 1) {
    if (playback->options.developer_print != NULL) {
      char message[96];
      snprintf(message, sizeof message, "Dropped frame: %lld > %lld\n",
               (long long)frame, (long long)(decoded + 1));
      playback->options.developer_print(
          playback->options.developer_print_ctx, message);
    }
    playback->epoch = time - decoded * 1000 / 14;
  }
  qa_cin_frame previous;
  bool has_previous = playback->has_picture;
  if (has_previous) {
    previous = playback->picture; /* ownership moves */
  } else {
    memset(&previous, 0, sizeof previous);
  }
  playback->picture = playback->pending;
  playback->has_picture = playback->has_pending;
  memset(&playback->pending, 0, sizeof playback->pending);
  playback->has_pending = false;
  if (!playback_read(playback, &playback->pending, &playback->has_pending)) {
    if (has_previous) qa_cin_frame_dispose(&previous);
    return false;
  }
  if (!playback->has_pending) {
    if (playback->options.hold) {
      if (!playback->has_picture && has_previous) {
        playback->picture = previous;
        playback->has_picture = true;
        has_previous = false;
      }
      playback->state = QA_CIN_PLAYBACK_HELD;
    } else if (playback->options.loop) {
      playback->pass++;
      if (playback->has_picture) {
        qa_cin_frame_dispose(&playback->picture);
        playback->has_picture = false;
      }
      if (!qa_cin_playback_restart(playback, clock)) {
        if (has_previous) qa_cin_frame_dispose(&previous);
        return false;
      }
      playback->state = QA_CIN_PLAYBACK_LOOPED;
    } else {
      if (playback->has_picture) {
        qa_cin_frame_dispose(&playback->picture);
        playback->has_picture = false;
      }
      playback->state = QA_CIN_PLAYBACK_ENDED;
    }
  } else {
    playback->state = QA_CIN_PLAYBACK_PLAYING;
  }
  if (has_previous) qa_cin_frame_dispose(&previous);
  out->status = playback->state;
  out->update_kind = QA_CIN_UPDATE_FRAME;
  out->has_frame = qa_cin_playback_current_frame(playback, &out->frame);
  return true;
}

void qa_cin_playback_close(qa_cin_playback *playback) {
  playback->state = QA_CIN_PLAYBACK_ENDED;
  qa_cin_decoder_close(&playback->decoder);
}
