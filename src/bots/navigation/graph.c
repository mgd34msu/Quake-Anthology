/* Port of src/bots/navigation/graph.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/graph.h"
#include "qa/bots/navigation/helpers.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const qa_travel_mode AAS_MODES[20] = {
    QA_TM_UNKNOWN, QA_TM_UNKNOWN, QA_TM_WALK, QA_TM_CROUCH, QA_TM_JUMP,
    QA_TM_JUMP, QA_TM_LADDER, QA_TM_DROP, QA_TM_SWIM, QA_TM_WATER_JUMP,
    QA_TM_TELEPORT, QA_TM_MOVER, QA_TM_ROCKET_JUMP, QA_TM_BFG_JUMP, QA_TM_GRAPPLE,
    QA_TM_DOUBLE_JUMP, QA_TM_RAMP_JUMP, QA_TM_STRAFE_JUMP, QA_TM_JUMP_PAD, QA_TM_MOVER};
static const qa_travel_mode KEX_MODES[15] = {
    QA_TM_WALK, QA_TM_JUMP, QA_TM_TELEPORT, QA_TM_DROP, QA_TM_JUMP_PAD,
    QA_TM_JUMP, QA_TM_MOVER, QA_TM_MOVER, QA_TM_JUMP, QA_TM_CROUCH,
    QA_TM_LADDER, QA_TM_JUMP, QA_TM_JUMP, QA_TM_ROCKET_JUMP, QA_TM_UNKNOWN};

qa_travel_mode qa_aas_travel_mode(int64_t type) {
  int64_t i = type & 0xffffff;
  return (i >= 0 && i < 20) ? AAS_MODES[i] : QA_TM_UNKNOWN;
}
qa_travel_mode qa_kex_travel_mode(int64_t type) {
  return (type >= 0 && type < 15) ? KEX_MODES[type] : QA_TM_UNKNOWN;
}
int64_t qa_aas_travel_flag(int64_t type) {
  int64_t index = type & 0xffffff;
  if (index < 2 || index > 19) return 1;
  if (index == 19) return 0x01000000;
  return (int64_t)1 << (index >= 7 ? index : index - 1);
}
int64_t qa_aas_area_travel_flags(int64_t contents, int64_t flags) {
  int64_t v = contents;
  return ((v & 1) != 0 ? 0x00100000 : (v & 4) != 0 ? 0x00200000 : (v & 2) != 0 ? 0x00400000 : 0x00080000) |
         ((v & 256) != 0 ? 0x00800000 : 0) | ((v & 2048) != 0 ? 0x08000000 : 0) |
         ((v & 4096) != 0 ? 0x10000000 : 0) | ((flags & 16) != 0 ? 0x04000000 : 0);
}

/* --- clusters (Kosaraju, TS iteration order) --- */
typedef struct { int64_t *v; size_t n, cap; } I64List;
static bool push64(I64List *l, int64_t x) {
  if (l->n == l->cap) { size_t c = l->cap ? l->cap * 2 : 8; int64_t *p = realloc(l->v, c * sizeof *p); if (!p) return false; l->v = p; l->cap = c; }
  l->v[l->n++] = x; return true;
}
static bool contains64(const I64List *l, int64_t x) { for (size_t i = 0; i < l->n; i++) if (l->v[i] == x) return true; return false; }
typedef struct { int64_t node; bool exit; } Frame;
typedef struct { Frame *v; size_t n, cap; } FrameList;
static bool pushf(FrameList *l, Frame f) {
  if (l->n == l->cap) { size_t c = l->cap ? l->cap * 2 : 16; Frame *p = realloc(l->v, c * sizeof *p); if (!p) return false; l->v = p; l->cap = c; }
  l->v[l->n++] = f; return true;
}

void qa_navigation_clusters_destroy(qa_navigation_cluster_list *clusters, size_t count) {
  if (!clusters) return;
  for (size_t i = 0; i < count; i++) free((void *)clusters[i].nodes);
  free(clusters);
}

