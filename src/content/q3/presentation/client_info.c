/* Port of src/content/q3/presentation/client-info.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/client_info.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

bool qa_q3_pres_set_string(char **slot, const char *value) {
  if (value == NULL || value[0] == '\0') {
    free(*slot);
    *slot = NULL;
    return true;
  }
  size_t length = strlen(value) + 1;
  char *copy = malloc(length);
  if (copy == NULL) return false;
  memcpy(copy, value, length);
  free(*slot);
  *slot = copy;
  return true;
}

void qa_q3_pres_client_info_init(qa_q3_pres_client_info *info) {
  memset(info, 0, sizeof *info);
  info->team = QA_Q3_BASE_TEAM_FREE;
  info->footsteps = QA_Q3_FOOTSTEPS_NORMAL;
  info->gender = QA_Q3_GENDER_MALE;
  info->legs_model = qa_q3_pres_default_model;
  info->torso_model = qa_q3_pres_default_model;
  info->head_model = qa_q3_pres_default_model;
}

void qa_q3_pres_client_info_destroy(qa_q3_pres_client_info *info) {
  free(info->name);
  free(info->model_name);
  free(info->skin_name);
  free(info->head_model_name);
  free(info->head_skin_name);
  free(info->red_team);
  free(info->blue_team);
  memset(info, 0, sizeof *info);
}

void qa_q3_pres_client_info_set_animations(qa_q3_pres_client_info *info,
    const qa_q3_animation *animations, const bool *present, size_t count) {
  if (count != QA_Q3_TOTAL_ANIMATION_COUNT) {
    qa_range_error("Client animation table has the wrong length");
  }
  for (size_t index = 0; index < count; index++) {
    bool has_source = present == NULL || present[index];
    if (!has_source && index == (size_t)QA_Q3_ANIM_TORSO_NEGATIVE + 1) continue;
    if (!has_source) qa_range_error("Missing client animation cell %zu", index);
    info->animations[index] = animations[index];
  }
}

bool qa_q3_pres_client_info_copy_from(qa_q3_pres_client_info *info, const qa_q3_pres_client_info *source) {
  /* Object.assign(this, fields) for every field except animations, whose
   * cells keep their identity via set_animations. */
  if (!qa_q3_pres_set_string(&info->name, source->name)) return false;
  if (!qa_q3_pres_set_string(&info->model_name, source->model_name)) return false;
  if (!qa_q3_pres_set_string(&info->skin_name, source->skin_name)) return false;
  if (!qa_q3_pres_set_string(&info->head_model_name, source->head_model_name)) return false;
  if (!qa_q3_pres_set_string(&info->head_skin_name, source->head_skin_name)) return false;
  if (!qa_q3_pres_set_string(&info->red_team, source->red_team)) return false;
  if (!qa_q3_pres_set_string(&info->blue_team, source->blue_team)) return false;
  info->info_valid = source->info_valid;
  info->team = source->team;
  info->bot_skill = source->bot_skill;
  info->color1 = source->color1;
  info->color2 = source->color2;
  info->score = source->score;
  info->location = source->location;
  info->health = source->health;
  info->armor = source->armor;
  info->cur_weapon = source->cur_weapon;
  info->handicap = source->handicap;
  info->wins = source->wins;
  info->losses = source->losses;
  info->team_task = source->team_task;
  info->team_leader = source->team_leader;
  info->powerups = source->powerups;
  info->medkit_usage_time = source->medkit_usage_time;
  info->invulnerability_start_time = source->invulnerability_start_time;
  info->invulnerability_stop_time = source->invulnerability_stop_time;
  info->breath_puff_time = source->breath_puff_time;
  info->deferred = source->deferred;
  info->new_anims = source->new_anims;
  info->fixed_legs = source->fixed_legs;
  info->fixed_torso = source->fixed_torso;
  info->head_offset = source->head_offset;
  info->footsteps = source->footsteps;
  info->gender = source->gender;
  info->legs_model = source->legs_model;
  info->torso_model = source->torso_model;
  info->head_model = source->head_model;
  info->legs_skin = source->legs_skin;
  info->torso_skin = source->torso_skin;
  info->head_skin = source->head_skin;
  info->model_icon = source->model_icon;
  memcpy(info->sounds, source->sounds, sizeof info->sounds);
  qa_q3_pres_client_info_set_animations(info, source->animations, NULL, QA_Q3_TOTAL_ANIMATION_COUNT);
  return true;
}

qa_q3_player_animation_target qa_q3_pres_client_info_animation_target(qa_q3_pres_client_info *info) {
  qa_q3_player_animation_target target;
  target.footsteps = info->footsteps;
  target.head_offset = info->head_offset;
  target.gender = info->gender;
  target.fixed_legs = info->fixed_legs;
  target.fixed_torso = info->fixed_torso;
  target.animations = info->animations;
  target.animation_count = QA_Q3_TOTAL_ANIMATION_COUNT;
  return target;
}
