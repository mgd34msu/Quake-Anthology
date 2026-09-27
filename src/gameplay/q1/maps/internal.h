#ifndef QA_Q1_MAP_INTERNAL_H
#define QA_Q1_MAP_INTERNAL_H

#include "../internal.h"
#include "qa/game_q1_maps.h"

typedef enum q1_map_kind {
    Q1_MAP_FIELDS,
    Q1_MAP_WORLD,
    Q1_MAP_POINT,
    Q1_MAP_PATH,
    Q1_MAP_FOLLOW,
    Q1_MAP_CANCEL_PAUSE,
    Q1_MAP_SWITCH_PATH,
    Q1_MAP_WALL,
    Q1_MAP_MULTI,
    Q1_MAP_COUNTER,
    Q1_MAP_RELAY,
    Q1_MAP_TELEPORT,
    Q1_MAP_DESTINATION,
    Q1_MAP_HURT,
    Q1_MAP_PUSH,
    Q1_MAP_CHANGELEVEL,
    Q1_MAP_SETSKILL,
    Q1_MAP_REGISTERED,
    Q1_MAP_MONSTERJUMP,
    Q1_MAP_LIGHT,
    Q1_MAP_BARREL,
    Q1_MAP_DELAY,
    Q1_MAP_BOBBING_WATER,
    Q1_MAP_PUSHABLE,
    Q1_MAP_PUSHABLE_PROXY,
    Q1_MAP_SPAWNER,
    Q1_MAP_HIP_COUNTER,
    Q1_MAP_ONCOUNT,
    Q1_MAP_USE_KEY,
    Q1_MAP_REMOVE_TRIGGER,
    Q1_MAP_GRAVITY_TRIGGER,
    Q1_MAP_DECOY_TRIGGER,
    Q1_MAP_WATERFALL,
    Q1_MAP_THRESHOLD,
    Q1_MAP_BREAKAWAY,
    Q1_MAP_PENDULUM,
    Q1_MAP_ROGUE_PLAT,
    Q1_MAP_ELEVATOR_BUTTON,
    Q1_MAP_ROGUE_PLAT_TRIGGER,
    Q1_MAP_TIME_MACHINE,
    Q1_MAP_TIME_CORE,
    Q1_MAP_TIME_BOOM,
    Q1_MAP_TIME_STOP,
    Q1_MAP_ENDING_ACTOR,
    Q1_MAP_CAMERA_TRACKER,
    Q1_MAP_ROGUE_QUAKE,
    Q1_MAP_ROGUE_QUAKE_FIELD,
    Q1_MAP_ROGUE_QUAKE_KILL,
    Q1_MAP_BUZZSAW,
    Q1_MAP_LTRAIL_START,
    Q1_MAP_LTRAIL_RELAY,
    Q1_MAP_LTRAIL_END,
    Q1_MAP_DOOR,
    Q1_MAP_BUTTON,
    Q1_MAP_SECRET_DOOR,
    Q1_MAP_PLAT,
    Q1_MAP_TRAIN,
    Q1_MAP_TRAIN2,
    Q1_MAP_DOOR_TRIGGER,
    Q1_MAP_PLAT_TRIGGER,
    Q1_MAP_GATE,
    Q1_MAP_STATIC,
    Q1_MAP_AMBIENT,
    Q1_MAP_SIGIL,
    Q1_MAP_SHOOTER,
    Q1_MAP_FIREBALL_SOURCE,
    Q1_MAP_FIREBALL,
    Q1_MAP_BUBBLES,
    Q1_MAP_NOISE,
    Q1_MAP_VIEW,
    Q1_MAP_LIGHTNING,
    Q1_MAP_SOUND,
    Q1_MAP_HIP_AMBIENT,
    Q1_MAP_COMMAND,
    Q1_MAP_TELEPORT_EFFECT,
    Q1_MAP_EXPLODER,
    Q1_MAP_RUBBLE_SOURCE,
    Q1_MAP_RUBBLE,
    Q1_MAP_EARTHQUAKE,
    Q1_MAP_PARTICLE_FIELD,
    Q1_MAP_TOGGLE_WALL,
    Q1_MAP_WALL_SPRITE,
    Q1_MAP_SACRIFICE
} q1_map_kind;
typedef enum q1_map_action {
    Q1_MAP_IDLE,
    Q1_MAP_REARM,
    Q1_MAP_REMOVE,
    Q1_MAP_DELAYED_USE,
    Q1_MAP_BEGIN_LEVEL,
    Q1_MAP_PENDING_LEVEL,
    Q1_MAP_FINALE_TIMER,
    Q1_MAP_BARREL_EXPLODE,
    Q1_MAP_MOVE_DONE,
    Q1_MAP_DOOR_TOP,
    Q1_MAP_DOOR_BOTTOM,
    Q1_MAP_DOOR_DOWN,
    Q1_MAP_BUTTON_TOP,
    Q1_MAP_BUTTON_BOTTOM,
    Q1_MAP_BUTTON_RETURN,
    Q1_MAP_SECRET_FIRST,
    Q1_MAP_SECRET_SECOND,
    Q1_MAP_SECRET_TOP,
    Q1_MAP_SECRET_RETURN,
    Q1_MAP_SECRET_LAST_WAIT,
    Q1_MAP_SECRET_LAST,
    Q1_MAP_SECRET_BOTTOM,
    Q1_MAP_PLAT_TOP,
    Q1_MAP_PLAT_BOTTOM,
    Q1_MAP_PLAT_DOWN,
    Q1_MAP_TRAIN_FIND,
    Q1_MAP_TRAIN_NEXT,
    Q1_MAP_TRAIN_WAIT,
    Q1_MAP_SIGIL_PLACE,
    Q1_MAP_SHOOTER_FIRE,
    Q1_MAP_FIREBALL_FLY,
    Q1_MAP_BUBBLES_MAKE,
    Q1_MAP_NOISE_REPEAT,
    Q1_MAP_LIGHTNING_FIRE,
    Q1_MAP_FINALE_TWO,
    Q1_MAP_FINALE_THREE,
    Q1_MAP_FINALE_WAIT,
    Q1_MAP_FINALE_SIX,
    Q1_MAP_SOUND_REPEAT,
    Q1_MAP_EXPLODER_FIRE,
    Q1_MAP_SACRIFICE_ANIMATE,
    Q1_MAP_SACRIFICE_FLOAT,
    Q1_MAP_BOB_WATER,
    Q1_MAP_COUNTER_START,
    Q1_MAP_COUNTER_TICK,
    Q1_MAP_PENDULUM_SWING,
    Q1_MAP_ROGUE_PLAT_UP,
    Q1_MAP_ROGUE_PLAT_DOWN,
    Q1_MAP_ROGUE_PLAT_TOP,
    Q1_MAP_ROGUE_PLAT_BOTTOM,
    Q1_MAP_ELEVATOR_STOP,
    Q1_MAP_ELEVATOR_BUTTON_WAIT,
    Q1_MAP_ELEVATOR_BUTTON_RETURN,
    Q1_MAP_ELEVATOR_BUTTON_DONE,
    Q1_MAP_TIME_BOOM_THINK,
    Q1_MAP_TIME_STOP_SHAKE,
    Q1_MAP_TIME_FALL,
    Q1_MAP_TIME_CRASH_THINK,
    Q1_MAP_ENDING_CONTROL,
    Q1_MAP_ENDING_RUN,
    Q1_MAP_ENDING_FIRE,
    Q1_MAP_ENDING_TELEPORT,
    Q1_MAP_CAMERA_TRACK,
    Q1_MAP_ROGUE_QUAKE_START,
    Q1_MAP_ROGUE_QUAKE_STOP,
    Q1_MAP_ROGUE_QUAKE_RUMBLE,
    Q1_MAP_SAW_START,
    Q1_MAP_SAW_FLY,
    Q1_MAP_SAW_STAND,
    Q1_MAP_LTRAIL_FIRE,
    Q1_MAP_LTRAIL_CHAIN
} q1_map_action;
typedef enum q1_map_position { Q1_MAP_BOTTOM, Q1_MAP_UP, Q1_MAP_TOP, Q1_MAP_DOWN } q1_map_position;
typedef enum q1_time_reaction { Q1_TIME_NO_REACTION, Q1_TIME_PAIN, Q1_TIME_CRASH } q1_time_reaction;
typedef struct q1_door_group {
    struct q1_door_group *next;
    qa_actor_id *members;
    size_t count;
} q1_door_group;
typedef struct q1_map_rogue_platform {
    double last_use, last_move, go_time;
    float floor, target_floor;
    uint8_t go_to;
    bool called, disabled;
} q1_map_rogue_platform;
typedef struct q1_map_movement {
    qa_vec3 pos1, pos2, dest1, dest2, destination;
    q1_door_group *group;
    q1_map_action done;
    q1_map_position position;
    qa_actor_id goal;
    float next_speed;
    bool moving, activated;
    q1_map_rogue_platform rogue;
} q1_map_movement;
struct q1_map_state {
    struct q1_map_state *allocated_next, *pool_next;
    q1_map_kind kind;
    q1_map_action action;
    qa_string_id original_model, map, noise[4], endtext, intermissiontext, netname, event;
    qa_string_id spawn_function, spawn_classname;
    qa_vec3 movedir, mangle, view_offset;
    float height, lip, width, length, pause_time, volume, duration, distance, initial_think;
    float spawn_multi, spawn_silent, gravity, current_ammo, weapon, frags;
    int32_t sounds, style, color_map, impulse;
    uint32_t inline_model;
    float counter_value;
    int32_t particle_color;
    double cooldown, active_until;
    bool has_inline_model, has_movedir, has_view_offset, touch_enabled, use_enabled, dormant,
        effect_active;
    struct {
        qa_string_id model;
        qa_bounds bounds;
        qa_physics_solid solid;
        q1_think_kind think;
        bool valid;
    } spawn_template;
    union {
        qa_target_use delayed;
        qa_q1_campaign_timer finale;
        q1_map_movement mover;
        qa_vec3 push_origin;
        qa_actor_id spawn_master;
        uint8_t pendulum_step;
        q1_time_reaction time_reaction;
        struct {
            qa_actor_id move_target;
            qa_vec3 view_angles;
            float rockets;
            uint8_t fire_stage;
        } follower;
        struct {
            float ticks;
            bool running, stop_after_cycle;
        } counter;
        struct {
            float amplitude;
            double last_time;
        } bob;
        struct {
            qa_vec3 start, end;
            unsigned plane;
        } particles;
    } pending;
};
struct q1_map_runtime {
    qa_q1_map_options options;
    q1_map_state *allocated, *spare, *retired;
    q1_door_group *door_groups;
    qa_actor_id world_actor, electrodes[2];
    qa_actor_id time_machine, ending_actor;
    double lightning_end;
    float pendulum_impact, elevator_direction;
    qa_q1_map_finale_view finale;
    bool finale_started, finale_dismissed;
    double earthquake_end;
    bool quake_active;
    bool final_new_game_travel;
    bool rogue_cutscene, rogue_ending_started;
    bool rogue_quake_active;
    float rogue_quake_intensity;
    uint8_t rogue_actor_stage;
    uint32_t total_secrets, found_secrets;
};

