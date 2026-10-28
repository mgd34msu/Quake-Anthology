/* Port of src/content/q3/presentation/hud.ts. GPL-2.0-or-later.
 * HUD composition from id Software's cg_draw.c. */
#ifndef QA_CONTENT_Q3_PRESENTATION_HUD_H
#define QA_CONTENT_Q3_PRESENTATION_HUD_H
#include <stdbool.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/presentation/draw_icons.h"
#include "qa/content/q3/presentation/draw_status.h"
#include "qa/content/q3/presentation/hud_corners.h"
#include "qa/content/q3/presentation/prediction.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/content/q3/presentation/weapons.h"
#include "qa/core/cvars/index.h"
#include "qa/text/q3_font.h"
typedef struct qa_q3_pres_base_scoreboard qa_q3_pres_base_scoreboard;
typedef struct qa_q3_pres_mission_hud qa_q3_pres_mission_hud;
typedef enum qa_q3_pres_hud_cvar {
  QA_Q3_PRES_HUD_CVAR_DRAW_2D, QA_Q3_PRES_HUD_CVAR_DRAW_STATUS, QA_Q3_PRES_HUD_CVAR_DRAW_ICONS,
  QA_Q3_PRES_HUD_CVAR_DRAW_3D_ICONS, QA_Q3_PRES_HUD_CVAR_DRAW_REWARDS, QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR,
  QA_Q3_PRES_HUD_CVAR_CROSSHAIR_HEALTH, QA_Q3_PRES_HUD_CVAR_CROSSHAIR_SIZE, QA_Q3_PRES_HUD_CVAR_CROSSHAIR_X,
  QA_Q3_PRES_HUD_CVAR_CROSSHAIR_Y, QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR_NAMES,
  QA_Q3_PRES_HUD_CVAR_DRAW_AMMO_WARNING, QA_Q3_PRES_HUD_CVAR_PAUSED
} qa_q3_pres_hud_cvar;
const char *qa_q3_pres_hud_cvar_name(qa_q3_pres_hud_cvar cvar);
/* TS ClientHudHost. weapon_hud hooks are the player-state reader pick. */
typedef struct qa_q3_pres_client_hud_host {
  qa_q3_pres_client_draw_icons *icons;
  qa_q3_pres_client_draw_status *status;
  qa_q3_pres_client_hud_corners *corners;
  qa_q3_pres_prediction_runtime *prediction;
  qa_q3_pres_client_weapon_runtime *weapons;
  void *ctx;
  double (*rand)(void *ctx);
  qa_cvar_snapshot (*read_vm_cvar)(void *ctx, qa_q3_pres_hud_cvar name);
  void (*start_local_sound)(void *ctx, qa_pcm_sound *sound, double channel);
  /* Media picks read by the status bar / crosshair / rewards. */
  const qa_q3_pres_scene_shader *(*crosshair_shader)(void *ctx, double index);
  const qa_q3_pres_scene_shader *(*number_shader)(void *ctx, double index);
} qa_q3_pres_client_hud_host;
/* TS ClientHudVariant. */
typedef struct qa_q3_pres_client_hud_variant {
  qa_q3_base_product kind;
  qa_q3_pres_base_scoreboard *scoreboard; /* baseq3 */
  const qa_q3_font_set *fonts; /* missionpack */
  qa_q3_pres_mission_hud *menus; /* missionpack */
  void *menus_ctx;
  bool (*draw_scoreboard)(void *ctx);
  void (*draw_timed_menus)(void *ctx);
} qa_q3_pres_client_hud_variant;
typedef struct qa_q3_pres_client_hud {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_game_static_state *static_state;
  qa_q3_pres_client_hud_host host;
  qa_q3_pres_client_hud_variant variant;
  double prox_time, prox_counter, prox_tick;
} qa_q3_pres_client_hud;
/* Canonical-sharing mismatch aborts with the TS Error text. */
void qa_q3_pres_client_hud_init(qa_q3_pres_client_hud *hud, qa_q3_pres_client_game_state *state,
    qa_q3_pres_client_game_static_state *static_state, qa_q3_pres_client_hud_host host,
    qa_q3_pres_client_hud_variant variant);
void qa_q3_pres_client_hud_draw_2d(qa_q3_pres_client_hud *hud);
void qa_q3_pres_client_hud_draw_status_bar(qa_q3_pres_client_hud *hud);
void qa_q3_pres_client_hud_draw_crosshair(qa_q3_pres_client_hud *hud);
void qa_q3_pres_client_hud_draw_crosshair_names(qa_q3_pres_client_hud *hud);
void qa_q3_pres_client_hud_draw_spectator(qa_q3_pres_client_hud *hud);
void qa_q3_pres_client_hud_draw_vote(qa_q3_pres_client_hud *hud);
void qa_q3_pres_client_hud_draw_team_vote(qa_q3_pres_client_hud *hud);
void qa_q3_pres_client_hud_draw_follow(qa_q3_pres_client_hud *hud);
void qa_q3_pres_client_hud_draw_ammo_warning(qa_q3_pres_client_hud *hud);
void qa_q3_pres_client_hud_draw_warmup(qa_q3_pres_client_hud *hud);
bool qa_q3_pres_client_hud_draw_scoreboard(qa_q3_pres_client_hud *hud);
bool qa_q3_pres_client_hud_draw_tourney_scoreboard(qa_q3_pres_client_hud *hud);
#endif
