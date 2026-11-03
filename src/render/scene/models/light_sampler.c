/* Port of src/render/scene/models/light-sampler.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/light_sampler.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/formats/q3_map/entities.h"
#include "qa/foundation/support.h"
#include "qa/materials/q2_lightgrid.h"
#include "qa/foundation/js.h"

static const qa_vec3 BLACK = {0, 0, 0}, WHITE = {1, 1, 1};
static bool bit(double flags, int32_t mask) { return (qa_int32(flags) & mask) != 0; }

/* Unified Q1/Q2 map access. */
typedef struct legacy_map {
  bool is_q1;
  const qa_bsp_plane *planes; size_t plane_count;
  const qa_vec3 *vertices; size_t vertex_count;
  const qa_bsp_edge *edges; size_t edge_count;
  qa_double_view surface_edges;
  const qa_bsp_node *nodes; size_t node_count;
  const qa_bsp_face *faces; size_t face_count;
  qa_bsp_lighting lighting;
  const qa_bsp_lighting *lighting_source; /* the world map's own lighting (outlives this view) */
  const qa_decoupled_lightmap *decoupled; size_t decoupled_count;
  /* texture info projections, normalized */
  const qa_q1_texture_info *q1_info; size_t q1_info_count;
  const qa_q2_texture_info *q2_info; size_t q2_info_count;
  const qa_q2_lightgrid *lightgrid; /* q2, nullable */
} legacy_map;

static bool legacy_map_of(qa_decoded_world world, legacy_map *out) {
  memset(out, 0, sizeof *out);
  if (world.kind == QA_DECODED_WORLD_Q1) {
    const qa_q1_world_geometry *m = world.as.q1;
    out->is_q1 = true;
    out->planes = m->planes; out->plane_count = m->plane_count;
    out->vertices = m->vertices; out->vertex_count = m->vertex_count;
    out->edges = m->edges; out->edge_count = m->edge_count;
    out->surface_edges = m->surface_edges;
    out->nodes = m->nodes; out->node_count = m->node_count;
    out->faces = m->faces; out->face_count = m->face_count;
    out->lighting = m->lighting;
    out->lighting_source = &m->lighting;
    out->decoupled = m->decoupled_lightmaps;
    out->decoupled_count = m->decoupled_lightmap_count;
    out->q1_info = m->texture_info; out->q1_info_count = m->texture_info_count;
    return true;
  }
  if (world.kind == QA_DECODED_WORLD_Q2) {
    const qa_q2_world_geometry *m = world.as.q2;
    out->planes = m->planes; out->plane_count = m->plane_count;
    out->vertices = m->vertices; out->vertex_count = m->vertex_count;
    out->edges = m->edges; out->edge_count = m->edge_count;
    out->surface_edges = m->surface_edges;
    out->nodes = m->nodes; out->node_count = m->node_count;
    out->faces = m->faces; out->face_count = m->face_count;
    out->lighting = m->lighting;
    out->lighting_source = &m->lighting;
    out->decoupled = m->decoupled_lightmaps;
    out->decoupled_count = m->decoupled_lightmap_count;
    out->q2_info = m->texture_info; out->q2_info_count = m->texture_info_count;
    out->lightgrid = m->lightgrid;
    return true;
  }
  return false;
}

/* geometry.ts at() mirror. */
static const void *brush_at(const void *values, size_t count, size_t size,
                            double index) {
  if (values == NULL || !(index >= 0) || trunc(index) != index ||
      index >= (double)count)
    qa_range_error("Brush surface index %s outside %zu", QA_JS_NUMBER(index), count);
  return (const char *)values + (size_t)index * size;
}

/* prepareBrushFace mirror (TS owner: src/render/scene/geometry.ts),
 * LightmapFace portion only (warp=false geometry is unused here). */
