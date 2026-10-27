/* Port of src/bots/behavior/q3/navigation-types.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_NAVIGATION_TYPES_H
#define QA_BOTS_BEHAVIOR_Q3_NAVIGATION_TYPES_H
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum {
  QA_TRAVEL_INVALID = 1,
  QA_TRAVEL_WALK = 2,
  QA_TRAVEL_CROUCH = 3,
  QA_TRAVEL_BARRIERJUMP = 4,
  QA_TRAVEL_JUMP = 5,
  QA_TRAVEL_LADDER = 6,
  QA_TRAVEL_WALKOFFLEDGE = 7,
  QA_TRAVEL_SWIM = 8,
  QA_TRAVEL_WATERJUMP = 9,
  QA_TRAVEL_TELEPORT = 10,
  QA_TRAVEL_ELEVATOR = 11,
  QA_TRAVEL_ROCKETJUMP = 12,
  QA_TRAVEL_BFGJUMP = 13,
  QA_TRAVEL_GRAPPLEHOOK = 14,
  QA_TRAVEL_DOUBLEJUMP = 15,
  QA_TRAVEL_RAMPJUMP = 16,
  QA_TRAVEL_STRAFEJUMP = 17,
  QA_TRAVEL_JUMPPAD = 18,
  QA_TRAVEL_FUNCBOB = 19
};
#define QA_TRAVEL_MASK 0xffffff
#define QA_TRAVEL_NOTTEAM1 (1 << 24)
#define QA_TRAVEL_NOTTEAM2 (2 << 24)
enum {
  QA_TFL_INVALID = 0x1,
  QA_TFL_WALK = 0x2,
  QA_TFL_CROUCH = 0x4,
  QA_TFL_BARRIERJUMP = 0x8,
  QA_TFL_JUMP = 0x10,
  QA_TFL_LADDER = 0x20,
  QA_TFL_WALKOFFLEDGE = 0x80,
  QA_TFL_SWIM = 0x100,
  QA_TFL_WATERJUMP = 0x200,
  QA_TFL_TELEPORT = 0x400,
  QA_TFL_ELEVATOR = 0x800,
  QA_TFL_ROCKETJUMP = 0x1000,
  QA_TFL_BFGJUMP = 0x2000,
  QA_TFL_GRAPPLEHOOK = 0x4000,
  QA_TFL_DOUBLEJUMP = 0x8000,
  QA_TFL_RAMPJUMP = 0x10000,
  QA_TFL_STRAFEJUMP = 0x20000,
  QA_TFL_JUMPPAD = 0x40000,
  QA_TFL_AIR = 0x80000,
  QA_TFL_WATER = 0x100000,
  QA_TFL_SLIME = 0x200000,
  QA_TFL_LAVA = 0x400000,
  QA_TFL_DONOTENTER = 0x800000,
  QA_TFL_FUNCBOB = 0x1000000,
  QA_TFL_FLIGHT = 0x2000000,
  QA_TFL_BRIDGE = 0x4000000,
  QA_TFL_NOTTEAM1 = 0x8000000,
  QA_TFL_NOTTEAM2 = 0x10000000,
  QA_TFL_DEFAULT = 0x011c0fbe
};
int64_t qa_travel_flag_for_type(int64_t travel_type);
typedef struct qa_route_query {
  int64_t area;
  qa_vec3 origin;
  int64_t goal_area;
  int64_t travel_flags;
} qa_route_query;
typedef struct qa_area_travel_time_query {
  int64_t area;
  bool has_origin;
  qa_vec3 origin;
  int64_t goal_area;
  int64_t travel_flags;
} qa_area_travel_time_query;
enum { QA_ALT_ROUTE_ALL = 1, QA_ALT_ROUTE_CLUSTER_PORTALS = 2, QA_ALT_ROUTE_VIEW_PORTALS = 4 };
typedef struct qa_alternative_route_query {
  qa_vec3 start;
  int64_t start_area;
  qa_vec3 goal;
  int64_t goal_area;
  int64_t travel_flags;
  int64_t maximum_goals;
  int64_t type;
} qa_alternative_route_query;
typedef struct qa_alternative_goal {
  qa_vec3 origin;
  int64_t area;
  double start_travel_time, goal_travel_time, extra_travel_time;
} qa_alternative_goal;
typedef enum qa_route_result_kind { QA_ROUTE_FOUND, QA_ROUTE_UNREACHABLE } qa_route_result_kind;
typedef struct qa_route_result {
  qa_route_result_kind kind;
  double travel_time;
  int64_t next_reachability;
} qa_route_result;
enum {
  QA_ROUTE_STOP_NONE = 0,
  QA_ROUTE_STOP_NO_ROUTE = 1,
  QA_ROUTE_STOP_USE_TRAVEL_TYPE = 2,
  QA_ROUTE_STOP_ENTER_CONTENTS = 4,
  QA_ROUTE_STOP_ENTER_AREA = 8
};
typedef struct qa_predict_route_query {
  qa_route_query route;
  int64_t maximum_areas, maximum_time, stop_event, stop_contents, stop_travel_flags, stop_area;
} qa_predict_route_query;
typedef struct qa_predicted_route {
  bool succeeded;
  int64_t stop_event, end_area, end_contents, end_travel_flags;
  qa_vec3 end_position;
  double time;
} qa_predicted_route;
typedef struct qa_bot_navigation_area {
  int64_t contents, flags, presence_type, cluster, reachable_area_count;
} qa_bot_navigation_area;
typedef struct qa_bot_movement_prediction {
  int64_t entity_num;
  qa_vec3 origin;
  int64_t presence;
  bool on_ground;
  qa_vec3 velocity, command_move;
  int64_t command_frames, max_frames;
  double frame_time;
  int64_t stop_events, stop_area;
  bool visualize;
} qa_bot_movement_prediction;
/* TS BotNavigation interface as fn+ctx vtable (owner: navigation-types.ts). */
typedef struct qa_bot_navigation_bots_a {
  bool ready;
  void *ctx;
  int64_t (*point_area)(void *ctx, qa_vec3 origin);
  int64_t (*reachability_area)(void *ctx, qa_vec3 origin, int64_t client);
  int64_t (*fuzzy_point_reachability_area)(void *ctx, qa_vec3 origin);
  qa_bot_navigation_area (*get_area)(void *ctx, int64_t number);
  void (*set_area_enabled)(void *ctx, int64_t area, bool enabled);
  double (*area_travel_time_to_goal)(void *ctx, qa_area_travel_time_query q);
  qa_route_result (*route)(void *ctx, qa_route_query q);
  qa_predicted_route (*predict_route)(void *ctx, qa_predict_route_query q);
  bool (*swimming)(void *ctx, qa_vec3 origin);
  qa_bounds (*presence_bounds)(void *ctx, int64_t presence);
} qa_bot_navigation_bots_a;
#endif
