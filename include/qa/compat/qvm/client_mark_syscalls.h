/*
 * Port of src/compat/qvm/client-mark-syscalls.ts. The projector is the
 * content/q3 BspMarkProjector (mark_projector.h); the query is its
 * SourceMarkProjection. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_MARK_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_MARK_SYSCALLS_H

#include <stdbool.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/content/q3/presentation/mark_projector.h"

/* TS Pick<BspMarkProjector, "markFragmentsRecord">: a structural TS type
 * over the content SourceMarkProjection query; member receives ctx. */
typedef struct qa_qvm_mark_projector {
  void *ctx;
  double (*mark_fragments_record)(void *ctx, qa_q3_pres_source_mark_projection query);
} qa_qvm_mark_projector;

/* BspMarkProjector as the Pick (ctx is the borrowed projector). */
qa_qvm_mark_projector qa_qvm_bsp_mark_projector(qa_q3_pres_bsp_mark_projector *projector);

/* TS qvmClientMarkSyscall: false is TS null. */
bool qa_qvm_client_mark_syscall(const qa_qvm_host_call *call, qa_qvm_mark_projector projector,
                                qa_qvm_host_result *out);

#endif
