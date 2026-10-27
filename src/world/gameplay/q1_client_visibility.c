/* Port of src/world/gameplay/q1-client-visibility.ts. GPL-2.0-or-later. */
#include "qa/world/gameplay/q1_client_visibility.h"

#include <stdlib.h>

#include "qa/foundation/support.h"

struct qa_q1_client_visibility {
  int64_t max_clients;
  void *client_ctx;
  qa_q1_visibility_client (*client)(void *ctx, int64_t slot);
  qa_q1_client_visibility_scene scene;
  qa_q1_client_visibility_state state;
  double maximum_cluster;
};

qa_q1_client_visibility *qa_q1_client_visibility_create(
    int64_t max_clients, void *client_ctx,
    qa_q1_visibility_client (*client)(void *ctx, int64_t slot),
    qa_q1_client_visibility_scene visibility) {
  if (max_clients < 1 || max_clients > QA_MAX_SAFE_INTEGER)
    qa_range_error("Q1 visibility requires reserved client slots");
  qa_q1_client_visibility *value = calloc(1, sizeof *value);
  if (value == NULL) return NULL;
  value->max_clients = max_clients;
  value->client_ctx = client_ctx;
  value->client = client;
  value->scene = visibility;
  value->state.last_check_slot = 0;
  value->state.last_check_time = 0;
  value->state.has_checked_cluster = false;
  double maximum = -1;
  for (size_t index = 0; index < visibility.leaf_count; index++) {
    double cluster = visibility.leaf_cluster(visibility.ctx, (double)index);
    if (cluster > maximum) maximum = cluster;
  }
  value->maximum_cluster = maximum;
  return value;
}
void qa_q1_client_visibility_destroy(qa_q1_client_visibility *visibility) { free(visibility); }

static double eye_cluster(qa_q1_client_visibility *visibility, qa_q1_client_eye eye,
                          qa_numeric_operations numeric) {
  qa_vec3 point;
  point.x = numeric.add(numeric.ctx, eye.origin.x, eye.view_offset.x);
  point.y = numeric.add(numeric.ctx, eye.origin.y, eye.view_offset.y);
  point.z = numeric.add(numeric.ctx, eye.origin.z, eye.view_offset.z);
  return visibility->scene.leaf_cluster(visibility->scene.ctx,
                                        visibility->scene.point_leaf(visibility->scene.ctx, point));
}

bool qa_q1_client_visibility_check(qa_q1_client_visibility *visibility, qa_q1_client_eye observer,
                                   double time_seconds, qa_numeric_operations numeric,
                                   qa_actor_id *out) {
  if (time_seconds - visibility->state.last_check_time >= 0.1) {
    int64_t slot_now = (int64_t)visibility->state.last_check_slot;
    int64_t previous = slot_now < 1 ? 1 : slot_now > visibility->max_clients
                                              ? visibility->max_clients : slot_now;
    int64_t slot = previous == visibility->max_clients ? 1 : previous + 1;
    qa_q1_visibility_client client = visibility->client(visibility->client_ctx, slot);
    for (;;) {
      if (slot == previous || (!client.free && !(client.health <= 0) && !client.notarget)) break;
      slot = slot == visibility->max_clients ? 1 : slot + 1;
      client = visibility->client(visibility->client_ctx, slot);
    }
    qa_q1_client_eye client_eye = {client.origin, client.view_offset};
    visibility->state.last_check_slot = (double)slot;
    visibility->state.last_check_time = time_seconds;
    visibility->state.has_checked_cluster = true;
    visibility->state.checked_cluster = eye_cluster(visibility, client_eye, numeric);
  }
  qa_q1_visibility_client client =
      visibility->client(visibility->client_ctx, (int64_t)visibility->state.last_check_slot);
  if (client.free || client.health <= 0 || !visibility->state.has_checked_cluster) return false;
  double observer_cluster = eye_cluster(visibility, observer, numeric);
  if (!visibility->scene.cluster_visible(visibility->scene.ctx,
                                         visibility->state.checked_cluster, observer_cluster,
                                         "pvs"))
    return false;
  if (!client.has_actor) return false;
  *out = client.actor;
  return true;
}

qa_q1_client_visibility_state qa_q1_client_visibility_capture(qa_q1_client_visibility *visibility) {
  return visibility->state;
}

void qa_q1_client_visibility_restore(qa_q1_client_visibility *visibility,
                                     const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "q1-client-visibility")) abort();
  qa_save_reader slot_reader, time_reader, cluster_reader;
  if (!qa_save_reader_field(&reader, "lastCheckSlot", &slot_reader)) abort();
  if (!qa_save_reader_field(&reader, "lastCheckTime", &time_reader)) abort();
  if (!qa_save_reader_field(&reader, "checkedCluster", &cluster_reader)) abort();
  double last_check_slot = qa_save_reader_integer_min(&slot_reader, 0);
  double last_check_time = qa_save_reader_finite(&time_reader);
  bool has_cluster = !qa_save_reader_is_null(&cluster_reader);
  double checked_cluster = 0;
  if (has_cluster) checked_cluster = qa_save_reader_integer_min(&cluster_reader, -1);
  if (last_check_slot > (double)visibility->max_clients || last_check_time < 0 ||
      (has_cluster && checked_cluster > visibility->maximum_cluster))
    qa_save_reader_fail(&reader, "visibility cache outside source range");
  if ((last_check_slot == 0) != !has_cluster || (last_check_slot == 0 && last_check_time != 0))
    qa_save_reader_fail(&reader, "invalid initial visibility cache");
  visibility->state.last_check_slot = last_check_slot;
  visibility->state.last_check_time = last_check_time;
  visibility->state.has_checked_cluster = has_cluster;
  visibility->state.checked_cluster = checked_cluster;
  qa_save_reader_destroy(&slot_reader);
  qa_save_reader_destroy(&time_reader);
  qa_save_reader_destroy(&cluster_reader);
  qa_save_reader_destroy(&reader);
}
