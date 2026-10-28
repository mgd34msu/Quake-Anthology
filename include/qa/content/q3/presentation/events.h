/* Port of src/content/q3/presentation/events.ts. GPL-2.0-or-later.
 * Entity events from id Software's code/cgame/cg_event.c. TS async
 * presentEvent/prepareWeapon/voiceChatLocal are synchronous host calls.
 * Mirrored types (TS owners unported): ClientWeaponRuntime pick +
 * ImpactSound/BulletHit (weapons.ts) and ClientEffects picks
 * (effects.ts) as the *_mirror structs below; replace when they land. */
#ifndef QA_CONTENT_Q3_PRESENTATION_EVENTS_H
#define QA_CONTENT_Q3_PRESENTATION_EVENTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/foundation/animation_config.h"
#include "qa/content/q3/presentation/client_info.h"
#include "qa/content/q3/presentation/local_entities.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/math.h"
typedef enum qa_q3_pres_client_event_sound {
  QA_Q3_PRES_USE_NOTHING_SOUND, /* "useNothingSound" */
  QA_Q3_PRES_MEDKIT_SOUND, /* "medkitSound" */
  QA_Q3_PRES_LAND_SOUND, /* "landSound" */
  QA_Q3_PRES_JUMP_PAD_SOUND, /* "jumpPadSound" */
  QA_Q3_PRES_WATR_IN_SOUND, /* "watrInSound" */
  QA_Q3_PRES_WATR_OUT_SOUND, /* "watrOutSound" */
  QA_Q3_PRES_WATR_UN_SOUND, /* "watrUnSound" */
  QA_Q3_PRES_N_HEALTH_SOUND, /* "n_healthSound" */
  QA_Q3_PRES_SELECT_SOUND, /* "selectSound" */
  QA_Q3_PRES_TELE_IN_SOUND, /* "teleInSound" */
  QA_Q3_PRES_TELE_OUT_SOUND, /* "teleOutSound" */
  QA_Q3_PRES_RESPAWN_SOUND, /* "respawnSound" */
  QA_Q3_PRES_HGRENB1A_SOUND, /* "hgrenb1aSound" */
  QA_Q3_PRES_HGRENB2A_SOUND, /* "hgrenb2aSound" */
  QA_Q3_PRES_CAPTURE_YOUR_TEAM_SOUND, /* "captureYourTeamSound" */
  QA_Q3_PRES_CAPTURE_OPPONENT_SOUND, /* "captureOpponentSound" */
  QA_Q3_PRES_RETURN_YOUR_TEAM_SOUND, /* "returnYourTeamSound" */
  QA_Q3_PRES_RETURN_OPPONENT_SOUND, /* "returnOpponentSound" */
  QA_Q3_PRES_BLUE_FLAG_RETURNED_SOUND, /* "blueFlagReturnedSound" */
  QA_Q3_PRES_RED_FLAG_RETURNED_SOUND, /* "redFlagReturnedSound" */
  QA_Q3_PRES_ENEMY_TOOK_YOUR_FLAG_SOUND, /* "enemyTookYourFlagSound" */
  QA_Q3_PRES_YOUR_TEAM_TOOK_ENEMY_FLAG_SOUND, /* "yourTeamTookEnemyFlagSound" */
  QA_Q3_PRES_YOUR_BASE_IS_UNDER_ATTACK_SOUND, /* "yourBaseIsUnderAttackSound" */
  QA_Q3_PRES_RED_SCORED_SOUND, /* "redScoredSound" */
  QA_Q3_PRES_BLUE_SCORED_SOUND, /* "blueScoredSound" */
  QA_Q3_PRES_RED_LEADS_SOUND, /* "redLeadsSound" */
  QA_Q3_PRES_BLUE_LEADS_SOUND, /* "blueLeadsSound" */
  QA_Q3_PRES_TEAMS_TIED_SOUND, /* "teamsTiedSound" */
  QA_Q3_PRES_QUAD_SOUND, /* "quadSound" */
  QA_Q3_PRES_PROTECT_SOUND, /* "protectSound" */
  QA_Q3_PRES_REGEN_SOUND, /* "regenSound" */
  QA_Q3_PRES_GIB_SOUND, /* "gibSound" */
  QA_Q3_PRES_CLIENT_EVENT_SOUND_COUNT
} qa_q3_pres_client_event_sound;
typedef enum qa_q3_pres_mission_event_sound {
  QA_Q3_PRES_USE_INVULNERABILITY_SOUND, /* "useInvulnerabilitySound" */
  QA_Q3_PRES_SCOUT_SOUND, /* "scoutSound" */
  QA_Q3_PRES_GUARD_SOUND, /* "guardSound" */
  QA_Q3_PRES_DOUBLER_SOUND, /* "doublerSound" */
  QA_Q3_PRES_AMMOREGEN_SOUND, /* "ammoregenSound" */
  QA_Q3_PRES_WSTBIMPL_SOUND, /* "wstbimplSound" */
  QA_Q3_PRES_WSTBIMPM_SOUND, /* "wstbimpmSound" */
  QA_Q3_PRES_WSTBIMPD_SOUND, /* "wstbimpdSound" */
  QA_Q3_PRES_WSTBACTV_SOUND, /* "wstbactvSound" */
  QA_Q3_PRES_YOUR_TEAM_TOOK_THE_FLAG_SOUND, /* "yourTeamTookTheFlagSound" */
  QA_Q3_PRES_ENEMY_TOOK_THE_FLAG_SOUND, /* "enemyTookTheFlagSound" */
  QA_Q3_PRES_KAMIKAZE_FAR_SOUND, /* "kamikazeFarSound" */
  QA_Q3_PRES_MISSION_EVENT_SOUND_COUNT
} qa_q3_pres_mission_event_sound;
const char *qa_q3_pres_client_event_sound_name(qa_q3_pres_client_event_sound sound);
const char *qa_q3_pres_mission_event_sound_name(qa_q3_pres_mission_event_sound sound);
/* Footstep sound banks keyed by PlayerFootsteps then splash/metal,
 * matching Q3_FOOTSTEP_PATHS order in character_resources.h. */
