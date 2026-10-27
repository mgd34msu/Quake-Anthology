/*
 * Port of src/compat/qvm/client-mark-syscalls.ts. The projector's
 * SourceMarkProjection callbacks (content/q3/presentation/
 * mark-projector.ts, content module) mirror as qa_qvmx_mark_query.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_MARK_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_MARK_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/math.h"

typedef struct qa_qvmx_mark_fragment { double first_point, point_count; } qa_qvmx_mark_fragment;

typedef struct qa_qvmx_mark_query {
  double point_count, max_points, max_fragments;
  void *ctx;
  qa_vec3 (*read_projection)(void *ctx);
  qa_vec3 (*read_point)(void *ctx, double index);
  void (*write_fragment)(void *ctx, double index, qa_qvmx_mark_fragment fragment);
  void (*write_points)(void *ctx, double first_point, const qa_vec3 *values, size_t count);
} qa_qvmx_mark_query;

typedef struct qa_qvm_mark_projector {
  void *ctx;
  double (*mark_fragments_record)(void *ctx, qa_qvmx_mark_query query);
} qa_qvm_mark_projector;

bool qa_qvm_client_mark_syscall(const qa_qvm_host_call *call, qa_qvm_mark_projector projector,
                                qa_qvm_host_result *out);

#endif
