#ifndef QA_FRONTEND_SELECTED_EFFECTS_H
#define QA_FRONTEND_SELECTED_EFFECTS_H

#include "visual_access.h"
#include "qa/application_selected_effects.h"
#include "qa/q3_presentation_save.h"
#include "../../presentation/q3_native/events.h"

typedef struct frontend_selected_effects frontend_selected_effects;
typedef struct frontend_selected_effects_group frontend_selected_effects_group;
typedef struct frontend_selected_effects_pose {
    /* Borrowed only during event(); the actual entered composer reobserves
     * its captured CHARACTER/presentation/world-body precedence. */
    qa_actor_id actor;
    qa_vec3 origin;
    void *context;
    bool (*current)(void *);
} frontend_selected_effects_pose;
typedef struct frontend_selected_effects_ref {
    qa_q3_ref_entity ref;
    float cull_radius;
} frontend_selected_effects_ref;
typedef struct frontend_selected_effects_poly {
    int32_t shader;
    const qa_scene_vertex *vertices;
    size_t count;
    qa_scene_fog_volume fog;
} frontend_selected_effects_poly;
typedef struct frontend_selected_effects_view {
    qa_actor_owner provider;
    qa_product_id product;
    qa_q3_product q3_product;
    uint32_t physical_seat;
    uint64_t identity;
    uint64_t primary_identity;
    /* Actual captured pool/material time, sampled from the world receipt. */
    int32_t source_time_ms;
    uint64_t sampled_application_frame;
    bool prepared;
    frontend_visual_owner_view content;
    const qa_vfs *source_files;
    qa_audio_bank *sounds;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    q3n_media *media;
    q3n_events *events;
} frontend_selected_effects_view;

typedef enum frontend_selected_effects_parent_kind {
    FRONTEND_EFFECTS_PARENT_SOURCE, FRONTEND_EFFECTS_PARENT_NATIVE
} frontend_selected_effects_parent_kind;
typedef struct frontend_selected_effects_parent {
    frontend_selected_effects_parent_kind kind;
    size_t ordinal;
    qa_q3_presentation_binding binding;
} frontend_selected_effects_parent;

/* The actual primary constructor supplies the physical display/audio/map
 * binding. A separate registry and pool emit only the selected producer's
 * canonical Q3 character effects; no primary S/PS or event is translated. */
bool frontend_selected_effects_event(qa_frontend *, const q3n_frame *primary,
    const qa_application_effect_event *, const frontend_selected_effects_pose *, bool *admitted, qa_error *);
/* Advance once before RenderScene. Seat callbacks borrow the resulting refs;
 * the far-world sample preserves each seat's camera-near cull independently. */
bool frontend_selected_effects_prepare(qa_frontend *, const q3n_frame *primary, qa_error *);
bool frontend_selected_effects_lights(qa_frontend *, const q3n_frame *primary, qa_error *);
size_t frontend_selected_effects_ref_count(const frontend_selected_effects_group *);
const frontend_selected_effects_ref *frontend_selected_effects_ref_at(
    const frontend_selected_effects_group *, size_t);
size_t frontend_selected_effects_poly_count(const frontend_selected_effects_group *);
bool frontend_selected_effects_poly_at(const frontend_selected_effects_group *, size_t,
    frontend_selected_effects_poly *, qa_error *);
size_t frontend_selected_effects_count(const qa_frontend *);
bool frontend_selected_effects_at(const qa_frontend *, size_t,
    frontend_selected_effects_view *, qa_error *);
bool frontend_selected_effects_parent_read(const qa_frontend *, size_t,
    frontend_selected_effects_parent *, qa_error *);
bool frontend_selected_effects_q3_ready(const qa_frontend *, size_t,
    const qa_q3_presentation_options *, const qa_q3_presentation_asset_options *, qa_error *);
const frontend_selected_effects_group *frontend_selected_effects_group_at(const qa_frontend *, size_t);
bool frontend_selected_effects_idle(const qa_frontend *);
bool frontend_selected_effects_retire(qa_frontend *, qa_error *);
bool frontend_selected_effects_retire_source(qa_frontend *, qa_actor_owner,
    const qa_launch_instance *, qa_error *);
bool frontend_selected_effects_retire_parent(qa_frontend *, const qa_q3_presentation *, qa_error *);
bool frontend_selected_effects_output_read(const qa_frontend *, const q3n_frame *, size_t,
    const qa_q3_scene_options *, const qa_scene_frame *, const frontend_selected_effects_group **,
    frontend_selected_effects_view *, qa_error *);
bool frontend_selected_effects_round(qa_frontend *, qa_error *);
bool frontend_selected_effects_content_visit(const qa_frontend *,
    const qa_application_content_visitor *, qa_error *);

#endif
