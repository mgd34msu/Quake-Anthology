/* Port of src/content/q3/presentation/state.ts. GPL-2.0-or-later.
 * Owned client state from id Software's code/cgame/cg_local.h.
 * KEYSTONE for the presentation draw wave: ClientGameState (cg_t) and
 * ClientGameStaticState (cgs_t) are typed against the landed
 * base/shared records, not the old base/game/state.h mirrors.
 * String fields are owned char*, NULL is TS ""; assign with
 * qa_q3_pres_set_string(). Sound/shader handles are borrowed, NULL is
 * TS null. snap/next_snap borrow snapshots owned by the caller. */
#ifndef QA_CONTENT_Q3_PRESENTATION_STATE_H
#define QA_CONTENT_Q3_PRESENTATION_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/foundation/player_pose.h"
#include "qa/content/q3/presentation/client_info.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/refdef.h"
#include "qa/content/q3/presentation/retail_snapshot.h"
#include "qa/contracts/math.h"
enum {
  QA_Q3_PRES_MAX_CLIENTS = 64,
  QA_Q3_PRES_MAX_GAME_MODELS = 256,
  QA_Q3_PRES_MAX_GAME_SOUNDS = 256,
  QA_Q3_PRES_MAX_ENTITIES = 1024,
  QA_Q3_PRES_TEAM_CHAT_COUNT = 8,
  QA_Q3_PRES_SOUND_BUFFER_COUNT = 20,
  QA_Q3_PRES_REWARD_COUNT = 10,
  QA_Q3_PRES_SKULL_TRAIL_POSITIONS = 10
};
typedef struct qa_q3_pres_client_score {
  double client, score, ping, time, score_flags, accuracy, impressive_count, excellent_count;
  double guantlet_count, defend_count, assist_count, perfect, captures, team;
} qa_q3_pres_client_score;
typedef struct qa_q3_pres_client_reward {
  qa_pcm_sound *sound;                          /* borrowed, nullable */
  const qa_q3_pres_scene_shader *shader;        /* nullable */
  double count;
} qa_q3_pres_client_reward;
/* TS ClientPlayerEntity = PlayerPoseState + flag pose frame + weapon fx. */
typedef struct qa_q3_pres_client_player_entity {
  qa_q3_player_pose_state pose;
  qa_q3_pose_lerp_frame flag;
  double lightning_firing;
  qa_vec3 railgun_impact;
  bool railgun_flash;
  double barrel_angle, barrel_time;
  bool barrel_spinning;
} qa_q3_pres_client_player_entity;
qa_q3_pres_client_player_entity qa_q3_pres_create_client_player_entity(void);
typedef struct qa_q3_pres_skull_trail {
  qa_vec3 positions[QA_Q3_PRES_SKULL_TRAIL_POSITIONS];
  double num_positions;
} qa_q3_pres_skull_trail;
typedef struct qa_q3_pres_client_entity {
  qa_q3_base_entity_state current_state, next_state;
  bool interpolate, current_valid;
  double muzzle_flash_time, previous_event, teleport_flag, trail_time, dust_trail_time, misc_time, snapshot_time;
  qa_q3_pres_client_player_entity player;
  double error_time;
  qa_vec3 error_origin, error_angles;
  bool extrapolated;
  qa_vec3 raw_origin, raw_angles, beam_end, lerp_origin, lerp_angles;
} qa_q3_pres_client_entity;
void qa_q3_pres_client_entity_init(qa_q3_pres_client_entity *entity);
/* TS ClientEntity[] (solidEntities / triggerEntities): borrowed pointers
 * into the state's entity table; reset by setting count back to 0. */
typedef struct qa_q3_pres_client_entity_list {
  qa_q3_pres_client_entity **items; /* owned array of borrowed pointers */
  size_t count, capacity;
} qa_q3_pres_client_entity_list;
bool qa_q3_pres_client_entity_list_push(qa_q3_pres_client_entity_list *list, qa_q3_pres_client_entity *entity);
void qa_q3_pres_client_entity_list_destroy(qa_q3_pres_client_entity_list *list);

typedef struct qa_q3_pres_client_game_static_state {
  qa_q3_base_product product;
  double server_command_sequence, cursor_x, cursor_y, event_handling;
  const qa_q3_pres_scene_shader *active_cursor; /* nullable */
  char *team_chat_msgs[QA_Q3_PRES_TEAM_CHAT_COUNT]; /* owned, NULL = "" */
  double team_chat_msg_times[QA_Q3_PRES_TEAM_CHAT_COUNT];
  double team_chat_pos, team_last_chat_pos, current_voice_client;
  double accept_order_time, accept_task, accept_leader;
  char *accept_voice; /* owned, NULL = "" */
  double current_order;
  bool order_pending;
  double order_time;
  qa_q3_pres_client_info client_info[QA_Q3_PRES_MAX_CLIENTS];
  qa_q3_pres_scene_model game_models[QA_Q3_PRES_MAX_GAME_MODELS];
  qa_pcm_sound *game_sounds[QA_Q3_PRES_MAX_GAME_SOUNDS]; /* borrowed, nullable */
  qa_q3_base_game_type game_type;
  double dm_flags, team_flags, fraglimit, capturelimit, timelimit, maxclients;
  char *mapname; /* owned, NULL = "" */
  double local_server;
  char *red_team, *blue_team; /* owned, NULL = "" */
  double vote_time, vote_yes, vote_no;
  bool vote_modified;
  char *vote_string; /* owned, NULL = "" */
  double team_vote_time[2], team_vote_yes[2], team_vote_no[2];
  bool team_vote_modified[2];
  char *team_vote_string[2]; /* owned, NULL = "" */
  double level_start_time, scores1, scores2, redflag, blueflag, flag_status;
} qa_q3_pres_client_game_static_state;
void qa_q3_pres_client_game_static_state_init(qa_q3_pres_client_game_static_state *state, qa_q3_base_product product);
void qa_q3_pres_client_game_static_state_destroy(qa_q3_pres_client_game_static_state *state);

