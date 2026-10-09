#ifndef QA_FRONTEND_SHARED_REGISTER_H
#define QA_FRONTEND_SHARED_REGISTER_H
#include "internal.h"
#include "qa/console_cvars_prepare.h"
#include "remote_q2_effects.h"
/* The factory supplies its actual selected source dialect, or NULL for a
 * menu with no source. NULL retains generic effects/music defaults 0.7/1;
 * Q3 source defaults are 0.8/0.25. Native output/gamma come from their real
 * owners. Register before configuration or pure QACV import on ENGINE. */
bool frontend_shared_register(qa_cvars *,const qa_ruleset_id *,
    qa_audio_output_format,float gamma,qa_error *);
bool frontend_shared_menu_track_valid(const char *);
/* Called by the real CLIENT initializer on its new physical heap. The retained
 * normalized profile selects Classic/Rerelease defaults; cold import keeps
 * the already imported private records. */
bool frontend_source_q2_settings_register(const qa_launch_instance *,qa_cvars *,
    const qa_command_context *,qa_error *);
bool frontend_source_q2_effects_register(qa_cvars *, const qa_command_context *,
    frontend_remote_q2_effects_profile, qa_error *);
/* The actual first/restarted physical Source renderer applies R_Register
 * latches and near-clip initialization before consuming its policy rows.
 * Imported renderers retain their decoded continuation. */
bool frontend_shared_q3_renderer_initialize(qa_frontend *,qa_error *);
/* Actual Source color initialization normalizes its own canonical rows on
 * the exact pending edit, or the returned live registry when no edit exists. */
bool frontend_source_color_clamp(qa_frontend *,const qa_cvars_edit *,qa_error *);
/* The actual first/restarted physical Source color owner applies R_Register
 * latches before capability observation. Ordinary namespace reads do not. */
bool frontend_source_color_register(qa_frontend *,const qa_cvars_edit *,qa_error *);
/* Caller-qualified live registry or returned canonical edit initialization. */
bool frontend_source_renderer_values_initialize(qa_cvars *,qa_cvars_edit *,qa_error *);
bool frontend_source_color_values_register(qa_cvars *,qa_cvars_edit *,qa_error *);
bool frontend_source_color_values_initialize(qa_cvars *,qa_cvars_edit *,qa_error *);
#endif
