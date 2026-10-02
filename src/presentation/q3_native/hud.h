#ifndef QA_Q3_NATIVE_HUD_H
#define QA_Q3_NATIVE_HUD_H
#include "view.h"
#include "server_commands.h"

typedef struct q3n_hud q3n_hud;
typedef struct q3n_hud_settings {
    bool draw_2d, draw_status, draw_icons, draw_3d_icons, draw_rewards;
    bool crosshair_health, draw_crosshair_names, draw_ammo_warning, paused;
    bool draw_snapshot, draw_fps, draw_timer, draw_attacker, lagometer, no_predict, synchronous_clients;
    int32_t crosshair, crosshair_x, crosshair_y, team_overlay, team_chat_height, team_chat_time;
    /* Constructor-cached cgs.localServer, projected by the actual CLIENT. */
    int32_t local_server;
    float crosshair_size, center_time;
} q3n_hud_settings;
typedef struct q3n_hud_options {
    qa_application *application;
    const qa_application_native_q3_presentation *source;
    qa_q3_presentation_assets *assets;
    qa_native_q3_client_service *client;
    qa_native_q3_remote_client_service *remote_client;
    q3n_compiled_source *compiled_source;
    uint32_t seat;
    /* Actual physical compositor ordinal; the authored source seat ID may
     * differ. UI/fonts borrow this frontend seat until parent retirement. */
    uint32_t presentation_seat;
    qa_ui *ui;
    void *context;
    int32_t (*milliseconds)(void *);
    bool (*load_deferred)(void *, const q3n_frame *, qa_error *);
    bool (*client_command)(void *, const q3n_frame *, const char *, qa_error *);
    /* Reads the actual current command number minus CMD_BACKUP plus one,
     * lazily at CG_DrawDisconnect. Missing remote ring rows are errors. */
    bool (*oldest_command)(void *, const q3n_frame *, qa_q3_usercmd *, qa_error *);
    bool (*weapon_warning)(void *, const q3n_frame *, q3n_weapon_hud *, qa_error *);
    /* Team Arena paints its real parsed cgame menus and fonts through their
     * native owner. The base HUD never substitutes an approximate layout. */
    bool (*mission_paint)(void *, const q3n_frame *, bool scoreboard, bool first_time, qa_error *);
    bool (*mission_order)(void *, const q3n_frame *, qa_error *);
    bool (*mission_timed)(void *, const q3n_frame *, qa_error *);
    bool (*mission_text)(void *, const q3n_frame *, const char *, float y, float scale,
        const float color[4], int32_t style, bool integer_half_width, qa_error *);
    bool (*mission_center_line)(void *, const q3n_frame *, const char *, float y,
        const float color[4], float *height, qa_error *);
} q3n_hud_options;
typedef struct q3n_hud_state {
    char center_print[1024];
    int32_t center_print_time, center_print_y, center_print_char_width, center_print_lines;
    int32_t crosshair_client, crosshair_client_time, score_fade_time, deferred_player_loading;
    int32_t prox_time, prox_counter, prox_tick;
    int32_t head_start_time, head_end_time;
    float head_start_yaw, head_end_yaw, head_start_pitch, head_end_pitch;
    bool show_scores, scoreboard_showing, scoreboard_first_time;
} q3n_hud_state;
bool q3n_hud_create(const q3n_hud_options *, q3n_hud **, qa_error *);
bool q3n_hud_create_restored(const q3n_hud_options *, q3n_hud **, qa_error *);
bool q3n_hud_create_remote(const q3n_hud_options *, q3n_hud **, qa_error *);
bool q3n_hud_create_compiled(const q3n_hud_options *, q3n_hud **, qa_error *);
void q3n_hud_destroy(q3n_hud *);
bool q3n_hud_idle(const q3n_hud *);
const q3n_hud_state *q3n_hud_read(const q3n_hud *);
bool q3n_hud_weapon_read(q3n_hud *, const q3n_frame *, q3n_weapon_hud *, qa_error *);
/* Scalar text publication needs the actual initialized CLIENT and entered
 * frame; it does not consume a snapshot or predicted player. */
bool q3n_hud_center_print(q3n_hud *, const q3n_frame *, const char *, int32_t y, int32_t width, qa_error *);
void q3n_hud_scores(q3n_hud *, bool show, int32_t source_time);
/* Stamps the shared scoreboard request clock before reliable output. An
 * entered remote console can request scores before its first snapshot. */
bool q3n_hud_scores_request(q3n_hud *, const q3n_frame *, bool *due, qa_error *);
bool q3n_hud_frame(q3n_hud *, const q3n_frame *, const q3n_hud_settings *,
    q3n_server_commands *, q3n_player_state *, qa_scene_rect viewport, qa_error *);
bool q3n_hud_tile_clear(q3n_hud *, const q3n_frame *, qa_scene_rect viewport, qa_error *);
/* Samples are real client timing receipts. Local direct GAME has no invented
 * packet history; its completed-frame offset is observed as zero. */
void q3n_hud_frame_sample(q3n_hud *, int32_t offset);
void q3n_hud_snapshot_sample(q3n_hud *, bool dropped, int32_t ping, int32_t flags);
void q3n_hud_disconnect_command(q3n_hud *, int32_t oldest_command_time);
void q3n_hud_round(q3n_hud *);
bool q3n_hud_checkpoint(const q3n_hud *, qa_buffer *, qa_error *);
bool q3n_hud_restore(q3n_hud *, qa_bytes, qa_error *);
#endif
