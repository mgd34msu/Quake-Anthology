/*
 * Port of src/compat/qvm/client-collision-syscalls.ts.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_COLLISION_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_COLLISION_SYSCALLS_H

#include <stdbool.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/math.h"
#include "qa/world/collision/q3/clip_models.h"
#include "qa/world/collision/q3/model.h"
#include "qa/world/collision/q3/world.h"

/* TS QvmClientClipModels = Pick<SourceClipModels, "modelCount" |
 * "inlineModel" | "tempBoxModel" | "pointContents" |
 * "transformedPointContents" | "traceWithoutNodes" | "trace" |
 * "transformedTrace"> & { world: { hasNodes } }. A structural TS type
 * that SourceClipModels and SharedQvmClientClipModels both satisfy; each
 * member receives ctx. trace_without_nodes false is the TS null. */
typedef struct qa_qvm_client_clip_models {
  void *ctx;
  bool (*has_nodes)(void *ctx);
  double (*model_count)(void *ctx);
  double (*inline_model)(void *ctx, double index);
  double (*temp_box_model)(void *ctx, qa_vec3 min, qa_vec3 max, bool capsule);
  double (*point_contents)(void *ctx, qa_vec3 point, double handle);
  double (*transformed_point_contents)(void *ctx, qa_vec3 point, double handle, qa_vec3 origin,
                                       qa_vec3 angles);
  bool (*trace_without_nodes)(void *ctx, double handle, qa_source_trace_result *out);
  qa_source_trace_result (*trace)(void *ctx, qa_temporary_trace_query query, double handle);
  qa_source_trace_result (*transformed_trace)(void *ctx, qa_temporary_trace_query query,
                                              double handle, qa_vec3 origin, qa_vec3 angles);
} qa_qvm_client_clip_models;

/* SourceClipModels as QvmClientClipModels (ctx is the borrowed models). */
qa_qvm_client_clip_models qa_qvm_source_client_clip_models(qa_source_clip_models *models);

/* TS QvmClientCollisionServices. */
typedef struct qa_qvm_client_collision_services {
  void *ctx;
  qa_qvm_client_clip_models (*models)(void *ctx);
  void (*load_map)(void *ctx, const char *name);
} qa_qvm_client_collision_services;

bool qa_qvm_client_collision_syscall(const qa_qvm_host_call *call,
                                     qa_qvm_client_collision_services services,
                                     qa_qvm_host_result *out);

#endif
