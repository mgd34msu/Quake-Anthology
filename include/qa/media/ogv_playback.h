/*
 * Port of src/media/ogv-playback.ts: Theora video with an optional
 * Vorbis soundtrack over the TS-framed Ogg movie. The soundtrack
 * is the VorbisPcmStream from src/audio/streams.ts
 * (qa/audio/streams.h). GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_OGV_PLAYBACK_H
#define QA_MEDIA_OGV_PLAYBACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/audio/streams.h"
#include "qa/media/ogg.h"
#include "qa/media/types.h"
#include "qa/platform/theora.h"

typedef struct qa_ogv_playback_options {
  qa_media_clock clock;
  bool loop;
  bool hold;
  bool silent;
  void *on_audio_ctx;
  void (*on_audio)(void *ctx, const qa_cinematic_audio *audio);
} qa_ogv_playback_options;

typedef enum qa_ogv_playback_status {
  QA_OGV_PLAYBACK_PLAYING,
  QA_OGV_PLAYBACK_LOOPED,
  QA_OGV_PLAYBACK_HELD,
  QA_OGV_PLAYBACK_ENDED
} qa_ogv_playback_status;
const char *qa_ogv_playback_status_name(qa_ogv_playback_status status);

typedef struct qa_ogv_playback_tick {
  qa_ogv_playback_status status;
  bool is_frame;    /* update kind "frame" */
  bool has_frame;   /* the frame itself may be absent */
  qa_cinematic_frame frame; /* owned copy when has_frame */
} qa_ogv_playback_tick;
void qa_ogv_playback_tick_dispose(qa_ogv_playback_tick *tick);

typedef struct qa_ogv_playback {
  qa_ogg_movie movie;
  qa_theora_decoder decoder;
  qa_pcm_stream *audio; /* owned; NULL when silent or no soundtrack */
  qa_ogv_playback_options options;
  double epoch;
  int64_t pass;
  int64_t next_index;
  bool has_picture;
  qa_cinematic_frame picture; /* owned when has_picture */
  qa_ogv_playback_status state; /* playing/held/ended */
  bool closed;
  bool reset_audio;
  bool has_closed_audio_position;
  int64_t closed_audio_position;
} qa_ogv_playback;

/* `checkpoint` NULL starts fresh. False only on OOM (decode errors
 * abort as thrown). */
bool qa_ogv_playback_init(qa_ogv_playback *playback, const uint8_t *bytes,
                          size_t length,
                          const qa_ogv_playback_options *options,
                          const qa_save_value *checkpoint);
void qa_ogv_playback_destroy(qa_ogv_playback *playback);

qa_save_value *qa_ogv_playback_capture_checkpoint(qa_ogv_playback *playback);
void qa_ogv_playback_dimensions(const qa_ogv_playback *playback,
                                int64_t *width, int64_t *height);
/* Borrowed published picture (the TS returns it directly). */
const qa_cinematic_frame *qa_ogv_playback_current_frame(
    const qa_ogv_playback *playback);
/* False only on OOM. */
bool qa_ogv_playback_run(qa_ogv_playback *playback, qa_media_clock clock,
                         qa_ogv_playback_tick *out);
void qa_ogv_playback_close(qa_ogv_playback *playback);

#endif
