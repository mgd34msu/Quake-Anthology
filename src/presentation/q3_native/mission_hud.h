#ifndef QA_Q3_NATIVE_MISSION_HUD_H
#define QA_Q3_NATIVE_MISSION_HUD_H
#include "hud.h"
#include "qa/font.h"
typedef struct q3n_mission_hud q3n_mission_hud;
typedef struct q3n_mission_hud_options {
    qa_application *application;
    const qa_application_native_q3_presentation *source;
    qa_native_q3_client_service *client;
    qa_native_q3_wire_reader *reader;
    qa_application_q3_client_context recipient;
    qa_vfs *content;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    qa_font_library *fonts;
    uint32_t seat;
    void *context;
    int32_t (*milliseconds)(void *);
    void (*print)(void *, const char *);
    bool (*key_catcher)(void *, int32_t, qa_error *);
} q3n_mission_hud_options;
bool q3n_mission_hud_create(const q3n_mission_hud_options *, q3n_mission_hud **, qa_error *);
bool q3n_mission_hud_create_restored(const q3n_mission_hud_options *, q3n_mission_hud **, qa_error *);
void q3n_mission_hud_destroy(q3n_mission_hud *);
bool q3n_mission_hud_idle(const q3n_mission_hud *);
bool q3n_mission_hud_bind(q3n_mission_hud *, q3n_hud *, qa_error *);
bool q3n_mission_hud_initialize(q3n_mission_hud *, const q3n_frame *, q3n_command_init_stage, qa_error *);
bool q3n_mission_hud_paint(q3n_mission_hud *, const q3n_frame *, bool scoreboard, bool first_time, qa_error *);
bool q3n_mission_hud_timed(q3n_mission_hud *, const q3n_frame *, qa_error *);
bool q3n_mission_hud_text(q3n_mission_hud *, const q3n_frame *, const char *, float y, float scale,
    const float color[4], int32_t style, bool integer_half_width, qa_error *);
bool q3n_mission_hud_center_line(q3n_mission_hud *, const q3n_frame *, const char *, float y,
    const float color[4], float *height, qa_error *);
bool q3n_mission_hud_response(q3n_mission_hud *, const q3n_frame *, const q3n_command_state *, qa_error *);
bool q3n_mission_hud_score_selection(q3n_mission_hud *, const q3n_frame *, const q3n_command_state *, qa_error *);
bool q3n_mission_hud_message(q3n_mission_hud *, const q3n_frame *, int32_t type, const char *, qa_error *);
bool q3n_mission_hud_key(q3n_mission_hud *, const q3n_frame *, int32_t key, bool down, qa_error *);
bool q3n_mission_hud_mouse(q3n_mission_hud *, const q3n_frame *, int32_t dx, int32_t dy, qa_error *);
bool q3n_mission_hud_event(q3n_mission_hud *, const q3n_frame *, int32_t type, qa_error *);
bool q3n_mission_hud_team_menu(q3n_mission_hud *, const q3n_frame *, bool show, qa_error *);
bool q3n_mission_hud_client_number(q3n_mission_hud *, const q3n_frame *, const char *, int32_t *, qa_error *);
bool q3n_mission_hud_menu_buffer(q3n_mission_hud *, const q3n_frame *, const char *, qa_buffer *, bool *found, qa_error *);
bool q3n_mission_hud_load_menus(q3n_mission_hud *, const q3n_frame *, const char *, qa_error *);
bool q3n_mission_hud_reset(q3n_mission_hud *, const q3n_frame *, bool strings, qa_error *);
bool q3n_mission_hud_select(q3n_mission_hud *, const q3n_frame *, bool next, qa_error *);
bool q3n_mission_hud_next_order(q3n_mission_hud *, const q3n_frame *, qa_error *);
bool q3n_mission_hud_check_order(q3n_mission_hud *, const q3n_frame *, qa_error *);
bool q3n_mission_hud_scroll(q3n_mission_hud *, const q3n_frame *, bool down, qa_error *);
bool q3n_mission_hud_checkpoint(const q3n_mission_hud *, qa_buffer *, qa_error *);
bool q3n_mission_hud_restore(q3n_mission_hud *, qa_bytes, qa_error *);
#endif
