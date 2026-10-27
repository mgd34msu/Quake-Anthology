/* Port of src/formats/q3-map/world.ts. GPL-2.0-or-later. */
#include "qa/formats/q3_map/world.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static qa_bsp_child child(double value) {
  qa_bsp_child result;
  if (value < 0) { result.is_node = false; result.index = -value - 1; }
  else { result.is_node = true; result.index = value; }
  return result;
}

void qa_q3_adapt_bsp(const qa_q3_bsp_map *map, qa_q3_world_geometry *out) {
  memset(out, 0, sizeof *out);
  qa_bsp_plane *planes = xmalloc(map->plane_count * sizeof *planes);
  for (size_t i = 0; i < map->plane_count; i++) {
    qa_vec3 normal = map->planes[i].normal;
    planes[i].plane = map->planes[i];
    planes[i].type = normal.x == 1 ? 0 : normal.y == 1 ? 1 : normal.z == 1 ? 2 : 3;
    planes[i].signbits = (normal.x < 0 ? 1 : 0) | (normal.y < 0 ? 2 : 0) | (normal.z < 0 ? 4 : 0);
  }
  qa_bsp_node *nodes = xmalloc(map->node_count * sizeof *nodes);
  for (size_t i = 0; i < map->node_count; i++) {
    memset(&nodes[i], 0, sizeof nodes[i]);
    nodes[i].plane = map->nodes[i].plane;
    nodes[i].children[0] = child(map->nodes[i].children[0]);
    nodes[i].children[1] = child(map->nodes[i].children[1]);
    nodes[i].bounds = map->nodes[i].bounds;
  }
  qa_q3_bsp_leaf *leaves = xmalloc(map->leaf_count * sizeof *leaves);
  for (size_t i = 0; i < map->leaf_count; i++) {
    leaves[i].cluster = map->leaves[i].cluster;
    leaves[i].area = map->leaves[i].area;
    leaves[i].bounds = map->leaves[i].bounds;
    leaves[i].surfaces.first = map->leaves[i].first_surface;
    leaves[i].surfaces.count = map->leaves[i].surface_count;
    leaves[i].brushes.first = map->leaves[i].first_brush;
    leaves[i].brushes.count = map->leaves[i].brush_count;
  }
  qa_q3_bsp_model *models = xmalloc(map->model_count * sizeof *models);
  for (size_t i = 0; i < map->model_count; i++) {
    models[i].bounds = map->models[i].bounds;
    models[i].surfaces.first = map->models[i].first_surface;
    models[i].surfaces.count = map->models[i].surface_count;
    models[i].brushes.first = map->models[i].first_brush;
    models[i].brushes.count = map->models[i].brush_count;
  }
  qa_q3_bsp_brush *brushes = xmalloc(map->brush_count * sizeof *brushes);
  for (size_t i = 0; i < map->brush_count; i++) {
    brushes[i].sides.first = map->brushes[i].first_side;
    brushes[i].sides.count = map->brushes[i].side_count;
    brushes[i].shader = map->brushes[i].shader;
  }
  qa_q3_bsp_surface *surfaces = xmalloc(map->surface_count * sizeof *surfaces);
  for (size_t i = 0; i < map->surface_count; i++) {
    const qa_q3_raw_surface *value = &map->surfaces[i];
    surfaces[i].kind = value->type;
    surfaces[i].shader = value->shader;
    surfaces[i].fog = value->fog;
    surfaces[i].vertices.first = value->first_vertex;
    surfaces[i].vertices.count = value->vertex_count;
    surfaces[i].indices.first = value->first_index;
    surfaces[i].indices.count = value->index_count;
    surfaces[i].lightmap.image = value->lightmap;
    surfaces[i].lightmap.x = value->lightmap_x;
    surfaces[i].lightmap.y = value->lightmap_y;
    surfaces[i].lightmap.width = value->lightmap_width;
    surfaces[i].lightmap.height = value->lightmap_height;
    surfaces[i].lightmap.origin = value->lightmap_origin;
    for (int j = 0; j < 3; j++) surfaces[i].lightmap.vectors[j] = value->lightmap_vectors[j];
    surfaces[i].width = value->patch_width;
    surfaces[i].height = value->patch_height;
  }
  out->entities = map->entities;
  out->shaders = map->shaders;
  out->shader_count = map->shader_count;
  out->planes = planes;
  out->plane_count = map->plane_count;
  out->nodes = nodes;
  out->node_count = map->node_count;
  out->leaves = leaves;
  out->leaf_count = map->leaf_count;
  out->leaf_surfaces.values = map->leaf_surfaces;
  out->leaf_surfaces.count = map->leaf_surface_count;
  out->leaf_brushes.values = map->leaf_brushes;
  out->leaf_brushes.count = map->leaf_brush_count;
  out->models = models;
  out->model_count = map->model_count;
  out->brushes = brushes;
  out->brush_count = map->brush_count;
  out->brush_sides = map->brush_sides;
  out->brush_side_count = map->brush_side_count;
  out->vertices = map->vertices;
  out->vertex_count = map->vertex_count;
  out->indices.values = map->indices;
  out->indices.count = map->index_count;
  out->fogs = map->fogs;
  out->fog_count = map->fog_count;
  out->surfaces = surfaces;
  out->surface_count = map->surface_count;
  out->lightmaps = map->lightmaps;
  out->lightmap_count = map->lightmap_count;
  out->light_grid = map->light_grid;
  out->light_grid_count = map->light_grid_count;
  out->visibility = map->visibility;
}

void qa_q3_adapted_world_destroy(qa_q3_world_geometry *world) {
  if (world == NULL) return;
  free((void *)world->planes);
  free((void *)world->nodes);
  free((void *)world->leaves);
  free((void *)world->models);
  free((void *)world->brushes);
  free((void *)world->surfaces);
  memset(world, 0, sizeof *world);
}

void qa_q3_decode_world(const uint8_t *data, size_t data_length, const char *source,
                        qa_q3_decoded_world *out) {
  qa_q3_parse_bsp(data, data_length, source, &out->map);
  qa_q3_adapt_bsp(&out->map, &out->world);
}

void qa_q3_decoded_world_destroy(qa_q3_decoded_world *world) {
  if (world == NULL) return;
  qa_q3_adapted_world_destroy(&world->world);
  qa_q3_bsp_map_destroy(&world->map);
  memset(world, 0, sizeof *world);
}
