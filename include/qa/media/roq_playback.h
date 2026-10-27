/*
 * Port of src/media/roq-playback.ts (CIN_RunCinematic and
 * RoQInterrupt from id Software cl_cin.c). C accommodations: the
 * byte form copies the data (as the TS does), the stream form
 * borrows it; frames are qa_cinematic_frame values (the TS
 * RoqPlaybackFrame shape, checkpointed through readCinematicFrame);
 * callbacks share one ctx. GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_ROQ_PLAYBACK_H
#define QA_MEDIA_ROQ_PLAYBACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/media/roq.h"
#include "qa/media/types.h"

typedef qa_cinematic_frame qa_roq_playback_frame;

typedef struct qa_roq_frame_pointer {
  int64_t offset;
  int64_t byte_length;
} qa_roq_frame_pointer;

typedef struct qa_roq_playback_audio {
  const int16_t *samples; /* borrowed for the callback */
  size_t sample_count;
  int channels;        /* 1 or 2 */
  int64_t sample_rate; /* 22050 */
  int64_t loop;
  /* Sample-frame offset, independent of interleaved channels. */
  int64_t source_sample;
  double source_time;
  double time;
  /* ZA_SOUND_STEREO resets the raw stream before the first
   * QUAD_INFO of each pass. */
  bool reset_stream;
} qa_roq_playback_audio;

typedef struct qa_roq_playback_options {
  qa_media_clock clock;
  const char *source; /* nullable, "<RoQ playback>" */
  bool loop;
  bool hold;
  bool silent;
  bool shader;
  void *ctx;
  void (*on_audio)(void *ctx, const qa_roq_playback_audio *audio);
  void (*developer_print)(void *ctx, const char *text);
  void (*on_info)(void *ctx, int64_t width, int64_t height); /* nullable */
  void (*on_frame)(void *ctx, const qa_roq_playback_frame *frame,
                   qa_roq_frame_pointer pointer); /* nullable */
  void (*before_raw_stream_reset)(void *ctx);     /* nullable */
  qa_roq_scratch *scratch; /* borrowed; nullable */
} qa_roq_playback_options;

typedef enum qa_roq_playback_status {
  QA_ROQ_PLAYBACK_PLAYING,
  QA_ROQ_PLAYBACK_HELD,
  QA_ROQ_PLAYBACK_ENDED,
  QA_ROQ_PLAYBACK_LOOPED
} qa_roq_playback_status;
const char *qa_roq_playback_status_name(qa_roq_playback_status status);

typedef struct qa_roq_playback_tick {
  qa_roq_playback_status status;
  bool has_frame; /* update kind "frame" */
  qa_roq_playback_frame frame; /* owned when has_frame */
} qa_roq_playback_tick;
void qa_roq_playback_tick_dispose(qa_roq_playback_tick *tick);

typedef struct qa_roq_playback {
  bool is_stream;
  uint8_t *data; /* owned copy; byte form */
  size_t data_length;
  qa_roq_stream *stream; /* borrowed; stream form */
  char *source;          /* owned */
  bool loop;
  bool hold;
  bool silent;
  bool shader;
  void *ctx;
  void (*on_audio)(void *ctx, const qa_roq_playback_audio *audio);
  void (*developer_print)(void *ctx, const char *text);
  void (*on_info)(void *ctx, int64_t width, int64_t height);
  void (*on_frame)(void *ctx, const qa_roq_playback_frame *frame,
                   qa_roq_frame_pointer pointer);
  void (*before_raw_stream_reset)(void *ctx);
  qa_roq_scratch *scratch; /* borrowed, nullable */
  qa_roq_decoder decoder;
  uint32_t epoch;
  int64_t last_time;
  qa_roq_playback_status state;
  bool has_frame;
  qa_roq_playback_frame frame; /* owned when has_frame */
  bool has_pointer;
  qa_roq_frame_pointer pointer;
  int64_t decoded_frames;
  int64_t source_sample;
  int64_t loop_index;
} qa_roq_playback;

/* `checkpoint` NULL starts fresh. False only on OOM. */
bool qa_roq_playback_init_bytes(qa_roq_playback *playback,
                                const uint8_t *data, size_t length,
                                const qa_roq_playback_options *options,
                                const qa_save_value *checkpoint);
bool qa_roq_playback_init_stream(qa_roq_playback *playback,
                                 qa_roq_stream *stream,
                                 const qa_roq_playback_options *options,
                                 const qa_save_value *checkpoint);
/* RoQReset can initialize a retained file before CIN_Play has
 * built its decoder. */
bool qa_roq_playback_from_reset(qa_roq_playback *playback,
                                qa_roq_stream *stream,
                                const qa_roq_playback_options *options);
void qa_roq_playback_destroy(qa_roq_playback *playback);

qa_save_value *qa_roq_playback_capture_checkpoint(qa_roq_playback *playback);
int64_t qa_roq_playback_frame_rate(const qa_roq_playback *playback);
/* False when no frame is published (or on OOM); *out owned. */
bool qa_roq_playback_current_frame(const qa_roq_playback *playback,
                                   qa_roq_playback_frame *out);
bool qa_roq_playback_current_pointer(const qa_roq_playback *playback,
                                     qa_roq_frame_pointer *out);
/* False until QUAD_INFO has been read. */
bool qa_roq_playback_dimensions(const qa_roq_playback *playback,
                                int64_t *width, int64_t *height);
/* Rewinds the decoder and clears the published frame. */
bool qa_roq_playback_reset(qa_roq_playback *playback, qa_media_clock clock);
/* Source RoQReset on handle switching: retain physical buffers,
 * codebooks and the last published image. */
void qa_roq_playback_restart(qa_roq_playback *playback, qa_media_clock clock);
/* False only on OOM. */
bool qa_roq_playback_run(qa_roq_playback *playback, qa_media_clock clock,
                         qa_roq_playback_tick *out);

#endif
