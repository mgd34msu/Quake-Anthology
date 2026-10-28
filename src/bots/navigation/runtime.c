/* Port of src/bots/navigation/runtime.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/runtime.h"
#include "qa/bots/navigation/aas.h"
#include "qa/bots/navigation/graph.h"
#include "qa/bots/navigation/helpers.h"
#include "qa/bots/navigation/train.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dups(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
static const char *format_name(qa_decoded_world_kind k) {
  return k == QA_DECODED_WORLD_Q1 ? "q1-bsp" : k == QA_DECODED_WORLD_Q2 ? "q2-bsp" : "q3-bsp";
}
static int64_t node_index(const qa_navigation_runtime *rt, int64_t id) {
  for (size_t i = 0; i < rt->graph->node_count; i++) if (rt->graph->nodes[i].id == id) return (int64_t)i;
  return -1;
}
const qa_navigation_node *qa_navigation_runtime_node(const qa_navigation_runtime *rt, int64_t id) {
  int64_t i = node_index(rt, id);
  return i < 0 ? NULL : &rt->graph->nodes[i];
}
const qa_navigation_edge *const *qa_navigation_runtime_outgoing(const qa_navigation_runtime *rt, int64_t id, size_t *out_count) {
  int64_t i = node_index(rt, id);
  if (i < 0) { *out_count = 0; return NULL; }
  *out_count = rt->outgoing_counts[i];
  return rt->outgoing[i];
}
/* small id maps (insertion order preserved, like TS Map) */
static qa_navigation_id_bool *enabled_find(qa_navigation_runtime *rt, int64_t id) {
  for (size_t i = 0; i < rt->enabled_count; i++) if (rt->enabled[i].id == id) return &rt->enabled[i];
  return NULL;
}
static bool enabled_set(qa_navigation_runtime *rt, int64_t id, bool v) {
  qa_navigation_id_bool *e = enabled_find(rt, id);
  if (e) { e->enabled = v; return true; }
  if (rt->enabled_count == rt->enabled_cap) {
    size_t c = rt->enabled_cap ? rt->enabled_cap * 2 : 8;
    qa_navigation_id_bool *p = realloc(rt->enabled, c * sizeof *p);
    if (!p) return false;
    rt->enabled = p; rt->enabled_cap = c;
  }
  rt->enabled[rt->enabled_count++] = (qa_navigation_id_bool){id, v};
  return true;
}
static qa_navigation_id_reason *blocked_find(qa_navigation_runtime *rt, int64_t id) {
  for (size_t i = 0; i < rt->blocked_count; i++) if (rt->blocked[i].id == id) return &rt->blocked[i];
  return NULL;
}
static qa_navigation_id_seconds *admission_find(qa_navigation_runtime *rt, int64_t id) {
  for (size_t i = 0; i < rt->admission_count; i++) if (rt->admission_seconds[i].id == id) return &rt->admission_seconds[i];
  return NULL;
}
static bool admission_set(qa_navigation_runtime *rt, int64_t id, double v) {
  qa_navigation_id_seconds *e = admission_find(rt, id);
  if (e) { e->seconds = v; return true; }
  if (rt->admission_count == rt->admission_cap) {
    size_t c = rt->admission_cap ? rt->admission_cap * 2 : 8;
    qa_navigation_id_seconds *p = realloc(rt->admission_seconds, c * sizeof *p);
    if (!p) return false;
    rt->admission_seconds = p; rt->admission_cap = c;
  }
  rt->admission_seconds[rt->admission_count++] = (qa_navigation_id_seconds){id, v};
  return true;
}
static void invalidate(qa_navigation_runtime *rt) { rt->generation++; rt->admission_count = 0; }
static void refresh(qa_navigation_runtime *rt) {
  if (rt->world_revision != rt->world->revision) { rt->world_revision = rt->world->revision; invalidate(rt); }
}
static bool disabled_has(const qa_navigation_route_query *q, int64_t id) {
  if (!q) return false;
  for (size_t i = 0; i < q->disabled_area_count; i++) if (q->disabled_areas[i] == id) return true;
  return false;
}
static bool static_node_allowed(qa_navigation_runtime *rt, const qa_navigation_node *node,
                                const qa_navigation_route_query *query) {
  qa_navigation_profile profile;
  if (!qa_nav_node_profile(&rt->graph->profile, node, &profile)) return false;
  qa_navigation_id_bool *en = enabled_find(rt, node->id);
  if ((en && !en->enabled) || disabled_has(query, node->id)) return false;
  if (node->source.kind == QA_NS_AAS && (node->flags & 8) != 0 && !(en && en->enabled)) return false;
  if (node->source.kind == QA_NS_NAV3) {
    if ((node->flags & 8192) != 0 || (profile.monster && (node->flags & 256) != 0)) return false;
    if ((node->flags & 512) != 0 && !(profile.capabilities & (1u << QA_TM_CROUCH))) return false;
  }
  return true;
}
static bool static_edge_allowed(qa_navigation_runtime *rt, const qa_navigation_edge *edge,
                                const qa_navigation_route_query *query) {
  const qa_navigation_profile *profile = &rt->graph->profile;
  if (!(profile->capabilities & (1u << edge->mode)) || edge->mode == QA_TM_UNKNOWN || blocked_find(rt, edge->id)) return false;
  if (query && query->edge_filter && !query->edge_filter(query->edge_filter_ctx, edge)) return false;
  if (query && query->has_travel_flags && (qa_navigation_edge_travel_flag(edge) & query->travel_flags) == 0) return false;
  const qa_navigation_node *target = qa_navigation_runtime_node(rt, edge->to);
  const qa_navigation_node *source = qa_navigation_runtime_node(rt, edge->from);
  if (!source || !target || !static_node_allowed(rt, source, query) || !static_node_allowed(rt, target, query)) return false;
  if (edge->mode == QA_TM_DROP && edge->start.z - edge->end.z > profile->maximum_drop) return false;
  if (edge->source.kind == QA_NS_AAS) {
    if (query && query->has_travel_flags && (qa_aas_travel_flag(edge->source_travel_type) & query->travel_flags) == 0) return false;
    if ((profile->team == 0 && (edge->source_travel_type & 0x01000000) != 0) ||
        (profile->team == 1 && (edge->source_travel_type & 0x02000000) != 0)) return false;
    const qa_aas_asset *asset = rt->graph->aas_asset;
    if (asset && edge->to >= 0 && edge->to < (int64_t)asset->setting_count) {
      const qa_aas_setting_rec *settings = &asset->settings[edge->to];
      if (query && query->has_travel_flags &&
          (qa_aas_area_travel_flags(settings->contents, settings->flags) & ~query->travel_flags) != 0) return false;
      if ((profile->team == 0 && (settings->contents & 2048) != 0) ||
          (profile->team == 1 && (settings->contents & 4096) != 0)) return false;
    }
  } else if (edge->source.kind == QA_NS_NAV3) {
    if ((edge->source_flags & 64) != 0) return false;
    if (profile->team >= 0 && (edge->source_flags & (profile->team == 0 ? 1 : 2)) == 0) return false;
  }
  return true;
}
static bool estimate_allowed(void *ctx, const qa_navigation_edge *edge, bool has_flags, int64_t flags) {
  qa_navigation_runtime *rt = ctx;
  qa_navigation_route_query q; memset(&q, 0, sizeof q);
  q.has_travel_flags = has_flags; q.travel_flags = flags;
  return static_edge_allowed(rt, edge, has_flags ? &q : NULL);
}

