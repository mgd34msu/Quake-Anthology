/* Port of src/content/q3/base/game/entities.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_ENTITIES_H
#define QA_CONTENT_Q3_BASE_GAME_ENTITIES_H
#include <stdbool.h>
#include "qa/content/q3/base/game/rankings.h"
#include "qa/content/q3/base/game/save_callbacks.h"
#include "qa/content/q3/base/game/state.h"
/* TS EntityPoolOptions.records as fn+ctx. */
typedef struct qa_q3_entity_records {
  void *ctx; qa_q3_game_entity *(*get)(void *ctx, double slot);
  qa_q3_game_entity *(*native_by_actor)(void *ctx, qa_actor_id actor);
  qa_q3_game_client *(*client)(void *ctx, double slot);
  qa_q3_game_entity *(*activate)(void *ctx, double slot);
  void (*release)(void *ctx, qa_q3_game_entity *entity);
  void (*deactivate_client)(void *ctx, double slot);
} qa_q3_entity_records;
/* event_debug NULL is TS eventDebug undefined; borrowed, set on every client ps. */
typedef struct qa_q3_entity_pool_options {
  qa_q3_base_predictable_event_debug *event_debug;
  qa_q3_entity_records records; qa_q3_base_product product; double max_clients, map_start_time;
  void *ctx; double (*time)(void *ctx); void (*print)(void *ctx, const char *message);
  void (*link)(void *ctx, qa_q3_game_entity *entity); void (*unlink)(void *ctx, qa_q3_game_entity *entity);
} qa_q3_entity_pool_options;
typedef enum qa_q3_entity_event_status { QA_Q3_EVENT_INACTIVE, QA_Q3_EVENT_ACTIVE, QA_Q3_EVENT_WAITING, QA_Q3_EVENT_FREED } qa_q3_entity_event_status;
const char *qa_q3_entity_event_status_name(qa_q3_entity_event_status status);
struct qa_q3_game_utility_scratch;
typedef struct qa_q3_entity_pool {
  qa_q3_entity_pool_options options; qa_q3_ranking_reports rankings; qa_q3_callback_catalog callbacks;
  qa_q3_game_client *clients[QA_Q3_MAX_CLIENTS]; struct qa_q3_game_utility_scratch *utilities;
  double num_entities, max_clients;
} qa_q3_entity_pool;
void qa_q3_init_game_entity(qa_q3_game_entity *entity);
void qa_q3_set_origin(qa_q3_game_entity *entity, qa_vec3 origin);
void qa_q3_run_think(qa_q3_game_entity *entity, double time);
/* utilities scratch owned by pool; created in init, freed in destroy (utilities.c). */
void qa_q3_entity_pool_init(qa_q3_entity_pool *pool, qa_q3_entity_pool_options options);
void qa_q3_entity_pool_destroy(qa_q3_entity_pool *pool);
qa_q3_game_entity *qa_q3_entity_pool_activate_client(qa_q3_entity_pool *pool, double slot);
void qa_q3_entity_pool_deactivate_client(qa_q3_entity_pool *pool, double slot);
typedef struct qa_q3_pool_counts { double num_entities, max_clients; } qa_q3_pool_counts;
void qa_q3_entity_pool_restore_counts(qa_q3_entity_pool *pool, qa_q3_pool_counts state);
void qa_q3_entity_pool_clear_level(qa_q3_entity_pool *pool);
void qa_q3_entity_pool_clear_entities(qa_q3_entity_pool *pool);
void qa_q3_entity_pool_initialize_clients(qa_q3_entity_pool *pool, double max_clients);
qa_q3_game_entity *qa_q3_entity_pool_get(qa_q3_entity_pool *pool, double number);
qa_q3_game_entity *qa_q3_entity_pool_at(qa_q3_entity_pool *pool, double number);
qa_q3_game_client *qa_q3_entity_pool_client_at(qa_q3_entity_pool *pool, double number);
qa_q3_game_entity *qa_q3_entity_pool_spawn(qa_q3_entity_pool *pool);
bool qa_q3_entity_pool_entities_free(qa_q3_entity_pool *pool);
void qa_q3_entity_pool_free(qa_q3_entity_pool *pool, qa_q3_game_entity *entity);
qa_q3_game_entity *qa_q3_entity_pool_temp_entity(qa_q3_entity_pool *pool, qa_vec3 origin, double event);
/* Returns false for TS null. */
bool qa_q3_entity_pool_add_predictable_event(qa_q3_entity_pool *pool, qa_q3_game_entity *entity, double event, double parameter, qa_q3_base_predictable_event *out);
void qa_q3_entity_pool_add_event(qa_q3_entity_pool *pool, qa_q3_game_entity *entity, double event, double parameter);
qa_q3_entity_event_status qa_q3_entity_pool_expire_events(qa_q3_entity_pool *pool, qa_q3_game_entity *entity);
#endif
