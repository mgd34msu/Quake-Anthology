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
    Q1_MAP_SACRIFICE,
    Q1_MAP_SPIKE_MINE,
    Q1_MAP_HIP_LIGHTNING,
    Q1_MAP_HIP_LIGHTNING_TRIGGERED,
    Q1_MAP_HIP_LIGHTNING_SWITCHED,
    Q1_MAP_TESLA,
    Q1_MAP_GODS_WRATH,
    Q1_MAP_GRAVITY_WELL,
    Q1_MAP_HIP_BOLT,
    Q1_MAP_TESLA_BOLT,
    Q1_MAP_ROTATE_INFO,
    Q1_MAP_ROTATE_PATH,
    Q1_MAP_ROTATE_OBJECT,
    Q1_MAP_ROTATE_ENTITY,
    Q1_MAP_ROTATE_DOOR,
    Q1_MAP_MOVEWALL,
    Q1_MAP_CLOCK,
    Q1_MAP_ROTATE_TRAIN,
    Q1_MAP_DYNAMIC_LIGHT,
    Q1_MAP_LIGHT_RAMP,
    Q1_MAP_CANDLE,
    Q1_MAP_GAS_FLAME,
    Q1_MAP_GAS_SEGMENT,
    Q1_MAP_ROPE,
    Q1_MAP_ROPE_SEGMENT,
    Q1_MAP_SHELTER,
    Q1_MAP_ADDON_BOB,
    Q1_MAP_ADDON_TOSS,
    Q1_MAP_ADDON_SHATTER,
    Q1_MAP_ADDON_DEBRIS,
    Q1_MAP_ADDON_EXPLODE,
    Q1_MAP_ADDON_HURT,
    Q1_MAP_ADDON_FADE,
    Q1_MAP_ADDON_MODEL,
    Q1_MAP_ADDON_ROTATE,
    Q1_MAP_ADDON_AXIS,
    Q1_MAP_ADDON_BREAKABLE,
    Q1_MAP_ADDON_COUNTER,
    Q1_MAP_ADDON_COUNTER_TIMED,
    Q1_MAP_ADDON_REPEATER,
    Q1_MAP_ADDON_MULTITOUCH,
    Q1_MAP_ADDON_EXPLOSION,
    Q1_MAP_ADDON_CHANGE_TARGET,
    Q1_MAP_ADDON_CLEANUP,
    Q1_MAP_ADDON_ALWAYS,
    Q1_MAP_ADDON_RUNE_RELAY,
    Q1_MAP_ADDON_RUNE_COUNTER,
    Q1_MAP_ADDON_BN_RELAY,
    Q1_MAP_ADDON_SACRIFICE_COUNTER,
    Q1_MAP_ADDON_CHECK_SACRIFICES,
    Q1_MAP_ADDON_KILL_MONSTER,
    Q1_MAP_ADDON_HEALTH_RELAY,
    Q1_MAP_RUNE_INDICATOR,
    Q1_MAP_SIGIL_FIXER,
    Q1_MAP_ADDON_DOOR_RELAY,
    Q1_MAP_ADDON_DOOR_GROUP,
    Q1_MAP_ADDON_LORE,
    Q1_MAP_ADDON_MUSIC,
    Q1_MAP_ADDON_HEAL,
    Q1_MAP_ADDON_QUAD,
    Q1_MAP_ADDON_SILENT_TELEPORT,
    Q1_MAP_ADDON_CUTSCENE,
    Q1_MAP_ADDON_SKILL,
    Q1_MAP_ADDON_EXPLOSION_REPEATER,
    Q1_MAP_HORDE_MANAGER,
    Q1_MAP_HORDE_NORMAL,
    Q1_MAP_HORDE_RANGED,
    Q1_MAP_HORDE_FLYING,
    Q1_MAP_HORDE_BOSS,
    Q1_MAP_HORDE_AMMO,
    Q1_MAP_HORDE_ITEM,
    Q1_MAP_HORDE_KEY,
    Q1_MAP_ADDON_SHAKE,
    Q1_MAP_ADDON_SOUND,
    Q1_MAP_ADDON_LIGHTNING,
    Q1_MAP_ADDON_FADE_TRIGGER,
    Q1_MAP_ADDON_FADE_MANAGER,
    Q1_MAP_ADDON_FREEZE,
    Q1_MAP_ADDON_EMBERS,
    Q1_MAP_ADDON_EMBERS_TALL,
    Q1_MAP_ADDON_PARTICLE_TELE,
    Q1_MAP_ADDON_FOUNTAIN,
    Q1_MAP_ELECTRODE_TARGET,
    Q1_MAP_EGG_OPENER,
    Q1_MAP_FOG_INFO,
    Q1_MAP_FOG_TRIGGER,
    Q1_MAP_FOG_TRANSITION,
    Q1_MAP_ROGUE_RUBBLE_SOURCE,
    Q1_MAP_ROGUE_RUBBLE,
    Q1_MAP_ROGUE_EXPLOSION_TRIGGER,
    Q1_MAP_ROGUE_LAMP,
    Q1_MAP_CTF_VOTE_EXIT,
    Q1_MAP_CTF_CHANGELEVEL
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
    Q1_MAP_LTRAIL_CHAIN,
    Q1_MAP_MINE_FIRST,
    Q1_MAP_MINE_HOME,
    Q1_MAP_HIP_LIGHTNING_FIRST,
    Q1_MAP_HIP_LIGHTNING_TICK,
    Q1_MAP_HIP_BOLT_TICK,
    Q1_MAP_TESLA_TICK,
    Q1_MAP_TESLA_BOLT_TICK,
    Q1_MAP_GRAVITY_PULL,
    Q1_MAP_ROTATE_FIRST,
    Q1_MAP_ROTATE_TICK,
    Q1_MAP_ROTATE_DOOR_TICK,
    Q1_MAP_ROTATE_DOOR_DONE,
    Q1_MAP_MOVEWALL_TICK,
    Q1_MAP_CLOCK_FIRST,
    Q1_MAP_CLOCK_TICK,
    Q1_MAP_ROTATE_TRAIN_TICK,
    Q1_MAP_LIGHT_RAMP_INIT,
    Q1_MAP_ADDON_BOB_STEP,
    Q1_MAP_ADDON_TOSS_STOP,
    Q1_MAP_ADDON_TOSS_START,
    Q1_MAP_ADDON_TOSS_CASCADE,
    Q1_MAP_ADDON_DEBRIS_WAKE,
    Q1_MAP_ADDON_DEBRIS_FADE,
    Q1_MAP_ADDON_EXPLODE_FIRE,
    Q1_MAP_ADDON_MODEL_LOOP,
    Q1_MAP_ADDON_MODEL_ONCE,
    Q1_MAP_ADDON_BREAKABLE_STOP,
    Q1_MAP_ADDON_COUNTER_RESET,
    Q1_MAP_ADDON_REPEAT_TICK,
    Q1_MAP_ADDON_MULTITOUCH_EMPTY,
    Q1_MAP_ADDON_TARGETS,
    Q1_MAP_ADDON_EXPLOSION_FIRE,
    Q1_MAP_CAMPAIGN_USE_TARGETS,
    Q1_MAP_SIGIL_FIX,
    Q1_MAP_ADDON_EXPLOSION_REPEAT,
    Q1_MAP_ADDON_SHAKE_TICK,
    Q1_MAP_ADDON_FADE_TICK,
    Q1_MAP_ADDON_PARTICLE_TICK,
    Q1_MAP_ROGUE_RUBBLE_THROW,
    Q1_MAP_FOREIGN_REMOVE,
    Q1_MAP_CTF_NEXTLEVEL
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
typedef struct q1_rotate_target {
    qa_actor_id actor, owner;
    qa_vec3 original, current;
    uint8_t type;
} q1_rotate_target;
typedef struct q1_map_rotation {
    qa_vec3 origin, rate, dest1, dest2, destination;
    qa_vec3 final_angle, final_destination;
    qa_actor_id goal;
    double last_time, end_time;
    double progress_start, inverse_duration;
    uint8_t phase;
    uint8_t next;
    bool linked;
} q1_map_rotation;
typedef struct q1_addon_contact {
    qa_actor_id actor;
    qa_actor_id fog_active;
    qa_actor_id secret_marker, exit_marker;
    qa_vec3 fog_color;
    float fog_density;
    double fly_sound, lore_active, voted;
    float hunger_time, super_time;
    bool sheltered, has_hunger;
    bool secret_hunter, exit_hunter, monster_hunter, buddha;
    uint32_t effects;
} q1_addon_contact;
struct q1_map_state {
    struct q1_map_state *allocated_next, *pool_next;
    q1_map_kind kind;
    q1_map_action action;
    qa_string_id original_model, map, noise[4], endtext, intermissiontext, netname, event;
    qa_string_id spawn_function, spawn_classname;
    qa_string_id group, path, category, fog_info_entity;
    qa_vec3 movedir, mangle, view_offset, rotate;
    qa_vec3 dest, dest2, pos2;
    qa_vec3 particle_size;
    qa_vec3 fog_color;
    float fog_density;
    float height, lip, width, length, pause_time, volume, duration, distance, initial_think;
    float spawn_multi, spawn_silent, gravity, current_ammo, weapon, frags;
    int32_t sounds, style, color_map, impulse;
    uint32_t inline_model;
    float counter_value, field_state, goal_state;
    int32_t particle_color;
    double cooldown, active_until;
    bool has_inline_model, has_movedir, has_view_offset, touch_enabled, use_enabled, dormant,
        effect_active, has_dest2, electrode_button;
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
        struct {
            qa_actor_id enemy, last_victim;
            qa_vec3 endpoint;
            double search_until, pulse_until, cycle_until, sound_after, switch_due;
            uint8_t attack;
            bool enabled, pulsing, killed;
        } hazard;
        q1_map_rotation rotation;
        struct {
            qa_actor_id chain;
            qa_vec3 origin;
            uint8_t phase;
        } addon;
        struct {
            qa_vec3 origin;
            uint8_t phase;
        } brush;
        uint32_t horde_start_flags;
    } pending;
};
struct q1_map_runtime {
    qa_q1_map_options options;
    q1_map_state *allocated, *spare, *retired;
    q1_door_group *door_groups;
    q1_rotate_target *rotated_targets;
    qa_actor_id *frame_ticks;
    q1_addon_contact *addon_contacts;
    uint32_t frame_tick_count;
    qa_actor_id world_actor, electrodes[2];
    qa_actor_id time_machine, ending_actor;
    double lightning_end;
    float pendulum_impact, elevator_direction;
    qa_q1_map_finale_view finale;
    bool finale_started, finale_dismissed;
    double earthquake_end;
    bool quake_active;
    bool dump_coordinates;
    qa_actor_id ctf_vote_leader;
    double ctf_vote_exit_time;
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
bool q1_map_ctf_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_ctf_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_ctf_frame(qa_q1_game *, qa_error *);
bool q1_map_ctf_nextlevel_think(qa_q1_game *, q1_actor *, qa_error *);
static inline bool q1_map_is_ctf(q1_map_kind kind) {
    return kind == Q1_MAP_CTF_VOTE_EXIT || kind == Q1_MAP_CTF_CHANGELEVEL;
}
static inline bool q1_map_is_rogue_misc(q1_map_kind kind) {
    return kind >= Q1_MAP_ROGUE_RUBBLE_SOURCE && kind <= Q1_MAP_ROGUE_LAMP;
}
bool q1_map_rogue_misc_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_rogue_misc_use(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_rogue_misc_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_rogue_misc_reaction(qa_q1_game *, q1_actor *, const qa_damage_outcome *, qa_error *);
bool q1_map_rogue_rubble_throw(qa_q1_game *, q1_actor *, qa_error *);
static inline bool q1_map_is_addon_field(const qa_q1_game *g, q1_map_kind kind) {
    return (g->options.program == QA_Q1_DOPA || g->options.program == QA_Q1_MG1 ||
            g->options.program == QA_Q1_MG3) &&
           (kind == Q1_MAP_HURT || kind == Q1_MAP_PUSH || kind == Q1_MAP_SHELTER);
}
bool q1_map_addon_field_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_field_use(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_field_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_addon_hunger(qa_q1_game *, qa_actor_id, float, qa_error *);
q1_addon_contact *q1_map_addon_contact(qa_q1_game *, qa_actor_id, bool create, qa_error *);
bool q1_map_mg3_impulse(qa_q1_game *, qa_actor_id, uint8_t, bool *handled, qa_error *);
bool q1_map_mg3_buddha(const qa_q1_game *, qa_actor_id);
static inline bool q1_map_is_fog(q1_map_kind kind) {
    return kind >= Q1_MAP_FOG_INFO && kind <= Q1_MAP_FOG_TRANSITION;
}
bool q1_map_addon_fog_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_fog_activate(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
static inline bool q1_map_is_addon_campaign(q1_map_kind kind) {
    return kind == Q1_MAP_RUNE_INDICATOR || kind == Q1_MAP_SIGIL_FIXER ||
           kind == Q1_MAP_ELECTRODE_TARGET || kind == Q1_MAP_EGG_OPENER;
}
static inline bool q1_map_campaign_action_matches(q1_map_kind kind, q1_map_action action) {
    return action == Q1_MAP_CAMPAIGN_USE_TARGETS
               ? kind == Q1_MAP_GATE || kind == Q1_MAP_RUNE_INDICATOR
               : action == Q1_MAP_SIGIL_FIX && kind == Q1_MAP_SIGIL_FIXER;
}
bool q1_map_addon_campaign_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_campaign_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_addon_campaign_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
bool q1_map_addon_electrode_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_addon_egg_mover(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_sigil_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_sigil_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_addon_bossgate_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_changelevel_begin(qa_q1_game *, q1_actor *, qa_error *);
static inline bool q1_map_is_addon_trigger(q1_map_kind kind) {
    return kind >= Q1_MAP_ADDON_COUNTER && kind <= Q1_MAP_ADDON_HEALTH_RELAY;
}
static inline bool q1_map_addon_trigger_action_matches(q1_map_kind kind, q1_map_action action) {
    switch (action) {
    case Q1_MAP_ADDON_COUNTER_RESET:
        return kind == Q1_MAP_ADDON_COUNTER_TIMED;
    case Q1_MAP_ADDON_REPEAT_TICK:
        return kind == Q1_MAP_ADDON_REPEATER;
    case Q1_MAP_ADDON_MULTITOUCH_EMPTY:
        return kind == Q1_MAP_ADDON_MULTITOUCH || kind == Q1_MAP_ADDON_CHECK_SACRIFICES;
    case Q1_MAP_ADDON_TARGETS:
        return kind == Q1_MAP_ADDON_ALWAYS || kind == Q1_MAP_ADDON_RUNE_RELAY;
    case Q1_MAP_ADDON_EXPLOSION_FIRE:
        return kind == Q1_MAP_ADDON_EXPLOSION;
    default:
        return false;
    }
}
bool q1_map_addon_trigger_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_trigger_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_addon_trigger_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_addon_trigger_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
static inline bool q1_map_is_addon_control(q1_map_kind kind) {
    return kind >= Q1_MAP_ADDON_DOOR_RELAY && kind <= Q1_MAP_ADDON_EXPLOSION_REPEATER;
}
bool q1_map_addon_control_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_control_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_addon_control_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_addon_control_think(qa_q1_game *, q1_actor *, qa_error *);
static inline bool q1_map_is_addon_effect(q1_map_kind kind) {
    return kind >= Q1_MAP_ADDON_SHAKE && kind <= Q1_MAP_ADDON_FOUNTAIN;
}
static inline bool q1_map_addon_effect_action_matches(q1_map_kind kind, q1_map_action action) {
    return action == Q1_MAP_ADDON_SHAKE_TICK ? kind == Q1_MAP_ADDON_SHAKE
         : action == Q1_MAP_ADDON_FADE_TICK ? kind == Q1_MAP_ADDON_FADE_MANAGER
         : action == Q1_MAP_ADDON_PARTICLE_TICK &&
           kind >= Q1_MAP_ADDON_EMBERS && kind <= Q1_MAP_ADDON_FOUNTAIN;
}
bool q1_map_addon_effect_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_effect_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_addon_effect_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
bool q1_map_addon_lore_admit(qa_q1_game *, qa_actor_id, bool *, qa_error *);
bool q1_map_addon_quad_mark(qa_q1_game *, qa_actor_id, qa_error *);
bool q1_map_addon_relay_mover(qa_q1_game *, q1_actor *, bool, qa_actor_id, qa_error *);
static inline bool q1_map_is_horde(q1_map_kind kind) {
    return kind >= Q1_MAP_HORDE_MANAGER && kind <= Q1_MAP_HORDE_KEY;
}
bool q1_map_horde_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_horde_use(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_horde_present(qa_q1_game *);
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
static inline bool q1_map_is_hip_hazard(q1_map_kind kind) {
    return kind >= Q1_MAP_SPIKE_MINE && kind <= Q1_MAP_TESLA_BOLT;
}
bool q1_map_hip_hazard_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_hip_hazard_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_hip_hazard_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_hip_hazard_reaction(qa_q1_game *, q1_actor *, const qa_damage_outcome *, qa_error *);
bool q1_map_hip_hazard_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
static inline bool q1_map_is_rotation(q1_map_kind kind) {
    return kind >= Q1_MAP_ROTATE_INFO && kind <= Q1_MAP_ROTATE_TRAIN;
}
bool q1_map_rotation_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_rotation_use(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_rotation_touch(qa_q1_game *, q1_actor *, qa_actor_id, bool blocked, qa_error *);
bool q1_map_rotation_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
void q1_map_rotation_released(qa_q1_game *, qa_actor_id);
static inline bool q1_map_is_addon_visual(q1_map_kind kind) {
    return kind >= Q1_MAP_DYNAMIC_LIGHT && kind <= Q1_MAP_ROPE_SEGMENT;
}
bool q1_map_addon_visual_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_light_spawn(qa_q1_game *, q1_actor *, bool *handled, qa_error *);
bool q1_map_addon_visual_use(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_visual_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_frame(qa_q1_game *, qa_error *);
bool q1_map_level_frame(qa_q1_game *, const qa_source_frame *, qa_error *);
bool q1_map_frame_tick_add(qa_q1_game *, qa_actor_id, qa_error *);
void q1_map_frame_tick_remove(qa_q1_game *, qa_actor_id);
static inline bool q1_map_is_addon_brush(q1_map_kind kind) {
    return kind >= Q1_MAP_ADDON_BOB && kind <= Q1_MAP_ADDON_BREAKABLE;
}
static inline bool q1_map_addon_action_matches(q1_map_kind kind, q1_map_action action) {
    switch (action) {
    case Q1_MAP_ADDON_BOB_STEP: return kind == Q1_MAP_ADDON_BOB;
    case Q1_MAP_ADDON_TOSS_STOP: case Q1_MAP_ADDON_TOSS_START: case Q1_MAP_ADDON_TOSS_CASCADE:
        return kind == Q1_MAP_ADDON_TOSS;
    case Q1_MAP_ADDON_DEBRIS_WAKE: case Q1_MAP_ADDON_DEBRIS_FADE:
        return kind == Q1_MAP_ADDON_DEBRIS;
    case Q1_MAP_ADDON_EXPLODE_FIRE: return kind == Q1_MAP_ADDON_EXPLODE;
    case Q1_MAP_ADDON_MODEL_LOOP: case Q1_MAP_ADDON_MODEL_ONCE:
        return kind == Q1_MAP_ADDON_MODEL;
    case Q1_MAP_ADDON_BREAKABLE_STOP: return kind == Q1_MAP_ADDON_BREAKABLE;
    default: return false;
    }
}
bool q1_map_addon_brush_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_brush_use(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_addon_brush_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_addon_brush_blocked(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_addon_brush_reaction(qa_q1_game *, q1_actor *, const qa_damage_outcome *, qa_error *);
bool q1_map_addon_brush_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
bool q1_map_addon_brush_frame(qa_q1_game *, q1_actor *, qa_error *);

#endif
