/* Port of src/bots/navigation/estimate-aas.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/estimate_aas.h"
#include "qa/bots/navigation/graph.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void at_check(int64_t index, size_t count) {
  if (index < 0 || index >= (int64_t)count)
    qa_range_error("Missing AAS estimate metadata %lld", (long long)index);
}

int64_t qa_aas_estimate_area_time(int64_t presence, int64_t flags, qa_vec3 start, qa_vec3 end) {
  float x = (float)((float)start.x - (float)end.x);
  float y = (float)((float)start.y - (float)end.y);
  float z = (float)((float)start.z - (float)end.z);
  float xx = (float)((double)x * x), yy = (float)((double)y * y), zz = (float)((double)z * z);
  float sum = (float)((float)((double)xx + yy) + (double)zz);
  float distance = (float)sqrt((double)sum);
  float factor = (presence & 2) == 0 ? (float)1.3 : (flags & 4) != 0 ? 1.0f : (float)0.33;
  double value = trunc((double)(float)((double)distance * factor));
  if (!isfinite(value) || value > 0x7fffffff) qa_range_error("AAS estimate distance exceeds source int range");
  int64_t v = (int64_t)value;
  if (v < 1) v = 1;
  return v & 0xffff;
}

/* --- topology --- */
static bool build_topology(qa_aas_navigation_estimates *e) {
  const qa_aas_asset *asset = e->asset;
  size_t areas = asset->area_count;
  e->incoming = calloc(areas ? areas : 1, sizeof *e->incoming);
  if (!e->incoming) return false;
  size_t *caps = calloc(areas ? areas : 1, sizeof *caps);
  if (!caps) return false;
  bool ok = true;
  for (int64_t area = 1; ok && area < (int64_t)areas; area++) {
    at_check(area, asset->setting_count);
    const qa_aas_setting_rec *settings = &asset->settings[area];
    int64_t limit = settings->reach_count < 128 ? settings->reach_count : 128;
    for (int64_t offset = 0; offset < limit && ok; offset++) {
      int64_t reach = settings->first_reach + offset;
      at_check(reach, asset->reachability_count);
      int64_t target = asset->reachability[reach].area;
      at_check(target, areas);
      qa_aas_estimate_incoming_list *list = &e->incoming[target];
      if (list->count == caps[target]) {
        size_t c = caps[target] ? caps[target] * 2 : 4;
        qa_aas_estimate_incoming *p = realloc(list->v, c * sizeof *p);
        if (!p) { ok = false; break; }
        list->v = p; caps[target] = c;
      }
      list->v[list->count++] = (qa_aas_estimate_incoming){area, reach};
    }
  }
  free(caps);
  if (!ok) return false;
  for (size_t i = 0; i < areas; i++) {
    qa_aas_estimate_incoming_list *list = &e->incoming[i];
    for (size_t a = 0, b = list->count ? list->count - 1 : 0; a < b; a++, b--) {
      qa_aas_estimate_incoming tmp = list->v[a]; list->v[a] = list->v[b]; list->v[b] = tmp;
    }
  }
  e->crossings = calloc(asset->setting_count ? asset->setting_count : 1, sizeof *e->crossings);
  e->crossing_rows = calloc(asset->setting_count ? asset->setting_count : 1, sizeof *e->crossing_rows);
  if (!e->crossings || !e->crossing_rows) return false;
  for (size_t area = 0; area < asset->setting_count && ok; area++) {
    const qa_aas_setting_rec *settings = &asset->settings[area];
    size_t rows = settings->reach_count > 0 ? (size_t)settings->reach_count : 0;
    e->crossing_rows[area] = rows;
    e->crossings[area] = calloc(rows ? rows : 1, sizeof *e->crossings[area]);
    if (!e->crossings[area]) return false;
    at_check((int64_t)area, areas);
    size_t in_count = e->incoming[area].count;
    for (size_t offset = 0; offset < rows && ok; offset++) {
      int64_t ridx = settings->first_reach + (int64_t)offset;
      at_check(ridx, asset->reachability_count);
      qa_vec3 start = asset->reachability[ridx].start;
      uint16_t *row = calloc(in_count ? in_count : 1, sizeof *row);
      if (!row) return false;
      e->crossings[area][offset] = row;
      for (size_t k = 0; k < in_count; k++) {
        int64_t lr = e->incoming[area].v[k].reach;
        at_check(lr, asset->reachability_count);
        row[k] = (uint16_t)qa_aas_estimate_area_time(settings->presence, settings->flags,
                                                     asset->reachability[lr].end, start);
      }
    }
  }
  if (!ok) return false;
  e->portal_maxima = calloc(asset->portal_count ? asset->portal_count : 1, sizeof *e->portal_maxima);
  if (!e->portal_maxima) return false;
  for (size_t p = 0; p < asset->portal_count; p++) {
    int64_t area = asset->portals[p].area;
    at_check(area, asset->setting_count);
    int64_t maximum = 0;
    for (size_t r = 0; r < e->crossing_rows[area]; r++) {
      at_check(area, asset->area_count);
      for (size_t k = 0; k < e->incoming[area].count; k++)
        if (e->crossings[area][r][k] > maximum) maximum = e->crossings[area][r][k];
    }
    e->portal_maxima[p] = maximum;
  }
  return true;
}

