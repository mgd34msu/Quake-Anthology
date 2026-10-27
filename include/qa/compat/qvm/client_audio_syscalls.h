/*
 * Port of src/compat/qvm/client-audio-syscalls.ts.
 * qa_qvmx_client_sound mirrors content/q3/presentation/client.ts
 * Q3ClientSound (content presentation unported); its bank mirrors
 * Q3PresentationSoundBank's used surface. soundForIndex's TS
 * undefined (invalid handle) is kind QA_QVMX_SOUND_UNDEFINED,
 * distinct from a null PcmSound. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_AUDIO_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_AUDIO_SYSCALLS_H

#include <stdbool.h>

#include "qa/audio/wav.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/math.h"

typedef enum qa_qvmx_sound_kind {
  QA_QVMX_SOUND_UNDEFINED,
  QA_QVMX_SOUND_NULL,
  QA_QVMX_SOUND_PCM
} qa_qvmx_sound_kind;

typedef struct qa_qvmx_sound_result {
  qa_qvmx_sound_kind kind;
  const qa_pcm_sound *pcm; /* borrowed */
} qa_qvmx_sound_result;

typedef struct qa_qvmx_sound_bank {
  void *ctx;
  qa_qvmx_sound_result (*sound_for_index)(void *ctx, double index);
  /* TS registerSound(path|null, compressed): Promise<PcmSound|null>. */
  const qa_pcm_sound *(*register_sound)(void *ctx, const char *path, bool compressed);
  double (*index_for_sound)(void *ctx, const qa_pcm_sound *sound);
} qa_qvmx_sound_bank;

typedef struct qa_qvmx_client_sound {
  qa_qvmx_sound_bank bank;
  void *ctx;
  void (*start_sound)(void *ctx, bool has_origin, qa_vec3 origin, double entity, double channel,
                      const qa_pcm_sound *pcm);
  void (*start_local_sound)(void *ctx, const qa_pcm_sound *pcm, double channel);
  void (*add_loop_sound)(void *ctx, double entity, qa_vec3 origin, qa_vec3 velocity,
                         const qa_pcm_sound *pcm, bool real);
  void (*update_sound_position)(void *ctx, double entity, qa_vec3 position);
  void (*stop_looping_sound)(void *ctx, double entity);
  void (*clear_looping_sounds)(void *ctx, bool kill_all);
  void (*set_listener)(void *ctx, double client, qa_vec3 origin, qa_axis axis);
  void (*start_background_track)(void *ctx, const char *intro, const char *loop);
} qa_qvmx_client_sound;

typedef struct qa_qvm_client_audio_services {
  qa_qvm_role role; /* "cgame" | "ui" */
  qa_qvmx_client_sound sound;
  void *ctx;
  void (*print)(void *ctx, const char *text);
} qa_qvm_client_audio_services;

bool qa_qvm_client_audio_syscall(const qa_qvm_host_call *call,
                                 qa_qvm_client_audio_services services, qa_qvm_host_result *out);

#endif
