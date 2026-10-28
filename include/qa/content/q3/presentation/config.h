/* Port of src/content/q3/presentation/config.ts. GPL-2.0-or-later.
 * CG_RegisterCvars / CG_ForceModelChange / CG_UpdateCvars from id
 * Software's code/cgame/cg_main.c, with vmCvar_t copying from cvar.c.
 * The cvar registry is the landed qa/core/cvars registry used directly.
 * TS async forceModelChange/updateCvars are synchronous host calls. */
#ifndef QA_CONTENT_Q3_PRESENTATION_CONFIG_H
#define QA_CONTENT_Q3_PRESENTATION_CONFIG_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/core/cvars/index.h"
typedef enum qa_q3_pres_client_vm_cvar_symbol {
  QA_Q3_PRES_VM_CG_IGNORE, /* "cg_ignore" */
  QA_Q3_PRES_VM_CG_AUTOSWITCH, /* "cg_autoswitch" */
  QA_Q3_PRES_VM_CG_DRAW_GUN, /* "cg_drawGun" */
  QA_Q3_PRES_VM_CG_ZOOM_FOV, /* "cg_zoomFov" */
  QA_Q3_PRES_VM_CG_FOV, /* "cg_fov" */
  QA_Q3_PRES_VM_CG_VIEWSIZE, /* "cg_viewsize" */
  QA_Q3_PRES_VM_CG_STEREO_SEPARATION, /* "cg_stereoSeparation" */
  QA_Q3_PRES_VM_CG_SHADOWS, /* "cg_shadows" */
  QA_Q3_PRES_VM_CG_GIBS, /* "cg_gibs" */
  QA_Q3_PRES_VM_CG_DRAW2_D, /* "cg_draw2D" */
  QA_Q3_PRES_VM_CG_DRAW_STATUS, /* "cg_drawStatus" */
  QA_Q3_PRES_VM_CG_DRAW_TIMER, /* "cg_drawTimer" */
  QA_Q3_PRES_VM_CG_DRAW_F_P_S, /* "cg_drawFPS" */
  QA_Q3_PRES_VM_CG_DRAW_SNAPSHOT, /* "cg_drawSnapshot" */
  QA_Q3_PRES_VM_CG_DRAW3D_ICONS, /* "cg_draw3dIcons" */
  QA_Q3_PRES_VM_CG_DRAW_ICONS, /* "cg_drawIcons" */
  QA_Q3_PRES_VM_CG_DRAW_AMMO_WARNING, /* "cg_drawAmmoWarning" */
  QA_Q3_PRES_VM_CG_DRAW_ATTACKER, /* "cg_drawAttacker" */
  QA_Q3_PRES_VM_CG_DRAW_CROSSHAIR, /* "cg_drawCrosshair" */
  QA_Q3_PRES_VM_CG_DRAW_CROSSHAIR_NAMES, /* "cg_drawCrosshairNames" */
  QA_Q3_PRES_VM_CG_DRAW_REWARDS, /* "cg_drawRewards" */
  QA_Q3_PRES_VM_CG_CROSSHAIR_SIZE, /* "cg_crosshairSize" */
  QA_Q3_PRES_VM_CG_CROSSHAIR_HEALTH, /* "cg_crosshairHealth" */
  QA_Q3_PRES_VM_CG_CROSSHAIR_X, /* "cg_crosshairX" */
  QA_Q3_PRES_VM_CG_CROSSHAIR_Y, /* "cg_crosshairY" */
  QA_Q3_PRES_VM_CG_BRASS_TIME, /* "cg_brassTime" */
  QA_Q3_PRES_VM_CG_SIMPLE_ITEMS, /* "cg_simpleItems" */
  QA_Q3_PRES_VM_CG_ADD_MARKS, /* "cg_addMarks" */
  QA_Q3_PRES_VM_CG_LAGOMETER, /* "cg_lagometer" */
  QA_Q3_PRES_VM_CG_RAIL_TRAIL_TIME, /* "cg_railTrailTime" */
  QA_Q3_PRES_VM_CG_GUN_X, /* "cg_gun_x" */
  QA_Q3_PRES_VM_CG_GUN_Y, /* "cg_gun_y" */
  QA_Q3_PRES_VM_CG_GUN_Z, /* "cg_gun_z" */
  QA_Q3_PRES_VM_CG_CENTERTIME, /* "cg_centertime" */
  QA_Q3_PRES_VM_CG_RUNPITCH, /* "cg_runpitch" */
  QA_Q3_PRES_VM_CG_RUNROLL, /* "cg_runroll" */
  QA_Q3_PRES_VM_CG_BOBUP, /* "cg_bobup" */
  QA_Q3_PRES_VM_CG_BOBPITCH, /* "cg_bobpitch" */
  QA_Q3_PRES_VM_CG_BOBROLL, /* "cg_bobroll" */
  QA_Q3_PRES_VM_CG_SWING_SPEED, /* "cg_swingSpeed" */
  QA_Q3_PRES_VM_CG_ANIM_SPEED, /* "cg_animSpeed" */
  QA_Q3_PRES_VM_CG_DEBUG_ANIM, /* "cg_debugAnim" */
  QA_Q3_PRES_VM_CG_DEBUG_POSITION, /* "cg_debugPosition" */
  QA_Q3_PRES_VM_CG_DEBUG_EVENTS, /* "cg_debugEvents" */
  QA_Q3_PRES_VM_CG_ERROR_DECAY, /* "cg_errorDecay" */
  QA_Q3_PRES_VM_CG_NOPREDICT, /* "cg_nopredict" */
  QA_Q3_PRES_VM_CG_NO_PLAYER_ANIMS, /* "cg_noPlayerAnims" */
  QA_Q3_PRES_VM_CG_SHOWMISS, /* "cg_showmiss" */
  QA_Q3_PRES_VM_CG_FOOTSTEPS, /* "cg_footsteps" */
  QA_Q3_PRES_VM_CG_TRACER_CHANCE, /* "cg_tracerChance" */
  QA_Q3_PRES_VM_CG_TRACER_WIDTH, /* "cg_tracerWidth" */
  QA_Q3_PRES_VM_CG_TRACER_LENGTH, /* "cg_tracerLength" */
  QA_Q3_PRES_VM_CG_THIRD_PERSON_RANGE, /* "cg_thirdPersonRange" */
  QA_Q3_PRES_VM_CG_THIRD_PERSON_ANGLE, /* "cg_thirdPersonAngle" */
  QA_Q3_PRES_VM_CG_THIRD_PERSON, /* "cg_thirdPerson" */
  QA_Q3_PRES_VM_CG_TEAM_CHAT_TIME, /* "cg_teamChatTime" */
  QA_Q3_PRES_VM_CG_TEAM_CHAT_HEIGHT, /* "cg_teamChatHeight" */
  QA_Q3_PRES_VM_CG_FORCE_MODEL, /* "cg_forceModel" */
  QA_Q3_PRES_VM_CG_PREDICT_ITEMS, /* "cg_predictItems" */
  QA_Q3_PRES_VM_CG_DEFER_PLAYERS, /* "cg_deferPlayers" */
  QA_Q3_PRES_VM_CG_DRAW_TEAM_OVERLAY, /* "cg_drawTeamOverlay" */
  QA_Q3_PRES_VM_CG_TEAM_OVERLAY_USERINFO, /* "cg_teamOverlayUserinfo" */
  QA_Q3_PRES_VM_CG_STATS, /* "cg_stats" */
  QA_Q3_PRES_VM_CG_DRAW_FRIEND, /* "cg_drawFriend" */
  QA_Q3_PRES_VM_CG_TEAM_CHATS_ONLY, /* "cg_teamChatsOnly" */
  QA_Q3_PRES_VM_CG_NO_VOICE_CHATS, /* "cg_noVoiceChats" */
  QA_Q3_PRES_VM_CG_NO_VOICE_TEXT, /* "cg_noVoiceText" */
  QA_Q3_PRES_VM_CG_BUILD_SCRIPT, /* "cg_buildScript" */
  QA_Q3_PRES_VM_CG_PAUSED, /* "cg_paused" */
  QA_Q3_PRES_VM_CG_BLOOD, /* "cg_blood" */
  QA_Q3_PRES_VM_CG_SYNCHRONOUS_CLIENTS, /* "cg_synchronousClients" */
  QA_Q3_PRES_VM_CG_RED_TEAM_NAME, /* "cg_redTeamName" */
  QA_Q3_PRES_VM_CG_BLUE_TEAM_NAME, /* "cg_blueTeamName" */
  QA_Q3_PRES_VM_CG_CURRENT_SELECTED_PLAYER, /* "cg_currentSelectedPlayer" */
  QA_Q3_PRES_VM_CG_CURRENT_SELECTED_PLAYER_NAME, /* "cg_currentSelectedPlayerName" */
  QA_Q3_PRES_VM_CG_SINGLE_PLAYER, /* "cg_singlePlayer" */
  QA_Q3_PRES_VM_CG_ENABLE_DUST, /* "cg_enableDust" */
  QA_Q3_PRES_VM_CG_ENABLE_BREATH, /* "cg_enableBreath" */
  QA_Q3_PRES_VM_CG_SINGLE_PLAYER_ACTIVE, /* "cg_singlePlayerActive" */
  QA_Q3_PRES_VM_CG_RECORD_S_P_DEMO, /* "cg_recordSPDemo" */
  QA_Q3_PRES_VM_CG_RECORD_S_P_DEMO_NAME, /* "cg_recordSPDemoName" */
  QA_Q3_PRES_VM_CG_OBELISK_RESPAWN_DELAY, /* "cg_obeliskRespawnDelay" */
  QA_Q3_PRES_VM_CG_HUD_FILES, /* "cg_hudFiles" */
  QA_Q3_PRES_VM_CG_CAMERA_ORBIT, /* "cg_cameraOrbit" */
  QA_Q3_PRES_VM_CG_CAMERA_ORBIT_DELAY, /* "cg_cameraOrbitDelay" */
  QA_Q3_PRES_VM_CG_TIMESCALE_FADE_END, /* "cg_timescaleFadeEnd" */
  QA_Q3_PRES_VM_CG_TIMESCALE_FADE_SPEED, /* "cg_timescaleFadeSpeed" */
  QA_Q3_PRES_VM_CG_TIMESCALE, /* "cg_timescale" */
  QA_Q3_PRES_VM_CG_SCORE_PLUM, /* "cg_scorePlum" */
  QA_Q3_PRES_VM_CG_SMOOTH_CLIENTS, /* "cg_smoothClients" */
  QA_Q3_PRES_VM_CG_CAMERA_MODE, /* "cg_cameraMode" */
  QA_Q3_PRES_VM_PMOVE_FIXED, /* "pmove_fixed" */
  QA_Q3_PRES_VM_PMOVE_MSEC, /* "pmove_msec" */
  QA_Q3_PRES_VM_CG_NO_TAUNT, /* "cg_noTaunt" */
  QA_Q3_PRES_VM_CG_NO_PROJECTILE_TRAIL, /* "cg_noProjectileTrail" */
  QA_Q3_PRES_VM_CG_SMALL_FONT, /* "cg_smallFont" */
  QA_Q3_PRES_VM_CG_BIG_FONT, /* "cg_bigFont" */
  QA_Q3_PRES_VM_CG_OLD_RAIL, /* "cg_oldRail" */
  QA_Q3_PRES_VM_CG_OLD_ROCKET, /* "cg_oldRocket" */
  QA_Q3_PRES_VM_CG_OLD_PLASMA, /* "cg_oldPlasma" */
  QA_Q3_PRES_VM_CG_TRUE_LIGHTNING, /* "cg_trueLightning" */
  QA_Q3_PRES_VM_SYMBOL_COUNT
} qa_q3_pres_client_vm_cvar_symbol;
const char *qa_q3_pres_client_vm_cvar_symbol_name(qa_q3_pres_client_vm_cvar_symbol symbol);
typedef struct qa_q3_pres_cvar_definition {
  qa_q3_pres_client_vm_cvar_symbol symbol;
  const char *name;
  const char *default_value;
  double flags; /* QA_CVAR_FLAG_* word */
} qa_q3_pres_cvar_definition;
/* TS cvarTable(product): static frozen table. */
const qa_q3_pres_cvar_definition *qa_q3_pres_cvar_table(qa_q3_base_product product, size_t *out_count);
/* TS ClientConfigurationHost. clients is Pick<ClientInfoStore,
 * "newClientInfo">; config_string returns a borrowed TS string.
 * status_visible NULL is the TS absent optional. */
