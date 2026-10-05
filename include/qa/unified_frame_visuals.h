#ifndef QA_UNIFIED_FRAME_VISUALS_H
#define QA_UNIFIED_FRAME_VISUALS_H

#include "qa/unified_frame_player.h"

typedef struct qa_unified_model_attachment { char *path, *tag; } qa_unified_model_attachment;
typedef struct qa_unified_weapon_anchor { char *path, *tag; qa_vec3 offset; float fov_above, fov_scale; } qa_unified_weapon_anchor;
typedef struct qa_unified_q2_flare {
    char *image;
    float fade_start, fade_end, scale;
    qa_vec3 color, rim_color;
    bool has_rim_color, lock_angle;
} qa_unified_q2_flare;
typedef struct qa_unified_q3_weapon_view {
    int32_t time_ms, torso_animation, last_fire_ms, bob_cycle, weapon;
    bool has_last_fire_ms, firing;
    float horizontal_speed;
} qa_unified_q3_weapon_view;
typedef struct qa_unified_model_state {
    qa_actor_id actor;
    qa_game_family family;
    char *content, *path, *skin_path, *weapon_item;
    int64_t frame, old_frame, skin;
    uint64_t effects;
    uint32_t render_flags;
    qa_vec3 origin, angles, previous_origin;
    float scale, alpha, back_lerp;
    bool visible, view_weapon, native_held_weapon, has_previous_origin, has_alpha, has_player_colors;
    uint8_t player_colors;
    qa_unified_source_identity *render_source, *render_equipment;
    bool equipment_slot;
    qa_unified_q2_flare *flare;
    qa_unified_q3_weapon_view *q3_weapon;
    qa_unified_weapon_anchor *anchor;
    qa_unified_model_attachment *attachments;
    size_t attachment_count;
} qa_unified_model_state;
typedef struct qa_unified_character_state {
    qa_actor_id actor;
    qa_vec3 origin, angles, velocity;
    int32_t movement_direction, legs, torso, legs_timer_ms, torso_timer_ms, team;
    uint32_t source_flags, powerups;
    float color[4], scale, opacity;
    bool has_team;
} qa_unified_character_state;
typedef struct qa_unified_world_text {
    char *content, *text;
    qa_vec3 origin, angles;
    float color[4], cell_size, distance_cull_factor;
    bool billboard, depth_test;
} qa_unified_world_text;
struct qa_unified_frame_visuals {
    qa_unified_model_state *models;
    size_t model_count;
    qa_unified_character_state *characters;
    size_t character_count;
    qa_unified_world_text *world_text;
    size_t world_text_count;
};

#endif
