#ifndef QA_FRONTEND_SELECTED_CHARACTER_H
#define QA_FRONTEND_SELECTED_CHARACTER_H

#include "visual_access.h"
#include "qa/application_character_selection.h"
#include "qa/application_selected_q3_character.h"
#include "qa/application_q3_asset_selection.h"
#include "qa/persistence_content.h"
#include "../../presentation/q3_native/selected_media.h"

typedef struct frontend_selected_character frontend_selected_character;
typedef struct frontend_selected_character_pose frontend_selected_character_pose;
typedef struct frontend_selected_character_output frontend_selected_character_output;
typedef struct frontend_selected_character_view {
    uint32_t launch_seat;
    qa_native_q3_character_selection selection;
    qa_application_q3_asset_selection appearance;
    const qa_launch_instance *appearance_launch;
    frontend_visual_owner_view content;
    qa_q3_presentation_assets *assets;
    int32_t models[3], skins[3];
    const qa_resource *resources[8];
    const qa_vfs_acquisition *receipts[8];
    const qa_player_animation_config *animation;
} frontend_selected_character_view;

typedef struct frontend_selected_character_frame {
    int32_t time, frame_milliseconds;
    float swing_speed, shader_time, shadow_plane;
    bool no_player_animations, personal_model, has_shadow_plane;
} frontend_selected_character_frame;
typedef struct frontend_selected_character_pass {
    /* 0/1/2 are source legs/torso/head; 3 is an authored whole-body pass. */
    uint32_t part;
    /* The actual source-part helper ordinal; repetitions in it remain. */
    uint32_t helper;
    qa_q3_ref_entity material;
} frontend_selected_character_pass;

/* Admission owns an immutable model/skin/animation bundle in the selected
 * declaration's genuine content. Unsupported or absent declarations leave
 * admitted=false; the caller keeps the primary body until admission succeeds. */
bool frontend_selected_character_prepare(qa_frontend *, uint32_t launch_seat,
    uint32_t physical_seat, const qa_application_selected_q3_character *,
    void *context, bool (*current)(void *), frontend_selected_character_pose **,
    bool *admitted, qa_error *);
bool frontend_selected_character_animation(const frontend_selected_character_pose *,
    q3n_selected_animation *, qa_error *);
bool frontend_selected_character_build(frontend_selected_character_pose *,
    const qa_application_selected_q3_character *, const frontend_selected_character_frame *,
    frontend_selected_character_output **, qa_error *);
size_t frontend_selected_character_output_count(const frontend_selected_character_output *);
const qa_q3_ref_entity *frontend_selected_character_output_part(
    const frontend_selected_character_output *, uint32_t part);
const qa_q3_presentation_assets *frontend_selected_character_output_assets(
    const frontend_selected_character_output *);
bool frontend_selected_character_output_current(const frontend_selected_character_output *);
bool frontend_selected_character_output_origin(const frontend_selected_character_output *,
    qa_actor_id expected_actor, qa_vec3 *, qa_error *);
bool frontend_selected_character_output_passes(frontend_selected_character_output *,
    qa_q3_presentation *, const qa_q3_presentation_assets *primary,
    const frontend_selected_character_pass *, size_t,
    const qa_q3_scene_options *, uint32_t first_order, qa_scene_frame *, qa_error *);
bool frontend_selected_character_output_submit(frontend_selected_character_output *,
    qa_q3_presentation *, const qa_q3_scene_options *, uint32_t first_order,
    qa_scene_frame *, qa_error *);
void frontend_selected_character_output_destroy(frontend_selected_character_output *);
bool frontend_selected_character_idle(const qa_frontend *);
bool frontend_selected_character_retire(qa_frontend *, qa_error *);
size_t frontend_selected_character_count(const qa_frontend *);
bool frontend_selected_character_at(const qa_frontend *, size_t,
    frontend_selected_character_view *, qa_error *);
bool frontend_selected_character_content_visit(const qa_frontend *,
    const qa_application_content_visitor *, qa_error *);

#endif
