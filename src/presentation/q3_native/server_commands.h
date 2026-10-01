#ifndef QA_Q3_NATIVE_SERVER_COMMANDS_H
#define QA_Q3_NATIVE_SERVER_COMMANDS_H

#include "frame.h"
#include "qa/application_native_q3_client.h"
#include "qa/application_native_q3_wire.h"

typedef struct q3n_server_commands q3n_server_commands;
typedef struct q3n_command_score {
    int32_t client, score, ping, time, score_flags, accuracy;
    int32_t impressive, excellent, gauntlet, defend, assist, perfect, captures, team;
} q3n_command_score;
typedef struct q3n_command_state {
    int32_t server_command_sequence, game_type, dm_flags, team_flags;
    int32_t fraglimit, capturelimit, timelimit, max_clients, level_start_time;
    char mapname[64], red_team[64], blue_team[64];
    int32_t scores1, scores2, warmup, warmup_count, team_scores[2], num_scores;
    q3n_command_score scores[64];
    int32_t sorted_team_players[8], num_sorted_team_players;
    int32_t vote_time, vote_yes, vote_no, team_vote_time[2], team_vote_yes[2], team_vote_no[2];
    char vote_string[1024], team_vote_string[2][1024];
    bool vote_modified, team_vote_modified[2];
    int32_t red_flag, blue_flag, flag_status;
    bool intermission_started, map_restart, level_shot;
    char spectator_list[1024];
    int32_t spectator_length;
    float spectator_width;
    char team_chat[8][241];
    int32_t team_chat_times[8], team_chat_position, team_chat_last_position;
    int32_t current_voice_client, accept_order_time, accept_task, accept_leader;
    char accept_voice[32];
} q3n_command_state;

/* The actual native GAME reliable reader claims engine commands and assembles
 * bcs fragments before publishing this borrowed receipt. It retains the real
 * local reader/acknowledgment lease. No network client or snapshot is created. */
typedef struct q3n_server_command_receipt {
    /* The frontend adds its fresh recipient context to this real wire claim. */
    qa_native_q3_wire_receipt wire;
    q3n_remote_command remote;
    qa_application_q3_client_context recipient;
    uint64_t publication_generation, map_revision;
    int32_t sequence;
    bool present;
    const qa_command_tokens *arguments;
} q3n_server_command_receipt;
typedef enum q3n_command_message_kind {
    Q3N_COMMAND_PRINT, Q3N_COMMAND_CHAT, Q3N_COMMAND_TEAM_CHAT, Q3N_COMMAND_VOICE
} q3n_command_message_kind;
typedef struct q3n_command_message {
    q3n_command_message_kind kind;
    const q3n_frame *frame;
    const qa_application_q3_client_context *recipient;
    const char *text, *voice_command;
    /* Only voice has an authored numeric sender. Plain server text never
     * guesses a sender from names or selected role. */
    int32_t sender_client;
    qa_actor_id sender_actor;
    bool sender_present;
} q3n_command_message;
typedef enum q3n_command_init_stage {
    Q3N_INIT_CONSOLE_COMMANDS, Q3N_INIT_COLLISION_MAP, Q3N_INIT_STRING_TABLE,
    Q3N_INIT_PARTICLES, Q3N_INIT_CLIENT_LOADING,
    Q3N_INIT_MISSION_ASSETS, Q3N_INIT_HUD_MENU, Q3N_INIT_TEAM_CHAT
} q3n_command_init_stage;
/* Borrowed child owners, contexts and callbacks survive every active call.
 * Callbacks may request retirement; actual destruction waits for idle. The
 * frontend producer explicitly binds any mixed source notification route. */
