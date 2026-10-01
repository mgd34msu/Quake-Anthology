#ifndef QA_APPLICATION_SELECTED_Q3_CHARACTER_H
#define QA_APPLICATION_SELECTED_Q3_CHARACTER_H

#include "qa/application.h"
#include "qa/game_q3.h"

/* A copied canonical CHARACTER presentation, independent of GAME client
 * numbering. Metadata and game remain borrowed at this completed source cut. */
typedef struct qa_application_selected_q3_character {
    qa_actor_id actor;
    qa_actor_owner provider;
    const qa_q3_game *game;
    const qa_launch_instance *launch;
    qa_product_id product;
    qa_source_frame source_frame;
    int32_t source_time_ms;
    uint64_t publication_generation, map_revision, application_frame;
    uint64_t control_sequence;
    qa_body_state body;
    qa_vec3 view_angles;
    int32_t movement_direction, legs_animation, torso_animation;
    uint32_t source_flags;
    float scale, opacity;
    bool present;
} qa_application_selected_q3_character;

/* found=false means the full actor has no native selected Q3 CHARACTER player
 * and control. No observer invokes a source callback or advances behavior. */
bool qa_application_selected_q3_character_read(qa_application *, qa_actor_id,
    qa_application_selected_q3_character *, bool *found, qa_error *);
bool qa_application_selected_q3_character_current(qa_application *,
    const qa_application_selected_q3_character *);

#endif