enum { QA_Q3_PRES_FOOTSTEP_SPLASH = 5, QA_Q3_PRES_FOOTSTEP_METAL = 6, QA_Q3_PRES_FOOTSTEP_KIND_COUNT = 7 };
typedef struct qa_q3_pres_client_event_media {
  qa_pcm_sound *sounds[QA_Q3_PRES_CLIENT_EVENT_SOUND_COUNT]; /* borrowed, nullable */
  qa_pcm_sound *footsteps[QA_Q3_PRES_FOOTSTEP_KIND_COUNT][4]; /* borrowed, nullable */
  qa_pcm_sound *const *game_sounds; size_t game_sound_count; /* borrowed, nullable entries */
  const qa_q3_pres_scene_shader *smoke_puff_shader; /* nullable */
} qa_q3_pres_client_event_media;
typedef struct qa_q3_pres_client_event_options {
  qa_q3_base_game_type game_type;
  bool debug_events, footsteps, autoswitch, demo_playback, no_predict, synchronous_clients;
  bool single_player_active, camera_orbit;
} qa_q3_pres_client_event_options;
/* Mirror: TS ImpactSound / BulletHit (weapons.ts, unported). */
typedef enum qa_q3_pres_impact_sound_mirror {
  QA_Q3_PRES_IMPACT_DEFAULT = 0, QA_Q3_PRES_IMPACT_METAL = 1, QA_Q3_PRES_IMPACT_FLESH = 2
} qa_q3_pres_impact_sound_mirror;
typedef struct qa_q3_pres_bullet_hit_mirror {
  bool is_wall; /* TS kind: "wall" | "flesh" */
  qa_vec3 normal;   /* wall */
  double entity_num; /* flesh */
} qa_q3_pres_bullet_hit_mirror;
/* Mirror: TS Pick<ClientWeaponRuntime, ...> (weapons.ts, unported). */
typedef struct qa_q3_pres_client_weapon_runtime_mirror {
  void *ctx;
  void (*out_of_ammo_change)(void *ctx);
  void (*fire_weapon)(void *ctx, qa_q3_pres_client_entity *entity);
  void (*missile_hit_player)(void *ctx, double weapon, qa_vec3 origin, qa_vec3 direction, double entity_num);
  void (*missile_hit_wall)(void *ctx, double weapon, double client_num, qa_vec3 origin, qa_vec3 direction,
                           qa_q3_pres_impact_sound_mirror sound_type);
  void (*rail_trail)(void *ctx, double client_num, qa_vec3 start, qa_vec3 end);
  void (*bullet)(void *ctx, qa_vec3 end, double source, qa_q3_pres_bullet_hit_mirror hit);
  void (*shotgun_fire)(void *ctx, qa_q3_base_entity_state *state);
} qa_q3_pres_client_weapon_runtime_mirror;
/* Mirror: TS Pick<ClientEffects, "smokePuff"|"spawnEffect"|"gibPlayer"|
 * "scorePlum"> (effects.ts, unported). */
