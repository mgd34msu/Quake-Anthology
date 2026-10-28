/* Port of src/app/bootstrap/renderer-diagnostics.ts over landed
 * command-buffer / material / Q3-resource types. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_RENDERER_DIAGNOSTICS_H
#define QA_APP_BOOTSTRAP_RENDERER_DIAGNOSTICS_H
#include <stddef.h>
#include "qa/app/bootstrap/renderer.h"
#include "qa/content/q3/presentation/resources.h"
#include "qa/contracts/common.h"
#include "qa/core/commands/index.h"
#include "qa/render/scene/material_registrations.h"
typedef struct qa_app_renderer_diagnostic_services {
  void *ctx;
  qa_command_buffer *commands;
  qa_app_renderer_diagnostics (*renderer)(void *ctx);
  const qa_registered_scene_material *(*shaders)(void *ctx, bool sorted, size_t *out_count);
  /* NULL resources = TS null. */
  qa_q3_pres_q3_renderer_resources *(*resources)(void *ctx, qa_command_context source);
  void (*print)(void *ctx, const char *text, qa_command_context source);
} qa_app_renderer_diagnostic_services;
typedef struct qa_app_renderer_diagnostics_registration qa_app_renderer_diagnostics_registration;
/* TS registerRendererDiagnostics returns the unregister closure. */
qa_app_renderer_diagnostics_registration *qa_app_register_renderer_diagnostics(qa_app_renderer_diagnostic_services services);
void qa_app_renderer_diagnostics_unregister(qa_app_renderer_diagnostics_registration *r);
#endif