bool qa_navigation_runtime_init(qa_navigation_runtime *rt, const qa_navigation_graph *graph,
                                const qa_navigation_world *world) {
  memset(rt, 0, sizeof *rt);
  rt->graph = graph; rt->world = world; rt->world_revision = world->revision;
  for (size_t i = 0; i < graph->node_count; i++)
    for (size_t j = i + 1; j < graph->node_count; j++)
      if (graph->nodes[i].id == graph->nodes[j].id)
        qa_range_error("Duplicate navigation node %lld", (long long)graph->nodes[i].id);
  size_t n = graph->node_count ? graph->node_count : 1;
  rt->outgoing = calloc(n, sizeof *rt->outgoing);
  rt->outgoing_counts = calloc(n, sizeof *rt->outgoing_counts);
  size_t *caps = calloc(n, sizeof *caps);
  if (!rt->outgoing || !rt->outgoing_counts || !caps) { free(caps); qa_navigation_runtime_destroy(rt); return false; }
  for (size_t i = 0; i < graph->edge_count; i++) {
    const qa_navigation_edge *edge = &graph->edges[i];
    int64_t from = node_index(rt, edge->from);
    bool dup = false;
    for (size_t j = 0; j < i; j++) if (graph->edges[j].id == edge->id) dup = true;
    if (from < 0 || !qa_navigation_runtime_node(rt, edge->to) || dup)
      qa_range_error("Invalid navigation edge identity/reference");
    if (!isfinite(edge->travel_seconds) || edge->travel_seconds < 0)
      qa_range_error("Invalid navigation edge cost");
    if (rt->outgoing_counts[from] == caps[from]) {
      size_t c = caps[from] ? caps[from] * 2 : 4;
      const qa_navigation_edge **p = realloc((void *)rt->outgoing[from], c * sizeof *p);
      if (!p) { free(caps); qa_navigation_runtime_destroy(rt); return false; }
      rt->outgoing[from] = p; caps[from] = c;
    }
    rt->outgoing[from][rt->outgoing_counts[from]++] = edge;
  }
  free(caps);
  if (!qa_navigation_estimates_init(&rt->estimates, graph, rt, estimate_allowed)) {
    qa_navigation_runtime_destroy(rt);
    return false;
  }
  return true;
}
void qa_navigation_runtime_destroy(qa_navigation_runtime *rt) {
  if (!rt) return;
  qa_navigation_estimates_destroy(&rt->estimates);
  if (rt->outgoing && rt->graph) for (size_t i = 0; i < rt->graph->node_count; i++) free((void *)rt->outgoing[i]);
  free((void *)rt->outgoing); free(rt->outgoing_counts);
  free(rt->enabled);
  for (size_t i = 0; i < rt->blocked_count; i++) free(rt->blocked[i].reason);
  free(rt->blocked); free(rt->admission_seconds);
  memset(rt, 0, sizeof *rt);
}
void qa_navigation_runtime_checkpoint_destroy(qa_navigation_runtime_checkpoint *c) {
  if (!c) return;
  free(c->enabled);
  for (size_t i = 0; i < c->blocked_count; i++) free(c->blocked[i].reason);
  free(c->blocked); free(c->admission_seconds);
  memset(c, 0, sizeof *c);
}
bool qa_navigation_runtime_make_checkpoint(qa_navigation_runtime *rt, qa_navigation_runtime_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->map_name = rt->graph->map.name;
  out->map_format = format_name(rt->graph->map.format);
  out->map_digest = rt->graph->map.digest;
  out->enabled_count = rt->enabled_count;
  out->blocked_count = rt->blocked_count;
  out->admission_count = rt->admission_count;
  if (out->enabled_count) {
    out->enabled = malloc(out->enabled_count * sizeof *out->enabled);
    if (!out->enabled) goto oom;
    memcpy(out->enabled, rt->enabled, out->enabled_count * sizeof *out->enabled);
  }
  if (out->blocked_count) {
    out->blocked = calloc(out->blocked_count, sizeof *out->blocked);
    if (!out->blocked) goto oom;
    for (size_t i = 0; i < out->blocked_count; i++) {
      out->blocked[i].id = rt->blocked[i].id;
      out->blocked[i].reason = dups(rt->blocked[i].reason);
      if (!out->blocked[i].reason) goto oom;
    }
  }
  if (out->admission_count) {
    out->admission_seconds = malloc(out->admission_count * sizeof *out->admission_seconds);
    if (!out->admission_seconds) goto oom;
    memcpy(out->admission_seconds, rt->admission_seconds, out->admission_count * sizeof *out->admission_seconds);
  }
  out->world_revision = (double)rt->world_revision;
  out->generation = rt->generation;
  return true;
oom:
  qa_navigation_runtime_checkpoint_destroy(out);
  return false;
}

