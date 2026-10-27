/*
 * Port of src/compat/qvm/client-collision-syscalls.ts.
 * QvmClientClipModels is a Pick of SourceClipModels
 * (qa_source_clip_models) plus its world's hasNodes.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_COLLISION_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_COLLISION_SYSCALLS_H

#include <stdbool.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/world/collision/q3/clip_models.h"

typedef qa_source_clip_models qa_qvm_client_clip_models;

typedef struct qa_qvm_client_collision_services {
  void *ctx;
  qa_qvm_client_clip_models *(*models)(void *ctx);
  void (*load_map)(void *ctx, const char *name);
} qa_qvm_client_collision_services;

bool qa_qvm_client_collision_syscall(const qa_qvm_host_call *call,
                                     qa_qvm_client_collision_services services,
                                     qa_qvm_host_result *out);

#endif
