/* Port of src/content/q3/presentation/mission-hud.ts. GPL-2.0-or-later.
 * Team Arena menu HUD owner: menu registration, chat/order state,
 * score feeders and timed-menu painting around the landed UI runtime
 * (supplied through the host as paint/frame callbacks). */
#ifndef QA_CONTENT_Q3_PRESENTATION_MISSION_HUD_H
#define QA_CONTENT_Q3_PRESENTATION_MISSION_HUD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/presentation/mission_owner_draw.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/math.h"
typedef enum qa_q3_pres_mission_score_feeder {
  QA_Q3_PRES_FEEDER_RED = 5, QA_Q3_PRES_FEEDER_BLUE = 6, QA_Q3_PRES_FEEDER_SCOREBOARD = 11
} qa_q3_pres_mission_score_feeder;
/* TS MissionHudHost, collapsed to the callbacks the owner state uses:
 * menu loading/painting and event handling go through the host's
 * landed UiRuntime; this module owns chat/team/orders/feeder state. */
typedef struct qa_q3_pres_mission_hud_host {
  void *ctx;
  const char *(*config_string)(void *ctx, double index);
  void (*print)(void *ctx, const char *text);
  void (*send_client_command)(void *ctx, const char *text);
  bool (*load_menus)(void *ctx, const char *path);
  void (*reset_menus)(void *ctx);
  bool (*menu_scoreboard_open)(void *ctx);
  void (*paint_all)(void *ctx, double time);
  void (*close_by_name)(void *ctx, const char *name);
  void (*show_menu_by_name)(void *ctx, const char *name);
  double (*milliseconds)(void *ctx);
} qa_q3_pres_mission_hud_host;
typedef struct qa_q3_pres_mission_hud {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_game_static_state *static_state;
  qa_q3_pres_mission_owner_draw *owner_draw;
  qa_q3_pres_mission_hud_host host;
  bool menus_loaded;
  double score_selection[3]; /* indexed by feeder slot */
  double menu_open_time;
} qa_q3_pres_mission_hud;
void qa_q3_pres_mission_hud_init(qa_q3_pres_mission_hud *hud, qa_q3_pres_client_game_state *state,
    qa_q3_pres_client_game_static_state *static_state, qa_q3_pres_mission_owner_draw *owner_draw,
    qa_q3_pres_mission_hud_host host);
void qa_q3_pres_mission_hud_reset_strings(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_reset_menus(qa_q3_pres_mission_hud *hud);
/* False only on OOM in the host menu load. */
bool qa_q3_pres_mission_hud_load_menus(qa_q3_pres_mission_hud *hud, const char *path);
void qa_q3_pres_mission_hud_dispose(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_init_team_chat(qa_q3_pres_mission_hud *hud);
/* False only on OOM storing the chat line. */
bool qa_q3_pres_mission_hud_chat(qa_q3_pres_mission_hud *hud, const char *text);
void qa_q3_pres_mission_hud_check_order_pending(qa_q3_pres_mission_hud *hud);
double qa_q3_pres_mission_hud_get_selected_player(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_select_next_player(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_select_previous_player(qa_q3_pres_mission_hud *hud);
double qa_q3_pres_mission_hud_feeder_count(qa_q3_pres_mission_hud *hud, double feeder);
bool qa_q3_pres_mission_hud_feeder_item(qa_q3_pres_mission_hud *hud, double feeder, double index,
                                        char *text, size_t text_size);
double qa_q3_pres_mission_hud_feeder_selection(qa_q3_pres_mission_hud *hud, double feeder);
void qa_q3_pres_mission_hud_set_score_selection(qa_q3_pres_mission_hud *hud, double feeder, double index);
void qa_q3_pres_mission_hud_clear_scoreboard(qa_q3_pres_mission_hud *hud);
bool qa_q3_pres_mission_hud_menu_scoreboard(qa_q3_pres_mission_hud *hud);
bool qa_q3_pres_mission_hud_scroll_feeder(qa_q3_pres_mission_hud *hud, double feeder, bool down);
void qa_q3_pres_mission_hud_frame(qa_q3_pres_mission_hud *hud, double time);
void qa_q3_pres_mission_hud_paint_all(qa_q3_pres_mission_hud *hud);
bool qa_q3_pres_mission_hud_draw_scoreboard(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_draw_timed_menus(qa_q3_pres_mission_hud *hud);
double qa_q3_pres_mission_hud_client_num_from_name(qa_q3_pres_mission_hud *hud, const char *name);
void qa_q3_pres_mission_hud_hide_team_menu(qa_q3_pres_mission_hud *hud);
void qa_q3_pres_mission_hud_show_team_menu(qa_q3_pres_mission_hud *hud);
bool qa_q3_pres_mission_hud_other_team_has_flag(qa_q3_pres_mission_hud *hud);
bool qa_q3_pres_mission_hud_your_team_has_flag(qa_q3_pres_mission_hud *hud);
#endif
