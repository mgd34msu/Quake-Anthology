/* Port of src/content/q2/foundation/monsters/alternate-fly.ts.
 * Q2AlternateFlySteeringState is AlternateFlyState & Pick<MonsterState>;
 * here qa_q2m_state* serves directly. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MONSTERS_ALTERNATE_FLY_H
#define QA_CONTENT_Q2_FOUNDATION_MONSTERS_ALTERNATE_FLY_H
#include "qa/content/q2/foundation/monsters/types.h"
typedef struct qa_q2m_alternate_fly_input {
  qa_q2m_state *state;
  qa_body_state body;
  const qa_body_state *enemy; /* nullable */
  const qa_vec3 *goal;        /* nullable */
  double flags, now, frame_seconds;
} qa_q2m_alternate_fly_input;
typedef struct qa_q2m_alternate_fly_services {
  qa_q2_rerelease_random *random;
  void *ctx;
  qa_trace_result (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds, double mask);
  double (*point_contents)(void *ctx, qa_vec3 point);
  bool (*visible_enemy)(void *ctx);
} qa_q2m_alternate_fly_services;
typedef enum qa_q2m_fly_result_kind { QA_Q2M_FLY_FALLBACK, QA_Q2M_FLY_STEERED } qa_q2m_fly_result_kind;
typedef struct qa_q2m_alternate_fly_result {
  qa_q2m_fly_result_kind kind;
  qa_vec3 velocity;
  bool has_pitch; double pitch;
} qa_q2m_alternate_fly_result;
qa_q2m_alternate_fly_result qa_q2m_steer_alternate_fly(qa_q2m_alternate_fly_input input,
                                                       const qa_q2m_alternate_fly_services *services);
bool qa_q2m_alternate_fly_step(qa_q2m_context *context);
#endif
