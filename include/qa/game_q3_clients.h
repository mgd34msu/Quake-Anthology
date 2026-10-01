#ifndef QA_GAME_Q3_CLIENTS_H
#define QA_GAME_Q3_CLIENTS_H

#include "qa/game_q3.h"

bool qa_q3_client_team_state_read(const qa_q3_game *, uint32_t,
                                  qa_q3_source_player_team_state *, qa_error *);
bool qa_q3_client_team_state_write(qa_q3_game *, uint32_t,
                                   const qa_q3_source_player_team_state *, qa_error *);
bool qa_q3_client_bot_state_read(const qa_q3_game *, uint32_t,
                                 qa_q3_bot_player_state *, qa_error *);
#include "qa/game_q3_client_types.h"
#include "qa/game_q3_source.h"

/* Slot reads include the stable disconnected source records. Actor reads and
 * mutations require the actual native client slot and full canonical actor. */
bool qa_q3_client_slot_read(const qa_q3_game *, uint32_t,
                           qa_q3_native_client *, qa_error *);
bool qa_q3_client_read(const qa_q3_game *, qa_actor_id,
                      qa_q3_native_client *, qa_error *);
bool qa_q3_client_session_slot_read(const qa_q3_game *, uint32_t,
                                    qa_q3_client_session *, qa_error *);
bool qa_q3_client_session_read(const qa_q3_game *, qa_actor_id,
                               qa_q3_client_session *, qa_error *);
bool qa_q3_client_session_slot_write(qa_q3_game *, uint32_t, uint32_t field_mask,
                                     const qa_q3_client_session *, qa_error *);
bool qa_q3_client_connect(qa_q3_game *, qa_actor_id, bool bot, qa_error *);
/* ClientBegin's source client state writes, before ordinary ClientSpawn. The
 * application owner performs its entity reset, spawn, command and outputs. */
