/* Port of src/content/q3/foundation/events.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_FOUNDATION_EVENTS_H
#define QA_CONTENT_Q3_FOUNDATION_EVENTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/foundation/animation_config.h"
#include "qa/content/q3/foundation/character.h"
#include "qa/content/q3/foundation/player_pose.h"

typedef enum qa_q3_character_presentation_effect_kind {
  QA_Q3_EFFECT_CUSTOM_SOUND, QA_Q3_EFFECT_SOUND, QA_Q3_EFFECT_FOOTSTEP,
  QA_Q3_EFFECT_JUMP_PAD_SMOKE, QA_Q3_EFFECT_TELEPORT, QA_Q3_EFFECT_WEAPON_FIRE,
  QA_Q3_EFFECT_OUT_OF_AMMO, QA_Q3_EFFECT_GIB_PLAYER, QA_Q3_EFFECT_STOP_LOOPING_SOUND,
  QA_Q3_EFFECT_SOURCE_EVENT
} qa_q3_character_presentation_effect_kind;
/* TS material: PlayerFootsteps | "metal" | "splash". */
typedef enum qa_q3_footstep_material {
  QA_Q3_FOOTSTEP_MATERIAL_NORMAL, QA_Q3_FOOTSTEP_MATERIAL_BOOT, QA_Q3_FOOTSTEP_MATERIAL_FLESH,
  QA_Q3_FOOTSTEP_MATERIAL_MECH, QA_Q3_FOOTSTEP_MATERIAL_ENERGY, QA_Q3_FOOTSTEP_MATERIAL_METAL,
  QA_Q3_FOOTSTEP_MATERIAL_SPLASH
} qa_q3_footstep_material;
const char *qa_q3_footstep_material_name(qa_q3_footstep_material material);
typedef struct qa_q3_character_presentation_effect {
  qa_q3_character_presentation_effect_kind kind;
  const char *channel; /* "auto"|"voice"|"body" */
  char text[96]; /* custom-sound name / sound path */
  qa_q3_footstep_material material; double variant;
  double radius, duration_milliseconds;
  const char *direction; /* "in"|"out" */
  qa_q3_character_event event; /* source-event */
} qa_q3_character_presentation_effect;
typedef struct qa_q3_character_event_options {
  bool local, footsteps, predict_steps; double source_flags;
} qa_q3_character_event_options;
typedef struct qa_q3_character_event_presenter {
  qa_q3_player_pose_state *pose; qa_q3_player_footsteps footsteps;
  void *random_ctx; double (*rand_fn)(void *ctx); /* TS {rand(): number} */
  double step_time, step_change, land_time, land_change, muzzle_flash_time;
} qa_q3_character_event_presenter;
void qa_q3_character_event_presenter_init(qa_q3_character_event_presenter *presenter,
    qa_q3_player_pose_state *pose, qa_q3_player_footsteps footsteps, double (*rand_fn)(void*), void *random_ctx);
/* Writes up to capacity effects; returns the effect count. */
size_t qa_q3_character_event_presenter_pain(qa_q3_character_event_presenter *presenter,
    double time_milliseconds, double health, qa_q3_character_presentation_effect *out, size_t capacity);
size_t qa_q3_character_event_presenter_event(qa_q3_character_event_presenter *presenter,
    qa_q3_character_event source, qa_q3_character_event_options options,
    qa_q3_character_presentation_effect *out, size_t capacity);
#endif