static qa_lightmap_face prepare_brush_face(const legacy_map *map,
                                           const qa_bsp_face *face,
                                           double face_index) {
  qa_texture_projection projection;
  if (map->is_q1)
    projection = ((const qa_q1_texture_info *)brush_at(
                      map->q1_info, map->q1_info_count,
                      sizeof(qa_q1_texture_info), face->texture_info))->projection;
  else
    projection = ((const qa_q2_texture_info *)brush_at(
                      map->q2_info, map->q2_info_count,
                      sizeof(qa_q2_texture_info), face->texture_info))->projection;
  const qa_bsp_plane *raw = brush_at(map->planes, map->plane_count,
                                     sizeof(qa_bsp_plane), face->plane);
  qa_plane plane = raw->plane;
  if (face->back) {
    plane.normal = qa_scale3(raw->plane.normal, -1);
    plane.distance = -raw->plane.distance;
  }
  double min_s = INFINITY, min_t = INFINITY, max_s = -INFINITY, max_t = -INFINITY;
  for (double e = 0; e < face->edges.count; e++) {
    double signed_edge = *(const double *)brush_at(
        map->surface_edges.values, map->surface_edges.count, sizeof(double),
        face->edges.first + e);
    const qa_bsp_edge *edge = brush_at(map->edges, map->edge_count,
                                       sizeof(qa_bsp_edge), fabs(signed_edge));
    double vertex_index = edge->vertices[signed_edge >= 0 ? 0 : 1];
    const qa_vec3 *point = brush_at(map->vertices, map->vertex_count,
                                    sizeof(qa_vec3), vertex_index);
    double s = qa_dot3(*point, (qa_vec3){projection.s.x, projection.s.y, projection.s.z}) + projection.s.w;
    double t = qa_dot3(*point, (qa_vec3){projection.t.x, projection.t.y, projection.t.z}) + projection.t.w;
    min_s = qa_js_min(min_s, s); min_t = qa_js_min(min_t, t);
    max_s = qa_js_max(max_s, s); max_t = qa_js_max(max_t, t);
  }
  qa_vec2 mins = {floor(min_s / 16) * 16, floor(min_t / 16) * 16};
  const qa_decoupled_lightmap *mapping = NULL;
  if (map->decoupled != NULL && face_index >= 0 &&
      trunc(face_index) == face_index && face_index < (double)map->decoupled_count)
    mapping = &map->decoupled[(size_t)face_index];
  qa_lightmap_face out = {0};
  out.plane = plane;
  out.projection.kind = mapping == NULL ? "classic" : "decoupled";
  out.projection.texture = projection;
  out.projection.texture_mins = mins;
  if (mapping != NULL) out.projection.mapping = *mapping;
  out.width = mapping != NULL ? mapping->width
                              : ceil(max_s / 16) - floor(min_s / 16) + 1;
  out.height = mapping != NULL ? mapping->height
                               : ceil(max_t / 16) - floor(min_t / 16) + 1;
  bool has_sample_offset;
  double sample_offset;
  if (mapping == NULL) {
    has_sample_offset = face->has_lighting_offset;
    sample_offset = face->lighting_offset;
  } else {
    has_sample_offset = mapping->has_lighting_offset;
    sample_offset = mapping->lighting_offset;
  }
  if (!has_sample_offset) {
    out.lighting = NULL;
    out.offset = 0;
  } else {
    out.offset = sample_offset * ((map->is_q1 && map->lighting.is_rgb8) ? 3 : 1);
    /* TS map.lighting: the world's object, retained by the sampler's faces. */
    out.lighting = map->lighting_source;
  }
  out.styles = face->styles.values;
  out.style_count = face->styles.count;
  return out;
}

