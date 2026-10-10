#ifndef QA_UNIFIED_FRAME_PREDICTION_H
#define QA_UNIFIED_FRAME_PREDICTION_H

#include "qa/network_unified_frame.h"
#include "qa/movement.h"

typedef struct qa_unified_movement_numeric {
    qa_string_id id;
    uint32_t radix, scalar_mantissa_bits, double_mantissa_bits;
    int32_t evaluation_method, rounding;
    bool native_c, qw_origin_binary64;
} qa_unified_movement_numeric;

typedef struct qa_unified_weapon_state {
    qa_game_family kind;
    double frame, attack_finished_seconds, source_weapon;
    int32_t gun_frame, state, machinegun_shots, time_ms;
    qa_string_id pending_weapon;
    bool grenade_milliseconds, grenade_blew_up;
    double grenade_seconds;
    int64_t grenade_time_ms;
} qa_unified_weapon_state;
typedef struct qa_unified_animation_state {
    qa_game_family family;
    double frame, next_frame_seconds;
    int32_t end_frame, priority, legs, torso, legs_timer_ms, torso_timer_ms;
    bool duck, run;
} qa_unified_animation_state;

struct qa_unified_frame_prediction {
    qa_actor_id actor;
    int64_t sequence;
    double command_time_ms;
    qa_string_id profile_id;
    qa_clock_config clock;
    qa_unified_movement_numeric numeric;
    qa_movement_profile profile;
    qa_movement_state state;
    char *arsenal_provider, *character_provider;
    qa_string_id active_weapon;
    qa_unified_weapon_state weapon;
    qa_unified_inventory_entry *ammo;
    size_t ammo_count;
    qa_unified_animation_state animation;
    qa_movement_posture standing, crouched, dead;
    qa_bounds invulnerability_bounds, bounds;
    qa_vec3 view_angles, view_offset;
    float view_height;
    qa_movement_environment environment;
    bool has_client_view_offset;
    qa_vec3 client_view_offset;
    qa_movement_ground ground;
    int32_t water_level, water_type;
    bool has_rerelease_origin;
    qa_vec3 rerelease_origin;
};

#endif