typedef struct q3n_server_command_options {
    qa_application *application;
    qa_native_q3_client_service *client;
    /* Same borrowed local reader retained by client services and client-info. */
    qa_native_q3_wire_reader *reader;
    qa_native_q3_remote_client_service *remote_client;
    q3n_remote_source *remote_source;
    qa_application_q3_client_context recipient;
    uint64_t publication_generation, map_revision;
    qa_q3_product product;
    qa_vfs *content;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    q3n_clients *clients;
    q3n_media *media;
    q3n_events *events;
    void *context;
    bool (*current)(void *, const q3n_frame *, const qa_application_q3_client_context *);
    bool (*read_command)(void *, const q3n_frame *, int32_t sequence,
        q3n_server_command_receipt *, qa_error *);
    bool (*receipt_current)(void *, const q3n_frame *, const q3n_server_command_receipt *);
    bool (*message)(void *, const q3n_command_message *, qa_error *);
    bool (*center_print)(void *, const q3n_frame *, const qa_application_q3_client_context *,
        const char *, int32_t y, int32_t width, qa_error *);
    bool (*client_settings)(void *, const q3n_frame *, bool loading, q3n_client_settings *, qa_error *);
    bool (*loading)(void *, const q3n_frame *, const char *, int32_t item_or_minus_one, qa_error *);
    /* Collision stage binds the real source world, returning its actual inline
     * model extent. Other stages leave the extent unchanged. */
    bool (*initialize_stage)(void *, const q3n_frame *, q3n_command_init_stage,
        const char *mapname, int32_t physical_client_or_minus_one,
        uint32_t *inline_models, qa_error *);
    bool (*clear_particles)(void *, const q3n_frame *, qa_error *);
    bool (*score_selection)(void *, const q3n_frame *, const q3n_command_state *, qa_error *);
    bool (*response_head)(void *, const q3n_frame *, const q3n_command_state *, qa_error *);
    int32_t (*memory_remaining)(void *);
} q3n_server_command_options;
bool q3n_server_commands_create(const q3n_server_command_options *, q3n_server_commands **, qa_error *);
bool q3n_server_commands_create_remote(const q3n_server_command_options *, q3n_server_commands **, qa_error *);
void q3n_server_commands_destroy(q3n_server_commands *);
bool q3n_server_commands_idle(const q3n_server_commands *);
const q3n_command_state *q3n_server_commands_state(const q3n_server_commands *);
/* Runs the real donor constructor once. The sequence is the actual producer
 * gamestate baseline, never a guessed zero or a newest-command shortcut. */
bool q3n_server_commands_initialize(q3n_server_commands *, const q3n_frame *,
    int32_t initial_server_command_sequence, qa_error *);
bool q3n_server_commands_execute(q3n_server_commands *, const q3n_frame *,
    int32_t latest_sequence, qa_error *);
/* Dispatch the actual Network receipt already adopted by the snapshot owner.
 * This advances CGAME's distinct command sequence and never reexecutes it. */
bool q3n_server_commands_remote_dispatch(q3n_server_commands *, const q3n_frame *,
    const q3n_server_command_receipt *, qa_error *);
bool q3n_server_commands_voice(q3n_server_commands *, const q3n_frame *, int32_t mode,
    bool voice_only, int32_t client, int32_t color, const char *command, qa_error *);
bool q3n_server_commands_finish(q3n_server_commands *, const q3n_frame *, qa_error *);
bool q3n_server_commands_vote_drawn(q3n_server_commands *, const q3n_frame *,
    int32_t team_or_minus_one, qa_error *);
bool q3n_server_commands_warmup_drawn(q3n_server_commands *, const q3n_frame *,
    int32_t warmup, int32_t count, qa_error *);
bool q3n_server_commands_chat_drawn(q3n_server_commands *, const q3n_frame *,
    int32_t last_position, qa_error *);
bool q3n_server_commands_map_restart_taken(q3n_server_commands *, const q3n_frame *, qa_error *);
/* The console calls scores_clear only for a new request while scores were
 * hidden. An expired order remains unchanged when it is answered. */
bool q3n_server_commands_scores_clear(q3n_server_commands *, const q3n_frame *, qa_error *);
bool q3n_server_commands_spectators_build(q3n_server_commands *, const q3n_frame *, qa_error *);
bool q3n_server_commands_order_answered(q3n_server_commands *, const q3n_frame *, qa_error *);
/* Import binds already restored client/backend/producer owners and only reads
 * numeric sound holders under the aggregate backend capture lease. */
bool q3n_server_commands_checkpoint(const q3n_server_commands *, qa_buffer *, qa_error *);
bool q3n_server_commands_restore(q3n_server_commands *, qa_bytes, qa_error *);

#endif
