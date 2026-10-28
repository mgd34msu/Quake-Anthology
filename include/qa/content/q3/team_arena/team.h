/* Port of src/content/q3/team-arena/team.ts. GPL-2.0-or-later.
 * ps.generic1/ps.weapon have no landed mirror fields: generic1 goes
 * through host accessors; teamplayInfoMessage reads s.weapon for the
 * TS ps.weapon slot. captureSaveState's object becomes a struct. */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_TEAM_H
#define QA_CONTENT_Q3_TEAM_ARENA_TEAM_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/world.h"
#include "qa/persistence/value.h"

typedef enum qa_q3_ta_flag_status {
  QA_Q3_TA_FLAG_AT_BASE = 0, QA_Q3_TA_FLAG_TAKEN = 1, QA_Q3_TA_FLAG_TAKEN_RED = 2,
  QA_Q3_TA_FLAG_TAKEN_BLUE = 3, QA_Q3_TA_FLAG_DROPPED = 4
} qa_q3_ta_flag_status;
typedef enum qa_q3_ta_global_team_sound {
  QA_Q3_TA_GTS_RED_CAPTURE = 0, QA_Q3_TA_GTS_BLUE_CAPTURE = 1, QA_Q3_TA_GTS_RED_RETURN = 2,
  QA_Q3_TA_GTS_BLUE_RETURN = 3, QA_Q3_TA_GTS_RED_TAKEN = 4, QA_Q3_TA_GTS_BLUE_TAKEN = 5,
  QA_Q3_TA_GTS_RED_OBELISK_ATTACKED = 6, QA_Q3_TA_GTS_BLUE_OBELISK_ATTACKED = 7,
  QA_Q3_TA_GTS_RED_SCORED = 8, QA_Q3_TA_GTS_BLUE_SCORED = 9, QA_Q3_TA_GTS_RED_TOOK_LEAD = 10,
  QA_Q3_TA_GTS_BLUE_TOOK_LEAD = 11, QA_Q3_TA_GTS_TIED = 12, QA_Q3_TA_GTS_KAMIKAZE = 13
} qa_q3_ta_global_team_sound;

typedef struct qa_q3_ta_obelisk_settings {
  double health, regen_period_seconds, regen_amount, respawn_delay_seconds;
} qa_q3_ta_obelisk_settings;

/* TS TeamHost = TeamServices & product (+ obelisk for missionpack).
 * has_obelisk false is the baseq3 variant. */
typedef struct qa_q3_ta_team_host {
  qa_q3_base_product product;
  bool has_obelisk; qa_q3_ta_obelisk_settings obelisk;
  qa_q3_entity_pool *pool;
  qa_q3_base_server_world *world;                 /* borrowed */
  qa_q3_base_actor_spatial_queries *spatial;      /* borrowed; trace_actor */
  double game_type, time;
  qa_q3_game_player_slots_mirror *team_scores;
  const double *sorted_clients; size_t sorted_client_count;
  qa_q3_game_entity *location_head;
  void *ctx;
  void (*send_server_command)(void *ctx, double client_num, const char *text);
  void (*set_configstring)(void *ctx, double index, const char *text);
  void (*warn)(void *ctx, const char *text);
  void (*add_score)(void *ctx, qa_q3_game_entity *player, qa_vec3 origin, double score);
  void (*calculate_ranks)(void *ctx);
  void (*respawn_item)(void *ctx, qa_q3_game_entity *item);
  bool (*in_pvs)(void *ctx, qa_vec3 first, qa_vec3 second);
  double (*generic1)(void *ctx, qa_q3_game_client *client);
  void (*set_generic1)(void *ctx, qa_q3_game_client *client, double value);
} qa_q3_ta_team_host;

/* TS TeamGameState. Status fields hold FlagStatus | -1 as double. */
typedef struct qa_q3_ta_team_game_state {
  double last_flag_capture, last_capture_team;
  double red_status, blue_status, flag_status;
  double red_taken_time, blue_taken_time;
  double red_obelisk_attacked_time, blue_obelisk_attacked_time;
} qa_q3_ta_team_game_state;
void qa_q3_ta_team_game_state_init(qa_q3_ta_team_game_state *state);

qa_q3_base_team qa_q3_ta_other_team(qa_q3_base_team team);
const char *qa_q3_ta_team_name(qa_q3_base_team team);
const char *qa_q3_ta_other_team_name(qa_q3_base_team team);
const char *qa_q3_ta_team_color_string(qa_q3_base_team team);
bool qa_q3_ta_on_same_team(double game_type, qa_q3_game_entity *first, qa_q3_game_entity *second);
void qa_q3_ta_spawn_team_point(qa_q3_game_entity *entity);

