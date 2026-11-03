/* Port of src/content/q3/base/game/save-values.ts. GPL-2.0-or-later.
 * Value structs are the TS Pick<> sets; string fields are owned copies
 * (free with *_destroy). */
#ifndef QA_CONTENT_Q3_BASE_GAME_SAVE_VALUES_H
#define QA_CONTENT_Q3_BASE_GAME_SAVE_VALUES_H
#include <stdbool.h>
#include "qa/content/q3/base/game/level.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/persistence/value.h"
typedef struct qa_q3_entity_values {
  double spawnflags; bool never_free; double flags; char *model, *model2; double freetime, event_time;
  bool free_after_event, unlink_after_event, physics_object; double physics_bounce, clipmask, mover_state;
  double sound_pos1, sound_1to2, sound_2to1, sound_pos2, sound_loop; qa_vec3 pos1, pos2; char *message;
  double timestamp, angle; char *target, *targetname, *team, *target_shader_name, *target_shader_new_name;
  double speed; qa_vec3 movedir; double pain_debounce_time, fly_sound_debounce_time, last_move_time;
  double damage, splash_damage, splash_radius, method_of_death, splash_method_of_death, count;
  double kamikaze_time, kamikaze_shock_time, watertype, waterlevel, noise_index, wait, random;
} qa_q3_entity_values;
qa_q3_entity_values qa_q3_capture_entity_values(qa_q3_game_entity *source);
void qa_q3_entity_values_destroy(qa_q3_entity_values *values);
void qa_q3_restore_entity_values(qa_q3_game_entity *target, qa_q3_entity_values *state);
typedef struct qa_q3_client_values {
  bool ready_to_exit, noclip; double last_cmd_time, buttons, old_buttons, latched_buttons; qa_vec3 old_origin;
  double damage_armor, damage_blood, damage_knockback; qa_vec3 damage_from; bool damage_from_world;
  double accurate_count, accuracy_shots, accuracy_hits, last_killed_client, last_hurt_client, last_hurt_mod;
  double respawn_time, inactivity_time; bool inactivity_warning; double reward_time, air_out_time, last_kill_time;
  bool fire_held; double switch_team_time, time_residual, portal_id, invulnerability_time;
} qa_q3_client_values;
qa_q3_client_values qa_q3_capture_client_values(qa_q3_game_client *source);
void qa_q3_restore_client_values(qa_q3_game_client *target, qa_q3_client_values state);
/* TS PlayerValues = Pick<PlayerState, ...> in TS field order. */
typedef struct qa_q3_player_values {
  double command_time, pm_type, bob_cycle, pm_flags, pm_time, weapon_time, gravity, speed; qa_vec3 delta_angles;
  double ground_entity_num, legs_timer, legs_anim, torso_timer, torso_anim, movement_dir; qa_vec3 grapple_point;
  double e_flags, event_sequence, external_event, external_event_parm, external_event_time, client_num, weapon, weapon_state;
  qa_vec3 viewangles; double viewheight, damage_event, damage_yaw, damage_pitch, damage_count, generic1, loop_sound;
  double jumppad_ent, ping, pmove_framecount, jumppad_frame, entity_event_sequence;
} qa_q3_player_values;
qa_q3_player_values qa_q3_capture_player_values(qa_q3_base_player_state *source);
void qa_q3_restore_player_values(qa_q3_base_player_state *target, qa_q3_player_values state);
typedef struct qa_q3_persistant_values {
  double connected; bool local_client, initial_spawn, predict_item_pickup, pmove_fixed; char *netname;
  double max_health, enter_time, vote_count, team_vote_count; bool team_info;
} qa_q3_persistant_values;
qa_q3_persistant_values qa_q3_capture_persistant_values(qa_q3_client_persistant *source);
void qa_q3_persistant_values_destroy(qa_q3_persistant_values *values);
void qa_q3_restore_persistant_values(qa_q3_client_persistant *target, qa_q3_persistant_values *state);
typedef qa_q3_player_team_state qa_q3_team_values;
qa_q3_team_values qa_q3_capture_team_values(qa_q3_player_team_state *source);
void qa_q3_restore_team_values(qa_q3_player_team_state *target, qa_q3_team_values state);
typedef qa_q3_client_session qa_q3_session_values;
qa_q3_session_values qa_q3_capture_session_values(qa_q3_client_session *source);
void qa_q3_restore_session_values(qa_q3_client_session *target, qa_q3_session_values state);
typedef struct qa_q3_network_values {
  double number, e_type, e_flags, time, time2; qa_vec3 origin, origin2, angles, angles2;
  double other_entity_num, other_entity_num2, ground_entity_num, constant_light, loop_sound;
  double modelindex, modelindex2, client_num, frame, solid, event, event_parm, powerups, weapon;
  double legs_anim, torso_anim, generic1;
} qa_q3_network_values;
qa_q3_network_values qa_q3_capture_network_values(qa_q3_entity_state_record *source);
void qa_q3_restore_network_values(qa_q3_entity_state_record *target, qa_q3_network_values state);
typedef struct qa_q3_level_values {
  double time, start_time, warmup_time, warmup_modification_count; bool restarted;
  double num_connected_clients, num_non_spectator_clients, num_playing_clients, num_voting_clients;
  double follow1, follow2, intermission_time, intermission_queued; qa_vec3 intermission_origin, intermission_angle;
  char *changemap; bool ready_to_exit; double exit_time, frame_num, previous_time; bool new_session; double fry_sound;
} qa_q3_level_values;
qa_q3_level_values qa_q3_capture_level_values(qa_q3_game_level *source);
void qa_q3_level_values_destroy(qa_q3_level_values *values);
void qa_q3_restore_level_values(qa_q3_game_level *target, qa_q3_level_values *state);
/* SaveReader-backed reads (TS read*Values + readVector). */
qa_vec3 qa_q3_read_vector(const qa_save_reader *reader);
qa_q3_entity_values qa_q3_read_entity_values(const qa_save_reader *reader);
qa_q3_client_values qa_q3_read_client_values(const qa_save_reader *reader);
qa_q3_player_values qa_q3_read_player_values(const qa_save_reader *reader);
qa_q3_persistant_values qa_q3_read_persistant_values(const qa_save_reader *reader);
qa_q3_team_values qa_q3_read_team_values(const qa_save_reader *reader);
qa_q3_session_values qa_q3_read_session_values(const qa_save_reader *reader);
qa_q3_network_values qa_q3_read_network_values(const qa_save_reader *reader);
qa_q3_level_values qa_q3_read_level_values(const qa_save_reader *reader);
#endif
