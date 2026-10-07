#ifndef QA_FRONTEND_EQUIPMENT_SOURCE_H
#define QA_FRONTEND_EQUIPMENT_SOURCE_H

#include "equipment_media.h"
#include "qa/application_q3_equipment_source.h"
#include "qa/application_q3_client.h"

typedef struct frontend_equipment_source frontend_equipment_source;
typedef struct frontend_equipment_source_options {
    qa_frontend *frontend;
    qa_actor_owner receiver;
    uint32_t seat, physical_seat;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    void *lease;
    /* Borrow increments the actual linked CGAME lease operation counters.
     * Current is a pure full-context/lifetime qualification; release closes
     * the successful borrow after all held scopes and view submissions unwind. */
    bool (*borrow)(void *, qa_application_q3_client_context *, qa_error *);
    bool (*current)(void *, const qa_application_q3_client_context *);
    void (*release)(void *);
    /* Native CGAME supplies its genuine frame/cvar request producer. Original
     * CGAME uses the retained source hook requests when this is NULL. */
    bool (*requests)(void *, bool *hud, bool *view, qa_error *);
    void *requests_context;
} frontend_equipment_source_options;

bool frontend_equipment_source_create(const frontend_equipment_source_options *,
    frontend_equipment_source **, qa_error *);
void frontend_equipment_source_services(frontend_equipment_source *,
    qa_application_q3_equipment_services *);
bool frontend_equipment_source_idle(const frontend_equipment_source *);
bool frontend_equipment_source_destroy(frontend_equipment_source *, qa_error *);
void frontend_equipment_source_clear(frontend_equipment_source *);
/* Actual declared weapon scopes attribute Source queue indices to full actors.
 * Borrow these only at the successful scene_completed callback before clear. */
bool frontend_equipment_source_scene_actors(const frontend_equipment_source *,
    size_t entity_count, qa_actor_id *out, qa_error *);
bool frontend_equipment_source_scene_views(const frontend_equipment_source *,
    size_t entity_count, bool *out, qa_error *);
bool frontend_equipment_source_scene_polygons(const frontend_equipment_source *,
    size_t polygon_count, qa_actor_id *actors, bool *views, qa_error *);
bool frontend_equipment_source_scene_lights(const frontend_equipment_source *,
    size_t light_count, qa_actor_id *actors, bool *views, qa_error *);
bool frontend_equipment_source_scene_weapons(const frontend_equipment_source *,
    const qa_application_q3_equipment_source_weapon **rows, size_t *count, qa_error *);
bool frontend_equipment_source_rebind_ready(const frontend_equipment_source *,
    const qa_frontend *owned, qa_error *);
void frontend_equipment_source_rebind(frontend_equipment_source *, qa_frontend *destination);
bool frontend_equipment_source_weapon(const frontend_equipment_source *,
    qa_application_equipment_view *, bool *requested, qa_error *);
/* Native CGAME supplies the real physical actor, authored torso and source
 * powerups. Authored=true routes through the held-pass services; otherwise
 * suppression requires submitted=true from the native selected Q3 kernel. */
bool frontend_equipment_source_native_held(frontend_equipment_source *,
    qa_actor_id, const qa_q3_ref_entity *, int32_t powerups, bool personal_model,
    bool *authored, bool *submitted, qa_error *);
bool frontend_equipment_source_native_held_from(frontend_equipment_source *,
    qa_actor_id, const qa_q3_presentation_assets *parent_assets, const qa_q3_ref_entity *,
    int32_t powerups, bool personal_model, bool *authored, bool *submitted, qa_error *);
bool frontend_equipment_source_selected_read(frontend_equipment_source *,
    qa_actor_id, qa_application_equipment_view *, qa_error *);
bool frontend_equipment_source_held_begin_from(frontend_equipment_source *,
    qa_actor_id, const qa_q3_presentation_assets *parent_assets, const qa_q3_ref_entity *,
    void **token, bool *selected, qa_error *);
/* Own selected view output is prepared before the primary kernel. An actual
 * selected hidden request is consumed without constructing a source parent. */
bool frontend_equipment_source_native_view(frontend_equipment_source *, float field_of_view,
    bool *consumed, qa_error *);
/* Called by the real source renderer owner at its current view boundaries. */
bool frontend_equipment_source_prepare_view(frontend_equipment_source *,
    const qa_q3_refdef *, qa_q3_scene_options *, qa_error *);
bool frontend_equipment_source_submit(frontend_equipment_source *,
    const qa_q3_scene_options *, qa_scene_frame *, qa_error *);
/* Retains the actual last Draw HUD observation. The caller supplies its fully
 * qualified restored client tuple; decode invokes no constructor or service. */

#endif
