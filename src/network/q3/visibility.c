/* Port of src/network/q3/visibility.ts. GPL-2.0-or-later. */
#include "qa/network/q3/visibility.h"
#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
  const qa_q3_visibility_bindings *host;
  int64_t client_num;
  int64_t selected[1024]; size_t count;
  bool visited[1024];
  uint8_t area_bits[32];
  int64_t area_bytes;
} sel_t;
static void sel_add(sel_t *s, int64_t number) {
  if (number >= 0 && number < 1024 && s->visited[number]) return;
  if (number >= 0 && number < 1024) s->visited[number] = true;
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
      state->number = (int32_t)number;
    }
    if (flags & QA_Q3_SVF_NO_CLIENT) continue;
    if ((flags & QA_Q3_SVF_SINGLE_CLIENT) && entity.single_client != s->client_num) continue;
    if ((flags & QA_Q3_SVF_NOT_SINGLE_CLIENT) && entity.single_client == s->client_num) continue;
    if (flags & QA_Q3_SVF_CLIENT_MASK) {
      if (s->client_num >= 32) qa_common_error(QA_COMMON_ERROR_DROP, "SVF_CLIENTMASK: cientNum > 32\n");
      if (~(int32_t)entity.single_client & (1 << s->client_num)) continue;
    }
    if (number >= 0 && number < 1024 && s->visited[number]) continue;
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
      if (state->generic1) {
        qa_vec3 delta = qa_sub3(state->origin, origin);
        double distance = (float)((float)state->generic1 * (float)state->generic1);
        distance = (float)distance;
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
  s.visited[player->client_num] = true;
  qa_vec3 eye = { player->origin.x, player->origin.y, (float)(player->origin.z + (float)player->viewheight) };
  eye.z = (float)eye.z;
  visible_from(&s, eye);
  qsort(s.selected, s.count, sizeof(int64_t), cmp_i64);
  if (s.area_bytes < 0 || s.area_bytes > 32) qa_range_error("Source area bits exceed 32 bytes");
  for (int i = 0; i < 32; i++) s.area_bits[i] = (uint8_t)(s.area_bits[i] ^ 255);
  out->area_mask = malloc(s.area_bytes ? (size_t)s.area_bytes : 1);
  out->entities = malloc((s.count ? s.count : 1) * sizeof *out->entities);
  if (!out->area_mask || !out->entities) { free(out->area_mask); free(out->entities); return false; }
  memcpy(out->area_mask, s.area_bits, (size_t)s.area_bytes);
  out->area_mask_len = (size_t)s.area_bytes;
  for (size_t i = 0; i < s.count; i++) out->entities[i] = host->entity(host->ctx, s.selected[i]).state;
  out->entity_count = s.count;
  return true;
}
void qa_q3_visible_entities_destroy(qa_q3_visible_entities *v) { free(v->area_mask); free(v->entities); v->area_mask = NULL; v->entities = NULL; }
