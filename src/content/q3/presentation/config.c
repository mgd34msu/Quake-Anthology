/* Port of src/content/q3/presentation/config.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/config.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/numeric.h"
#include "qa/foundation/support.h"

enum { MAX_CLIENTS = 64, CS_PLAYERS = 544, MAX_CVAR_VALUE_STRING = 256, MAX_TOKEN_CHARS = 1024 };

/* Enum/table rows generated from config.ts by /tmp/gen_config_table.py. */
static const qa_q3_pres_cvar_definition BASE_TABLE[] = {
  {QA_Q3_PRES_VM_CG_IGNORE, "cg_ignore", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_AUTOSWITCH, "cg_autoswitch", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_GUN, "cg_drawGun", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_ZOOM_FOV, "cg_zoomfov", "22.5", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_FOV, "cg_fov", "90", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_VIEWSIZE, "cg_viewsize", "100", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_STEREO_SEPARATION, "cg_stereoSeparation", "0.4", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SHADOWS, "cg_shadows", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_GIBS, "cg_gibs", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW2_D, "cg_draw2D", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_STATUS, "cg_drawStatus", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_TIMER, "cg_drawTimer", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_F_P_S, "cg_drawFPS", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_SNAPSHOT, "cg_drawSnapshot", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW3D_ICONS, "cg_draw3dIcons", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_ICONS, "cg_drawIcons", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_AMMO_WARNING, "cg_drawAmmoWarning", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_ATTACKER, "cg_drawAttacker", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_CROSSHAIR, "cg_drawCrosshair", "4", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_CROSSHAIR_NAMES, "cg_drawCrosshairNames", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_REWARDS, "cg_drawRewards", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_CROSSHAIR_SIZE, "cg_crosshairSize", "24", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_CROSSHAIR_HEALTH, "cg_crosshairHealth", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_CROSSHAIR_X, "cg_crosshairX", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_CROSSHAIR_Y, "cg_crosshairY", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_BRASS_TIME, "cg_brassTime", "2500", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SIMPLE_ITEMS, "cg_simpleItems", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_ADD_MARKS, "cg_marks", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_LAGOMETER, "cg_lagometer", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_RAIL_TRAIL_TIME, "cg_railTrailTime", "400", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_GUN_X, "cg_gunX", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_GUN_Y, "cg_gunY", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_GUN_Z, "cg_gunZ", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_CENTERTIME, "cg_centertime", "3", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_RUNPITCH, "cg_runpitch", "0.002", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_RUNROLL, "cg_runroll", "0.005", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_BOBUP, "cg_bobup", "0.005", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_BOBPITCH, "cg_bobpitch", "0.002", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_BOBROLL, "cg_bobroll", "0.002", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SWING_SPEED, "cg_swingSpeed", "0.3", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_ANIM_SPEED, "cg_animspeed", "1", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_DEBUG_ANIM, "cg_debuganim", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_DEBUG_POSITION, "cg_debugposition", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_DEBUG_EVENTS, "cg_debugevents", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_ERROR_DECAY, "cg_errordecay", "100", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_NOPREDICT, "cg_nopredict", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_NO_PLAYER_ANIMS, "cg_noplayeranims", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_SHOWMISS, "cg_showmiss", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_FOOTSTEPS, "cg_footsteps", "1", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_TRACER_CHANCE, "cg_tracerchance", "0.4", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_TRACER_WIDTH, "cg_tracerwidth", "1", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_TRACER_LENGTH, "cg_tracerlength", "100", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_THIRD_PERSON_RANGE, "cg_thirdPersonRange", "40", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_THIRD_PERSON_ANGLE, "cg_thirdPersonAngle", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_THIRD_PERSON, "cg_thirdPerson", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_TEAM_CHAT_TIME, "cg_teamChatTime", "3000", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_TEAM_CHAT_HEIGHT, "cg_teamChatHeight", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_FORCE_MODEL, "cg_forceModel", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_PREDICT_ITEMS, "cg_predictItems", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DEFER_PLAYERS, "cg_deferPlayers", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_TEAM_OVERLAY, "cg_drawTeamOverlay", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_TEAM_OVERLAY_USERINFO, "teamoverlay", "0", QA_CVAR_FLAG_READ_ONLY | QA_CVAR_FLAG_USER_INFO},
  {QA_Q3_PRES_VM_CG_STATS, "cg_stats", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_DRAW_FRIEND, "cg_drawFriend", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_TEAM_CHATS_ONLY, "cg_teamChatsOnly", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_NO_VOICE_CHATS, "cg_noVoiceChats", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_NO_VOICE_TEXT, "cg_noVoiceText", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_BUILD_SCRIPT, "com_buildScript", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_PAUSED, "cl_paused", "0", QA_CVAR_FLAG_READ_ONLY},
  {QA_Q3_PRES_VM_CG_BLOOD, "com_blood", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SYNCHRONOUS_CLIENTS, "g_synchronousClients", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_CAMERA_ORBIT, "cg_cameraOrbit", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_CAMERA_ORBIT_DELAY, "cg_cameraOrbitDelay", "50", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_TIMESCALE_FADE_END, "cg_timescaleFadeEnd", "1", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_TIMESCALE_FADE_SPEED, "cg_timescaleFadeSpeed", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_TIMESCALE, "timescale", "1", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_SCORE_PLUM, "cg_scorePlums", "1", QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SMOOTH_CLIENTS, "cg_smoothClients", "0", QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_CAMERA_MODE, "com_cameraMode", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_PMOVE_FIXED, "pmove_fixed", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_PMOVE_MSEC, "pmove_msec", "8", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_PMOVE_LADDERS, "pmove_ladders", "1", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_NO_TAUNT, "cg_noTaunt", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_NO_PROJECTILE_TRAIL, "cg_noProjectileTrail", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SMALL_FONT, "ui_smallFont", "0.25", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_BIG_FONT, "ui_bigFont", "0.4", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_OLD_RAIL, "cg_oldRail", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_OLD_ROCKET, "cg_oldRocket", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_OLD_PLASMA, "cg_oldPlasma", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_TRUE_LIGHTNING, "cg_trueLightning", "0.0", QA_CVAR_FLAG_ARCHIVE},
};
static const qa_q3_pres_cvar_definition MISSION_TABLE[] = {
  {QA_Q3_PRES_VM_CG_IGNORE, "cg_ignore", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_AUTOSWITCH, "cg_autoswitch", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_GUN, "cg_drawGun", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_ZOOM_FOV, "cg_zoomfov", "22.5", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_FOV, "cg_fov", "90", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_VIEWSIZE, "cg_viewsize", "100", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_STEREO_SEPARATION, "cg_stereoSeparation", "0.4", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SHADOWS, "cg_shadows", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_GIBS, "cg_gibs", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW2_D, "cg_draw2D", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_STATUS, "cg_drawStatus", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_TIMER, "cg_drawTimer", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_F_P_S, "cg_drawFPS", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_SNAPSHOT, "cg_drawSnapshot", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW3D_ICONS, "cg_draw3dIcons", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_ICONS, "cg_drawIcons", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_AMMO_WARNING, "cg_drawAmmoWarning", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_ATTACKER, "cg_drawAttacker", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_CROSSHAIR, "cg_drawCrosshair", "4", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_CROSSHAIR_NAMES, "cg_drawCrosshairNames", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_REWARDS, "cg_drawRewards", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_CROSSHAIR_SIZE, "cg_crosshairSize", "24", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_CROSSHAIR_HEALTH, "cg_crosshairHealth", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_CROSSHAIR_X, "cg_crosshairX", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_CROSSHAIR_Y, "cg_crosshairY", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_BRASS_TIME, "cg_brassTime", "2500", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SIMPLE_ITEMS, "cg_simpleItems", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_ADD_MARKS, "cg_marks", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_LAGOMETER, "cg_lagometer", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_RAIL_TRAIL_TIME, "cg_railTrailTime", "400", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_GUN_X, "cg_gunX", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_GUN_Y, "cg_gunY", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_GUN_Z, "cg_gunZ", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_CENTERTIME, "cg_centertime", "3", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_RUNPITCH, "cg_runpitch", "0.002", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_RUNROLL, "cg_runroll", "0.005", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_BOBUP, "cg_bobup", "0.005", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_BOBPITCH, "cg_bobpitch", "0.002", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_BOBROLL, "cg_bobroll", "0.002", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SWING_SPEED, "cg_swingSpeed", "0.3", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_ANIM_SPEED, "cg_animspeed", "1", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_DEBUG_ANIM, "cg_debuganim", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_DEBUG_POSITION, "cg_debugposition", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_DEBUG_EVENTS, "cg_debugevents", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_ERROR_DECAY, "cg_errordecay", "100", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_NOPREDICT, "cg_nopredict", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_NO_PLAYER_ANIMS, "cg_noplayeranims", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_SHOWMISS, "cg_showmiss", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_FOOTSTEPS, "cg_footsteps", "1", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_TRACER_CHANCE, "cg_tracerchance", "0.4", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_TRACER_WIDTH, "cg_tracerwidth", "1", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_TRACER_LENGTH, "cg_tracerlength", "100", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_THIRD_PERSON_RANGE, "cg_thirdPersonRange", "40", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_THIRD_PERSON_ANGLE, "cg_thirdPersonAngle", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_THIRD_PERSON, "cg_thirdPerson", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_TEAM_CHAT_TIME, "cg_teamChatTime", "3000", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_TEAM_CHAT_HEIGHT, "cg_teamChatHeight", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_FORCE_MODEL, "cg_forceModel", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_PREDICT_ITEMS, "cg_predictItems", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DEFER_PLAYERS, "cg_deferPlayers", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_DRAW_TEAM_OVERLAY, "cg_drawTeamOverlay", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_TEAM_OVERLAY_USERINFO, "teamoverlay", "0", QA_CVAR_FLAG_READ_ONLY | QA_CVAR_FLAG_USER_INFO},
  {QA_Q3_PRES_VM_CG_STATS, "cg_stats", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_DRAW_FRIEND, "cg_drawFriend", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_TEAM_CHATS_ONLY, "cg_teamChatsOnly", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_NO_VOICE_CHATS, "cg_noVoiceChats", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_NO_VOICE_TEXT, "cg_noVoiceText", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_BUILD_SCRIPT, "com_buildScript", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_PAUSED, "cl_paused", "0", QA_CVAR_FLAG_READ_ONLY},
  {QA_Q3_PRES_VM_CG_BLOOD, "com_blood", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SYNCHRONOUS_CLIENTS, "g_synchronousClients", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_RED_TEAM_NAME, "g_redteam", "Stroggs", QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_SERVER_INFO | QA_CVAR_FLAG_USER_INFO},
  {QA_Q3_PRES_VM_CG_BLUE_TEAM_NAME, "g_blueteam", "Pagans", QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_SERVER_INFO | QA_CVAR_FLAG_USER_INFO},
  {QA_Q3_PRES_VM_CG_CURRENT_SELECTED_PLAYER, "cg_currentSelectedPlayer", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_CURRENT_SELECTED_PLAYER_NAME, "cg_currentSelectedPlayerName", "", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SINGLE_PLAYER, "ui_singlePlayerActive", "0", QA_CVAR_FLAG_USER_INFO},
  {QA_Q3_PRES_VM_CG_ENABLE_DUST, "g_enableDust", "0", QA_CVAR_FLAG_SERVER_INFO},
  {QA_Q3_PRES_VM_CG_ENABLE_BREATH, "g_enableBreath", "0", QA_CVAR_FLAG_SERVER_INFO},
  {QA_Q3_PRES_VM_CG_SINGLE_PLAYER_ACTIVE, "ui_singlePlayerActive", "0", QA_CVAR_FLAG_USER_INFO},
  {QA_Q3_PRES_VM_CG_RECORD_S_P_DEMO, "ui_recordSPDemo", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_RECORD_S_P_DEMO_NAME, "ui_recordSPDemoName", "", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_OBELISK_RESPAWN_DELAY, "g_obeliskRespawnDelay", "10", QA_CVAR_FLAG_SERVER_INFO},
  {QA_Q3_PRES_VM_CG_HUD_FILES, "cg_hudFiles", "ui/hud.txt", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_CAMERA_ORBIT, "cg_cameraOrbit", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_CG_CAMERA_ORBIT_DELAY, "cg_cameraOrbitDelay", "50", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_TIMESCALE_FADE_END, "cg_timescaleFadeEnd", "1", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_TIMESCALE_FADE_SPEED, "cg_timescaleFadeSpeed", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_TIMESCALE, "timescale", "1", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_SCORE_PLUM, "cg_scorePlums", "1", QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SMOOTH_CLIENTS, "cg_smoothClients", "0", QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_CAMERA_MODE, "com_cameraMode", "0", QA_CVAR_FLAG_CHEAT},
  {QA_Q3_PRES_VM_PMOVE_FIXED, "pmove_fixed", "0", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_PMOVE_MSEC, "pmove_msec", "8", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_PMOVE_LADDERS, "pmove_ladders", "1", QA_CVAR_FLAG_NONE},
  {QA_Q3_PRES_VM_CG_NO_TAUNT, "cg_noTaunt", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_NO_PROJECTILE_TRAIL, "cg_noProjectileTrail", "0", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_SMALL_FONT, "ui_smallFont", "0.25", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_BIG_FONT, "ui_bigFont", "0.4", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_OLD_RAIL, "cg_oldRail", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_OLD_ROCKET, "cg_oldRocket", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_OLD_PLASMA, "cg_oldPlasma", "1", QA_CVAR_FLAG_ARCHIVE},
  {QA_Q3_PRES_VM_CG_TRUE_LIGHTNING, "cg_trueLightning", "0.0", QA_CVAR_FLAG_ARCHIVE},
};
static const char *const SYMBOL_NAMES[] = {
  "cg_ignore",
  "cg_autoswitch",
  "cg_drawGun",
  "cg_zoomFov",
  "cg_fov",
  "cg_viewsize",
  "cg_stereoSeparation",
  "cg_shadows",
  "cg_gibs",
  "cg_draw2D",
  "cg_drawStatus",
  "cg_drawTimer",
  "cg_drawFPS",
  "cg_drawSnapshot",
  "cg_draw3dIcons",
  "cg_drawIcons",
  "cg_drawAmmoWarning",
  "cg_drawAttacker",
  "cg_drawCrosshair",
  "cg_drawCrosshairNames",
  "cg_drawRewards",
  "cg_crosshairSize",
  "cg_crosshairHealth",
  "cg_crosshairX",
  "cg_crosshairY",
  "cg_brassTime",
  "cg_simpleItems",
  "cg_addMarks",
  "cg_lagometer",
  "cg_railTrailTime",
  "cg_gun_x",
  "cg_gun_y",
  "cg_gun_z",
  "cg_centertime",
  "cg_runpitch",
  "cg_runroll",
  "cg_bobup",
  "cg_bobpitch",
  "cg_bobroll",
  "cg_swingSpeed",
  "cg_animSpeed",
  "cg_debugAnim",
  "cg_debugPosition",
  "cg_debugEvents",
  "cg_errorDecay",
  "cg_nopredict",
  "cg_noPlayerAnims",
  "cg_showmiss",
  "cg_footsteps",
  "cg_tracerChance",
  "cg_tracerWidth",
  "cg_tracerLength",
  "cg_thirdPersonRange",
  "cg_thirdPersonAngle",
  "cg_thirdPerson",
  "cg_teamChatTime",
  "cg_teamChatHeight",
  "cg_forceModel",
  "cg_predictItems",
  "cg_deferPlayers",
  "cg_drawTeamOverlay",
  "cg_teamOverlayUserinfo",
  "cg_stats",
  "cg_drawFriend",
  "cg_teamChatsOnly",
  "cg_noVoiceChats",
  "cg_noVoiceText",
  "cg_buildScript",
  "cg_paused",
  "cg_blood",
  "cg_synchronousClients",
  "cg_redTeamName",
  "cg_blueTeamName",
  "cg_currentSelectedPlayer",
  "cg_currentSelectedPlayerName",
  "cg_singlePlayer",
  "cg_enableDust",
  "cg_enableBreath",
  "cg_singlePlayerActive",
  "cg_recordSPDemo",
  "cg_recordSPDemoName",
  "cg_obeliskRespawnDelay",
  "cg_hudFiles",
  "cg_cameraOrbit",
  "cg_cameraOrbitDelay",
  "cg_timescaleFadeEnd",
  "cg_timescaleFadeSpeed",
  "cg_timescale",
  "cg_scorePlum",
  "cg_smoothClients",
  "cg_cameraMode",
  "pmove_fixed",
  "pmove_msec",
  "pmove_ladders",
  "cg_noTaunt",
  "cg_noProjectileTrail",
  "cg_smallFont",
  "cg_bigFont",
  "cg_oldRail",
  "cg_oldRocket",
  "cg_oldPlasma",
  "cg_trueLightning",
};

