/* Port of src/app/bootstrap/simulation/native-pmove-contacts.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_pmove_contacts.h"
#include <math.h>
#include <stdlib.h>
#include "qa/foundation/support.h"

bool qa_app_sim_reported_ground_plane(const qa_app_sim_native_slice_contact *contacts, size_t count, qa_plane *out) {
  for (size_t index = count; index-- > 0;) {
    if (contacts[index].has_plane && contacts[index].plane.normal.z >= QA_APP_SIM_WALKABLE_NORMAL_Z) { *out = contacts[index].plane; return true; }
  }
  return false;
}

qa_plane qa_app_sim_overlap_plane(qa_vec3 origin, qa_bounds bounds, qa_bounds other) {
  qa_vec3 min = {origin.x + bounds.min.x, origin.y + bounds.min.y, origin.z + bounds.min.z};
  qa_vec3 max = {origin.x + bounds.max.x, origin.y + bounds.max.y, origin.z + bounds.max.z};
  struct { double depth; qa_vec3 normal; double distance; } candidates[6] = {
    {fabs(min.x - other.max.x), {1, 0, 0}, other.max.x},
    {fabs(max.x - other.min.x), {-1, 0, 0}, -other.min.x},
    {fabs(min.y - other.max.y), {0, 1, 0}, other.max.y},
    {fabs(max.y - other.min.y), {0, -1, 0}, -other.min.y},
    {fabs(min.z - other.max.z), {0, 0, 1}, other.max.z},
    {fabs(max.z - other.min.z), {0, 0, -1}, -other.min.z},
  };
  size_t best = 0;
  /* A NaN depth never compares less, so the earlier candidate stays (TS `<`). */
  for (size_t index = 0; index < 6; index++) if (candidates[index].depth < candidates[best].depth) best = index;
  qa_plane plane = {candidates[best].normal, candidates[best].distance};
  return plane;
}

/* TS Set<number>.has: SameValueZero. */
static bool same_slot(double a, double b) { return a == b || (isnan(a) && isnan(b)); }
typedef struct touch_list { qa_app_sim_native_pmove_touch *items; size_t count, capacity; } touch_list;
static void add(touch_list *list, qa_app_sim_native_slot_of_fn slot_of, void *ctx, qa_actor_id actor, qa_plane plane) {
  double slot;
  if (!slot_of(ctx, actor, &slot)) return;
  for (size_t index = 0; index < list->count; index++) if (same_slot(list->items[index].slot, slot)) return;
  if (list->count == list->capacity) {
    size_t capacity = list->capacity ? list->capacity * 2 : 8;
    qa_app_sim_native_pmove_touch *grown = realloc(list->items, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    list->items = grown; list->capacity = capacity;
  }
  list->items[list->count].slot = slot; list->items[list->count].plane = plane; list->count++;
}
qa_app_sim_native_pmove_touch *qa_app_sim_pmove_touch_list(const qa_app_sim_native_slice_contact *reported, size_t reported_count,
    const qa_app_sim_native_overlap *overlapped, size_t overlapped_count, qa_app_sim_native_slot_of_fn slot_of, void *ctx,
    size_t *out_count) {
  touch_list list = {NULL, 0, 0};
  for (size_t index = 0; index < reported_count; index++)
    if (reported[index].has_actor && reported[index].has_plane) add(&list, slot_of, ctx, reported[index].actor, reported[index].plane);
  for (size_t index = 0; index < overlapped_count; index++) add(&list, slot_of, ctx, overlapped[index].actor, overlapped[index].plane);
  *out_count = list.count;
  return list.items;
}