bool qa_aas_navigation_estimates_init(qa_aas_navigation_estimates *e, const qa_navigation_graph *graph,
                                      const qa_aas_asset *asset, void *ctx,
                                      qa_nav_estimate_eligibility allowed) {
  memset(e, 0, sizeof *e);
  e->graph = graph; e->asset = asset; e->ctx = ctx; e->allowed = allowed;
  if (!build_topology(e)) { qa_aas_navigation_estimates_destroy(e); return false; }
  return true;
}
static void free_caches(qa_aas_navigation_estimates *e) {
  for (size_t i = 0; i < e->cache_count; i++) { free(e->caches[i].times); free(e->caches[i].reaches); }
  e->cache_count = 0; e->cache_bytes = 0;
}
void qa_aas_navigation_estimates_destroy(qa_aas_navigation_estimates *e) {
  if (!e) return;
  if (e->incoming && e->asset) for (size_t i = 0; i < e->asset->area_count; i++) free(e->incoming[i].v);
  free(e->incoming);
  if (e->crossings && e->asset) {
    for (size_t a = 0; a < e->asset->setting_count; a++) {
      if (e->crossings[a]) for (size_t r = 0; r < e->crossing_rows[a]; r++) free(e->crossings[a][r]);
      free(e->crossings[a]);
    }
  }
  free(e->crossings); free(e->crossing_rows); free(e->portal_maxima);
  free_caches(e); free(e->caches);
  memset(e, 0, sizeof *e);
}
void qa_aas_navigation_estimates_invalidate(qa_aas_navigation_estimates *e) { free_caches(e); }

