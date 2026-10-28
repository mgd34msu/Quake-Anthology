/* Port of src/content/q3/presentation/mission-owner-draw.ts. GPL-2.0-or-later.
 * Team Arena CG owner-drawing from id Software's cg_draw.c. */
#ifndef QA_CONTENT_Q3_PRESENTATION_MISSION_OWNER_DRAW_H
#define QA_CONTENT_Q3_PRESENTATION_MISSION_OWNER_DRAW_H
#include <stdbool.h>
#include "qa/content/q3/presentation/config.h"
#include "qa/content/q3/presentation/draw_icons.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/math.h"
#include "qa/text/q3_font.h"
#include "qa/ui/common/legacy/runtime.h"
typedef enum qa_q3_pres_mission_owner_draw_id {
  QA_Q3_PRES_OD_PLAYER_ARMOR_ICON = 1, QA_Q3_PRES_OD_PLAYER_ARMOR_VALUE = 2, QA_Q3_PRES_OD_PLAYER_HEAD = 3,
  QA_Q3_PRES_OD_PLAYER_HEALTH = 4, QA_Q3_PRES_OD_PLAYER_AMMO_ICON = 5, QA_Q3_PRES_OD_PLAYER_AMMO_VALUE = 6,
  QA_Q3_PRES_OD_SELECTEDPLAYER_HEAD = 7, QA_Q3_PRES_OD_SELECTEDPLAYER_NAME = 8,
  QA_Q3_PRES_OD_SELECTEDPLAYER_LOCATION = 9, QA_Q3_PRES_OD_SELECTEDPLAYER_STATUS = 10,
  QA_Q3_PRES_OD_SELECTEDPLAYER_WEAPON = 11, QA_Q3_PRES_OD_SELECTEDPLAYER_POWERUP = 12,
  QA_Q3_PRES_OD_PLAYER_ITEM = 19, QA_Q3_PRES_OD_PLAYER_SCORE = 20, QA_Q3_PRES_OD_BLUE_FLAGHEAD = 21,
  QA_Q3_PRES_OD_BLUE_FLAGSTATUS = 22, QA_Q3_PRES_OD_BLUE_FLAGNAME = 23, QA_Q3_PRES_OD_RED_FLAGHEAD = 24,
  QA_Q3_PRES_OD_RED_FLAGSTATUS = 25, QA_Q3_PRES_OD_RED_FLAGNAME = 26, QA_Q3_PRES_OD_BLUE_SCORE = 27,
  QA_Q3_PRES_OD_RED_SCORE = 28, QA_Q3_PRES_OD_RED_NAME = 29, QA_Q3_PRES_OD_BLUE_NAME = 30,
  QA_Q3_PRES_OD_HARVESTER_SKULLS = 31, QA_Q3_PRES_OD_ONEFLAG_STATUS = 32, QA_Q3_PRES_OD_PLAYER_LOCATION = 33,
  QA_Q3_PRES_OD_TEAM_COLOR = 34, QA_Q3_PRES_OD_CTF_POWERUP = 35, QA_Q3_PRES_OD_AREA_POWERUP = 36,
  QA_Q3_PRES_OD_PLAYER_HASFLAG = 38, QA_Q3_PRES_OD_GAME_TYPE = 39, QA_Q3_PRES_OD_SELECTEDPLAYER_ARMOR = 40,
  QA_Q3_PRES_OD_SELECTEDPLAYER_HEALTH = 41, QA_Q3_PRES_OD_PLAYER_STATUS = 42, QA_Q3_PRES_OD_AREA_SYSTEMCHAT = 46,
  QA_Q3_PRES_OD_AREA_TEAMCHAT = 47, QA_Q3_PRES_OD_AREA_CHAT = 48, QA_Q3_PRES_OD_GAME_STATUS = 49,
  QA_Q3_PRES_OD_KILLER = 50, QA_Q3_PRES_OD_PLAYER_ARMOR_ICON2D = 51, QA_Q3_PRES_OD_PLAYER_AMMO_ICON2D = 52,
  QA_Q3_PRES_OD_ACCURACY = 53, QA_Q3_PRES_OD_ASSISTS = 54, QA_Q3_PRES_OD_DEFEND = 55,
  QA_Q3_PRES_OD_EXCELLENT = 56, QA_Q3_PRES_OD_IMPRESSIVE = 57, QA_Q3_PRES_OD_PERFECT = 58,
  QA_Q3_PRES_OD_GAUNTLET = 59, QA_Q3_PRES_OD_SPECTATORS = 60, QA_Q3_PRES_OD_TEAMINFO = 61,
  QA_Q3_PRES_OD_VOICE_HEAD = 62, QA_Q3_PRES_OD_VOICE_NAME = 63, QA_Q3_PRES_OD_PLAYER_HASFLAG2D = 64,
  QA_Q3_PRES_OD_HARVESTER_SKULLS2D = 65, QA_Q3_PRES_OD_CAPFRAGLIMIT = 66, QA_Q3_PRES_OD_1STPLACE = 67,
  QA_Q3_PRES_OD_2NDPLACE = 68, QA_Q3_PRES_OD_CAPTURES = 69
} qa_q3_pres_mission_owner_draw_id;
const char *qa_q3_pres_mission_owner_draw_id_name(qa_q3_pres_mission_owner_draw_id id);
enum {
  QA_Q3_PRES_OD_SHOW_BLUE_TEAM_HAS_REDFLAG = 0x1, QA_Q3_PRES_OD_SHOW_RED_TEAM_HAS_BLUEFLAG = 0x2,
  QA_Q3_PRES_OD_SHOW_ANYTEAMGAME = 0x4, QA_Q3_PRES_OD_SHOW_HARVESTER = 0x8, QA_Q3_PRES_OD_SHOW_ONEFLAG = 0x10,
  QA_Q3_PRES_OD_SHOW_CTF = 0x20, QA_Q3_PRES_OD_SHOW_OBELISK = 0x40, QA_Q3_PRES_OD_SHOW_HEALTHCRITICAL = 0x80,
  QA_Q3_PRES_OD_SHOW_SINGLEPLAYER = 0x100, QA_Q3_PRES_OD_SHOW_TOURNAMENT = 0x200,
  QA_Q3_PRES_OD_SHOW_DURINGINCOMINGVOICE = 0x400, QA_Q3_PRES_OD_SHOW_IF_PLAYER_HAS_FLAG = 0x800,
  QA_Q3_PRES_OD_SHOW_LANPLAYONLY = 0x1000, QA_Q3_PRES_OD_SHOW_MINED = 0x2000,
  QA_Q3_PRES_OD_SHOW_HEALTHOK = 0x4000, QA_Q3_PRES_OD_SHOW_TEAMINFO = 0x8000,
  QA_Q3_PRES_OD_SHOW_NOTEAMINFO = 0x10000, QA_Q3_PRES_OD_SHOW_OTHERTEAMHASFLAG = 0x20000,
  QA_Q3_PRES_OD_SHOW_YOURTEAMHASENEMYFLAG = 0x40000, QA_Q3_PRES_OD_SHOW_ANYNONTEAMGAME = 0x80000,
  QA_Q3_PRES_OD_SHOW_2DONLY = 0x10000000
};
/* TS MissionOwnerDrawHost. media picks: armor/ammo icons + item icons. */
typedef struct qa_q3_pres_mission_owner_draw_host {
  qa_q3_pres_client_draw_icons *icons;
  void *ctx;
  const qa_q3_font_set *(*fonts)(void *ctx);
  qa_cvar_snapshot (*read_vm_cvar)(void *ctx, const char *name);
  const char *(*config_string)(void *ctx, double index);
  double (*selected_player)(void *ctx);
  void (*chat)(void *ctx, const char **system, const char **team1, const char **team2);
  const qa_q3_pres_scene_shader *armor_icon; /* nullable */
  const qa_q3_pres_scene_shader *(*weapon_icon)(void *ctx, double weapon);
  const qa_q3_pres_scene_shader *(*item_icon)(void *ctx, double item_index);
} qa_q3_pres_mission_owner_draw_host;
typedef struct qa_q3_pres_mission_owner_draw {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_game_static_state *static_state;
  qa_q3_pres_mission_owner_draw_host host;
} qa_q3_pres_mission_owner_draw;
void qa_q3_pres_mission_owner_draw_init(qa_q3_pres_mission_owner_draw *draw,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state,
    qa_q3_pres_mission_owner_draw_host host);
/* TS paint(UiOwnerDrawPaintRequest). */
void qa_q3_pres_mission_owner_draw_paint(qa_q3_pres_mission_owner_draw *draw,
                                         const qa_ui_owner_draw_paint_request *request);
bool qa_q3_pres_mission_owner_draw_other_team_has_flag(qa_q3_pres_mission_owner_draw *draw);
bool qa_q3_pres_mission_owner_draw_your_team_has_flag(qa_q3_pres_mission_owner_draw *draw);
#endif
