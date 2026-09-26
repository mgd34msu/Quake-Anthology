#ifndef QA_PHYSICS_H
#define QA_PHYSICS_H

#include "qa/world.h"
#include "qa/scheduler.h"

typedef enum qa_physics_motion {
    QA_PHYSICS_STATIONARY, QA_PHYSICS_NOCLIP, QA_PHYSICS_PUSH, QA_PHYSICS_STOP,
    QA_PHYSICS_TOSS, QA_PHYSICS_NEW_TOSS, QA_PHYSICS_BOUNCE,
    QA_PHYSICS_WALL_BOUNCE, QA_PHYSICS_FLY, QA_PHYSICS_FLY_MISSILE,
    QA_PHYSICS_STEP
} qa_physics_motion;
typedef enum qa_physics_solid {
    QA_PHYSICS_NOT_SOLID, QA_PHYSICS_TRIGGER, QA_PHYSICS_BOX,
    QA_PHYSICS_BRUSH, QA_PHYSICS_CORPSE
} qa_physics_solid;
enum qa_physics_flags {
    QA_PHYSICS_FLYING = 1u << 0, QA_PHYSICS_SWIMMING = 1u << 1,
    QA_PHYSICS_PARTIAL_GROUND = 1u << 2, QA_PHYSICS_DEAD = 1u << 3,
    QA_PHYSICS_PLAYER = 1u << 4, QA_PHYSICS_MONSTER = 1u << 5,
    QA_PHYSICS_ALWAYS_TOUCH = 1u << 6, QA_PHYSICS_TEAM_SLAVE = 1u << 7,
    QA_PHYSICS_KILL_VELOCITY = 1u << 8, QA_PHYSICS_ONGROUND = 1u << 9,
    QA_PHYSICS_SUPER_STEP = 1u << 10,
    QA_PHYSICS_KEEP_MOVE_WHILE_TURNING = 1u << 11
};

/* These fields belong to the game/provider. Bodies, collision and source
 * clocks remain owned by the shared world and session. Defaults are explicit;
 * a zero clip_mask is significant for NEW_TOSS. */
typedef struct qa_physics_properties {
    qa_collision_family family;
    qa_physics_motion motion;
    qa_physics_solid solid;
    bool q2_rerelease;
    uint32_t flags, clip_mask;
    qa_vec3 angular_velocity, gravity_direction;
    float gravity_scale, delta_yaw, ideal_yaw, yaw_speed;
    int32_t water_level, water_type;
    qa_actor_id enemy, goal;
    int64_t local_time_ns, next_think_ns;
} qa_physics_properties;

typedef enum qa_physics_event_kind {
    QA_PHYSICS_WATER_ENTER, QA_PHYSICS_WATER_LEAVE, QA_PHYSICS_LAND
} qa_physics_event_kind;
typedef struct qa_physics_event {
    qa_physics_event_kind kind;
    qa_actor_id actor;
    qa_vec3 origin;
} qa_physics_event;
typedef struct qa_physics_services {
    void *context;
    /* False means no physics provider for this live actor. Never retain a
     * returned record or provider pointer across a callback. */
    bool (*read)(void *, qa_actor_id, qa_physics_properties *);
    bool (*write)(void *, qa_actor_id, const qa_physics_properties *, qa_error *);
    bool (*touch)(void *, const qa_touch_contact *, qa_error *);
    bool (*blocked)(void *, qa_actor_id pusher, qa_actor_id obstacle, qa_error *);
    bool (*event)(void *, const qa_physics_event *, qa_error *);
    /* Q1 local-pusher think: next_think_ns is cleared before entry. Other
     * thinks use the session scheduler at the caller's source boundary. */
    bool (*pusher_think)(void *, qa_actor_id, const qa_source_frame *, qa_error *);
    /* Ascending source traversal. NULL orders source slots then owner/host
     * slot. The candidate IDs are captured before any push callbacks. */
    int (*source_order)(void *, qa_actor_id, qa_actor_id);
    uint32_t (*random_integer)(void *);
    /* Native/QC water callbacks can retain their original sound/state policy. */
    bool (*q1_water_transition)(void *, qa_actor_id, qa_error *);
    /* Q2 monster AI owns hazards, bad areas, alternate-fly steering and
     * special ground decisions. NULL accepts an otherwise valid ground move. */
    bool (*accept_ground)(void *, qa_actor_id, qa_vec3 destination);
} qa_physics_services;
typedef struct qa_physics {
    qa_world *world;
    qa_actor_id world_actor;
    qa_physics_services services;
    float gravity, max_velocity, stop_speed;
    /* Session/prediction-owned Q2 rerelease movement scratch, shared with
     * qa_movement_input.q2r_pml_origin and included by that owner's saves.
     * NULL selects the local retained value for entity-only use. */
    qa_vec3 *q2r_pml_origin;
    qa_vec3 q2r_local_origin;
    /* Non-NULL only during a synchronous pusher-team transaction. Saves and
     * destruction require this to be NULL. Owned by the active call. */
    struct qa_physics_transaction *push_transaction;
} qa_physics;
typedef enum qa_physics_status {
    QA_PHYSICS_MOVED, QA_PHYSICS_STOPPED, QA_PHYSICS_REMOVED,
    QA_PHYSICS_UNMANAGED, QA_PHYSICS_SLAVE, QA_PHYSICS_BLOCKED
} qa_physics_status;
typedef struct qa_physics_result {
    qa_physics_status status;
    qa_actor_id obstacle;
    uint32_t collisions;
} qa_physics_result;