void qa_navigation_runtime_restore_checkpoint(qa_navigation_runtime *rt, const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "navigation")) qa_range_error("Out of memory");
  qa_save_reader field, map, entry, sub;
  if (!qa_save_reader_field(&reader, "version", &field)) qa_range_error("Out of memory");
  (void)qa_save_reader_literal_number(&field, 1);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&reader, "map", &map)) qa_range_error("Out of memory");
  if (!qa_save_reader_field(&map, "name", &sub)) qa_range_error("Out of memory");
  (void)qa_save_reader_literal_string(&sub, rt->graph->map.name);
  qa_save_reader_destroy(&sub);
  if (!qa_save_reader_field(&map, "format", &sub)) qa_range_error("Out of memory");
  (void)qa_save_reader_literal_string(&sub, format_name(rt->graph->map.format));
  qa_save_reader_destroy(&sub);
  if (!qa_save_reader_field(&map, "digest", &sub)) qa_range_error("Out of memory");
  (void)qa_save_reader_literal_string(&sub, rt->graph->map.digest);
  qa_save_reader_destroy(&sub);
  qa_save_reader_destroy(&map);
  /* enabled (known node ids) */
  qa_navigation_id_bool *enabled = NULL; size_t en_n = 0, en_cap = 0;
  if (!qa_save_reader_field(&reader, "enabled", &field)) qa_range_error("Out of memory");
  size_t count = qa_save_reader_list_count(&field);
  for (size_t i = 0; i < count; i++) {
    if (!qa_save_reader_item(&field, i, &entry)) qa_range_error("Out of memory");
    if (!qa_save_reader_field(&entry, "id", &sub)) qa_range_error("Out of memory");
    int64_t id = (int64_t)qa_save_reader_integer_min(&sub, 0);
    qa_save_reader_destroy(&sub);
    bool dup = false;
    for (size_t j = 0; j < en_n; j++) if (enabled[j].id == id) dup = true;
    if (!qa_navigation_runtime_node(rt, id) || dup) qa_save_reader_fail(&entry, "unknown or duplicate navigation identity");
    if (!qa_save_reader_field(&entry, "enabled", &sub)) qa_range_error("Out of memory");
    bool v = qa_save_reader_boolean(&sub);
    qa_save_reader_destroy(&sub);
    qa_save_reader_destroy(&entry);
    if (en_n == en_cap) { en_cap = en_cap ? en_cap * 2 : 8; enabled = realloc(enabled, en_cap * sizeof *enabled); if (!enabled) qa_range_error("Out of memory"); }
    enabled[en_n++] = (qa_navigation_id_bool){id, v};
  }
  qa_save_reader_destroy(&field);
  /* blocked (known edge ids) */
  qa_navigation_id_reason *blocked = NULL; size_t bl_n = 0, bl_cap = 0;
  if (!qa_save_reader_field(&reader, "blocked", &field)) qa_range_error("Out of memory");
  count = qa_save_reader_list_count(&field);
  for (size_t i = 0; i < count; i++) {
    if (!qa_save_reader_item(&field, i, &entry)) qa_range_error("Out of memory");
    if (!qa_save_reader_field(&entry, "id", &sub)) qa_range_error("Out of memory");
    int64_t id = (int64_t)qa_save_reader_integer_min(&sub, 0);
    qa_save_reader_destroy(&sub);
    bool known = false, dup = false;
    for (size_t j = 0; j < rt->graph->edge_count; j++) if (rt->graph->edges[j].id == id) known = true;
    for (size_t j = 0; j < bl_n; j++) if (blocked[j].id == id) dup = true;
    if (!known || dup) qa_save_reader_fail(&entry, "unknown or duplicate navigation identity");
    if (!qa_save_reader_field(&entry, "reason", &sub)) qa_range_error("Out of memory");
    const char *reason = qa_save_reader_string(&sub);
    char *copy = dups(reason);
    qa_save_reader_destroy(&sub);
    qa_save_reader_destroy(&entry);
    if (!copy) qa_range_error("Out of memory");
    if (bl_n == bl_cap) { bl_cap = bl_cap ? bl_cap * 2 : 8; blocked = realloc(blocked, bl_cap * sizeof *blocked); if (!blocked) qa_range_error("Out of memory"); }
    blocked[bl_n++] = (qa_navigation_id_reason){id, copy};
  }
  qa_save_reader_destroy(&field);
  /* admissionSeconds (known edge ids) */
  qa_navigation_id_seconds *admissions = NULL; size_t ad_n = 0, ad_cap = 0;
  if (!qa_save_reader_field(&reader, "admissionSeconds", &field)) qa_range_error("Out of memory");
  count = qa_save_reader_list_count(&field);
  for (size_t i = 0; i < count; i++) {
    if (!qa_save_reader_item(&field, i, &entry)) qa_range_error("Out of memory");
    if (!qa_save_reader_field(&entry, "id", &sub)) qa_range_error("Out of memory");
    int64_t id = (int64_t)qa_save_reader_integer_min(&sub, 0);
    qa_save_reader_destroy(&sub);
    bool known = false, dup = false;
    for (size_t j = 0; j < rt->graph->edge_count; j++) if (rt->graph->edges[j].id == id) known = true;
    for (size_t j = 0; j < ad_n; j++) if (admissions[j].id == id) dup = true;
    if (!known || dup) qa_save_reader_fail(&entry, "unknown or duplicate navigation identity");
    if (!qa_save_reader_field(&entry, "seconds", &sub)) qa_range_error("Out of memory");
    double seconds = qa_save_reader_finite(&sub);
    qa_save_reader_destroy(&sub);
    if (seconds < 0) qa_save_reader_fail(&entry, "negative admission duration");
    qa_save_reader_destroy(&entry);
    if (ad_n == ad_cap) { ad_cap = ad_cap ? ad_cap * 2 : 8; admissions = realloc(admissions, ad_cap * sizeof *admissions); if (!admissions) qa_range_error("Out of memory"); }
    admissions[ad_n++] = (qa_navigation_id_seconds){id, seconds};
  }
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&reader, "worldRevision", &field)) qa_range_error("Out of memory");
  double revision = qa_save_reader_finite(&field);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&reader, "generation", &field)) qa_range_error("Out of memory");
  int64_t generation = (int64_t)qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&reader);
  free(rt->enabled); rt->enabled = enabled; rt->enabled_count = en_n; rt->enabled_cap = en_cap;
  for (size_t i = 0; i < rt->blocked_count; i++) free(rt->blocked[i].reason);
  free(rt->blocked); rt->blocked = blocked; rt->blocked_count = bl_n; rt->blocked_cap = bl_cap;
  free(rt->admission_seconds); rt->admission_seconds = admissions; rt->admission_count = ad_n; rt->admission_cap = ad_cap;
  rt->world_revision = (int64_t)revision; rt->generation = generation;
  qa_navigation_estimates_invalidate(&rt->estimates);
}
int64_t qa_navigation_runtime_generation(qa_navigation_runtime *rt) { refresh(rt); return rt->generation; }
bool qa_navigation_runtime_boarding_elevator(qa_navigation_runtime *rt, int64_t node, qa_navigation_boarding_elevator *out) {
  size_t n = 0;
  const qa_navigation_edge *const *edges = qa_navigation_runtime_outgoing(rt, node, &n);
  for (size_t i = 0; i < n; i++) {
    const qa_navigation_edge *edge = edges[i];
    if ((edge->source.kind != QA_NS_NAV3 && edge->source.kind != QA_NS_NAV2) || edge->mode != QA_TM_MOVER ||
        edge->source_travel_type != 6 || !edge->has_entity) continue;
    qa_navigation_entity_state state;
    if (!rt->world->get_entity(rt->world->ctx, edge->entity, &state)) continue;
    if (state.has_elevator && state.enabled && !state.locked) {
      out->edge = edge; out->actor = state.actor;
      out->origin = state.elevator_origin; out->bottom = state.elevator_bottom; out->top = state.elevator_top;
      out->phase = state.elevator_phase;
      return true;
    }
  }
  return false;
}
bool qa_navigation_runtime_boarding_train(qa_navigation_runtime *rt, int64_t node, qa_navigation_boarding_train *out) {
  size_t n = 0;
  const qa_navigation_edge *const *edges = qa_navigation_runtime_outgoing(rt, node, &n);
  for (size_t i = 0; i < n; i++) {
    const qa_navigation_edge *edge = edges[i];
    if (edge->mode != QA_TM_MOVER || !edge->has_entity) continue;
    qa_navigation_entity_state state;
    if (!rt->world->get_entity(rt->world->ctx, edge->entity, &state)) continue;
    if (!state.has_train || !state.train_running) continue;
    qa_navigation_train_ride ride;
    if (qa_navigation_find_train_ride(&state, edge, &rt->graph->profile, &ride)) {
      out->edge = edge; out->state = state; out->boarding = ride.boarding; out->arrival = ride.arrival;
      return true;
    }
  }
  return false;
}
static qa_bot_transport_step step_unavailable(void) { return (qa_bot_transport_step){true, QA_TRANSPORT_UNAVAILABLE, false, {0, 0, 0}}; }
qa_bot_transport_step qa_navigation_runtime_train_step(qa_navigation_runtime *rt, const qa_navigation_edge *edge,
                                                       qa_vec3 origin, const qa_actor_id *ground) {
  qa_bot_transport_step none; memset(&none, 0, sizeof none);
  if (edge->mode != QA_TM_MOVER || !edge->has_entity) return none;
  qa_navigation_entity_state state;
  if (!rt->world->get_entity(rt->world->ctx, edge->entity, &state) || !state.has_train) return none;
  qa_navigation_train_ride ride;
  bool has_ride = qa_navigation_find_train_ride(&state, edge, &rt->graph->profile, &ride);
  bool aboard = ground && qa_same_actor(*ground, state.actor);
  if (!has_ride || !static_edge_allowed(rt, edge, NULL) || (!aboard && !state.train_running)) return step_unavailable();
  if (rt->world->hazard(rt->world->ctx, qa_nav_translated(origin, rt->graph->profile.shape.bounds))) return step_unavailable();
  if (aboard && !state.train_running && qa_nav_distance(state.train_origin, ride.arrival.origin) > rt->graph->profile.maximum_step) return step_unavailable();
  if (aboard && qa_nav_distance(state.train_origin, ride.arrival.origin) > rt->graph->profile.maximum_step)
    return (qa_bot_transport_step){true, QA_TRANSPORT_RIDE, false, {0, 0, 0}};
  qa_vec3 staging = edge->has_hint ? edge->hint.funnel : edge->start;
  qa_vec3 target = aboard ? edge->end
                   : qa_nav_distance(state.train_origin, ride.boarding.origin) <= rt->graph->profile.maximum_step ? edge->start : staging;
  if (!aboard && target.x == staging.x && target.y == staging.y && target.z == staging.z && qa_nav_distance(origin, staging) <= 8)
    return (qa_bot_transport_step){true, QA_TRANSPORT_WAIT, false, {0, 0, 0}};
  if (rt->world->hazard(rt->world->ctx, qa_nav_translated(target, rt->graph->profile.shape.bounds))) return step_unavailable();
  qa_traversal_request req; memset(&req, 0, sizeof req);
  req.from = origin; req.to = target; req.mode = QA_TM_WALK;
  qa_traversal_admission admission = rt->world->admit(rt->world->ctx, req, rt->graph->profile);
  bool admitted = admission.admitted;
  qa_traversal_admission_destroy(&admission);
  return admitted ? (qa_bot_transport_step){true, QA_TRANSPORT_MOVE, aboard, target} : step_unavailable();
}
bool qa_navigation_runtime_enable_area(qa_navigation_runtime *rt, int64_t id, bool enabled) {
  const qa_navigation_node *node = qa_navigation_runtime_node(rt, id);
  if (!node) qa_range_error("Unknown navigation area %lld", (long long)id);
  qa_navigation_id_bool *en = enabled_find(rt, id);
  bool previous = en ? en->enabled : !(node->source.kind == QA_NS_AAS && (node->flags & 8) != 0);
  if (previous == enabled) return previous;
  if (!enabled_set(rt, id, enabled)) qa_range_error("Out of memory");
  invalidate(rt); qa_navigation_estimates_invalidate(&rt->estimates);
  return previous;
}
void qa_navigation_runtime_block_edge(qa_navigation_runtime *rt, int64_t id, const char *reason) {
  bool known = false;
  for (size_t i = 0; i < rt->graph->edge_count; i++) if (rt->graph->edges[i].id == id) known = true;
  if (!known) qa_range_error("Unknown navigation edge %lld", (long long)id);
  qa_navigation_id_reason *existing = blocked_find(rt, id);
  if (!reason) {
    if (existing) {
      size_t idx = (size_t)(existing - rt->blocked);
      free(existing->reason);
      memmove(&rt->blocked[idx], &rt->blocked[idx + 1], (rt->blocked_count - idx - 1) * sizeof *rt->blocked);
      rt->blocked_count--;
    }
  } else if (existing) {
    char *copy = dups(reason);
    if (!copy) qa_range_error("Out of memory");
    free(existing->reason); existing->reason = copy;
  } else {
    if (rt->blocked_count == rt->blocked_cap) {
      size_t c = rt->blocked_cap ? rt->blocked_cap * 2 : 8;
      qa_navigation_id_reason *p = realloc(rt->blocked, c * sizeof *p);
      if (!p) qa_range_error("Out of memory");
      rt->blocked = p; rt->blocked_cap = c;
    }
    char *copy = dups(reason);
    if (!copy) qa_range_error("Out of memory");
    rt->blocked[rt->blocked_count++] = (qa_navigation_id_reason){id, copy};
  }
  invalidate(rt); qa_navigation_estimates_invalidate(&rt->estimates);
}

