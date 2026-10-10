#ifndef QA_GAME_Q3_CLIENT_TYPES_H
#define QA_GAME_Q3_CLIENT_TYPES_H

#include "qa/network_q3.h"
#include "qa/math.h"
#include "qa/collision.h"

#define QA_Q3_NATIVE_CLIENTS 64u
#define QA_Q3_NATIVE_NETNAME 36u

typedef enum qa_q3_client_connection {
    QA_Q3_CLIENT_DISCONNECTED,
    QA_Q3_CLIENT_CONNECTING,
    QA_Q3_CLIENT_CONNECTED
} qa_q3_client_connection;
enum {
    QA_Q3_SPECTATOR_NOT, QA_Q3_SPECTATOR_FREE,
    QA_Q3_SPECTATOR_FOLLOW, QA_Q3_SPECTATOR_SCOREBOARD
};

typedef struct qa_q3_client_session {
    int32_t team, spectator_time_ms, spectator_state, spectator_client;
    int32_t wins, losses, team_leader;
} qa_q3_client_session;
enum {
    QA_Q3_CLIENT_SESSION_TEAM = 1u << 0,
    QA_Q3_CLIENT_SESSION_TIME = 1u << 1,
    QA_Q3_CLIENT_SESSION_STATE = 1u << 2,
    QA_Q3_CLIENT_SESSION_CLIENT = 1u << 3,
    QA_Q3_CLIENT_SESSION_WINS = 1u << 4,
    QA_Q3_CLIENT_SESSION_LOSSES = 1u << 5,
    QA_Q3_CLIENT_SESSION_LEADER = 1u << 6,
    QA_Q3_CLIENT_SESSION_ALL = 127u
};

typedef struct qa_q3_source_client_counts {
    int32_t num_connected, num_non_spectator, num_playing, num_voting;
    int32_t num_team_voting[2];
    int32_t follow1, follow2;
    uint32_t sorted_clients[QA_Q3_NATIVE_CLIENTS];
} qa_q3_source_client_counts;

typedef struct qa_q3_source_player_team_state {
    int32_t captures, base_defense, carrier_defense, flag_recovery, frag_carrier, assists;
    float last_hurt_carrier_ms, last_returned_flag_ms, flag_since_ms, last_fragged_carrier_ms;
} qa_q3_source_player_team_state;
typedef struct qa_q3_bot_player_state {
    bool present, has_player;
    int32_t pm_type, score, last_hurt_client, last_hurt_mod;
} qa_q3_bot_player_state;

/* The source pers.cmd is copied from the accepted engine command. Session,
 * movement commandTime, inventory and combat retain their actual owners. */
typedef struct qa_q3_client_rule_tail {
    qa_q3_client_connection connected;
    qa_q3_usercmd command;
    qa_q3_client_session session;
    qa_q3_source_player_team_state team;
    int32_t retired_score;
    qa_shape_kind source_model_shape;
    int32_t max_health, enter_time_ms, team_state, team_location;
    int32_t switch_team_time_ms;
    int32_t vote_count, team_vote_count;
    uint32_t old_buttons, buttons, latched_buttons;
    int32_t inactivity_time_ms;
    qa_vec3 old_origin;
    qa_q3_player followed_player;
    bool local_client, initial_spawn, predict_item_pickup, pmove_fixed, team_info;
    bool ready_to_exit;
    bool inactivity_warning, has_followed_player;
} qa_q3_client_rule_tail;

/* Stack/checkpoint projection; current identity belongs to the actor page. */
typedef struct qa_q3_native_client {
    qa_q3_client_rule_tail rule;
    int32_t ping;
    char netname[QA_Q3_NATIVE_NETNAME];
} qa_q3_native_client;

#endif
