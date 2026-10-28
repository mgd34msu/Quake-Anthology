/* Port of src/content/q3/presentation/audio.ts. GPL-2.0-or-later.
 * Source handles refer to shared decoded PCM (&asset->pcm identity)
 * and preserve compressed-registration intent. TS async registration
 * is synchronous; register_sound returns false only on OOM. */
#ifndef QA_CONTENT_Q3_PRESENTATION_AUDIO_H
#define QA_CONTENT_Q3_PRESENTATION_AUDIO_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/bank.h"
#include "qa/audio/mixer.h"
#include "qa/audio/types.h"
#include "qa/audio/wav.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"
/* TS SoundRegistration. sound NULL is TS null; path is owned. */
typedef struct qa_q3_pres_sound_registration {
  const char *path;
  bool compressed;
  const qa_sound_asset *sound; /* nullable */
} qa_q3_pres_sound_registration;
typedef struct qa_q3_pres_presentation_sound_bank qa_q3_pres_presentation_sound_bank;
/* bank/zero_sound borrowed. load_sync NULL asset is TS null.
 * NULL only on OOM. */
qa_q3_pres_presentation_sound_bank *qa_q3_pres_presentation_sound_bank_create(
    qa_sound_bank *bank, const qa_sound_asset *zero_sound,
    const qa_sound_asset *(*load_sync)(void *ctx, const char *path, bool compressed), void *load_ctx);
void qa_q3_pres_presentation_sound_bank_destroy(qa_q3_pres_presentation_sound_bank *bank);
qa_sound_bank *qa_q3_pres_presentation_sound_bank_bank(qa_q3_pres_presentation_sound_bank *bank);
const qa_sound_asset *qa_q3_pres_presentation_sound_bank_zero_sound(qa_q3_pres_presentation_sound_bank *bank);
/* Owned checkpoint list (qa_save_value_free); NULL only on OOM.
 * Pending registration aborts with the TS Error text. */
qa_save_value *qa_q3_pres_presentation_sound_bank_capture_checkpoint(qa_q3_pres_presentation_sound_bank *bank);
/* TS async restoreCheckpoint, synchronous. False only on OOM. */
bool qa_q3_pres_presentation_sound_bank_restore_checkpoint(qa_q3_pres_presentation_sound_bank *bank,
                                                           const qa_save_value *value);
/* path NULL aborts with the TS Error text. False only on OOM;
 * *out NULL is TS null. */
bool qa_q3_pres_presentation_sound_bank_register_sound(qa_q3_pres_presentation_sound_bank *bank,
    const char *path, bool compressed, qa_pcm_sound **out);
/* TS sound(): path NULL returns NULL (no abort). False only on OOM. */
bool qa_q3_pres_presentation_sound_bank_sound(qa_q3_pres_presentation_sound_bank *bank,
    const char *path, bool compressed, qa_pcm_sound **out);
/* Foreign PCM aborts with the TS Error text. */
double qa_q3_pres_presentation_sound_bank_index_for_sound(qa_q3_pres_presentation_sound_bank *bank,
                                                          qa_pcm_sound *sound);
const qa_sound_asset *qa_q3_pres_presentation_sound_bank_asset(qa_q3_pres_presentation_sound_bank *bank,
                                                               qa_pcm_sound *sound);
/* Unregistered handles abort with the TS RangeError text. */
qa_pcm_sound *qa_q3_pres_presentation_sound_bank_sound_at_index(qa_q3_pres_presentation_sound_bank *bank,
                                                                double index);
/* TS soundForIndex: *out NULL with *has false is TS undefined. */
void qa_q3_pres_presentation_sound_bank_sound_for_index(qa_q3_pres_presentation_sound_bank *bank, double index,
                                                        qa_pcm_sound **out, bool *has);
/* Borrowed span of the request rows (insertion order). */
const qa_q3_pres_sound_registration *qa_q3_pres_presentation_sound_bank_registrations(
    qa_q3_pres_presentation_sound_bank *bank, size_t *out_count);
/* ClientSoundBank view (resources.h shape) without a circular include:
 * use qa_q3_pres_presentation_sound_bank_as_client_bank in client.c. */
/* TS Q3AudioTarget. actor writes false when the source actor is gone. */
typedef struct qa_q3_pres_audio_target {
  qa_seat_id seat;
  qa_q3_pres_presentation_sound_bank *sounds;
  void *ctx;
  bool (*actor)(void *ctx, double source_number, qa_actor_id *out);
  double (*frame_number)(void *ctx);
  void (*play)(void *ctx, qa_play_sound sound);
  void (*loop)(void *ctx, qa_loop_sound sound);
  void (*update_actor)(void *ctx, qa_actor_id actor, qa_vec3 position);
  void (*stop_loop)(void *ctx, qa_seat_id seat, qa_actor_id actor);
} qa_q3_pres_audio_target;
typedef struct qa_q3_pres_presentation_audio { qa_q3_pres_audio_target target; } qa_q3_pres_presentation_audio;
void qa_q3_pres_presentation_audio_init(qa_q3_pres_presentation_audio *audio, qa_q3_pres_audio_target target);
void qa_q3_pres_presentation_audio_start_source_sound(qa_q3_pres_presentation_audio *audio,
    qa_pcm_sound *sound, qa_start_sound_options options);
/* origin NULL is TS null. */
void qa_q3_pres_presentation_audio_start_sound(qa_q3_pres_presentation_audio *audio, const qa_vec3 *origin,
    double entity, double channel, qa_pcm_sound *sound);
void qa_q3_pres_presentation_audio_start_local_sound(qa_q3_pres_presentation_audio *audio,
    qa_pcm_sound *sound, double channel);
void qa_q3_pres_presentation_audio_add_loop_sound(qa_q3_pres_presentation_audio *audio, double entity,
    qa_vec3 origin, qa_vec3 velocity, qa_pcm_sound *sound, bool real_loop);
void qa_q3_pres_presentation_audio_update_sound_position(qa_q3_pres_presentation_audio *audio,
    double entity, qa_vec3 origin);
void qa_q3_pres_presentation_audio_stop_looping_sound(qa_q3_pres_presentation_audio *audio, double entity);
#endif
