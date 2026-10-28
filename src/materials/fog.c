/* Port of src/materials/fog.ts. GPL-2.0-or-later. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/materials/fog.h"

static float fr(double v) { return (float)v; }
static bool eq(const char *a, const char *b) {
  return a != NULL && strcmp(a, b) == 0;
}

qa_fog_volume qa_prepare_fog_volume(qa_fog_brush_map map, double index,
                                    qa_q3_material_fog parameters) {
  int64_t fi = (int64_t)index;
  if (fi < 0 || (size_t)fi >= map.fog_count)
    qa_range_error("fog index %lld outside %zu", (long long)fi, map.fog_count);
  qa_fog_brush_map_entry fog = map.fogs[fi];
  int64_t bi = (int64_t)fog.brush;
  if (bi < 0 || (size_t)bi >= map.brush_count)
    qa_range_error("fog index %lld outside %zu", (long long)bi, map.brush_count);
  qa_fog_brush_entry brush = map.brushes[bi];
  if (brush.side_count < 6)
    qa_range_error("fog brush %lld has fewer than six axial sides",
                   (long long)fog.brush);
  qa_plane planes[8];
  int side_total = fog.visible_side >= 0 && fog.visible_side >= 6
      ? (int)fog.visible_side + 1 : 6;
  /* visibleSide < 6 reuses an axial plane; fetch sides 0..max needed. */
  qa_plane *fetched = planes;
  qa_plane *heap = NULL;
  if (side_total > 8) {
    heap = malloc((size_t)side_total * sizeof *heap);
    if (heap == NULL) qa_range_error("fog allocation failed");
    fetched = heap;
  }
  for (int side = 0; side < side_total; side++) {
    int64_t si = (int64_t)brush.first_side + side;
    if (si < 0 || (size_t)si >= map.brush_side_count)
      qa_range_error("fog index %lld outside %zu", (long long)si,
                     map.brush_side_count);
    int64_t pi = (int64_t)map.brush_sides[si].plane;
    if (pi < 0 || (size_t)pi >= map.plane_count)
      qa_range_error("fog index %lld outside %zu", (long long)pi,
                     map.plane_count);
    fetched[side] = map.planes[pi];
  }
  qa_fog_volume out;
  memset(&out, 0, sizeof out);
  out.bounds.min = (qa_vec3){-fetched[0].distance, -fetched[2].distance,
                             -fetched[4].distance};
  out.bounds.max = (qa_vec3){fetched[1].distance, fetched[3].distance,
                             fetched[5].distance};
  if (fog.visible_side >= 0) {
    qa_plane outside = fetched[(int)fog.visible_side];
    out.has_surface = true;
    out.surface.normal = qa_sub3((qa_vec3){0, 0, 0}, outside.normal);
    out.surface.distance = -outside.distance;
  }
  free(heap);
  double depth = parameters.depth_for_opaque;
  out.color = (qa_vec4){
      (qa_mat_to_int32(fr(parameters.color.x * 255)) & 255) / 255.0,
      (qa_mat_to_int32(fr(parameters.color.y * 255)) & 255) / 255.0,
      (qa_mat_to_int32(fr(parameters.color.z * 255)) & 255) / 255.0, 1};
  out.tc_scale = fr(1.0 / fr((depth > 1 ? depth : 1) * 8));
  return out;
}

qa_fog_coordinates_state qa_fog_coordinates(qa_fog_volume fog, qa_vec3 origin,
                                            qa_vec3 forward) {
  qa_fog_coordinates_state state;
  state.distance_vector = qa_scale3(forward, fog.tc_scale);
  qa_vec3 neg_origin = qa_sub3((qa_vec3){0, 0, 0}, origin);
  state.offset = fr(fr(qa_dot3(neg_origin, forward) * fog.tc_scale) + 1.0 / 512);
  state.has_surface = fog.has_surface;
  state.surface = fog.surface;
  state.eye_depth = !fog.has_surface ? 1
      : fr(qa_dot3(origin, fog.surface.normal) - fog.surface.distance);
  return state;
}

qa_vec2 qa_fog_coordinates_at(qa_fog_coordinates_state state,
                              qa_vec3 position) {
  double x = fr(qa_dot3(position, state.distance_vector) + state.offset);
  if (!state.has_surface) {
    qa_vec2 out = {x, 31.0 / 32};
    return out;
  }
  double depth = fr(qa_dot3(position, state.surface.normal) -
                    state.surface.distance);
  double y;
  if (state.eye_depth < 0)
    y = depth < 1 ? 1.0 / 32
        : fr(1.0 / 32 + fr(fr(30.0 / 32 * depth) / fr(depth - state.eye_depth)));
  else
    y = depth < 0 ? 1.0 / 32 : 31.0 / 32;
  qa_vec2 out = {x, y};
  return out;
}