/* --- caches (LRU by key, 16 MiB byte cap) --- */
static qa_aas_estimate_cache *cache_find(qa_aas_navigation_estimates *e, const char *key) {
  for (size_t i = 0; i < e->cache_count; i++) {
    if (!strcmp(e->caches[i].key, key)) {
      qa_aas_estimate_cache hit = e->caches[i];
      memmove(&e->caches[i], &e->caches[i + 1], (e->cache_count - i - 1) * sizeof hit);
      e->caches[e->cache_count - 1] = hit;
      return &e->caches[e->cache_count - 1];
    }
  }
  return NULL;
}
static qa_aas_estimate_cache *cache_remember(qa_aas_navigation_estimates *e, const char *key, qa_aas_estimate_cache cache) {
  if (e->cache_count == e->cache_cap) {
    size_t c = e->cache_cap ? e->cache_cap * 2 : 8;
    qa_aas_estimate_cache *p = realloc(e->caches, c * sizeof *p);
    if (!p) { free(cache.times); free(cache.reaches); return NULL; }
    e->caches = p; e->cache_cap = c;
  }
  snprintf(cache.key, sizeof cache.key, "%s", key);
  e->caches[e->cache_count++] = cache;
  e->cache_bytes += (int64_t)(cache.count * 2 + cache.count);
  while (e->cache_bytes > 16 * 1024 * 1024 && e->cache_count > 1) {
    e->cache_bytes -= (int64_t)(e->caches[0].count * 2 + e->caches[0].count);
    free(e->caches[0].times); free(e->caches[0].reaches);
    memmove(&e->caches[0], &e->caches[1], (e->cache_count - 1) * sizeof *e->caches);
    e->cache_count--;
  }
  /* eviction may have shifted; the remembered cache is newest */
  return &e->caches[e->cache_count - 1];
}
static const qa_navigation_edge *edge_by_id(qa_aas_navigation_estimates *e, int64_t id) {
  for (size_t i = e->graph->edge_count; i-- > 0;) if (e->graph->edges[i].id == id) return &e->graph->edges[i];
  return NULL;
}
static int64_t cluster_area(qa_aas_navigation_estimates *e, int64_t cluster, int64_t area) {
  at_check(area, e->asset->setting_count);
  const qa_aas_setting_rec *settings = &e->asset->settings[area];
  if (settings->cluster > 0) return settings->cluster_area;
  at_check(-settings->cluster, e->asset->portal_count);
  const qa_aas_portal_rec *portal = &e->asset->portals[-settings->cluster];
  return portal->cluster_areas[portal->front_cluster == cluster ? 0 : 1];
}

typedef struct { int64_t area, cluster, time; const uint16_t *row; size_t row_len; bool queued; } Update;

