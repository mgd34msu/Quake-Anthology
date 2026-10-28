/* Port of src/bots/navigation/aas-cluster.ts.
 * Source clustering and portal discovery from id Software
 * be_aas_cluster.c. GPL-2.0-or-later. */
#include "qa/bots/navigation/aas_cluster.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PORTALS 65536
#define MAX_PORTAL_INDEX 65536
#define MAX_CLUSTERS 65536
#define MAX_PORTAL_AREAS 1024
#define CLUSTER_PORTAL 8
#define ROUTE_PORTAL 32
#define VIEW_PORTAL 512
#define AREA_GROUNDED 1
#define FACE_SOLID 1

typedef struct ClusteringWorld {
  const qa_aas_asset *asset;
  qa_aas_setting_rec *settings; /* owned copy */
  qa_aas_portal_rec *portals; size_t portal_cap;
  qa_aas_cluster_rec *clusters; size_t cluster_cap;
  int64_t *portal_indexes; size_t portal_index_cap;
  int64_t num_portals, num_clusters, portal_index_size;
  bool save_file;
} ClusteringWorld;
typedef struct AasClustering {
  ClusteringWorld *world;
  void *print_ctx;
  qa_aas_cluster_print_fn print_text; /* print and log both route here */
  bool no_face_flood;
} AasClustering;

