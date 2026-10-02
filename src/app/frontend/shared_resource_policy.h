#ifndef QA_FRONTEND_SHARED_RESOURCE_POLICY_H
#define QA_FRONTEND_SHARED_RESOURCE_POLICY_H

#include "internal.h"
#include "qa/console_cvars_prepare.h"
#include "qa/application_client_prepare.h"

typedef struct frontend_shared_resource_policy frontend_shared_resource_policy;
typedef struct frontend_video_guests frontend_video_guests;
typedef struct frontend_model_policy {
    bool q1_enhanced, q2_load, q2_use, source_distance;
    double q2_distance, distance;
} frontend_model_policy;

/* Actual ENGINE values, preserving the source numeric thresholds and the
 * literal source-distance policy. No source-family registry substitutes. */
bool frontend_model_policy_edit_read(const qa_cvars_edit *, frontend_model_policy *, qa_error *);
bool frontend_model_policy_read(const qa_frontend *, frontend_model_policy *, qa_error *);
bool frontend_model_policy_load(const frontend_model_policy *, qa_scene_family, const qa_model *);
bool frontend_model_policy_select(const frontend_model_policy *, qa_scene_family, const qa_model *,
    double distance, bool shadow);
double frontend_model_policy_distance(const frontend_model_policy *, const qa_model *);
bool frontend_image_policy_read(const qa_frontend *, qa_scene_image_policy policies[3], qa_error *);
bool frontend_image_policy_edit_read(const qa_cvars_edit *, qa_scene_image_policy policies[3], qa_error *);
/* Fresh live banks use the exact pending candidate edit when one is held.
 * Imported detached banks keep their genuine decoded policy continuation. */
bool frontend_image_policy_initialize(qa_frontend *, qa_scene_resources *, qa_error *);
bool frontend_resource_policy_admission_edit(const qa_frontend *, const qa_cvars_edit **, qa_error *);

/* The actual canonical ticket and complete frontend resource roster remain
 * held through preparation, publication and checked retirement. */
bool frontend_shared_resource_policy_prepare(qa_frontend *, const qa_launch_snapshot *, const qa_cvars_edit *,
    frontend_shared_resource_policy **, qa_error *);
bool frontend_shared_resource_policy_begin(qa_frontend *, const qa_launch_snapshot *, const qa_cvars_edit *,
    frontend_shared_resource_policy **, qa_error *);
bool frontend_shared_resource_policy_begin_client(qa_frontend *, const qa_application_client_preparation *,
    const qa_cvars_edit *, frontend_shared_resource_policy **, qa_error *);
bool frontend_shared_resource_policy_prepare_children(frontend_shared_resource_policy *, qa_error *);
/* Only the real vid_restart ticket admits this committed-policy refresh after
 * physical renderer replacement and before reconstructed CG/UI Init. */
bool frontend_shared_resource_policy_restart_prepare(qa_frontend *, const frontend_video_guests *,
    frontend_shared_resource_policy **, qa_error *);
qa_scene_resource_policy *frontend_shared_resource_policy_images(
    const frontend_shared_resource_policy *, const qa_scene_resources *);
qa_font_resource_policy *frontend_shared_resource_policy_fonts(
    const frontend_shared_resource_policy *, const qa_font_library *);
bool frontend_shared_resource_policy_ready(frontend_shared_resource_policy *, qa_error *);
bool frontend_shared_resource_policy_ready_is(const frontend_shared_resource_policy *);
void frontend_shared_resource_policy_publish(frontend_shared_resource_policy *);
/* Entered StartupFlow consumption has installed the exact candidate. Every
 * retained child receipt and the complete old physical roster still qualify. */
bool frontend_shared_resource_policy_consume_ready_is(const frontend_shared_resource_policy *);
void frontend_shared_resource_policy_consume(frontend_shared_resource_policy *);
/* After bank migration and actual native surface/gamma transfer. */
void frontend_shared_resource_policy_render_publish(frontend_shared_resource_policy *);
bool frontend_shared_resource_policy_finish(frontend_shared_resource_policy **, qa_error *);
bool frontend_shared_resource_policy_abort(frontend_shared_resource_policy **, qa_error *);

#endif
