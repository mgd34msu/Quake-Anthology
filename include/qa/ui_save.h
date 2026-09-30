#ifndef QA_UI_SAVE_H
#define QA_UI_SAVE_H
#include "qa/hud.h"
#include "qa/source_save.h"
typedef struct qa_ui_checkpoint_refs {
    void *context;
    bool (*image_encode)(void *, const qa_scene_image *, qa_buffer *, qa_error *);
    bool (*image_decode)(void *, qa_bytes, const qa_scene_image **, qa_error *);
} qa_ui_checkpoint_refs;
bool qa_hud_checkpoint(qa_hud *, const qa_ui_checkpoint_refs *, qa_buffer *, qa_error *);
/* Candidate UI, application and image owners must outlive the returned HUD.
 * Restoration invokes no HUD read/source_draw or input/menu callbacks. */
bool qa_hud_restore(qa_bytes, const qa_hud_options *, const qa_ui_checkpoint_refs *, qa_hud **, qa_error *);
#endif