const char *qa_q3_pres_client_vm_cvar_symbol_name(qa_q3_pres_client_vm_cvar_symbol symbol) {
  if (symbol < 0 || symbol >= QA_Q3_PRES_VM_SYMBOL_COUNT) return "";
  return SYMBOL_NAMES[symbol];
}

const qa_q3_pres_cvar_definition *qa_q3_pres_cvar_table(qa_q3_base_product product, size_t *out_count) {
  if (product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    *out_count = sizeof MISSION_TABLE / sizeof MISSION_TABLE[0];
    return MISSION_TABLE;
  }
  *out_count = sizeof BASE_TABLE / sizeof BASE_TABLE[0];
  return BASE_TABLE;
}

struct qa_q3_pres_client_configuration {
  qa_q3_base_product product;
  qa_q3_pres_client_configuration_host host;
  const qa_q3_pres_cvar_definition *table;
  size_t table_count;
  qa_cvar_snapshot *cache; /* [SYMBOL_COUNT], present per has_cache */
  bool *has_cache;
  double force_model_modification_count;
  double draw_team_overlay_modification_count;
  bool registered, updating;
};
typedef qa_q3_pres_client_configuration configuration_t;

static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}
static bool snapshot_copy(qa_cvar_snapshot *out, const qa_cvar_snapshot *source) {
  memset(out, 0, sizeof *out);
  out->name = source->name == NULL ? NULL : xstrdup(source->name);
  out->value = source->value == NULL ? NULL : xstrdup(source->value);
  out->reset_value = source->reset_value == NULL ? NULL : xstrdup(source->reset_value);
  out->latched_value = source->latched_value == NULL ? NULL : xstrdup(source->latched_value);
  if ((source->name != NULL && out->name == NULL) || (source->value != NULL && out->value == NULL) ||
      (source->reset_value != NULL && out->reset_value == NULL) ||
      (source->latched_value != NULL && out->latched_value == NULL)) {
    qa_cvar_read_free(out);
    return false;
  }
  out->flags = source->flags;
  out->modified = source->modified;
  out->modification_count = source->modification_count;
  out->numeric_value = source->numeric_value;
  out->integer_value = source->integer_value;
  return true;
}
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double truncated = trunc(fmod(fabs(value), 4294967296.0));
  uint32_t bits = qa_js_to_uint32(truncated);
  if (value < 0) bits = ~bits + 1u;
  int32_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}
