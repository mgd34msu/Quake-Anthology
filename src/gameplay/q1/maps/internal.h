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
    Q1_MAP_DELAY
} q1_map_kind;
typedef enum q1_map_action {
    Q1_MAP_IDLE,
    Q1_MAP_REARM,
    Q1_MAP_REMOVE,
    Q1_MAP_DELAYED_USE,
    Q1_MAP_BEGIN_LEVEL,
    Q1_MAP_PENDING_LEVEL,
    Q1_MAP_FINALE_TIMER,
    Q1_MAP_BARREL_EXPLODE
} q1_map_action;
struct q1_map_state {
    struct q1_map_state *allocated_next, *pool_next;
    q1_map_kind kind;
    q1_map_action action;
    qa_string_id original_model, map, noise[4], endtext, intermissiontext;
    qa_vec3 movedir, mangle;
    float height, lip, width, length, pause_time, volume, duration, distance;
    int32_t sounds, style, color_map;
    uint32_t inline_model;
    double cooldown;
    bool has_inline_model, has_movedir, touch_enabled, use_enabled, dormant;
    union {
        qa_target_use delayed;
        qa_q1_campaign_timer finale;
    } pending;
};
struct q1_map_runtime {
    qa_q1_map_options options;
    q1_map_state *allocated, *spare, *retired;
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
bool q1_map_trigger_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_trigger_touch(qa_q1_game *, q1_actor *, const qa_touch_contact *, qa_error *);
bool q1_map_trigger_use(qa_q1_game *, q1_actor *, qa_actor_id other, qa_actor_id activator,
                        qa_error *);
bool q1_map_multi_fire(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);

#endif
