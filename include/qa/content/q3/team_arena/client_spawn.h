/* Port of src/content/q3/team-arena/client-spawn.ts. GPL-2.0-or-later.
 * Mirror gaps: ps.ping/ps.weapon/ps.commandTime and s.legsAnim/torsoAnim
 * have no landed fields (weapon uses s.weapon; the rest are skipped);
 * ps.eventSequence is the mirror generic counter; the TS
 * playerStateToEntityState calls are the host snapshot callback. */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_CLIENT_SPAWN_H
#define QA_CONTENT_Q3_TEAM_ARENA_CLIENT_SPAWN_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/base/game/spawn.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/game/utilities.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/base/world.h"
#include "qa/content/q3/team_arena/client_effects.h"
#include "qa/content/q3/team_arena/client_events.h" /* qa_q3_ta_spawn_pose */
#include "qa/contracts/identity.h"
#include "qa/persistence/value.h"

enum { QA_Q3_TA_BODY_QUEUE_SIZE = 8 };

/* TS SpawnPoint. */
typedef struct qa_q3_ta_spawn_point { qa_vec3 origin, angles; qa_q3_game_entity *entity; } qa_q3_ta_spawn_point;
/* TS ClientSpawnFrame. */
typedef struct qa_q3_ta_client_spawn_frame {
  double time, game_type, inactivity_seconds, intermission_time;
} qa_q3_ta_client_spawn_frame;

/* TS ClientSpawnHost. think_pool/think_world back the constructor's
 * shared-storage check. selected_player NULL fn is TS undefined. */
typedef struct qa_q3_ta_client_spawn_host {
  qa_q3_entity_pool *pool;
  qa_q3_base_server_world *world;
  qa_q3_base_actor_spatial_queries *spatial;
  qa_q3_game_random *random;
  qa_q3_entity_pool *think_pool;
  qa_q3_base_server_world *think_world;
  qa_q3_entity_callback player_die, body_die;
  void *ctx;
  bool (*is_player)(void *ctx, qa_actor_id actor);
  qa_q3_ta_client_spawn_frame (*frame)(void *ctx);
  qa_q3_base_user_command (*user_command)(void *ctx, double client_num);
  const char *(*handicap)(void *ctx, double client_num); /* borrowed */
  qa_q3_ta_spawn_pose (*find_intermission_point)(void *ctx);
  void (*move_to_intermission)(void *ctx, qa_q3_game_entity *entity);
  void (*kill_box)(void *ctx, qa_q3_game_entity *entity);
  void (*selected_player)(void *ctx, qa_q3_game_entity *entity, qa_q3_ta_spawn_pose pose);
  void (*client_think)(void *ctx, double client_num, qa_q3_base_user_command command);
  qa_q3_ta_client_effects_context (*effects)(void *ctx);
  qa_q3_target_use_context (*targets)(void *ctx);
  void (*snapshot)(void *ctx, qa_q3_game_client *client, qa_q3_game_entity *entity);
} qa_q3_ta_client_spawn_host;

void qa_q3_ta_spawn_deathmatch_point(qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables);
void qa_q3_ta_spawn_player_start(qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables);
void qa_q3_ta_set_client_view_angle(qa_q3_game_entity *entity, qa_vec3 angles);

/* TS ClientSpawnState. has_body_queue false is TS null. */
typedef struct qa_q3_ta_client_spawn_state {
  bool has_body_queue;
  qa_q3_game_entity *body_queue_entities[QA_Q3_TA_BODY_QUEUE_SIZE];
  double body_queue_index;
} qa_q3_ta_client_spawn_state;
void qa_q3_ta_client_spawn_state_init(qa_q3_ta_client_spawn_state *state);

/* TS captureSaveState() result shape. */
typedef struct qa_q3_ta_client_spawn_save_state {
  bool has_body_queue;
  double entity_slots[QA_Q3_TA_BODY_QUEUE_SIZE];
  double index;
} qa_q3_ta_client_spawn_save_state;

typedef struct qa_q3_ta_client_spawn_runtime {
  qa_q3_ta_client_spawn_host host;
  qa_q3_ta_client_spawn_state *state;
  qa_q3_ta_client_spawn_state owned_state;
} qa_q3_ta_client_spawn_runtime;
void qa_q3_ta_client_spawn_runtime_init(qa_q3_ta_client_spawn_runtime *runtime,
    qa_q3_ta_client_spawn_host host, qa_q3_ta_client_spawn_state *state);
qa_q3_ta_client_spawn_save_state qa_q3_ta_client_spawn_runtime_capture_save_state(qa_q3_ta_client_spawn_runtime *runtime);
void qa_q3_ta_client_spawn_runtime_restore_save_state(qa_q3_ta_client_spawn_runtime *runtime, const qa_save_value *value);
bool qa_q3_ta_client_spawn_runtime_spot_would_telefrag(qa_q3_ta_client_spawn_runtime *runtime, qa_q3_game_entity *spot);
qa_q3_game_entity *qa_q3_ta_client_spawn_runtime_select_nearest(qa_q3_ta_client_spawn_runtime *runtime, qa_vec3 from);
qa_q3_game_entity *qa_q3_ta_client_spawn_runtime_select_random(qa_q3_ta_client_spawn_runtime *runtime);
qa_q3_ta_spawn_point qa_q3_ta_client_spawn_runtime_select_spawn_point(qa_q3_ta_client_spawn_runtime *runtime, qa_vec3 avoid);
qa_q3_ta_spawn_point qa_q3_ta_client_spawn_runtime_select_initial(qa_q3_ta_client_spawn_runtime *runtime);
qa_q3_ta_spawn_point qa_q3_ta_client_spawn_runtime_select_team(qa_q3_ta_client_spawn_runtime *runtime,
    qa_q3_base_team team, qa_q3_team_state state);
void qa_q3_ta_client_spawn_runtime_init_body_queue(qa_q3_ta_client_spawn_runtime *runtime);
void qa_q3_ta_client_spawn_runtime_body_sink(qa_q3_ta_client_spawn_runtime *runtime, qa_q3_game_entity *body);
qa_q3_game_entity *qa_q3_ta_client_spawn_runtime_copy_to_body_queue(qa_q3_ta_client_spawn_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_ta_client_spawn_runtime_client_spawn(qa_q3_ta_client_spawn_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_ta_client_spawn_runtime_respawn(qa_q3_ta_client_spawn_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_ta_client_spawn_runtime_bind_save_callbacks(qa_q3_ta_client_spawn_runtime *runtime);
#endif