static bool evaluate_node_allowed(qa_navigation_runtime *rt, const qa_navigation_node *node,
                                  const qa_navigation_route_query *query, bool await_elevator) {
  qa_navigation_profile profile;
  if (!qa_nav_node_profile(&rt->graph->profile, node, &profile)) return false;
  if (!static_node_allowed(rt, node, query)) return false;
  int64_t medium = qa_nav_contents_world(rt->world, &profile, node->origin);
  if ((medium & 6) != 0 || ((medium & 1) != 0 && !(profile.capabilities & (1u << QA_TM_SWIM)))) return false;
  if (rt->world->hazard(rt->world->ctx, qa_nav_translated(node->origin, profile.shape.bounds))) return false;
  if (!await_elevator && !qa_nav_clear_world(rt->world, &profile, node->origin, node->origin)) return false;
  if (node->source.kind == QA_NS_NAV3 && (node->flags & 2048) != 0 && (medium & 7) == 0) return false;
  if (node->source.kind == QA_NS_NAV3 && (node->flags & 64) != 0) {
    qa_trace_shape shape; memset(&shape, 0, sizeof shape);
    shape.kind = "box";
    shape.bounds = profile.shape.bounds;
    shape.bounds.min.z = 0; shape.bounds.max.z = 0;
    qa_query_target target; memset(&target, 0, sizeof target);
    qa_vec3 end = node->origin; end.z -= 96;
    qa_trace_query q = {node->origin, end, shape, target, profile.policy, profile.movement.numeric, rt->world->pass_actor};
    qa_trace_result result = rt->world->scene.trace(rt->world->scene.ctx, q);
    if (result.fraction == 1 && !await_elevator) return false;
  }
  return true;
}
typedef bool (*NodeAllowedFn)(void *ctx, const qa_navigation_node *node, bool await_elevator);
static bool plain_node_allowed(void *ctx, const qa_navigation_node *node, bool await_elevator) {
  void **pair = ctx;
  return evaluate_node_allowed(pair[0], node, pair[1], await_elevator);
}
static bool edge_allowed_full(qa_navigation_runtime *rt, const qa_navigation_edge *edge,
                              const qa_navigation_route_query *query, NodeAllowedFn node_allowed, void *na_ctx) {
  if (!static_edge_allowed(rt, edge, query)) return false;
  const qa_navigation_node *target = qa_navigation_runtime_node(rt, edge->to);
  qa_navigation_boarding_elevator elevator;
  bool has_elevator = qa_navigation_runtime_boarding_elevator(rt, edge->to, &elevator);
  qa_navigation_boarding_train train;
  bool has_train = qa_navigation_runtime_boarding_train(rt, edge->to, &train);
  if (!target) return false;
  qa_navigation_entity_state source_state;
  bool has_source = edge->mode == QA_TM_MOVER && edge->has_entity &&
                    rt->world->get_entity(rt->world->ctx, edge->entity, &source_state);
  qa_navigation_train_ride ride;
  bool has_ride = has_source && qa_navigation_find_train_ride(&source_state, edge, &rt->graph->profile, &ride);
  bool awaiting_train =
      (has_train && qa_nav_distance(train.state.train_origin, train.boarding.origin) > rt->graph->profile.maximum_step) ||
      (has_source && source_state.has_train && source_state.train_running && has_ride &&
       qa_nav_distance(source_state.train_origin, ride.arrival.origin) > rt->graph->profile.maximum_step);
  if (awaiting_train && !qa_nav_clear_world(rt->world, &rt->graph->profile, target->origin, target->origin)) return false;
  if (!node_allowed(na_ctx, target, awaiting_train || (has_elevator && elevator.phase != 0))) return false;
  if (edge->has_entity) {
    qa_navigation_entity_state state;
    if (!rt->world->get_entity(rt->world->ctx, edge->entity, &state) || !state.enabled || state.locked) return false;
  }
  return true;
}
bool qa_navigation_runtime_edge_allowed(qa_navigation_runtime *rt, const qa_navigation_edge *edge,
                                        const qa_navigation_route_query *query) {
  void *pair[2] = {rt, (void *)query};
  return edge_allowed_full(rt, edge, query, plain_node_allowed, pair);
}
const qa_navigation_node *qa_navigation_runtime_nearest(qa_navigation_runtime *rt, qa_vec3 point, double radius) {
  refresh(rt);
  typedef struct { const qa_navigation_node *node; double d; qa_navigation_profile profile; size_t order; bool crouched; } Cand;
  Cand *cands = NULL; size_t cn = 0, ccap = 0;
  for (size_t i = 0; i < rt->graph->node_count; i++) {
    const qa_navigation_node *node = &rt->graph->nodes[i];
    double d = qa_nav_distance(point, node->origin);
    if (d > radius) continue;
    qa_navigation_profile profile;
    if (!qa_nav_node_profile(&rt->graph->profile, node, &profile)) continue;
    bool crouched = !(profile.shape.bounds.min.x == rt->graph->profile.shape.bounds.min.x &&
                      profile.shape.bounds.min.y == rt->graph->profile.shape.bounds.min.y &&
                      profile.shape.bounds.min.z == rt->graph->profile.shape.bounds.min.z &&
                      profile.shape.bounds.max.x == rt->graph->profile.shape.bounds.max.x &&
                      profile.shape.bounds.max.y == rt->graph->profile.shape.bounds.max.y &&
                      profile.shape.bounds.max.z == rt->graph->profile.shape.bounds.max.z);
    if (cn == ccap) { ccap = ccap ? ccap * 2 : 16; cands = realloc(cands, ccap * sizeof *cands); if (!cands) return NULL; }
    cands[cn] = (Cand){node, d, profile, cn, crouched}; cn++;
  }
  /* stable sort by distance (insertion order breaks ties, TS sort is stable) */
  for (size_t i = 1; i < cn; i++) {
    Cand key = cands[i]; size_t j = i;
    while (j > 0 && (cands[j - 1].d > key.d || (cands[j - 1].d == key.d && cands[j - 1].order > key.order))) { cands[j] = cands[j - 1]; j--; }
    cands[j] = key;
  }
  int solid_cache[2] = {-1, -1};
  const qa_navigation_node *selected = NULL;
  double best = radius;
  for (size_t i = 0; i < cn; i++) {
    Cand *c = &cands[i];
    if (c->d > best) break;
    int slot = c->crouched ? 1 : 0;
    if (solid_cache[slot] < 0) {
      qa_trace_result probe = qa_nav_trace_world(rt->world, &c->profile, point, point);
      solid_cache[slot] = (probe.start_solid || probe.all_solid) ? 1 : 0;
    }
    if (solid_cache[slot]) continue;
    if (evaluate_node_allowed(rt, c->node, NULL, false) && qa_nav_clear_world(rt->world, &c->profile, point, c->node->origin)) {
      best = c->d; selected = c->node;
    }
  }
  free(cands);
  return selected;
}
bool qa_navigation_runtime_area_at(qa_navigation_runtime *rt, qa_vec3 point, int64_t *out) {
  if (rt->graph->aas_asset) {
    int64_t area = qa_aas_point_area(rt->graph->aas_asset, point);
    if (!qa_navigation_runtime_node(rt, area)) return false;
    *out = area; return true;
  }
  const qa_navigation_node *node = qa_navigation_runtime_nearest(rt, point, 512);
  if (!node) return false;
  *out = node->id; return true;
}
int64_t *qa_navigation_runtime_bbox_areas(qa_navigation_runtime *rt, qa_bounds bounds, size_t *out_count) {
  if (rt->graph->aas_asset)
    return qa_aas_bbox_areas(rt->graph->aas_asset, bounds, (int64_t)rt->graph->aas_asset->area_count, out_count);
  int64_t *out = malloc((rt->graph->node_count ? rt->graph->node_count : 1) * sizeof *out);
  if (!out) { *out_count = 0; return NULL; }
  size_t n = 0;
  for (size_t i = 0; i < rt->graph->node_count; i++) {
    const qa_navigation_node *node = &rt->graph->nodes[i];
    if (node->bounds.min.x <= bounds.max.x && node->bounds.max.x >= bounds.min.x &&
        node->bounds.min.y <= bounds.max.y && node->bounds.max.y >= bounds.min.y &&
        node->bounds.min.z <= bounds.max.z && node->bounds.max.z >= bounds.min.z)
      out[n++] = node->id;
  }
  *out_count = n;
  return out;
}
qa_navigation_area_crossing *qa_navigation_runtime_trace_areas(qa_navigation_runtime *rt, qa_vec3 start, qa_vec3 end,
                                                               int64_t maximum, size_t *out_count) {
  if (rt->graph->aas_asset) {
    qa_aas_area_crossing *crossings = qa_aas_trace_areas(rt->graph->aas_asset, start, end, maximum, out_count);
    /* qa_aas_area_crossing and qa_navigation_area_crossing share layout */
    return (qa_navigation_area_crossing *)crossings;
  }
  if (maximum < 0) qa_range_error("Invalid navigation area limit");
  typedef struct { int64_t area; qa_vec3 point; double fraction; size_t order; } Cross;
  Cross *crossed = NULL; size_t cn = 0, ccap = 0;
  *out_count = 0;
  for (size_t i = 0; i < rt->graph->node_count; i++) {
    const qa_navigation_node *node = &rt->graph->nodes[i];
    double enter = 0, leave = 1;
    double s[3] = {start.x, start.y, start.z}, e[3] = {end.x, end.y, end.z};
    double mn[3] = {node->bounds.min.x, node->bounds.min.y, node->bounds.min.z};
    double mx[3] = {node->bounds.max.x, node->bounds.max.y, node->bounds.max.z};
    for (int axis = 0; axis < 3; axis++) {
      double delta = e[axis] - s[axis];
      if (delta == 0) { if (s[axis] < mn[axis] || s[axis] > mx[axis]) leave = -1; continue; }
      double first = (mn[axis] - s[axis]) / delta, second = (mx[axis] - s[axis]) / delta;
      enter = fmax(enter, fmin(first, second)); leave = fmin(leave, fmax(first, second));
    }
    if (enter <= leave) {
      if (cn == ccap) { ccap = ccap ? ccap * 2 : 16; crossed = realloc(crossed, ccap * sizeof *crossed); if (!crossed) return NULL; }
      crossed[cn++] = (Cross){node->id,
                              {start.x + (end.x - start.x) * enter, start.y + (end.y - start.y) * enter,
                               start.z + (end.z - start.z) * enter},
                              enter, cn};
    }
  }
  for (size_t i = 1; i < cn; i++) {
    Cross key = crossed[i]; size_t j = i;
    while (j > 0 && (crossed[j - 1].fraction > key.fraction || (crossed[j - 1].fraction == key.fraction && crossed[j - 1].order > key.order))) { crossed[j] = crossed[j - 1]; j--; }
    crossed[j] = key;
  }
  size_t n = cn < (size_t)maximum ? cn : (size_t)maximum;
  qa_navigation_area_crossing *out = malloc((n ? n : 1) * sizeof *out);
  if (!out) { free(crossed); return NULL; }
  for (size_t i = 0; i < n; i++) out[i] = (qa_navigation_area_crossing){crossed[i].area, crossed[i].point};
  free(crossed);
  *out_count = n;
  return out;
}

