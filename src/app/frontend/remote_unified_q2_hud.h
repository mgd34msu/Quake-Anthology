#ifndef QA_FRONTEND_REMOTE_UNIFIED_Q2_HUD_H
#define QA_FRONTEND_REMOTE_UNIFIED_Q2_HUD_H
#include "remote_unified_q2.h"

typedef struct frontend_unified_q2_rr_hud frontend_unified_q2_rr_hud;
bool frontend_unified_q2_rr_known(const qa_unified_document *, qa_json_id);
bool frontend_unified_q2_rr_create(qa_frontend *, frontend_remote_unified *,
    frontend_unified_media *, frontend_unified_events *, frontend_unified_q2_rr_hud **, qa_error *);
bool frontend_unified_q2_rr_validate(frontend_unified_q2_rr_hud *,
    const qa_unified_document *, qa_json_id, qa_error *);
bool frontend_unified_q2_rr_presentation(frontend_unified_q2_rr_hud *,
    const qa_unified_document *, qa_json_id, bool *, qa_error *);
bool frontend_unified_q2_rr_help_visible(const frontend_unified_q2_rr_hud *);
bool frontend_unified_q2_rr_overlay(frontend_unified_q2_rr_hud *,
    const qa_unified_document *, qa_json_id, qa_error *);
bool frontend_unified_q2_rr_owner_validate(frontend_unified_q2_rr_hud *,
    const qa_unified_document *, qa_json_id, qa_error *);
bool frontend_unified_q2_rr_owner_retire(frontend_unified_q2_rr_hud *,
    const qa_unified_document *, qa_json_id, qa_error *);
bool frontend_unified_q2_rr_frame_prepare(frontend_unified_q2_rr_hud *, const qa_unified_document *, qa_error *);
bool frontend_unified_q2_rr_frame_ready(frontend_unified_q2_rr_hud *, const qa_unified_document *, qa_error *);
bool frontend_unified_q2_rr_frame_restore_bind(frontend_unified_q2_rr_hud *, const qa_unified_document *, qa_error *);
void frontend_unified_q2_rr_frame_commit(frontend_unified_q2_rr_hud *);
void frontend_unified_q2_rr_frame_abort(frontend_unified_q2_rr_hud *);
bool frontend_unified_q2_rr_world(frontend_unified_q2_rr_hud *, const qa_scene_view *,
    const qa_scene_world_input *, qa_scene_frame *, qa_error *);
bool frontend_unified_q2_rr_draw(frontend_unified_q2_rr_hud *, qa_ui *,
    qa_scene_rect, qa_scene_frame *, qa_error *);
bool frontend_unified_q2_rr_idle(const frontend_unified_q2_rr_hud *);
bool frontend_unified_q2_rr_destroy(frontend_unified_q2_rr_hud **, qa_error *);
bool frontend_unified_q2_rr_checkpoint(frontend_unified_q2_rr_hud *,
    const frontend_unified_q2_refs *, qa_buffer *, qa_error *);
bool frontend_unified_q2_rr_restore(qa_frontend *, frontend_remote_unified *,
    frontend_unified_media *, frontend_unified_events *, const frontend_unified_q2_refs *,
    qa_bytes, frontend_unified_q2_rr_hud **, qa_error *);
bool frontend_unified_q2_rr_checkpoint_ready(const frontend_unified_q2_rr_hud *);
#endif
