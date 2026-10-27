/* Port of src/render/scene/flare.ts. GPL-2.0-or-later. */
#include "qa/render/scene/flare.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/render/scene/view.h"
static void *xalloc(size_t n) { void *p = malloc(n ? n : 1); return p; }
static char lc(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
bool qa_default_flare_image(const char *path) {
  /* TS toLowerCase + backslash->slash (ASCII; see PORTING string note). */
  size_t len = strlen(path);
  char *name = xalloc(len + 1);
  if (!name) return false;
  for (size_t i = 0; i < len; i++) { char c = path[i] == '\\' ? '/' : path[i]; name[i] = lc(c); }
  name[len] = '\0';
  bool r = !strcmp(name, "misc/flare.tga") || !strncmp(name, "sprites/psx_flare", 17);
  free(name);
  return r;
}
void qa_scene_owned_geometry_destroy(qa_scene_owned_geometry *g) {
  if (!g) return;
  free(g->owned_vertices); free(g->owned_indices); memset(g, 0, sizeof *g);
}
bool qa_flare_geometry(qa_scene_flare flare, qa_vec3 origin, qa_scene_camera camera,
                       const char *image_path, qa_scene_owned_geometry *out) {
  memset(out, 0, sizeof *out);
  qa_vec3 delta = qa_sub3(origin, camera.origin);
  double distance = qa_length3(delta);
  if (distance < flare.fade_start) {
    out->geometry.vertices = NULL; out->geometry.vertex_count = 0;
    out->geometry.indices = NULL; out->geometry.index_count = 0;
    return true;
  }
  double fraction = distance >= flare.fade_end ? 1 : (distance - flare.fade_start) / (flare.fade_end - flare.fade_start);
  bool standard = qa_default_flare_image(image_path);
  double size = (standard ? 50.0 : 25.0) * flare.scale;
  qa_vec3 direction = qa_normalize3(delta);
  qa_vec3 rotated = { direction.z, -direction.x, direction.y };
  qa_vec3 right = flare.lock_angle ? qa_scale3(camera.axis.vectors[1], -1)
    : qa_normalize3(qa_sub3(rotated, qa_scale3(direction, qa_dot3(rotated, direction))));
  qa_vec3 up = flare.lock_angle ? camera.axis.vectors[2] : qa_cross3(right, direction);
  double alpha = trunc((standard ? 160.0 : 128.0) * fraction);
  qa_vec3 rim = flare.rim_color ? *flare.rim_color : flare.color;
  out->owned_vertices = xalloc(5 * sizeof *out->owned_vertices);
  out->owned_indices = xalloc(12 * sizeof *out->owned_indices);
  if (!out->owned_vertices || !out->owned_indices) { qa_scene_owned_geometry_destroy(out); return false; }
  const double spec[5][4] = { {0,0,0.5,0.5}, {-1,-1,0,1}, {-1,1,0,0}, {1,1,1,0}, {1,-1,1,1} };
  for (int i = 0; i < 5; i++) {
    qa_vec3 color3 = i == 0 ? flare.color : rim;
    qa_material_vertex v;
    memset(&v, 0, sizeof v);
    v.position = qa_add3(origin, qa_add3(qa_scale3(right, spec[i][0] * size), qa_scale3(up, spec[i][1] * size)));
    v.normal = qa_scale3(direction, -1);
    v.tex_coord = qa_math_vec2(spec[i][2], spec[i][3]);
    v.lightmap_coord = qa_math_vec2(0, 0);
    v.color = qa_math_vec4(color3.x, color3.y, color3.z, alpha);
    out->owned_vertices[i] = v;
  }
  static const double idx[12] = { 0,2,3, 0,3,4, 0,4,1, 0,1,2 };
  memcpy(out->owned_indices, idx, sizeof idx);
  out->geometry.vertices = out->owned_vertices; out->geometry.vertex_count = 5;
  out->geometry.indices = out->owned_indices; out->geometry.index_count = 12;
  return true;
}
void qa_scene_owned_batch_destroy(qa_scene_owned_batch *b) {
  if (!b) return;
  free(b->owned_vertices); free(b->owned_indices); memset(b, 0, sizeof *b);
}
bool qa_prepare_flare(qa_scene_flare flare, qa_vec3 origin, qa_scene_camera camera,
                      qa_renderer_image image, const char *image_path, qa_scene_owned_batch *out) {
  memset(out, 0, sizeof *out);
  qa_scene_owned_geometry geometry;
  if (!qa_flare_geometry(flare, origin, camera, image_path, &geometry)) return false;
  qa_scene_view_projector project = qa_scene_create_view_projector(camera);
  size_t nv = geometry.geometry.vertex_count, ni = geometry.geometry.index_count;
  out->owned_vertices = xalloc((nv ? nv : 1) * sizeof *out->owned_vertices);
  out->owned_indices = xalloc((ni ? ni : 1) * sizeof *out->owned_indices);
  if (!out->owned_vertices || !out->owned_indices) {
    qa_scene_owned_geometry_destroy(&geometry); qa_scene_owned_batch_destroy(out); return false;
  }
  for (size_t i = 0; i < nv; i++) {
    const qa_material_vertex *v = &geometry.geometry.vertices[i];
    out->owned_vertices[i].position = qa_scene_projector_project(project, v->position);
    out->owned_vertices[i].tex_coord = v->tex_coord;
    out->owned_vertices[i].color = qa_math_vec4(v->color.x/255, v->color.y/255, v->color.z/255, v->color.w/255);
  }
  if (ni) memcpy(out->owned_indices, geometry.geometry.indices, ni * sizeof(double));
  qa_scene_owned_geometry_destroy(&geometry);
  qa_draw_batch *b = &out->batch;
  memset(b, 0, sizeof *b);
  b->primitive_is_lines = false;
  b->texturing_is_pair = false; /* TS texturing "single" */
  b->lighting.kind = "vertex";
  b->indices.values = out->owned_indices; b->indices.count = ni;
  b->vertices = out->owned_vertices; b->vertex_count = nv;
  if (qa_default_flare_image(image_path)) b->texture_effect = "luminance-alpha";
  b->texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; b->texture.image = image;
  b->state.blend.source = "src-alpha"; b->state.blend.destination = "one";
  b->state.depth_test = "less-equal"; b->state.depth_write = false;
  b->state.alpha_test = "none"; b->state.cull = "none";
  b->state.depth_range[0] = 0; b->state.depth_range[1] = 1; b->state.polygon_offset = NULL;
  return true;
}
