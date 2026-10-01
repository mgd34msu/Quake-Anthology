#ifndef QA_FRONTEND_UI_RESTORE_H
#define QA_FRONTEND_UI_RESTORE_H
#include "internal.h"
#include "scene_identity.h"
#include "qa/ui_save.h"
#include "qa/input_save.h"

/* Input graph import precedes the controller. The controller resolver only
 * observes its real prepared handler and that handler's restored input token. */
qa_ui_checkpoint_refs frontend_seat_ui_refs(frontend_seat *);
qa_hud_checkpoint_refs frontend_hud_image_refs(frontend_scene_namespace *);
#endif
