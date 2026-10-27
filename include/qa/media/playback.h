/*
 * Port of src/media/playback.ts: format-dispatching cinematic
 * playback over one movie-local clock. C accommodations: sources
 * borrow their bytes/image pixels; the open factory is a function
 * pointer plus ctx (cinematicBytes fills it from the stored byte
 * view); currentFrame/tick frames are owned copies. The unexported
 * TS PlaybackClock is exposed because the struct embeds it.
 * GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_PLAYBACK_H
#define QA_MEDIA_PLAYBACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/media/cin_playback.h"
#include "qa/media/ogv_playback.h"
#include "qa/media/roq_playback.h"
#include "qa/media/types.h"

typedef enum qa_cinematic_format {
  QA_CINEMATIC_FORMAT_ROQ,
  QA_CINEMATIC_FORMAT_CIN,
  QA_CINEMATIC_FORMAT_OGV,
  QA_CINEMATIC_FORMAT_IMAGE
} qa_cinematic_format;
const char *qa_cinematic_format_name(qa_cinematic_format format);

/* TS CinematicSource. `open` NULL means bytes-backed (the
 * cinematicBytes form); image fields serve the image form. */
typedef struct qa_cinematic_source {
  qa_cinematic_format format;
  const char *source; /* borrowed */
  void *open_ctx;
  qa_media_input *(*open)(void *ctx); /* nullable */
  const uint8_t *bytes; /* borrowed; bytes-backed form */
  size_t bytes_length;
  int64_t width;  /* image form */
  int64_t height; /* image form */
  const uint8_t *rgba; /* borrowed unless rgba_owned */
  size_t rgba_length;
  bool rgba_owned;
} qa_cinematic_source;
/* Opens a fresh input for a file-format source (TS source.open). */
qa_media_input *qa_cinematic_source_open(const qa_cinematic_source *source);
void qa_cinematic_source_dispose(qa_cinematic_source *source);

/* TS cinematicBytes; format is roq/cin/ogv, source NULL means
 * "<cinematic>". The result borrows `bytes` and `source`. */
qa_cinematic_source qa_cinematic_bytes(qa_cinematic_format format,
                                       const uint8_t *bytes, size_t length,
                                       const char *source);

/* The clock is local to one movie, so pausing a seat cannot pause
 * another movie. */
typedef struct qa_playback_clock {
  qa_media_clock wall;
  double start;
  bool has_paused_at;
  double paused_at;
  double paused_duration;
  double offset;
} qa_playback_clock;
void qa_playback_clock_init(qa_playback_clock *clock, qa_media_clock wall,
                            const qa_save_value *checkpoint);
qa_save_value *qa_playback_clock_capture_checkpoint(
    qa_playback_clock *clock);
double qa_playback_clock_sample(qa_playback_clock *clock);
void qa_playback_clock_pause(qa_playback_clock *clock, bool paused);
qa_media_clock qa_playback_clock_as_media_clock(qa_playback_clock *clock);

/* TS decoderStatus/sourceStatus vocabulary (adds "looped"). */
typedef enum qa_cinematic_decoder_status {
  QA_CINEMATIC_DECODER_PLAYING,
  QA_CINEMATIC_DECODER_PAUSED,
  QA_CINEMATIC_DECODER_HELD,
  QA_CINEMATIC_DECODER_ENDED,
  QA_CINEMATIC_DECODER_STOPPED,
  QA_CINEMATIC_DECODER_LOOPED
} qa_cinematic_decoder_status;
const char *qa_cinematic_decoder_status_name(
    qa_cinematic_decoder_status status);

typedef struct qa_cinematic_playback {
  qa_cinematic_target target;
  qa_cinematic_options options;
  qa_playback_clock clock;
  qa_cinematic_format format;
  union {
    struct {
      qa_roq_playback playback;
      qa_roq_stream *stream;  /* owned */
      qa_roq_scratch *scratch; /* owned */
    } roq;
    qa_cin_playback cin;
    qa_ogv_playback ogv;
    qa_cinematic_frame image; /* owned */
  } movie;
  qa_cinematic_status state;
  qa_cinematic_decoder_status decoder_status;
  bool has_picture;
  qa_cinematic_frame picture; /* owned when has_picture */
  bool dirty;
  int64_t frame_revision;
  bool completed;
  bool closed;
  char *source; /* owned */
} qa_cinematic_playback;

/* `checkpoint` NULL starts fresh. False only on OOM. */
bool qa_cinematic_playback_init(qa_cinematic_playback *playback,
                                const qa_cinematic_source *source,
                                const qa_cinematic_options *options,
                                const qa_save_value *checkpoint);
void qa_cinematic_playback_destroy(qa_cinematic_playback *playback);

qa_save_value *qa_cinematic_playback_capture_checkpoint(
    qa_cinematic_playback *playback);
qa_cinematic_status qa_cinematic_playback_status(
    const qa_cinematic_playback *playback);
qa_cinematic_decoder_status qa_cinematic_playback_source_status(
    const qa_cinematic_playback *playback);
int64_t qa_cinematic_playback_revision(
    const qa_cinematic_playback *playback);
double qa_cinematic_playback_time_milliseconds(
    qa_cinematic_playback *playback);
qa_cinematic_timeline qa_cinematic_playback_timeline(
    qa_cinematic_playback *playback);
/* False when there is no picture (or on OOM); *out owned on true. */
bool qa_cinematic_playback_current_frame(
    const qa_cinematic_playback *playback, qa_cinematic_frame *out);
/* False only on OOM. */
bool qa_cinematic_playback_tick(qa_cinematic_playback *playback,
                                qa_cinematic_tick *out);
void qa_cinematic_playback_pause(qa_cinematic_playback *playback,
                                 bool paused);
void qa_cinematic_playback_skip(qa_cinematic_playback *playback);
void qa_cinematic_playback_stop(qa_cinematic_playback *playback);
void qa_cinematic_playback_close(qa_cinematic_playback *playback);

#endif
