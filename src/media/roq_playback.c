/* Port of src/media/roq-playback.ts. GPL-2.0-or-later. */
#include "qa/media/roq_playback.h"

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

const char *qa_roq_playback_status_name(qa_roq_playback_status status) {
  switch (status) {
    case QA_ROQ_PLAYBACK_PLAYING: return "playing";
    case QA_ROQ_PLAYBACK_HELD: return "held";
    case QA_ROQ_PLAYBACK_ENDED: return "ended";
    case QA_ROQ_PLAYBACK_LOOPED: return "looped";
  }
  return "playing";
}

void qa_roq_playback_tick_dispose(qa_roq_playback_tick *tick) {
  if (tick->has_frame) qa_cinematic_frame_dispose(&tick->frame);
  memset(tick, 0, sizeof *tick);
}

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

static double clock_time(double time) {
  if (!isfinite(time) || time < 0 || (float)time > 0x7fffffff) {
    qa_range_error(
        "Cinematic clock must fit finite nonnegative signed milliseconds");
  }
  return time;
}

static uint32_t epoch_of(qa_media_clock clock) {
  return (uint32_t)trunc(clock_time(clock.sample(clock.ctx)));
}

static bool set_value(qa_save_value *object, const char *key,
                      qa_save_value *child) {
  return child != NULL && qa_save_value_set(object, key, child);
}
static bool set_number(qa_save_value *object, const char *key, double value) {
  return set_value(object, key, qa_save_value_number(value));
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

static bool make_decoder(qa_roq_playback *playback) {
  qa_roq_decoder_options options;
  memset(&options, 0, sizeof options);
  options.has_end_policy = true;
  options.end_policy = QA_ROQ_END_CINEMATIC_LOOKAHEAD;
  options.silent = playback->silent;
  options.scratch = playback->scratch;
  if (playback->is_stream) {
    return qa_roq_decoder_init_stream(&playback->decoder, playback->stream,
                                      playback->source, &options);
  }
  return qa_roq_decoder_init_bytes(&playback->decoder, playback->data,
                                   playback->data_length, playback->source,
                                   &options);
}

static void restore_checkpoint(qa_roq_playback *playback,
                               const qa_save_value *value) {
  qa_save_reader r;
  reader_init(&r, value, "roq-playback");
  qa_save_reader f;
  read_field(&r, "decoder", &f);
  qa_roq_decoder_restore_checkpoint(&playback->decoder, f.value);
  qa_save_reader_destroy(&f);
  read_field(&r, "epoch", &f);
  playback->epoch = (uint32_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(&r, "lastTime", &f);
  playback->last_time = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  static const char *const state_choices[4] = {"playing", "held", "ended",
                                               "looped"};
  read_field(&r, "state", &f);
  const char *state = qa_save_reader_choice_string(&f, state_choices, 4);
  playback->state = strcmp(state, "held") == 0 ? QA_ROQ_PLAYBACK_HELD
                    : strcmp(state, "ended") == 0 ? QA_ROQ_PLAYBACK_ENDED
                    : strcmp(state, "looped") == 0 ? QA_ROQ_PLAYBACK_LOOPED
                                                   : QA_ROQ_PLAYBACK_PLAYING;
  qa_save_reader_destroy(&f);
  read_field(&r, "frame", &f);
  playback->has_frame = qa_read_cinematic_frame(&f, &playback->frame);
  qa_save_reader_destroy(&f);
  read_field(&r, "pointer", &f);
  if (!qa_save_reader_is_null(&f)) {
    qa_save_reader g;
    read_field(&f, "offset", &g);
    playback->pointer.offset = (int64_t)qa_save_reader_integer_min(&g, 0);
    qa_save_reader_destroy(&g);
    read_field(&f, "byteLength", &g);
    playback->pointer.byte_length = (int64_t)qa_save_reader_integer_min(&g, 0);
    qa_save_reader_destroy(&g);
    playback->has_pointer = true;
  }
  qa_save_reader_destroy(&f);
  read_field(&r, "decodedFrames", &f);
  playback->decoded_frames = (int64_t)qa_save_reader_integer_min(&f, -1);
  qa_save_reader_destroy(&f);
  read_field(&r, "sourceSample", &f);
  playback->source_sample = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(&r, "loopIndex", &f);
  playback->loop_index = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&r);
}

static bool playback_setup(qa_roq_playback *playback,
                           const qa_roq_playback_options *options,
                           const qa_save_value *checkpoint) {
  playback->source = dup_text(options->source == NULL ? "<RoQ playback>"
                                                       : options->source);
  if (playback->source == NULL) return false;
  playback->loop = options->loop;
  playback->hold = options->hold;
  playback->silent = options->silent;
  playback->shader = options->shader;
  playback->ctx = options->ctx;
  playback->on_audio = options->on_audio;
  playback->developer_print = options->developer_print;
  playback->on_info = options->on_info;
  playback->on_frame = options->on_frame;
  playback->before_raw_stream_reset = options->before_raw_stream_reset;
  playback->scratch = options->scratch;
  playback->state = QA_ROQ_PLAYBACK_PLAYING;
  playback->decoded_frames = -1;
  if (!make_decoder(playback)) return false;
  playback->epoch = epoch_of(options->clock);
  playback->last_time = (int64_t)playback->epoch;
  if (checkpoint != NULL) restore_checkpoint(playback, checkpoint);
  return true;
}

bool qa_roq_playback_init_bytes(qa_roq_playback *playback,
                                const uint8_t *data, size_t length,
                                const qa_roq_playback_options *options,
                                const qa_save_value *checkpoint) {
  memset(playback, 0, sizeof *playback);
  playback->is_stream = false;
  playback->data = (uint8_t *)malloc(length == 0 ? 1 : length);
  if (playback->data == NULL) return false;
  memcpy(playback->data, data, length);
  playback->data_length = length;
  return playback_setup(playback, options, checkpoint);
}

bool qa_roq_playback_init_stream(qa_roq_playback *playback,
                                 qa_roq_stream *stream,
                                 const qa_roq_playback_options *options,
                                 const qa_save_value *checkpoint) {
  memset(playback, 0, sizeof *playback);
  playback->is_stream = true;
  playback->stream = stream;
  return playback_setup(playback, options, checkpoint);
}

bool qa_roq_playback_from_reset(qa_roq_playback *playback,
                                qa_roq_stream *stream,
                                const qa_roq_playback_options *options) {
  qa_roq_stream_rewind(stream);
  if (!qa_roq_playback_init_stream(playback, stream, options, NULL)) {
    return false;
  }
  playback->state = QA_ROQ_PLAYBACK_LOOPED;
  return true;
}

void qa_roq_playback_destroy(qa_roq_playback *playback) {
  if (playback->has_frame) qa_cinematic_frame_dispose(&playback->frame);
  qa_roq_decoder_destroy(&playback->decoder);
  free(playback->data);
  free(playback->source);
  memset(playback, 0, sizeof *playback);
}

qa_save_value *qa_roq_playback_capture_checkpoint(qa_roq_playback *playback) {
  qa_save_value *decoder =
      qa_roq_decoder_capture_checkpoint(&playback->decoder);
  qa_roq_playback_frame current;
  bool has_current = qa_roq_playback_current_frame(playback, &current);
  qa_save_value *frame =
      qa_cinematic_frame_to_save(has_current ? &current : NULL);
  if (has_current) qa_cinematic_frame_dispose(&current);
  qa_save_value *pointer = NULL;
  if (playback->has_pointer) {
    pointer = qa_save_value_object();
    if (pointer != NULL &&
        !(set_number(pointer, "offset", (double)playback->pointer.offset) &&
          set_number(pointer, "byteLength",
                     (double)playback->pointer.byte_length))) {
      qa_save_value_free(pointer);
      pointer = NULL;
    }
  } else {
    pointer = qa_save_value_null();
  }
  qa_save_value *object = qa_save_value_object();
  if (decoder == NULL || frame == NULL || pointer == NULL ||
      object == NULL) {
    qa_save_value_free(decoder);
    qa_save_value_free(frame);
    qa_save_value_free(pointer);
    qa_save_value_free(object);
    return NULL;
  }
  bool ok = set_value(object, "decoder", decoder) &&
            set_number(object, "epoch", (double)playback->epoch) &&
            set_number(object, "lastTime", (double)playback->last_time) &&
            set_value(object, "state",
                      qa_save_value_string(
                          qa_roq_playback_status_name(playback->state))) &&
            set_value(object, "frame", frame) &&
            set_value(object, "pointer", pointer) &&
            set_number(object, "decodedFrames",
                       (double)playback->decoded_frames) &&
            set_number(object, "sourceSample",
                       (double)playback->source_sample) &&
            set_number(object, "loopIndex", (double)playback->loop_index);
  if (!ok) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

int64_t qa_roq_playback_frame_rate(const qa_roq_playback *playback) {
  return qa_roq_decoder_frame_rate(&playback->decoder);
}

bool qa_roq_playback_current_frame(const qa_roq_playback *playback,
                                   qa_roq_playback_frame *out) {
  memset(out, 0, sizeof *out);
  if (!playback->has_frame) return false;
  return qa_copy_cinematic_frame(&playback->frame, out);
}

bool qa_roq_playback_current_pointer(const qa_roq_playback *playback,
                                     qa_roq_frame_pointer *out) {
  if (!playback->has_pointer) return false;
  *out = playback->pointer;
  return true;
}

bool qa_roq_playback_dimensions(const qa_roq_playback *playback,
                                int64_t *width, int64_t *height) {
  if (qa_roq_decoder_width(&playback->decoder) == 0) return false;
  *width = qa_roq_decoder_width(&playback->decoder);
  *height = qa_roq_decoder_height(&playback->decoder);
  return true;
}

void qa_roq_playback_restart(qa_roq_playback *playback,
                             qa_media_clock clock) {
  qa_roq_decoder_rewind(&playback->decoder);
  playback->epoch = epoch_of(clock);
  playback->last_time = (int64_t)playback->epoch;
  playback->state = QA_ROQ_PLAYBACK_LOOPED;
  playback->decoded_frames = -1;
  playback->source_sample = 0;
  playback->loop_index = 0;
}

static void reset_loop(qa_roq_playback *playback, qa_media_clock clock) {
  qa_roq_decoder_rewind(&playback->decoder);
  playback->epoch = epoch_of(clock);
  playback->last_time = (int64_t)playback->epoch;
  playback->state = QA_ROQ_PLAYBACK_LOOPED;
  playback->decoded_frames = -1;
  playback->source_sample = 0;
  playback->loop_index++;
}

bool qa_roq_playback_reset(qa_roq_playback *playback, qa_media_clock clock) {
  if (playback->scratch != NULL) qa_roq_scratch_clear(playback->scratch);
  qa_roq_decoder_destroy(&playback->decoder);
  if (!make_decoder(playback)) return false;
  if (playback->has_frame) {
    qa_cinematic_frame_dispose(&playback->frame);
    playback->has_frame = false;
  }
  playback->has_pointer = false;
  qa_roq_playback_restart(playback, clock);
  playback->state = QA_ROQ_PLAYBACK_PLAYING;
  return true;
}

typedef struct hook_ctx {
  qa_roq_playback *playback;
  qa_media_clock clock;
} hook_ctx;

static void hook_before_stereo(void *raw) {
  hook_ctx *ctx = (hook_ctx *)raw;
  qa_roq_playback *playback = ctx->playback;
  if (!playback->silent && playback->decoded_frames == -1 &&
      playback->before_raw_stream_reset != NULL) {
    playback->before_raw_stream_reset(playback->ctx);
  }
}

static void hook_after_info(void *raw) {
  hook_ctx *ctx = (hook_ctx *)raw;
  qa_roq_playback *playback = ctx->playback;
  if (playback->decoded_frames == -1) {
    if (playback->on_info != NULL) {
      playback->on_info(playback->ctx,
                        qa_roq_decoder_width(&playback->decoder),
                        qa_roq_decoder_height(&playback->decoder));
    }
    playback->epoch = epoch_of(ctx->clock);
    playback->last_time = (int64_t)playback->epoch;
  }
  if (playback->decoded_frames != 1) playback->decoded_frames = 0;
}

static void hook_after_audio(void *raw, const qa_roq_event *event) {
  hook_ctx *ctx = (hook_ctx *)raw;
  qa_roq_playback *playback = ctx->playback;
  double source_time = (double)playback->source_sample * 1000.0 /
                       (double)event->sample_rate;
  qa_roq_playback_audio audio;
  memset(&audio, 0, sizeof audio);
  audio.samples = event->samples;
  audio.sample_count = event->sample_count;
  audio.channels = event->channels;
  audio.sample_rate = event->sample_rate;
  audio.loop = playback->loop_index;
  audio.source_sample = playback->source_sample;
  audio.source_time = source_time;
  audio.time = (double)playback->epoch + source_time;
  audio.reset_stream =
      event->channels == 2 && playback->decoded_frames == -1;
  playback->on_audio(playback->ctx, &audio);
  playback->source_sample +=
      (int64_t)(event->sample_count / (size_t)event->channels);
}

static int64_t target_frames(qa_roq_playback *playback,
                             qa_media_clock clock) {
  double time = clock_time(clock.sample(clock.ctx));
  return (int64_t)trunc((float)((time - (double)playback->epoch) * 3.0 / 100.0));
}

bool qa_roq_playback_run(qa_roq_playback *playback, qa_media_clock clock,
                         qa_roq_playback_tick *out) {
  memset(out, 0, sizeof *out);
  if (playback->state == QA_ROQ_PLAYBACK_HELD ||
      playback->state == QA_ROQ_PLAYBACK_ENDED) {
    out->status = playback->state;
    return true;
  }
  /* CIN_RunCinematic uses 30 fps regardless of the RoQ rate. */
  int64_t this_time =
      (int64_t)trunc((float)clock_time(clock.sample(clock.ctx)));
  int32_t gap = (int32_t)((uint32_t)this_time - (uint32_t)playback->last_time);
  if (playback->shader && (gap > 100 || gap < -100)) {
    playback->epoch = playback->epoch + (uint32_t)gap;
  }
  int64_t target = target_frames(playback, clock);
  uint32_t epoch = playback->epoch;
  bool dirty = false;
  hook_ctx hooks;
  hooks.playback = playback;
  hooks.clock = clock;
  while (playback->state == QA_ROQ_PLAYBACK_PLAYING &&
         (target != playback->decoded_frames ||
          qa_roq_decoder_in_packet(&playback->decoder) ||
          qa_roq_decoder_has_invalid_lookahead(&playback->decoder))) {
    bool had_invalid =
        qa_roq_decoder_has_invalid_lookahead(&playback->decoder);
    qa_roq_event event;
    if (!qa_roq_decoder_next_chunk(&playback->decoder, hook_before_stereo,
                                   &hooks, hook_after_info, &hooks,
                                   hook_after_audio, &hooks, &event)) {
      return false;
    }
    if (event.kind == QA_ROQ_EVENT_FRAME) {
      playback->decoded_frames++;
      qa_roq_playback_frame frame;
      memset(&frame, 0, sizeof frame);
      frame.rgba = event.rgba;
      frame.rgba_length = event.rgba_length;
      event.rgba = NULL;
      frame.width = qa_roq_decoder_width(&playback->decoder);
      frame.height = qa_roq_decoder_height(&playback->decoder);
      frame.index = event.index;
      frame.loop = playback->loop_index;
      frame.source_time = event.time;
      frame.time = (double)playback->epoch +
                   (double)playback->decoded_frames * 1000.0 / 30.0;
      if (playback->has_frame) qa_cinematic_frame_dispose(&playback->frame);
      playback->frame = frame;
      playback->has_frame = true;
      int64_t byte_length = frame.width * frame.height * 4;
      playback->pointer.offset = byte_length * (event.index & 1);
      playback->pointer.byte_length = byte_length;
      playback->has_pointer = true;
      dirty = true;
      if (playback->on_frame != NULL) {
        playback->on_frame(playback->ctx, &playback->frame,
                           playback->pointer);
      }
    }
    if (!had_invalid &&
        qa_roq_decoder_has_invalid_lookahead(&playback->decoder) &&
        !qa_roq_decoder_reset_after_run(&playback->decoder)) {
      playback->developer_print(playback->ctx,
                                "roq_size>65536||roq_id==0x1084\n");
    }
    if (event.kind == QA_ROQ_EVENT_END) {
      if (playback->hold &&
          !qa_roq_decoder_has_invalid_lookahead(&playback->decoder)) {
        playback->state = QA_ROQ_PLAYBACK_HELD;
      } else if (playback->loop &&
                 !qa_roq_decoder_reset_after_run(&playback->decoder)) {
        /* RoQReset rebases startTime to the clock that observed
         * EOF, even after a long stall. */
        reset_loop(playback, clock);
      } else {
        playback->state = QA_ROQ_PLAYBACK_ENDED;
      }
    }
    qa_roq_event_dispose(&event);
    /* RoQInterrupt processes an entire packet before
     * CIN_RunCinematic checks startTime. */
    if (!qa_roq_decoder_in_packet(&playback->decoder) &&
        epoch != playback->epoch) {
      target = target_frames(playback, clock);
      epoch = playback->epoch;
    }
  }
  playback->last_time = this_time;
  if (playback->state == QA_ROQ_PLAYBACK_LOOPED) {
    playback->state = QA_ROQ_PLAYBACK_PLAYING;
  }
  if (playback->state == QA_ROQ_PLAYBACK_ENDED && playback->loop) {
    reset_loop(playback, clock);
  }
  out->status = playback->state;
  if (dirty) {
    out->has_frame = qa_copy_cinematic_frame(&playback->frame, &out->frame);
    if (!out->has_frame) return false;
  }
  return true;
}