double qa_fog_factor(double s, double t) {
  double distance = fr(s - 1.0 / 512);
  if (distance < 0 || t < 1.0 / 32) return 0;
  if (t < 31.0 / 32)
    distance = fr(distance * fr(fr(t - 1.0 / 32) / (30.0 / 32)));
  distance = fr(distance * 8);
  if (distance > 1) distance = 1;
  int64_t index = (int64_t)trunc(fr(distance * 255));
  if (index < 0 || index >= 256)
    qa_range_error("fog index %lld outside 256", (long long)index);
  return fr(sqrt(fr(index / 255.0)));
}

qa_image_level qa_create_fog_texture(uint8_t *pixels, size_t length) {
  if (pixels == NULL) {
    length = 256 * 32 * 4;
    pixels = malloc(length);
    if (pixels == NULL) qa_range_error("fog allocation failed");
  }
  if (length != 256 * 32 * 4)
    qa_range_error("Fog texture requires 256x32 RGBA bytes");
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 256; x++) {
      double density = qa_fog_factor((x + 0.5) / 256, (y + 0.5) / 32);
      uint8_t *p = pixels + (y * 256 + x) * 4;
      p[0] = 255; p[1] = 255; p[2] = 255;
      p[3] = (uint8_t)qa_mat_to_int32(fr(255 * density));
    }
  }
  qa_image_level out = {256, 32, {pixels, length}};
  return out;
}

static bool is_blended(qa_blend_state blend) {
  return !eq(blend.source, "one") || !eq(blend.destination, "zero");
}

qa_fog_adjustment_kind qa_fog_adjustment(qa_mat_shader_stage stage,
                                         qa_mat_shader_stage first) {
  if (!is_blended(stage.blend) || !is_blended(first.blend)) return "none";
  const char *source = stage.blend.source, *destination = stage.blend.destination;
  if ((eq(source, "one") && eq(destination, "one")) ||
      (eq(source, "zero") && eq(destination, "one-minus-src-color")))
    return "rgb";
  if (eq(source, "src-alpha") && eq(destination, "one-minus-src-alpha"))
    return "alpha";
  if (eq(source, "one") && eq(destination, "one-minus-src-alpha"))
    return "rgba";
  return "none";
}

static double js_round(double v) { return floor(v + 0.5); }

qa_vec4 qa_attenuate_fog_color(qa_vec4 color, const char *adjustment,
                               qa_vec2 coordinates) {
  if (eq(adjustment, "none")) return color;
  double attenuation = fr(1 - qa_fog_factor(coordinates.x, coordinates.y));
  bool rgb = eq(adjustment, "rgb") || eq(adjustment, "rgba");
  bool alpha = eq(adjustment, "alpha") || eq(adjustment, "rgba");
  qa_vec4 out = color;
  if (rgb) {
    out.x = trunc(fr(js_round(color.x * 255) * attenuation)) / 255;
    out.y = trunc(fr(js_round(color.y * 255) * attenuation)) / 255;
    out.z = trunc(fr(js_round(color.z * 255) * attenuation)) / 255;
  }
  if (alpha) out.w = trunc(fr(js_round(color.w * 255) * attenuation)) / 255;
  return out;
}

double qa_shader_sort(const qa_mat_shader_definition *shader) {
  if (shader == NULL) return 3;
  if (shader->stage_count == 0) return 7;
  if (shader->sky != NULL) return 2;
  if (shader->has_sort && shader->sort != 0) return shader->sort;
  if (shader->polygon_offset) return 4;
  qa_mat_parsed_stage first = shader->stages[0];
  return is_blended(first.stage.blend) ? (first.stage.depth_write ? 5 : 9) : 3;
}

qa_fog_pass qa_shader_fog_pass(const qa_mat_shader_definition *shader) {
  if (qa_shader_sort(shader) <= 3) return "equal";
  if (shader != NULL) {
    for (size_t i = 0; i < shader->surface_parms.count; i++)
      if (eq(shader->surface_parms.values[i], "fog")) return "less-equal";
  }
  return "none";
}

qa_render_state qa_fog_pass_state(const char *pass, const char *cull) {
  qa_render_state out = {{"src-alpha", "one-minus-src-alpha"}, pass, false,
                         "none", cull, {0, 1}, NULL};
  return out;
}
