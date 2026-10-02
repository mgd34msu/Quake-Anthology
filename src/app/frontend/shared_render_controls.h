#ifndef QA_FRONTEND_SHARED_RENDER_CONTROLS_H
#define QA_FRONTEND_SHARED_RENDER_CONTROLS_H
#include "internal.h"
#include "qa/console_cvars_prepare.h"
#include "qa/render_controls.h"

typedef struct frontend_shared_render_controls frontend_shared_render_controls;
/* Prepare after the surface child, before sealing the canonical ENGINE edit. */
bool frontend_shared_render_controls_prepare(qa_frontend *, const qa_cvars_edit *,
    frontend_shared_render_controls **, qa_error *);
bool frontend_shared_render_controls_ready(const frontend_shared_render_controls *, qa_error *);
bool frontend_shared_render_controls_ready_is(const frontend_shared_render_controls *);
void frontend_shared_render_controls_publish(frontend_shared_render_controls *);
void frontend_shared_render_controls_consume(frontend_shared_render_controls **);
bool frontend_shared_render_controls_finish(frontend_shared_render_controls **, qa_error *);
bool frontend_shared_render_controls_abort(frontend_shared_render_controls **, qa_error *);
/* Read the actual live ENGINE scalar at a reached idle renderer boundary. */
bool frontend_render_controls_live(qa_frontend *, qa_error *);
/* Borrow a real registered ENGINE renderer row, without alias projection. */
const qa_cvar_view *frontend_render_control_record(const qa_cvars *, const char *);
/* CLIENTCG owns this live Source scalar; read it at the reached RenderScene. */
bool frontend_q3_shadow_mode_read(const qa_cvars *, uint32_t *, qa_error *);
#endif
