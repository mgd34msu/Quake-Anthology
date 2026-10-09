#ifndef QA_FRONTEND_Q3_RENDER_POLICY_H
#define QA_FRONTEND_Q3_RENDER_POLICY_H
#include "shared_render_controls.h"

/* Bind fixed ENGINE row references after shared registration or registry replacement. */
void frontend_render_cvars_bind(qa_frontend *);

/* Actual Source constructors use committed ENGINE rows before registration;
 * prepared resource children use their returned candidate ENGINE edit. */
bool frontend_q3_material_profile_read(qa_frontend *, const qa_cvars_edit *,
    qa_material_profile *, qa_error *);
bool frontend_q3_material_profile_initialize(qa_frontend *, qa_material_library *, qa_error *);
bool frontend_q3_material_ui_fullscreen_read(void *, bool *, qa_error *);
bool frontend_q3_material_source_bind(qa_frontend *, qa_material_library *, qa_error *);
bool frontend_q3_source_image_admit(void *,const qa_scene_image *,uint32_t texture_unit,qa_error *);
bool frontend_q3_source_no_bind_read(qa_frontend *,const qa_cvars_edit *,bool *,qa_error *);
bool frontend_q3_source_restart_read(qa_frontend *,const qa_cvars_edit *,
    qa_render_source_restart_values *,qa_error *);
bool frontend_q3_renderer_options_read(qa_frontend *, qa_q3_presentation_options *, qa_error *);
bool frontend_q3_renderer_hardware_read(const qa_frontend *,int32_t *,uint32_t *,qa_error *);
/* Install live Source view scalars after effectful scene preparation. */
bool frontend_q3_scene_policy_read(qa_frontend *, qa_q3_scene_options *, qa_error *);
bool frontend_q3_material_diagnostics_read(qa_frontend *, qa_scene_source_diagnostics *, qa_error *);
bool frontend_q3_frame_policy_read(qa_frontend *, qa_error *);
bool frontend_q3_texture_mode_initialize(qa_frontend *, qa_error *);
bool frontend_q3_scene_limits_initialize(qa_frontend *,qa_error *);
bool frontend_q3_texture_mode_begin_frame(qa_frontend *, qa_error *);
bool frontend_q3_source_begin_frame(qa_frontend *,int32_t stereo_frame,qa_error *);
/* Apply Source world construction controls before immutable geometry loads. */
bool frontend_q3_world_policy_read(qa_frontend *, const qa_cvars_edit *,
    qa_scene_world_options *, qa_error *);
bool frontend_q3_world_policy_initialize(qa_frontend *, qa_scene_world_options *, qa_error *);
#endif
