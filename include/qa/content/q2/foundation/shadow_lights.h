/* Port of src/content/q2/foundation/shadow-lights.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_SHADOW_LIGHTS_H
#define QA_CONTENT_Q2_FOUNDATION_SHADOW_LIGHTS_H
#include "qa/content/q2/foundation/host.h"
typedef struct qa_q2_shadow_light_state {
  qa_actor_id actor; qa_vec3 origin, color; bool visible;
  double radius, intensity, resolution, fade_start, fade_end, lightstyle;
  bool has_cone; qa_vec3 cone_direction; double cone_cos_half_angle;
} qa_q2_shadow_light_state;
extern const qa_q2_use QA_Q2_DYNAMIC_LIGHT_USE;
void qa_q2_dynamic_light_use_fn(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a);
void qa_q2_spawn_shadow_light(qa_q2_entity *e, qa_q2_game_services *g);
void qa_q2_emit_shadow_light(qa_q2_entity *e, qa_q2_game_services *g);
void qa_q2_emit_shadow_lights(qa_q2_game_services *g);
#endif