static void at_fail(int64_t index, size_t length) {
  qa_range_error("AAS clustering index %lld outside initialized allocation of %zu", (long long)index, length);
}
static int64_t abs64(int64_t v) { return v < 0 ? -v : v; }
static int64_t at_i64(const int64_t *values, size_t count, int64_t index) {
  if (index < 0 || index >= (int64_t)count) at_fail(index, count);
  return values[index];
}
static const qa_aas_area_rec *at_area(ClusteringWorld *w, int64_t index) {
  if (index < 0 || index >= (int64_t)w->asset->area_count) at_fail(index, w->asset->area_count);
  return &w->asset->areas[index];
}
static const qa_aas_face_rec *at_face(ClusteringWorld *w, int64_t index) {
  if (index < 0 || index >= (int64_t)w->asset->face_count) at_fail(index, w->asset->face_count);
  return &w->asset->faces[index];
}
static qa_aas_setting_rec *at_setting(ClusteringWorld *w, int64_t index) {
  if (index < 0 || index >= (int64_t)w->asset->setting_count) at_fail(index, w->asset->setting_count);
  return &w->settings[index];
}
static qa_aas_portal_rec *at_portal(ClusteringWorld *w, int64_t index) {
  if (index < 0 || index >= (int64_t)w->portal_cap) at_fail(index, w->portal_cap);
  return &w->portals[index];
}
static qa_aas_cluster_rec *at_cluster(ClusteringWorld *w, int64_t index) {
  if (index < 0 || index >= (int64_t)w->cluster_cap) at_fail(index, w->cluster_cap);
  return &w->clusters[index];
}
static int64_t portal_index_value(ClusteringWorld *w, int64_t index) {
  return at_i64(w->portal_indexes, w->portal_index_cap, index);
}
static void set_portal_index(ClusteringWorld *w, int64_t index, int64_t value) {
  (void)at_i64(w->portal_indexes, w->portal_index_cap, index);
  w->portal_indexes[index] = value;
}
static int64_t source_integer(double value) {
  double integer = trunc(value);
  if (!isfinite(integer) || integer < -2147483648.0 || integer > 2147483647.0)
    qa_range_error("AAS_InitClustering: source float-to-int conversion is undefined");
  return (int64_t)integer;
}
typedef struct { int64_t *v; size_t count, cap; } I64List;
static void list_push(I64List *list, int64_t value) {
  if (list->count == list->cap) {
    size_t c = list->cap ? list->cap * 2 : 16;
    int64_t *p = realloc(list->v, c * sizeof *p);
    if (!p) qa_range_error("Out of memory");
    list->v = p; list->cap = c;
  }
  list->v[list->count++] = value;
}
static void portal_area_push(I64List *values, int64_t value, const char *allocation) {
  if (values->count == MAX_PORTAL_AREAS)
    qa_range_error("AAS_CheckAreaForPossiblePortals: source %s[%d] write exceeds allocation", allocation, MAX_PORTAL_AREAS);
  list_push(values, value);
}
static bool list_contains(const I64List *list, int64_t value) {
  for (size_t i = 0; i < list->count; i++) if (list->v[i] == value) return true;
  return false;
}
static int64_t list_index_of(const I64List *list, int64_t value) {
  for (size_t i = 0; i < list->count; i++) if (list->v[i] == value) return (int64_t)i;
  return -1;
}
static void print_text(AasClustering *cl, const char *text) {
  if (cl->print_text) cl->print_text(cl->print_ctx, text);
}
static void print_severity(AasClustering *cl, int64_t severity, const char *text) {
  (void)severity;
  print_text(cl, text);
}
static bool connected_areas(ClusteringWorld *world, const I64List *area_numbers) {
  if (area_numbers->count < 1) return false;
  if (area_numbers->count == 1) return true;
  uint8_t *connected = calloc(MAX_PORTAL_AREAS, 1);
  if (!connected) qa_range_error("Out of memory");
  I64List pending = {0, 0, 0};
  list_push(&pending, 0);
  while (pending.count != 0) {
    int64_t current = pending.v[--pending.count];
    if (current < 0 || current >= MAX_PORTAL_AREAS) at_fail(current, MAX_PORTAL_AREAS);
    if (connected[current] != 0) continue;
    connected[current] = 1;
    int64_t area_number = at_i64(area_numbers->v, area_numbers->count, current);
    const qa_aas_area_rec *area = at_area(world, area_number);
    for (int64_t face_index = area->face_count - 1; face_index >= 0; face_index--) {
      const qa_aas_face_rec *face = at_face(world, abs64(at_i64(world->asset->face_indexes, world->asset->face_index_count, area->first_face + face_index)));
      if ((face->flags & FACE_SOLID) != 0) continue;
      int64_t other_area = face->front_area != area_number ? face->front_area : face->back_area;
      int64_t other_index = list_index_of(area_numbers, other_area);
      if (other_index >= 0) {
        if (other_index >= MAX_PORTAL_AREAS) at_fail(other_index, MAX_PORTAL_AREAS);
        if (connected[other_index] == 0) list_push(&pending, other_index);
      }
    }
  }
  free(pending.v);
  bool all = true;
  for (size_t index = 0; index < area_numbers->count; index++) {
    if (index >= MAX_PORTAL_AREAS) at_fail((int64_t)index, MAX_PORTAL_AREAS);
    if (connected[index] == 0) all = false;
  }
  free(connected);
  return all;
}
static void remove_cluster_areas(AasClustering *cl) {
  for (int64_t area = 1; area < (int64_t)cl->world->asset->area_count; area++)
    at_setting(cl->world, area)->cluster = 0;
}
static bool update_portal(AasClustering *cl, int64_t area, int64_t cluster_number) {
  ClusteringWorld *w = cl->world;
  char message[256];
  int64_t portal_number = 1;
  for (; portal_number < w->num_portals; portal_number++)
    if (at_portal(w, portal_number)->area == area) break;
  if (portal_number == w->num_portals) {
    snprintf(message, sizeof message, "no portal of area %lld", (long long)area);
    print_severity(cl, 4, message);
    return true;
  }
  qa_aas_portal_rec *portal = at_portal(w, portal_number);
  if (portal->front_cluster == cluster_number || portal->back_cluster == cluster_number) return true;
  if (portal->front_cluster == 0) portal->front_cluster = cluster_number;
  else if (portal->back_cluster == 0) portal->back_cluster = cluster_number;
  else {
    at_setting(w, area)->contents &= ~CLUSTER_PORTAL;
    snprintf(message, sizeof message, "portal area %lld is seperating more than two clusters\r\n", (long long)area);
    print_text(cl, message);
    return false;
  }
  if (w->portal_index_size >= MAX_PORTAL_INDEX) {
    print_severity(cl, 4, "AAS_MAX_PORTALINDEXSIZE");
    return true;
  }
  at_setting(w, area)->cluster = -portal_number;
  qa_aas_cluster_rec *cluster = at_cluster(w, cluster_number);
  set_portal_index(w, cluster->first_portal + cluster->portal_count, portal_number);
  w->portal_index_size++;
  cluster->portal_count++;
  return true;
}
enum { FRAME_ENTER = 0, FRAME_FACES = 1, FRAME_REACH = 2 };
typedef struct { int kind; int64_t area_number, index; } FloodFrame;
static bool flood_cluster_areas(AasClustering *cl, int64_t area_number, int64_t cluster_number) {
  ClusteringWorld *w = cl->world;
  char message[256];
  FloodFrame *stack = NULL;
  size_t count = 0, cap = 0;
#define PUSH_FRAME(f) do { \
    if (count == cap) { cap = cap ? cap * 2 : 32; FloodFrame *p = realloc(stack, cap * sizeof *p); \
      if (!p) { free(stack); qa_range_error("Out of memory"); } stack = p; } \
    stack[count++] = (f); } while (0)
  PUSH_FRAME(((FloodFrame){FRAME_ENTER, area_number, 0}));
  bool ok = true;
  while (count != 0 && ok) {
    FloodFrame frame = stack[--count];
    if (frame.kind == FRAME_ENTER) {
      if (frame.area_number <= 0 || frame.area_number >= (int64_t)w->asset->area_count) {
        print_severity(cl, 4, "AAS_FloodClusterAreas_r: areanum out of range");
        ok = false; break;
      }
      qa_aas_setting_rec *settings = at_setting(w, frame.area_number);
      if (settings->cluster > 0) {
        if (settings->cluster == cluster_number) continue;
        snprintf(message, sizeof message, "cluster %lld touched cluster %lld at area %lld\r\n",
                 (long long)cluster_number, (long long)settings->cluster, (long long)frame.area_number);
        print_severity(cl, 4, message);
        ok = false; break;
      }
      if ((settings->contents & CLUSTER_PORTAL) != 0) {
        if (!update_portal(cl, frame.area_number, cluster_number)) { ok = false; break; }
        continue;
      }
      settings->cluster = cluster_number;
      qa_aas_cluster_rec *cluster = at_cluster(w, cluster_number);
      settings->cluster_area = cluster->area_count;
      cluster->area_count++;
      PUSH_FRAME(((FloodFrame){cl->no_face_flood ? FRAME_REACH : FRAME_FACES, frame.area_number, 0}));
    } else if (frame.kind == FRAME_FACES) {
      const qa_aas_area_rec *area = at_area(w, frame.area_number);
      if (frame.index >= area->face_count) {
        PUSH_FRAME(((FloodFrame){FRAME_REACH, frame.area_number, 0}));
        continue;
      }
      const qa_aas_face_rec *face = at_face(w, abs64(at_i64(w->asset->face_indexes, w->asset->face_index_count, area->first_face + frame.index)));
      PUSH_FRAME(((FloodFrame){FRAME_FACES, frame.area_number, frame.index + 1}));
      int64_t other_area = face->front_area == frame.area_number ? face->back_area : face->front_area;
      if (other_area != 0) PUSH_FRAME(((FloodFrame){FRAME_ENTER, other_area, 0}));
    } else {
      qa_aas_setting_rec *settings = at_setting(w, frame.area_number);
      if (frame.index >= settings->reach_count) continue;
      if (settings->first_reach + frame.index < 0 || settings->first_reach + frame.index >= (int64_t)w->asset->reachability_count)
        at_fail(settings->first_reach + frame.index, w->asset->reachability_count);
      int64_t other_area = w->asset->reachability[settings->first_reach + frame.index].area;
      PUSH_FRAME(((FloodFrame){FRAME_REACH, frame.area_number, frame.index + 1}));
      if (other_area != 0) PUSH_FRAME(((FloodFrame){FRAME_ENTER, other_area, 0}));
    }
  }
  free(stack);
