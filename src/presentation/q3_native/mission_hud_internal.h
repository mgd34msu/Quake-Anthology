#ifndef QA_Q3_NATIVE_MISSION_HUD_INTERNAL_H
#define QA_Q3_NATIVE_MISSION_HUD_INTERNAL_H
#define CGAME 1
#define MISSIONPACK 1
#include "mission_hud.h"
#include "hud_internal.h"
#include "authored_menu_context.h"
struct q3n_mission_hud {
    q3n_mission_hud_options options;
    const qa_q3_game *source_game;
    uint64_t publication_generation, map_revision;
    q3n_hud *hud;
    q3menu_context *menus;
    displayContextDef_t display;
    const qa_font *font_holders[3];
    const q3n_frame *frame;
    const q3n_command_state *commands;
    q3n_hud_settings settings;
    q3n_hud_draw draw;
    char system_chat[256], team_chat[2][256];
    int32_t selected_score, cursor_x, cursor_y, active_cursor, event_handling, voice_time;
    int32_t order_time, current_order;
    bool order_pending, busy, loaded, text_policy_active;
    int32_t scoreboard_menu, captured_menu;
    int32_t spectator_offset, spectator_time, spectator_paint_x, spectator_paint_x2;
    float spectator_width;
    int32_t spectator_length;
};
q3n_mission_hud *q3nm_active(void);
bool q3nm_current(q3n_mission_hud *, const q3n_frame *, qa_error *);
bool q3nm_begin(q3n_mission_hud *, const q3n_frame *, qa_error *, q3menu_context **);
bool q3nm_end(q3n_mission_hud *, q3menu_context *, bool);
void q3nm_result(bool);
int32_t q3nm_integer(q3n_mission_hud *, const char *);
float q3nm_number(q3n_mission_hud *, const char *);
bool q3nm_cvar(q3n_mission_hud *, const char *, qa_native_q3_client_cvar *, qa_error *);
bool q3nm_integer_set(q3n_mission_hud *, const char *, int32_t, qa_error *);
bool q3nm_console(q3n_mission_hud *, const char *, qa_error *);
const qa_q3_player *q3nm_player(const q3n_mission_hud *);
const qa_q3_player *q3nm_require_player(q3n_mission_hud *);
bool q3nm_set(q3n_mission_hud *, const char *, const char *);
int q3nm_width(const char *, float, int);
int q3nm_height(const char *, float, int);
void q3nm_text(float, float, float, float color[4], const char *, float, int, int);
float q3nm_limit(const char *, float, float, float, const float[4], float, int);
void q3nm_owner(float, float, float, float, float, float, int, int, int, float, float, float[4], int, int);
qboolean q3nm_visible(int);
float q3nm_value(int);
int q3nm_owner_width(int, float);
int q3nm_selected(q3n_mission_hud *);
int q3nm_status(q3n_mission_hud *, int);
const q3n_client_info *q3nm_client(q3n_mission_hud *, int);
const char *q3nm_location(q3n_mission_hud *, int);
void q3nm_font_record(const fontInfo_t *, qa_q3_font_record *);
void q3nm_font_import(const qa_q3_font_record *, fontInfo_t *);
bool q3nm_preferences(q3n_mission_hud *);
#endif
