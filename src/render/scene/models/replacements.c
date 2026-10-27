/* Port of src/render/scene/models/replacements.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/replacements.h"

#include <string.h>

#include "qa/core/math.h"

const qa_model_replacement_policy qa_default_model_replacement_policy = {
    true, true, true, 2048, false, 0};

bool qa_load_model_replacement(qa_game_family family,
                               qa_model_replacement_policy policy) {
  return family == QA_GAME_FAMILY_Q1
             ? policy.q1_enhanced
             : family == QA_GAME_FAMILY_Q2 && policy.q2_load;
}

static bool is_alias(const qa_scene_entity *entity) {
  return entity->model.kind == QA_DECODED_MODEL_Q1_ALIAS ||
         entity->model.kind == QA_DECODED_MODEL_Q2_ALIAS;
}

bool qa_replacement_entity(const qa_scene_entity *entity, qa_scene_entity *out) {
  if (!is_alias(entity)) return false;
  const qa_model_replacement *replacement = entity->model.replacement;
  if (replacement == NULL) return false;
  *out = *entity;
  out->resource = replacement->resource;
  out->model.kind = QA_DECODED_MODEL_MD5;
  out->model.as.md5 = &replacement->model;
  out->model.replacement = NULL;
  return true;
}

qa_scene_entity qa_select_model_entity(const qa_scene_entity *entity,
                                       qa_vec3 camera,
                                       qa_model_replacement_policy policy,
                                       const char *purpose) {
  if (!is_alias(entity)) return *entity;
  bool q1 = entity->model.kind == QA_DECODED_MODEL_Q1_ALIAS;
  if (q1 ? !policy.q1_enhanced : !policy.q2_load || !policy.q2_use)
    return *entity;
  double distance = policy.has_distance
                        ? policy.distance
                        : (!q1 ? policy.q2_distance : 0);
  bool view = purpose == NULL || strcmp(purpose, "view") == 0;
  if (view && distance > 0 &&
      qa_length3(qa_sub3(entity->transform.origin, camera)) > distance)
    return *entity;
  qa_scene_entity replacement;
  return qa_replacement_entity(entity, &replacement) ? replacement : *entity;
}