/* --- candidate search (Dijkstra with TS Queue heap) --- */
typedef struct { int64_t node; double cost; } QItem;
typedef struct { QItem *v; size_t n, cap; } Queue;
static bool qpush(Queue *q, QItem value) {
  if (q->n == q->cap) { size_t c = q->cap ? q->cap * 2 : 16; QItem *p = realloc(q->v, c * sizeof *p); if (!p) return false; q->v = p; q->cap = c; }
  size_t index = q->n; q->v[q->n++] = value;
  while (index > 0) {
    size_t parent = (index - 1) / 2;
    if (q->v[parent].cost <= value.cost) break;
    q->v[index] = q->v[parent]; index = parent;
  }
  q->v[index] = value; return true;
}
static bool qpop(Queue *q, QItem *out) {
  if (!q->n) return false;
  QItem result = q->v[0], tail = q->v[--q->n];
  if (!q->n) { *out = result; return true; }
  size_t index = 0;
  while (index * 2 + 1 < q->n) {
    size_t child = index * 2 + 1;
    if (child + 1 < q->n && q->v[child + 1].cost < q->v[child].cost) child++;
    if (tail.cost <= q->v[child].cost) break;
    q->v[index] = q->v[child]; index = child;
  }
  q->v[index] = tail; *out = result; return true;
}
typedef struct { int64_t node; double cost; const qa_navigation_edge *parent; bool has_cost; } CostEntry;
static CostEntry *cost_find(CostEntry *v, size_t n, int64_t node) {
  for (size_t i = 0; i < n; i++) if (v[i].node == node) return &v[i];
  return NULL;
}
/* Returns owned edge-pointer array or NULL (no candidate / OOM). */
static const qa_navigation_edge **candidate(qa_navigation_runtime *rt, int64_t start, int64_t goal,
                                            const qa_navigation_route_query *query,
                                            const int64_t *rejected, size_t rejected_count,
                                            NodeAllowedFn node_allowed, void *na_ctx, size_t *out_count) {
  *out_count = 0;
  Queue queue = {0};
  CostEntry *costs = NULL; size_t cn = 0, ccap = 0;
  if (!qpush(&queue, (QItem){start, 0})) return NULL;
  costs = malloc(8 * sizeof *costs); if (!costs) { free(queue.v); return NULL; }
  ccap = 8; costs[cn++] = (CostEntry){start, 0, NULL, true};
  QItem current;
  while (qpop(&queue, &current)) {
    CostEntry *known = cost_find(costs, cn, current.node);
    if (!known || !known->has_cost || known->cost != current.cost) continue;
    if (current.node == goal) {
      size_t length = 0;
      for (int64_t node = goal; node != start;) {
        CostEntry *e = cost_find(costs, cn, node);
        if (!e || !e->parent) qa_range_error("Navigation predecessor chain is incomplete");
        length++; node = e->parent->from;
      }
      const qa_navigation_edge **path = malloc((length ? length : 1) * sizeof *path);
      if (!path) { free(queue.v); free(costs); return NULL; }
      size_t at = length;
      for (int64_t node = goal; node != start;) {
        CostEntry *e = cost_find(costs, cn, node);
        path[--at] = e->parent; node = e->parent->from;
      }
      free(queue.v); free(costs);
      *out_count = length;
      return path;
    }
    size_t oc = 0;
    const qa_navigation_edge *const *edges = qa_navigation_runtime_outgoing(rt, current.node, &oc);
    for (size_t i = 0; i < oc; i++) {
      const qa_navigation_edge *edge = edges[i];
      bool rej = false;
      for (size_t j = 0; j < rejected_count; j++) if (rejected[j] == edge->id) rej = true;
      if (rej || !edge_allowed_full(rt, edge, query, node_allowed, na_ctx)) continue;
      qa_navigation_id_seconds *adm = admission_find(rt, edge->id);
      double cost = current.cost + (adm ? adm->seconds : edge->travel_seconds);
      CostEntry *existing = cost_find(costs, cn, edge->to);
      if (cost >= (existing && existing->has_cost ? existing->cost : INFINITY)) continue;
      if (!existing) {
        if (cn == ccap) { ccap *= 2; costs = realloc(costs, ccap * sizeof *costs); if (!costs) { free(queue.v); return NULL; } }
        costs[cn++] = (CostEntry){edge->to, cost, edge, true};
      } else { existing->cost = cost; existing->parent = edge; existing->has_cost = true; }
      if (!qpush(&queue, (QItem){edge->to, cost})) { free(queue.v); free(costs); return NULL; }
    }
  }
  free(queue.v); free(costs);
  return NULL;
}

