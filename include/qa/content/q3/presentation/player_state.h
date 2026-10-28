/* Port of src/content/q3/presentation/player-state.ts. GPL-2.0-or-later.
 * Player-state transitions from id Software's code/cgame/cg_playerstate.c.
 * TS async event checks are synchronous host calls. The events pick is
 * Pick<ClientEventRuntime, "entityEvent"|"painEvent"> (events.h). */
#ifndef QA_CONTENT_Q3_PRESENTATION_PLAYER_STATE_H
#define QA_CONTENT_Q3_PRESENTATION_PLAYER_STATE_H
#include <stdbool.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/ui.h"
typedef enum qa_q3_pres_player_state_sound {
  QA_Q3_PRES_NO_AMMO_SOUND, /* "noAmmoSound" */
  QA_Q3_PRES_HIT_SOUND, /* "hitSound" */
  QA_Q3_PRES_HIT_TEAM_SOUND, /* "hitTeamSound" */
  QA_Q3_PRES_CAPTURE_AWARD_SOUND, /* "captureAwardSound" */
  QA_Q3_PRES_IMPRESSIVE_SOUND, /* "impressiveSound" */
  QA_Q3_PRES_EXCELLENT_SOUND, /* "excellentSound" */
  QA_Q3_PRES_HUMILIATION_SOUND, /* "humiliationSound" */
  QA_Q3_PRES_DEFEND_SOUND, /* "defendSound" */
  QA_Q3_PRES_ASSIST_SOUND, /* "assistSound" */
  QA_Q3_PRES_DENIED_SOUND, /* "deniedSound" */
  QA_Q3_PRES_HOLY_SHIT_SOUND, /* "holyShitSound" */
  QA_Q3_PRES_YOU_HAVE_FLAG_SOUND, /* "youHaveFlagSound" */
  QA_Q3_PRES_TAKEN_LEAD_SOUND, /* "takenLeadSound" */
  QA_Q3_PRES_TIED_LEAD_SOUND, /* "tiedLeadSound" */
  QA_Q3_PRES_LOST_LEAD_SOUND, /* "lostLeadSound" */
  QA_Q3_PRES_SUDDEN_DEATH_SOUND, /* "suddenDeathSound" */
  QA_Q3_PRES_ONE_MINUTE_SOUND, /* "oneMinuteSound" */
  QA_Q3_PRES_FIVE_MINUTE_SOUND, /* "fiveMinuteSound" */
  QA_Q3_PRES_ONE_FRAG_SOUND, /* "oneFragSound" */
  QA_Q3_PRES_TWO_FRAG_SOUND, /* "twoFragSound" */
  QA_Q3_PRES_THREE_FRAG_SOUND, /* "threeFragSound" */
  QA_Q3_PRES_PLAYER_STATE_SOUND_COUNT
} qa_q3_pres_player_state_sound;

typedef enum qa_q3_pres_mission_player_state_sound {
  QA_Q3_PRES_HIT_SOUND_HIGH_ARMOR, /* "hitSoundHighArmor" */
  QA_Q3_PRES_HIT_SOUND_LOW_ARMOR, /* "hitSoundLowArmor" */
  QA_Q3_PRES_FIRST_IMPRESSIVE_SOUND, /* "firstImpressiveSound" */
  QA_Q3_PRES_FIRST_EXCELLENT_SOUND, /* "firstExcellentSound" */
  QA_Q3_PRES_FIRST_HUMILIATION_SOUND, /* "firstHumiliationSound" */
  QA_Q3_PRES_MISSION_PLAYER_STATE_SOUND_COUNT
} qa_q3_pres_mission_player_state_sound;