bool qa_model_light_sampler_init(qa_model_light_sampler *sampler,
                                 const qa_world_scene *world) {
  memset(sampler, 0, sizeof *sampler);
  sampler->world = world;
  qa_decoded_world map = world->map;
  if (map.kind == QA_DECODED_WORLD_Q3) {
    const qa_q3_world_geometry *q3 = map.as.q3;
    size_t entity_count = 0;
    qa_q3_entity *entities = qa_q3_parse_entities(q3->entities, NULL, &entity_count);
    qa_bounds *bounds = calloc(q3->model_count ? q3->model_count : 1, sizeof *bounds);
    qa_lighting_map_record *records =
        calloc(entity_count ? entity_count : 1, sizeof *records);
    qa_lighting_map_pair *pairs = NULL;
    size_t pair_total = 0;
    for (size_t i = 0; i < entity_count; i++) pair_total += entities[i].property_count;
    pairs = calloc(pair_total ? pair_total : 1, sizeof *pairs);
    if (bounds == NULL || records == NULL || pairs == NULL) {
      free(bounds); free(records); free(pairs);
      qa_q3_entities_destroy(entities, entity_count);
      return false;
    }
    for (size_t i = 0; i < q3->model_count; i++) bounds[i] = q3->models[i].bounds;
    size_t cursor = 0;
    for (size_t i = 0; i < entity_count; i++) {
      records[i].entries = pairs + cursor;
      records[i].entry_count = entities[i].property_count;
      for (size_t k = 0; k < entities[i].property_count; k++) {
        pairs[cursor].key = entities[i].properties[k].key;
        pairs[cursor].value = entities[i].properties[k].value;
        cursor++;
      }
    }
    qa_lighting_map lighting_map = {q3->light_grid, q3->light_grid_count,
                                    bounds, q3->model_count,
                                    records, entity_count};
    qa_prepare_light_grid_result prepared = qa_prepare_light_grid(
        lighting_map,
        world->options.has_q3_lightmap_overbright ? world->options.q3_lightmap_overbright : 2, 0);
    sampler->has_grid = prepared.has_grid;
    sampler->grid = prepared.grid;
    free(bounds); free(records); free(pairs);
    qa_q3_entities_destroy(entities, entity_count);
    return true;
  }
  legacy_map legacy;
  if (!legacy_map_of(map, &legacy)) return true;
  size_t capacity = 0;
  for (size_t si = 0; si < world->surface_count; si++) {
    const qa_world_surface *surface = &world->surfaces[si];
    if (surface->is_q3 || surface->legacy_lightmap != NULL) continue;
    bool material_is_q1 = surface->material.kind != NULL && strcmp(surface->material.kind, "q1") == 0;
    bool ordinary = material_is_q1
                        ? (surface->material.surface != NULL &&
                           (strcmp(surface->material.surface, "ordinary") == 0 ||
                            strcmp(surface->material.surface, "fence") == 0))
                        : ((qa_int32(surface->material.surface_flags) & (4 | 8)) == 0);
    double index = surface->index;
    if (!ordinary || !(index >= 0) || trunc(index) != index ||
        index >= (double)legacy.face_count)
      continue;
    if (sampler->unlit_count == capacity) {
      capacity = capacity ? capacity * 2 : 8;
      sampler->unlit_indices =
          realloc(sampler->unlit_indices, capacity * sizeof *sampler->unlit_indices);
      sampler->unlit_faces =
          realloc(sampler->unlit_faces, capacity * sizeof *sampler->unlit_faces);
      if (sampler->unlit_indices == NULL || sampler->unlit_faces == NULL)
        return false;
    }
    sampler->unlit_indices[sampler->unlit_count] = index;
    sampler->unlit_faces[sampler->unlit_count] =
        prepare_brush_face(&legacy, &legacy.faces[(size_t)index], index);
    sampler->unlit_count++;
  }
  return true;
}

void qa_model_light_sampler_destroy(qa_model_light_sampler *sampler) {
  if (sampler->has_grid) qa_light_grid_destroy(&sampler->grid);
  free(sampler->unlit_indices);
  free(sampler->unlit_faces);
  memset(sampler, 0, sizeof *sampler);
}

