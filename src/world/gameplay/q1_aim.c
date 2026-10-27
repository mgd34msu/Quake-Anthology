/* Port of src/world/gameplay/q1-aim.ts. GPL-2.0-or-later. */
#include "qa/world/gameplay/q1_aim.h"

#include "qa/core/math.h"

qa_vec3 qa_aim_q1(qa_vec3 origin, qa_vec3 forward, double threshold, qa_q1_aim_targets targets) {
  const qa_mutable_vector_math *math = qa_q1_donor_math();
  qa_vec3 start = origin;
  /* TS Math.fround(origin.z + 20). */
  start.z = (double)(float)(origin.z + 20);
  qa_vec3 end = {0, 0, 0};
  qa_mvm_vector_ma(math, qa_vec3_access_plain(&start), 2048, qa_vec3_access_plain(&forward),
                   qa_vec3_access_plain(&end));
  qa_actor_id hit;
  if (targets.trace(targets.ctx, start, end, &hit) && targets.eligible(targets.ctx, hit))
    return forward;
  double best = threshold;
  bool has_selected = false;
  qa_vec3 selected = {0, 0, 0};
  for (size_t i = 0; i < targets.target_count; i++) {
    qa_actor_id actor = targets.targets[i];
    if (!targets.eligible(targets.ctx, actor)) continue;
    qa_body_state_result body = targets.body(targets.ctx, actor);
    if (!body.has_value) continue;
    /* TS Math.fround staged component sums. */
    end.x = (double)(float)(body.value.origin.x +
                            0.5 * (body.value.bounds.min.x + body.value.bounds.max.x));
    end.y = (double)(float)(body.value.origin.y +
                            0.5 * (body.value.bounds.min.y + body.value.bounds.max.y));
    end.z = (double)(float)(body.value.origin.z +
                            0.5 * (body.value.bounds.min.z + body.value.bounds.max.z));
    qa_vec3 direction = {0, 0, 0};
    qa_mvm_vector_subtract(math, qa_vec3_access_plain(&end), qa_vec3_access_plain(&start),
                           qa_vec3_access_plain(&direction));
    (void)qa_mvm_vector_normalize(math, qa_vec3_access_plain(&direction));
    double distance = qa_mvm_dot_product(math, qa_vec3_access_plain(&direction),
                                         qa_vec3_access_plain(&forward));
    if (distance < best) continue;
    if (targets.trace(targets.ctx, start, end, &hit) && qa_same_actor(hit, actor)) {
      best = distance;
      selected = body.value.origin;
      has_selected = true;
    }
  }
  if (!has_selected) return forward;
  qa_vec3 delta = {0, 0, 0};
  qa_mvm_vector_subtract(math, qa_vec3_access_plain(&selected), qa_vec3_access_plain(&origin),
                         qa_vec3_access_plain(&delta));
  qa_mvm_vector_scale(math, qa_vec3_access_plain(&forward),
                      qa_mvm_dot_product(math, qa_vec3_access_plain(&delta),
                                         qa_vec3_access_plain(&forward)),
                      qa_vec3_access_plain(&end));
  end.z = delta.z;
  (void)qa_mvm_vector_normalize(math, qa_vec3_access_plain(&end));
  return end;
}
