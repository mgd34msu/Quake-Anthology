#ifndef QA_FRONTEND_VISUAL_ACCESS_H
#define QA_FRONTEND_VISUAL_ACCESS_H

#include "visual_restore.h"

/* Live media admission through the actual selected provider. Returned holders
 * borrow the physical appearance cache; restore uses imported inventories. */
bool frontend_visual_model_acquire(qa_frontend *, qa_actor_owner provider,
    qa_game_family, const char *path, const qa_resource *retained_source,
    frontend_visual_model_view *, qa_error *);
bool frontend_visual_media_acquire(qa_frontend *, qa_actor_owner provider,
    qa_game_family, frontend_visual_owner_view *, qa_error *);

#endif