typedef struct qa_q3_pres_client_game_state {
  qa_q3_base_product product;
  double client_num, processed_snapshot_num;
  qa_q3_pres_client_entity *entities; /* owned [MAX_ENTITIES]; TS private table */
  qa_q3_pres_client_entity predicted_player_entity;
  qa_q3_pres_skull_trail skull_trails[QA_Q3_PRES_MAX_CLIENTS];
  qa_q3_pres_client_entity_list solid_entities, trigger_entities;
  double num_scores, selected_score, scores_request_time;
  bool show_scores;
  double score_fade_time;
  bool score_board_showing;
  double deferred_player_loading;
  char *center_print; /* owned, NULL = "" */
  double center_print_time, center_print_char_width, center_print_y, center_print_lines;
  double head_start_yaw, head_end_yaw, head_start_pitch, head_end_pitch, head_start_time, head_end_time;
  double voice_time, crosshair_client_num, crosshair_client_time;
  qa_q3_pres_client_score scores[QA_Q3_PRES_MAX_CLIENTS];
  double team_scores[2], num_sorted_team_players, sorted_team_players[8];
  double warmup_count;
  bool level_shot;
  char *info_screen_text; /* owned, NULL = "" */
  double voice_chat_time, voice_chat_buffer_in, voice_chat_buffer_out;
  double sound_buffer_in, sound_buffer_out, sound_time;
  qa_pcm_sound *sound_buffer[QA_Q3_PRES_SOUND_BUFFER_COUNT]; /* borrowed, nullable */
  char *spectator_list; /* owned, NULL = "" */
  double spectator_len, spectator_width, spectator_time, spectator_offset, spectator_paint_x, spectator_paint_x2, spectator_paint_len;
  double latest_snapshot_num, latest_snapshot_time;
  qa_q3_pres_retail_snapshot *snap, *next_snap; /* borrowed, nullable */
  double time, old_time, frame_time, physics_time, frame_interpolation;
  bool this_frame_teleport, next_frame_teleport;
  double client_frame;
  qa_vec3 auto_angles, auto_angles_fast;
  qa_axis auto_axis, auto_axis_fast;
  bool map_restart, hyperspace;
  qa_q3_base_source_player_state predicted_player_state;
  bool valid_pps;
  double predicted_error_time;
  qa_vec3 predicted_error;
  double event_sequence;
  char *killer_name; /* owned, NULL = "" */
  double item_pickup, item_pickup_time, item_pickup_blend_time;
  double weapon_select, weapon_select_time;
  double land_change, land_time, step_change, step_time, powerup_active, powerup_time;
  qa_q3_pres_refdef refdef;
  qa_vec3 refdef_view_angles;
  double bob_cycle, xyspeed, bob_frac_sin;
  bool rendering_third_person, test_gun;
  qa_vec3 kick_angles, kick_origin;
  double damage_time, damage_kick_end_time, attacker_time, low_ammo_warning;
  double reward_stack, reward_time;
  qa_q3_pres_client_reward rewards[QA_Q3_PRES_REWARD_COUNT];
  bool intermission_started;
  double warmup, timelimit_warnings, fraglimit_warnings;
  double damage_pitch, damage_roll, damage_x, damage_y, damage_value;
  double duck_change, duck_time;
  bool zoomed;
  double zoom_time, zoom_sensitivity, next_orbit_time;
  char *test_model_name; /* owned, NULL = "" */
  qa_q3_pres_ref_model_entity test_model_entity;
  qa_q3_base_player_state_slots predictable_events;
} qa_q3_pres_client_game_state;
/* TS ClientGameState constructor: invalid client/snapshot numbers abort
 * with the exact TS RangeError text. False only on OOM. */
bool qa_q3_pres_client_game_state_init(qa_q3_pres_client_game_state *state, qa_q3_base_product product,
    double client_num, double processed_snapshot_num);
void qa_q3_pres_client_game_state_destroy(qa_q3_pres_client_game_state *state);
/* TS entityAt(number): out-of-table numbers abort with the TS RangeError. */
qa_q3_pres_client_entity *qa_q3_pres_client_game_state_entity_at(qa_q3_pres_client_game_state *state, double number);
#endif
