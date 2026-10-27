/* Port of src/media/playback.ts. GPL-2.0-or-later. */
#include "qa/media/playback.h"

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

const char *qa_cinematic_format_name(qa_cinematic_format format) {
  switch (format) {
    case QA_CINEMATIC_FORMAT_ROQ: return "roq";
    case QA_CINEMATIC_FORMAT_CIN: return "cin";
    case QA_CINEMATIC_FORMAT_OGV: return "ogv";
    case QA_CINEMATIC_FORMAT_IMAGE: return "image";
  }
  return "roq";
}

const char *qa_cinematic_decoder_status_name(
    qa_cinematic_decoder_status status) {
  switch (status) {
    case QA_CINEMATIC_DECODER_PLAYING: return "playing";
    case QA_CINEMATIC_DECODER_PAUSED: return "paused";
    case QA_CINEMATIC_DECODER_HELD: return "held";
    case QA_CINEMATIC_DECODER_ENDED: return "ended";
    case QA_CINEMATIC_DECODER_STOPPED: return "stopped";
    case QA_CINEMATIC_DECODER_LOOPED: return "looped";
  }
  return "playing";
}

qa_media_input *qa_cinematic_source_open(const qa_cinematic_source *source) {
  if (source->open != NULL) return source->open(source->open_ctx);
  return qa_media_bytes(source->bytes, source->bytes_length, source->source);
}

void qa_cinematic_source_dispose(qa_cinematic_source *source) {
  if (source->rgba_owned) free((void *)source->rgba);
  memset(source, 0, sizeof *source);
}

qa_cinematic_source qa_cinematic_bytes(qa_cinematic_format format,
                                       const uint8_t *bytes, size_t length,
                                       const char *source) {
  qa_cinematic_source out;
  memset(&out, 0, sizeof out);
  out.format = format;
  out.source = source == NULL ? "<cinematic>" : source;
  out.bytes = bytes;
  out.bytes_length = length;
  return out;
}

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

static double read_wall(qa_playback_clock *clock) {
  double time = clock->wall.sample(clock->wall.ctx);
  if (!isfinite(time) || time < 0) {
    qa_range_error("Media clock must be finite and nonnegative");
  }
  return time;
}

void qa_playback_clock_init(qa_playback_clock *clock, qa_media_clock wall,
                            const qa_save_value *checkpoint) {
  memset(clock, 0, sizeof *clock);
  clock->wall = wall;
  clock->start = read_wall(clock);
  clock->offset = 0;
  if (checkpoint != NULL) {
    qa_save_reader r;
    if (!qa_save_reader_init(&r, checkpoint, "cinematic-clock")) {
      media_error("out of memory");
    }
    qa_save_reader f;
    if (!qa_save_reader_field(&r, "elapsed", &f)) {
      media_error("out of memory");
    }
    double elapsed = qa_save_reader_finite(&f);
    qa_save_reader_destroy(&f);
    if (elapsed < 0) qa_save_reader_fail(&r, "negative elapsed time");
    clock->offset = elapsed;
    if (!qa_save_reader_field(&r, "paused", &f)) {
      media_error("out of memory");
    }
    bool paused = qa_save_reader_boolean(&f);
    qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&r);
    if (paused) {
      clock->has_paused_at = true;
      clock->paused_at = clock->start;
    }
  }
}

double qa_playback_clock_sample(qa_playback_clock *clock) {
  double now = clock->has_paused_at ? clock->paused_at : read_wall(clock);
  double sample = clock->offset + now - clock->start - clock->paused_duration;
  return sample < 0 ? 0 : sample;
}

void qa_playback_clock_pause(qa_playback_clock *clock, bool paused) {
  if (paused && !clock->has_paused_at) {
    clock->paused_at = read_wall(clock);
    clock->has_paused_at = true;
  } else if (!paused && clock->has_paused_at) {
    clock->paused_duration += read_wall(clock) - clock->paused_at;
    clock->has_paused_at = false;
  }
}

