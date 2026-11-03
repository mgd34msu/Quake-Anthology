/*
 * Port of src/compat/qvm/client-audio-syscalls.ts. sound is the
 * content/q3 Q3ClientSound (client.h) over its Q3PresentationSoundBank;
 * registerSound's Promise settles synchronously. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_AUDIO_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_AUDIO_SYSCALLS_H

#include <stdbool.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/content/q3/presentation/client.h"

/* TS QvmClientAudioServices. */
typedef struct qa_qvm_client_audio_services {
  qa_qvm_role role; /* "cgame" | "ui" */
  qa_q3_pres_client_sound sound;
  void *ctx;
  void (*print)(void *ctx, const char *text);
} qa_qvm_client_audio_services;

bool qa_qvm_client_audio_syscall(const qa_qvm_host_call *call,
                                 qa_qvm_client_audio_services services, qa_qvm_host_result *out);

#endif