qa_navigation_cluster_list *qa_navigation_clusters(const qa_navigation_node *nodes, size_t node_count,
                                                   const qa_navigation_edge *edges, size_t edge_count,
                                                   size_t *out_count) {
  *out_count = 0;
  /* adjacency indexed by position in nodes[]; unknown ids have none */
  I64List *outg = calloc(node_count ? node_count : 1, sizeof *outg);
  I64List *inc = calloc(node_count ? node_count : 1, sizeof *inc);
  if (!outg || !inc) { free(outg); free(inc); return NULL; }
  bool ok = true;
  for (size_t e = 0; e < edge_count && ok; e++) {
    for (size_t i = 0; i < node_count; i++) if (nodes[i].id == edges[e].from) { ok = push64(&outg[i], edges[e].to); break; }
    if (!ok) break;
    for (size_t i = 0; i < node_count; i++) if (nodes[i].id == edges[e].to) { ok = push64(&inc[i], edges[e].from); break; }
  }
  I64List seen = {0}, order = {0};
  qa_navigation_cluster_list *clusters = NULL; size_t cn = 0, ccap = 0;
  if (ok) {
    for (size_t ni = 0; ni < node_count && ok; ni++) {
      FrameList stack = {0};
      ok = pushf(&stack, (Frame){nodes[ni].id, false});
      while (ok && stack.n > 0) {
        Frame fr = stack.v[--stack.n];
        if (fr.exit) { ok = push64(&order, fr.node); continue; }
        if (contains64(&seen, fr.node)) continue;
        ok = push64(&seen, fr.node) && pushf(&stack, (Frame){fr.node, true});
        if (!ok) break;
        const I64List *adj = NULL;
        for (size_t i = 0; i < node_count; i++) if (nodes[i].id == fr.node) { adj = &outg[i]; break; }
        if (adj) for (size_t k = 0; k < adj->n && ok; k++) if (!contains64(&seen, adj->v[k])) ok = pushf(&stack, (Frame){adj->v[k], false});
      }
      free(stack.v);
    }
  }
  if (ok) {
    seen.n = 0;
    for (size_t oi = order.n; oi-- > 0 && ok;) {
      int64_t start = order.v[oi];
      if (contains64(&seen, start)) continue;
      I64List cluster = {0}, stack = {0};
      ok = push64(&stack, start);
      while (ok && stack.n > 0) {
        int64_t node = stack.v[--stack.n];
        if (contains64(&seen, node)) continue;
        ok = push64(&seen, node) && push64(&cluster, node);
        if (!ok) break;
        const I64List *adj = NULL;
        for (size_t i = 0; i < node_count; i++) if (nodes[i].id == node) { adj = &inc[i]; break; }
        if (adj) for (size_t k = 0; k < adj->n && ok; k++) if (!contains64(&seen, adj->v[k])) ok = push64(&stack, adj->v[k]);
      }
      free(stack.v);
      if (!ok) { free(cluster.v); break; }
      if (cn == ccap) { size_t c = ccap ? ccap * 2 : 8; qa_navigation_cluster_list *p = realloc(clusters, c * sizeof *p); if (!p) { free(cluster.v); ok = false; break; } clusters = p; ccap = c; }
      clusters[cn].nodes = cluster.v; clusters[cn].count = cluster.n; cn++;
    }
  }
  for (size_t i = 0; i < node_count; i++) { free(outg[i].v); free(inc[i].v); }
  free(outg); free(inc); free(seen.v); free(order.v);
  if (!ok) { qa_navigation_clusters_destroy(clusters, cn); return NULL; }
  *out_count = cn;
  return clusters;
}