/* TS captureSaveState() result shape. */
typedef struct qa_q3_ta_team_save_state {
  qa_q3_ta_team_game_state state;
  bool has_neutral_obelisk; double neutral_obelisk_slot; /* false = TS null */
  double last_team_location_time;
} qa_q3_ta_team_save_state;

/* TS TeamRuntime. */
typedef struct qa_q3_ta_team_runtime {
  qa_q3_ta_team_host host;
  qa_q3_ta_team_game_state state;
  qa_q3_game_entity *neutral_obelisk;
  double last_team_location_time;
} qa_q3_ta_team_runtime;
void qa_q3_ta_team_runtime_init(qa_q3_ta_team_runtime *runtime, qa_q3_ta_team_host host);
qa_q3_ta_team_save_state qa_q3_ta_team_runtime_capture_save_state(qa_q3_ta_team_runtime *runtime);
void qa_q3_ta_team_runtime_restore_save_state(qa_q3_ta_team_runtime *runtime, const qa_save_value *value);
void qa_q3_ta_team_runtime_init_game(qa_q3_ta_team_runtime *runtime);
void qa_q3_ta_team_runtime_print_message(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, const char *text);
void qa_q3_ta_team_runtime_add_team_score(qa_q3_ta_team_runtime *runtime, qa_vec3 origin, qa_q3_base_team team, double score);
void qa_q3_ta_team_runtime_set_flag_status(qa_q3_ta_team_runtime *runtime, qa_q3_base_team team, qa_q3_ta_flag_status status);
void qa_q3_ta_team_runtime_check_dropped_item(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_ta_team_runtime_force_gesture(qa_q3_ta_team_runtime *runtime, qa_q3_base_team team);
void qa_q3_ta_team_runtime_check_hurt_carrier(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
void qa_q3_ta_team_runtime_frag_bonuses(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
qa_q3_game_entity *qa_q3_ta_team_runtime_reset_flag(qa_q3_ta_team_runtime *runtime, qa_q3_base_team team);
void qa_q3_ta_team_runtime_reset_flags(qa_q3_ta_team_runtime *runtime);
void qa_q3_ta_team_runtime_return_flag_sound(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_base_team team);
void qa_q3_ta_team_runtime_take_flag_sound(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_base_team team);
void qa_q3_ta_team_runtime_capture_flag_sound(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_base_team team);
void qa_q3_ta_team_runtime_return_flag(qa_q3_ta_team_runtime *runtime, qa_q3_base_team team);
void qa_q3_ta_team_runtime_free_entity(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_ta_team_runtime_dropped_flag_think(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity);
double qa_q3_ta_team_runtime_touch_our_flag(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_game_entity *other, qa_q3_base_team team);
double qa_q3_ta_team_runtime_touch_enemy_flag(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_game_entity *other, qa_q3_base_team team);
double qa_q3_ta_team_runtime_pickup_team(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_game_entity *other);
qa_q3_game_entity *qa_q3_ta_team_runtime_get_location(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity);
/* Malloc'd message (NULL = TS null or OOM distinguished by *out_is_null). */
char *qa_q3_ta_team_runtime_get_location_message(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, double capacity, bool *out_is_null);
void qa_q3_ta_team_runtime_teamplay_info_message(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_ta_team_runtime_check_team_status(qa_q3_ta_team_runtime *runtime);
/* TS readonly obelisk callback fields, as callback values bound to runtime. */
qa_q3_entity_callback qa_q3_ta_team_runtime_obelisk_regen(qa_q3_ta_team_runtime *runtime);
qa_q3_entity_callback qa_q3_ta_team_runtime_obelisk_respawn(qa_q3_ta_team_runtime *runtime);
qa_q3_entity_callback qa_q3_ta_team_runtime_obelisk_die(qa_q3_ta_team_runtime *runtime);
qa_q3_entity_callback qa_q3_ta_team_runtime_obelisk_touch(qa_q3_ta_team_runtime *runtime);
qa_q3_entity_callback qa_q3_ta_team_runtime_obelisk_pain(qa_q3_ta_team_runtime *runtime);
qa_q3_game_entity *qa_q3_ta_team_runtime_spawn_obelisk(qa_q3_ta_team_runtime *runtime, qa_vec3 origin, qa_q3_base_team team, double spawnflags);
void qa_q3_ta_team_runtime_spawn_team_obelisk(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_base_team team);
void qa_q3_ta_team_runtime_spawn_neutral_obelisk(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity);
bool qa_q3_ta_team_runtime_check_obelisk_attack(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *obelisk, qa_q3_game_entity *attacker);
void qa_q3_ta_team_runtime_bind_save_callbacks(qa_q3_ta_team_runtime *runtime);
#endif