/* --- traversal --- */
typedef struct {
  qa_navigation_route_prediction prediction;
  qa_vec3 cursor; double seconds;
  qa_vec3 *points; size_t point_count, point_cap;
} RouteTraversal;
static bool traversal_push(RouteTraversal *st, qa_vec3 p) {
  if (st->point_count == st->point_cap) {
    size_t c = st->point_cap ? st->point_cap * 2 : 16;
    qa_vec3 *np = realloc(st->points, c * sizeof *np);
    if (!np) return false;
    st->points = np; st->point_cap = c;
  }
  st->points[st->point_count++] = p; return true;
}
static void traversal_init(qa_navigation_runtime *rt, RouteTraversal *st, qa_vec3 from) {
  memset(st, 0, sizeof *st);
  st->prediction = rt->world->begin_route(rt->world->ctx, rt->graph->profile);
  st->cursor = from;
  (void)traversal_push(st, from);
}
static qa_traversal_admission traversal_admit(RouteTraversal *st, qa_traversal_request request) {
  qa_traversal_admission result = st->prediction.admit(st->prediction.ctx, request);
  if (result.admitted) {
    if (result.trajectory_count == 0 || !isfinite(result.seconds) || result.seconds < 0)
      qa_range_error("Movement admission returned no trajectory or invalid duration");
    st->seconds += result.seconds;
    for (size_t i = 1; i < result.trajectory_count; i++) (void)traversal_push(st, result.trajectory[i]);
    st->cursor = result.trajectory[result.trajectory_count - 1];
  }
  return result;
}
static qa_traversal_request make_request(qa_vec3 from, qa_vec3 to, qa_travel_mode mode,
                                         bool has_hint, qa_traversal_hint hint,
                                         bool has_entity, qa_navigation_entity_binding entity) {
  qa_traversal_request r; memset(&r, 0, sizeof r);
  r.from = from; r.to = to; r.mode = mode; r.has_hint = has_hint; r.hint = hint;
  r.has_entity = has_entity; r.entity = entity;
  return r;
}
static bool traverse_edge(qa_navigation_runtime *rt, const qa_navigation_edge *edge, RouteTraversal *state) {
  qa_navigation_entity_state train_state;
  bool has_train_state = edge->mode == QA_TM_MOVER && edge->has_entity &&
                         rt->world->get_entity(rt->world->ctx, edge->entity, &train_state) && train_state.has_train;
  if (has_train_state) {
    qa_navigation_train_ride ride;
    if (!train_state.train_running || !qa_navigation_find_train_ride(&train_state, edge, &rt->graph->profile, &ride)) return false;
    qa_vec3 staging = edge->has_hint ? edge->hint.funnel : edge->start;
    if (qa_nav_distance(state->cursor, staging) > 1) {
      qa_traversal_admission a = traversal_admit(state, make_request(state->cursor, staging, QA_TM_WALK, false, (qa_traversal_hint){0}, false, (qa_navigation_entity_binding){0}));
      bool ok = a.admitted; qa_traversal_admission_destroy(&a);
      if (!ok) return false;
    }
    (void)traversal_push(state, edge->start); (void)traversal_push(state, edge->end);
    state->cursor = edge->end; state->seconds += edge->travel_seconds;
    state->prediction = rt->world->begin_route(rt->world->ctx, rt->graph->profile);
    return true;
  }
  qa_navigation_entity_state mover;
  bool has_mover = edge->mode == QA_TM_MOVER && (edge->source.kind == QA_NS_NAV3 || edge->source.kind == QA_NS_NAV2) &&
                   edge->source_travel_type == 6 && edge->has_entity &&
                   rt->world->get_entity(rt->world->ctx, edge->entity, &mover) && mover.has_elevator;
  if (has_mover && mover.enabled && !mover.locked) {
    if (mover.elevator_top.z <= mover.elevator_bottom.z || mover.elevator_top.x != mover.elevator_bottom.x ||
        mover.elevator_top.y != mover.elevator_bottom.y) return false;
    qa_vec3 staging = edge->has_hint ? edge->hint.funnel : edge->start;
    const qa_navigation_node *from_node = qa_navigation_runtime_node(rt, edge->from);
    double radius = from_node ? from_node->radius : 0;
    double limit = fmax(rt->graph->profile.maximum_step, radius);
    if (qa_nav_distance(state->cursor, edge->start) > limit && qa_nav_distance(state->cursor, staging) > 1) {
      qa_traversal_admission a = traversal_admit(state, make_request(state->cursor, staging, QA_TM_WALK, false, (qa_traversal_hint){0}, false, (qa_navigation_entity_binding){0}));
      bool ok = a.admitted; qa_traversal_admission_destroy(&a);
      if (!ok) return false;
    }
    (void)traversal_push(state, edge->end);
    state->cursor = edge->end; state->seconds += edge->travel_seconds;
    state->prediction = rt->world->begin_route(rt->world->ctx, rt->graph->profile);
    return true;
  }
  if (qa_nav_distance(state->cursor, edge->start) > 1) {
    qa_traversal_admission a = traversal_admit(state, make_request(state->cursor, edge->start,
        edge->mode == QA_TM_CROUCH ? QA_TM_CROUCH : QA_TM_WALK, false, (qa_traversal_hint){0}, false, (qa_navigation_entity_binding){0}));
    bool ok = a.admitted; qa_traversal_admission_destroy(&a);
    if (!ok) return false;
  }
  qa_vec3 landing = edge->end;
  qa_navigation_boarding_elevator boarding;
  bool has_boarding = edge->mode == QA_TM_WALK && (edge->source.kind == QA_NS_NAV3 || edge->source.kind == QA_NS_NAV2) &&
                      qa_navigation_runtime_boarding_elevator(rt, edge->to, &boarding);
  qa_navigation_boarding_train train_boarding;
  bool has_train_boarding = edge->mode == QA_TM_WALK && qa_navigation_runtime_boarding_train(rt, edge->to, &train_boarding);
  if (has_train_boarding && qa_nav_distance(train_boarding.state.train_origin, train_boarding.boarding.origin) > rt->graph->profile.maximum_step)
    landing = train_boarding.edge->has_hint ? train_boarding.edge->hint.funnel : edge->start;
  else if (has_boarding && boarding.phase != 0) landing = edge->start;
  else if (has_boarding) {
    qa_query_target target; memset(&target, 0, sizeof target);
    qa_vec3 end = edge->end; end.z -= 96;
    qa_trace_query q = {edge->end, end, rt->graph->profile.shape, target, rt->graph->profile.policy,
                        rt->graph->profile.movement.numeric, rt->world->pass_actor};
    qa_trace_result floor = rt->world->scene.trace(rt->world->scene.ctx, q);
    if (!floor.start_solid && !floor.all_solid && floor.hit.kind == QA_TRACE_HIT_ACTOR &&
        qa_same_actor(floor.hit.actor, boarding.actor) && floor.contact.has_plane &&
        floor.contact.plane.normal.z >= rt->graph->profile.minimum_floor_normal)
      landing = floor.end;
  }
  qa_traversal_admission result = traversal_admit(state, make_request(state->cursor, landing, edge->mode,
      edge->has_hint, edge->hint, edge->has_entity, edge->entity));
  bool admitted = result.admitted;
  if (admitted) (void)admission_set(rt, edge->id, result.seconds);
  qa_traversal_admission_destroy(&result);
  return admitted;
}
qa_traversal_admission qa_navigation_runtime_admit_edge(qa_navigation_runtime *rt, const qa_navigation_edge *edge, qa_vec3 origin) {
  refresh(rt);
  qa_traversal_admission out; memset(&out, 0, sizeof out);
  if (!qa_navigation_runtime_edge_allowed(rt, edge, NULL)) { out.reason = "Selected traversal is disabled or obstructed"; return out; }
  RouteTraversal state; traversal_init(rt, &state, origin);
  if (!traverse_edge(rt, edge, &state)) { free(state.points); out.reason = "Selected movement cannot perform this traversal"; return out; }
  out.admitted = true; out.seconds = state.seconds;
  out.trajectory = state.points; out.trajectory_count = state.point_count;
  return out;
}
qa_navigation_estimate_result qa_navigation_runtime_estimate(qa_navigation_runtime *rt, qa_navigation_estimate_query query) {
  return qa_navigation_estimates_estimate(&rt->estimates, query);
}
typedef struct { qa_navigation_runtime *rt; const qa_navigation_route_query *query;
                 qa_navigation_id_bool *grounded; size_t grounded_count, grounded_cap;
                 qa_navigation_id_bool *elevator; size_t elevator_count, elevator_cap; } CachedAllowed;