typedef enum qa_q3_pres_reward_medal {
  QA_Q3_PRES_MEDAL_CAPTURE, /* "medalCapture" */
  QA_Q3_PRES_MEDAL_IMPRESSIVE, /* "medalImpressive" */
  QA_Q3_PRES_MEDAL_EXCELLENT, /* "medalExcellent" */
  QA_Q3_PRES_MEDAL_GAUNTLET, /* "medalGauntlet" */
  QA_Q3_PRES_MEDAL_DEFEND, /* "medalDefend" */
  QA_Q3_PRES_MEDAL_ASSIST, /* "medalAssist" */
  QA_Q3_PRES_REWARD_MEDAL_COUNT
} qa_q3_pres_reward_medal;
const char *qa_q3_pres_player_state_sound_name(qa_q3_pres_player_state_sound sound);
const char *qa_q3_pres_mission_player_state_sound_name(qa_q3_pres_mission_player_state_sound sound);
const char *qa_q3_pres_reward_medal_name(qa_q3_pres_reward_medal medal);
/* TS WeaponHudReader result. */
typedef struct qa_q3_pres_weapon_hud_reading {
  bool has_status;
  qa_weapon_hud_status status;
  qa_arsenal_ammo_warning warning; /* "none" | "low" | "empty" */
} qa_q3_pres_weapon_hud_reading;
typedef struct qa_q3_pres_player_state_events {
  void *ctx;
  void (*entity_event)(void *ctx, qa_q3_pres_client_entity *entity, qa_vec3 position);
  void (*pain_event)(void *ctx, qa_q3_pres_client_entity *entity, double health);
} qa_q3_pres_player_state_events;
/* TS PlayerStateHost: mission_sounds read only for missionpack.
 * weapon_hud NULL is the TS absent optional. */
typedef struct qa_q3_pres_player_state_host {
  qa_q3_base_product product;
  qa_pcm_sound *mission_sounds[QA_Q3_PRES_MISSION_PLAYER_STATE_SOUND_COUNT]; /* borrowed, nullable */
  void *weapon_hud_ctx;
  qa_q3_pres_weapon_hud_reading (*weapon_hud)(void *ctx);
  qa_q3_pres_client_game_static_state *static_state;
  qa_q3_pres_player_state_events events;
  qa_pcm_sound *sounds[QA_Q3_PRES_PLAYER_STATE_SOUND_COUNT]; /* borrowed, nullable */
  const qa_q3_pres_scene_shader *medals[QA_Q3_PRES_REWARD_MEDAL_COUNT]; /* nullable */
  bool show_miss;
  void *ctx;
  void (*start_local_sound)(void *ctx, qa_pcm_sound *sound, double channel);
  void (*add_buffered_sound)(void *ctx, qa_pcm_sound *sound);
  void (*print)(void *ctx, const char *message);
} qa_q3_pres_player_state_host;
typedef struct qa_q3_pres_player_state_runtime {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_player_state_host host;
} qa_q3_pres_player_state_runtime;
/* Product mismatch aborts with the TS Error text. */
void qa_q3_pres_player_state_runtime_init(qa_q3_pres_player_state_runtime *runtime,
    qa_q3_pres_client_game_state *state, qa_q3_pres_player_state_host host);
void qa_q3_pres_player_state_runtime_check_ammo(qa_q3_pres_player_state_runtime *runtime);
void qa_q3_pres_player_state_runtime_damage_feedback(qa_q3_pres_player_state_runtime *runtime,
    double yaw_byte, double pitch_byte, double damage);
void qa_q3_pres_player_state_runtime_respawn(qa_q3_pres_player_state_runtime *runtime);
void qa_q3_pres_player_state_runtime_check_playerstate_events(qa_q3_pres_player_state_runtime *runtime,
    qa_q3_base_source_player_state *current, const qa_q3_base_source_player_state *previous);
void qa_q3_pres_player_state_runtime_check_changed_predictable_events(
    qa_q3_pres_player_state_runtime *runtime, const qa_q3_base_source_player_state *ps);
void qa_q3_pres_player_state_runtime_check_local_sounds(qa_q3_pres_player_state_runtime *runtime,
    const qa_q3_base_source_player_state *ps, const qa_q3_base_source_player_state *previous);
void qa_q3_pres_player_state_runtime_transition_player_state(qa_q3_pres_player_state_runtime *runtime,
    qa_q3_base_source_player_state *current, qa_q3_base_source_player_state *previous);
#endif
