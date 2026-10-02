#ifndef QA_BOT_MOVEMENT_H
#define QA_BOT_MOVEMENT_H
#include "qa/bot_actions.h"
#include "qa/bot_goals.h"
#include "qa/bots_allocator.h"

#define QA_BOT_AVOID_SPOTS 32
enum {
    QA_BOT_MOVE_BARRIER_JUMP = 1,
    QA_BOT_MOVE_ON_GROUND = 2,
    QA_BOT_MOVE_SWIMMING = 4,
    QA_BOT_MOVE_AGAINST_LADDER = 8,
    QA_BOT_MOVE_WATER_JUMP = 16,
    QA_BOT_MOVE_TELEPORTED = 32,
    QA_BOT_MOVE_GRAPPLE_PULL = 64,
    QA_BOT_MOVE_ACTIVE_GRAPPLE = 128,
    QA_BOT_MOVE_GRAPPLE_RESET = 256,
    QA_BOT_MOVE_WALK = 512
};
enum {
    QA_BOT_MOVE_VIEW = 1,
    QA_BOT_MOVE_SWIM_VIEW = 2,
    QA_BOT_MOVE_WAITING = 4,
    QA_BOT_MOVE_VIEW_SET = 8,
    QA_BOT_MOVE_WEAPON = 16,
    QA_BOT_MOVE_ON_OBSTACLE = 32,
    QA_BOT_MOVE_ON_BOBBING = 64,
    QA_BOT_MOVE_ON_ELEVATOR = 128,
    QA_BOT_MOVE_AVOID_SPOT = 256
};
enum {
    QA_BOT_DIRECTION_WALK = 1,
    QA_BOT_DIRECTION_CROUCH = 2,
    QA_BOT_DIRECTION_JUMP = 4,
    QA_BOT_DIRECTION_GRAPPLE = 8,
    QA_BOT_DIRECTION_ROCKET_JUMP = 16,
    QA_BOT_DIRECTION_BFG_JUMP = 32
};
typedef struct qa_bot_move_input {
    qa_vec3 origin, velocity, view_offset;
    int32_t entity, client;
    float think_time;
    uint32_t presence;
    qa_vec3 view_angles;
    uint32_t flags;
} qa_bot_move_input;
typedef struct qa_bot_avoid_spot {
    qa_vec3 origin;
    float radius;
    int32_t type;
} qa_bot_avoid_spot;
typedef struct qa_bot_move_state {
    qa_bot_move_input input;
    uint32_t area, last_area, last_goal_area, last_reachability, reach_area, jump_reach;
    qa_vec3 last_origin;
    float grapple_visible_time, last_grapple_distance, reachability_time;
    uint32_t avoid_reachability;
    float avoid_time;
    int32_t avoid_tries;
    qa_bot_avoid_spot avoid_spots[QA_BOT_AVOID_SPOTS];
    uint32_t avoid_count;
    bool walk_progress;
    uint32_t walk_edge;
} qa_bot_move_state;
typedef struct qa_bot_move_result {
    bool failure, blocked;
    int32_t type, block_entity, travel_type;
    uint32_t flags;
    int32_t weapon;
    qa_vec3 direction, ideal_view_angles;
} qa_bot_move_result;
typedef enum qa_bot_model_kind {
    QA_BOT_MODEL_ELEVATOR,
    QA_BOT_MODEL_BOBBING,
    QA_BOT_MODEL_DOOR,
    QA_BOT_MODEL_TRAIN,
    QA_BOT_MODEL_STATIC
} qa_bot_model_kind;
typedef struct qa_bot_travel_model {
    int32_t entity;
    qa_actor_id actor;
    qa_vec3 origin;
    qa_bounds bounds;
    qa_bot_model_kind kind;
} qa_bot_travel_model;
typedef enum qa_bot_grapple_observation {
    QA_BOT_GRAPPLE_NONE,
    QA_BOT_GRAPPLE_FLYING,
    QA_BOT_GRAPPLE_PULLING
} qa_bot_grapple_observation;
typedef struct qa_bot_move_services {
    void *context;
    qa_bot_navigation *(*navigation)(void *, int32_t client);
    qa_actor_id (*actor)(void *, int32_t entity);
    int32_t (*entity_number)(void *, qa_actor_id);
    bool (*model)(void *, int32_t model, qa_bot_travel_model *, bool *, qa_error *);
    int32_t (*entity_model)(void *, int32_t entity);
    int32_t (*next_entity)(void *, int32_t after);
    int32_t (*entity_type)(void *, int32_t entity);
    int32_t (*entity_weapon)(void *, int32_t entity);
    /* Queries the selected actual arsenal; no source weapon fallback if absent. */
    bool (*travel_weapon)(void *, int32_t client, qa_nav_travel, int32_t *, bool *, qa_error *);
    bool (*grapple_state)(void *, int32_t client, qa_bot_grapple_observation *, qa_error *);
    void (*diagnostic)(void *, qa_script_severity, const char *);
    qa_bot_random_source random;
} qa_bot_move_services;
typedef struct qa_bot_moves qa_bot_moves;
bool qa_bot_moves_create(uint32_t maximum, qa_bot_library *, qa_bot_actions *,
                         const qa_bot_move_services *, qa_bot_moves **, qa_error *);
