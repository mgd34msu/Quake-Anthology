#ifndef QA_FRONTEND_REMOTE_UNIFIED_RENDER_H
#define QA_FRONTEND_REMOTE_UNIFIED_RENDER_H
#include "remote_unified_media.h"
#include "remote_unified_prediction.h"
typedef struct frontend_unified_render frontend_unified_render;
typedef struct frontend_unified_render_children {
    void *context;
    bool (*world_input)(void *, qa_scene_world_input *, qa_error *);
    bool (*lights)(void *, const qa_scene_view *, const qa_scene_world_input *,
        const qa_scene_light **, size_t *, qa_error *);
    bool (*world)(void *, const qa_scene_view *, const qa_scene_world_input *, qa_scene_frame *, qa_error *);
    bool (*hud)(void *, qa_ui *, qa_scene_rect, qa_scene_frame *, qa_error *);
    bool (*model)(void *, qa_actor_id, const char *content, const char *path, qa_scene_model_input *, qa_error *);
} frontend_unified_render_children;
/* Owns a clone of one actually received frame and registered immutable model
 * bindings. No local application world or player state is observed. */
bool frontend_unified_render_create(qa_frontend *, frontend_remote_unified *,
    frontend_unified_media *, const qa_unified_document *, frontend_unified_render **, qa_error *);
bool frontend_unified_render_draw(frontend_unified_render *,
    const frontend_unified_prediction_view *, const frontend_unified_render_children *,
    float stereo, qa_audio_listener *, qa_error *);
bool frontend_unified_render_idle(const frontend_unified_render *);
bool frontend_unified_render_destroy(frontend_unified_render **, qa_error *);
#endif
