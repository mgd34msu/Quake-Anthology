#ifndef QA_BOT_MOVEMENT_INTERNAL_H
#define QA_BOT_MOVEMENT_INTERNAL_H
#include "qa/bot_movement_source.h"
#include "qa/bots_allocator.h"
#include "qa/stamp.h"
#include <setjmp.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
typedef enum bot_move_variable {
    BOT_STEP,
    BOT_BARRIER,
    BOT_GRAVITY,
    BOT_ROCKET,
    BOT_BFG,
    BOT_GRAPPLE,
    BOT_MISSILE_TYPE,
    BOT_OFFHAND_GRAPPLE,
    BOT_GRAPPLE_ON,
    BOT_GRAPPLE_OFF,
    BOT_MOVE_VARIABLE_COUNT
} bot_move_variable;
enum { BOT_MOVE_STATE_BYTES = 772, BOT_MOVE_MAX_STATES = 64 };
typedef enum bot_move_field {
    BM_ORIGIN=0, BM_VELOCITY=12, BM_VIEW_OFFSET=24, BM_ENTITY=36, BM_CLIENT=40,
    BM_THINK_TIME=44, BM_PRESENCE=48, BM_VIEW_ANGLES=52, BM_AREA=64,
    BM_LAST_AREA=68, BM_LAST_GOAL_AREA=72, BM_LAST_REACHABILITY=76,
    BM_LAST_ORIGIN=80, BM_REACH_AREA=92, BM_FLAGS=96, BM_JUMP_REACH=100,
    BM_GRAPPLE_VISIBLE_TIME=104, BM_LAST_GRAPPLE_DISTANCE=108,
    BM_REACHABILITY_TIME=112, BM_AVOID_REACHABILITY=116, BM_AVOID_TIME=120,
    BM_AVOID_TRIES=124, BM_AVOID_SPOTS=128, BM_AVOID_COUNT=768
} bot_move_field;
typedef struct bot_move_record {
    qa_bot_moves *owner;
    qa_bot_memory_allocation allocation;
    bool walk_progress;
    uint32_t walk_edge;
    qa_nav_route route;
    qa_nav_map route_map;
    qa_actor_id route_actor;
    uint32_t route_goal, route_flags, route_move_flags;
    size_t route_cursor;
} bot_move_record;
typedef struct bot_move_scope {
    jmp_buf jump;
    struct bot_move_scope *previous;
    qa_error *error;
    bool busy;
} bot_move_scope;
/* The scope implements source exceptions from an invalidated raw alias. All
 * route storage belongs to the movement owner, including on an early exit. */
#define BOT_MOVE_OPERATION(m,e,call) do { \
    if (!(m)) return (call); \
    bot_move_scope scope = {.previous=(m)->scope, .error=(e), .busy=(m)->busy}; \
    (m)->scope=&scope; \
    bool operation_ok; \
    if (setjmp(scope.jump)) operation_ok=false; else operation_ok=(call); \
    (m)->scope=scope.previous; (m)->busy=scope.busy; \
    return operation_ok; \
} while (0)
typedef struct bot_move_slot {
    bool used;
    bot_move_record state;
} bot_move_slot;
struct qa_bot_moves {
    uint32_t maximum;
    bot_move_slot *slots;
    qa_bot_library *library;
    qa_bot_memory *memory;
    bot_move_scope *scope;
    qa_bot_actions *actions;
    qa_bot_move_services services;
    const qa_bot_variable *variables[BOT_MOVE_VARIABLE_COUNT];
    float time;
    bool busy;
    qa_nav_prediction_result prediction;
    qa_nav_route trajectory;
    qa_nav_workspace *workspace;
    qa_vec3 *points;
    size_t point_count, point_capacity;
    qa_stamp_set visited;
};
typedef struct bot_travel {
    qa_bot_moves *moves;
    bot_move_record *state;
    qa_bot_navigation *navigation;
    qa_navigation *runtime;
    const qa_nav_graph_view *graph;
    qa_actor_id actor;
} bot_travel;
typedef struct bot_reach {
    const qa_nav_edge *graph_edge;
    uint32_t area, number, type, time;
    int32_t face, edge;
    qa_vec3 start, end;
} bot_reach;
enum {
    BOT_INVALID = 1,
    BOT_WALK = 2,
    BOT_CROUCH = 3,
    BOT_BARRIER_JUMP = 4,
    BOT_JUMP = 5,
    BOT_LADDER = 6,
    BOT_DROP = 7,
    BOT_SWIM = 8,
    BOT_WATER_JUMP = 9,
    BOT_TELEPORT = 10,
    BOT_ELEVATOR = 11,
    BOT_ROCKET_JUMP = 12,
    BOT_BFG_JUMP = 13,
    BOT_GRAPPLE_HOOK = 14,
    BOT_DOUBLE_JUMP = 15,
    BOT_RAMP_JUMP = 16,
    BOT_STRAFE_JUMP = 17,
    BOT_JUMP_PAD = 18,
    BOT_BOBBING = 19,
    BOT_TRAVEL_MASK = 0xffffff
};
static inline qa_vec3 bot_horizontal(qa_vec3 from, qa_vec3 to) {
    return qa_v3(to.x - from.x, to.y - from.y, 0);
}
static inline qa_vec3 bot_ma(qa_vec3 start, float distance, qa_vec3 direction) {
    return qa_vec_add(start, qa_vec_scale(direction, distance));
}
static inline float bot_variable(const bot_travel *t, bot_move_variable v) {
    return t->moves->variables[v]->value;
}
bool bot_travel_begin(qa_bot_moves *, bot_move_record *, bot_travel *, qa_error *);
bool bot_travel_begin_client(qa_bot_moves *, int32_t, bot_travel *, qa_error *);
bool bot_travel_ready(const qa_bot_moves *, qa_error *);
bool bot_reach_read(const bot_travel *, uint32_t, bot_reach *, bool *, qa_error *);
bool bot_reach_describe(const bot_travel *, const qa_nav_edge *, bot_reach *, qa_error *);
bool bot_reach_select(bot_travel *, const qa_bot_move_goal_source *, uint32_t, uint32_t, uint32_t *, uint32_t *,
                      qa_error *);