bool qa_q3_client_begin_state(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_client_disconnect(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_client_disconnect_items(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_client_copy_body_queue(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_client_toss_cube(qa_q3_game *, qa_actor_id, int32_t source_team,
                           int32_t timeout_seconds, qa_actor_id *, qa_error *);
bool qa_q3_client_spectator(qa_q3_game *, qa_actor_id, bool, qa_error *);
/* After the real session and PM_FOLLOW writes, clear the native BOT flag and
 * restore this source player's clientNum to its fixed physical client slot. */
bool qa_q3_client_stop_following_state(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_client_slot_stop_following_state(qa_q3_game *, uint32_t, qa_error *);
bool qa_q3_client_player_flags_update(qa_q3_game *, uint32_t,
                                     uint32_t set_bits, uint32_t clear_bits, qa_error *);
bool qa_q3_client_command(qa_q3_game *, qa_actor_id,
                         const qa_q3_usercmd *, qa_error *);
bool qa_q3_client_received_command(qa_q3_game *, qa_actor_id,
                                   const qa_q3_usercmd *, qa_error *);
bool qa_q3_client_buttons(qa_q3_game *, qa_actor_id, uint32_t, bool latch,
                          uint32_t *old_buttons, qa_error *);
bool qa_q3_client_consume_gesture(qa_q3_game *, qa_actor_id, bool *, qa_error *);
bool qa_q3_client_flag_powerup(qa_q3_game *, qa_actor_id, uint32_t tag,
                              int32_t value, qa_error *);
bool qa_q3_client_team_location(qa_q3_game *, uint32_t source_slot, int32_t, qa_error *);
bool qa_q3_client_team_state(qa_q3_game *, qa_actor_id, int32_t, qa_error *);
bool qa_q3_client_ready(qa_q3_game *, qa_actor_id, bool, qa_error *);
bool qa_q3_client_rank(qa_q3_game *, uint32_t source_slot, int32_t, qa_error *);
bool qa_q3_client_persistent_team(qa_q3_game *, uint32_t source_slot, int32_t, qa_error *);
bool qa_q3_client_tokens_read(const qa_q3_game *, qa_actor_id, int32_t *, qa_error *);
bool qa_q3_client_tokens_write(qa_q3_game *, qa_actor_id, int32_t, qa_error *);
bool qa_q3_client_inactivity_read(const qa_q3_game *, qa_actor_id, int32_t *, bool *, qa_error *);
bool qa_q3_client_inactivity_write(qa_q3_game *, qa_actor_id, int32_t, bool, qa_error *);
bool qa_q3_client_follow_read(const qa_q3_game *, uint32_t, qa_q3_player *, bool *, qa_error *);
bool qa_q3_client_follow_copy(qa_q3_game *, qa_actor_id, const qa_q3_player *, qa_error *);
bool qa_q3_client_follow_clear(qa_q3_game *, uint32_t, qa_error *);
bool qa_q3_client_follow_scoreboard(qa_q3_game *, qa_actor_id, bool, qa_error *);
bool qa_q3_client_think_prepare(qa_q3_game *, qa_actor_id, const qa_q3_usercmd *,
                              uint32_t *, qa_q3_usercmd *, qa_error *);
bool qa_q3_client_think_event_time(qa_q3_game *, qa_actor_id, uint32_t, qa_error *);
bool qa_q3_client_fire_held_finish(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_client_reward_expire(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_client_events(qa_q3_game *, qa_actor_id, uint32_t old_event_sequence, qa_error *);
bool qa_q3_client_think_finish(qa_q3_game *, qa_actor_id, int32_t msec, qa_error *);
bool qa_q3_client_movement_complete(qa_q3_game *, qa_actor_id, int32_t, qa_vec3,
                                    float, qa_movement_ground, qa_error *);
bool qa_q3_client_movement_water(qa_q3_game *, qa_actor_id, int32_t, int32_t, qa_error *);
bool qa_q3_client_spectator_origin(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_client_current_origin(qa_q3_game *, qa_actor_id, qa_vec3, qa_error *);
bool qa_q3_client_current_origin_finish(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_client_award(qa_q3_game *, qa_actor_id, qa_q3_source_award, int32_t, qa_error *);
bool qa_q3_client_move_intermission(qa_q3_game *, qa_actor_id, qa_vec3, qa_vec3, qa_error *);
bool qa_q3_client_score_reset(qa_q3_game *, uint32_t, qa_error *);
bool qa_q3_client_connecting(qa_q3_game *, uint32_t, qa_error *);
bool qa_q3_client_touch_policy(const qa_q3_game *, qa_actor_id, bool *, bool *, bool *, qa_error *);
bool qa_q3_client_jumppad_finish(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_client_world_effects(qa_q3_game *, qa_actor_id, int32_t, int32_t, qa_error *);
bool qa_q3_client_movement_water_read(const qa_q3_game *, qa_actor_id, int32_t *, int32_t *, qa_error *);
bool qa_q3_client_end_prepare(qa_q3_game *, qa_actor_id, int32_t, int32_t, bool *, qa_error *);
bool qa_q3_client_command_time(const qa_q3_game *, qa_actor_id, int32_t *, qa_error *);
bool qa_q3_client_think_complete(qa_q3_game *, qa_actor_id, int32_t command_time_ms, qa_error *);
bool qa_q3_client_teleport_event(qa_q3_game *, qa_actor_id, bool entering, qa_error *);
bool qa_q3_client_ping(qa_q3_game *, qa_actor_id, int32_t, qa_error *);
bool qa_q3_client_initial_spawn(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_client_team_switch_time(qa_q3_game *, qa_actor_id, int32_t, qa_error *);
bool qa_q3_client_server_flags(const qa_q3_game *, uint32_t, uint32_t *, qa_error *);
bool qa_q3_client_set_server_flags(qa_q3_game *, qa_actor_id, uint32_t, qa_error *);
/* G_AddBot activates the real fixed source row before raw userinfo/Connect. */
bool qa_q3_client_activate_bot(qa_q3_game *, qa_actor_id, qa_error *);
typedef struct qa_q3_client_taunt {
    qa_actor_id enemy;
    uint32_t enemy_source_slot;
    bool enemy_source_present;
    int32_t last_killed_client, last_hurt_client, last_hurt_mod, reward_time_ms;
} qa_q3_client_taunt;
bool qa_q3_client_taunt_read(const qa_q3_game *, uint32_t, qa_q3_client_taunt *, qa_error *);
bool qa_q3_client_taunt_enemy_clear(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_client_taunt_kill_clear(qa_q3_game *, qa_actor_id, qa_error *);

/* ClientUserinfoChanged's bounded source read and name cleaner. The returned
 * text is terminated and source byte characters are retained. */
void qa_q3_client_info_value(const char *, const char *, char *, size_t);
void qa_q3_client_clean_name(const char *, char [QA_Q3_NATIVE_NETNAME]);
bool qa_q3_client_userinfo(qa_q3_game *, qa_actor_id, const char *,
                          bool scoreboard, qa_error *);
bool qa_q3_client_slot_userinfo(qa_q3_game *, uint32_t, const char *,
                               bool scoreboard, qa_error *);
/* Selected-source refresh publishes the real name/session presentation without
 * changing handicap, prediction or the physical source's maximum health. */
bool qa_q3_client_selected_presentation(qa_q3_game *, qa_actor_id,
                                        const char *, int32_t game_type, qa_error *);

#endif