static qa_vec3 add_dynamic(qa_vec3 color, qa_vec3 point,
                           const qa_world_view_input *input) {
  qa_vec3 result = color; /* TS input.lights ?? [] */
  for (size_t i = 0; i < input->light_count; i++) {
    const qa_surface_dynamic_light *light = &input->lights[i];
    double amount = (light->radius - qa_length3(qa_sub3(point, light->origin))) / 256;
    if (amount > 0) result = qa_add3(result, qa_scale3(light->color, amount));
  }
  return result;
}

typedef struct trace_ctx {
  qa_model_light_sampler *sampler;
  const legacy_map *map;
  const qa_world_view_input *input;
} trace_ctx;

static const qa_lightmap_face *unlit_face(qa_model_light_sampler *sampler,
                                          double index) {
  for (size_t i = 0; i < sampler->unlit_count; i++)
    if (sampler->unlit_indices[i] == index) return &sampler->unlit_faces[i];
  return NULL;
}

static bool trace_light(trace_ctx *ctx, qa_bsp_child child, qa_vec3 start,
                        qa_vec3 end, qa_model_light_sample *out) {
  if (!child.is_node) return false;
  const legacy_map *map = ctx->map;
  if (!(child.index >= 0) || trunc(child.index) != child.index ||
      child.index >= (double)map->node_count)
    qa_range_error("Missing light-trace BSP node %s", QA_JS_NUMBER(child.index));
  const qa_bsp_node *node = &map->nodes[(size_t)child.index];
  if (!(node->plane >= 0) || trunc(node->plane) != node->plane ||
      node->plane >= (double)map->plane_count)
    qa_range_error("Missing light-trace BSP plane %s", QA_JS_NUMBER(node->plane));
  qa_plane plane = map->planes[(size_t)node->plane].plane;
  double front = qa_dot3(start, plane.normal) - plane.distance;
  double back = qa_dot3(end, plane.normal) - plane.distance;
  int side = front < 0 ? 1 : 0;
  if ((back < 0 ? 1 : 0) == side)
    return trace_light(ctx, node->children[side], start, end, out);
  double fraction = front / (front - back);
  qa_vec3 middle = qa_add3(start, qa_scale3(qa_sub3(end, start), fraction));
  if (trace_light(ctx, node->children[side], start, middle, out)) return true;
  for (double i = 0; i < node->faces.count; i++) {
    double surface_index = node->faces.first + i;
    const qa_world_scene *world = ctx->sampler->world;
    if (!(surface_index >= 0) || trunc(surface_index) != surface_index ||
        surface_index >= (double)world->surface_count)
      continue;
    const qa_world_surface *surface = &world->surfaces[(size_t)surface_index];
    if (surface->is_q3) continue;
    const qa_lightmap_face *face = surface->legacy_lightmap != NULL
                                       ? &surface->legacy_lightmap->face
                                       : unlit_face(ctx->sampler, surface->index);
    if (face == NULL) continue;
    qa_vec2 coordinates = qa_lightmap_coordinates(middle, face->projection);
    qa_vec2 uv = coordinates;
    if (strcmp(face->projection.kind, "classic") == 0) {
      qa_texture_projection texture = face->projection.texture;
      uv.x = (trunc(qa_dot3(middle, (qa_vec3){texture.s.x, texture.s.y, texture.s.z}) +
                    texture.s.w) - face->projection.texture_mins.x) / 16;
      uv.y = (trunc(qa_dot3(middle, (qa_vec3){texture.t.x, texture.t.y, texture.t.z}) +
                    texture.t.w) - face->projection.texture_mins.y) / 16;
    }
    if (uv.x < 0 || uv.y < 0 || uv.x > face->width - 1 || uv.y > face->height - 1)
      continue;
    memset(out, 0, sizeof *out);
    out->has_floor = true;
    out->floor_point = middle;
    out->floor_plane = plane;
    if (face->lighting == NULL) {
      out->color = BLACK;
      return true;
    }
    const qa_bsp_lighting *lighting = face->lighting;
    double pixel = trunc(uv.y) * face->width + trunc(uv.x);
    double channels = lighting->is_rgb8 ? 3 : 1;
    double r = 0, g = 0, b = 0;
    for (size_t layer = 0; layer < face->style_count; layer++) {
      double style = face->styles[layer];
      if (style == 255) break;
      double offset = face->offset +
                      ((double)layer * face->width * face->height + pixel) * channels;
      double red = 0, green = 0, blue = 0;
      if (offset >= 0 && trunc(offset) == offset &&
          offset < (double)lighting->samples.length)
        red = lighting->samples.bytes[(size_t)offset];
      double go = offset + (channels == 3 ? 1 : 0);
      double bo = offset + (channels == 3 ? 2 : 0);
      if (go >= 0 && trunc(go) == go && go < (double)lighting->samples.length)
        green = lighting->samples.bytes[(size_t)go];
      if (bo >= 0 && trunc(bo) == bo && bo < (double)lighting->samples.length)
        blue = lighting->samples.bytes[(size_t)bo];
      if (map->is_q1) {
        double value = 256;
        const qa_world_view_input *input = ctx->input;
        if (input->q1_styles != NULL && style >= 0 && trunc(style) == style &&
            style < (double)input->q1_style_count)
          value = input->q1_styles[(size_t)style];
        r += red * value; g += green * value; b += blue * value;
      } else {
        qa_vec3 value = WHITE;
        const qa_world_view_input *input = ctx->input;
        if (input->q2_styles != NULL && style >= 0 && trunc(style) == style &&
            style < (double)input->q2_style_count)
          value = input->q2_styles[(size_t)style].rgb;
        r += red * value.x; g += green * value.y; b += blue * value.z;
      }
    }
    out->color = map->is_q1
                     ? (qa_vec3){(qa_int32(r) >> 8) / 255.0,
                                 (qa_int32(g) >> 8) / 255.0,
                                 (qa_int32(b) >> 8) / 255.0}
                     : (qa_vec3){r / 255, g / 255, b / 255};
    return true;
  }
  return trace_light(ctx, node->children[side == 0 ? 1 : 0], middle, end, out);
}