/* --- navigationFromAsset --- */
static void at_check(int64_t index, size_t count) {
  if (index < 0 || index >= (int64_t)count)
    qa_range_error("Navigation record %lld exceeds %lld", (long long)index, (long long)count);
}
typedef struct { qa_navigation_node *v; size_t n, cap; } NodeList;
typedef struct { qa_navigation_edge *v; size_t n, cap; } EdgeList;
static bool push_node(NodeList *l, qa_navigation_node x) {
  if (l->n == l->cap) { size_t c = l->cap ? l->cap * 2 : 16; qa_navigation_node *p = realloc(l->v, c * sizeof *p); if (!p) return false; l->v = p; l->cap = c; }
  l->v[l->n++] = x; return true;
}
static bool push_edge(EdgeList *l, qa_navigation_edge x) {
  if (l->n == l->cap) { size_t c = l->cap ? l->cap * 2 : 16; qa_navigation_edge *p = realloc(l->v, c * sizeof *p); if (!p) return false; l->v = p; l->cap = c; }
  l->v[l->n++] = x; return true;
}

bool qa_navigation_from_asset(qa_navigation_graph *out, qa_navigation_map_identity map,
                              const qa_aas_asset *aas, const qa_kex_navigation_asset *kex,
                              qa_navigation_profile profile, const qa_navigation_world *world) {
  memset(out, 0, sizeof *out);
  qa_nav_validate_profile(&profile);
  NodeList nodes = {0}; EdgeList edges = {0};
  bool ok = true;
  if (aas) {
    for (int64_t number = 1; ok && number < (int64_t)aas->area_count; number++) {
      at_check(number, aas->area_count); at_check(number, aas->setting_count);
      const qa_aas_area_rec *area = &aas->areas[number];
      const qa_aas_setting_rec *setting = &aas->settings[number];
      qa_navigation_profile posture = profile;
      if ((setting->presence & 2) == 0 && (setting->presence & 4) != 0) {
        qa_navigation_profile crouched;
        if (qa_nav_crouched_profile(&profile, &crouched)) posture = crouched;
      }
      qa_vec3 origin = area->center;
      if ((setting->flags & 1) != 0) {
        qa_vec3 dest = area->center;
        dest.z = area->bounds.min.z + posture.shape.bounds.min.z - profile.maximum_step;
        qa_trace_result floor = qa_nav_trace_world(world, &posture, area->center, dest);
        if (!floor.start_solid && !floor.all_solid && floor.fraction < 1 && floor.contact.has_plane &&
            floor.contact.plane.normal.z >= profile.minimum_floor_normal) {
          origin = floor.end;
        } else {
          for (int64_t idx = setting->first_reach; idx < setting->first_reach + setting->reach_count; idx++) {
            at_check(idx, aas->reachability_count);
            const qa_aas_reach_rec *reach = &aas->reachability[idx];
            if (qa_nav_clear_world(world, &posture, reach->start, reach->start)) { origin = reach->start; break; }
          }
        }
      }
      qa_navigation_node node; memset(&node, 0, sizeof node);
      node.id = number; node.origin = origin; node.bounds = area->bounds; node.radius = 0;
      node.contents = qa_nav_contents_world(world, &profile, origin);
      node.flags = setting->flags; node.presence = setting->presence;
      node.has_source_cluster = true; node.source_cluster = setting->cluster;
      node.source = (qa_navigation_source){QA_NS_AAS, number, false, 0};
      ok = push_node(&nodes, node);
      if (!ok) break;
      for (int64_t idx = setting->first_reach; idx < setting->first_reach + setting->reach_count && ok; idx++) {
        at_check(idx, aas->reachability_count);
        const qa_aas_reach_rec *reach = &aas->reachability[idx];
        qa_travel_mode mode = qa_aas_travel_mode(reach->travel_type);
        if (reach->area == 0) continue;
        bool moving = mode == QA_TM_MOVER || mode == QA_TM_TELEPORT || mode == QA_TM_JUMP_PAD;
        qa_navigation_edge edge; memset(&edge, 0, sizeof edge);
        edge.id = idx; edge.from = number; edge.to = reach->area; edge.mode = mode;
        edge.start = reach->start; edge.end = reach->end;
        edge.travel_seconds = fmax(1.0, (double)reach->travel_time) / 100.0;
        edge.source_travel_type = reach->travel_type; edge.source_flags = 0;
        if (moving) {
          edge.has_entity = true;
          edge.entity.has_model = mode == QA_TM_MOVER;
          edge.entity.model = mode == QA_TM_MOVER ? (reach->face & 0xffff) : 0;
          edge.entity.bounds = area->bounds;
          /* raw [face,edge]: stored in edge-owned scratch via static? use malloc'd 2 doubles */
          double *raw = malloc(2 * sizeof *raw);
          if (!raw) { ok = false; break; }
          raw[0] = (double)reach->face; raw[1] = (double)reach->edge;
          edge.entity.raw = raw; edge.entity.raw_count = 2;
        }
        edge.source = (qa_navigation_source){QA_NS_AAS, number, true, idx};
        if (!push_edge(&edges, edge)) { free((void *)edge.entity.raw); ok = false; }
      }
    }
  } else if (kex) {
    double rise = -profile.shape.bounds.min.z;
    qa_navigation_source_kind kind = kex->is_nav3 ? QA_NS_NAV3 : QA_NS_NAV2;
    for (size_t number = 0; ok && number < kex->node_count; number++) {
      const qa_kex_nav_node *kn = &kex->nodes[number];
      qa_vec3 lifted = kn->origin; lifted.z += rise;
      qa_navigation_node node; memset(&node, 0, sizeof node);
      node.id = (int64_t)number; node.origin = lifted;
      node.bounds = qa_nav_translated(lifted, profile.shape.bounds);
      node.radius = (double)kn->radius; node.flags = kn->flags;
      node.contents = qa_nav_contents_world(world, &profile, lifted);
      node.presence = 0; node.has_source_cluster = false;
      node.source = (qa_navigation_source){kind, (int64_t)number, false, 0};
      ok = push_node(&nodes, node);
    }
    for (size_t number = 0; ok && number < kex->node_count; number++) {
      const qa_kex_nav_node *kn = &kex->nodes[number];
      for (int64_t idx = kn->first_link; idx < kn->first_link + kn->link_count && ok; idx++) {
        at_check(idx, kex->link_count);
        const qa_kex_nav_link *link = &kex->links[idx];
        const qa_kex_nav_entity *entity = NULL;
        for (size_t e = kex->entity_count; e-- > 0;) if (kex->entities[e].link == idx) { entity = &kex->entities[e]; break; }
        bool has_hint = link->has_traversal;
        qa_traversal_hint hint; memset(&hint, 0, sizeof hint);
        if (has_hint) {
          at_check(link->traversal, kex->traversal_count);
          hint = kex->traversals[link->traversal];
          hint.funnel.z += rise; hint.start.z += rise; hint.end.z += rise;
        }
        at_check((int64_t)number, nodes.n); at_check(link->target, nodes.n);
        qa_vec3 start = has_hint ? hint.start : nodes.v[number].origin;
        qa_vec3 end = has_hint ? hint.end : nodes.v[link->target].origin;
        qa_travel_mode mode = qa_kex_travel_mode(link->type);
        qa_navigation_edge edge; memset(&edge, 0, sizeof edge);
        edge.id = idx; edge.from = (int64_t)number; edge.to = link->target; edge.mode = mode;
        edge.start = start; edge.end = end; edge.source_travel_type = link->type; edge.source_flags = link->flags;
        edge.travel_seconds = mode == QA_TM_TELEPORT ? 0.01 : fmax(0.01, qa_nav_distance(start, end) * kex->heuristic / 320.0);
        edge.has_hint = has_hint; edge.hint = hint;
        if (!entity) {
          if (mode == QA_TM_TELEPORT || mode == QA_TM_JUMP_PAD || mode == QA_TM_MOVER) {
            edge.has_entity = true; edge.entity.has_model = false;
            edge.entity.bounds = nodes.v[number].bounds; edge.entity.raw = NULL; edge.entity.raw_count = 0;
          }
        } else {
          edge.has_entity = true;
          int64_t model = 0; bool has_model = entity->has_model;
          if (has_model) {
            model = entity->model;
            if (kex->is_nav3) {
              if (model <= 1 || model == 255) has_model = false;
              else model = model - (model > 255 ? 2 : 1);
            }
          }
          edge.entity.has_model = has_model; edge.entity.model = model;
          edge.entity.bounds = entity->bounds;
          edge.entity.raw = entity->tail; edge.entity.raw_count = entity->tail_count;
        }
        edge.source = (qa_navigation_source){kind, (int64_t)number, true, idx};
        ok = push_edge(&edges, edge);
      }
    }
  }
  qa_navigation_rejected *rejected = NULL; size_t rn = 0, rcap = 0;
  if (ok) {
    for (size_t i = 0; i < edges.n && ok; i++) {
      if (edges.v[i].mode != QA_TM_UNKNOWN) continue;
      char buf[96];
      snprintf(buf, sizeof buf, "Unsupported source travel type %lld", (long long)edges.v[i].source_travel_type);
      char *reason = malloc(strlen(buf) + 1);
      if (!reason) { ok = false; break; }
      strcpy(reason, buf);
      if (rn == rcap) { size_t c = rcap ? rcap * 2 : 8; qa_navigation_rejected *p = realloc(rejected, c * sizeof *p); if (!p) { free(reason); ok = false; break; } rejected = p; rcap = c; }
      rejected[rn].source = edges.v[i].source; rejected[rn].reason = reason; rn++;
    }
  }
  size_t cluster_count = 0;
  qa_navigation_cluster_list *clusters = NULL;
  if (ok) {
    clusters = qa_navigation_clusters(nodes.v, nodes.n, edges.v, edges.n, &cluster_count);
    if (!clusters && nodes.n > 0) ok = false;
  }
  if (!ok) {
    /* aas entity.raw arrays are owned by edges; kex raw borrows asset tail (aas flag distinguishes) */
    if (aas) for (size_t i = 0; i < edges.n; i++) free((void *)edges.v[i].entity.raw);
    free(nodes.v); free(edges.v);
    for (size_t i = 0; i < rn; i++) free((void *)rejected[i].reason);
    free(rejected);
    qa_navigation_clusters_destroy(clusters, cluster_count);
    return false;
  }
  out->map = map; out->profile = profile;
  out->aas_asset = aas; out->kex_asset = kex;
  out->nodes = nodes.v; out->node_count = nodes.n;
  out->edges = edges.v; out->edge_count = edges.n;
  out->clusters = clusters; out->cluster_count = cluster_count;
  out->rejected = rejected; out->rejected_count = rn;
  return true;
}

int64_t qa_navigation_edge_travel_flag(const qa_navigation_edge *edge) {
  if (edge->source.kind == QA_NS_AAS) return qa_aas_travel_flag(edge->source_travel_type);
  switch (edge->mode) {
    case QA_TM_WALK: return 2;
    case QA_TM_CROUCH: return 4;
    case QA_TM_JUMP: return 16;
    case QA_TM_DROP: return 128;
    case QA_TM_SWIM: return 256;
    case QA_TM_WATER_JUMP: return 512;
    case QA_TM_LADDER: return 32;
    case QA_TM_TELEPORT: return 1024;
    case QA_TM_MOVER: return 16779264;
    case QA_TM_JUMP_PAD: return 262144;
    case QA_TM_ROCKET_JUMP: return 4096;
    case QA_TM_BFG_JUMP: return 8192;
    case QA_TM_GRAPPLE: return 16384;
    case QA_TM_DOUBLE_JUMP: return 32768;
    case QA_TM_RAMP_JUMP: return 65536;
    case QA_TM_STRAFE_JUMP: return 131072;
    case QA_TM_UNKNOWN: return 1;
    default: return 1;
  }
}