/* TS sourceString(value, capacity). C strings are bytes, so the TS
 * charCode > 255 rejection cannot occur. Writes into caller storage of
 * capacity bytes (value truncated to capacity - 1 plus NUL); returns the
 * full pre-truncation length when out_length is given. */
static void source_string(const char *value, double capacity, char *out, size_t *out_length) {
  if (capacity != trunc(capacity) || capacity < 1) {
    qa_range_error("Source string capacity must be positive");
  }
  size_t length = strlen(value);
  if (out_length != NULL) *out_length = length;
  size_t keep = (size_t)capacity - 1;
  size_t take = length < keep ? length : keep;
  memcpy(out, value, take);
  out[take] = '\0';
}
static void changed_snapshot(const qa_cvar_snapshot *engine, qa_cvar_snapshot *out) {
  char value[MAX_CVAR_VALUE_STRING];
  size_t length = 0;
  source_string(engine->value != NULL ? engine->value : "", MAX_CVAR_VALUE_STRING, value, &length);
  if (strlen(value) != length) {
    qa_range_error("Cvar_Update: src %s length %zu exceeds MAX_CVAR_VALUE_STRING",
        engine->value != NULL ? engine->value : "", length);
  }
  if (!snapshot_copy(out, engine)) qa_range_error("out of memory");
  free(out->value);
  out->value = xstrdup(value);
  if (out->value == NULL) qa_range_error("out of memory");
  out->numeric_value = (double)(float)engine->numeric_value;
  out->integer_value = engine->integer_value;
  out->modification_count = engine->modification_count;
}

