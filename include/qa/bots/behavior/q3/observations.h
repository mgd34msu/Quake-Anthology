/* Port of src/bots/behavior/q3/observations.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_OBSERVATIONS_H
#define QA_BOTS_BEHAVIOR_Q3_OBSERVATIONS_H
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_bot_entity_update {
  bool has_generation;
  int64_t generation;
  double type, flags;
  qa_vec3 origin, angles, old_origin, mins, maxs;
  double ground_entity, solid, model_index, model_index2, frame, event, event_parameter, powerups,
      weapon, legs_animation, torso_animation;
} qa_bot_entity_update;
typedef struct qa_aas_entity_info_bots_a {
  bool valid;
  int64_t number;
  bool has_generation;
  int64_t generation;
  double type, flags;
  qa_vec3 origin, angles, old_origin, mins, maxs, last_visible_origin;
  float last_update_time, update_interval;
  double ground_entity, solid, model_index, model_index2, frame, event, event_parameter, powerups,
      weapon, legs_animation, torso_animation;
} qa_aas_entity_info_bots_a;
/* TS emptyInfo (exported): the invalid, zeroed record for entity `number`. */
qa_aas_entity_info_bots_a qa_aas_entity_info_empty_bots_a(int64_t number);
typedef struct qa_bot_entity_observations {
  qa_aas_entity_info_bots_a *records;
  int64_t capacity;
} qa_bot_entity_observations;
bool qa_bot_entity_observations_init(qa_bot_entity_observations *o, int64_t capacity);
void qa_bot_entity_observations_destroy(qa_bot_entity_observations *o);
void qa_bot_entity_observations_invalidate(qa_bot_entity_observations *o);
void qa_bot_entity_observations_update(qa_bot_entity_observations *o, int64_t number,
                                       const qa_bot_entity_update *src, double time);
qa_aas_entity_info_bots_a qa_bot_entity_observations_info(const qa_bot_entity_observations *o,
                                                          int64_t number);
int64_t qa_bot_entity_observations_next_entity(const qa_bot_entity_observations *o, int64_t after);
#endif
