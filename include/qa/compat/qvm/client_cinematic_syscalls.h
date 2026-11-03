/*
 * Port of src/compat/qvm/client-cinematic-syscalls.ts.
 * Rect is contracts/render.h qa_rect; Draw2D is text/draw2d.h qa_draw2d
 * (borrowed). TS playGuest's Promise settles synchronously.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_CINEMATIC_SYSCALLS_H
#define QA_COMPAT_QVM_CLIENT_CINEMATIC_SYSCALLS_H

#include <stdbool.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/render.h"
#include "qa/text/draw2d.h"

/* TS QvmClientCinematics. */
typedef struct qa_qvm_client_cinematics {
  void *ctx;
  double (*play_guest)(void *ctx, const char *path, qa_rect rect, double bits);
  double (*run_guest)(void *ctx, double handle);
  double (*stop_guest)(void *ctx, double handle);
  void (*draw_guest)(void *ctx, double handle, const qa_draw2d *draw);
  void (*set_extents)(void *ctx, double handle, qa_rect rect);
} qa_qvm_client_cinematics;

/* TS QvmClientCinematicServices. */
typedef struct qa_qvm_client_cinematic_services {
  qa_qvm_client_cinematics cinematics;
  const qa_draw2d *draw;
  void *ctx;
  void (*developer_print)(void *ctx, const char *text);
} qa_qvm_client_cinematic_services;

bool qa_qvm_client_cinematic_syscall(const qa_qvm_host_call *call,
                                     qa_qvm_client_cinematic_services services,
                                     qa_qvm_host_result *out);

#endif
