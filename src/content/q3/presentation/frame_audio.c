/* Port of src/content/q3/presentation/frame-audio.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/frame_audio.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
void qa_q3_pres_client_frame_audio_init(qa_q3_pres_client_frame_audio *audio,
    qa_q3_pres_client_game_state *state, qa_pcm_sound *wear_off_sound,
    qa_q3_pres_client_frame_audio_host host) {
  audio->state = state;
  audio->wear_off_sound = wear_off_sound;
  audio->host = host;
}
static double buffer_slot(double value) {
  /* TS indexes soundBuffer[value]; landed state stores doubles. */
  return trunc(value);
}
void qa_q3_pres_client_frame_audio_add_buffered_sound(qa_q3_pres_client_frame_audio *audio, qa_pcm_sound *sound) {
  if (!sound) return;
  qa_q3_pres_client_game_state *state = audio->state;
  double in = buffer_slot(state->sound_buffer_in);
  if (in >= 0 && in < QA_Q3_PRES_SOUND_BUFFER_COUNT) state->sound_buffer[(size_t)in] = sound;
  state->sound_buffer_in = fmod(state->sound_buffer_in + 1, 20);
  if (state->sound_buffer_in == state->sound_buffer_out) state->sound_buffer_out = fmod(state->sound_buffer_out + 1, 20);
}
void qa_q3_pres_client_frame_audio_play_buffered_sounds(qa_q3_pres_client_frame_audio *audio) {
  qa_q3_pres_client_game_state *state = audio->state;
  if (state->sound_time >= state->time || state->sound_buffer_out == state->sound_buffer_in) return;
  double out = state->sound_buffer_out;
  if (out != trunc(out) || out < 0 || out >= QA_Q3_PRES_SOUND_BUFFER_COUNT) {
    char message[128];
    snprintf(message, sizeof message, "CG_PlayBufferedSounds: sound buffer index %s outside 0..19", QA_JS_NUMBER(out));
    qa_range_error("%s", message);
  }
  qa_pcm_sound *sound = state->sound_buffer[(size_t)out];
  if (!sound) return;
  audio->host.start_local_sound(audio->host.ctx, sound, 7);
  state->sound_buffer[(size_t)out] = NULL;
  state->sound_buffer_out = fmod(state->sound_buffer_out + 1, 20);
  state->sound_time = qa_int32(state->time + 750);
}
void qa_q3_pres_client_frame_audio_powerup_timer_sounds(qa_q3_pres_client_frame_audio *audio) {
  qa_q3_pres_client_game_state *state = audio->state;
  if (!state->snap) qa_range_error("CG_PowerupTimerSounds requires an active snapshot");
  for (double slot = 0; slot < 16; slot++) {
    double expiry = qa_q3_base_player_state_slots_get(&state->snap->player_state.powerups, slot);
    if (expiry <= state->time) continue;
    double remaining = qa_int32(expiry - state->time);
    if (remaining >= 5000) continue;
    double previous = qa_int32(expiry - state->old_time);
    if (trunc(remaining / 1000) != trunc(previous / 1000))
      audio->host.start_sound(audio->host.ctx, NULL, state->snap->player_state.client_num, 4,
                              audio->wear_off_sound);
  }
}