bool q1_map_fail(qa_error *, const char *);
static inline bool q1_map_text(qa_q1_game *g, qa_string_id text) {
    return qa_strings_text(qa_session_strings(g->services.session), text).size != 0;
}
q1_map_state *q1_map_allocate(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_schedule(qa_q1_game *, q1_actor *, double delay, q1_map_action, qa_error *);
bool q1_map_damageable(qa_q1_game *, q1_actor *, bool, qa_error *);
bool q1_map_targets(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_player(qa_q1_game *, qa_actor_id);
bool q1_map_grounded(qa_q1_game *, q1_actor *, qa_actor_id);
bool q1_map_ambient(qa_q1_game *, qa_vec3, const char *, float, qa_error *);
bool q1_map_make_static(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_lightstyle(qa_q1_game *, q1_actor *, const char *, qa_error *);
bool q1_map_trigger_init(qa_q1_game *, q1_actor *, bool zero_direction, qa_error *);
qa_vec3 q1_map_direction(qa_vec3 angles);
bool q1_map_trigger_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_path_use(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_path_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_hip_path_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_follow_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_trigger_touch(qa_q1_game *, q1_actor *, const qa_touch_contact *, qa_error *);
bool q1_map_trigger_use(qa_q1_game *, q1_actor *, qa_actor_id other, qa_actor_id activator,
                        qa_error *);
bool q1_map_multi_fire(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_is_mover(q1_map_kind);
bool q1_map_mover_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_mover_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_mover_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_mover_blocked(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_mover_reaction(qa_q1_game *, q1_actor *, const qa_damage_outcome *, qa_error *);
bool q1_map_mover_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
bool q1_map_move(qa_q1_game *, q1_actor *, qa_vec3, q1_map_action, qa_error *);
bool q1_map_plat_trigger(qa_q1_game *, q1_actor *, q1_map_kind, qa_bounds, float height,
                         qa_error *);
static inline bool q1_map_is_rogue_plat(q1_map_kind kind) {
    return kind >= Q1_MAP_ROGUE_PLAT && kind <= Q1_MAP_ROGUE_PLAT_TRIGGER;
}
bool q1_map_rogue_plat_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_rogue_plat_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_actor_id, qa_error *);
bool q1_map_rogue_plat_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_rogue_plat_blocked(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_rogue_plat_reaction(qa_q1_game *, q1_actor *, const qa_damage_outcome *, qa_error *);
bool q1_map_rogue_plat_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
static inline bool q1_map_is_time_actor(q1_map_kind kind) {
    return kind >= Q1_MAP_TIME_MACHINE && kind <= Q1_MAP_TIME_STOP;
}
bool q1_map_time_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_time_reaction(qa_q1_game *, q1_actor *, const qa_damage_outcome *, qa_error *);
bool q1_map_time_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
bool q1_map_rogue_ending(qa_q1_game *, qa_actor_id player, qa_error *);
bool q1_map_ending_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
static inline bool q1_map_is_rogue_hazard(q1_map_kind kind) {
    return kind >= Q1_MAP_ROGUE_QUAKE && kind <= Q1_MAP_LTRAIL_END;
}
bool q1_map_rogue_hazard_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_rogue_hazard_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_actor_id, qa_error *);
bool q1_map_rogue_hazard_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_rogue_hazard_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
bool q1_map_rogue_shake(qa_q1_game *, qa_actor_id, float intensity, qa_error *);
bool q1_map_finale_emit(qa_q1_game *, uint32_t stage, const char *text, qa_error *);
bool q1_map_train_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_train_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
bool q1_map_train_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_hip_brush_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_hip_spawner_spawn(qa_q1_game *, q1_actor *, const qa_q1_spawn *, qa_error *);
bool q1_map_hip_spawner_use(qa_q1_game *, q1_actor *, qa_error *);
static inline bool q1_map_is_hip_trigger(q1_map_kind kind) {
    return kind >= Q1_MAP_HIP_COUNTER && kind <= Q1_MAP_BREAKAWAY;
}
bool q1_map_hip_trigger_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_hip_trigger_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_actor_id, qa_error *);
bool q1_map_hip_trigger_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_hip_trigger_reaction(qa_q1_game *, q1_actor *, const qa_damage_outcome *, qa_error *);
bool q1_map_hip_trigger_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
bool q1_map_pendulum_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_pendulum_use(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_pendulum_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_pendulum_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_pushable_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_bob_water(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_special_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_special_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_special_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_special_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
void q1_map_cancel(qa_q1_game *, q1_actor *);
bool q1_map_timer(qa_q1_game *, const char *, q1_actor **, qa_error *);
bool q1_map_door_down(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_lightning_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_boss_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
bool q1_map_hip_misc_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_hip_misc_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_hip_misc_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_hip_misc_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
bool q1_map_hip_particles_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_hip_particles_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_hip_particles_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_sacrifice_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_sacrifice_gib(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_sacrifice_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);

#endif