qa_physics_properties qa_physics_properties_default(qa_collision_family);
bool qa_physics_init(qa_physics *, qa_world *, qa_actor_id world_actor,
                     const qa_physics_services *, qa_error *);
/* One kinematic step. The caller runs source prethink/think/postthink and
 * team-chain decisions. No clock advances, actor traversal or attachment
 * transport happens here. Callback mutations are committed, not rolled back
 * on error. Every resumed access rechecks the full actor generation. */
bool qa_physics_step(qa_physics *, qa_actor_id, const qa_source_frame *,
                     qa_physics_result *, qa_error *);
bool qa_physics_fly_move(qa_physics *, qa_actor_id, float elapsed_seconds,
                         bool exact_clip_mask, qa_physics_result *, qa_error *);
bool qa_physics_push_entity(qa_physics *, qa_actor_id, qa_vec3 displacement,
                            const qa_actor_id *excluded, size_t excluded_count,
                            qa_trace_result *, qa_error *);
bool qa_physics_touch_triggers(qa_physics *, qa_actor_id, qa_error *);
bool qa_physics_impact(qa_physics *, qa_actor_id, const qa_trace_result *, qa_error *);
bool qa_physics_water_transition(qa_physics *, qa_actor_id, qa_vec3 previous_origin,
                                 qa_error *);
qa_vec3 qa_physics_clip_velocity(qa_vec3, qa_vec3 normal, float overbounce);

typedef struct qa_physics_push {
    qa_actor_id actor;
    qa_vec3 displacement, angular_displacement;
    uint64_t elapsed_ns;
} qa_physics_push;
/* Q1 invokes blocked before restoring moved actors in forward order. Q2
 * restores the whole team in reverse order before blocked; success touches
 * triggers in reverse save order after all parts commit. */
bool qa_physics_push_pusher(qa_physics *, const qa_physics_push *,
                            qa_physics_result *, qa_error *);
bool qa_physics_push_team(qa_physics *, const qa_physics_push *, size_t count,
                          qa_physics_result *, qa_error *);
bool qa_physics_step_q1_pusher(qa_physics *, qa_actor_id,
                              const qa_source_frame *, bool rotate,
                              qa_physics_result *, qa_error *);

/* Monster decisions remain in the game. These implement the source geometric
 * checks and optional commit/relink, including Q2 ceiling gravity and water. */
bool qa_physics_check_bottom(qa_physics *, qa_actor_id, qa_vec3 origin,
                             bool *supported, qa_error *);
bool qa_physics_check_ground(qa_physics *, qa_actor_id, qa_error *);
bool qa_physics_categorize_water(qa_physics *, qa_actor_id, qa_error *);
bool qa_physics_drop_to_floor(qa_physics *, qa_actor_id, float distance,
                              bool *dropped, qa_error *);
bool qa_physics_monster_step(qa_physics *, qa_actor_id, qa_vec3 displacement,
                             float elapsed_seconds, bool commit, bool relink,
                             bool *moved, qa_error *);
bool qa_physics_walk_move(qa_physics *, qa_actor_id, float yaw, float distance,
                          float elapsed_seconds, bool commit, bool relink,
                          bool *moved, qa_error *);
bool qa_physics_change_yaw(qa_physics *, qa_actor_id, float elapsed_seconds,
                           qa_error *);
bool qa_physics_step_direction(qa_physics *, qa_actor_id, float yaw, float distance,
                               float elapsed_seconds, bool *moved, qa_error *);
bool qa_physics_close_enough(qa_physics *, qa_actor_id, qa_actor_id goal,
                             float distance);
bool qa_physics_q1_chase_direction(qa_physics *, qa_actor_id, qa_actor_id goal,
                                   float distance, qa_error *);
bool qa_physics_q1_move_to_goal(qa_physics *, qa_actor_id, qa_actor_id goal,
                               float distance, bool contact, qa_error *);

typedef enum qa_trajectory_type {
    QA_TRAJECTORY_STATIONARY, QA_TRAJECTORY_INTERPOLATE, QA_TRAJECTORY_LINEAR,
    QA_TRAJECTORY_LINEAR_STOP, QA_TRAJECTORY_SINE, QA_TRAJECTORY_GRAVITY
} qa_trajectory_type;
typedef struct qa_trajectory {
    qa_trajectory_type type;
    int32_t time_ms, duration_ms;
    qa_vec3 base, delta;
} qa_trajectory;
/* Native Q3 uses 800 for these shared game/cgame trajectories. The explicit
 * gravity argument also permits selected providers to evaluate other values. */