static bool cached_lookup(qa_navigation_id_bool *v, size_t n, int64_t id, bool *out) {
  for (size_t i = 0; i < n; i++) if (v[i].id == id) { *out = v[i].enabled; return true; }
  return false;
}
static void cached_store(qa_navigation_id_bool **v, size_t *n, size_t *cap, int64_t id, bool value) {
  if (*n == *cap) { *cap = *cap ? *cap * 2 : 16; *v = realloc(*v, *cap * sizeof **v); if (!*v) return; }
  (*v)[(*n)++] = (qa_navigation_id_bool){id, value};
}
static bool cached_node_allowed(void *ctx, const qa_navigation_node *node, bool await_elevator) {
  CachedAllowed *c = ctx;
  bool known;
  if (await_elevator) {
    if (cached_lookup(c->elevator, c->elevator_count, node->id, &known)) return known;
    known = evaluate_node_allowed(c->rt, node, c->query, true);
    cached_store(&c->elevator, &c->elevator_count, &c->elevator_cap, node->id, known);
    return known;
  }
  if (cached_lookup(c->grounded, c->grounded_count, node->id, &known)) return known;
  known = evaluate_node_allowed(c->rt, node, c->query, false);
  cached_store(&c->grounded, &c->grounded_count, &c->grounded_cap, node->id, known);
  return known;
}
static qa_navigation_route_result unreachable(const char *reason) {
  qa_navigation_route_result r; memset(&r, 0, sizeof r); r.reason = reason; return r;
}
qa_navigation_route_result qa_navigation_runtime_route(qa_navigation_runtime *rt, qa_navigation_route_query query) {
  refresh(rt);
  int64_t start = 0, goal = 0;
  bool has_start = query.has_start_node ? (start = query.start_node, true) : qa_navigation_runtime_area_at(rt, query.start, &start);
  bool has_goal = query.has_goal_node ? (goal = query.goal_node, true) : qa_navigation_runtime_area_at(rt, query.goal, &goal);
  if (!has_start || !has_goal || !qa_navigation_runtime_node(rt, start) || !qa_navigation_runtime_node(rt, goal))
    return unreachable("start or goal has no navigation area");
  const qa_navigation_node *start_node = qa_navigation_runtime_node(rt, start);
  if (!evaluate_node_allowed(rt, start_node, &query, false))
    return unreachable("start area is disabled or occupied");
  CachedAllowed cache; memset(&cache, 0, sizeof cache); cache.rt = rt; cache.query = &query;
  int64_t *rejected = NULL; size_t rejected_count = 0, rejected_cap = 0;
  int64_t maximum_searches = query.has_maximum_searches ? query.maximum_searches : 64;
  qa_navigation_route_result out;
  bool done = false, returned_early = false;
  static char exhausted[128];
  out = unreachable("no route satisfies source flags, character capabilities, and current obstacles");
  for (int64_t attempt = 0; attempt < maximum_searches && !done && !returned_early; attempt++) {
    size_t edge_count = 0;
    const qa_navigation_edge **edges = candidate(rt, start, goal, &query, rejected, rejected_count, cached_node_allowed, &cache, &edge_count);
    if (!edges) { out = unreachable("no route satisfies source flags, character capabilities, and current obstacles"); returned_early = true; break; }
    RouteTraversal state; traversal_init(rt, &state, query.start);
    bool failed = false;
    for (size_t i = 0; i < edge_count; i++) {
      if (!traverse_edge(rt, edges[i], &state)) {
        if (rejected_count == rejected_cap) { rejected_cap = rejected_cap ? rejected_cap * 2 : 16; rejected = realloc(rejected, rejected_cap * sizeof *rejected); }
        if (rejected) rejected[rejected_count++] = edges[i]->id;
        failed = true; break;
      }
    }
    if (failed) { free(state.points); free((void *)edges); continue; }
    if (qa_nav_distance(state.cursor, query.goal) > 1) {
      qa_traversal_admission a = traversal_admit(&state, make_request(state.cursor, query.goal, QA_TM_WALK, false, (qa_traversal_hint){0}, false, (qa_navigation_entity_binding){0}));
      bool ok = a.admitted; qa_traversal_admission_destroy(&a);
      if (!ok) { free(state.points); free((void *)edges); out = unreachable("selected movement cannot reach the goal within its area"); returned_early = true; break; }
    }
    qa_navigation_route route; memset(&route, 0, sizeof route);
    route.map = rt->graph->map;
    route.node_count = edge_count + 1;
    route.nodes = malloc(route.node_count * sizeof *route.nodes);
    route.edge_count = edge_count;
    route.edges = edge_count ? malloc(edge_count * sizeof *route.edges) : NULL;
    if (!route.nodes || (edge_count && !route.edges)) { free(state.points); free((void *)edges); free(route.nodes); free(route.edges); out = unreachable("out of memory"); returned_early = true; break; }
    route.nodes[0] = start;
    for (size_t i = 0; i < edge_count; i++) { route.nodes[i + 1] = edges[i]->to; route.edges[i] = *edges[i]; }
    route.points = state.points; route.point_count = state.point_count;
    route.travel_seconds = state.seconds; route.generation = rt->generation;
    memset(&out, 0, sizeof out); out.is_route = true; out.route = route;
    free((void *)edges);
    done = true;
  }
  if (!done && !returned_early) {
    snprintf(exhausted, sizeof exhausted, "movement admission exhausted %lld candidate routes", (long long)maximum_searches);
    out = unreachable(exhausted);
  }
  free(rejected); free(cache.grounded); free(cache.elevator);
  return out;
}
bool qa_navigation_runtime_route_still_valid(qa_navigation_runtime *rt, const qa_navigation_route *route) {
  refresh(rt);
  if (!route->map.digest || !rt->graph->map.digest || strcmp(route->map.digest, rt->graph->map.digest)) return false;
  if (route->generation != rt->generation) return false;
  for (size_t i = 0; i < route->edge_count; i++) if (!qa_navigation_runtime_edge_allowed(rt, &route->edges[i], NULL)) return false;
  return true;
}
qa_navigation_debug_line *qa_navigation_runtime_debug_lines(qa_navigation_runtime *rt, size_t *out_count) {
  refresh(rt);
  qa_navigation_debug_line *out = malloc((rt->graph->edge_count ? rt->graph->edge_count : 1) * sizeof *out);
  if (!out) { *out_count = 0; return NULL; }
  for (size_t i = 0; i < rt->graph->edge_count; i++) {
    const qa_navigation_edge *e = &rt->graph->edges[i];
    out[i] = (qa_navigation_debug_line){e->start, e->end, e->id, qa_navigation_runtime_edge_allowed(rt, e, NULL), e->mode};
  }
  *out_count = rt->graph->edge_count;
  return out;
}