typedef struct qa_q3_pres_client_configuration_host {
  void *status_ctx;
  bool (*status_visible)(void *ctx);
  qa_cvar_registry *cvars;
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_game_static_state *static_state;
  void *clients_ctx;
  bool (*new_client_info)(void *ctx, double client_num, const char *config); /* false only OOM */
  void *config_ctx;
  const char *(*config_string)(void *ctx, double index);
} qa_q3_pres_client_configuration_host;
typedef struct qa_q3_pres_client_configuration qa_q3_pres_client_configuration;
/* Product mismatch aborts with the TS Error text. False only on OOM. */
bool qa_q3_pres_client_configuration_init(qa_q3_pres_client_configuration **out, qa_q3_base_product product,
    qa_q3_pres_client_configuration_host host);
void qa_q3_pres_client_configuration_destroy(qa_q3_pres_client_configuration *configuration);
void qa_q3_pres_client_configuration_register_cvars(qa_q3_pres_client_configuration *configuration);
/* Deep copy into out (qa_cvar_read_free by caller). False only on OOM;
 * unknown symbols/names abort with the exact TS Error text. */
bool qa_q3_pres_client_configuration_read_vm_symbol(qa_q3_pres_client_configuration *configuration,
    qa_q3_pres_client_vm_cvar_symbol symbol, qa_cvar_snapshot *out);
bool qa_q3_pres_client_configuration_read_vm_cvar(qa_q3_pres_client_configuration *configuration,
    const char *name, qa_cvar_snapshot *out);
/* False only on OOM. */
bool qa_q3_pres_client_configuration_set_vm_numeric_value(qa_q3_pres_client_configuration *configuration,
    qa_q3_pres_client_vm_cvar_symbol symbol, double value);
bool qa_q3_pres_client_configuration_set_vm_integer(qa_q3_pres_client_configuration *configuration,
    qa_q3_pres_client_vm_cvar_symbol symbol, double value);
/* False only on OOM in the client-info reload. */
bool qa_q3_pres_client_configuration_force_model_change(qa_q3_pres_client_configuration *configuration);
bool qa_q3_pres_client_configuration_update_cvars(qa_q3_pres_client_configuration *configuration);
#endif