qa_save_value *qa_playback_clock_capture_checkpoint(
    qa_playback_clock *clock) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  qa_save_value *elapsed =
      qa_save_value_number(qa_playback_clock_sample(clock));
  qa_save_value *paused = qa_save_value_boolean(clock->has_paused_at);
  bool ok = elapsed != NULL && paused != NULL &&
            qa_save_value_set(object, "elapsed", elapsed) &&
            qa_save_value_set(object, "paused", paused);
  if (!ok) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

static double playback_clock_sample_ctx(void *ctx) {
  return qa_playback_clock_sample((qa_playback_clock *)ctx);
}

qa_media_clock qa_playback_clock_as_media_clock(qa_playback_clock *clock) {
  qa_media_clock media = {clock, playback_clock_sample_ctx};
  return media;
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

/* Adapters from the per-format audio/print shapes to CinematicOptions. */
static void adapt_cin_audio(void *ctx, const qa_cinematic_audio *audio) {
  qa_cinematic_playback *playback = (qa_cinematic_playback *)ctx;
  playback->options.on_audio(playback->options.on_audio_ctx, audio,
                             playback->target);
}

static void adapt_roq_audio(void *ctx, const qa_roq_playback_audio *audio) {
  qa_cinematic_playback *playback = (qa_cinematic_playback *)ctx;
  qa_cinematic_audio out;
  memset(&out, 0, sizeof out);
  out.is_u8 = false;
  out.samples_s16 = audio->samples;
  out.sample_count = audio->sample_count;
  out.channels = audio->channels;
  out.sample_rate = audio->sample_rate;
  out.source_sample = audio->source_sample;
  out.source_time = audio->source_time;
  out.time = audio->time;
  out.loop = audio->loop;
  out.reset_stream = audio->reset_stream;
  playback->options.on_audio(playback->options.on_audio_ctx, &out,
                             playback->target);
}

static void adapt_print(void *ctx, const char *text) {
  qa_cinematic_playback *playback = (qa_cinematic_playback *)ctx;
  if (playback->options.developer_print != NULL) {
    playback->options.developer_print(playback->options.developer_print_ctx,
                                      text);
  }
}

static void adapt_before_reset(void *ctx) {
  qa_cinematic_playback *playback = (qa_cinematic_playback *)ctx;
  playback->options.on_audio_reset(playback->options.on_audio_reset_ctx,
                                   playback->target);
}

static qa_media_input *roq_open_ctx(void *ctx) {
  return qa_cinematic_source_open((const qa_cinematic_source *)ctx);
}

static qa_cinematic_decoder_status decoder_status_of(const char *name) {
  if (strcmp(name, "paused") == 0) return QA_CINEMATIC_DECODER_PAUSED;
  if (strcmp(name, "held") == 0) return QA_CINEMATIC_DECODER_HELD;
  if (strcmp(name, "ended") == 0) return QA_CINEMATIC_DECODER_ENDED;
  if (strcmp(name, "stopped") == 0) return QA_CINEMATIC_DECODER_STOPPED;
  if (strcmp(name, "looped") == 0) return QA_CINEMATIC_DECODER_LOOPED;
  return QA_CINEMATIC_DECODER_PLAYING;
}

static qa_cinematic_status status_of(const char *name) {
  if (strcmp(name, "paused") == 0) return QA_CINEMATIC_PAUSED;
  if (strcmp(name, "held") == 0) return QA_CINEMATIC_HELD;
  if (strcmp(name, "ended") == 0) return QA_CINEMATIC_ENDED;
  if (strcmp(name, "stopped") == 0) return QA_CINEMATIC_STOPPED;
  return QA_CINEMATIC_PLAYING;
}

bool qa_cinematic_playback_init(qa_cinematic_playback *playback,
                                const qa_cinematic_source *source,
                                const qa_cinematic_options *options,
                                const qa_save_value *checkpoint) {
  memset(playback, 0, sizeof *playback);
  playback->options = *options;
  playback->target = options->target;
  playback->format = source->format;
  playback->state = QA_CINEMATIC_PLAYING;
  playback->decoder_status = QA_CINEMATIC_DECODER_PLAYING;
  playback->source = dup_text(source->source);
  if (playback->source == NULL) return false;
  const qa_save_value *saved_clock = NULL;
  const qa_save_value *saved_decoder = NULL;
  bool has_saved = checkpoint != NULL;
  qa_save_reader saved;
  memset(&saved, 0, sizeof saved);
  if (has_saved) {
    reader_init(&saved, checkpoint, "cinematic");
    qa_save_reader f;
    read_field(&saved, "version", &f);
    qa_save_reader_literal_number(&f, 1);
    qa_save_reader_destroy(&f);
    read_field(&saved, "source", &f);
    qa_save_reader_literal_string(&f, source->source);
    qa_save_reader_destroy(&f);
    read_field(&saved, "format", &f);
    qa_save_reader_literal_string(&f,
                                  qa_cinematic_format_name(source->format));
    qa_save_reader_destroy(&f);
    read_field(&saved, "loop", &f);
    qa_save_reader_literal_boolean(&f, options->loop);
    qa_save_reader_destroy(&f);
    read_field(&saved, "hold", &f);
    qa_save_reader_literal_boolean(&f, options->hold);
    qa_save_reader_destroy(&f);
    read_field(&saved, "silent", &f);
    qa_save_reader_literal_boolean(&f, options->silent);
    qa_save_reader_destroy(&f);
    read_field(&saved, "shader", &f);
    qa_save_reader_literal_boolean(
        &f, options->target.kind == QA_CINEMATIC_TARGET_MATERIAL);
    qa_save_reader_destroy(&f);
    static const char *const state_choices[5] = {"playing", "paused", "held",
                                                 "ended", "stopped"};
    read_field(&saved, "state", &f);
    playback->state =
        status_of(qa_save_reader_choice_string(&f, state_choices, 5));
    qa_save_reader_destroy(&f);
    static const char *const decoder_choices[6] = {
        "playing", "paused", "held", "ended", "stopped", "looped"};
    read_field(&saved, "decoderStatus", &f);
    playback->decoder_status = decoder_status_of(
        qa_save_reader_choice_string(&f, decoder_choices, 6));
    qa_save_reader_destroy(&f);
    read_field(&saved, "picture", &f);
    playback->has_picture = qa_read_cinematic_frame(&f, &playback->picture);
    qa_save_reader_destroy(&f);
    read_field(&saved, "dirty", &f);
    playback->dirty = qa_save_reader_boolean(&f);
    qa_save_reader_destroy(&f);
    read_field(&saved, "frameRevision", &f);
    playback->frame_revision = (int64_t)qa_save_reader_integer_min(&f, 0);
    qa_save_reader_destroy(&f);
    read_field(&saved, "completed", &f);
    playback->completed = qa_save_reader_boolean(&f);
    qa_save_reader_destroy(&f);
    read_field(&saved, "closed", &f);
    playback->closed = qa_save_reader_boolean(&f);
    qa_save_reader_destroy(&f);
    saved_clock = qa_save_value_get(checkpoint, "clock");
    saved_decoder = qa_save_value_get(checkpoint, "decoder");
  }
  qa_playback_clock_init(&playback->clock, options->clock, saved_clock);
  qa_media_clock clock = qa_playback_clock_as_media_clock(&playback->clock);
  bool ok = true;
  switch (source->format) {
    case QA_CINEMATIC_FORMAT_OGV: {
      qa_media_input *input = qa_cinematic_source_open(source);
      if (input == NULL) {
        ok = false;
        break;
      }
      uint8_t *bytes = qa_read_media(input, 0, input->byte_length);
      size_t length = (size_t)input->byte_length;
      qa_media_input_close(input);
      if (bytes == NULL && length != 0) {
        ok = false;
        break;
      }
      qa_ogv_playback_options ogv_options;
      memset(&ogv_options, 0, sizeof ogv_options);
      ogv_options.clock = clock;
      ogv_options.loop = options->loop;
      ogv_options.hold = options->hold;
      ogv_options.silent = options->silent;
      ogv_options.on_audio_ctx = playback;
      ogv_options.on_audio = adapt_cin_audio;
      ok = qa_ogv_playback_init(&playback->movie.ogv, bytes, length,
                                &ogv_options, saved_decoder);
      free(bytes);
      if (ok && !has_saved) {
        const qa_cinematic_frame *frame =
            qa_ogv_playback_current_frame(&playback->movie.ogv);
        playback->has_picture =
            qa_copy_cinematic_frame(frame, &playback->picture);
        playback->dirty = true;
      }
      break;
    }
    case QA_CINEMATIC_FORMAT_ROQ: {
      playback->movie.roq.scratch =
          (qa_roq_scratch *)malloc(sizeof(qa_roq_scratch));
      if (playback->movie.roq.scratch == NULL ||
          !qa_roq_scratch_init(playback->movie.roq.scratch)) {
        free(playback->movie.roq.scratch);
        playback->movie.roq.scratch = NULL;
        ok = false;
        break;
      }
      qa_roq_stream *stream = qa_roq_stream_open(
          roq_open_ctx, (void *)source, source->source,
          playback->movie.roq.scratch->file, QA_ROQ_SCRATCH_FILE_LENGTH);
      if (stream == NULL) {
        /* RoqStream.open yields undefined for a null/empty input;
         * an OOM there is indistinguishable and takes this path. */
        media_error("Empty cinematic: %s", source->source);
      }
      playback->movie.roq.stream = stream;
      qa_roq_playback_options roq_options;
      memset(&roq_options, 0, sizeof roq_options);
      roq_options.clock = clock;
      roq_options.source = source->source;
      roq_options.loop = options->loop;
      roq_options.hold = options->hold;
      roq_options.silent = options->silent;
      roq_options.shader =
          options->target.kind == QA_CINEMATIC_TARGET_MATERIAL;
      roq_options.ctx = playback;
      roq_options.on_audio = adapt_roq_audio;
      roq_options.developer_print = adapt_print;
      roq_options.before_raw_stream_reset = adapt_before_reset;
      roq_options.scratch = playback->movie.roq.scratch;
      ok = qa_roq_playback_init_stream(&playback->movie.roq.playback,
                                       stream, &roq_options, saved_decoder);
      if (!ok) {
        qa_roq_stream_destroy(stream);
        playback->movie.roq.stream = NULL;
      }
      break;
    }
    case QA_CINEMATIC_FORMAT_CIN: {
      qa_media_input *input = qa_cinematic_source_open(source);
      if (input == NULL) {
        ok = false;
        break;
      }
      qa_cin_playback_options cin_options;
      memset(&cin_options, 0, sizeof cin_options);
      cin_options.clock = clock;
      cin_options.source = source->source;
      cin_options.loop = options->loop;
      cin_options.hold = options->hold;
      cin_options.silent = options->silent;
      cin_options.on_audio_ctx = playback;
      cin_options.on_audio = adapt_cin_audio;
      cin_options.developer_print_ctx = playback;
      cin_options.developer_print = adapt_print;
      /* CinPlayback owns the input even when init fails. */
      ok = qa_cin_playback_init(&playback->movie.cin, input, &cin_options,
                                saved_decoder);
      if (ok && !has_saved) {
        playback->has_picture = qa_cin_playback_current_frame(
            &playback->movie.cin, &playback->picture);
        playback->dirty = true;
      }
      break;
    }
    case QA_CINEMATIC_FORMAT_IMAGE: {
      if (source->width <= 0 || source->height <= 0 ||
          source->rgba_length !=
              (size_t)(source->width * source->height * 4)) {
        qa_range_error("Invalid cinematic still image");
      }
      qa_cinematic_frame frame;
      memset(&frame, 0, sizeof frame);
      frame.rgba = (uint8_t *)malloc(
          source->rgba_length == 0 ? 1 : source->rgba_length);
      if (frame.rgba == NULL) {
        ok = false;
        break;
      }
      memcpy(frame.rgba, source->rgba, source->rgba_length);
      frame.rgba_length = source->rgba_length;
      frame.width = source->width;
      frame.height = source->height;
      playback->movie.image = frame;
      if (!has_saved) {
        playback->has_picture =
            qa_copy_cinematic_frame(&frame, &playback->picture);
        playback->dirty = true;
        playback->state = QA_CINEMATIC_HELD;
        qa_playback_clock_pause(&playback->clock, true);
      }
      break;
    }
  }
  if (has_saved) qa_save_reader_destroy(&saved);
  return ok;
}

static void release_input(qa_cinematic_playback *playback) {
  if (playback->closed) return;
  playback->closed = true;
  if (playback->format == QA_CINEMATIC_FORMAT_ROQ) {
    qa_roq_stream_close(playback->movie.roq.stream);
  } else if (playback->format == QA_CINEMATIC_FORMAT_CIN) {
    qa_cin_playback_close(&playback->movie.cin);
  } else if (playback->format == QA_CINEMATIC_FORMAT_OGV) {
    qa_ogv_playback_close(&playback->movie.ogv);
  }
}

static void finish(qa_cinematic_playback *playback,
                   qa_cinematic_end_reason reason) {
  if (playback->completed) return;
  playback->completed = true;
  qa_playback_clock_pause(&playback->clock, true);
  release_input(playback);
  playback->options.on_audio_reset(playback->options.on_audio_reset_ctx,
                                   playback->target);
  playback->options.on_complete(playback->options.on_complete_ctx, reason,
                                playback->target);
}

void qa_cinematic_playback_destroy(qa_cinematic_playback *playback) {
  switch (playback->format) {
    case QA_CINEMATIC_FORMAT_ROQ:
      qa_roq_playback_destroy(&playback->movie.roq.playback);
      qa_roq_stream_destroy(playback->movie.roq.stream);
      if (playback->movie.roq.scratch != NULL) {
        qa_roq_scratch_destroy(playback->movie.roq.scratch);
        free(playback->movie.roq.scratch);
      }
      break;
    case QA_CINEMATIC_FORMAT_CIN:
      qa_cin_playback_destroy(&playback->movie.cin);
      break;
    case QA_CINEMATIC_FORMAT_OGV:
      qa_ogv_playback_destroy(&playback->movie.ogv);
      break;
    case QA_CINEMATIC_FORMAT_IMAGE:
      qa_cinematic_frame_dispose(&playback->movie.image);
      break;
  }
  if (playback->has_picture) qa_cinematic_frame_dispose(&playback->picture);
  free(playback->source);
  memset(playback, 0, sizeof *playback);
}

qa_save_value *qa_cinematic_playback_capture_checkpoint(
    qa_cinematic_playback *playback) {
  qa_save_value *clock =
      qa_playback_clock_capture_checkpoint(&playback->clock);
  qa_save_value *picture = qa_cinematic_frame_to_save(
      playback->has_picture ? &playback->picture : NULL);
  qa_save_value *decoder = NULL;
  switch (playback->format) {
    case QA_CINEMATIC_FORMAT_ROQ:
      decoder = qa_roq_playback_capture_checkpoint(
          &playback->movie.roq.playback);
      break;
    case QA_CINEMATIC_FORMAT_CIN:
      decoder = qa_cin_playback_capture_checkpoint(&playback->movie.cin);
      break;
    case QA_CINEMATIC_FORMAT_OGV:
      decoder = qa_ogv_playback_capture_checkpoint(&playback->movie.ogv);
      break;
    case QA_CINEMATIC_FORMAT_IMAGE:
      decoder = qa_save_value_null();
      break;
  }
  qa_save_value *object = qa_save_value_object();
  if (clock == NULL || picture == NULL || decoder == NULL ||
      object == NULL) {
    qa_save_value_free(clock);
    qa_save_value_free(picture);
    qa_save_value_free(decoder);
    qa_save_value_free(object);
    return NULL;
  }
  bool ok = set_number(object, "version", 1) &&
            set_value(object, "source", qa_save_value_string(playback->source)) &&
            set_value(object, "format",
                      qa_save_value_string(
                          qa_cinematic_format_name(playback->format))) &&
            set_bool(object, "loop", playback->options.loop) &&
            set_bool(object, "hold", playback->options.hold) &&
            set_bool(object, "silent", playback->options.silent) &&
            set_bool(object, "shader",
                     playback->target.kind == QA_CINEMATIC_TARGET_MATERIAL) &&
            set_value(object, "clock", clock) &&
            set_value(object, "state",
                      qa_save_value_string(
                          qa_cinematic_status_name(playback->state))) &&
            set_value(object, "decoderStatus",
                      qa_save_value_string(qa_cinematic_decoder_status_name(
                          playback->decoder_status))) &&
            set_value(object, "picture", picture) &&
            set_bool(object, "dirty", playback->dirty) &&
            set_number(object, "frameRevision",
                       (double)playback->frame_revision) &&
            set_bool(object, "completed", playback->completed) &&
            set_bool(object, "closed", playback->closed) &&
            set_value(object, "decoder", decoder);
  if (!ok) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

qa_cinematic_status qa_cinematic_playback_status(
    const qa_cinematic_playback *playback) {
  return playback->state;
}

qa_cinematic_decoder_status qa_cinematic_playback_source_status(
    const qa_cinematic_playback *playback) {
  if (playback->state != QA_CINEMATIC_PLAYING) {
    switch (playback->state) {
      case QA_CINEMATIC_PAUSED: return QA_CINEMATIC_DECODER_PAUSED;
      case QA_CINEMATIC_HELD: return QA_CINEMATIC_DECODER_HELD;
      case QA_CINEMATIC_ENDED: return QA_CINEMATIC_DECODER_ENDED;
      case QA_CINEMATIC_STOPPED: return QA_CINEMATIC_DECODER_STOPPED;
      case QA_CINEMATIC_PLAYING: break;
    }
  }
  return playback->decoder_status;
}

int64_t qa_cinematic_playback_revision(
    const qa_cinematic_playback *playback) {
  return playback->frame_revision;
}

double qa_cinematic_playback_time_milliseconds(
    qa_cinematic_playback *playback) {
  return qa_playback_clock_sample(&playback->clock);
}

qa_cinematic_timeline qa_cinematic_playback_timeline(
    qa_cinematic_playback *playback) {
  double elapsed = qa_playback_clock_sample(&playback->clock);
  qa_cinematic_timeline timeline;
  timeline.source = playback->source;
  timeline.elapsed_milliseconds = elapsed;
  timeline.status = playback->state;
  timeline.loop = playback->has_picture ? playback->picture.loop : 0;
  if (!playback->has_picture) {
    timeline.source_time_milliseconds = elapsed;
  } else {
    double delta = elapsed - playback->picture.time;
    timeline.source_time_milliseconds =
        playback->picture.source_time + (delta < 0 ? 0 : delta);
  }
  return timeline;
}

bool qa_cinematic_playback_current_frame(
    const qa_cinematic_playback *playback, qa_cinematic_frame *out) {
  memset(out, 0, sizeof *out);
  if (!playback->has_picture) return false;
  return qa_copy_cinematic_frame(&playback->picture, out);
}

bool qa_cinematic_playback_tick(qa_cinematic_playback *playback,
                                qa_cinematic_tick *out) {
  memset(out, 0, sizeof *out);
  bool changed = playback->dirty;
  playback->dirty = false;
  if (playback->state == QA_CINEMATIC_PLAYING &&
      playback->format != QA_CINEMATIC_FORMAT_IMAGE) {
    qa_media_clock clock = qa_playback_clock_as_media_clock(&playback->clock);
    const char *status = "playing";
    bool is_frame = false;
    bool has_frame = false;
    qa_cinematic_frame frame;
    memset(&frame, 0, sizeof frame);
    bool ok = true;
    if (playback->format == QA_CINEMATIC_FORMAT_ROQ) {
      qa_roq_playback_tick tick;
      ok = qa_roq_playback_run(&playback->movie.roq.playback, clock, &tick);
      if (ok) {
        status = qa_roq_playback_status_name(tick.status);
        is_frame = tick.has_frame;
        has_frame = tick.has_frame;
        if (has_frame) frame = tick.frame;
        else qa_roq_playback_tick_dispose(&tick);
      }
    } else if (playback->format == QA_CINEMATIC_FORMAT_CIN) {
      qa_cin_playback_tick tick;
      ok = qa_cin_playback_run(&playback->movie.cin, clock,
                               QA_MEDIA_FOCUS_GAME, &tick);
      if (ok) {
        status = qa_cin_playback_status_name(tick.status);
        is_frame = tick.update_kind == QA_CIN_UPDATE_FRAME;
        has_frame = tick.has_frame;
        if (is_frame && has_frame) frame = tick.frame;
        else qa_cin_playback_tick_dispose(&tick);
      }
    } else {
      qa_ogv_playback_tick tick;
      ok = qa_ogv_playback_run(&playback->movie.ogv, clock, &tick);
      if (ok) {
        status = qa_ogv_playback_status_name(tick.status);
        is_frame = tick.is_frame;
        has_frame = tick.has_frame;
        if (is_frame && has_frame) frame = tick.frame;
        else qa_ogv_playback_tick_dispose(&tick);
      }
    }
    if (!ok) return false;
    playback->decoder_status = decoder_status_of(status);
    if (is_frame) {
      if (playback->has_picture) qa_cinematic_frame_dispose(&playback->picture);
      playback->picture = frame;
      playback->has_picture = has_frame;
      playback->frame_revision++;
      changed = true;
    }
    if (strcmp(status, "held") == 0) {
      playback->state = QA_CINEMATIC_HELD;
      qa_playback_clock_pause(&playback->clock, true);
    } else if (strcmp(status, "ended") == 0) {
      playback->state = QA_CINEMATIC_ENDED;
      finish(playback, QA_CINEMATIC_END_FINISHED);
    }
  }
  out->status = playback->state;
  out->changed = changed;
  out->has_frame = qa_cinematic_playback_current_frame(playback, &out->frame);
  return !playback->has_picture || out->has_frame;
}

void qa_cinematic_playback_pause(qa_cinematic_playback *playback,
                                 bool paused) {
  if ((paused && playback->state != QA_CINEMATIC_PLAYING) ||
      (!paused && playback->state != QA_CINEMATIC_PAUSED)) {
    return;
  }
  qa_playback_clock_pause(&playback->clock, paused);
  playback->state = paused ? QA_CINEMATIC_PAUSED : QA_CINEMATIC_PLAYING;
  playback->options.on_audio_pause(playback->options.on_audio_pause_ctx,
                                   paused, playback->target);
}

void qa_cinematic_playback_skip(qa_cinematic_playback *playback) {
  if (playback->completed) return;
  playback->state = QA_CINEMATIC_ENDED;
  finish(playback, QA_CINEMATIC_END_SKIPPED);
}

void qa_cinematic_playback_stop(qa_cinematic_playback *playback) {
  if (playback->completed) return;
  playback->state = QA_CINEMATIC_STOPPED;
  finish(playback, QA_CINEMATIC_END_STOPPED);
}

void qa_cinematic_playback_close(qa_cinematic_playback *playback) {
  qa_cinematic_playback_stop(playback);
  release_input(playback);
}
