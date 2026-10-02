#ifndef QA_FRONTEND_REMOTE_UNIFIED_RENDER_H
#define QA_FRONTEND_REMOTE_UNIFIED_RENDER_H
#include "remote_unified_media.h"
#include "remote_unified_prediction.h"
typedef struct frontend_unified_render frontend_unified_render;
/* Owns a clone of one actually received frame and registered immutable model
 * bindings. No local application world or player state is observed. */
bool frontend_unified_render_create(qa_frontend *, frontend_remote_unified *,
    frontend_unified_media *, const qa_unified_document *, frontend_unified_render **, qa_error *);
bool frontend_unified_render_draw(frontend_unified_render *,
    const frontend_unified_prediction_view *, float stereo, qa_audio_listener *, qa_error *);
bool frontend_unified_render_destroy(frontend_unified_render **, qa_error *);
#endif
