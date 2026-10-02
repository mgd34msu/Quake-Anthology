#ifndef QA_FRONTEND_Q3_RENDER_POLICY_H
#define QA_FRONTEND_Q3_RENDER_POLICY_H
#include "shared_render_controls.h"

/* Actual Source constructors use committed ENGINE rows before registration;
 * prepared resource children use their returned candidate ENGINE edit. */
bool frontend_q3_material_profile_read(qa_frontend *, const qa_cvars_edit *,
    qa_material_profile *, qa_error *);
bool frontend_q3_material_profile_initialize(qa_frontend *, qa_material_library *, qa_error *);
bool frontend_q3_material_ui_fullscreen_read(void *, bool *, qa_error *);
bool frontend_q3_material_source_bind(qa_frontend *, qa_material_library *, qa_error *);
bool frontend_q3_renderer_options_read(qa_frontend *, qa_q3_presentation_options *, qa_error *);
/* Install live Source view scalars after effectful scene preparation. */
bool frontend_q3_scene_policy_read(qa_frontend *, qa_q3_scene_options *, qa_error *);
bool frontend_q3_material_diagnostics_read(qa_frontend *, qa_scene_source_diagnostics *, qa_error *);
bool frontend_q3_frame_policy_read(qa_frontend *, qa_error *);
bool frontend_q3_texture_mode_initialize(qa_frontend *, qa_error *);
bool frontend_q3_texture_mode_begin_frame(qa_frontend *, qa_error *);
/* Apply Source world construction controls before immutable geometry loads. */
bool frontend_q3_world_policy_read(qa_frontend *, const qa_cvars_edit *,
    qa_scene_world_options *, qa_error *);
bool frontend_q3_world_policy_initialize(qa_frontend *, qa_scene_world_options *, qa_error *);
#endif
