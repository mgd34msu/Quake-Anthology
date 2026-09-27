#ifndef QA_Q1_MAP_INTERNAL_H
#define QA_Q1_MAP_INTERNAL_H

#include "../internal.h"
#include "qa/game_q1_maps.h"

typedef enum q1_map_kind {
    Q1_MAP_FIELDS,
    Q1_MAP_WORLD,
    Q1_MAP_POINT,
    Q1_MAP_PATH,
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
    Q1_MAP_DOOR,
    Q1_MAP_BUTTON,
    Q1_MAP_SECRET_DOOR,
    Q1_MAP_PLAT,
    Q1_MAP_TRAIN,
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
    Q1_MAP_EARTHQUAKE
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
    Q1_MAP_EXPLODER_FIRE
} q1_map_action;
typedef enum q1_map_position { Q1_MAP_BOTTOM, Q1_MAP_UP, Q1_MAP_TOP, Q1_MAP_DOWN } q1_map_position;
typedef struct q1_door_group {
    struct q1_door_group *next;
    qa_actor_id *members;
    size_t count;
} q1_door_group;
typedef struct q1_map_movement {
    qa_vec3 pos1, pos2, dest1, dest2, destination;
    q1_door_group *group;
    q1_map_action done;
    q1_map_position position;
    bool moving, activated;
} q1_map_movement;
struct q1_map_state {
    struct q1_map_state *allocated_next, *pool_next;
    q1_map_kind kind;
    q1_map_action action;
    qa_string_id original_model, map, noise[4], endtext, intermissiontext;
    qa_vec3 movedir, mangle;
    float height, lip, width, length, pause_time, volume, duration, distance, initial_think;
    int32_t sounds, style, color_map, impulse;
    uint32_t inline_model;
    double cooldown;
    bool has_inline_model, has_movedir, touch_enabled, use_enabled, dormant, effect_active;
    union {
        qa_target_use delayed;
        qa_q1_campaign_timer finale;
        q1_map_movement mover;
    } pending;
};
struct q1_map_runtime {
    qa_q1_map_options options;
    q1_map_state *allocated, *spare, *retired;
    q1_door_group *door_groups;
    qa_actor_id world_actor, electrodes[2];
    double lightning_end;
    qa_q1_map_finale_view finale;
    bool finale_started, finale_dismissed;
    double earthquake_end;
    bool quake_active;
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
bool q1_map_lightstyle(qa_q1_game *, q1_actor *, const char *, qa_error *);
bool q1_map_trigger_init(qa_q1_game *, q1_actor *, bool zero_direction, qa_error *);
qa_vec3 q1_map_direction(qa_vec3 angles);
bool q1_map_trigger_spawn(qa_q1_game *, q1_actor *, qa_error *);
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
bool q1_map_train_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_train_think(qa_q1_game *, q1_actor *, q1_map_action, qa_error *);
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

#endif
