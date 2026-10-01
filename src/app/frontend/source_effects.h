#ifndef QA_FRONTEND_SOURCE_EFFECTS_H
#define QA_FRONTEND_SOURCE_EFFECTS_H

#include "internal.h"
#include "qa/application_q3_client.h"
#include "qa/q3_presentation_save.h"
#include "qa/ui_preferences.h"

typedef struct frontend_source_effects frontend_source_effects;

/* The genuine host RenderScene enter owns this token until its paired leave.
 * A failed begin may retain a token; end consumes it without source callbacks.
 * The caller retains its exact frontend role lease for the entire bracket.
 * Actual pre-Init loading renders return success without an effects token. */
bool frontend_source_effects_begin(qa_frontend *, const qa_q3_host *,
    const qa_qvm_call *, const qa_application_q3_client_context *,
    qa_q3_presentation *, uint32_t physical_seat, const qa_q3_refdef *,
    frontend_source_effects **, qa_error *);
bool frontend_source_effects_current(const frontend_source_effects *);
bool frontend_source_effects_prepare(frontend_source_effects *,
    const qa_q3_refdef *, qa_q3_scene_options *, qa_error *);
bool frontend_source_effects_submit(frontend_source_effects *,
    const qa_q3_scene_options *, qa_scene_frame *, qa_error *);
void frontend_source_effects_end(frontend_source_effects *);

/* Borrow only this actual entered host's physical backend and preference
 * snapshot. No native GAME frame, source clock, or source entity is invented. */
bool frontend_source_effects_primary_read(const frontend_source_effects *,
    const qa_frontend *, qa_q3_presentation **, uint32_t *, qa_ui_preferences *);
bool frontend_source_effects_binding_read(const frontend_source_effects *,
    const qa_frontend *, qa_q3_presentation_binding *);

#endif