#undef PUSH_FRAME
  return ok;
}
static int64_t reach_at_area(ClusteringWorld *w, int64_t index) {
  if (index < 0 || index >= (int64_t)w->asset->reachability_count) at_fail(index, w->asset->reachability_count);
  return w->asset->reachability[index].area;
}
static bool flood_using_reachabilities(AasClustering *cl, int64_t cluster_number) {
  ClusteringWorld *w = cl->world;
  for (int64_t area = 1; area < (int64_t)w->asset->area_count; area++) {
    qa_aas_setting_rec *settings = at_setting(w, area);
    if (settings->cluster != 0 || (settings->contents & CLUSTER_PORTAL) != 0) continue;
    for (int64_t index = 0; index < settings->reach_count; index++) {
      int64_t target = reach_at_area(w, settings->first_reach + index);
      qa_aas_setting_rec *other = at_setting(w, target);
      if ((other->contents & CLUSTER_PORTAL) != 0) continue;
      if (other->cluster != 0) {
        if (!flood_cluster_areas(cl, area, cluster_number)) return false;
        area = 0;
        break;
      }
    }
  }
  return true;
}
static int64_t area_reachability(AasClustering *cl, int64_t area) {
  char message[256];
  if (area < 0 || area >= (int64_t)cl->world->asset->area_count) {
    snprintf(message, sizeof message, "AAS_AreaReachability: areanum %lld out of range", (long long)area);
    print_severity(cl, 4, message);
    return 0;
  }
  return at_setting(cl->world, area)->reach_count;
}
static void number_cluster_areas(AasClustering *cl, int64_t cluster_number) {
  ClusteringWorld *w = cl->world;
  qa_aas_cluster_rec *cluster = at_cluster(w, cluster_number);
  cluster->area_count = 0;
  cluster->reachability_area_count = 0;
  for (int pass = 0; pass < 2; pass++) {
    bool with_reach = pass == 0;
    for (int64_t area = 1; area < (int64_t)w->asset->area_count; area++) {
      qa_aas_setting_rec *settings = at_setting(w, area);
      if (settings->cluster != cluster_number || (area_reachability(cl, area) != 0) != with_reach) continue;
      settings->cluster_area = cluster->area_count++;
      if (with_reach) cluster->reachability_area_count++;
    }
    for (int64_t index = 0; index < cluster->portal_count; index++) {
      qa_aas_portal_rec *portal = at_portal(w, portal_index_value(w, cluster->first_portal + index));
      if ((area_reachability(cl, portal->area) != 0) != with_reach) continue;
      portal->cluster_areas[portal->front_cluster == cluster_number ? 0 : 1] = cluster->area_count++;
      if (with_reach) cluster->reachability_area_count++;
    }
  }
}
static bool find_clusters(AasClustering *cl) {
  ClusteringWorld *w = cl->world;
  remove_cluster_areas(cl);
  for (int64_t area = 1; area < (int64_t)w->asset->area_count; area++) {
    qa_aas_setting_rec *settings = at_setting(w, area);
    if (settings->cluster != 0) continue;
    if (cl->no_face_flood && settings->reach_count == 0) continue;
    if ((settings->contents & CLUSTER_PORTAL) != 0) continue;
    if (w->num_clusters >= MAX_CLUSTERS) {
      print_severity(cl, 4, "AAS_MAX_CLUSTERS");
      return false;
    }
    qa_aas_cluster_rec *cluster = at_cluster(w, w->num_clusters);
    cluster->area_count = 0;
    cluster->reachability_area_count = 0;
    cluster->first_portal = w->portal_index_size;
    cluster->portal_count = 0;
    if (!flood_cluster_areas(cl, area, w->num_clusters)) return false;
    if (!flood_using_reachabilities(cl, w->num_clusters)) return false;
    number_cluster_areas(cl, w->num_clusters);
    w->num_clusters++;
  }
  return true;
}
static void create_portals(AasClustering *cl) {
  ClusteringWorld *w = cl->world;
  for (int64_t area = 1; area < (int64_t)w->asset->area_count; area++) {
    if ((at_setting(w, area)->contents & CLUSTER_PORTAL) == 0) continue;
    if (w->num_portals >= MAX_PORTALS) {
      print_severity(cl, 4, "AAS_MAX_PORTALS");
      return;
    }
    qa_aas_portal_rec *portal = at_portal(w, w->num_portals);
    portal->area = area;
    portal->front_cluster = 0;
    portal->back_cluster = 0;
    w->num_portals++;
  }
}
static int64_t adjacent_areas(AasClustering *cl, I64List *area_numbers, int64_t area_number) {
  ClusteringWorld *w = cl->world;
  portal_area_push(area_numbers, area_number, "areanums");
  const qa_aas_area_rec *area = at_area(w, area_number);
  int64_t presence = at_setting(w, area_number)->presence;
  for (int64_t index = 0; index < area->face_count; index++) {
    const qa_aas_face_rec *face = at_face(w, abs64(at_i64(w->asset->face_indexes, w->asset->face_index_count, area->first_face + index)));
    if ((face->flags & FACE_SOLID) != 0) continue;
    int64_t other_area = face->front_area != area_number ? face->front_area : face->back_area;
    int64_t other_presence = at_setting(w, other_area)->presence;
    if ((presence & ~other_presence) == 0 || (other_presence & ~presence) != 0 || list_contains(area_numbers, other_area)) continue;
    if (area_numbers->count >= MAX_PORTAL_AREAS) {
      print_severity(cl, 4, "MAX_PORTALAREAS");
      return (int64_t)area_numbers->count;
    }
    adjacent_areas(cl, area_numbers, other_area);
  }
  return (int64_t)area_numbers->count;
}
static int64_t check_area_for_portals(AasClustering *cl, int64_t area_number) {
  ClusteringWorld *w = cl->world;
  char message[256];
  qa_aas_setting_rec *settings = at_setting(w, area_number);
  if ((settings->contents & CLUSTER_PORTAL) != 0 || (settings->flags & AREA_GROUNDED) == 0) return 0;
  I64List area_numbers = {0, 0, 0}, front_faces = {0, 0, 0}, back_faces = {0, 0, 0};
  I64List front_areas = {0, 0, 0}, back_areas = {0, 0, 0};
  int32_t *front_counts = calloc(MAX_PORTAL_AREAS, sizeof *front_counts);
  int32_t *back_counts = calloc(MAX_PORTAL_AREAS, sizeof *back_counts);
  if (!front_counts || !back_counts) qa_range_error("Out of memory");
  int64_t front_plane = -1, back_plane = -1;
  int64_t result = 0;
  adjacent_areas(cl, &area_numbers, area_number);
  for (size_t index = 0; index < area_numbers.count; index++) {
    int64_t current_area = at_i64(area_numbers.v, area_numbers.count, (int64_t)index);
    const qa_aas_area_rec *area = at_area(w, current_area);
    for (int64_t face_index = 0; face_index < area->face_count; face_index++) {
      int64_t face_number = abs64(at_i64(w->asset->face_indexes, w->asset->face_index_count, area->first_face + face_index));
      const qa_aas_face_rec *face = at_face(w, face_number);
      if ((face->flags & FACE_SOLID) != 0) continue;
      bool touches_other = false;
      for (size_t oi = 0; oi < area_numbers.count; oi++) {
        if (oi == index) continue;
        int64_t other = area_numbers.v[oi];
        if (face->front_area == other || face->back_area == other) touches_other = true;
      }
      if (touches_other) continue;
      int64_t other_area = face->front_area == current_area ? face->back_area : face->front_area;
      if ((at_setting(w, other_area)->contents & CLUSTER_PORTAL) != 0) goto done;
      int64_t plane = face->plane & ~1LL;
      if (index >= MAX_PORTAL_AREAS) at_fail((int64_t)index, MAX_PORTAL_AREAS);
      if (front_plane < 0 || plane == front_plane) {
        front_plane = plane;
        portal_area_push(&front_faces, face_number, "frontfacenums");
        if (!list_contains(&front_areas, other_area)) portal_area_push(&front_areas, other_area, "frontareanums");
        front_counts[index] = front_counts[index] + 1;
      } else if (back_plane < 0 || plane == back_plane) {
        back_plane = plane;
        portal_area_push(&back_faces, face_number, "backfacenums");
        if (!list_contains(&back_areas, other_area)) portal_area_push(&back_areas, other_area, "backareanums");
        back_counts[index] = back_counts[index] + 1;
      } else goto done;
    }
  }
  for (size_t index = 0; index < area_numbers.count; index++) {
    if (front_counts[index] == 0 || back_counts[index] == 0) goto done;
  }
  if (!connected_areas(w, &front_areas) || !connected_areas(w, &back_areas)) goto done;
  for (size_t fi = 0; fi < front_faces.count; fi++) {
    const qa_aas_face_rec *front = at_face(w, front_faces.v[fi]);
    for (int64_t front_edge = 0; front_edge < front->edge_count; front_edge++) {
      int64_t edge_number = abs64(at_i64(w->asset->edge_indexes, w->asset->edge_index_count, front->first_edge + front_edge));
      for (size_t bi = 0; bi < back_faces.count; bi++) {
        const qa_aas_face_rec *back = at_face(w, back_faces.v[bi]);
        for (int64_t back_edge = 0; back_edge < back->edge_count; back_edge++)
          if (edge_number == abs64(at_i64(w->asset->edge_indexes, w->asset->edge_index_count, back->first_edge + back_edge))) goto done;
      }
    }
  }
  for (size_t i = 0; i < area_numbers.count; i++) {
    int64_t area = area_numbers.v[i];
    at_setting(w, area)->contents |= CLUSTER_PORTAL;
    at_setting(w, area)->contents |= ROUTE_PORTAL;
    snprintf(message, sizeof message, "possible portal: %lld\r\n", (long long)area);
    print_text(cl, message);
  }
  result = (int64_t)area_numbers.count;
done:
  free(area_numbers.v); free(front_faces.v); free(back_faces.v); free(front_areas.v); free(back_areas.v);
  free(front_counts); free(back_counts);
  return result;
}
static int32_t to_i32(int64_t v) { return (int32_t)(uint32_t)v; }
static void padded6(int64_t value, char *out, size_t cap) { snprintf(out, cap, "%6lld", (long long)value); }
static void find_possible_portals(AasClustering *cl) {
  char message[256], pad[32];
  int32_t count = 0;
  for (int64_t area = 1; area < (int64_t)cl->world->asset->area_count; area++)
    count = to_i32(count + check_area_for_portals(cl, area));
  padded6(count, pad, sizeof pad);
  snprintf(message, sizeof message, "\r%s possible portal areas\n", pad);
  print_severity(cl, 1, message);
}
static bool test_portals(AasClustering *cl) {
  ClusteringWorld *w = cl->world;
  char message[256];
  for (int64_t portal_number = 1; portal_number < w->num_portals; portal_number++) {
    qa_aas_portal_rec *portal = at_portal(w, portal_number);
    if (portal->front_cluster == 0) {
      at_setting(w, portal->area)->contents &= ~CLUSTER_PORTAL;
      snprintf(message, sizeof message, "portal area %lld has no front cluster\r\n", (long long)portal->area);
      print_text(cl, message);
      return false;
    }
    if (portal->back_cluster == 0) {
      at_setting(w, portal->area)->contents &= ~CLUSTER_PORTAL;
      snprintf(message, sizeof message, "portal area %lld has no back cluster\r\n", (long long)portal->area);
      print_text(cl, message);
      return false;
    }
  }
  return true;
}
static void count_forced_portals(AasClustering *cl) {
  char message[256], pad[32];
  int64_t count = 0;
  for (int64_t area = 1; area < (int64_t)cl->world->asset->area_count; area++) {
    if ((at_setting(cl->world, area)->contents & CLUSTER_PORTAL) == 0) continue;
    snprintf(message, sizeof message, "area %lld is a forced portal area\r\n", (long long)area);
    print_text(cl, message);
    count++;
  }
  padded6(count, pad, sizeof pad);
  snprintf(message, sizeof message, "%s forced portal areas\n", pad);
  print_severity(cl, 1, message);
}
static void create_view_portals(AasClustering *cl) {
  for (int64_t area = 1; area < (int64_t)cl->world->asset->area_count; area++) {
    qa_aas_setting_rec *settings = at_setting(cl->world, area);
    if ((settings->contents & CLUSTER_PORTAL) != 0) settings->contents |= VIEW_PORTAL;
  }
}
static void set_view_as_cluster_portals(AasClustering *cl) {
  for (int64_t area = 1; area < (int64_t)cl->world->asset->area_count; area++) {
    qa_aas_setting_rec *settings = at_setting(cl->world, area);
    if ((settings->contents & VIEW_PORTAL) != 0) settings->contents |= CLUSTER_PORTAL;
  }
}
static void initialize_clustering(AasClustering *cl, double (*get_value)(const char *)) {
  ClusteringWorld *w = cl->world;
  char message[512], pad[32];
  if (w->num_clusters >= 1 && source_integer(get_value("forceclustering")) == 0 &&
      source_integer(get_value("forcereachability")) == 0) return;
  set_view_as_cluster_portals(cl);
  count_forced_portals(cl);
  remove_cluster_areas(cl);
  find_possible_portals(cl);
  create_view_portals(cl);
  w->portals = calloc(MAX_PORTALS, sizeof *w->portals);
  w->portal_indexes = calloc(MAX_PORTAL_INDEX, sizeof *w->portal_indexes);
  w->clusters = calloc(MAX_CLUSTERS, sizeof *w->clusters);
  if (!w->portals || !w->portal_indexes || !w->clusters) qa_range_error("Out of memory");
  w->portal_cap = MAX_PORTALS;
  w->portal_index_cap = MAX_PORTAL_INDEX;
  w->cluster_cap = MAX_CLUSTERS;
  int32_t removed_portal_areas = 0;
  padded6(removed_portal_areas, pad, sizeof pad);
  snprintf(message, sizeof message, "\r%s removed portal areas", pad);
  print_severity(cl, 1, message);
  for (;;) {
    padded6(removed_portal_areas, pad, sizeof pad);
    snprintf(message, sizeof message, "\r%s", pad);
    print_severity(cl, 1, message);
    w->num_portals = 1;
    w->portal_index_size = 0;
    w->num_clusters = 1;
    create_portals(cl);
    removed_portal_areas = to_i32(removed_portal_areas + 1);
    if (!find_clusters(cl)) continue;
    if (!test_portals(cl)) continue;
    break;
  }
  print_severity(cl, 1, "\n");
  w->save_file = true;
  for (int64_t portal = 1; portal < w->num_portals; portal++) {
    snprintf(message, sizeof message, "portal %lld: area %lld\r\n", (long long)portal, (long long)at_portal(w, portal)->area);
    print_text(cl, message);
  }
  padded6(w->num_portals, pad, sizeof pad);
  snprintf(message, sizeof message, "%s portals created\n", pad);
  print_severity(cl, 1, message);
  padded6(w->num_clusters, pad, sizeof pad);
  snprintf(message, sizeof message, "%s clusters created\n", pad);
  print_severity(cl, 1, message);
  for (int64_t cluster = 1; cluster < w->num_clusters; cluster++) {
    snprintf(message, sizeof message, "cluster %lld has %lld reachability areas\n",
             (long long)cluster, (long long)at_cluster(w, cluster)->reachability_area_count);
    print_severity(cl, 1, message);
  }
  int32_t reachability_areas = 0, total = 0;
  for (int64_t cluster = 0; cluster < w->num_clusters; cluster++) {
    int64_t count = at_cluster(w, cluster)->reachability_area_count;
    reachability_areas = to_i32(reachability_areas + count);
    total = to_i32(total + to_i32(count * count));
  }
  total = to_i32(total + to_i32((int64_t)reachability_areas * w->num_portals));
  padded6(reachability_areas, pad, sizeof pad);
  snprintf(message, sizeof message, "%s total reachability areas\n", pad);
  print_severity(cl, 1, message);
  padded6(to_i32((int64_t)total * 3), pad, sizeof pad);
  snprintf(message, sizeof message, "%s AAS memory/CPU usage (the lower the better)\n", pad);
  print_severity(cl, 1, message);
}
static double always_one(const char *name) { (void)name; return 1; }
void qa_aas_clustering_result_destroy(qa_aas_asset *asset) {
  if (!asset) return;
  free(asset->settings);
  free(asset->portals);
  free(asset->portal_indexes);
  free(asset->clusters);
  asset->settings = NULL; asset->setting_count = 0;
  asset->portals = NULL; asset->portal_count = 0;
  asset->portal_indexes = NULL; asset->portal_index_count = 0;
  asset->clusters = NULL; asset->cluster_count = 0;
}
bool qa_cluster_aas(qa_aas_asset *out, const qa_aas_asset *asset, void *print_ctx, qa_aas_cluster_print_fn print) {
  ClusteringWorld world; memset(&world, 0, sizeof world);
  world.asset = asset;
  if (asset->setting_count) {
    world.settings = malloc(asset->setting_count * sizeof *world.settings);
    if (!world.settings) return false;
    memcpy(world.settings, asset->settings, asset->setting_count * sizeof *world.settings);
  }
  AasClustering clustering;
  clustering.world = &world;
  clustering.print_ctx = print_ctx;
  clustering.print_text = print;
  clustering.no_face_flood = true;
  initialize_clustering(&clustering, always_one);
  qa_aas_portal_rec *portals = NULL;
  qa_aas_cluster_rec *clusters = NULL;
  int64_t *portal_indexes = NULL;
  if (world.num_portals > 0) {
    portals = malloc((size_t)world.num_portals * sizeof *portals);
    if (!portals) goto oom;
    memcpy(portals, world.portals, (size_t)world.num_portals * sizeof *portals);
  }
  if (world.portal_index_size > 0) {
    portal_indexes = malloc((size_t)world.portal_index_size * sizeof *portal_indexes);
    if (!portal_indexes) goto oom;
    memcpy(portal_indexes, world.portal_indexes, (size_t)world.portal_index_size * sizeof *portal_indexes);
  }
  if (world.num_clusters > 0) {
    clusters = malloc((size_t)world.num_clusters * sizeof *clusters);
    if (!clusters) goto oom;
    memcpy(clusters, world.clusters, (size_t)world.num_clusters * sizeof *clusters);
  }
  *out = *asset;
  out->lumps = NULL; out->lump_count = 0;
  out->settings = world.settings;
  out->portals = portals; out->portal_count = (size_t)(world.num_portals > 0 ? world.num_portals : 0);
  out->portal_indexes = portal_indexes; out->portal_index_count = (size_t)(world.portal_index_size > 0 ? world.portal_index_size : 0);
  out->clusters = clusters; out->cluster_count = (size_t)(world.num_clusters > 0 ? world.num_clusters : 0);
  free(world.portals); free(world.portal_indexes); free(world.clusters);
  return true;
oom:
  free(portals); free(clusters); free(portal_indexes);
  free(world.settings); free(world.portals); free(world.portal_indexes); free(world.clusters);
  return false;
}
