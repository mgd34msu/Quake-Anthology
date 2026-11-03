/* Port of src/app/bootstrap/render-settings.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_RENDER_SETTINGS_H
#define QA_APP_BOOTSTRAP_RENDER_SETTINGS_H
#include "qa/core/cvars/index.h"
#include "qa/materials/shader_types.h"
#include "qa/render/scene/particles/primitives.h"
/* TS registerRenderSettings: r_shadows plus registerQ3RendererCvars. */
void qa_app_register_render_settings(qa_cvar_registry *cvars);
/* TS registerQ3RendererCvars (tr_init.c renderer cvars). */
void qa_app_register_q3_renderer_cvars(qa_cvar_registry *cvars);
/* TS Q3RendererSettings. */
typedef struct qa_app_q3_renderer_settings {
  qa_rail_settings rail;
  double curve_error;
} qa_app_q3_renderer_settings;
/* TS q3RendererSettings; cvars NULL is TS null/undefined. */
qa_app_q3_renderer_settings qa_app_q3_renderer_settings_from(qa_cvar_registry *cvars);
/* TS shaderProfileFromCvars; cvars NULL is TS null/undefined. */
qa_mat_finish_profile qa_app_shader_profile_from_cvars(qa_cvar_registry *cvars);
#endif
