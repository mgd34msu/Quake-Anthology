/* Port of src/media/audio.ts. GPL-2.0-or-later. */
#include "qa/media/audio.h"

#include <string.h>

qa_cinematic_audio_binding qa_cinematic_audio_bind(
    qa_cinematic_mixer mixer, const char *id, double gain) {
  qa_cinematic_audio_binding binding;
  binding.mixer = mixer;
  binding.id = id;
  binding.gain = gain;
  return binding;
}

void qa_cinematic_audio_on_audio(void *ctx,
                                 const qa_cinematic_audio *audio,
                                 qa_cinematic_target target) {
  qa_cinematic_audio_binding *binding = (qa_cinematic_audio_binding *)ctx;
  qa_audio_stream_target stream_target;
  memset(&stream_target, 0, sizeof stream_target);
  stream_target.id = binding->id;
  stream_target.gain = binding->gain;
  if (target.kind == QA_CINEMATIC_TARGET_SEAT) {
    stream_target.audience.kind = QA_AUDIO_AUDIENCE_SEAT;
    stream_target.audience.seat = target.seat;
  } else {
    stream_target.audience.kind = QA_AUDIO_AUDIENCE_WORLD;
  }
  qa_stream_pcm pcm;
  memset(&pcm, 0, sizeof pcm);
  pcm.is_u8 = audio->is_u8;
  pcm.samples_s16 = audio->samples_s16;
  pcm.samples_u8 = audio->samples_u8;
  pcm.sample_count = audio->sample_count;
  pcm.channels = audio->channels;
  pcm.sample_rate = audio->sample_rate;
  pcm.source_sample = audio->source_sample;
  pcm.reset_stream = audio->reset_stream;
  binding->mixer.queue_stream(binding->mixer.ctx, &stream_target, &pcm);
}

void qa_cinematic_audio_on_reset(void *ctx, qa_cinematic_target target) {
  (void)target;
  qa_cinematic_audio_binding *binding = (qa_cinematic_audio_binding *)ctx;
  binding->mixer.stop_stream(binding->mixer.ctx, binding->id);
}

void qa_cinematic_audio_on_pause(void *ctx, bool paused,
                                 qa_cinematic_target target) {
  (void)target;
  qa_cinematic_audio_binding *binding = (qa_cinematic_audio_binding *)ctx;
  binding->mixer.pause_stream(binding->mixer.ctx, binding->id, paused);
}

void qa_cinematic_audio_apply(qa_cinematic_audio_binding *binding,
                              qa_cinematic_options *options) {
  options->on_audio_ctx = binding;
  options->on_audio = qa_cinematic_audio_on_audio;
  options->on_audio_reset_ctx = binding;
  options->on_audio_reset = qa_cinematic_audio_on_reset;
  options->on_audio_pause_ctx = binding;
  options->on_audio_pause = qa_cinematic_audio_on_pause;
}