qa_model_light_sample qa_model_light_sampler_sample(
    qa_model_light_sampler *sampler, qa_vec3 point,
    const qa_world_view_input *input, bool include_dynamic) {
  qa_model_light_sample out = {0};
  qa_decoded_world map = sampler->world->map;
  if (map.kind == QA_DECODED_WORLD_Q3) {
    qa_lighting_sample light;
    qa_vec3 color = WHITE;
    if (qa_light_for_point(sampler->has_grid ? &sampler->grid : NULL, point,
                           (qa_lighting_scales){1, 1}, &light))
      color = qa_scale3(qa_add3(light.ambient_light, light.directed_light), 1.0 / 255);
    out.color = include_dynamic ? add_dynamic(color, point, input) : color;
    return out;
  }
  legacy_map legacy;
  if (!legacy_map_of(map, &legacy)) { out.color = WHITE; return out; }
  if (legacy.lighting.samples.length == 0) { out.color = WHITE; return out; }
  trace_ctx ctx = {sampler, &legacy, input};
  bool has_hit = false;
  qa_model_light_sample hit = {0};
  if (legacy.node_count != 0) {
    qa_vec3 end = point;
    end.z = point.z - 2048;
    has_hit = trace_light(&ctx, (qa_bsp_child){true, 0}, point, end, &hit);
  }
  qa_vec3 raw = BLACK;
  bool has_raw = false;
  if (!legacy.is_q1 && legacy.lightgrid != NULL) {
    static qa_q2_light_style defaults[256];
    static bool defaults_ready = false;
    if (!defaults_ready) {
      for (int i = 0; i < 256; i++) defaults[i] = (qa_q2_light_style){{1, 1, 1}, 3};
      defaults_ready = true;
    }
    const qa_q2_light_style *styles = defaults;
    size_t style_count = 256;
    if (input->q2_styles != NULL) {
      styles = input->q2_styles;
      style_count = input->q2_style_count;
    }
    has_raw = qa_q2_light_grid_point(*legacy.lightgrid, point, styles,
                                     style_count, NULL, &raw);
  }
  if (!has_raw) raw = has_hit ? hit.color : BLACK;
  qa_vec3 color = !legacy.is_q1
                      ? qa_scale3(raw, sampler->world->options.has_q2_light_modulate
                                           ? sampler->world->options.q2_light_modulate
                                           : 1)
                      : raw;
  out.color = include_dynamic ? add_dynamic(color, point, input) : color;
  if (has_hit) {
    out.has_floor = hit.has_floor;
    out.floor_point = hit.floor_point;
    out.floor_plane = hit.floor_plane;
  }
  return out;
}

