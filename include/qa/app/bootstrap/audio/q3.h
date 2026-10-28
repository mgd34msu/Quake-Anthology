/* Port of src/app/bootstrap/audio/q3.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_AUDIO_Q3_H
#define QA_APP_BOOTSTRAP_AUDIO_Q3_H
#include <stdbool.h>
#include "qa/audio/types.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
/* TS Q3SeatAudioOperation (kind-tagged). */
typedef enum qa_app_q3_seat_audio_operation_kind {
  QA_APP_Q3_SEAT_AUDIO_PLAY,        /* "play" */
  QA_APP_Q3_SEAT_AUDIO_LOOP,        /* "loop" */
  QA_APP_Q3_SEAT_AUDIO_POSITION,    /* "position" */
  QA_APP_Q3_SEAT_AUDIO_STOP_LOOP,   /* "stop-loop" */
  QA_APP_Q3_SEAT_AUDIO_CLEAR_LOOPS, /* "clear-loops" */
  QA_APP_Q3_SEAT_AUDIO_RELEASE_OWNER /* "release-owner" */
} qa_app_q3_seat_audio_operation_kind;
typedef struct qa_app_q3_seat_audio_operation {
  qa_app_q3_seat_audio_operation_kind kind;
  qa_play_sound play;       /* play */
  qa_loop_sound loop;       /* loop */
  qa_actor_id actor;        /* position / stop-loop */
  qa_vec3 origin;           /* position */
  bool kill_all;            /* clear-loops */
} qa_app_q3_seat_audio_operation;
/* TS Q3SeatAudioFrame. has_owner is the TS owner?: present flag. */
typedef struct qa_app_q3_seat_audio_frame {
  qa_content_id content;
  qa_seat_id seat;
  bool has_owner;
  qa_provider_id owner;
  const qa_app_q3_seat_audio_operation *operations;
  size_t operation_count;
} qa_app_q3_seat_audio_frame;
/* TS q3VoiceFallback: returns "sarge" | "james". */
const char *qa_app_q3_voice_fallback(const qa_content_catalog *catalog, qa_content_id content, bool team_game);
#endif