bool qa_q3_pres_client_configuration_init(configuration_t **out, qa_q3_base_product product,
    qa_q3_pres_client_configuration_host host) {
  if (host.state->product != product || host.static_state->product != product) {
    qa_range_error("Client configuration services must share one product");
  }
  configuration_t *configuration = calloc(1, sizeof *configuration);
  if (configuration == NULL) return false;
  configuration->product = product;
  configuration->host = host;
  configuration->table = qa_q3_pres_cvar_table(product, &configuration->table_count);
  configuration->cache = calloc(QA_Q3_PRES_VM_SYMBOL_COUNT, sizeof *configuration->cache);
  configuration->has_cache = calloc(QA_Q3_PRES_VM_SYMBOL_COUNT, sizeof *configuration->has_cache);
  if (configuration->cache == NULL || configuration->has_cache == NULL) {
    qa_q3_pres_client_configuration_destroy(configuration);
    return false;
  }
  configuration->draw_team_overlay_modification_count = -1;
  for (size_t i = 0; i < configuration->table_count; i++) {
    const qa_q3_pres_cvar_definition *definition = &configuration->table[i];
    qa_cvar_snapshot *slot = &configuration->cache[definition->symbol];
    slot->name = xstrdup(definition->name);
    slot->value = xstrdup("");
    slot->reset_value = xstrdup("");
    configuration->has_cache[definition->symbol] = true;
    if (slot->name == NULL || slot->value == NULL || slot->reset_value == NULL) {
      qa_q3_pres_client_configuration_destroy(configuration);
      return false;
    }
  }
  *out = configuration;
  return true;
}

