/* Port of src/content/q3/team-arena/client-think.ts. GPL-2.0-or-later.
 * Mirror gaps: ps.commandTime/weapon/weaponTime/gravity/speed and the
 * jumppad counters have no landed fields; commandTime/weapon/
 * weaponTime/gravity/speed go through host accessors (NULL setter skips
 * the store), jumppad reset is skipped. Snapshot conversion is a host
 * callback; the default speed multiplier mirrors client-effects logic
 * on mirror slots. */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_CLIENT_THINK_H
#define QA_CONTENT_Q3_TEAM_ARENA_CLIENT_THINK_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/base/world.h"
#include "qa/content/q3/team_arena/client_effects.h"
#include "qa/content/q3/team_arena/movement_host.h"
#include "qa/contracts/identity.h"

typedef struct qa_q3_ta_client_think_frame { double time, intermission_time, intermission_queued; } qa_q3_ta_client_think_frame;
typedef struct qa_q3_ta_client_think_settings {
  double debug_move; bool synchronous_clients, pmove_fixed; double pmove_msec;
  double gravity, speed, dmflags; bool smooth_clients; double force_respawn_seconds; bool single_player;
} qa_q3_ta_client_think_settings;

/* TS ClientTouchAccess. native NULL is TS null. */
typedef struct qa_q3_ta_client_touch_access {
  void *ctx;
  qa_q3_game_entity *(*native)(void *ctx, qa_actor_id actor);
  bool (*is_trigger)(void *ctx, qa_actor_id actor);
  void (*touch)(void *ctx, qa_actor_id self, qa_actor_id other);
} qa_q3_ta_client_touch_access;

/* TS ClientThinkHost. timer_ownership/speed_multiplier NULL fn is TS
 * undefined (has_* in results distinguish omission). */
typedef struct qa_q3_ta_client_think_host {
  qa_q3_ta_client_movement_host movement;
  qa_q3_entity_pool *pool;
  qa_q3_base_server_world *world;
  qa_q3_base_actor_spatial_queries *spatial;
  qa_q3_ta_client_touch_access touches;
  qa_q3_ta_event_context effects;
  void *ctx;
  bool (*timer_ownership)(void *ctx, qa_actor_id actor, qa_q3_ta_client_timer_ownership *out);
  bool (*speed_multiplier)(void *ctx, qa_actor_id actor, double *out);
  qa_q3_ta_client_think_frame (*frame)(void *ctx);
  qa_q3_ta_client_think_settings (*settings)(void *ctx);
  void (*set_pmove_msec)(void *ctx, double milliseconds);
  void (*intermission_think)(void *ctx, qa_q3_game_client *client);
  void (*spectator_think)(void *ctx, qa_q3_game_entity *entity, qa_q3_base_user_command command);
  bool (*check_inactivity)(void *ctx, qa_q3_game_client *client);
  void (*free_hook)(void *ctx, qa_q3_game_entity *hook);
  bool (*check_gauntlet_attack)(void *ctx, qa_q3_game_entity *entity);
  void (*client_events)(void *ctx, qa_q3_game_entity *entity, double old_event_sequence);
  void (*respawn)(void *ctx, qa_q3_game_entity *entity);
  void (*append_console_command)(void *ctx, const char *command);
  bool (*is_door_trigger)(void *ctx, qa_q3_game_entity *entity);
  void (*bot_test_aas)(void *ctx, qa_vec3 origin);
  void (*snapshot)(void *ctx, qa_q3_game_client *client, qa_q3_game_entity *entity);
  void (*snapshot_extrapolate)(void *ctx, qa_q3_game_client *client, qa_q3_game_entity *entity, double time);
  /* Mirror-gap ps accessors (commandTime is required for msec). */
  double (*command_time)(void *ctx, qa_q3_game_client *client);
  double (*weapon)(void *ctx, qa_q3_game_client *client);
  double (*weapon_time)(void *ctx, qa_q3_game_client *client);
  void (*set_gravity)(void *ctx, qa_q3_game_client *client, double value);
  void (*set_speed)(void *ctx, qa_q3_game_client *client, double value);
} qa_q3_ta_client_think_host;

typedef struct qa_q3_ta_client_think_runtime { qa_q3_ta_client_think_host host; } qa_q3_ta_client_think_runtime;
void qa_q3_ta_client_think_runtime_init(qa_q3_ta_client_think_runtime *runtime, qa_q3_ta_client_think_host host);
void qa_q3_ta_client_think_runtime_client_think(qa_q3_ta_client_think_runtime *runtime,
    double client_num, qa_q3_base_user_command command);
void qa_q3_ta_client_think_runtime_run_client(qa_q3_ta_client_think_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_ta_client_think_runtime_client_impacts(qa_q3_ta_client_think_runtime *runtime,
    qa_q3_game_entity *entity, const qa_actor_id *contacts, size_t contact_count);
void qa_q3_ta_client_think_runtime_touch_triggers(qa_q3_ta_client_think_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_ta_client_think_runtime_client_think_real(qa_q3_ta_client_think_runtime *runtime, qa_q3_game_entity *entity);

void qa_q3_ta_expand_invulnerability(qa_q3_entity_pool *pool, qa_q3_base_server_world *world, qa_q3_game_entity *entity);
#endif
