/* Port of src/app/bootstrap/q3-client/visibility.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_client/visibility.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct link_entry {
  int64_t number;
  qa_q3_visibility_link link;
  int64_t clusters[16];
} link_entry;
struct qa_app_q3_snapshot_selection {
  qa_q3_visible_entities visible;
  qa_q3_entity_state_record *records; /* per source row */
  int64_t *record_numbers;
  size_t record_count;
  link_entry *links;
  size_t link_count;
  qa_q3_entity_state_record *fallback;
  size_t fallback_count, fallback_capacity;
  qa_shared_scene_queries *queries;
  bool *row_linked;
  int64_t *row_flags, *row_single;
};
static void *xmalloc(size_t n) { void *p = malloc(n == 0 ? 1 : n); if (p == NULL) qa_range_error("Out of memory"); return p; }
static int64_t vis_point_leaf(void *ctx, qa_vec3 point) {
  qa_app_q3_snapshot_selection *s = ctx;
  return (int64_t)qa_shared_point_leaf(s->queries, point);
}
static int64_t vis_leaf_area(void *ctx, int64_t leaf) {
  qa_app_q3_snapshot_selection *s = ctx;
  return (int64_t)qa_shared_leaf_area(s->queries, (double)leaf);
}
static int64_t vis_leaf_cluster(void *ctx, int64_t leaf) {
  qa_app_q3_snapshot_selection *s = ctx;
  return (int64_t)qa_shared_leaf_cluster(s->queries, (double)leaf);
}
static bool vis_areas_connected(void *ctx, int64_t first, int64_t second) {
  qa_app_q3_snapshot_selection *s = ctx;
  return qa_shared_areas_connected(s->queries, (double)first, (double)second);
}
static int64_t vis_write_area_bits(void *ctx, uint8_t *bytes, int64_t area) {
  qa_app_q3_snapshot_selection *s = ctx;
  size_t length = 0;
  const uint8_t *bits = qa_shared_area_bits(s->queries, (double)area, &length);
  /* The landed bindings pass a fixed-size mask; the TS length guard
   * compares against the caller's byte array, whose size the C
   * signature does not carry (source MAX_MAP_AREAS/8 = 32 in the
   * landed selection). */
  if (length > 32) qa_range_error("Selected world exceeds source Q3 area mask storage");
  for (size_t i = 0; i < length; i++) bytes[i] = (uint8_t)(bytes[i] | bits[i]);
  return (int64_t)length;
}
static int64_t vis_pvs_byte(void *ctx, int64_t cluster, int64_t index) {
  qa_app_q3_snapshot_selection *s = ctx;
  int64_t byte = 0;
  for (int bit = 0; bit < 8; bit++)
    if (qa_shared_cluster_visible(s->queries, (double)cluster, (double)(index * 8 + bit), "pvs"))
      byte |= 1LL << bit;
  return byte;
}
static qa_q3_visibility_entity vis_entity(void *ctx, int64_t number) {
  qa_app_q3_snapshot_selection *s = ctx;
  for (size_t i = 0; i < s->record_count; i++) {
    if (s->record_numbers[i] == number) {
      qa_q3_visibility_entity out = {&s->records[i], s->row_linked[i], s->row_flags[i], s->row_single[i]};
      return out;
    }
  }
  if (s->fallback_count == s->fallback_capacity) {
    size_t cap = s->fallback_capacity == 0 ? 8 : s->fallback_capacity * 2;
    qa_q3_entity_state_record *next = realloc(s->fallback, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    s->fallback = next; s->fallback_capacity = cap;
  }
  qa_q3_entity_state_record *state = &s->fallback[s->fallback_count++];
  qa_q3_entity_state_init(state);
  state->number = (int32_t)number;
  qa_q3_visibility_entity out = {state, false, 0, 0};
  return out;
}
static bool vis_link(void *ctx, int64_t number, qa_q3_visibility_link *out) {
  qa_app_q3_snapshot_selection *s = ctx;
  for (size_t i = 0; i < s->link_count; i++)
    if (s->links[i].number == number) { *out = s->links[i].link; return true; }
  return false;
}
qa_app_q3_snapshot_selection *qa_app_select_application_q3_snapshot(
    const qa_q3_player_state_record *player,
    const qa_app_q3_snapshot_source_entity *entities, size_t entity_row_count,
    qa_shared_scene_queries *queries,
    bool (*bounds)(void *ctx, double number, qa_bounds *out), void *bounds_ctx,
    double leaf_count, void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_app_q3_snapshot_selection *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->queries = queries;
  s->records = xmalloc(entity_row_count * sizeof *s->records);
  s->record_numbers = xmalloc(entity_row_count * sizeof *s->record_numbers);
  s->links = xmalloc(entity_row_count * sizeof *s->links);
  /* Row metadata for vis_entity, kept in a parallel scratch via the
   * record's own fields plus these arrays. */
  bool *row_linked = xmalloc(entity_row_count * sizeof *row_linked);
  int64_t *row_flags = xmalloc(entity_row_count * sizeof *row_flags);
  int64_t *row_single = xmalloc(entity_row_count * sizeof *row_single);
  int64_t entity_count = 0;
  for (size_t r = 0; r < entity_row_count; r++) {
    const qa_app_q3_snapshot_source_entity *row = &entities[r];
    int64_t number = row->state->number;
    if (number + 1 > entity_count) entity_count = number + 1;
    qa_q3_entity_state_init(&s->records[s->record_count]);
    qa_q3_entity_state_record_copy_from(&s->records[s->record_count], row->state);
    s->record_numbers[s->record_count] = number;
    row_linked[s->record_count] = row->linked;
    row_flags[s->record_count] = (int64_t)row->server_flags;
    row_single[s->record_count] = (int64_t)row->single_client;
    s->record_count++;
    if (!row->linked) continue;
    qa_bounds box;
    if (!bounds(bounds_ctx, (double)number, &box))
      qa_range_error("Linked Q3 source entity %lld lost its shared body bounds", (long long)number);
    qa_leaf_query_result leaf_result = qa_shared_box_leaves(queries, box, leaf_count);
    size_t all_count = leaf_result.leaves.count;
    size_t leaf_take = all_count < 128 ? all_count : 128;
    int64_t areanum = -1, areanum2 = -1;
    for (size_t i = 0; i < leaf_take; i++) {
      double area = qa_shared_leaf_area(queries, leaf_result.leaves.values[i]);
      if (area == -1) continue;
      if (areanum != -1 && areanum != (int64_t)area) areanum2 = (int64_t)area;
      else areanum = (int64_t)area;
    }
    link_entry *link = &s->links[s->link_count];
    link->number = number;
    size_t cluster_count = 0;
    int64_t last_cluster = 0;
    for (size_t i = 0; i < leaf_take; i++) {
      double cluster = qa_shared_leaf_cluster(queries, leaf_result.leaves.values[i]);
      if (cluster == -1) continue;
      link->clusters[cluster_count++] = (int64_t)cluster;
      if (cluster_count == 16) {
        if (all_count > 0)
          last_cluster = (int64_t)qa_shared_leaf_cluster(queries, leaf_result.leaves.values[all_count - 1]);
        break;
      }
    }
    link->link.areanum = areanum;
    link->link.areanum2 = areanum2;
    link->link.clusters = link->clusters;
    link->link.cluster_count = cluster_count;
    link->link.last_cluster = last_cluster;
    s->link_count++;
  }
  s->row_linked = row_linked;
  s->row_flags = row_flags;
  s->row_single = row_single;
  qa_q3_visibility_bindings bindings;
  bindings.collision.point_leafnum = vis_point_leaf;
  bindings.collision.leaf_area = vis_leaf_area;
  bindings.collision.leaf_cluster = vis_leaf_cluster;
  bindings.collision.write_area_bits = vis_write_area_bits;
  bindings.collision.pvs_byte_at = vis_pvs_byte;
  bindings.collision.areas_connected = vis_areas_connected;
  bindings.collision.ctx = s;
  bindings.entity_count = entity_count;
  bindings.dead = false;
  bindings.entity = vis_entity;
  bindings.link = vis_link;
  bindings.ctx = s;
  bindings.print = print;
  bindings.print_ctx = print_ctx;
  qa_q3_select_snapshot_entities(player, &bindings, &s->visible);
  return s;
}
const qa_q3_visible_entities *qa_app_q3_snapshot_selection_visible(const qa_app_q3_snapshot_selection *selection) {
  return &selection->visible;
}
void qa_app_q3_snapshot_selection_destroy(qa_app_q3_snapshot_selection *selection) {
  if (selection == NULL) return;
  qa_q3_visible_entities_destroy(&selection->visible);
  free(selection->records);
  free(selection->record_numbers);
  free(selection->links);
  free(selection->fallback);
  free(selection->row_linked);
  free(selection->row_flags);
  free(selection->row_single);
  free(selection);
}