static qa_aas_estimate_cache *area_cache(qa_aas_navigation_estimates *e, int64_t cluster, int64_t goal, int64_t flags) {
  char key[80];
  snprintf(key, sizeof key, "a:%lld:%lld:%lld", (long long)cluster, (long long)goal, (long long)flags);
  qa_aas_estimate_cache *hit = cache_find(e, key);
  if (hit) return hit;
  at_check(cluster, e->asset->cluster_count);
  int64_t count_i = e->asset->clusters[cluster].reachability_area_count;
  size_t count = count_i > 0 ? (size_t)count_i : 0;
  qa_aas_estimate_cache cache; memset(&cache, 0, sizeof cache);
  cache.count = count;
  cache.times = calloc(count ? count : 1, sizeof *cache.times);
  cache.reaches = calloc(count ? count : 1, sizeof *cache.reaches);
  if (!cache.times || !cache.reaches) { free(cache.times); free(cache.reaches); return NULL; }
  int64_t goal_index = cluster_area(e, cluster, goal);
  if (goal_index >= count_i) return cache_remember(e, key, cache);
  Update *updates = calloc(count ? count : 1, sizeof *updates);
  Update **queue = malloc((count + 1) * sizeof *queue);
  uint16_t *first_row = NULL;
  if (!updates || !queue) { free(updates); free(queue); free(cache.times); free(cache.reaches); return NULL; }
  at_check(goal, e->asset->area_count);
  size_t first_len = e->incoming[goal].count;
  first_row = calloc(first_len ? first_len : 1, sizeof *first_row);
  if (!first_row) { free(updates); free(queue); free(cache.times); free(cache.reaches); return NULL; }
  Update *first = &updates[goal_index];
  first->area = goal; first->time = 1; first->row = first_row; first->row_len = first_len;
  cache.times[goal_index] = 1;
  size_t qn = 0, qcap = count + 1; queue[qn++] = first;
  for (size_t cursor = 0; cursor < qn; cursor++) {
    Update *current = queue[cursor]; current->queued = false;
    at_check(current->area, e->asset->area_count);
    const qa_aas_estimate_incoming_list *incoming = &e->incoming[current->area];
    for (size_t ordinal = 0; ordinal < incoming->count; ordinal++) {
      qa_aas_estimate_incoming link = incoming->v[ordinal];
      at_check(link.reach, e->asset->reachability_count);
      const qa_aas_reach_rec *reach = &e->asset->reachability[link.reach];
      const qa_navigation_edge *edge = edge_by_id(e, link.reach);
      if (!edge) continue;
      if ((qa_aas_travel_flag(reach->travel_type) & ~flags) != 0) continue;
      if (!e->allowed(e->ctx, edge, true, flags)) continue;
      at_check(reach->area, e->asset->setting_count);
      const qa_aas_setting_rec *dest = &e->asset->settings[reach->area];
      if ((qa_aas_area_travel_flags(dest->contents, dest->flags) & ~flags) != 0) continue;
      at_check(link.area, e->asset->setting_count);
      const qa_aas_setting_rec *settings = &e->asset->settings[link.area];
      if (settings->cluster > 0 && settings->cluster != cluster) continue;
      int64_t index = cluster_area(e, cluster, link.area);
      if (index >= count_i) continue;
      if (index < 0) at_check(index, count);
      int64_t rowval = ordinal < current->row_len ? current->row[ordinal] : 0;
      if (ordinal >= current->row_len) at_check((int64_t)ordinal, current->row_len);
      int64_t time = (current->time + rowval + reach->travel_time) & 0xffff;
      int64_t previous = cache.times[index];
      if (previous != 0 && previous <= time) continue;
      cache.times[index] = (uint16_t)time;
      int64_t offset = link.reach - settings->first_reach;
      cache.reaches[index] = (uint8_t)offset;
      Update *next = &updates[index];
      next->area = link.area; next->time = time;
      at_check(link.area, e->asset->setting_count);
      if (offset < 0 || offset >= (int64_t)e->crossing_rows[link.area]) at_check(offset, e->crossing_rows[link.area]);
      next->row = e->crossings[link.area][offset];
      at_check(link.area, e->asset->area_count);
      next->row_len = e->incoming[link.area].count;
      if (!next->queued) {
        next->queued = true;
        if (qn == qcap) {
          Update **g = realloc(queue, qcap * 2 * sizeof *g);
          if (!g) { free(first_row); free(updates); free(queue); free(cache.times); free(cache.reaches); return NULL; }
          queue = g; qcap *= 2;
          /* updates/first pointers are into updates array, unaffected */
        }
        queue[qn++] = next;
      }
    }
  }
  free(first_row); free(updates); free(queue);
  return cache_remember(e, key, cache);
}

