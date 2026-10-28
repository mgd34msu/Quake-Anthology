/* Port of src/app/bootstrap/q3-client/qvm-display.ts. The
 * registerFont promise chain is synchronous. Resources ride the
 * landed RendererResources vtable (services.resources).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_QVM_DISPLAY_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_QVM_DISPLAY_H
#include <stdbool.h>
#include "qa/app/bootstrap/q3_client/assets.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/content/q3/presentation/resources.h"
/* TS QvmDisplayOptions. */
typedef struct qa_app_qvm_display_options {
  /* Pick<NativeRenderer, "backend"|"driver"|"glConfig">. */
  double backend_stencil_bits;
  const qa_app_gl_driver *driver;    /* NULL = TS null */
  const qa_app_gl_config *gl_config; /* NULL = TS null */
  qa_app_application_q3_assets *media;
  qa_q3_pres_q3_renderer_resources *resources; /* services.resources */
  void *viewport_ctx;
  void (*viewport)(void *ctx, double *out_width, double *out_height);
  void *assert_ctx;
  void (*assert_current)(void *ctx);
} qa_app_qvm_display_options;
/* TS qvmDisplaySyscall: false is the TS null. */
bool qa_app_qvm_display_syscall(const qa_qvm_host_call *call, qa_app_qvm_display_options options,
                                qa_qvm_host_result *out);
#endif
