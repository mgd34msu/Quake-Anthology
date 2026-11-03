/* Port of src/compat/qc/message-routing.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/message_routing.h"
#include <string.h>
static bool actor_eq(qa_actor_id a, qa_actor_id b){return qa_same_actor(a, b);}
bool qa_compat_qc_receives_quakeworld_message(qa_actor_id actor, qa_compat_qc_message_destination d, qa_compat_qc_message_origin origin, qa_compat_qc_message_visibility visibility) {
  if (d.kind == QA_QC_DEST_CLIENT) return actor_eq(actor, d.actor);
  if (d.kind == QA_QC_DEST_SIGNON) return false;
  if (d.kind == QA_QC_DEST_BROADCAST) return true;
  if (d.visibility && strcmp(d.visibility, "all") == 0) return true;
  qa_vec3 point = origin.fn(origin.ctx);
  qa_vec3 delta = {point.x - d.origin.x, point.y - d.origin.y, point.z - d.origin.z};
  if (d.visibility && strcmp(d.visibility, "phs") == 0 && delta.x*delta.x + delta.y*delta.y + delta.z*delta.z <= 1024.0*1024.0) return true;
  qa_q1_client_visibility_scene scene = visibility.fn(visibility.ctx);
  void * value = scene.ctx;
  double value2 = scene.leaf_cluster(scene.ctx, scene.point_leaf(scene.ctx, d.origin));
  double value3 = scene.leaf_cluster(scene.ctx, scene.point_leaf(scene.ctx, point));
  return scene.cluster_visible(value, value2, value3, d.visibility);
}