static qa_aas_estimate_cache *portal_cache(qa_aas_navigation_estimates *e, int64_t cluster, int64_t goal, int64_t flags) {
  char key[80];
  snprintf(key, sizeof key, "p:%lld:%lld", (long long)goal, (long long)flags);
  qa_aas_estimate_cache *hit = cache_find(e, key);
  if (hit) return hit;
  size_t count = e->asset->portal_count;
  qa_aas_estimate_cache cache; memset(&cache, 0, sizeof cache);
  cache.count = count;
  cache.times = calloc(count ? count : 1, sizeof *cache.times);
  cache.reaches = calloc(count ? count : 1, sizeof *cache.reaches);
  if (!cache.times || !cache.reaches) { free(cache.times); free(cache.reaches); return NULL; }
  Update *updates = calloc(count + 1, sizeof *updates);
  Update **queue = malloc((count + 2) * sizeof *queue);
  if (!updates || !queue) { free(updates); free(queue); free(cache.times); free(cache.reaches); return NULL; }
  Update *first = &updates[count];
  first->cluster = cluster; first->area = goal; first->time = 1;
  at_check(goal, e->asset->setting_count);
  int64_t goal_cluster = e->asset->settings[goal].cluster;
  if (goal_cluster < 0) {
    if (-goal_cluster >= (int64_t)count) at_check(-goal_cluster, count);
    cache.times[-goal_cluster] = 1;
  }
  size_t qn = 0, qcap = count + 2; queue[qn++] = first;
  for (size_t cursor = 0; cursor < qn; cursor++) {
    Update *current = queue[cursor]; current->queued = false;
    at_check(current->cluster, e->asset->cluster_count);
    const qa_aas_cluster_rec *record = &e->asset->clusters[current->cluster];
    qa_aas_estimate_cache *local = area_cache(e, current->cluster, current->area, flags);
    if (!local) { free(updates); free(queue); free(cache.times); free(cache.reaches); return NULL; }
    /* cache pointer may be invalidated by later area_cache calls; copy times locally per iteration use below by re-finding */
    for (int64_t offset = 0; offset < record->portal_count; offset++) {
      int64_t pidx = record->first_portal + offset;
      at_check(pidx, e->asset->portal_index_count);
      int64_t number = e->asset->portal_indexes[pidx];
      at_check(number, e->asset->portal_count);
      const qa_aas_portal_rec *portal = &e->asset->portals[number];
      if (portal->area == current->area) continue;
      int64_t index = cluster_area(e, current->cluster, portal->area);
      if (index >= record->reachability_area_count) continue;
      if (index < 0 || index >= (int64_t)local->count) at_check(index, local->count);
      int64_t local_time = local->times[index];
      if (local_time == 0) continue;
      int64_t time = (local_time + current->time) & 0xffff;
      int64_t previous = cache.times[number];
      if (previous != 0 && previous <= time) continue;
      cache.times[number] = (uint16_t)time;
      Update *next = &updates[number];
      next->cluster = portal->front_cluster == current->cluster ? portal->back_cluster : portal->front_cluster;
      next->area = portal->area;
      at_check(number, e->asset->portal_count);
      next->time = (time + e->portal_maxima[number]) & 0xffff;
      if (!next->queued) {
        next->queued = true;
        if (qn == qcap) { Update **g = realloc(queue, qcap * 2 * sizeof *g); if (!g) { free(updates); free(queue); free(cache.times); free(cache.reaches); return NULL; } queue = g; qcap *= 2; }
        queue[qn++] = next;
      }
    }
    (void)local;
  }
  free(updates); free(queue);
  return cache_remember(e, key, cache);
}