void qa_q3_pres_client_configuration_destroy(configuration_t *configuration) {
  if (configuration == NULL) return;
  if (configuration->cache != NULL) {
    for (int i = 0; i < QA_Q3_PRES_VM_SYMBOL_COUNT; i++) qa_cvar_read_free(&configuration->cache[i]);
  }
  free(configuration->cache);
  free(configuration->has_cache);
  free(configuration);
}

static void copy_definition(configuration_t *configuration, const qa_q3_pres_cvar_definition *definition,
    const qa_cvar_snapshot *engine, bool forced) {
  qa_cvar_snapshot *slot = &configuration->cache[definition->symbol];
  bool has_previous = configuration->has_cache[definition->symbol];
  if (!forced && has_previous && slot->modification_count == engine->modification_count) return;
  qa_cvar_snapshot changed;
  changed_snapshot(engine, &changed);
  if (has_previous) qa_cvar_read_free(slot);
  *slot = changed;
  configuration->has_cache[definition->symbol] = true;
}

void qa_q3_pres_client_configuration_register_cvars(configuration_t *configuration) {
  if (configuration->updating) qa_range_error("Cannot register cvars during a configuration update");
  for (size_t i = 0; i < configuration->table_count; i++) {
    const qa_q3_pres_cvar_definition *definition = &configuration->table[i];
    qa_cvar_snapshot engine;
    if (!qa_cvar_registry_register(configuration->host.cvars, definition->name,
            definition->default_value, definition->flags, &engine)) {
      qa_range_error("Cgame cvar registration failed: %s", definition->name);
    }
    copy_definition(configuration, definition, &engine, true);
    qa_cvar_read_free(&engine);
  }
  qa_cvar_snapshot running;
  bool has_running = qa_cvar_registry_get(configuration->host.cvars, "sv_running", &running);
  char running_text[MAX_TOKEN_CHARS];
  source_string(has_running && running.value != NULL ? running.value : "", MAX_TOKEN_CHARS, running_text, NULL);
  if (has_running) qa_cvar_read_free(&running);
  configuration->host.static_state->local_server = qa_game_atoi(running_text);
  configuration->force_model_modification_count =
      configuration->cache[QA_Q3_PRES_VM_CG_FORCE_MODEL].modification_count;
  bool mission = configuration->product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  qa_cvar_snapshot ignored;
  unsigned user_archive = QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_ARCHIVE;
  if (qa_cvar_registry_register(configuration->host.cvars, "model", "sarge", user_archive, &ignored)) {
    qa_cvar_read_free(&ignored);
  }
  if (qa_cvar_registry_register(configuration->host.cvars, "headmodel", "sarge", user_archive, &ignored)) {
    qa_cvar_read_free(&ignored);
  }
  if (qa_cvar_registry_register(configuration->host.cvars, "team_model", mission ? "james" : "sarge",
          user_archive, &ignored)) {
    qa_cvar_read_free(&ignored);
  }
  if (qa_cvar_registry_register(configuration->host.cvars, "team_headmodel", mission ? "*james" : "sarge",
          user_archive, &ignored)) {
    qa_cvar_read_free(&ignored);
  }
  configuration->registered = true;
}

