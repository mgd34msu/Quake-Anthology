/* Port of src/content/q3/presentation/hud-corners.ts. GPL-2.0-or-later.
 * Corner HUD fields from id Software's cg_draw.c. */
#ifndef QA_CONTENT_Q3_PRESENTATION_HUD_CORNERS_H
#define QA_CONTENT_Q3_PRESENTATION_HUD_CORNERS_H
#include <stdbool.h>
#include "qa/content/q3/presentation/draw_icons.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/core/cvars/index.h"
typedef enum qa_q3_pres_hud_corner_cvar {
  QA_Q3_PRES_CORNER_CVAR_TEAM_OVERLAY, QA_Q3_PRES_CORNER_CVAR_SNAPSHOT, QA_Q3_PRES_CORNER_CVAR_FPS,
  QA_Q3_PRES_CORNER_CVAR_TIMER, QA_Q3_PRES_CORNER_CVAR_ATTACKER, QA_Q3_PRES_CORNER_CVAR_TEAM_CHAT_HEIGHT,
  QA_Q3_PRES_CORNER_CVAR_TEAM_CHAT_TIME
} qa_q3_pres_hud_corner_cvar;
const char *qa_q3_pres_hud_corner_cvar_name(qa_q3_pres_hud_corner_cvar cvar);
typedef struct qa_q3_pres_hud_corners_host {
  void *ctx;
  qa_cvar_snapshot (*read_vm_cvar)(void *ctx, qa_q3_pres_hud_corner_cvar name);
  const char *(*config_string)(void *ctx, double index);
  double (*milliseconds)(void *ctx);
  /* Media picks (number shaders / item icons / fonts live in tools). */
  const qa_q3_pres_scene_shader *(*number_shader)(void *ctx, double index);
  const qa_q3_pres_scene_shader *(*item_icon)(void *ctx, double powerup);
  const qa_q3_pres_scene_shader *(*holdable_icon)(void *ctx, double holdable);
} qa_q3_pres_hud_corners_host;
typedef struct qa_q3_pres_client_hud_corners {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_game_static_state *static_state;
  qa_q3_pres_client_draw_icons *icons;
  qa_q3_pres_hud_corners_host host;
  double fps_frames[4];
  double fps_previous_times[4];
  double fps_previous;
  double fps_index;
} qa_q3_pres_client_hud_corners;
/* Product mismatch aborts with the TS Error text (base variant). */
void qa_q3_pres_client_hud_corners_init(qa_q3_pres_client_hud_corners *corners,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state,
    qa_q3_pres_client_draw_icons *icons, qa_q3_pres_hud_corners_host host);
/* TS drawField: digits drawn right-aligned in a field of `digits`. */
void qa_q3_pres_client_hud_corners_draw_field(qa_q3_pres_client_hud_corners *corners, double x, double y,
                                              double width, double value, double digits);
void qa_q3_pres_client_hud_corners_draw_upper_right(qa_q3_pres_client_hud_corners *corners);
void qa_q3_pres_client_hud_corners_draw_lower_right(qa_q3_pres_client_hud_corners *corners);
void qa_q3_pres_client_hud_corners_draw_lower_left(qa_q3_pres_client_hud_corners *corners);
void qa_q3_pres_client_hud_corners_draw_team_info(qa_q3_pres_client_hud_corners *corners);
#endif