void qa_bot_moves_destroy(qa_bot_moves *);
bool qa_bot_moves_shutdown(qa_bot_moves *, qa_error *);
/* Borrowed actual MEMORY owner/allocation. The 772 bytes use source little
 * endian fields; they are not a native qa_bot_move_state structure overlay. */
qa_bot_memory *qa_bot_moves_memory(const qa_bot_moves *);
bool qa_bot_moves_allocation(const qa_bot_moves *,uint32_t,qa_bot_memory_allocation *,qa_error *);
bool qa_bot_moves_active(const qa_bot_moves *);
bool qa_bot_moves_setup(qa_bot_moves *, qa_error *);
bool qa_bot_moves_time(qa_bot_moves *, float, qa_error *);
bool qa_bot_moves_allocate(qa_bot_moves *, uint32_t *, qa_error *);
bool qa_bot_moves_has_handle(const qa_bot_moves *, uint32_t);
bool qa_bot_moves_free(qa_bot_moves *, uint32_t, qa_error *);
bool qa_bot_moves_initialize(qa_bot_moves *, uint32_t, const qa_bot_move_input *, qa_error *);
bool qa_bot_moves_reset(qa_bot_moves *, uint32_t, qa_error *);
bool qa_bot_moves_reset_avoid(qa_bot_moves *, uint32_t, bool last_only, qa_error *);
bool qa_bot_moves_avoid_spot(qa_bot_moves *, uint32_t, const qa_bot_avoid_spot *, qa_error *);
bool qa_bot_moves_capture(const qa_bot_moves *, uint32_t, qa_bot_move_state *, qa_error *);
bool qa_bot_moves_restore(qa_bot_moves *, uint32_t, const qa_bot_move_state *, qa_error *);
bool qa_bot_moves_direction(qa_bot_moves *, uint32_t, qa_vec3, float speed, uint32_t type, bool *,
                            qa_error *);
bool qa_bot_moves_goal(qa_bot_moves *, uint32_t, const qa_bot_goal *, uint32_t travel_flags,
                       qa_bot_move_result *in_out, qa_error *);
bool qa_bot_moves_view_target(qa_bot_moves *, uint32_t, const qa_bot_goal *, uint32_t travel_flags,
                              float look_ahead, qa_vec3 *, bool *, qa_error *);
bool qa_bot_moves_visible_position(qa_bot_moves *, int32_t client, qa_vec3 origin, uint32_t area,
                                   const qa_bot_goal *, uint32_t travel_flags, qa_vec3 *, bool *,
                                   qa_error *);
#endif