/* Effective read (with the drawStatus override) into a deep copy. */
static bool read_effective(configuration_t *configuration, qa_q3_pres_client_vm_cvar_symbol symbol,
    qa_cvar_snapshot *out) {
  if (symbol < 0 || symbol >= QA_Q3_PRES_VM_SYMBOL_COUNT || !configuration->has_cache[symbol]) {
    qa_range_error("VM cvar %s is not registered for %s",
        qa_q3_pres_client_vm_cvar_symbol_name(symbol), qa_q3_base_product_name(configuration->product));
  }
  if (!snapshot_copy(out, &configuration->cache[symbol])) return false;
  if (symbol == QA_Q3_PRES_VM_CG_DRAW_STATUS && configuration->host.status_visible != NULL &&
      configuration->host.status_visible(configuration->host.status_ctx) == false) {
    free(out->value);
    out->value = xstrdup("0");
    if (out->value == NULL) {
      qa_cvar_read_free(out);
      return false;
    }
    out->numeric_value = 0;
    out->integer_value = 0;
  }
  return true;
}

bool qa_q3_pres_client_configuration_read_vm_symbol(configuration_t *configuration,
    qa_q3_pres_client_vm_cvar_symbol symbol, qa_cvar_snapshot *out) {
  return read_effective(configuration, symbol, out);
}
bool qa_q3_pres_client_configuration_set_vm_numeric_value(configuration_t *configuration,
    qa_q3_pres_client_vm_cvar_symbol symbol, double value) {
  qa_cvar_snapshot previous;
  if (!read_effective(configuration, symbol, &previous)) return false;
  previous.numeric_value = (double)(float)value;
  qa_cvar_read_free(&configuration->cache[symbol]);
  configuration->cache[symbol] = previous;
  return true;
}
bool qa_q3_pres_client_configuration_set_vm_integer(configuration_t *configuration,
    qa_q3_pres_client_vm_cvar_symbol symbol, double value) {
  qa_cvar_snapshot previous;
  if (!read_effective(configuration, symbol, &previous)) return false;
  previous.integer_value = to_int32(value);
  qa_cvar_read_free(&configuration->cache[symbol]);
  configuration->cache[symbol] = previous;
  return true;
}
static bool name_equal_folded(const char *a, const char *b) {
  for (;;) {
    unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
    if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
    if (ca != cb) return false;
    if (ca == 0) return true;
    a++;
    b++;
  }
}
bool qa_q3_pres_client_configuration_read_vm_cvar(configuration_t *configuration, const char *name,
    qa_cvar_snapshot *out) {
  for (size_t i = 0; i < configuration->table_count; i++) {
    if (name_equal_folded(configuration->table[i].name, name)) {
      return read_effective(configuration, configuration->table[i].symbol, out);
    }
  }
  qa_range_error("Cgame VM cvar %s is not registered for %s", name,
      qa_q3_base_product_name(configuration->product));
}