float bot_reach_time(const bot_reach *);
bool bot_travel_points(bot_travel *, const qa_bot_vector_source *, uint32_t, uint32_t, uint32_t, bool *,
                       qa_error *);
bool bot_trace(bot_travel *, qa_vec3, qa_vec3, const qa_bounds *, int32_t, uint32_t,
               qa_trace_result *, qa_error *);
bool bot_trace_box(bot_travel *, qa_vec3, qa_vec3, uint32_t, int32_t, qa_trace_result *,
                   qa_error *);
int32_t bot_trace_entity(const bot_travel *, const qa_trace_result *);
bool bot_on_ground(bot_travel *, bool *, qa_error *);
bool bot_against_ladder(bot_travel *, bool *, qa_error *);
bool bot_on_mover(bot_travel *, const bot_reach *, bool *, qa_error *);
bool bot_mover_down(bot_travel *, const bot_reach *, bool *, qa_error *);
bool bot_model(bot_travel *, int32_t, qa_bot_travel_model *, bool *, qa_error *);
bool bot_predict(bot_travel *, const qa_nav_prediction_query *, qa_error *);
bool bot_gap_distance(bot_travel *, qa_vec3, qa_vec3, float *, qa_error *);
bool bot_gap_distance_state(bot_travel *, qa_vec3, float *, qa_error *);
bool bot_barrier_jump(bot_travel *, qa_vec3, float, bool *, qa_error *);
bool bot_barrier_jump_from(bot_travel *, const qa_bot_vector_source *, float, bool *, qa_error *);
bool bot_blocked(bot_travel *, qa_vec3, bool, qa_bot_move_result *, qa_error *);
bool bot_air_control(bot_travel *, qa_vec3, bool *, qa_vec3 *, float *, qa_error *);
bool bot_jump_speed(bot_travel *, qa_vec3, qa_vec3, float, float *, qa_error *);
bool bot_jump_run_start(bot_travel *, const bot_reach *, qa_vec3 *, qa_error *);
uint32_t bot_area_presence(bot_travel *, uint32_t);
qa_vec3 bot_vector_angles(qa_vec3);
bool bot_move_action(bot_travel *, qa_vec3, float, qa_error *);
bool bot_flag_action(bot_travel *, uint32_t, qa_error *);
bool bot_jump_action(bot_travel *, bool, qa_error *);
bool bot_view_action(bot_travel *, qa_vec3, qa_error *);
bool bot_weapon_action(bot_travel *, int32_t, qa_error *);
bool bot_text_action(bot_travel *, const char *, qa_error *);
bool bot_ground_travel(bot_travel *, const bot_reach *, bool, qa_bot_move_result *, qa_error *);
bool bot_special_travel(bot_travel *, const bot_reach *, bool, qa_bot_move_result *, qa_error *);
bool bot_reset_grapple(bot_travel *, qa_error *);
bool bot_move_fail(qa_error *, const char *);
bool bot_move_mutable(qa_bot_moves *, qa_error *);
const char *bot_move_variable_name(bot_move_variable);
bot_move_record *bot_move_state(const qa_bot_moves *, uint32_t, qa_error *);
bot_move_record *bot_move_source_state(qa_bot_moves *, uint32_t);
void bot_move_avoid(qa_bot_moves *, bot_move_record *, uint32_t, float);
void bot_move_set_reach(bot_move_record *, uint32_t);
bool bot_move_record_span(const bot_move_record *, qa_bot_memory_span *, qa_error *);
uint32_t bot_move_word(const bot_move_record *, bot_move_field);
int32_t bot_move_integer(const bot_move_record *, bot_move_field);
float bot_move_float(const bot_move_record *, bot_move_field);
qa_vec3 bot_move_vector(const bot_move_record *, bot_move_field);
void bot_move_write_word(bot_move_record *, bot_move_field, uint32_t);
void bot_move_write_float(bot_move_record *, bot_move_field, float);
void bot_move_write_vector(bot_move_record *, bot_move_field, qa_vec3);
qa_bot_avoid_spot bot_move_spot(const bot_move_record *, int32_t);
void bot_move_spot_admit(const bot_move_record *, int32_t);
void bot_move_write_spot(bot_move_record *, int32_t, qa_bot_avoid_spot);
qa_bot_vector_source bot_move_origin_source(bot_move_record *);
void bot_move_record_snapshot(const bot_move_record *, qa_bot_move_state *);
void bot_move_route_clear(bot_move_record *);
bool bot_move_route_copy(bot_move_record *, const bot_move_record *, qa_error *);
bool bot_move_route_bind(bot_move_record *, qa_bot_navigation *, qa_error *);
bool bot_goal_area(const qa_bot_move_goal_source *, uint32_t *, qa_error *);
qa_bot_vector_source bot_goal_origin(const qa_bot_move_goal_source *);
bool bot_result_read(const qa_bot_move_result_io *, qa_bot_move_result_field, int32_t *, qa_error *);
bool bot_result_write(const qa_bot_move_result_io *, qa_bot_move_result_field, int32_t, qa_error *);
bool bot_result_flags(const qa_bot_move_result_io *, uint32_t, qa_error *);
bool bot_result_clear(const qa_bot_move_result_io *, qa_error *);
bool bot_result_copy(const qa_bot_move_result_io *, const qa_bot_move_result *, qa_error *);
#endif
