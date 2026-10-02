#ifndef QA_FRONTEND_Q3_COLOR_POLICY_H
#define QA_FRONTEND_Q3_COLOR_POLICY_H
#include "internal.h"
#include "qa/q3_color.h"
#include "qa/console_cvars_prepare.h"
#include "qa/display_save.h"

typedef struct frontend_q3_color frontend_q3_color;
typedef struct frontend_q3_color_ticket frontend_q3_color_ticket;
/* Entered by a genuine Source renderer constructor before its image loads. */
bool frontend_q3_source_color_ensure(qa_frontend *, qa_error *);
bool frontend_q3_source_color_lighting_read(qa_frontend *, const qa_cvars_edit *,
    qa_q3_color_lighting *, qa_error *);
bool frontend_q3_source_color_device_read(qa_frontend *, qa_q3_color_device *, qa_error *);
bool frontend_q3_source_upload_read(void *frontend, bool allow_picmip, bool mipmap,
    qa_q3_image_upload_options *, qa_error *);
bool frontend_q3_source_output(qa_frontend *, const qa_material_library *, qa_scene_rect, qa_error *);
/* Bracket generic QAUI/console primitives in an actual Source viewport. */
bool frontend_q3_generic_overlay_begin(qa_frontend *, qa_scene_rect, qa_error *);
bool frontend_q3_generic_overlay_end(qa_frontend *, qa_error *);
bool frontend_q3_source_recipient(qa_frontend *, qa_scene_world_input *, qa_error *);
/* A generic recipient reconstructs admitted Source textures in its own image
 * domain; ordinary generic output gamma remains the physical authority. */
bool frontend_q3_generic_recipient(qa_frontend *, qa_scene_world_input *, qa_error *);
bool frontend_q3_source_color_retire(qa_frontend *, qa_error *);
/* Target is the real installed display or entered native surface candidate.
 * Prepare before Source image-bank preparation; publish after video ownership. */
bool frontend_q3_source_color_prepare(qa_frontend *, const qa_cvars_edit *, qa_display *,
    frontend_q3_color_ticket **, qa_error *);
bool frontend_q3_source_color_restore_prepare(qa_frontend *, qa_display *,
    frontend_q3_color_ticket **, qa_error *);
bool frontend_q3_source_color_ready(frontend_q3_color_ticket *, qa_error *);
bool frontend_q3_source_color_ready_is(const frontend_q3_color_ticket *);
void frontend_q3_source_color_publish(frontend_q3_color_ticket *);
bool frontend_q3_source_color_abort(frontend_q3_color_ticket **, qa_error *);
bool frontend_q3_source_color_finish(frontend_q3_color_ticket **, qa_error *);
/* Cold-import cleanup stays rooted in the actual color owner after its
 * temporary alias is discarded. Abort must complete before display rollback. */
void frontend_q3_source_color_defer_finish(frontend_q3_color_ticket **);
void frontend_q3_source_color_defer_abort(frontend_q3_color_ticket **);
bool frontend_q3_source_color_publication_finish(qa_frontend *, qa_error *);
bool frontend_q3_source_color_checkpoint(const qa_frontend *, qa_buffer *, qa_error *);
/* Pure install after actual detached display/renderer import, before restored
 * Source material callbacks bind. No initialization, clamps or native writes. */
bool frontend_q3_source_color_restore(qa_frontend *, const qa_display_restore_guard *, qa_bytes, qa_error *);
#endif
