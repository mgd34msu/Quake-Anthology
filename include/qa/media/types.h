/*
 * Port of src/media/types.ts: cinematic clock/target/frame/audio
 * shapes, status literals, and the CinematicOptions callback bundle.
 * C accommodations: MediaClock and each option callback are a
 * function pointer plus ctx; nullable frames are a bool plus an out
 * frame; frame rgba is an owned malloc'd buffer (dispose frees it);
 * CinematicAudio is a borrowed sample view valid for the callback.
 * GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_TYPES_H
#define QA_MEDIA_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/persistence/value.h"

typedef struct qa_media_clock {
  void *ctx;
  double (*sample)(void *ctx);
} qa_media_clock;

typedef enum qa_cinematic_target_kind {
  QA_CINEMATIC_TARGET_SEAT,
  QA_CINEMATIC_TARGET_MATERIAL
} qa_cinematic_target_kind;
typedef struct qa_cinematic_target {
  qa_cinematic_target_kind kind;
  qa_seat_id seat;      /* seat kind */
  const char *id;       /* material kind; borrowed */
} qa_cinematic_target;

typedef struct qa_cinematic_frame {
  uint8_t *rgba; /* owned; NULL when empty */
  size_t rgba_length;
  int64_t width;
  int64_t height;
  int64_t index;
  int64_t loop;
  double source_time;
  double time;
} qa_cinematic_frame;
void qa_cinematic_frame_dispose(qa_cinematic_frame *frame);
/* TS copyCinematicFrame: false when `frame` is NULL or on OOM (then
 * *out is zeroed); on true *out owns a fresh rgba copy. */
bool qa_copy_cinematic_frame(const qa_cinematic_frame *frame,
                             qa_cinematic_frame *out);
/* TS readCinematicFrame over a checkpoint field reader: false when
 * the value is null; on true *out owns its rgba. */
bool qa_read_cinematic_frame(const qa_save_reader *reader,
                             qa_cinematic_frame *out);
/* Checkpoint form read by qa_read_cinematic_frame (the TS frame
 * object in a captureCheckpoint tree); NULL frame yields a null
 * value. Owned tree; NULL only on OOM. */
qa_save_value *qa_cinematic_frame_to_save(const qa_cinematic_frame *frame);

/* Borrowed view: samples are s16 unless is_u8. */
typedef struct qa_cinematic_audio {
  bool is_u8;
  const int16_t *samples_s16;
  const uint8_t *samples_u8;
  size_t sample_count;
  int channels; /* 1 or 2 */
  int64_t sample_rate;
  int64_t source_sample;
  double source_time;
  double time;
  int64_t loop;
  bool reset_stream;
} qa_cinematic_audio;

/* TS focus literals shared by the playback runners. */
typedef enum qa_media_focus {
  QA_MEDIA_FOCUS_GAME,
  QA_MEDIA_FOCUS_CONSOLE,
  QA_MEDIA_FOCUS_MENU
} qa_media_focus;

typedef enum qa_cinematic_status {
  QA_CINEMATIC_PLAYING,
  QA_CINEMATIC_PAUSED,
  QA_CINEMATIC_HELD,
  QA_CINEMATIC_ENDED,
  QA_CINEMATIC_STOPPED
} qa_cinematic_status;
const char *qa_cinematic_status_name(qa_cinematic_status status);

typedef enum qa_cinematic_end_reason {
  QA_CINEMATIC_END_FINISHED,
  QA_CINEMATIC_END_SKIPPED,
  QA_CINEMATIC_END_STOPPED
} qa_cinematic_end_reason;
const char *qa_cinematic_end_reason_name(qa_cinematic_end_reason reason);

typedef struct qa_cinematic_timeline {
  const char *source; /* borrowed */
  double source_time_milliseconds;
  double elapsed_milliseconds;
  int64_t loop;
  qa_cinematic_status status;
} qa_cinematic_timeline;

typedef struct qa_cinematic_tick {
  qa_cinematic_status status;
  bool has_frame;
  qa_cinematic_frame frame; /* owned when has_frame */
  bool changed;
} qa_cinematic_tick;
void qa_cinematic_tick_dispose(qa_cinematic_tick *tick);

typedef struct qa_cinematic_options {
  qa_media_clock clock;
  qa_cinematic_target target;
  bool loop;
  bool hold;
  bool silent;
  void *on_audio_ctx;
  void (*on_audio)(void *ctx, const qa_cinematic_audio *audio,
                   qa_cinematic_target target);
  void *on_audio_reset_ctx;
  void (*on_audio_reset)(void *ctx, qa_cinematic_target target);
  void *on_audio_pause_ctx;
  void (*on_audio_pause)(void *ctx, bool paused, qa_cinematic_target target);
  void *on_complete_ctx;
  void (*on_complete)(void *ctx, qa_cinematic_end_reason reason,
                      qa_cinematic_target target);
  void *developer_print_ctx;
  void (*developer_print)(void *ctx, const char *message); /* nullable */
} qa_cinematic_options;

#endif
