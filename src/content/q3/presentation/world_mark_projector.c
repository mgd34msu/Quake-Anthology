/* Port of worldMarkProjector (src/content/q3/presentation/mark-projector.ts).
 * GPL-2.0-or-later. */
#include "qa/content/q3/presentation/world_mark_projector.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static void *own(qa_q3_pres_world_mark_projector *p, size_t count, size_t size) {
  void *value = calloc(count ? count : 1, size);
  void **owned = realloc(p->owned, (p->owned_count + 1) * sizeof *owned);
  if (value == NULL || owned == NULL) { free(value); if (owned != NULL) p->owned = owned; qa_range_error("Out of memory"); }
  p->owned = owned;
  p->owned[p->owned_count++] = value;
  return value;
}
static qa_q3_pres_mark_surface face(qa_q3_pres_world_mark_projector *p, const qa_world_surface *surface,
                                    double surface_flags, double content_flags) {
  qa_q3_pres_mark_surface out; memset(&out, 0, sizeof out);
  out.kind = QA_Q3_PRES_MARK_FACE;
  out.surface_flags = surface_flags; out.content_flags = content_flags;
  out.plane = surface->plane;
  qa_vec3 *positions = own(p, surface->geometry.vertex_count, sizeof *positions);
  for (size_t i = 0; i < surface->geometry.vertex_count; i++) positions[i] = surface->geometry.vertices[i].position;
  out.vertices = positions; out.vertex_count = surface->geometry.vertex_count;
  out.indices = surface->geometry.indices; out.index_count = surface->geometry.index_count;
  return out;
}
void qa_q3_pres_world_mark_projector_init(qa_q3_pres_world_mark_projector *p, const qa_world_scene *world) {
  memset(p, 0, sizeof *p);
  qa_decoded_world map = world->map;
  p->surfaces = own(p, world->surface_count, sizeof *p->surfaces);
  for (size_t i = 0; i < world->surface_count; i++) {
    const qa_world_surface *surface = &world->surfaces[i];
    qa_q3_pres_mark_surface *out = &p->surfaces[i];
    memset(out, 0, sizeof *out);
    if (surface->is_q3) {
      if (map.kind != QA_DECODED_WORLD_Q3) qa_raise("Error", 0, 0, "Q3 material surface has a foreign BSP owner");
      const qa_q3_world_geometry *q3 = map.as.q3;
      double index = surface->index;
      const qa_q3_bsp_surface *source = index >= 0 && index < (double)q3->surface_count && index == (double)(size_t)index
          ? &q3->surfaces[(size_t)index] : NULL;
      const qa_q3_bsp_shader *shader = source != NULL && source->shader >= 0 && source->shader < (double)q3->shader_count
          && source->shader == (double)(size_t)source->shader ? &q3->shaders[(size_t)source->shader] : NULL;
      if (source == NULL || shader == NULL) qa_raise("RangeError", 0, 0, "Mark surface has no source shader");
      if (surface->grid != NULL) {
        const qa_patch_mesh *mesh = &surface->grid->mesh;
        out->kind = QA_Q3_PRES_MARK_GRID;
        out->surface_flags = shader->surface_flags; out->content_flags = shader->content_flags;
        qa_q3_pres_mark_grid_vertex *vertices = own(p, mesh->vertex_count, sizeof *vertices);
        for (size_t v = 0; v < mesh->vertex_count; v++) {
          vertices[v].position = mesh->vertices[v].position;
          vertices[v].normal = mesh->vertices[v].normal;
        }
        out->grid_vertices = vertices; out->grid_vertex_count = mesh->vertex_count;
        out->grid_width = mesh->width; out->grid_height = mesh->height;
        continue;
      }
      if (source->kind == NULL || strcmp(source->kind, "planar") != 0 || !surface->has_plane) { out->kind = QA_Q3_PRES_MARK_SKIP; continue; }
      *out = face(p, surface, shader->surface_flags, shader->content_flags);
      continue;
    }
    const qa_render_material *material = &surface->material;
    bool q1 = material->kind != NULL && strcmp(material->kind, "q1") == 0;
    bool skip = !surface->has_plane || (q1
        ? !(material->surface != NULL && (strcmp(material->surface, "ordinary") == 0 || strcmp(material->surface, "fence") == 0))
        : (qa_js_to_int32(material->surface_flags) & (4 | 8 | 128)) != 0);
    if (skip) { out->kind = QA_Q3_PRES_MARK_SKIP; continue; }
    *out = face(p, surface, 0, 0);
  }
  const qa_bsp_node *nodes; size_t node_count;
  const qa_bsp_plane *planes; size_t plane_count;
  qa_double_view leaf_surfaces;
  size_t leaf_count;
  if (map.kind == QA_DECODED_WORLD_Q3) {
    nodes = map.as.q3->nodes; node_count = map.as.q3->node_count; planes = map.as.q3->planes; plane_count = map.as.q3->plane_count;
    leaf_surfaces = map.as.q3->leaf_surfaces; leaf_count = map.as.q3->leaf_count;
  } else if (map.kind == QA_DECODED_WORLD_Q2) {
    nodes = map.as.q2->nodes; node_count = map.as.q2->node_count; planes = map.as.q2->planes; plane_count = map.as.q2->plane_count;
    leaf_surfaces = map.as.q2->leaf_faces; leaf_count = map.as.q2->leaf_count;
  } else {
    nodes = map.as.q1->nodes; node_count = map.as.q1->node_count; planes = map.as.q1->planes; plane_count = map.as.q1->plane_count;
    leaf_surfaces = map.as.q1->leaf_faces; leaf_count = map.as.q1->leaf_count;
  }
  p->leaves = own(p, leaf_count, sizeof *p->leaves);
  for (size_t i = 0; i < leaf_count; i++) {
    qa_index_range range = map.kind == QA_DECODED_WORLD_Q3 ? map.as.q3->leaves[i].surfaces
        : map.kind == QA_DECODED_WORLD_Q2 ? map.as.q2->leaves[i].faces : map.as.q1->leaves[i].faces;
    p->leaves[i].first_surface = range.first; p->leaves[i].surface_count = range.count;
  }
  p->nodes = own(p, node_count, sizeof *p->nodes);
  for (size_t i = 0; i < node_count; i++) {
    p->nodes[i].plane = nodes[i].plane;
    for (int c = 0; c < 2; c++) { p->nodes[i].child_is_node[c] = nodes[i].children[c].is_node; p->nodes[i].child_index[c] = nodes[i].children[c].index; }
  }
  p->planes = own(p, plane_count, sizeof *p->planes);
  for (size_t i = 0; i < plane_count; i++) p->planes[i] = planes[i].plane;
  qa_q3_pres_mark_geometry geometry; memset(&geometry, 0, sizeof geometry);
  geometry.nodes = p->nodes; geometry.node_count = node_count;
  geometry.planes = p->planes; geometry.plane_count = plane_count;
  geometry.leaves = p->leaves; geometry.leaf_count = leaf_count;
  geometry.leaf_surfaces = leaf_surfaces.values; geometry.leaf_surface_count = leaf_surfaces.count;
  geometry.surface_count = (double)world->surface_count;
  geometry.surfaces = p->surfaces; geometry.surfaces_count = world->surface_count;
  qa_q3_pres_bsp_mark_projector_init(&p->projector, geometry);
}
void qa_q3_pres_world_mark_projector_destroy(qa_q3_pres_world_mark_projector *p) {
  for (size_t i = 0; i < p->owned_count; i++) free(p->owned[i]);
  free(p->owned);
  memset(p, 0, sizeof *p);
}