typedef struct qa_q3_pres_event_effects_mirror {
  void *ctx;
  qa_q3_pres_local_entity *(*smoke_puff)(void *ctx, qa_q3_pres_smoke_puff_options_mirror options);
  qa_q3_pres_local_entity *(*spawn_effect)(void *ctx, qa_vec3 origin);
  void (*gib_player)(void *ctx, qa_vec3 origin);
  void (*score_plum)(void *ctx, double client, qa_vec3 origin, double score);
} qa_q3_pres_event_effects_mirror;
/* Mirror: TS mission Pick<ClientEffects, ...> (effects.ts, unported). */
typedef struct qa_q3_pres_mission_effects_mirror {
  void *ctx;
  qa_q3_pres_local_entity *(*kamikaze_effect)(void *ctx, qa_vec3 origin);
  void (*obelisk_explode)(void *ctx, qa_vec3 origin);
  void (*obelisk_pain)(void *ctx, qa_vec3 origin);
  qa_q3_pres_local_entity *(*invulnerability_impact)(void *ctx, qa_vec3 origin, qa_vec3 angles);
  qa_q3_pres_local_entity *(*invulnerability_juiced)(void *ctx, qa_vec3 origin);
  qa_q3_pres_local_entity *(*lightning_bolt_beam)(void *ctx, qa_vec3 start, qa_vec3 end);
} qa_q3_pres_mission_effects_mirror;
/* TS presentEvent source closure. */
typedef void (*qa_q3_pres_event_source_fn)(void *ctx);
/* TS ClientEventHost: mission fields are read only for missionpack.
 * start_sound's TS `origin: Vec3 | null` is has_origin + origin. */
typedef struct qa_q3_pres_client_event_host {
  qa_q3_base_product product;
  qa_pcm_sound *mission_sounds[QA_Q3_PRES_MISSION_EVENT_SOUND_COUNT]; /* borrowed, nullable */
  qa_q3_pres_mission_effects_mirror mission_effects;
  void *mission_ctx;
  void (*start_local_sound)(void *ctx, qa_pcm_sound *sound, double channel);
  void (*voice_chat_local)(void *ctx, double mode, bool voice_only, double client_num, double color,
                           const char *command);
  qa_q3_pres_client_event_media media;
  qa_q3_pres_client_event_options options;
  qa_q3_game_random *random;
  void *entities_ctx;
  void (*set_entity_sound_position)(void *ctx, qa_q3_pres_client_entity *entity);
  void (*beam)(void *ctx, qa_q3_pres_client_entity *entity);
  qa_q3_pres_client_weapon_runtime_mirror weapons;
  qa_q3_pres_event_effects_mirror effects;
  void *ctx;
  void (*present_event)(void *ctx, qa_q3_pres_client_entity *entity, qa_vec3 position,
                        qa_q3_pres_event_source_fn source, void *source_ctx);
  void (*prepare_weapon)(void *ctx, double weapon);
  qa_q3_pres_client_info *(*client_info)(void *ctx, double number);
  void *client_info_ctx;
  const char *(*player_name)(void *ctx, double number); /* nullable */
  const char *(*sound_config_string)(void *ctx, double index);
  qa_pcm_sound *(*custom_sound)(void *ctx, double client_num, const char *name);
  qa_pcm_sound *(*register_sound)(void *ctx, const char *path, bool compressed);
  void (*start_sound)(void *ctx, bool has_origin, qa_vec3 origin, double entity_num, double channel,
                      qa_pcm_sound *sound);
  void (*stop_looping_sound)(void *ctx, double entity_num);
  void (*add_buffered_sound)(void *ctx, qa_pcm_sound *sound);
  void (*print)(void *ctx, const char *message);
  void (*center_print)(void *ctx, const char *message, double y, double char_width);
} qa_q3_pres_client_event_host;
/* TS placeString. Malloc'd result (free by caller), NULL only on OOM. */
char *qa_q3_pres_place_string(double rank);
typedef struct qa_q3_pres_client_event_runtime {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_event_host host;
} qa_q3_pres_client_event_runtime;
/* Product mismatch aborts with the TS Error text. */
void qa_q3_pres_client_event_runtime_init(qa_q3_pres_client_event_runtime *runtime,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_event_host host);
void qa_q3_pres_client_event_runtime_pain_event(qa_q3_pres_client_event_runtime *runtime,
    qa_q3_pres_client_entity *entity, double health);
void qa_q3_pres_client_event_runtime_entity_event(qa_q3_pres_client_event_runtime *runtime,
    qa_q3_pres_client_entity *entity, qa_vec3 position);
void qa_q3_pres_client_event_runtime_check_events(qa_q3_pres_client_event_runtime *runtime,
    qa_q3_pres_client_entity *entity);
#endif
