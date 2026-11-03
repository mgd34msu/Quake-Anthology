/* Port of src/content/q2/foundation/weapons/projection.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/weapons/projection.h"
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/js.h"

static qa_vec3 v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }

double qa_q2w_actor_shot_mask(qa_q2_game_services *game, bool players_collide) {
  if (game->options.edition == QA_Q2_EDITION_CLASSIC) return QA_Q2W_SHOT_MASK;
  return players_collide ? QA_Q2W_PROJECTILE_MASK
                         : (double)(qa_js_to_int32(qa_js_to_uint32(QA_Q2W_PROJECTILE_MASK) & ~qa_js_to_uint32(QA_Q2W_PLAYER_CONTENTS)));
}

qa_q2w_projection qa_q2w_project_actor(qa_actor_id actor, qa_q2_game_services *game, qa_q2w_actor_view view, qa_vec3 angles, qa_vec3 offset) {
  qa_angle_vectors axes = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE);
  double side = view.hand == QA_Q2W_HAND_LEFT ? -offset.y : view.hand == QA_Q2W_HAND_CENTER ? 0 : offset.y;
  qa_body_state_result body = qa_shared_body_table_read(game->host->bodies, actor);
  if (!body.has_value) qa_raise("Error", 0, 0, "Q2 projection requires a shared actor body");
  qa_vec3 origin = body.value.origin;
  if (game->options.edition == QA_Q2_EDITION_CLASSIC) {
    qa_q2w_projection p;
    p.start = qa_add3_double(qa_add3_double(qa_add3_double(origin, qa_scale3_double(axes.forward, offset.x)),
                                            qa_scale3_double(axes.right, side)),
                             v3(0, 0, view.view_height + offset.z));
    p.direction = axes.forward;
    return p;
  }
  qa_vec3 eye = qa_add3_double(origin, v3(0, 0, view.view_height));
  qa_vec3 start = qa_add3_double(qa_add3_double(qa_add3_double(eye, qa_scale3_double(axes.forward, offset.x)),
                                                qa_scale3_double(axes.right, side)),
                                 qa_scale3_double(axes.up, offset.z));
  qa_q2_trace_request req;
  memset(&req, 0, sizeof req);
  req.start = eye;
  req.end = qa_add3_double(eye, qa_scale3_double(axes.forward, 8192));
  req.ignore = &actor;
  req.mask = (double)(qa_js_to_int32(qa_js_to_uint32(qa_q2w_actor_shot_mask(game, view.players_collide)) & ~(uint32_t)0x4000000));
  qa_trace_result trace = game->host->trace(game->host->ctx, req);
  bool close = strcmp(trace.kind, "q1") != 0
               && (qa_js_to_uint32(trace.contents) & (0x2000000u | qa_js_to_uint32(QA_Q2W_PLAYER_CONTENTS))) != 0
               && trace.fraction * 8192 < 128;
  qa_q2w_projection p;
  p.start = start;
  p.direction = trace.start_solid || close ? axes.forward
                                           : qa_normalize3_double_hypot(qa_sub3_double(trace.end, start));
  return p;
}