bool qa_trajectory_position(const qa_trajectory *, int32_t at_ms, float gravity,
                             qa_vec3 *, qa_error *);
bool qa_trajectory_velocity(const qa_trajectory *, int32_t at_ms, float gravity,
                             qa_vec3 *, qa_error *);
int32_t qa_physics_q3_hit_time(int32_t previous_ms, int32_t now_ms, float fraction);
/* Game projectile/hitscan integer truncation. Player pmove velocity uses its
 * separate nearest-even snap at the end of each movement substep. */
qa_vec3 qa_physics_q3_snap(qa_vec3);
qa_vec3 qa_physics_q3_snap_towards(qa_vec3, qa_vec3 target);
/* Moves and links once, retaining a stationary startsolid trace. The Q3 game
 * owns no-impact removal, damage, special impacts, events and post-move think. */
bool qa_physics_q3_missile_move(qa_physics *, qa_actor_id, const qa_trajectory *,
                                int32_t now_ms, qa_actor_id pass,
                                qa_trace_result *, qa_error *);
bool qa_physics_q3_bounce(qa_physics *, qa_actor_id, qa_trajectory *,
                          const qa_trace_result *, int32_t previous_ms,
                          int32_t now_ms, bool half, bool *stopped, qa_error *);

typedef enum qa_q3_mover_actor_kind {
    QA_Q3_MOVER_IGNORE, QA_Q3_MOVER_SHARED, QA_Q3_MOVER_NATIVE_FIXED,
    QA_Q3_MOVER_NATIVE_MOVABLE, QA_Q3_MOVER_NATIVE_PLAYER,
    QA_Q3_MOVER_PROXIMITY_MINE
} qa_q3_mover_actor_kind;
/* Provider-owned source metadata. The world body remains the authority for
 * origin, angles, bounds and ground. Native trajectory bases are distinct:
 * writing them must not implicitly move or link that body. SHARED identifies
 * an eligible foreign player/movable; attached/fixed foreign actors are IGNORE.
 * PROXIMITY_MINE is selected only by the Team Arena adapter. */
typedef struct qa_q3_mover_state {
    qa_q3_mover_actor_kind kind;
    qa_trajectory position, angular;
    int32_t delta_yaw_word, ground_entity_number;
    qa_actor_id team_next, proximity_pusher;
    qa_vec3 proximity_direction;
    bool stop, team_slave;
} qa_q3_mover_state;
typedef enum qa_q3_mover_action {
    QA_Q3_MOVER_CRUSH, QA_Q3_MOVER_PROXIMITY_TRIGGER,
    QA_Q3_MOVER_BLOCKED, QA_Q3_MOVER_REACHED, QA_Q3_MOVER_THINK
} qa_q3_mover_action;
typedef struct qa_q3_mover_services {
    void *context;
    /* False identifies an unmanaged actor. Native writes update source fields
     * only, preserving all metadata outside this record. Each callback may
     * remove actors; the service rechecks complete generation handles. */
    bool (*read)(void *, qa_actor_id, qa_q3_mover_state *);
    bool (*write)(void *, qa_actor_id, const qa_q3_mover_state *, qa_error *);
    /* CRUSH: actor=pusher, other=victim; source applies 99999/MOD_CRUSH.
     * PROXIMITY_TRIGGER: actor=mine, other=pusher; source clears loop sound,
     * emits the trigger event, explodes, then releases its trigger actor.
     * BLOCKED: actor=leader, other=obstacle. REACHED/THINK: other is null.
     * Decisions, damage, events and scheduled think dispatch belong here. */
    bool (*action)(void *, qa_q3_mover_action, qa_actor_id actor,
                    qa_actor_id other, int32_t now_ms, qa_error *);
    /* Optional source spatial policy overrides. Query preserves supplied
     * traversal order and returns at most capacity IDs; the source cap is 1024.
     * NULL uses the existing shared world, with Q3 query/trace policy. */
    bool (*query)(void *, qa_bounds, qa_actor_id *, size_t capacity,
                   size_t *count, qa_error *);
    bool (*trace)(void *, const qa_trace_query *, qa_trace_result *, qa_error *);
} qa_q3_mover_services;
/* Native Q3 mover transactions are separate from Q1/Q2 push_team: no eighth
 * snapping, native yaw words, source original-position fallback and reverse
 * rollback. One call owns the whole live team chain. No trigger touches or
 * attachment transport are added. Errors retain callback-committed effects.
 * run_team performs transport/reached/blocked only; run_mover also honors
 * TEAMSLAVE, skips stationary transport, and calls the source think action. */
bool qa_physics_q3_run_team(qa_physics *, const qa_q3_mover_services *,
                            qa_actor_id leader, int32_t previous_ms,
                            int32_t now_ms, qa_physics_result *, qa_error *);
bool qa_physics_q3_run_mover(qa_physics *, const qa_q3_mover_services *,
                             qa_actor_id leader, int32_t previous_ms,
                             int32_t now_ms, qa_physics_result *, qa_error *);

#endif
