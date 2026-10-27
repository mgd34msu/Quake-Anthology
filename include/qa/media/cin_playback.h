/*
 * Port of src/media/cin-playback.ts (Quake II SCR_PlayCinematic /
 * SCR_RunCinematic / SCR_ReadNextFrame). C accommodations: the
 * tick frame is an owned copy (dispose via
 * qa_cinematic_frame_dispose when has_frame); init takes ownership
 * of a MediaInput (bytes form borrows the bytes); close keeps the
 * struct, destroy also frees. GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_CIN_PLAYBACK_H
#define QA_MEDIA_CIN_PLAYBACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/media/cin.h"
#include "qa/media/types.h"

typedef struct qa_cin_playback_options {
  qa_media_clock clock;
  const char *source; /* nullable */
  bool loop;
  bool hold;
  bool silent;
  void *on_audio_ctx;
  void (*on_audio)(void *ctx, const qa_cinematic_audio *audio);
  void *developer_print_ctx;
  void (*developer_print)(void *ctx, const char *message); /* nullable */
} qa_cin_playback_options;

typedef enum qa_cin_playback_status {
  QA_CIN_PLAYBACK_PLAYING,
  QA_CIN_PLAYBACK_HELD,
  QA_CIN_PLAYBACK_ENDED,
  QA_CIN_PLAYBACK_LOOPED
} qa_cin_playback_status;
const char *qa_cin_playback_status_name(qa_cin_playback_status status);

typedef enum qa_cin_playback_update_kind {
  QA_CIN_UPDATE_UNCHANGED,
  QA_CIN_UPDATE_FRAME
} qa_cin_playback_update_kind;

typedef struct qa_cin_playback_tick {
  qa_cin_playback_status status;
  qa_cin_playback_update_kind update_kind;
  bool has_frame; /* frame update may still carry no picture */
  qa_cinematic_frame frame; /* owned when has_frame */
} qa_cin_playback_tick;
void qa_cin_playback_tick_dispose(qa_cin_playback_tick *tick);

typedef struct qa_cin_playback {
  qa_cin_decoder decoder;
  qa_cin_playback_options options;
  int64_t epoch;
  bool has_picture;
  qa_cin_frame picture; /* owned when has_picture */
  bool has_pending;
  qa_cin_frame pending; /* owned when has_pending */
  qa_cin_playback_status state;
  int64_t pass;
} qa_cin_playback;

/* `checkpoint` NULL starts fresh. False only on OOM. */
bool qa_cin_playback_init(qa_cin_playback *playback, qa_media_input *input,
                          const qa_cin_playback_options *options,
                          const qa_save_value *checkpoint);
bool qa_cin_playback_init_bytes(qa_cin_playback *playback,
                                const uint8_t *data, size_t length,
                                const qa_cin_playback_options *options,
                                const qa_save_value *checkpoint);
void qa_cin_playback_destroy(qa_cin_playback *playback);

qa_save_value *qa_cin_playback_capture_checkpoint(qa_cin_playback *playback);
void qa_cin_playback_dimensions(const qa_cin_playback *playback,
                                int64_t *width, int64_t *height);
/* False when there is no picture (or on OOM); *out owned on true. */
bool qa_cin_playback_current_frame(qa_cin_playback *playback,
                                   qa_cinematic_frame *out);
bool qa_cin_playback_restart(qa_cin_playback *playback,
                             qa_media_clock clock);
/* False only on OOM. */
bool qa_cin_playback_run(qa_cin_playback *playback, qa_media_clock clock,
                         qa_media_focus focus, qa_cin_playback_tick *out);
void qa_cin_playback_close(qa_cin_playback *playback);

#endif
