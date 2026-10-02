#ifndef QA_FRONTEND_REMOTE_UNIFIED_RENDER_SAVE_H
#define QA_FRONTEND_REMOTE_UNIFIED_RENDER_SAVE_H
#include "remote_unified_render.h"
#include "scene_identity.h"
#include "qa/hud.h"
typedef struct frontend_unified_render_refs {
    frontend_scene_namespace *scene;
    qa_hud_checkpoint_refs hud;
} frontend_unified_render_refs;
bool frontend_unified_render_checkpoint(frontend_unified_render *,
    const frontend_unified_render_refs *, qa_buffer *, qa_error *);
bool frontend_unified_render_restore(qa_frontend *, frontend_remote_unified *,
    frontend_unified_media *, const frontend_unified_render_refs *, qa_bytes,
    frontend_unified_render **, qa_error *);
#endif
