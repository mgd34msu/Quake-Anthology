/* Port of src/render/scene/models/replacements.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_MODELS_REPLACEMENTS_H
#define QA_RENDER_SCENE_MODELS_REPLACEMENTS_H

#include <stdbool.h>

#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"

/* TS ModelReplacementPolicy. distance: has_distance false is "source". */
typedef struct qa_model_replacement_policy {
  bool q1_enhanced, q2_load, q2_use;
  double q2_distance;
  bool has_distance;
  double distance;
} qa_model_replacement_policy;

extern const qa_model_replacement_policy qa_default_model_replacement_policy;

bool qa_load_model_replacement(qa_game_family family,
                               qa_model_replacement_policy policy);
/* False is TS null; on true *out is a shallow entity copy. */
bool qa_replacement_entity(const qa_scene_entity *entity, qa_scene_entity *out);
/* purpose: "view" | "shadow" (TS default "view"). Shallow copy. */
qa_scene_entity qa_select_model_entity(const qa_scene_entity *entity,
                                       qa_vec3 camera,
                                       qa_model_replacement_policy policy,
                                       const char *purpose);

#endif
