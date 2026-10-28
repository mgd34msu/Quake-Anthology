/* Port of src/content/q3/presentation/frame-audio.ts. GPL-2.0-or-later.
 * Buffered and powerup audio from id Software's code/cgame/cg_view.c. */
#ifndef QA_CONTENT_Q3_PRESENTATION_FRAME_AUDIO_H
#define QA_CONTENT_Q3_PRESENTATION_FRAME_AUDIO_H
#include <stdbool.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/math.h"
/* TS ClientFrameAudioHost. origin NULL is TS null. */
typedef struct qa_q3_pres_client_frame_audio_host {
  void *ctx;
  void (*start_local_sound)(void *ctx, qa_pcm_sound *sound, double channel);
  void (*start_sound)(void *ctx, const qa_vec3 *origin, double entity, double channel, qa_pcm_sound *sound);
} qa_q3_pres_client_frame_audio_host;
typedef struct qa_q3_pres_client_frame_audio {
  qa_q3_pres_client_game_state *state;
  qa_pcm_sound *wear_off_sound; /* borrowed, nullable */
  qa_q3_pres_client_frame_audio_host host;
} qa_q3_pres_client_frame_audio;
void qa_q3_pres_client_frame_audio_init(qa_q3_pres_client_frame_audio *audio,
    qa_q3_pres_client_game_state *state, qa_pcm_sound *wear_off_sound,
    qa_q3_pres_client_frame_audio_host host);
void qa_q3_pres_client_frame_audio_add_buffered_sound(qa_q3_pres_client_frame_audio *audio, qa_pcm_sound *sound);
void qa_q3_pres_client_frame_audio_play_buffered_sounds(qa_q3_pres_client_frame_audio *audio);
void qa_q3_pres_client_frame_audio_powerup_timer_sounds(qa_q3_pres_client_frame_audio *audio);
#endif
