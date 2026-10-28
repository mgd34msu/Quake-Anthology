/* Port of src/content/q3/presentation/client-info.ts. GPL-2.0-or-later.
 * Source clientInfo_t from id Software's code/cgame/cg_local.h.
 * String fields are owned char*, NULL is TS "". PcmSound / SceneSkin /
 * SceneShader handles are borrowed pointers, NULL is TS null. */
#ifndef QA_CONTENT_Q3_PRESENTATION_CLIENT_INFO_H
#define QA_CONTENT_Q3_PRESENTATION_CLIENT_INFO_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/foundation/animation_config.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/contracts/math.h"
enum { QA_Q3_PRES_CLIENT_INFO_SOUND_COUNT = 32 };
/* C-only support: replace an owned presentation string ("" / NULL -> NULL).
 * False only on OOM; on failure the old value is kept. */
bool qa_q3_pres_set_string(char **slot, const char *value);
typedef struct qa_q3_pres_client_info {
  bool info_valid;
  char *name; /* owned, NULL = "" */
  qa_q3_base_team team;
  double bot_skill;
  qa_vec3 color1, color2;
  double score, location, health, armor, cur_weapon, handicap, wins, losses, team_task;
  bool team_leader;
  double powerups, medkit_usage_time, invulnerability_start_time, invulnerability_stop_time, breath_puff_time;
  char *model_name, *skin_name, *head_model_name, *head_skin_name, *red_team, *blue_team; /* owned, NULL = "" */
  bool deferred, new_anims, fixed_legs, fixed_torso;
  qa_vec3 head_offset;
  qa_q3_player_footsteps footsteps;
  qa_q3_player_gender gender;
  qa_q3_pres_scene_model legs_model, torso_model, head_model;
  const qa_q3_pres_scene_skin *legs_skin, *torso_skin, *head_skin; /* nullable */
  const qa_q3_pres_scene_shader *model_icon; /* nullable */
  /* TS animations: FLAG_STAND2RUN + 1 == 37 stable cells. */
  qa_q3_animation_cell animations[QA_Q3_TOTAL_ANIMATION_COUNT];
  qa_pcm_sound *sounds[QA_Q3_PRES_CLIENT_INFO_SOUND_COUNT]; /* borrowed, nullable */
} qa_q3_pres_client_info;
void qa_q3_pres_client_info_init(qa_q3_pres_client_info *info);
void qa_q3_pres_client_info_destroy(qa_q3_pres_client_info *info);
/* TS setAnimations: present[i] false is TS null (tolerated only at
 * TORSO_NEGATIVE + 1). Wrong count / missing cell aborts with the exact
 * TS RangeError text. Cells are assigned in place, never reseated. */
void qa_q3_pres_client_info_set_animations(qa_q3_pres_client_info *info,
    const qa_q3_animation *animations, const bool *present, size_t count);
/* TS copyFrom (returns this). False only on OOM duplicating strings. */
bool qa_q3_pres_client_info_copy_from(qa_q3_pres_client_info *info, const qa_q3_pres_client_info *source);
/* TS `implements PlayerAnimationTarget` view over the live cells. */
qa_q3_player_animation_target qa_q3_pres_client_info_animation_target(qa_q3_pres_client_info *info);
#endif