qa_entity_lighting qa_model_light_sampler_entity_lighting(
    qa_model_light_sampler *sampler, const qa_scene_entity *entity,
    const qa_world_view_input *input, bool no_world_model) {
  double identity_light = input->has_identity_light ? input->identity_light : 1;
  qa_axis axis;
  double scales[3] = {entity->transform.scale.x, entity->transform.scale.y,
                      entity->transform.scale.z};
  for (int i = 0; i < 3; i++)
    axis.vectors[i] = qa_scale3(entity->transform.axis.vectors[i], scales[i]);
  if (sampler->world->map.kind == QA_DECODED_WORLD_Q3 || no_world_model) {
    qa_lighting_entity lighting_entity = {entity->transform.origin,
                                          entity->lighting_origin, axis,
                                          (entity->flags_kind != NULL &&
                                           strcmp(entity->flags_kind, "q3") == 0)
                                              ? entity->flags_bits
                                              : 0};
    qa_entity_lighting_state state = {0};
    state.ambient_scale = 1;
    state.directed_scale = 1;
    state.grid = sampler->has_grid ? &sampler->grid : NULL;
    state.no_world_model = no_world_model;
    state.identity_light = identity_light;
    state.identity_light_byte = trunc(identity_light * 255);
    /* TS this.world.shaders.sun?.direction ?? ...; input.q3Lights ?? []. */
    const qa_scene_shader_registry *shaders = sampler->world->shaders;
    state.sun_direction = shaders != NULL && shaders->has_sun
                              ? shaders->sun_value.direction
                              : qa_normalize3((qa_vec3){0.45, 0.3, 0.9});
    state.dynamic_lights = input->visibility.q3_lights;
    state.dynamic_light_count = input->visibility.q3_light_count;
    qa_entity_lighting out;
    return qa_setup_entity_lighting(lighting_entity, state, &out, NULL);
  }
  qa_vec3 point = (entity->flags_kind != NULL &&
                   strcmp(entity->flags_kind, "q3") == 0 &&
                   bit(entity->flags_bits, 128))
                      ? entity->lighting_origin
                      : entity->transform.origin;
  qa_vec3 color = qa_model_light_sampler_sample(sampler, point, input, true).color;
  qa_vec3 ambient = {qa_js_min(identity_light * 255, color.x * 255 + identity_light * 32),
                     qa_js_min(identity_light * 255, color.y * 255 + identity_light * 32),
                     qa_js_min(identity_light * 255, color.z * 255 + identity_light * 32)};
  qa_entity_lighting out = {0};
  out.ambient_light = ambient;
  out.directed_light = BLACK;
  out.light_dir = (qa_vec3){0, 0, 1};
  out.ambient_light_int =
      (uint32_t)((qa_int32(trunc(ambient.x)) & 255) |
                 ((qa_int32(trunc(ambient.y)) & 255) << 8) |
                 ((qa_int32(trunc(ambient.z)) & 255) << 16) | (int32_t)0xff000000);
  return out;
}
