#ifndef QA_FRONTEND_EQUIPMENT_Q3_H
#define QA_FRONTEND_EQUIPMENT_Q3_H

#include "visual_access.h"
#include "qa/application_equipment.h"
#include "qa/persistence_content.h"
#include "../../presentation/q3_native/selected_media.h"

typedef struct frontend_equipment_q3_presenter frontend_equipment_q3_presenter;
typedef struct frontend_equipment_q3_output frontend_equipment_q3_output;
typedef struct frontend_equipment_q3_owner_view {
    qa_actor_owner provider;
    qa_q3_product product;
    frontend_visual_owner_view content;
    qa_q3_presentation_assets *assets;
    q3n_selected_media *media;
} frontend_equipment_q3_owner_view;

/* Each selected content owns its actual numeric registry. Each full actor
 * owns one presenter shared by genuine view and held consumers. The caller's
 * current callback qualifies the real recipient lease around source services. */
bool frontend_equipment_q3_prepare(qa_frontend *, const qa_application_equipment_view *,
    bool view_required, const q3n_selected_animation *character,
    void *context, bool (*current)(void *), frontend_equipment_q3_presenter **, qa_error *);
bool frontend_equipment_q3_retain(frontend_equipment_q3_presenter *, qa_error *);
void frontend_equipment_q3_release(frontend_equipment_q3_presenter *);
bool frontend_equipment_q3_view(frontend_equipment_q3_presenter *,
    const qa_application_equipment_view *, const q3n_selected_weapon_view *, bool reduced_flashes,
    void *context, bool (*current)(void *), frontend_equipment_q3_output **,
    bool *submitted, qa_error *);
bool frontend_equipment_q3_held(frontend_equipment_q3_presenter *,
    const qa_application_equipment_view *, const q3n_selected_weapon_held *, bool reduced_flashes,
    void *context, bool (*current)(void *), frontend_equipment_q3_output **,
    bool *submitted, qa_error *);
size_t frontend_equipment_q3_output_count(const frontend_equipment_q3_output *);
bool frontend_equipment_q3_output_submit(frontend_equipment_q3_output *, qa_q3_presentation *,
    const qa_q3_scene_options *, uint32_t first_order, qa_scene_frame *, qa_error *);
void frontend_equipment_q3_output_destroy(frontend_equipment_q3_output *);
bool frontend_equipment_q3_idle(const qa_frontend *);
bool frontend_equipment_q3_retire(qa_frontend *, qa_error *);
void frontend_equipment_q3_destroy(qa_frontend *);
size_t frontend_equipment_q3_count(const qa_frontend *);
bool frontend_equipment_q3_at(const qa_frontend *, size_t,
    frontend_equipment_q3_owner_view *, qa_error *);
bool frontend_equipment_q3_content_visit(const qa_frontend *,
    const qa_application_content_visitor *, qa_error *);

#endif
