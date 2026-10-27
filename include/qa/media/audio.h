/*
 * Port of src/media/audio.ts: one PCM lane in the shared mixer
 * for a movie, regardless of renderer or video format. The mixer
 * is a vtable plus ctx; the optional checkpoint hooks carry the
 * RawAudioStream checkpoint tree (qa/audio/streams.h) as a
 * qa_save_value. The binding's three callbacks match the
 * CinematicOptions audio slots.
 * GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_AUDIO_H
#define QA_MEDIA_AUDIO_H

#include "qa/audio/types.h"
#include "qa/media/types.h"
#include "qa/persistence/value.h"

typedef struct qa_cinematic_mixer {
  void *ctx;
  void (*queue_stream)(void *ctx, const qa_audio_stream_target *target,
                       const qa_stream_pcm *pcm);
  void (*stop_stream)(void *ctx, const char *id);
  void (*pause_stream)(void *ctx, const char *id, bool paused);
  /* Optional; NULL mirrors an absent TS method. The returned tree
   * is owned by the caller (free with qa_save_value_free). */
  qa_save_value *(*capture_stream_checkpoint)(void *ctx, const char *id);
  void (*restore_stream_checkpoint)(void *ctx,
                                    const qa_audio_stream_target *target,
                                    const qa_save_value *value);
} qa_cinematic_mixer;

typedef struct qa_cinematic_audio_binding {
  qa_cinematic_mixer mixer;
  const char *id; /* borrowed */
  double gain;
} qa_cinematic_audio_binding;

/* TS cinematicAudio (gain defaults to 1 in the TS); named _bind
 * because qa_cinematic_audio is the CinematicAudio type. */
qa_cinematic_audio_binding qa_cinematic_audio_bind(
    qa_cinematic_mixer mixer, const char *id, double gain);
/* The three Pick<CinematicOptions, ...> callbacks; pass the
 * binding as each callback ctx. */
void qa_cinematic_audio_on_audio(void *ctx,
                                 const qa_cinematic_audio *audio,
                                 qa_cinematic_target target);
void qa_cinematic_audio_on_reset(void *ctx, qa_cinematic_target target);
void qa_cinematic_audio_on_pause(void *ctx, bool paused,
                                 qa_cinematic_target target);
/* Fills the three audio slots of `options` from the binding. */
void qa_cinematic_audio_apply(qa_cinematic_audio_binding *binding,
                              qa_cinematic_options *options);

#endif
