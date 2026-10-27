/* Port of src/content/q3/base/game/item-motion.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_ITEM_MOTION_H
#define QA_CONTENT_Q3_BASE_GAME_ITEM_MOTION_H
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/projectile.h"
#include "qa/content/q3/base/game/state.h"
typedef struct qa_q3_item_frame_time { double time, previous_time; } qa_q3_item_frame_time;
typedef struct qa_q3_item_world_mirror {
  void *ctx;
  qa_q3_actor_trace_result_mirror (*trace_actor)(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds shape, const qa_actor_id *pass, double mask);
  double (*point_contents)(void *ctx, qa_vec3 point, double pass);
} qa_q3_item_world_mirror;
typedef struct qa_q3_run_item_context {
  qa_q3_item_frame_time frame; qa_q3_entity_pool *entities; qa_q3_item_world_mirror world;
  qa_q3_entity_callback free_team_entity;
  const qa_q3_game_item_definition_mirror *items; size_t item_count;
} qa_q3_run_item_context;
typedef struct qa_q3_launch_item_context {
  qa_q3_entity_pool *entities; qa_q3_game_product product; double game_type, time;
  qa_q3_entity_callback touch_item, dropped_flag_think, check_dropped_team_item;
  const qa_q3_game_item_definition_mirror *items; size_t item_count;
} qa_q3_launch_item_context;
typedef struct qa_q3_drop_item_context {
  qa_q3_launch_item_context launch; void *random_ctx; double (*random)(void *ctx);
} qa_q3_drop_item_context;
void qa_q3_bounce_item(qa_q3_game_entity *entity, qa_q3_actor_trace_result_mirror trace, qa_q3_item_frame_time frame, qa_q3_entity_pool *entities);
void qa_q3_run_item(qa_q3_game_entity *entity, qa_q3_run_item_context context);
qa_q3_game_entity *qa_q3_launch_item(qa_q3_launch_item_context context, const qa_q3_game_item_definition_mirror *item, qa_vec3 origin, qa_vec3 velocity);
qa_q3_game_entity *qa_q3_drop_item(qa_q3_drop_item_context context, qa_q3_game_entity *entity, const qa_q3_game_item_definition_mirror *item, double angle);
void qa_q3_bind_launch_save_callbacks(qa_q3_launch_item_context context);
#endif