static bool reload_client_info(configuration_t *configuration) {
  for (int index = 0; index < MAX_CLIENTS; index++) {
    const char *config = configuration->host.config_string(configuration->host.config_ctx, CS_PLAYERS + index);
    if (config != NULL && config[0] != '\0') {
      if (!configuration->host.new_client_info(configuration->host.clients_ctx, index, config)) return false;
    }
  }
  return true;
}

bool qa_q3_pres_client_configuration_force_model_change(configuration_t *configuration) {
  if (!configuration->registered) qa_range_error("Client cvars must be registered before forcing models");
  if (configuration->updating) qa_range_error("Client configuration update is already active");
  configuration->updating = true;
  bool result = reload_client_info(configuration);
  configuration->updating = false;
  return result;
}

bool qa_q3_pres_client_configuration_update_cvars(configuration_t *configuration) {
  if (!configuration->registered) qa_range_error("Client cvars must be registered before updating");
  if (configuration->updating) qa_range_error("Client configuration update is already active");
  configuration->updating = true;
  bool result = true;
  for (size_t i = 0; i < configuration->table_count; i++) {
    const qa_q3_pres_cvar_definition *definition = &configuration->table[i];
    qa_cvar_snapshot engine;
    if (qa_cvar_registry_get(configuration->host.cvars, definition->name, &engine)) {
      copy_definition(configuration, definition, &engine, false);
      qa_cvar_read_free(&engine);
    }
  }
  qa_cvar_snapshot overlay;
  if (!read_effective(configuration, QA_Q3_PRES_VM_CG_DRAW_TEAM_OVERLAY, &overlay)) {
    result = false;
  } else {
    if (configuration->draw_team_overlay_modification_count != overlay.modification_count) {
      configuration->draw_team_overlay_modification_count = overlay.modification_count;
      qa_cvar_snapshot ignored;
      if (qa_cvar_registry_set(configuration->host.cvars, "teamoverlay",
              overlay.integer_value > 0 ? "1" : "0", true, &ignored)) {
        qa_cvar_read_free(&ignored);
      }
      if (qa_cvar_registry_set(configuration->host.cvars, "teamoverlay", "1", true, &ignored)) {
        qa_cvar_read_free(&ignored);
      }
    }
    qa_cvar_read_free(&overlay);
  }
  if (result) {
    qa_cvar_snapshot force_model;
    if (!read_effective(configuration, QA_Q3_PRES_VM_CG_FORCE_MODEL, &force_model)) {
      result = false;
    } else {
      if (configuration->force_model_modification_count != force_model.modification_count) {
        configuration->force_model_modification_count = force_model.modification_count;
        result = reload_client_info(configuration);
      }
      qa_cvar_read_free(&force_model);
    }
  }
  configuration->updating = false;
  return result;
}