qa_navigation_estimate_result qa_aas_navigation_estimates_estimate(qa_aas_navigation_estimates *e,
                                                                   qa_navigation_estimate_query query) {
  qa_navigation_estimate_result out; memset(&out, 0, sizeof out);
  int64_t area = query.start_node, goal = query.goal_node;
  if (area <= 0 || area >= (int64_t)e->asset->area_count || goal <= 0 || goal >= (int64_t)e->asset->area_count) return out;
  if (area == goal) { out.is_estimate = true; out.travel_time = 1; return out; }
  at_check(area, e->asset->setting_count); at_check(goal, e->asset->setting_count);
  const qa_aas_setting_rec *start = &e->asset->settings[area];
  const qa_aas_setting_rec *end = &e->asset->settings[goal];
  int64_t flags = (int64_t)(int32_t)(query.has_travel_flags ? query.travel_flags : 0x011c0fbeLL);
  if (((start->contents | end->contents) & 256) != 0) flags |= 0x00800000;
  int64_t cluster = start->cluster, goal_cluster = end->cluster;
  if (cluster < 0 && goal_cluster > 0) {
    at_check(-cluster, e->asset->portal_count);
    const qa_aas_portal_rec *portal = &e->asset->portals[-cluster];
    if (portal->front_cluster == goal_cluster || portal->back_cluster == goal_cluster) cluster = goal_cluster;
  } else if (cluster > 0 && goal_cluster < 0) {
    at_check(-goal_cluster, e->asset->portal_count);
    const qa_aas_portal_rec *portal = &e->asset->portals[-goal_cluster];
    if (portal->front_cluster == cluster || portal->back_cluster == cluster) goal_cluster = cluster;
  }
  if (cluster > 0 && cluster == goal_cluster) {
    qa_aas_estimate_cache *cache = area_cache(e, cluster, goal, flags);
    if (!cache) return out;
    int64_t index = cluster_area(e, cluster, area);
    at_check(cluster, e->asset->cluster_count);
    if (index >= e->asset->clusters[cluster].reachability_area_count) return out;
    if (index < 0 || index >= (int64_t)cache->count) at_check(index, cache->count);
    int64_t time = cache->times[index];
    if (time != 0) {
      out.is_estimate = true;
      if (!query.has_origin) { out.travel_time = (double)time; return out; }
      int64_t reach = start->first_reach + cache->reaches[index];
      at_check(reach, e->asset->reachability_count);
      out.travel_time = (double)(time + qa_aas_estimate_area_time(start->presence, start->flags, query.origin,
                                                                  e->asset->reachability[reach].start));
      const qa_navigation_edge *edge = edge_by_id(e, reach);
      if (edge) { out.has_first_edge = true; out.first_edge = *edge; }
      return out;
    }
  }
  cluster = start->cluster; goal_cluster = end->cluster;
  if (goal_cluster < 0) { at_check(-goal_cluster, e->asset->portal_count); goal_cluster = e->asset->portals[-goal_cluster].front_cluster; }
  qa_aas_estimate_cache *pcache = portal_cache(e, goal_cluster, goal, flags);
  if (!pcache) return out;
  if (cluster < 0) {
    if (-cluster >= (int64_t)pcache->count) at_check(-cluster, pcache->count);
    out.is_estimate = true;
    out.travel_time = (double)pcache->times[-cluster];
    if (query.has_origin) {
      int64_t reach = start->first_reach + pcache->reaches[-cluster];
      const qa_navigation_edge *edge = edge_by_id(e, reach);
      if (edge) { out.has_first_edge = true; out.first_edge = *edge; }
    }
    return out;
  }
  int64_t best_time = 0;
  at_check(cluster, e->asset->cluster_count);
  const qa_aas_cluster_rec *record = &e->asset->clusters[cluster];
  for (int64_t offset = 0; offset < record->portal_count; offset++) {
    int64_t pidx = record->first_portal + offset;
    at_check(pidx, e->asset->portal_index_count);
    int64_t number = e->asset->portal_indexes[pidx];
    /* pcache may move when area_cache remembers; re-find by key each round */
    char key[80]; snprintf(key, sizeof key, "p:%lld:%lld", (long long)goal, (long long)flags);
    pcache = cache_find(e, key);
    if (!pcache) return out;
    if (number < 0 || number >= (int64_t)pcache->count) at_check(number, pcache->count);
    int64_t portal_time = pcache->times[number];
    if (portal_time == 0) continue;
    at_check(number, e->asset->portal_count);
    const qa_aas_portal_rec *portal = &e->asset->portals[number];
    qa_aas_estimate_cache *local = area_cache(e, cluster, portal->area, flags);
    if (!local) return out;
    int64_t index = cluster_area(e, cluster, area);
    if (index >= record->reachability_area_count) continue;
    if (index < 0 || index >= (int64_t)local->count) at_check(index, local->count);
    int64_t local_time = local->times[index];
    if (local_time == 0) continue;
    bool has_reach = query.has_origin;
    int64_t reach = has_reach ? start->first_reach + local->reaches[index] : 0;
    int64_t time = (portal_time + local_time) & 0xffff;
    at_check(number, e->asset->portal_count);
    time = (time + e->portal_maxima[number]) & 0xffff;
    if (has_reach) {
      at_check(reach, e->asset->reachability_count);
      time = (time + qa_aas_estimate_area_time(start->presence, start->flags, query.origin,
                                               e->asset->reachability[reach].start)) & 0xffff;
    }
    if (best_time == 0 || time < best_time) {
      best_time = time;
      out.is_estimate = true; out.travel_time = (double)time;
      out.has_first_edge = false;
      if (has_reach) {
        const qa_navigation_edge *edge = edge_by_id(e, reach);
        if (edge) { out.has_first_edge = true; out.first_edge = *edge; }
      }
    }
  }
  return out;
}
