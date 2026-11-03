/*
 * Port of src/compat/qvm/client-render-syscalls.ts. resources is the
 * content/q3 Q3RendererResources (resources.h) and draw the text Draw2D
 * (draw2d.h). AddPolys vertices are borrowed for each addPoly call only.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_RENDER_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_RENDER_SYSCALLS_H

#include <stdbool.h>

#include "qa/compat/qvm/render_record.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/content/q3/presentation/resources.h"
#include "qa/text/draw2d.h"

/* TS qvmClientRenderSyscall: false is TS null. */
bool qa_qvm_client_render_syscall(const qa_qvm_host_call *call,
                                  qa_q3_pres_q3_renderer_resources *resources,
                                  const qa_draw2d *draw, qa_qvm_host_result *out);

#endif
