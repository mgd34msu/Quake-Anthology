/* Port of src/app/bootstrap/network/q2-mvd-presentation.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q2_mvd_presentation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/q2/solid.h"
#include "qa/foundation/js.h"
qa_app_q2_layout qa_app_q2_mvd_layout(qa_q2_mvd_profile profile) {
  if (!profile.extended || profile.rerelease) return qa_app_q2_application_layout(profile.protocol);
  qa_app_q2_layout layout = {62, 8254, 10302, 12350, 12606, 12862, 8192, 2048, 2048, 13630, 61, 60, 59, false, 0};
  return layout;
}
struct qa_app_q2_mvd_visibility_state {
  qa_app_q2_mvd_visibility_host host;
  qa_shared_scene_queries *applied_scene;
  uint8_t *applied_bits; size_t applied_length;
};
static const qa_q2_world_geometry *q2_geometry(qa_shared_scene_queries *scene, const char *message) {
  if (scene->geometry.kind != QA_DECODED_WORLD_Q2) qa_range_error("%s", message);
  return scene->geometry.as.q2;
}
static qa_vec3 player_origin(const qa_app_q2_mvd_visibility_state *s, const qa_q2_wire_player *player) {
  qa_vec3 out;
  if (s->host.profile.rerelease) {
    out.x = player->pmove.origin_f[0]; out.y = player->pmove.origin_f[1]; out.z = player->pmove.origin_f[2];
  } else {
    out.x = player->pmove.origin[0] / 8.0; out.y = player->pmove.origin[1] / 8.0; out.z = player->pmove.origin[2] / 8.0;
  }
  return out;
}
static qa_shared_scene_queries *scene_with_portals(qa_app_q2_mvd_visibility_state *s,
    const uint8_t *bits, size_t length) {
  qa_shared_scene_queries *scene = s->host.scene(s->host.ctx);
  const qa_q2_world_geometry *geometry = q2_geometry(scene, "MVD visibility requires the admitted Q2 BSP");
  if (s->applied_scene == scene && s->applied_length == length
      && (length == 0 || memcmp(s->applied_bits, bits, length) == 0)) return scene;
  for (size_t i = 0; i < geometry->area_portal_count; i++) {
    double portal = geometry->area_portals[i].portal;
    bool duplicate = false;
    for (size_t j = 0; j < i; j++)
      if (geometry->area_portals[j].portal == portal) { duplicate = true; break; }
    if (duplicate) continue;
    /* CM_SetPortalStates opens portals beyond the supplied byte count. */
    unsigned p = (unsigned)portal;
    size_t index = p >> 3;
    bool open = index >= length || (bits[index] & (1 << (p & 7))) != 0;
    qa_shared_set_area_portal_state(scene, portal, open);
  }
  uint8_t *copy = malloc(length ? length : 1);
  if (!copy) qa_range_error("out of memory");
  if (length) memcpy(copy, bits, length);
  free(s->applied_bits); s->applied_bits = copy; s->applied_length = length;
  s->applied_scene = scene;
  return scene;
}
static bool vis_visible(void *ctx, double leaf, bool is_pvs, const qa_q2_wire_player *player,
    const uint8_t *bits, size_t length) {
  qa_app_q2_mvd_visibility_state *s = ctx;
  qa_shared_scene_queries *scene = scene_with_portals(s, bits, length);
  const qa_q2_world_geometry *geometry = q2_geometry(scene, "MVD visibility requires the admitted Q2 BSP");
  if (leaf != trunc(leaf) || leaf < 0 || leaf >= (double)geometry->leaf_count)
    qa_range_error("MVD multicast leaf is outside the admitted BSP");
  double viewer = qa_shared_point_leaf(scene, player_origin(s, player));
  double cluster = qa_shared_leaf_cluster(scene, viewer);
  return cluster != -1
      && qa_shared_areas_connected(scene, qa_shared_leaf_area(scene, leaf), qa_shared_leaf_area(scene, viewer))
      && qa_shared_cluster_visible(scene, qa_shared_leaf_cluster(scene, leaf), cluster, is_pvs ? "pvs" : "phs");
}
static qa_vec3 view_origin(const qa_app_q2_mvd_visibility_state *s, const qa_q2_wire_player *player) {
  qa_vec3 point = player_origin(s, player);
  point.x += player->viewoffset[0]; point.y += player->viewoffset[1];
  point.z += player->viewoffset[2] + (s->host.profile.rerelease ? player->pmove.viewheight : 0);
  return point;
}
static bool inline_model_index(const char *path, double *out) {
  if (!path || path[0] != '*' || !path[1]) return false;
  double value = 0;
  for (const char *p = path + 1; *p; p++) {
    if (*p < '0' || *p > '9') return false;
    value = value * 10 + (*p - '0');
  }
  *out = value; return true;
}
static qa_bounds entity_bounds(qa_app_q2_mvd_visibility_state *s, const qa_q2_wire_entity *entity) {
  const char *path = s->host.model_path(s->host.ctx, entity->modelindex);
  double index;
  if (inline_model_index(path, &index))
    return qa_shared_model_bounds(s->host.scene(s->host.ctx), index);
  if (entity->solid != 0 && entity->solid != 31) {
    qa_q2_solid_encoding encoding;
    if (!qa_q2_get_solid_encoding(s->host.profile.protocol, s->host.profile.extended, &encoding))
      qa_range_error("out of memory"); /* TS passes undefined encoding into unpackQ2Solid */
    return qa_q2_unpack_solid(entity->solid, encoding);
  }
  qa_bounds zero = {{0, 0, 0}, {0, 0, 0}};
  return zero;
}
typedef struct mask_ctx {
  qa_shared_scene_queries *scene; const qa_q2_world_geometry *geometry;
  qa_leaf_query_result linked; const double *clusters; size_t cluster_count;
  const double *fat_clusters; size_t fat_count; double cluster; bool phs;
} mask_ctx;
static bool admitted(mask_ctx *m, double target) {
  if (m->phs) return qa_shared_cluster_visible(m->scene, m->cluster, target, "phs");
  for (size_t i = 0; i < m->fat_count; i++)
    if (qa_shared_cluster_visible(m->scene, m->fat_clusters[i], target, "pvs")) return true;
  return false;
}
static bool visit_node(mask_ctx *m, double node) {
  if (node < 0 || node != trunc(node) || node >= (double)m->geometry->node_count)
    qa_range_error("MVD entity topnode is outside the BSP");
  const qa_bsp_node *branch = &m->geometry->nodes[(size_t)node];
  for (int i = 0; i < 2; i++) {
    qa_bsp_child child = branch->children[i];
    bool hit = child.is_node ? visit_node(m, child.index)
        : admitted(m, qa_shared_leaf_cluster(m->scene, child.index));
    if (hit) return true;
  }
  return false;
}
static bool in_mask(mask_ctx *m) {
  if (m->linked.leaves.count < 128 && m->cluster_count <= 16) {
    for (size_t i = 0; i < m->cluster_count; i++)
      if (admitted(m, m->clusters[i])) return true;
    return false;
  }
  if (!m->linked.has_topnode) qa_range_error("MVD overflow entity has no BSP topnode");
  return visit_node(m, m->linked.topnode);
}
static size_t vis_entities(void *ctx, qa_q2_wire_entity *entities, size_t count,
    const qa_q2_wire_player *player, const uint8_t *bits, size_t length) {
  qa_app_q2_mvd_visibility_state *s = ctx;
  qa_shared_scene_queries *scene = scene_with_portals(s, bits, length);
  const qa_q2_world_geometry *geometry = q2_geometry(scene, "MVD entity admission requires the admitted Q2 BSP");
  qa_vec3 point = view_origin(s, player);
  double viewer = qa_shared_point_leaf(scene, point);
  double area = qa_shared_leaf_area(scene, viewer);
  double cluster = qa_shared_leaf_cluster(scene, viewer);
  qa_bounds fat_box = {{point.x - 8, point.y - 8, point.z - 8}, {point.x + 8, point.y + 8, point.z + 8}};
  qa_leaf_query_result fat = qa_shared_box_leaves(scene, fat_box, 64);
  double fat_clusters[64]; size_t fat_count = 0;
  for (size_t i = 0; i < fat.leaves.count && fat_count < 64; i++) {
    double c = qa_shared_leaf_cluster(scene, fat.leaves.values[i]);
    bool seen = false;
    for (size_t j = 0; j < fat_count; j++) if (fat_clusters[j] == c) seen = true;
    if (!seen) fat_clusters[fat_count++] = c;
  }
  size_t kept = 0;
  for (size_t e = 0; e < count; e++) {
    qa_q2_wire_entity entity = entities[e];
    bool shadow = (qa_js_to_int32(entity.renderfx) & 16384) != 0;
    bool beam = (qa_js_to_int32(entity.renderfx) & 128) != 0;
    if (entity.number == 0 || (entity.modelindex == 0 && entity.effects == 0
        && entity.sound == 0 && entity.event == 0 && !shadow)) continue;
    qa_bounds bounds = entity_bounds(s, &entity);
    if (entity.solid == 31 && (entity.angles[0] != 0 || entity.angles[1] != 0 || entity.angles[2] != 0)) {
      double radius = qa_js_max(fabs(bounds.min.x), qa_js_max(fabs(bounds.min.y),
          qa_js_max(fabs(bounds.min.z), qa_js_max(fabs(bounds.max.x),
          qa_js_max(fabs(bounds.max.y), fabs(bounds.max.z))))));
      bounds.min.x = bounds.min.y = bounds.min.z = -radius;
      bounds.max.x = bounds.max.y = bounds.max.z = radius;
    }
    double x = entity.origin[0], y = entity.origin[1], z = entity.origin[2];
    qa_bounds linked_box = {{x + bounds.min.x - 1, y + bounds.min.y - 1, z + bounds.min.z - 1},
        {x + bounds.max.x + 1, y + bounds.max.y + 1, z + bounds.max.z + 1}};
    qa_leaf_query_result linked = qa_shared_box_leaves(scene, linked_box, 128);
    double first_area = 0, second_area = 0;
    double clusters[128]; size_t cluster_count = 0;
    for (size_t i = 0; i < linked.leaves.count; i++) {
      double leaf = linked.leaves.values[i];
      double next = qa_shared_leaf_area(scene, leaf);
      if (next != 0) {
        if (first_area != 0 && first_area != next) second_area = next; else first_area = next;
      }
      double c = qa_shared_leaf_cluster(scene, leaf);
      if (c != -1 && cluster_count < 128) {
        bool seen = false;
        for (size_t j = 0; j < cluster_count; j++) if (clusters[j] == c) seen = true;
        if (!seen) clusters[cluster_count++] = c;
      }
    }
    if (!qa_shared_areas_connected(scene, area, first_area)
        && !qa_shared_areas_connected(scene, area, second_area)) continue;
    mask_ctx m = {scene, geometry, linked, clusters, cluster_count, fat_clusters, fat_count, cluster,
        beam || entity.sound != 0 || shadow};
    if (!in_mask(&m)) continue;
    double distance = qa_js_hypot3(point.x - x, point.y - y, point.z - z);
    if (entity.sound != 0) {
      double attenuation = entity.loop_attenuation;
      double multiplier = attenuation == -1 ? 0 : attenuation > 0 && attenuation != 3
          ? attenuation * 0.0006 : 0.003;
      if ((distance - 80) * multiplier > 1) {
        bool reject = entity.modelindex == 0;
        if (!reject && !beam) { m.phs = false; reject = !in_mask(&m); }
        if (reject) continue;
      }
    } else if (entity.modelindex == 0 && !shadow && distance > 400) continue;
    entities[kept++] = entity;
  }
  return kept;
}
static uint8_t *vis_area_bits(void *ctx, const qa_q2_wire_player *player,
    const uint8_t *bits, size_t length, size_t *out_length) {
  qa_app_q2_mvd_visibility_state *s = ctx;
  qa_shared_scene_queries *scene = scene_with_portals(s, bits, length);
  const uint8_t *result = qa_shared_area_bits(scene,
      qa_shared_leaf_area(scene, qa_shared_point_leaf(scene, view_origin(s, player))), out_length);
  if (!result) { *out_length = 0; return NULL; }
  uint8_t *copy = malloc(*out_length ? *out_length : 1);
  if (!copy) { *out_length = 0; return NULL; }
  if (*out_length) memcpy(copy, result, *out_length);
  return copy;
}
static bool vis_sound_audible(void *ctx, const float origin[3], const qa_q2_wire_player *player,
    const uint8_t *bits, size_t length) {
  qa_app_q2_mvd_visibility_state *s = ctx;
  qa_vec3 point = {origin[0], origin[1], origin[2]};
  double leaf = qa_shared_point_leaf(s->host.scene(s->host.ctx), point);
  return vis_visible(s, leaf, false, player, bits, length);
}
static void vis_sound_origin(void *ctx, const qa_q2_wire_entity *entity, float out[3]) {
  qa_app_q2_mvd_visibility_state *s = ctx;
  double x = entity->origin[0], y = entity->origin[1], z = entity->origin[2];
  if (entity->solid != 31) { out[0] = (float)x; out[1] = (float)y; out[2] = (float)z; return; }
  const char *path = s->host.model_path(s->host.ctx, entity->modelindex);
  double index;
  if (!inline_model_index(path, &index)) qa_range_error("MVD brush sound has no inline model");
  qa_bounds bounds = qa_shared_model_bounds(s->host.scene(s->host.ctx), index);
  out[0] = (float)(x + (bounds.min.x + bounds.max.x) / 2);
  out[1] = (float)(y + (bounds.min.y + bounds.max.y) / 2);
  out[2] = (float)(z + (bounds.min.z + bounds.max.z) / 2);
}
qa_app_q2_mvd_visibility_state *qa_app_q2_mvd_visibility_create(qa_app_q2_mvd_visibility_host host) {
  qa_app_q2_mvd_visibility_state *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->host = host;
  return s;
}
qa_q2_mvd_visibility qa_app_q2_mvd_visibility_vtable(qa_app_q2_mvd_visibility_state *state) {
  qa_q2_mvd_visibility v = {state, vis_entities, vis_visible, vis_area_bits,
      vis_sound_audible, vis_sound_origin};
  return v;
}
void qa_app_q2_mvd_visibility_destroy(qa_app_q2_mvd_visibility_state *state) {
  if (!state) return;
  free(state->applied_bits); free(state);
}
