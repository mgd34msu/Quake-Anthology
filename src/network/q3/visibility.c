/* Port of src/network/q3/visibility.ts. GPL-2.0-or-later. */
#include "qa/network/q3/visibility.h"
#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
typedef struct {
  const qa_q3_visibility_bindings *host;
  int64_t client_num;
  int64_t selected[256]; size_t count;
  bool *visited; int64_t visited_len; /* TS Set<number> over every examined number */
  uint8_t area_bits[32];
  int64_t area_bytes;
} sel_t;
static bool sel_visited(const sel_t *s, int64_t number) {
  return number >= 0 && number < s->visited_len && s->visited[number];
}
static void sel_add(sel_t *s, int64_t number) {
  if (sel_visited(s, number)) return;
  if (number >= 0 && number < s->visited_len) s->visited[number] = true;
  if (s->count == 256) return;
  if (number < 0 || number >= 1024) qa_range_error("Server snapshot entity outside source storage");
  s->selected[s->count++] = number;
}
static void visible_from(sel_t *s, qa_vec3 origin) {
  const qa_q3_visibility_bindings *host = s->host;
  if (host->dead) return;
  const qa_q3_visibility_world *col = &host->collision;
  int64_t leaf = col->point_leafnum(col->ctx, origin);
  int64_t area = col->leaf_area(col->ctx, leaf);
  s->area_bytes = col->write_area_bits(col->ctx, s->area_bits, area);
  int64_t pvs_cluster = col->leaf_cluster(col->ctx, leaf);
  for (int64_t number = 0; number < host->entity_count; number++) {
    qa_q3_visibility_entity entity = host->entity(host->ctx, number);
    qa_q3_entity_state_record *state = entity.state;
    int64_t flags = entity.flags;
    if (!entity.linked) continue;
    if (state->number != number) {
      if (host->print) host->print(host->print_ctx, "FIXING ENT->S.NUMBER!!!\n");
      state->number = (double)number;
    }
    if (flags & QA_Q3_SVF_NO_CLIENT) continue;
    if ((flags & QA_Q3_SVF_SINGLE_CLIENT) && entity.single_client != s->client_num) continue;
    if ((flags & QA_Q3_SVF_NOT_SINGLE_CLIENT) && entity.single_client == s->client_num) continue;
    if (flags & QA_Q3_SVF_CLIENT_MASK) {
      if (s->client_num >= 32) qa_common_error(QA_COMMON_ERROR_DROP, "SVF_CLIENTMASK: cientNum > 32\n");
      if (~(int32_t)entity.single_client & (1 << s->client_num)) continue;
    }
    if (sel_visited(s, number)) continue;
    if (flags & QA_Q3_SVF_BROADCAST) { sel_add(s, number); continue; }
    qa_q3_visibility_link link;
    if (!host->link(host->ctx, number, &link)) continue;
    if (!col->areas_connected(col->ctx, area, link.areanum) && !col->areas_connected(col->ctx, area, link.areanum2)) continue;
    if (link.cluster_count == 0) continue;
    int64_t cluster = 0; size_t index = 0;
    for (; index < link.cluster_count; index++) {
      cluster = link.clusters[index];
      if ((col->pvs_byte_at(col->ctx, pvs_cluster, cluster >> 3) & (1 << (cluster & 7))) != 0) break;
    }
    if (index == link.cluster_count) {
      if (!link.last_cluster) continue;
      for (; cluster <= link.last_cluster; cluster++)
        if ((col->pvs_byte_at(col->ctx, pvs_cluster, cluster >> 3) & (1 << (cluster & 7))) != 0) break;
      if (cluster == link.last_cluster) continue; /* original equality, incl. overflow cluster */
    }
    sel_add(s, number);
    if (flags & QA_Q3_SVF_PORTAL) {
      if (state->generic1 != 0 && !isnan(state->generic1)) { /* JS truthiness */
        qa_vec3 delta = qa_sub3(state->origin, origin);
        double distance = (float)((float)state->generic1 * (float)state->generic1);
        if (qa_dot3(delta, delta) > distance) continue;
      }
      visible_from(s, state->origin2);
    }
  }
}
static int cmp_i64(const void *a, const void *b) {
  int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
  return (x > y) - (x < y);
}
bool qa_q3_select_snapshot_entities(const qa_q3_player_state_record *player, const qa_q3_visibility_bindings *host, qa_q3_visible_entities *out) {
  memset(out, 0, sizeof *out);
  if (player->client_num < 0 || player->client_num >= 1024)
    qa_common_error(QA_COMMON_ERROR_DROP, "SV_SvEntityForGentity: bad gEnt");
  sel_t s; memset(&s, 0, sizeof s);
  s.host = host; s.client_num = player->client_num;
  /* Loop numbers run 0..entityCount-1, so this covers every number TS's Set can hold. */
  s.visited_len = host->entity_count > 1024 ? host->entity_count : 1024;
  if ((uint64_t)s.visited_len > SIZE_MAX / sizeof *s.visited) return false;
  s.visited = calloc((size_t)s.visited_len, sizeof *s.visited);
  if (!s.visited) return false;
  s.visited[player->client_num] = true;
  /* Host collision/entity callbacks and the drops above raise; release the set. */
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, s.visited);
  qa_vec3 eye = { player->origin.x, player->origin.y, (float)(player->origin.z + (float)player->viewheight) };
  visible_from(&s, eye);
  qa_cleanup_pop(&guard);
  free(s.visited); s.visited = NULL;
  qa_js_sort(s.selected, s.count, sizeof(int64_t), cmp_i64);
  if (s.area_bytes < 0 || s.area_bytes > 32) qa_range_error("Source area bits exceed 32 bytes");
  for (int i = 0; i < 32; i++) s.area_bits[i] = (uint8_t)(s.area_bits[i] ^ 255);
  uint8_t *area_mask = malloc(s.area_bytes ? (size_t)s.area_bytes : 1);
  qa_q3_entity_state_record **entities = malloc((s.count ? s.count : 1) * sizeof *entities);
  if (!area_mask || !entities) { free(area_mask); free(entities); return false; }
  memcpy(area_mask, s.area_bits, (size_t)s.area_bytes);
  qa_cleanup mask_guard, entities_guard;
  qa_cleanup_push(&mask_guard, free, area_mask);
  qa_cleanup_push(&entities_guard, free, entities);
  for (size_t i = 0; i < s.count; i++) entities[i] = host->entity(host->ctx, s.selected[i]).state;
  qa_cleanup_pop(&entities_guard);
  qa_cleanup_pop(&mask_guard);
  out->area_mask = area_mask; out->area_mask_len = (size_t)s.area_bytes;
  out->entities = entities; out->entity_count = s.count;
  return true;
}
void qa_q3_visible_entities_destroy(qa_q3_visible_entities *v) { free(v->area_mask); free(v->entities); v->area_mask = NULL; v->entities = NULL; }
