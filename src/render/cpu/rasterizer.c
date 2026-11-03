/* Port of src/render/cpu/rasterizer.ts. Quake III CPU backend; fixed-function
 * state follows id Software tr_backend.c and tr_shadows.c.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later. */
#include "qa/render/cpu/rasterizer.h"
#include "qa/foundation/support.h"
#include "qa/materials/source_state.h"
#include "qa/render/commands/dynamic_texture.h"
#include "qa/render/cpu/fog.h"
#include "qa/render/cpu/lighting.h"
#include "qa/render/cpu/lines.h"
#include "qa/render/cpu/row_parallel.h"
#include "qa/render/cpu/textures.h"
#include "qa/render/output_gamma.h"
#include "qa/render/scene/view.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

const qa_render_state QA_CPU_OPAQUE_STATE = { {"one","zero"}, "less-equal", true, "none", "none", {0,1}, NULL };

typedef struct client_coords { bool present; const char *origin; qa_vec2 *values; size_t count; } client_coords;
typedef struct current_coord { bool known; qa_vec2 value; } current_coord;
typedef struct current_color { bool known; qa_vec4 value; } current_color;
typedef struct pending_lighting { struct pending_lighting *next; qa_cpu_triangle_lighting lighting; qa_q2_fragment_light lights[8]; } pending_lighting;
typedef enum row_pool_state { ROW_POOL_UNKNOWN, ROW_POOL_NONE, ROW_POOL_READY } row_pool_state;

typedef struct rimpl {
  qa_cpu_framebuffer fb, main_fb, opacity_fb; bool has_opacity_fb;
  uint32_t *words; double *depth; uint32_t *stencil;
  uint32_t *opacity_words; double *opacity_depth; uint32_t *opacity_stencil;
  uint8_t *output_pixels; bool has_gamma; uint8_t gamma_table[256];
  qa_cpu_images images; qa_cpu_sample sampled;
  qa_rect viewport; bool has_clip; qa_vec4 clip_plane; bool has_vp; qa_mat4 view_projection;
  qa_render_state retained_state; qa_polygon_offset retained_offset;
  qa_vec4 clear_color; double clear_depth; double stencil_maximum, subpixel_scale;
  bool stencil_enabled; const char *stencil_function; double stencil_compare_mask, stencil_write_mask;
  const char *stencil_depth_fail; const char *stencil_depth_pass;
  bool color_write, opacity_active, closed;
  int current_unit;
  bool generic_arrays_once, color_client_enabled, primary_enabled, primary_client_enabled, secondary_enabled, secondary_client_enabled;
  client_coords client_tex_coords[2];
  const char *secondary_environment;
  current_coord current_tex_coords[2];
  current_color current_color;
  bool polygon_line; double line_width;
  bool has_source_bits; uint32_t source_bits;
  bool blend_enabled, depth_test_enabled, portal_view;
  qa_texture_storage levels[2][32];
  /* Row-parallel dispatch. */
  row_pool_state row_pool; qa_row_parallel_pool pool;
  bool collecting; qa_triangle_setup *pending_setups; size_t pending_count, pending_cap; double pending_pixels;
  pending_lighting *pending_lightings;
  qa_software_renderer *atlas_target;
  size_t count;
} rimpl;
static rimpl *R(qa_software_renderer *r) { return (rimpl *)r->impl; }
static void assert_open(qa_software_renderer *r) { if (r->impl == NULL || R(r)->closed) qa_range_error("CPU renderer is closed"); }
static int finite4(qa_vec4 v) { return isfinite(v.x) && isfinite(v.y) && isfinite(v.z) && isfinite(v.w); }
static double f32(double v) { return (double)(float)v; }
static bool is_safe_integer(double v) { return isfinite(v) && floor(v) == v && fabs(v) <= (double)QA_MAX_SAFE_INTEGER; }
static bool is_integer(double v) { return isfinite(v) && floor(v) == v; }
static void *xmalloc(size_t n) { void *m = malloc(n ? n : 1); if (m == NULL) qa_range_error("CPU renderer allocation failed"); return m; }
static bool streq(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static void free_pending_lightings(rimpl *p);

/* retainedState outlives the batch it came from; its offset is copied here. */
static void set_retained(rimpl *p, qa_render_state state) {
  if (state.polygon_offset != NULL) { p->retained_offset = *state.polygon_offset; state.polygon_offset = &p->retained_offset; }
  p->retained_state = state;
}

bool qa_software_renderer_init(qa_software_renderer *r, double width, double height, qa_renderer_resource_owner owner, double subpixel_bits, double stencil_bits, double alpha_bits) {
  if (!is_safe_integer(width) || !is_safe_integer(height) || width <= 0 || height <= 0 || !is_safe_integer(width * height * 8))
    qa_range_error("Image dimensions must be positive integers with a safe storage size");
  if (!is_integer(subpixel_bits) || subpixel_bits < 4 || subpixel_bits > 16) qa_range_error("CPU subpixel precision must be between 4 and 16 bits");
  if (!is_integer(stencil_bits) || stencil_bits < 0 || stencil_bits > 32) qa_range_error("CPU stencil precision must be between 0 and 32 bits");
  rimpl *p = (rimpl *)calloc(1, sizeof *p); if (!p) return false;
  r->impl = p; r->width = width; r->height = height; r->owner = owner; r->subpixel_bits = subpixel_bits; r->stencil_bits = stencil_bits; r->alpha_bits = alpha_bits;
  size_t count = (size_t)(width * height); p->count = count;
  p->words = (uint32_t *)calloc(count, sizeof(uint32_t)); p->depth = (double *)malloc(count * sizeof(double));
  p->stencil = stencil_bits == 0 ? NULL : (uint32_t *)calloc(count, sizeof(uint32_t));
  if (!p->words || !p->depth || (stencil_bits != 0 && !p->stencil)) { free(p->words); free(p->depth); free(p->stencil); free(p); r->impl = NULL; return false; }
  if (alpha_bits == 0) { uint32_t w = qa_cpu_color_word(0, 0, 0, 1); for (size_t i = 0; i < count; i++) p->words[i] = w; }
  for (size_t i = 0; i < count; i++) p->depth[i] = 1;
  p->stencil_maximum = qa_js_pow(2, stencil_bits) - 1; p->subpixel_scale = qa_js_pow(2, subpixel_bits);
  if (!qa_cpu_images_init(&p->images, owner)) { free(p->words); free(p->depth); free(p->stencil); free(p); r->impl = NULL; return false; }
  p->fb.width = width; p->fb.height = height; p->fb.pixels = (uint8_t *)p->words; p->fb.color_words = (int32_t *)p->words; p->fb.depth = p->depth; p->fb.stencil = p->stencil; p->fb.stride = width; p->fb.storage_count = count;
  p->main_fb = p->fb; p->viewport.x = 0; p->viewport.y = 0; p->viewport.width = width; p->viewport.height = height;
  p->clear_depth = 1; p->color_write = true; p->retained_state = QA_CPU_OPAQUE_STATE;
  p->stencil_function = "always"; p->stencil_compare_mask = 4294967295.0; p->stencil_write_mask = 4294967295.0; p->stencil_depth_fail = "keep"; p->stencil_depth_pass = "keep";
  p->sampled.r = p->sampled.g = p->sampled.b = p->sampled.a = 1;
  p->primary_enabled = true; p->secondary_environment = "modulate";
  p->current_tex_coords[0].known = true; p->current_tex_coords[1].known = true;
  p->current_color.known = true; p->current_color.value.x = p->current_color.value.y = p->current_color.value.z = p->current_color.value.w = 1;
  p->line_width = 1;
  p->has_source_bits = true; p->source_bits = QA_SOURCE_STATE_DEPTHTEST_DISABLE | QA_SOURCE_STATE_DEPTHMASK_TRUE;
  return true;
}
bool qa_software_renderer_init_default(qa_software_renderer *r, double w, double h, qa_renderer_resource_owner o) { return qa_software_renderer_init(r, w, h, o, 8, 8, 8); }
const uint8_t *qa_software_renderer_pixels(qa_software_renderer *r, size_t *len) { rimpl *p = R(r); if (len) *len = p->count * 4; return p->output_pixels ? p->output_pixels : p->fb.pixels; }
/* TS rowPool?.jobsRun ?? 0: zero with no pool and after close. */
const char *qa_software_renderer_row_pool_failure(qa_software_renderer *r) { return r->impl != NULL && R(r)->row_pool == ROW_POOL_NONE ? R(r)->pool.failure : NULL; }
double qa_software_renderer_row_parallel_batches(qa_software_renderer *r) { return r->impl != NULL && R(r)->row_pool == ROW_POOL_READY ? R(r)->pool.jobs_run : 0; }
void qa_software_renderer_finish(qa_software_renderer *r) {
  assert_open(r); rimpl *p = R(r); if (!p->has_gamma || !p->output_pixels) return;
  size_t n = p->count * 4;
  for (size_t i = 0; i < n; i++) { uint8_t v = p->fb.pixels[i]; p->output_pixels[i] = (i % 4 == 3) ? v : p->gamma_table[v]; }
}
void qa_software_renderer_set_output_gamma(qa_software_renderer *r, double gamma) {
  assert_open(r); rimpl *p = R(r);
  p->has_gamma = qa_output_gamma_table(gamma, p->gamma_table);
  free(p->output_pixels); p->output_pixels = p->has_gamma ? (uint8_t *)xmalloc(p->count * 4) : NULL;
  qa_software_renderer_finish(r);
}
bool qa_software_renderer_read_rgba(qa_software_renderer *r, qa_image_level *out) {
  assert_open(r); qa_software_renderer_finish(r); rimpl *p = R(r);
  size_t n = p->count * 4; uint8_t *copy = (uint8_t *)malloc(n ? n : 1); if (!copy) return false;
  memcpy(copy, qa_software_renderer_pixels(r, NULL), n);
  out->width = r->width; out->height = r->height; out->pixels.bytes = copy; out->pixels.length = n; return true;
}
void qa_software_renderer_with_object_opacity(qa_software_renderer *r, double opacity, qa_software_draw_fn draw, void *ctx) {
  assert_open(r); rimpl *p = R(r);
  if (!isfinite(opacity) || opacity < 0 || opacity > 1) qa_range_error("Object opacity must be in 0..1");
  if (p->opacity_active) qa_range_error("Object opacity scopes cannot nest");
  if (opacity == 0) return;
  if (opacity == 1) { p->opacity_active = true; draw(ctx); p->opacity_active = false; return; }
  qa_cpu_framebuffer parent = p->fb;
  if (!p->has_opacity_fb) {
    p->opacity_words = (uint32_t *)xmalloc(p->count * sizeof(uint32_t)); p->opacity_depth = (double *)xmalloc(p->count * sizeof(double));
    p->opacity_stencil = parent.stencil ? (uint32_t *)xmalloc(p->count * sizeof(uint32_t)) : NULL;
    p->opacity_fb = parent;
    p->opacity_fb.pixels = (uint8_t *)p->opacity_words; p->opacity_fb.color_words = (int32_t *)p->opacity_words; p->opacity_fb.depth = p->opacity_depth; p->opacity_fb.stencil = p->opacity_stencil;
    p->has_opacity_fb = true;
  }
  qa_cpu_framebuffer scratch = p->opacity_fb;
  memcpy(scratch.pixels, parent.pixels, p->count * 4); memcpy(scratch.depth, parent.depth, p->count * sizeof(double));
  if (scratch.stencil && parent.stencil) memcpy(scratch.stencil, parent.stencil, p->count * sizeof(uint32_t));
  qa_rect vp = p->viewport;
  p->opacity_active = true; p->fb = scratch;
  draw(ctx);
  double left = qa_js_max(0, vp.x), right = qa_js_min(r->width, vp.x + vp.width);
  double y_end = qa_js_min(r->height, vp.y + vp.height);
  for (double y = qa_js_max(0, vp.y); y < y_end; y++) {
    double off_end = (y * r->width + right) * 4;
    for (double off = (y * r->width + left) * 4; off < off_end; off++) {
      size_t o = (size_t)off;
      double backdrop = parent.pixels[o], result = scratch.pixels[o];
      parent.pixels[o] = (r->alpha_bits == 0 && o % 4 == 3) ? 255 : qa_js_to_uint8(qa_js_round(backdrop * (1 - opacity) + result * opacity));
    }
  }
  p->fb = parent; p->opacity_active = false;
}
void qa_software_renderer_apply_q1_fog(qa_software_renderer *r, qa_mat4 proj, qa_q1_fog fog, double sky, const char *output) { assert_open(r); rimpl *p = R(r); qa_apply_q1_depth_fog(&p->fb, p->viewport, proj, fog, sky, output, p->clear_depth); }
void qa_software_renderer_close(qa_software_renderer *r) {
  if (!r->impl) return;
  rimpl *p = R(r); if (p->closed) return;
  if (p->row_pool == ROW_POOL_READY) qa_row_parallel_pool_close(&p->pool);
  p->row_pool = ROW_POOL_NONE;
  qa_cpu_images_destroy(&p->images);
  free(p->words); free(p->depth); free(p->stencil); free(p->opacity_words); free(p->opacity_depth); free(p->opacity_stencil); free(p->output_pixels);
  for (int u = 0; u < 2; u++) free(p->client_tex_coords[u].values);
  free(p->pending_setups); free_pending_lightings(p);
  if (p->atlas_target != NULL) { qa_software_renderer_close(p->atlas_target); free(p->atlas_target); }
  p->closed = true; free(p); r->impl = NULL;
}
void qa_software_renderer_apply_image_resource(qa_software_renderer *r, qa_image_resource_operation op) { assert_open(r); qa_cpu_images_apply(&R(r)->images, op); }
void qa_software_renderer_select_draw_buffer(qa_software_renderer *r, const char *buffer, bool clear) {
  assert_open(r);
  if (strcmp(buffer, "front") && strcmp(buffer, "back")) qa_range_error("CPU stereo draw buffers are unavailable");
  if (clear) {
    rimpl *p = R(r); qa_render_view_state v; memset(&v, 0, sizeof v); v.viewport = p->viewport;
    qa_vec4 plane = p->clip_plane; v.clip_plane = p->has_clip ? &plane : NULL;
    qa_vec4 col; col.x = 1; col.y = 0; col.z = 0.5; col.w = 1;
    qa_render_view_clear cl; cl.depth = 1; cl.color = &col; cl.stencil = false; v.clear = &cl;
    qa_software_renderer_begin_view(r, v);
  }
}
void qa_software_renderer_clear_color_buffer(qa_software_renderer *r) {
  assert_open(r); rimpl *p = R(r); if (!p->color_write) return;
  qa_rect vp = p->viewport; qa_vec4 c = p->clear_color;
  uint32_t word = qa_cpu_color_word(c.x, c.y, c.z, r->alpha_bits == 0 ? 1 : c.w);
  double row_end = qa_js_min(r->height, vp.y + vp.height);
  for (double row = qa_js_max(0, vp.y); row < row_end; row++) {
    double begin = row * r->width + qa_js_max(0, vp.x), end = row * r->width + qa_js_min(r->width, vp.x + vp.width);
    if (begin < end) for (size_t i = (size_t)begin; i < (size_t)end; i++) p->fb.color_words[i] = (int32_t)word;
  }
}
void qa_software_renderer_begin_view(qa_software_renderer *r, qa_render_view_state view) {
  assert_open(r); rimpl *p = R(r);
  qa_rect vp = view.viewport;
  if (!is_safe_integer(vp.x) || !is_safe_integer(vp.y) || !is_safe_integer(vp.width) || !is_safe_integer(vp.height) || vp.width <= 0 || vp.height <= 0)
    qa_range_error("CPU viewport must have integer coordinates and positive dimensions");
  if (view.clip_plane && !finite4(*view.clip_plane)) qa_range_error("CPU clip plane must be finite");
  p->viewport = vp; p->has_clip = view.clip_plane != NULL; if (view.clip_plane) p->clip_plane = *view.clip_plane;
  p->has_vp = view.has_view_projection; p->view_projection = view.view_projection;
  p->portal_view = view.clip_plane != NULL;
  if (!view.clear) return;
  if (!isfinite(view.clear->depth) || (view.clear->color && !finite4(*view.clear->color))) qa_range_error("CPU clear values must be finite");
  p->clear_depth = qa_cpu_clamp01(view.clear->depth);
  if (view.clear->color) { p->clear_color = *view.clear->color; qa_software_renderer_clear_color_buffer(r); }
  double row_end = qa_js_min(r->height, vp.y + vp.height);
  for (double row = qa_js_max(0, vp.y); row < row_end; row++) {
    double begin = row * r->width + qa_js_max(0, vp.x), end = row * r->width + qa_js_min(r->width, vp.x + vp.width);
    if (begin >= end) continue;
    for (size_t i = (size_t)begin; i < (size_t)end; i++) { p->fb.depth[i] = p->clear_depth; if (view.clear->stencil && p->fb.stencil) p->fb.stencil[i] = 0; }
  }
}
double qa_software_renderer_read_depth_pixel(qa_software_renderer *r, double x, double y) {
  assert_open(r); rimpl *p = R(r);
  if (!is_integer(x) || !is_integer(y) || x < 0 || x >= r->width || y < 0 || y >= r->height) qa_range_error("CPU depth readback coordinates must be inside the framebuffer");
  return f32(p->fb.depth[(size_t)((r->height - 1 - y) * r->width + x)]);
}
void qa_software_renderer_set_overdraw_measurement(qa_software_renderer *r, bool enabled) {
  assert_open(r); rimpl *p = R(r);
  if (enabled && !p->fb.stencil) qa_range_error("CPU overdraw measurement requires stencil storage");
  p->stencil_enabled = enabled; p->stencil_function = "always"; p->stencil_compare_mask = 4294967295.0; p->stencil_write_mask = 4294967295.0; p->stencil_depth_fail = "increment"; p->stencil_depth_pass = "increment";
}
void qa_software_renderer_read_stencil_overdraw(qa_software_renderer *r, uint8_t *dest, size_t length) {
  assert_open(r); rimpl *p = R(r);
  if (!p->fb.stencil) qa_range_error("CPU stencil readback requires stencil storage");
  /* Rows are tightly packed, matching the GL backend's pack alignment of one. */
  double stride = r->width;
  if ((double)length < stride * r->height) qa_range_error("CPU stencil destination is too small");
  for (double row = 0; row < r->height; row++) for (double x = 0; x < r->width; x++)
    dest[(size_t)(row * stride + x)] = (uint8_t)(p->fb.stencil[(size_t)((r->height - 1 - row) * r->width + x)] & 255);
}

/* --- geometry --- */
typedef struct screen_vertex {
  double x, y, z, inverse_w, fog_depth_scale;
  qa_vec3 world_position, world_normal; qa_vec2 tex_coord, tex_coord2;
  double r, g, b, a;
} screen_vertex;
/* TS BatchSetupConstants. */
typedef struct batch_constants {
  double depth_near, depth_far; const char *blending;
  bool texture_consumed, primary_alpha, secondary_alpha; const char *secondary_environment;
  qa_cpu_lighting lighting; qa_depth_image_level depth_storage;
  bool vertex_lit; qa_cpu_triangle_lighting vertex_lighting;
} batch_constants;

static qa_cpu_vertex indexed_vertex(const qa_draw_batch *b, size_t offset) {
  if (offset >= b->indices.count) qa_range_error("Missing triangle index");
  double index = b->indices.values[offset];
  /* vertices[index]: a fractional, negative or NaN index reads undefined (Missing triangle vertex). */
  long ii = index >= 0 && index == floor(index) && index < 9007199254740992.0 ? (long)index : -1;
  qa_cpu_world_attributes attributes = qa_cpu_world_attributes_for(b->lighting, index);
  qa_cpu_vertex o; memset(&o, 0, sizeof o);
  if (b->texturing_is_pair) {
    if (ii < 0 || (size_t)ii >= b->pair_vertex_count) qa_range_error("Missing triangle vertex");
    o.position = b->pair_vertices[ii].vertex.position; o.color = b->pair_vertices[ii].vertex.color; o.tex_coord = b->pair_vertices[ii].vertex.tex_coord; o.tex_coord2 = b->pair_vertices[ii].tex_coord2;
  } else {
    if (ii < 0 || (size_t)ii >= b->vertex_count) qa_range_error("Missing triangle vertex");
    o.position = b->vertices[ii].position; o.color = b->vertices[ii].color; o.tex_coord = b->vertices[ii].tex_coord;
  }
  o.world_position = attributes.world_position; o.world_normal = attributes.world_normal;
  return o;
}
/* Clip planes 0..5 are left,right,bottom,top,near,far; 6 is the extra plane. */
enum { PLANE_LEFT, PLANE_RIGHT, PLANE_BOTTOM, PLANE_TOP, PLANE_NEAR, PLANE_FAR, PLANE_EXTRA };
static double plane_distance(const qa_cpu_vertex *v, int plane, const qa_vec4 *extra) {
  qa_vec4 q = v->position;
  switch (plane) {
    case PLANE_LEFT: return q.w + q.x; case PLANE_RIGHT: return q.w - q.x;
    case PLANE_BOTTOM: return q.w + q.y; case PLANE_TOP: return q.w - q.y;
    case PLANE_NEAR: return q.w + q.z; case PLANE_FAR: return q.w - q.z;
    default: return q.x * extra->x + q.y * extra->y + q.z * extra->z + q.w * extra->w;
  }
}
static double coordinate(double left, double right, double aw, double bw) {
  double anchor = qa_js_min(left, right);
  return anchor + (left - anchor) * aw + (right - anchor) * bw;
}
static qa_cpu_vertex intersect(const qa_cpu_vertex *a, const qa_cpu_vertex *b, double da, double db, int plane) {
  /* Symmetric weights give the same intersection for a shared edge in either direction. */
  double scale = qa_js_max(fabs(da), fabs(db));
  double ad = fabs(da) / scale, bd = fabs(db) / scale;
  double aw = bd / (ad + bd), bw = ad / (ad + bd);
  qa_cpu_vertex o;
  double w = a->position.w * aw + b->position.w * bw;
  double x = a->position.x * aw + b->position.x * bw, y = a->position.y * aw + b->position.y * bw, z = a->position.z * aw + b->position.z * bw;
  switch (plane) {
    case PLANE_LEFT: x = -w; break; case PLANE_RIGHT: x = w; break;
    case PLANE_BOTTOM: y = -w; break; case PLANE_TOP: y = w; break;
    case PLANE_NEAR: z = -w; break; case PLANE_FAR: z = w; break;
    default: break;
  }
  o.position.x = x; o.position.y = y; o.position.z = z; o.position.w = w;
  o.world_position.x = a->world_position.x * aw + b->world_position.x * bw; o.world_position.y = a->world_position.y * aw + b->world_position.y * bw; o.world_position.z = a->world_position.z * aw + b->world_position.z * bw;
  o.world_normal.x = a->world_normal.x * aw + b->world_normal.x * bw; o.world_normal.y = a->world_normal.y * aw + b->world_normal.y * bw; o.world_normal.z = a->world_normal.z * aw + b->world_normal.z * bw;
  o.tex_coord.x = coordinate(a->tex_coord.x, b->tex_coord.x, aw, bw); o.tex_coord.y = coordinate(a->tex_coord.y, b->tex_coord.y, aw, bw);
  o.tex_coord2.x = coordinate(a->tex_coord2.x, b->tex_coord2.x, aw, bw); o.tex_coord2.y = coordinate(a->tex_coord2.y, b->tex_coord2.y, aw, bw);
  o.color.x = a->color.x * aw + b->color.x * bw; o.color.y = a->color.y * aw + b->color.y * bw; o.color.z = a->color.z * aw + b->color.z * bw; o.color.w = a->color.w * aw + b->color.w * bw;
  return o;
}
#define CLIP_CAPACITY 16
static size_t clip_polygon(qa_cpu_vertex *polygon, size_t n, const qa_vec4 *extra) {
  double magnitude = 0;
  for (size_t i = 0; i < n; i++) { qa_vec4 q = polygon[i].position; magnitude = qa_js_max(qa_js_max(qa_js_max(qa_js_max(magnitude, fabs(q.x)), fabs(q.y)), fabs(q.z)), fabs(q.w)); }
  if (magnitude > DBL_MAX / 4) {
    /* Homogeneous coordinates admit a common scale. Bound the plane sums
     * without rejecting otherwise finite input or overflowing intersections. */
    for (size_t i = 0; i < n; i++) { polygon[i].position.x /= magnitude; polygon[i].position.y /= magnitude; polygon[i].position.z /= magnitude; polygon[i].position.w /= magnitude; }
  }
  qa_cpu_vertex output[CLIP_CAPACITY]; double distances[CLIP_CAPACITY];
  int planes = extra != NULL ? 7 : 6;
  for (int plane = 0; plane < planes; plane++) {
    if (n == 0) return 0;
    bool clipped = false;
    for (size_t i = 0; i < n; i++) { distances[i] = plane_distance(&polygon[i], plane, extra); if (distances[i] < 0) clipped = true; }
    if (!clipped) continue;
    size_t m = 0;
    const qa_cpu_vertex *previous = &polygon[n - 1]; double previous_distance = distances[n - 1];
    for (size_t i = 0; i < n; i++) {
      double distance = distances[i];
      if ((distance >= 0) != (previous_distance >= 0)) output[m++] = intersect(previous, &polygon[i], previous_distance, distance, plane);
      if (distance >= 0) output[m++] = polygon[i];
      previous = &polygon[i]; previous_distance = distance;
    }
    memcpy(polygon, output, m * sizeof *polygon); n = m;
  }
  /* All six planes permit w=0 only at the homogeneous origin. It has no
   * projected area; remove it before division instead of inventing an epsilon plane. */
  size_t kept = 0;
  for (size_t i = 0; i < n; i++) if (polygon[i].position.w > 0) polygon[kept++] = polygon[i];
  return kept;
}
static double subpixel(double value, double scale) {
  double scaled = f32(value) * scale, lower = floor(scaled), fraction = scaled - lower;
  return ((fraction < 0.5 || (fraction == 0.5 && fmod(lower, 2) == 0)) ? lower : lower + 1) / scale;
}
static screen_vertex project_vertex(const qa_cpu_vertex *vertex, const qa_rect *viewport, double w_scale, double subpixel_scale, double fog_scale) {
  qa_vec4 q = vertex->position;
  /* A common scale preserves all perspective ratios and bounds reciprocals. */
  double inverse_w = w_scale / q.w;
  screen_vertex o;
  o.x = subpixel(viewport->x + (q.x / q.w + 1) * viewport->width * 0.5, subpixel_scale);
  o.y = subpixel(viewport->y + (1 - q.y / q.w) * viewport->height * 0.5, subpixel_scale);
  o.z = q.z / q.w; o.inverse_w = inverse_w; o.fog_depth_scale = w_scale * fog_scale;
  o.world_position = vertex->world_position; o.world_normal = vertex->world_normal;
  o.tex_coord = vertex->tex_coord; o.tex_coord2 = vertex->tex_coord2;
  o.r = vertex->color.x * inverse_w; o.g = vertex->color.y * inverse_w; o.b = vertex->color.z * inverse_w; o.a = vertex->color.w * inverse_w;
  return o;
}
static double edge_of(const screen_vertex *a, const screen_vertex *b, double x, double y) { return (a->y - b->y) * x + (b->x - a->x) * y + (a->x * b->y - a->y * b->x); }
static bool lower_left(const screen_vertex *a, const screen_vertex *b) { return b->y < a->y || (b->y == a->y && b->x < a->x); }

static qa_cpu_lighting fragment_lighting(rimpl *p, const qa_draw_batch *batch, qa_depth_image_level *storage) {
  qa_cpu_lighting out; out.parameters = batch->lighting; out.depth = NULL;
  if (streq(batch->lighting.kind, "vertex")) return out;
  if (streq(batch->lighting.kind, "q2-model-shadow")) { *storage = qa_cpu_images_depth_image(&p->images, batch->lighting.atlas_value.image); out.depth = storage; }
  else if (batch->lighting.atlas != NULL) { *storage = qa_cpu_images_depth_image(&p->images, batch->lighting.atlas->image); out.depth = storage; }
  return out;
}
static const char *classify_blending(qa_blend_state b) {
  if (streq(b.source, "one") && streq(b.destination, "zero")) return "opaque";
  if (streq(b.source, "src-alpha") && streq(b.destination, "one-minus-src-alpha")) return "alpha";
  if (streq(b.source, "one") && streq(b.destination, "one")) return "add";
  if ((streq(b.source, "dst-color") && streq(b.destination, "zero")) || (streq(b.source, "zero") && streq(b.destination, "src-color"))) return "multiply";
  if (streq(b.source, "dst-color") && streq(b.destination, "one-minus-dst-alpha")) return "dst-color-inverse-dst-alpha";
  return "general";
}
static void batch_setup_constants(rimpl *p, const qa_draw_batch *batch, const qa_bound_texture *texture, const qa_bound_texture *secondary, batch_constants *out) {
  const qa_render_state *state = &batch->state;
  out->depth_near = qa_cpu_clamp01(state->depth_range[0]); out->depth_far = qa_cpu_clamp01(state->depth_range[1]);
  out->blending = classify_blending(state->blend);
  out->texture_consumed = p->color_write || !streq(state->alpha_test, "none");
  out->primary_alpha = texture->kind != QA_BOUND_TEXTURE_INCOMPLETE && qa_cpu_texture_has_alpha(texture->internal_format);
  out->secondary_alpha = secondary->kind != QA_BOUND_TEXTURE_INCOMPLETE && qa_cpu_texture_has_alpha(secondary->internal_format);
  out->secondary_environment = batch->texturing_is_pair ? batch->second_texture.environment : NULL;
  out->lighting = fragment_lighting(p, batch, &out->depth_storage);
  out->vertex_lit = streq(batch->lighting.kind, "vertex");
  if (out->vertex_lit) {
    /* The kernel never reads positions/normals unless fragment-lit, so zero triples stand in. */
    memset(&out->vertex_lighting, 0, sizeof out->vertex_lighting);
    out->vertex_lighting.base = out->lighting;
  }
}
static void triangle(qa_software_renderer *r, screen_vertex a, screen_vertex b, screen_vertex c, const qa_draw_batch *batch,
    const qa_bound_texture *texture, const qa_bound_texture *secondary_texture, const screen_vertex *interpolation, const batch_constants *invariants) {
  rimpl *p = R(r);
  double area = edge_of(&a, &b, c.x, c.y);
  if (!isfinite(area) || area == 0) return;
  if ((streq(batch->state.cull, "back") && area > 0) || (streq(batch->state.cull, "front") && area < 0)) return;
  if (area < 0) { screen_vertex previous = b; b = c; c = previous; area = -area; }
  double min_x = qa_js_max(qa_js_max(0, p->viewport.x), ceil(qa_js_min(qa_js_min(a.x, b.x), c.x) - 0.5));
  double max_x = qa_js_min(qa_js_min(r->width - 1, p->viewport.x + p->viewport.width - 1), floor(qa_js_max(qa_js_max(a.x, b.x), c.x) - 0.5));
  double min_y = qa_js_max(qa_js_max(0, p->viewport.y), ceil(qa_js_min(qa_js_min(a.y, b.y), c.y) - 0.5));
  double max_y = qa_js_min(qa_js_min(r->height - 1, p->viewport.y + p->viewport.height - 1), floor(qa_js_max(qa_js_max(a.y, b.y), c.y) - 0.5));
  bool edge_a_inclusive = lower_left(&b, &c), edge_b_inclusive = lower_left(&c, &a), edge_c_inclusive = lower_left(&a, &b);
  const screen_vertex *ia = interpolation != NULL ? &interpolation[0] : &a;
  const screen_vertex *ib = interpolation != NULL ? &interpolation[1] : &b;
  const screen_vertex *ic = interpolation != NULL ? &interpolation[2] : &c;
  double inverse_area = 1 / (interpolation == NULL ? area : edge_of(ia, ib, ic->x, ic->y));
  const qa_render_state *state = &batch->state;
  double depth_near = invariants->depth_near, depth_far = invariants->depth_far;
  bool white = ia->r == ia->inverse_w && ia->g == ia->inverse_w && ia->b == ia->inverse_w && ia->a == ia->inverse_w
    && ib->r == ib->inverse_w && ib->g == ib->inverse_w && ib->b == ib->inverse_w && ib->a == ib->inverse_w
    && ic->r == ic->inverse_w && ic->g == ic->inverse_w && ic->b == ic->inverse_w && ic->a == ic->inverse_w;
  qa_triangle_setup s; memset(&s, 0, sizeof s);
  double attribute_ax = ib->y - ic->y, attribute_ay = ic->x - ib->x, attribute_ac = ib->x * ic->y - ib->y * ic->x;
  double attribute_bx = ic->y - ia->y, attribute_by = ia->x - ic->x, attribute_bc = ic->x * ia->y - ic->y * ia->x;
  double attribute_cx = ia->y - ib->y, attribute_cy = ib->x - ia->x, attribute_cc = ia->x * ib->y - ia->y * ib->x;
  s.edge_ax = b.y - c.y; s.edge_ay = c.x - b.x; s.edge_ac = b.x * c.y - b.y * c.x;
  s.edge_bx = c.y - a.y; s.edge_by = a.x - c.x; s.edge_bc = c.x * a.y - c.y * a.x;
  s.edge_cx = a.y - b.y; s.edge_cy = b.x - a.x; s.edge_cc = a.x * b.y - a.y * b.x;
  double az = ia->z, bz = ib->z, cz = ic->z;
  s.constant_depth = az == bz && bz == cz;
  /* Fixed-point 24-bit depth resolution, matching the SDL GL depth buffer. */
  double polygon_depth_offset = state->polygon_offset == NULL ? 0
    : qa_js_max(fabs(az * attribute_ax + bz * attribute_bx + cz * attribute_cx), fabs(az * attribute_ay + bz * attribute_by + cz * attribute_cy))
      * fabs(inverse_area) * 0.5 * fabs(depth_far - depth_near) * f32(state->polygon_offset->factor) + qa_js_pow(2, -QA_CPU_OFFSET_DEPTH_BITS) * f32(state->polygon_offset->units);
  s.plane_depth = qa_cpu_clamp01(qa_cpu_clamp01(az * 0.5 + 0.5) * (depth_far - depth_near) + depth_near + polygon_depth_offset);
  double aiw = ia->inverse_w, biw = ib->inverse_w, ciw = ic->inverse_w;
  /* Delay UV/W until a common anchor is known. Subtracting U' - s*Q' with
   * large absolute s can invent a nonzero LOD for a constant coordinate. */
  double u_anchor = ia->tex_coord.x, v_anchor = ia->tex_coord.y;
  double au = (ia->tex_coord.x - u_anchor) * aiw, bu = (ib->tex_coord.x - u_anchor) * biw, cu = (ic->tex_coord.x - u_anchor) * ciw;
  double av = (ia->tex_coord.y - v_anchor) * aiw, bv = (ib->tex_coord.y - v_anchor) * biw, cv = (ic->tex_coord.y - v_anchor) * ciw;
  double u2_anchor = ia->tex_coord2.x, v2_anchor = ia->tex_coord2.y;
  double au2 = (ia->tex_coord2.x - u2_anchor) * aiw, bu2 = (ib->tex_coord2.x - u2_anchor) * biw, cu2 = (ic->tex_coord2.x - u2_anchor) * ciw;
  double av2 = (ia->tex_coord2.y - v2_anchor) * aiw, bv2 = (ib->tex_coord2.y - v2_anchor) * biw, cv2 = (ic->tex_coord2.y - v2_anchor) * ciw;
  double q_dx = (aiw * attribute_ax + biw * attribute_bx + ciw * attribute_cx) * inverse_area;
  double q_dy = (aiw * attribute_ay + biw * attribute_by + ciw * attribute_cy) * inverse_area;
  s.derivative.u_anchor = u_anchor; s.derivative.v_anchor = v_anchor;
  s.derivative.u_dx = (au * attribute_ax + bu * attribute_bx + cu * attribute_cx) * inverse_area;
  s.derivative.v_dx = (av * attribute_ax + bv * attribute_bx + cv * attribute_cx) * inverse_area; s.derivative.q_dx = q_dx;
  s.derivative.u_dy = (au * attribute_ay + bu * attribute_by + cu * attribute_cy) * inverse_area;
  s.derivative.v_dy = (av * attribute_ay + bv * attribute_by + cv * attribute_cy) * inverse_area; s.derivative.q_dy = q_dy;
  s.secondary_derivative.u_anchor = u2_anchor; s.secondary_derivative.v_anchor = v2_anchor;
  s.secondary_derivative.u_dx = (au2 * attribute_ax + bu2 * attribute_bx + cu2 * attribute_cx) * inverse_area;
  s.secondary_derivative.v_dx = (av2 * attribute_ax + bv2 * attribute_bx + cv2 * attribute_cx) * inverse_area; s.secondary_derivative.q_dx = q_dx;
  s.secondary_derivative.u_dy = (au2 * attribute_ay + bu2 * attribute_by + cu2 * attribute_cy) * inverse_area;
  s.secondary_derivative.v_dy = (av2 * attribute_ay + bv2 * attribute_by + cv2 * attribute_cy) * inverse_area; s.secondary_derivative.q_dy = q_dy;
  if (batch->fog != NULL) { s.fog = batch->fog; s.has_fog_depth_scale = true; s.fog_depth_scale = ia->fog_depth_scale; }
  s.texture_effect = batch->texture_effect;
  s.min_x = min_x; s.max_x = max_x; s.min_y = min_y; s.max_y = max_y; s.inverse_area = inverse_area; s.depth_near = depth_near; s.depth_far = depth_far;
  s.attribute_ax = attribute_ax; s.attribute_ay = attribute_ay; s.attribute_ac = attribute_ac;
  s.attribute_bx = attribute_bx; s.attribute_by = attribute_by; s.attribute_bc = attribute_bc;
  s.attribute_cx = attribute_cx; s.attribute_cy = attribute_cy; s.attribute_cc = attribute_cc;
  s.az = az; s.bz = bz; s.cz = cz; s.polygon_depth_offset = polygon_depth_offset;
  s.aiw = aiw; s.biw = biw; s.ciw = ciw; s.au = au; s.bu = bu; s.cu = cu; s.av = av; s.bv = bv; s.cv = cv;
  s.au2 = au2; s.bu2 = bu2; s.cu2 = cu2; s.av2 = av2; s.bv2 = bv2; s.cv2 = cv2;
  s.ar = ia->r; s.br = ib->r; s.cr = ic->r; s.ag = ia->g; s.bg = ib->g; s.cg = ic->g;
  s.ab = ia->b; s.bb = ib->b; s.cb = ic->b; s.aa = ia->a; s.ba = ib->a; s.ca = ic->a;
  s.edge_a_inclusive = edge_a_inclusive; s.edge_b_inclusive = edge_b_inclusive; s.edge_c_inclusive = edge_c_inclusive;
  s.white = white; s.depth_write = state->depth_write; s.stencil_enabled = p->stencil_enabled; s.color_write = p->color_write;
  s.texture_consumed = invariants->texture_consumed; s.primary_alpha = invariants->primary_alpha; s.secondary_alpha = invariants->secondary_alpha;
  s.blending = invariants->blending; s.depth_test = state->depth_test; s.alpha_test = state->alpha_test; s.secondary_environment = invariants->secondary_environment;
  s.texture = *texture; s.secondary_texture = *secondary_texture;
  s.width = r->width; s.height = r->height; s.blend = state->blend; s.alpha_bits = r->alpha_bits;
  s.stencil_function = p->stencil_function; s.stencil_compare_mask = p->stencil_compare_mask; s.stencil_write_mask = p->stencil_write_mask;
  s.stencil_maximum = p->stencil_maximum; s.stencil_depth_fail = p->stencil_depth_fail; s.stencil_depth_pass = p->stencil_depth_pass;
  qa_cpu_triangle_lighting local; qa_q2_fragment_light local_lights[8];
  if (invariants->vertex_lit) s.lighting = &invariants->vertex_lighting;
  else {
    qa_cpu_triangle_lighting *tl = &local; qa_q2_fragment_light *lights = local_lights;
    if (p->collecting) {
      pending_lighting *node = (pending_lighting *)xmalloc(sizeof *node);
      node->next = p->pending_lightings; p->pending_lightings = node;
      tl = &node->lighting; lights = node->lights;
    }
    qa_vec3 positions[3]; positions[0] = ia->world_position; positions[1] = ib->world_position; positions[2] = ic->world_position;
    tl->base = qa_cull_triangle_lights(invariants->lighting, positions, lights);
    for (int i = 0; i < 3; i++) tl->positions[i] = positions[i];
    tl->normals[0] = ia->world_normal; tl->normals[1] = ib->world_normal; tl->normals[2] = ic->world_normal;
    s.lighting = tl;
  }
  if (p->collecting) {
    if (p->pending_count == p->pending_cap) {
      size_t cap = p->pending_cap ? p->pending_cap * 2 : 64;
      qa_triangle_setup *grown = (qa_triangle_setup *)realloc(p->pending_setups, cap * sizeof *grown);
      if (grown == NULL) qa_range_error("CPU renderer allocation failed");
      p->pending_setups = grown; p->pending_cap = cap;
    }
    p->pending_setups[p->pending_count++] = s;
    p->pending_pixels += qa_js_max(0, s.max_x - s.min_x + 1) * qa_js_max(0, s.max_y - s.min_y + 1);
  } else qa_run_triangle_rows_default(&s, &p->fb, &p->sampled);
}

typedef struct line_ctx { qa_software_renderer *r; const qa_draw_batch *batch; const qa_bound_texture *texture, *secondary; qa_cpu_lighting lighting; } line_ctx;
static void line_fragment(line_ctx *c, qa_line_fragment fragment) {
  qa_software_renderer *rr = c->r; rimpl *p = R(rr); const qa_draw_batch *batch = c->batch;
  size_t index = (size_t)(fragment.y * rr->width + fragment.x);
  double previous_depth = p->fb.depth[index];
  const qa_render_state *state = &batch->state;
  double near_d = qa_cpu_clamp01(state->depth_range[0]), far_d = qa_cpu_clamp01(state->depth_range[1]);
  double depth = qa_cpu_clamp01(fragment.depth) * (far_d - near_d) + near_d;
  bool depth_passed = !((streq(state->depth_test, "less-equal") && depth > previous_depth) || (streq(state->depth_test, "equal") && depth != previous_depth));
  if (!depth_passed && !p->stencil_enabled) return;
  bool texture_consumed = p->color_write || !streq(state->alpha_test, "none");
  double r = qa_cpu_clamp01(fragment.color.x), g = qa_cpu_clamp01(fragment.color.y), b = qa_cpu_clamp01(fragment.color.z), alpha = qa_cpu_clamp01(fragment.color.w);
  qa_vec4 vertex_color; vertex_color.x = r; vertex_color.y = g; vertex_color.z = b; vertex_color.w = alpha;
  qa_cpu_sample texel; texel.r = texel.g = texel.b = texel.a = 1;
  if (texture_consumed && c->texture->kind != QA_BOUND_TEXTURE_INCOMPLETE) {
    bool has_alpha = qa_cpu_texture_has_alpha(c->texture->internal_format);
    qa_cpu_sample_line_bound(c->texture, fragment.tex_coord, fragment.tex_coord_derivative, &p->sampled);
    texel = p->sampled; if (!has_alpha) texel.a = 1;
    if (streq(batch->texture_effect, "luminance-alpha")) {
      double modulation = (p->sampled.r + p->sampled.g + p->sampled.b) / 3 * alpha;
      p->sampled.r *= modulation; p->sampled.g *= modulation; p->sampled.b *= modulation;
      texel.r = p->sampled.r; texel.g = p->sampled.g; texel.b = p->sampled.b;
    }
    r *= p->sampled.r; g *= p->sampled.g; b *= p->sampled.b;
    if (has_alpha) alpha *= p->sampled.a;
  }
  if (texture_consumed && !streq(batch->lighting.kind, "vertex")) {
    qa_cpu_sample result = qa_shade_q2_fragment(&c->lighting, fragment.world_position, fragment.world_normal, vertex_color, &texel);
    r = result.r; g = result.g; b = result.b; alpha = result.a;
  }
  if (texture_consumed && batch->texturing_is_pair && c->secondary->kind != QA_BOUND_TEXTURE_INCOMPLETE) {
    const char *environment = batch->second_texture.environment;
    qa_cpu_sample_line_bound(c->secondary, fragment.tex_coord2, fragment.tex_coord2_derivative, &p->sampled);
    r = qa_cpu_texture_color(r, p->sampled.r, environment); g = qa_cpu_texture_color(g, p->sampled.g, environment);
    b = qa_cpu_texture_color(b, p->sampled.b, environment);
    if (qa_cpu_texture_has_alpha(c->secondary->internal_format)) alpha = streq(environment, "replace") ? p->sampled.a : alpha * p->sampled.a;
  }
  if (batch->fog != NULL) {
    const qa_batch_fog *fog = batch->fog;
    bool constant = streq(fog->kind, "constant");
    double d = streq(fog->kind, "exp2") ? fog->density * fragment.eye_depth / 64 : 0;
    double amount = constant ? fog->amount : 1 - exp(-d * d);
    const char *effect = constant ? "color" : fog->effect != NULL ? fog->effect : "color";
    if (!streq(effect, "none")) { r = qa_cpu_clamp01(r); g = qa_cpu_clamp01(g); b = qa_cpu_clamp01(b); }
    if (streq(effect, "color")) { r += (fog->color.x - r) * amount; g += (fog->color.y - g) * amount; b += (fog->color.z - b) * amount; }
    if (streq(effect, "rgb") || streq(effect, "rgba")) { r *= 1 - amount; g *= 1 - amount; b *= 1 - amount; }
    if (streq(effect, "alpha") || streq(effect, "rgba")) alpha *= 1 - amount;
    if (streq(effect, "overlay")) { r = fog->color.x; g = fog->color.y; b = fog->color.z; alpha *= amount; }
  }
  if (!qa_cpu_passes_alpha(alpha, state->alpha_test)) return;
  if (p->stencil_enabled && !qa_cpu_stencil_fragment(p->fb.stencil, p->count, (double)index, depth_passed, p->stencil_function, p->stencil_compare_mask,
      p->stencil_write_mask, p->stencil_maximum, p->stencil_depth_fail, p->stencil_depth_pass)) return;
  if (!p->color_write) return;
  uint32_t destination = (uint32_t)p->fb.color_words[index];
  double dr = (QA_CPU_LITTLE_ENDIAN ? destination & 255 : destination >> 24) / 255.0;
  double dg = ((destination >> (QA_CPU_LITTLE_ENDIAN ? 8 : 16)) & 255) / 255.0;
  double db = ((destination >> (QA_CPU_LITTLE_ENDIAN ? 16 : 8)) & 255) / 255.0;
  double da = rr->alpha_bits == 0 ? 1 : (QA_CPU_LITTLE_ENDIAN ? destination >> 24 : destination & 255) / 255.0;
  r = qa_cpu_clamp01(r); g = qa_cpu_clamp01(g); b = qa_cpu_clamp01(b); alpha = qa_cpu_clamp01(alpha);
  size_t offset = index * 4;
  p->fb.pixels[offset] = (uint8_t)qa_cpu_blend(r, dr, alpha, da, state->blend, false);
  p->fb.pixels[offset + 1] = (uint8_t)qa_cpu_blend(g, dg, alpha, da, state->blend, false);
  p->fb.pixels[offset + 2] = (uint8_t)qa_cpu_blend(b, db, alpha, da, state->blend, false);
  p->fb.pixels[offset + 3] = rr->alpha_bits == 0 ? 255 : (uint8_t)qa_cpu_blend(alpha, da, alpha, da, state->blend, true);
  if (state->depth_write) p->fb.depth[index] = depth;
}
static void emit_line(void *ctx, qa_line_fragment fragment) {
  line_ctx *c = (line_ctx *)ctx; rimpl *p = R(c->r);
  double x = fragment.x + p->viewport.x, y = fragment.y + p->viewport.y;
  if (x >= 0 && x < c->r->width && y >= 0 && y < c->r->height) { fragment.x = x; fragment.y = y; line_fragment(c, fragment); }
}
static void rasterize_line(line_ctx *c, qa_cpu_vertex a, qa_cpu_vertex b, double width) {
  rimpl *p = R(c->r); qa_rect v = p->viewport;
  qa_line_scissor scissor;
  scissor.min_x = qa_js_max(0, -v.x); scissor.min_y = qa_js_max(0, -v.y);
  scissor.max_x = qa_js_min(v.width - 1, c->r->width - 1 - v.x); scissor.max_y = qa_js_min(v.height - 1, c->r->height - 1 - v.y);
  qa_rasterize_aliased_line(a, b, v.width, v.height, width, scissor, emit_line, c);
}
static void draw_line(line_ctx *c, qa_cpu_vertex a, qa_cpu_vertex b, double width) {
  rimpl *p = R(c->r);
  if (p->has_clip) {
    double da = plane_distance(&a, PLANE_EXTRA, &p->clip_plane), db = plane_distance(&b, PLANE_EXTRA, &p->clip_plane);
    if (da < 0 && db < 0) return;
    if (da < 0) a = intersect(&a, &b, da, db, PLANE_EXTRA);
    else if (db < 0) b = intersect(&a, &b, da, db, PLANE_EXTRA);
  }
  rasterize_line(c, a, b, width);
}
static void draw_polygon_outline(qa_software_renderer *r, const qa_cpu_vertex *polygon, size_t n, const qa_draw_batch *batch, const qa_bound_texture *texture, const qa_bound_texture *secondary, const batch_constants *invariants) {
  rimpl *p = R(r);
  if (n == 0) return;
  screen_vertex projected[CLIP_CAPACITY];
  for (size_t i = 0; i < n; i++) projected[i] = project_vertex(&polygon[i], &p->viewport, 1, p->subpixel_scale, 1);
  double area = 0; const screen_vertex *previous = &projected[n - 1];
  for (size_t i = 0; i < n; i++) { area += previous->x * projected[i].y - previous->y * projected[i].x; previous = &projected[i]; }
  if (!isfinite(area) || (streq(batch->state.cull, "back") && area >= 0) || (streq(batch->state.cull, "front") && area < 0)) return;
  /* GL_POLYGON_OFFSET_FILL has no effect on polygon-line fragments. Keep the
   * clipping-created boundary and never draw triangulation spokes. */
  line_ctx c; c.r = r; c.batch = batch; c.texture = texture; c.secondary = secondary; c.lighting = invariants->lighting;
  qa_cpu_vertex edge_start = polygon[0];
  for (size_t index = 1; index <= n; index++) {
    qa_cpu_vertex edge_end = index == n ? polygon[0] : polygon[index];
    /* clipPolygon already applied the portal plane. A second test can reject
     * its rounded boundary points. */
    rasterize_line(&c, edge_start, edge_end, p->line_width);
    edge_start = edge_end;
  }
}
static void draw_triangle(qa_software_renderer *r, const qa_cpu_vertex *a, const qa_cpu_vertex *b, const qa_cpu_vertex *c, const qa_draw_batch *batch,
    const qa_bound_texture *texture, const qa_bound_texture *secondary, const batch_constants *invariants) {
  rimpl *p = R(r);
  const qa_cpu_vertex *original[3] = {a, b, c};
  double magnitude = 0;
  if (batch->fog != NULL) for (int i = 0; i < 3; i++) { qa_vec4 q = original[i]->position; magnitude = qa_js_max(qa_js_max(qa_js_max(qa_js_max(magnitude, fabs(q.x)), fabs(q.y)), fabs(q.z)), fabs(q.w)); }
  double fog_scale = magnitude > DBL_MAX / 4 ? magnitude : 1;
  qa_cpu_vertex polygon[CLIP_CAPACITY]; polygon[0] = *a; polygon[1] = *b; polygon[2] = *c;
  size_t n = clip_polygon(polygon, 3, p->has_clip ? &p->clip_plane : NULL);
  if (n == 0) return;
  if (p->polygon_line) { draw_polygon_outline(r, polygon, n, batch, texture, secondary, invariants); return; }
  screen_vertex projected[3]; const screen_vertex *interpolation = NULL;
  bool all_inside = true, any_outside = false;
  for (int i = 0; i < 3; i++) {
    qa_vec4 q = original[i]->position;
    if (!(q.w > 0 && q.z >= -q.w && q.z <= q.w)) all_inside = false;
    if (fabs(q.x) > q.w || fabs(q.y) > q.w) any_outside = true;
  }
  if (all_inside && any_outside) {
    /* The measured GL profile retains the original snapped interpolation plane.
     * Rebuilding it from newly snapped clip vertices changes UVs on thin triangles.
     * Coverage still uses the bounded clipped polygon. */
    double scale = qa_js_min(qa_js_min(a->position.w, b->position.w), c->position.w);
    for (int i = 0; i < 3; i++) projected[i] = project_vertex(original[i], &p->viewport, scale, p->subpixel_scale, 1);
    double area = edge_of(&projected[0], &projected[1], projected[2].x, projected[2].y);
    if (isfinite(area)) {
      if (area == 0) return;
      interpolation = projected;
    }
  }
  for (size_t index = 1; index + 1 < n; index++) {
    double w_scale = qa_js_min(qa_js_min(polygon[0].position.w, polygon[index].position.w), polygon[index + 1].position.w);
    triangle(r, project_vertex(&polygon[0], &p->viewport, w_scale, p->subpixel_scale, fog_scale),
      project_vertex(&polygon[index], &p->viewport, w_scale, p->subpixel_scale, fog_scale),
      project_vertex(&polygon[index + 1], &p->viewport, w_scale, p->subpixel_scale, fog_scale), batch, texture, secondary, interpolation, invariants);
  }
}
static void draw_all(qa_software_renderer *r, const qa_draw_batch *batch, const qa_bound_texture *texture, const qa_bound_texture *secondary, const batch_constants *invariants) {
  for (size_t offset = 0; offset < batch->indices.count; offset += 3) {
    qa_cpu_vertex a = indexed_vertex(batch, offset), b = indexed_vertex(batch, offset + 1), c = indexed_vertex(batch, offset + 2);
    draw_triangle(r, &a, &b, &c, batch, texture, secondary, invariants);
  }
}
static qa_row_shared_framebuffer main_shared(qa_software_renderer *r) {
  rimpl *p = R(r); qa_row_shared_framebuffer shared;
  shared.width = r->width; shared.height = r->height; shared.color = (uint8_t *)p->words; shared.depth = p->depth; shared.stencil = p->stencil;
  return shared;
}
static void free_pending_lightings(rimpl *p) {
  while (p->pending_lightings != NULL) { pending_lighting *next = p->pending_lightings->next; free(p->pending_lightings); p->pending_lightings = next; }
}
typedef struct collect_ctx { qa_software_renderer *r; const qa_draw_batch *batch; const qa_bound_texture *texture, *secondary; const batch_constants *invariants; } collect_ctx;
static void collect_batch(void *ctx) { collect_ctx *c = (collect_ctx *)ctx; draw_all(c->r, c->batch, c->texture, c->secondary, c->invariants); }
static void draw_batch(qa_software_renderer *r, const qa_draw_batch *batch, const qa_bound_texture *texture, const qa_bound_texture *secondary) {
  rimpl *p = R(r);
  if (batch->indices.count == 0) return;
  if (batch->primitive_is_lines) {
    qa_depth_image_level depth_storage;
    line_ctx c; c.r = r; c.batch = batch; c.texture = texture; c.secondary = secondary; c.lighting = fragment_lighting(p, batch, &depth_storage);
    for (size_t offset = 0; offset < batch->indices.count; offset += 2)
      draw_line(&c, indexed_vertex(batch, offset), indexed_vertex(batch, offset + 1), batch->line_width);
    return;
  }
  batch_constants invariants;
  batch_setup_constants(p, batch, texture, secondary, &invariants);
  /* Row-parallel dispatch collects this batch's setups and rasterizes them by
   * disjoint row bands. Draw order per pixel is unchanged; nothing outside this
   * call can observe a partially rasterized batch. */
  if (p->fb.pixels != p->main_fb.pixels || p->opacity_active || p->collecting) { draw_all(r, batch, texture, secondary, &invariants); return; }
  if (p->row_pool == ROW_POOL_UNKNOWN) p->row_pool = qa_row_parallel_pool_try_create(&p->pool, main_shared(r)) ? ROW_POOL_READY : ROW_POOL_NONE;
  /* Without a worker pool the setups would run serially anyway; execute them immediately. */
  if (p->row_pool == ROW_POOL_NONE) { draw_all(r, batch, texture, secondary, &invariants); return; }
  free_pending_lightings(p); /* a previous batch's kernel error left them behind */
  p->collecting = true; p->pending_count = 0; p->pending_pixels = 0;
  collect_ctx collect = {r, batch, texture, secondary, &invariants}; char failure[1024];
  bool collected = qa_range_error_catch(collect_batch, &collect, failure, sizeof failure);
  p->collecting = false;
  if (!collected) {
    for (size_t i = 0; i < p->pending_count; i++) qa_run_triangle_rows_default(&p->pending_setups[i], &p->fb, &p->sampled);
    p->pending_count = 0; free_pending_lightings(p);
    qa_range_error("%s", failure);
  }
  /* The setups and their culled light storage stay alive through the synchronous barrier. */
  if (p->pending_count >= 4 && p->pending_pixels >= 50000) qa_row_parallel_pool_run(&p->pool, p->pending_setups, p->pending_count);
  else for (size_t i = 0; i < p->pending_count; i++) qa_run_triangle_rows_default(&p->pending_setups[i], &p->fb, &p->sampled);
  p->pending_count = 0;
  free_pending_lightings(p);
}

/* --- validation and prepared draws --- */
static void validate_batch(rimpl *p, const qa_draw_batch *b) {
  if (!streq(b->lighting.kind, "vertex")) {
    if (b->lighting.light_count > 8 || b->lighting.model_shadow_light_count > 8) qa_range_error("Q2 fragment lighting accepts at most eight selected lights per draw");
    for (size_t i = 0; i < b->indices.count; i++) {
      qa_cpu_world_attributes a = qa_cpu_world_attributes_for(b->lighting, b->indices.values[i]);
      if (!(isfinite(a.world_position.x) && isfinite(a.world_position.y) && isfinite(a.world_position.z)
        && isfinite(a.world_normal.x) && isfinite(a.world_normal.y) && isfinite(a.world_normal.z)))
        qa_range_error("CPU fragment lighting attributes must be finite");
    }
    qa_depth_image_level storage; (void)fragment_lighting(p, b, &storage);
  }
  size_t step = b->primitive_is_lines ? 2 : 3;
  if (b->indices.count % step != 0) qa_range_error("Incomplete CPU primitive indices");
  if (b->primitive_is_lines && (!isfinite(b->line_width) || b->line_width <= 0)) qa_range_error("CPU line width must be positive");
  size_t vc = b->texturing_is_pair ? b->pair_vertex_count : b->vertex_count;
  for (size_t i = 0; i < b->indices.count; i++) {
    double ix = b->indices.values[i];
    if (!is_safe_integer(ix) || ix < 0 || ix >= (double)vc) qa_range_error("CPU vertex index is outside its array");
  }
  if (b->texturing_is_pair) {
    for (size_t i = 0; i < b->pair_vertex_count; i++) {
      const qa_render_vertex *v = &b->pair_vertices[i].vertex;
      if (!finite4(v->position) || !finite4(v->color) || !isfinite(v->tex_coord.x) || !isfinite(v->tex_coord.y)) qa_range_error("CPU vertex attributes must be finite");
    }
    for (size_t i = 0; i < b->pair_vertex_count; i++) {
      const qa_multitexture_vertex *v = &b->pair_vertices[i];
      if (!isfinite(v->tex_coord2.x) || !isfinite(v->tex_coord2.y)) qa_range_error("CPU secondary coordinates must be finite");
    }
  } else {
    for (size_t i = 0; i < b->vertex_count; i++) {
      const qa_render_vertex *v = &b->vertices[i];
      if (!finite4(v->position) || !finite4(v->color) || !isfinite(v->tex_coord.x) || !isfinite(v->tex_coord.y)) qa_range_error("CPU vertex attributes must be finite");
    }
  }
}
/* TS snapshotBatch / snapshotSourceBatch storage. */
typedef struct owned_batch { qa_draw_batch batch; qa_render_vertex *vertices; qa_multitexture_vertex *pair_vertices; double *indices; qa_polygon_offset offset; } owned_batch;
static void own_batch(owned_batch *o, qa_draw_batch input, qa_render_state state, const qa_mat4 *view_projection) {
  memset(o, 0, sizeof *o);
  o->batch = input;
  if (state.polygon_offset != NULL) { o->offset = *state.polygon_offset; state.polygon_offset = &o->offset; }
  o->batch.state = state;
  o->indices = (double *)xmalloc(input.indices.count * sizeof *o->indices);
  if (input.indices.count) memcpy(o->indices, input.indices.values, input.indices.count * sizeof *o->indices);
  o->batch.indices.values = o->indices;
  bool world = streq(input.space, "world");
  if (input.texturing_is_pair) {
    o->pair_vertices = (qa_multitexture_vertex *)xmalloc(input.pair_vertex_count * sizeof *o->pair_vertices);
    if (input.pair_vertex_count) memcpy(o->pair_vertices, input.pair_vertices, input.pair_vertex_count * sizeof *o->pair_vertices);
    o->batch.pair_vertices = o->pair_vertices;
  } else {
    o->vertices = (qa_render_vertex *)xmalloc(input.vertex_count * sizeof *o->vertices);
    if (input.vertex_count) memcpy(o->vertices, input.vertices, input.vertex_count * sizeof *o->vertices);
    o->batch.vertices = o->vertices;
  }
  if (world) {
    size_t n = input.texturing_is_pair ? input.pair_vertex_count : input.vertex_count;
    if (n != 0 && view_projection == NULL) qa_range_error("CPU world-space batch requires a view projection");
    for (size_t i = 0; i < n; i++) {
      qa_vec4 *position = input.texturing_is_pair ? &o->pair_vertices[i].vertex.position : &o->vertices[i].position;
      qa_vec3 point; point.x = position->x; point.y = position->y; point.z = position->z;
      *position = qa_scene_project_point(*view_projection, point);
    }
  }
}
static void free_owned(owned_batch *o) { free(o->vertices); free(o->pair_vertices); free(o->indices); }
static void resolver_apply(void *ctx, qa_image_resource_operation op) { qa_software_renderer_apply_image_resource((qa_software_renderer *)ctx, op); }
static void apply_diagnostic_state(rimpl *p, qa_render_state state) {
  set_retained(p, state);
  p->blend_enabled = !streq(state.blend.source, "one") || !streq(state.blend.destination, "zero");
  p->depth_test_enabled = true;
  p->polygon_line = false;
  p->has_source_bits = false;
}
static qa_bound_texture bound_unit(rimpl *p, int unit) { return qa_cpu_images_bound(&p->images, unit, p->levels[unit], 32); }
static qa_bound_texture incomplete_texture(void) { qa_bound_texture t; memset(&t, 0, sizeof t); t.kind = QA_BOUND_TEXTURE_INCOMPLETE; return t; }

typedef enum phase { PHASE_PREPARED, PHASE_BEGUN, PHASE_TEXTURED, PHASE_DRAWN, PHASE_CLEANED } phase;
typedef struct prep { qa_software_renderer *r; owned_batch owned; qa_texture_resolver resolver; phase phase; double next_unit; } prep;
static void prep_begin(void *ctx) {
  prep *q = (prep *)ctx; assert_open(q->r);
  if (q->phase != PHASE_PREPARED) qa_range_error("CPU prepared draw has already begun");
  apply_diagnostic_state(R(q->r), q->owned.batch.state);
  q->phase = PHASE_BEGUN;
}
static void prep_texture(void *ctx, double unit, qa_texture_binding op) {
  prep *q = (prep *)ctx; assert_open(q->r); rimpl *p = R(q->r);
  if (q->phase != PHASE_BEGUN || unit != q->next_unit || (unit != 0 && unit != 1) || (unit == 1 && !q->owned.batch.texturing_is_pair))
    qa_range_error("CPU texture slots must execute in order");
  qa_cpu_images_bind(&p->images, unit, qa_texture_resolver_resolve(&q->resolver, op));
  p->current_unit = (int)unit;
  if (unit == 0) p->primary_enabled = true; else p->secondary_enabled = true;
  q->next_unit++;
}
static void prep_draw(void *ctx) {
  prep *q = (prep *)ctx; assert_open(q->r); rimpl *p = R(q->r);
  const qa_draw_batch *batch = &q->owned.batch;
  if (q->phase != PHASE_BEGUN || q->next_unit != (batch->texturing_is_pair ? 2 : 1)) qa_range_error("CPU prepared draw has unapplied texture slots");
  qa_bound_texture texture = bound_unit(p, 0), secondary = batch->texturing_is_pair ? bound_unit(p, 1) : incomplete_texture();
  draw_batch(q->r, batch, &texture, &secondary);
  if (batch->indices.count != 0) {
    p->current_color.known = false;
    p->current_tex_coords[0].known = false;
    if (batch->texturing_is_pair) p->current_tex_coords[1].known = false;
  }
  q->phase = PHASE_DRAWN;
}
static void prep_cleanup(void *ctx) {
  prep *q = (prep *)ctx; assert_open(q->r); rimpl *p = R(q->r);
  if (q->phase != PHASE_DRAWN) qa_range_error("CPU prepared draw has not completed");
  if (q->owned.batch.texturing_is_pair) p->secondary_enabled = false;
  p->current_unit = 0;
  q->phase = PHASE_CLEANED;
  qa_texture_resolver_destroy(&q->resolver); free_owned(&q->owned); free(q);
}
qa_prepared_backend_draw qa_software_renderer_prepare_geometry(qa_software_renderer *r, qa_draw_batch batch) {
  assert_open(r); rimpl *p = R(r);
  prep *q = (prep *)xmalloc(sizeof *q);
  own_batch(&q->owned, batch, batch.state, p->has_vp ? &p->view_projection : NULL);
  if (!qa_texture_resolver_init(&q->resolver, resolver_apply, r)) qa_range_error("CPU renderer allocation failed");
  validate_batch(p, &q->owned.batch);
  q->r = r; q->phase = PHASE_PREPARED; q->next_unit = 0;
  qa_prepared_backend_draw o; memset(&o, 0, sizeof o);
  o.ctx = q; o.begin = prep_begin; o.apply_texture = prep_texture; o.draw = prep_draw; o.cleanup = prep_cleanup;
  return o;
}
void qa_software_renderer_draw(qa_software_renderer *r, qa_draw_batch batch) {
  qa_prepared_backend_draw prepared = qa_software_renderer_prepare_geometry(r, batch);
  prepared.begin(prepared.ctx);
  prepared.apply_texture(prepared.ctx, 0, batch.texture);
  if (batch.texturing_is_pair) prepared.apply_texture(prepared.ctx, 1, batch.second_texture.binding);
  prepared.draw(prepared.ctx);
  prepared.cleanup(prepared.ctx);
}
static void draw_quad(qa_software_renderer *r, qa_renderer_image image, const double x[4], const double y[4], const double s[4], const double t[4], qa_vec4 color, qa_render_state state) {
  qa_render_vertex vertices[4]; static const double indices[6] = {0, 1, 2, 0, 2, 3};
  for (int i = 0; i < 4; i++) {
    vertices[i].position.x = x[i] * 2 / r->width - 1; vertices[i].position.y = 1 - y[i] * 2 / r->height; vertices[i].position.z = -1; vertices[i].position.w = 1;
    vertices[i].tex_coord.x = s[i]; vertices[i].tex_coord.y = t[i]; vertices[i].color = color;
  }
  qa_draw_batch b; memset(&b, 0, sizeof b);
  b.lighting.kind = "vertex"; b.indices.values = indices; b.indices.count = 6;
  b.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; b.texture.image = image; b.state = state; b.vertices = vertices; b.vertex_count = 4;
  qa_software_renderer_draw(r, b);
}
void qa_software_renderer_draw_stretch_pic(qa_software_renderer *r, qa_renderer_image image, qa_rect rect, qa_texture_rect uv, qa_vec4 color) {
  qa_render_view_state v; memset(&v, 0, sizeof v); v.viewport.width = r->width; v.viewport.height = r->height;
  qa_software_renderer_begin_view(r, v);
  double x[4] = {rect.x, rect.x + rect.width, rect.x + rect.width, rect.x}, y[4] = {rect.y, rect.y, rect.y + rect.height, rect.y + rect.height};
  double s[4] = {uv.s1, uv.s2, uv.s2, uv.s1}, t[4] = {uv.t1, uv.t1, uv.t2, uv.t2};
  qa_render_state state = QA_CPU_OPAQUE_STATE;
  state.blend.source = "src-alpha"; state.blend.destination = "one-minus-src-alpha"; state.depth_test = "always"; state.depth_write = false;
  draw_quad(r, image, x, y, s, t, color, state);
}
void qa_software_renderer_draw_show_image(qa_software_renderer *r, qa_renderer_image image, qa_rect rect, bool proportional) {
  double width = rect.width * (proportional ? image.width / 512 : 1), height = rect.height * (proportional ? image.height / 512 : 1);
  double x[4] = {rect.x, rect.x + width, rect.x + width, rect.x}, y[4] = {rect.y, rect.y, rect.y + height, rect.y + height};
  double s[4] = {0, 1, 1, 0}, t[4] = {0, 0, 1, 1};
  qa_vec4 white; white.x = white.y = white.z = white.w = 1;
  draw_quad(r, image, x, y, s, t, white, R(r)->retained_state);
}

/* --- immediate operations --- */
/* Atlas image row zero is V=0. Raster rows run from the top of the target. Only
 * pass rects are copied: cleared passes overwrite their rect in beginView, and
 * untouched regions round-trip identically. */
static void atlas_copy_rect(rimpl *target, qa_depth_image_level image, qa_rect viewport, bool to_target) {
  float *pixels = (float *)(uintptr_t)image.pixels; /* the images table owns this storage mutably */
  double x0 = qa_js_max(0, viewport.x), x1 = qa_js_min(image.width, viewport.x + viewport.width);
  double y0 = qa_js_max(0, viewport.y), y1 = qa_js_min(image.height, viewport.y + viewport.height);
  if (x0 >= x1) return;
  for (double y = y0; y < y1; y++) {
    size_t image_row = (size_t)(y * image.width), target_row = (size_t)((image.height - 1 - y) * image.width);
    for (size_t x = (size_t)x0; x < (size_t)x1; x++) {
      if (to_target) target->fb.depth[target_row + x] = pixels[image_row + x];
      else pixels[image_row + x] = (float)target->fb.depth[target_row + x];
    }
  }
}
static void immediate_depth_atlas(qa_software_renderer *r, const qa_render_operation *op) {
  rimpl *p = R(r);
  qa_depth_image_level image = qa_cpu_images_depth_image(&p->images, op->image);
  if (p->atlas_target != NULL && (p->atlas_target->width != image.width || p->atlas_target->height != image.height)) {
    qa_software_renderer_close(p->atlas_target); free(p->atlas_target); p->atlas_target = NULL;
  }
  if (p->atlas_target == NULL) {
    /* Reused target; passes fully specify viewport/clear/draw state. */
    p->atlas_target = (qa_software_renderer *)xmalloc(sizeof *p->atlas_target);
    if (!qa_software_renderer_init(p->atlas_target, image.width, image.height, r->owner, r->subpixel_bits, 0, 8)) qa_range_error("CPU renderer allocation failed");
  }
  qa_software_renderer *target = p->atlas_target; rimpl *tp = R(target);
  /* Atlas passes are small; a second pool would double the worker threads. */
  if (tp->row_pool == ROW_POOL_READY) qa_row_parallel_pool_close(&tp->pool);
  tp->row_pool = ROW_POOL_NONE;
  tp->color_write = false;
  for (size_t i = 0; i < op->pass_count; i++) if (!op->passes[i].has_clear_depth) atlas_copy_rect(tp, image, op->passes[i].viewport, true);
  for (size_t i = 0; i < op->pass_count; i++) {
    const qa_depth_atlas_pass *pass = &op->passes[i];
    qa_render_view_state view; memset(&view, 0, sizeof view);
    view.viewport = pass->viewport; view.viewport.y = image.height - pass->viewport.y - pass->viewport.height;
    qa_render_view_clear clear; clear.depth = pass->clear_depth; clear.color = NULL; clear.stencil = false;
    view.clear = pass->has_clear_depth ? &clear : NULL;
    qa_software_renderer_begin_view(target, view);
    for (size_t j = 0; j < pass->draw_count; j++) {
      const qa_depth_atlas_draw *draw = &pass->draws[j];
      size_t count = (draw->position_count + 3) / 4;
      qa_render_vertex *vertices = (qa_render_vertex *)xmalloc(count * sizeof *vertices);
      for (size_t k = 0; k < count; k++) {
        const double *q = draw->positions + k * 4; size_t left = draw->position_count - k * 4;
        vertices[k].position.x = q[0]; vertices[k].position.y = left > 1 ? q[1] : 0; vertices[k].position.z = left > 2 ? q[2] : 0; vertices[k].position.w = left > 3 ? q[3] : 0;
        vertices[k].tex_coord.x = 0; vertices[k].tex_coord.y = 0;
        vertices[k].color.x = vertices[k].color.y = vertices[k].color.z = vertices[k].color.w = 1;
      }
      qa_draw_batch batch; memset(&batch, 0, sizeof batch);
      batch.lighting.kind = "vertex"; batch.indices = draw->indices; batch.texture.kind = QA_TEXTURE_BINDING_RETAIN_CURRENT;
      batch.state = QA_CPU_OPAQUE_STATE; batch.state.cull = draw->cull; batch.state.polygon_offset = draw->polygon_offset;
      batch.vertices = vertices; batch.vertex_count = count;
      qa_software_renderer_draw(target, batch);
      free(vertices);
    }
  }
  for (size_t i = 0; i < op->pass_count; i++) atlas_copy_rect(tp, image, op->passes[i].viewport, false);
}
static void immediate_sky_side(qa_software_renderer *r, const qa_render_operation *op) {
  rimpl *p = R(r);
  for (size_t s = 0; s < op->strip_count; s++) {
    const qa_sky_strip *strip = &op->strips[s];
    if (strip->vertex_count < 2 || strip->vertex_count % 2 != 0) qa_range_error("CPU sky strips require paired row vertices");
    size_t index_count = (strip->vertex_count - 2) * 3;
    double *indices = (double *)xmalloc(index_count * sizeof *indices);
    for (size_t index = 0, o = 0; index + 2 < strip->vertex_count; index++) {
      indices[o++] = (double)(index + index % 2); indices[o++] = (double)(index + 1 - index % 2); indices[o++] = (double)(index + 2);
    }
    qa_render_vertex *vertices = (qa_render_vertex *)xmalloc(strip->vertex_count * sizeof *vertices);
    for (size_t i = 0; i < strip->vertex_count; i++) { vertices[i].position = strip->vertices[i].position; vertices[i].tex_coord = strip->vertices[i].tex_coord; vertices[i].color = op->color; }
    qa_draw_batch batch; memset(&batch, 0, sizeof batch);
    batch.lighting.kind = "vertex"; batch.indices.values = indices; batch.indices.count = index_count;
    batch.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; batch.texture.image = op->image; batch.state = p->retained_state;
    batch.state.blend.source = "one"; batch.state.blend.destination = "zero"; batch.state.alpha_test = "none";
    batch.vertices = vertices; batch.vertex_count = strip->vertex_count;
    qa_software_renderer_draw(r, batch);
    free(indices); free(vertices);
  }
}
static void immediate_shadow(qa_software_renderer *r, const qa_render_operation *op) {
  rimpl *p = R(r);
  if (r->stencil_bits < 4) return;
  bool finishing = op->kind == QA_RENDER_OP_SHADOW_FINISH; double shade = f32(finishing ? 0.6 : 0.2);
  const qa_vec4 *positions = finishing ? op->finish_positions : op->positions;
  size_t count = finishing ? 4 : op->position_count;
  qa_render_vertex *vertices = (qa_render_vertex *)xmalloc(count * sizeof *vertices);
  for (size_t i = 0; i < count; i++) {
    vertices[i].position = positions[i]; vertices[i].color.x = vertices[i].color.y = vertices[i].color.z = shade; vertices[i].color.w = 1;
    vertices[i].tex_coord.x = 0; vertices[i].tex_coord.y = 0;
  }
  p->stencil_enabled = true;
  p->stencil_function = finishing ? "nonzero" : "always";
  p->stencil_compare_mask = 255;
  p->stencil_depth_fail = "keep";
  p->stencil_depth_pass = finishing ? "keep" : "increment";
  p->color_write = finishing;
  if (finishing) p->has_clip = false;
  qa_render_state state = p->retained_state;
  state.alpha_test = "none"; state.depth_test = "less-equal"; state.depth_write = finishing;
  state.blend.source = finishing ? "dst-color" : "one"; state.blend.destination = "zero";
  state.cull = !finishing ? (op->mirror ? "front" : "back") : "none";
  static const double finish_indices[6] = {0, 1, 2, 0, 2, 3};
  qa_draw_batch batch; memset(&batch, 0, sizeof batch);
  batch.lighting.kind = "vertex"; batch.vertices = vertices; batch.vertex_count = count;
  if (finishing) { batch.indices.values = finish_indices; batch.indices.count = 6; } else batch.indices = op->indices;
  batch.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; batch.texture.image = op->white_image; batch.state = state;
  qa_software_renderer_draw(r, batch);
  if (!finishing) {
    p->stencil_depth_pass = "decrement";
    batch.state.cull = streq(state.cull, "front") ? "back" : "front";
    qa_software_renderer_draw(r, batch);
  }
  p->color_write = true;
  if (finishing) p->stencil_enabled = false;
  free(vertices);
}
void qa_software_renderer_draw_immediate(qa_software_renderer *r, qa_render_operation op) {
  assert_open(r); rimpl *p = R(r);
  switch (op.kind) {
    case QA_RENDER_OP_Q2_FOG: qa_apply_q2_depth_fog(&p->fb, op.fog, r->alpha_bits); return;
    case QA_RENDER_OP_DEPTH_ATLAS: immediate_depth_atlas(r, &op); return;
    case QA_RENDER_OP_DISABLE_PORTAL_CLIP: p->has_clip = false; return;
    case QA_RENDER_OP_DEPTH_RANGE: p->retained_state.depth_range[0] = op.range[0]; p->retained_state.depth_range[1] = op.range[1]; return;
    case QA_RENDER_OP_CULL: p->retained_state.cull = op.cull; return;
    case QA_RENDER_OP_POLYGON_OFFSET: {
      qa_render_state state = p->retained_state; state.polygon_offset = op.polygon_offset; set_retained(p, state); return;
    }
    case QA_RENDER_OP_SKY_SIDE: immediate_sky_side(r, &op); return;
    case QA_RENDER_OP_SHADOW_VOLUME: case QA_RENDER_OP_SHADOW_FINISH: immediate_shadow(r, &op); return;
    default: qa_range_error("Invalid CPU operation");
  }
}

/* --- source stages --- */
static void state_change_visit(qa_source_state_change change, void *ctx) {
  rimpl *p = (rimpl *)ctx;
  if (streq(change.kind, "depth-function")) p->retained_state.depth_test = change.value;
  else if (streq(change.kind, "blend")) {
    p->blend_enabled = change.enabled;
    if (change.enabled) { p->retained_state.blend.source = change.source; p->retained_state.blend.destination = change.destination; }
  }
  else if (streq(change.kind, "depth-write")) p->retained_state.depth_write = change.bool_value;
  else if (streq(change.kind, "polygon-mode")) p->polygon_line = streq(change.value, "line");
  else if (streq(change.kind, "depth-test")) p->depth_test_enabled = change.enabled;
  else if (streq(change.kind, "alpha-test")) p->retained_state.alpha_test = change.value;
  else qa_range_error("Invalid source state operation %s", change.kind);
}
static void apply_source_state(rimpl *p, uint32_t bits) {
  (void)qa_source_state_changes(p->has_source_bits, p->source_bits, bits, state_change_visit, p);
  p->has_source_bits = true; p->source_bits = bits;
}
static qa_render_state raster_state(rimpl *p) {
  qa_render_state state = p->retained_state;
  if (!p->blend_enabled) { state.blend.source = "one"; state.blend.destination = "zero"; }
  if (!p->depth_test_enabled) { state.depth_test = "always"; state.depth_write = false; }
  return state;
}
static void set_client(rimpl *p, int unit, const char *origin, qa_vec2 *values, size_t count) {
  client_coords *c = &p->client_tex_coords[unit];
  if (c->values != values) free(c->values);
  c->present = true; c->origin = origin; c->values = values; c->count = count;
}
static qa_vec2 *scratch_coords(const qa_source_stage_cell *scratch, size_t count, const char *origin) {
  qa_vec2 *values = (qa_vec2 *)xmalloc(count * sizeof *values);
  for (size_t i = 0; i < count; i++)
    values[i] = streq(origin, "svars0") ? scratch[i].tex_coord : streq(origin, "svars1") ? scratch[i].tex_coord2 : streq(origin, "raw0") ? scratch[i].raw_tex_coord : scratch[i].raw_tex_coord2;
  return values;
}
static void refresh_source_coordinates(rimpl *p, const qa_source_stage_cell *scratch, size_t count) {
  for (int unit = 0; unit < 2; unit++) {
    client_coords *array = &p->client_tex_coords[unit];
    if (!array->present || streq(array->origin, "local") || streq(array->origin, "direct")) continue;
    set_client(p, unit, array->origin, scratch_coords(scratch, count, array->origin), count);
  }
}
static void validate_client_coordinates(rimpl *p, const qa_double_view *indices) {
  for (int unit = 0; unit < 2; unit++) {
    if (!(unit == 0 ? p->primary_client_enabled : p->secondary_client_enabled)) continue;
    const client_coords *array = &p->client_tex_coords[unit];
    if (!array->present) qa_range_error("CPU texture unit %d has no source client coordinate array", unit);
    for (size_t i = 0; i < indices->count; i++) {
      double index = indices->values[i];
      /* array.values[index] is undefined for a fractional index too. */
      if (!(index >= 0) || index >= (double)array->count || index != floor(index)) qa_range_error("CPU texture unit %d client coordinate index is outside its array", unit);
      qa_vec2 c = array->values[(size_t)index];
      if (!isfinite(f32(c.x)) || !isfinite(f32(c.y))) qa_range_error("CPU source client coordinates must be finite float32 values");
    }
  }
}
static qa_bound_texture enabled_texture(rimpl *p, int unit) {
  return (unit == 0 ? p->primary_enabled : p->secondary_enabled) ? bound_unit(p, unit) : incomplete_texture();
}
static qa_bound_texture immediate_texture(rimpl *p, int unit) {
  qa_bound_texture texture = enabled_texture(p, unit);
  current_coord coordinate = p->current_tex_coords[unit];
  if (texture.kind == QA_BOUND_TEXTURE_INCOMPLETE || texture.kind == QA_BOUND_TEXTURE_CONSTANT) return texture;
  /* tr_shadows.c RB_ShadowTessEnd masks color and disables alpha testing. Those
   * fragments consume only depth/stencil, leaving this texture unsampled. */
  if (!p->color_write && streq(p->retained_state.alpha_test, "none")) return texture;
  if (coordinate.known) {
    qa_cpu_sample value; value.r = value.g = value.b = value.a = 1;
    qa_cpu_sample_bound(&texture, coordinate.value.x, coordinate.value.y, 0, &value);
    qa_bound_texture out = incomplete_texture();
    out.kind = QA_BOUND_TEXTURE_CONSTANT; out.internal_format = texture.internal_format; out.sample = value;
    return out;
  }
  const qa_texture_storage *base = &texture.levels[0];
  bool alpha = qa_cpu_texture_has_alpha(texture.internal_format);
  /* Unknown current coordinates require a proof across every reachable level,
   * including its actual storage conversion, and all possible border taps. */
  bool uniform = base->has_uniform;
  for (size_t i = 0; uniform && i < texture.level_count; i++) {
    const qa_texture_storage *level = &texture.levels[i];
    uniform = level->has_uniform && level->uniform.r == base->uniform.r && level->uniform.g == base->uniform.g && level->uniform.b == base->uniform.b
      && (!alpha || level->uniform.a == base->uniform.a);
  }
  if (uniform && (streq(texture.wrap, "repeat") || !texture.magnify_linear
      || (f32(texture.border_color.x) == base->uniform.r && f32(texture.border_color.y) == base->uniform.g
        && f32(texture.border_color.z) == base->uniform.b && (!alpha || f32(texture.border_color.w) == base->uniform.a)))) {
    qa_bound_texture out = incomplete_texture();
    out.kind = QA_BOUND_TEXTURE_CONSTANT; out.internal_format = texture.internal_format; out.sample = base->uniform;
    return out;
  }
  /* OpenGL 1.2.1 section 2.8 leaves enabled current attributes indeterminate after
   * DrawElements. A coordinate-dependent read has no source-defined sample. */
  qa_range_error("CPU immediate texture unit %d has source-indeterminate coordinates and coordinate-dependent texels", unit);
}
void qa_software_renderer_begin_source_iterator(qa_software_renderer *r, bool set_arrays_once, const qa_source_stage_cell *scratch, size_t scratch_count) {
  assert_open(r); rimpl *p = R(r);
  refresh_source_coordinates(p, scratch, scratch_count);
  p->generic_arrays_once = set_arrays_once;
  p->color_client_enabled = true;
  if (p->current_unit == 0) p->primary_client_enabled = true; else p->secondary_client_enabled = true;
  if (set_arrays_once) {
    client_coords *c = &p->client_tex_coords[p->current_unit];
    if (scratch_count == 0) { if (!c->present) c->count = 0; c->present = true; c->origin = "svars0"; }
    else set_client(p, p->current_unit, "svars0", scratch_coords(scratch, scratch_count, "svars0"), scratch_count);
  }
}
typedef struct source_prep {
  qa_software_renderer *r; owned_batch owned; qa_texture_resolver resolver;
  const qa_draw_batch *source_batch; /* live batch for the TS environment getter */
  bool has_source; uint32_t state_bits; const char *source_kind;
  qa_source_stage_cell *scratch; size_t scratch_count;
  bool paired, state_before_binding;
  phase phase; int next_unit; int pending_unit; /* -1 = null */
} source_prep;
static size_t batch_vertex_count(const qa_draw_batch *b) { return b->texturing_is_pair ? b->pair_vertex_count : b->vertex_count; }
static qa_vec2 batch_tex_coord(const qa_draw_batch *b, size_t i) { return b->texturing_is_pair ? b->pair_vertices[i].vertex.tex_coord : b->vertices[i].tex_coord; }
static void source_coordinates(source_prep *q, bool secondary) {
  rimpl *p = R(q->r); const char *kind = q->source_kind; const qa_draw_batch *batch = &q->owned.batch;
  const char *origin = kind == NULL ? "direct"
    : secondary ? (streq(kind, "lightmapped-pair") ? "raw1" : "svars1")
    : (streq(kind, "lightmapped-pair") || streq(kind, "vertex-lit")) ? "raw0" : streq(kind, "dlight") ? "local" : "svars0";
  size_t n = batch_vertex_count(batch);
  if (n == 0) {
    client_coords *c = &p->client_tex_coords[p->current_unit];
    if (!c->present) c->count = 0;
    c->present = true; c->origin = origin;
    return;
  }
  qa_vec2 *values;
  if (streq(origin, "raw0") || streq(origin, "raw1")) {
    if (q->scratch == NULL) qa_range_error("CPU raw source coordinates require stage scratch");
    values = scratch_coords(q->scratch, q->scratch_count, origin);
    n = q->scratch_count;
  } else {
    if (secondary && !batch->texturing_is_pair) qa_range_error("CPU secondary client coordinates require a paired batch");
    values = (qa_vec2 *)xmalloc(n * sizeof *values);
    for (size_t i = 0; i < n; i++) values[i] = secondary ? batch->pair_vertices[i].tex_coord2 : batch_tex_coord(batch, i);
  }
  set_client(p, p->current_unit, origin, values, n);
}
static void source_apply_state(source_prep *q) {
  rimpl *p = R(q->r);
  /* GL_State leaves physical cull, depth range and polygon offset to their
   * explicit surface operations. Single stages reach it after binding. */
  if (!q->has_source) apply_diagnostic_state(p, q->owned.batch.state);
  else apply_source_state(p, q->state_bits);
  /* DrawMultitextured applies this driver workaround without updating GL_State's cache. */
  if (streq(q->source_kind, "generic-pair") && p->portal_view) p->polygon_line = false;
  if (q->owned.batch.primitive_is_lines) p->line_width = q->owned.batch.line_width;
}
static void source_begin(void *ctx) {
  source_prep *q = (source_prep *)ctx; assert_open(q->r); rimpl *p = R(q->r); const char *kind = q->source_kind;
  if (q->phase != PHASE_PREPARED) qa_range_error("CPU prepared draw has already begun");
  if (q->scratch != NULL) refresh_source_coordinates(p, q->scratch, q->scratch_count);
  if ((!streq(kind, "generic-single") && !streq(kind, "generic-pair")) || !p->generic_arrays_once) p->color_client_enabled = true;
  if (streq(kind, "vertex-lit") || streq(kind, "dlight") || streq(kind, "fog")) {
    if (p->current_unit == 0) p->primary_client_enabled = true; else p->secondary_client_enabled = true;
  }
  if (q->state_before_binding) source_apply_state(q);
  q->phase = PHASE_BEGUN;
}
static void source_prepare_texture(void *ctx, double unit_value) {
  source_prep *q = (source_prep *)ctx; assert_open(q->r); rimpl *p = R(q->r); const char *kind = q->source_kind;
  int unit = (int)unit_value; const qa_draw_batch *batch = &q->owned.batch;
  if (q->phase != PHASE_BEGUN || q->pending_unit != -1 || unit_value != q->next_unit || (unit == 1 && !batch->texturing_is_pair))
    qa_range_error("CPU prepared texture slot order is invalid");
  if (unit == 1 && streq(kind, "lightmapped-pair")) source_coordinates(q, false);
  if (kind == NULL || batch->texturing_is_pair) p->current_unit = unit;
  if (unit == 0) {
    if (kind == NULL) p->primary_enabled = true;
    if (kind == NULL || streq(kind, "lightmapped-pair")) p->primary_client_enabled = true;
    if (!streq(kind, "lightmapped-pair") && (!streq(kind, "generic-single") || !p->generic_arrays_once)) source_coordinates(q, false);
  } else if (batch->texturing_is_pair) {
    p->secondary_enabled = true;
    if (!streq(kind, "lightmapped-pair")) p->secondary_client_enabled = true;
    const char *environment = q->source_batch->second_texture.environment;
    if (!streq(environment, "modulate") && !streq(environment, "add") && !streq(environment, "replace")) qa_range_error("CPU texture environment is invalid");
    p->secondary_environment = environment;
    if (!streq(kind, "lightmapped-pair")) source_coordinates(q, true);
  }
  q->pending_unit = unit;
}
static void source_apply_texture(void *ctx, double unit, qa_texture_binding op) {
  source_prep *q = (source_prep *)ctx; assert_open(q->r); rimpl *p = R(q->r);
  if (q->source_kind == NULL) source_prepare_texture(ctx, unit);
  if (q->phase != PHASE_BEGUN || q->pending_unit != (int)unit) qa_range_error("CPU prepared texture slot has not begun");
  qa_cpu_images_bind(&p->images, p->current_unit, qa_texture_resolver_resolve(&q->resolver, op));
  q->pending_unit = -1;
  q->next_unit++;
}
static void source_finish_textures(void *ctx) {
  source_prep *q = (source_prep *)ctx; assert_open(q->r); rimpl *p = R(q->r);
  if (q->phase != PHASE_BEGUN || q->pending_unit != -1 || q->next_unit != (q->owned.batch.texturing_is_pair ? 2 : 1))
    qa_range_error("CPU prepared draw has unapplied texture slots");
  if (!q->state_before_binding) source_apply_state(q);
  if (streq(q->source_kind, "lightmapped-pair")) {
    if (p->current_unit == 0) p->primary_client_enabled = true; else p->secondary_client_enabled = true;
    source_coordinates(q, true);
  }
  q->phase = PHASE_TEXTURED;
}
typedef struct strip_ctx {
  qa_software_renderer *r; const qa_draw_batch *batch; const qa_cpu_vertex *vertices; size_t vertex_count;
  bool discrete; qa_bound_texture texture, secondary;
  bool has_first, has_second, even; qa_cpu_vertex first, second;
  bool has_invariants; batch_constants invariants;
} strip_ctx;
static void strip_begin(void *ctx) { strip_ctx *s = (strip_ctx *)ctx; s->has_first = false; s->has_second = false; s->even = true; }
static void strip_end(void *ctx) { (void)ctx; }
static void strip_element(void *ctx, double index) {
  strip_ctx *s = (strip_ctx *)ctx; rimpl *p = R(s->r);
  if (!(index >= 0) || index >= (double)s->vertex_count || index != floor(index)) qa_range_error("CPU source strip vertex is missing");
  const qa_cpu_vertex *vertex = &s->vertices[(size_t)index];
  if (s->discrete || p->color_client_enabled) { p->current_color.known = true; p->current_color.value = vertex->color; }
  if (s->discrete && p->current_unit != 0) qa_range_error("Unsupported source R_ArrayElementDiscrete multitexture targets 0 and 1");
  if (s->discrete || p->primary_client_enabled) {
    p->current_tex_coords[0].known = true; p->current_tex_coords[0].value.x = f32(vertex->tex_coord.x); p->current_tex_coords[0].value.y = f32(vertex->tex_coord.y);
  }
  if (!s->discrete && p->secondary_client_enabled) {
    p->current_tex_coords[1].known = true; p->current_tex_coords[1].value.x = f32(vertex->tex_coord2.x); p->current_tex_coords[1].value.y = f32(vertex->tex_coord2.y);
  }
  if (!s->has_first) { s->first = *vertex; s->has_first = true; }
  else if (!s->has_second) { s->second = *vertex; s->has_second = true; }
  else {
    /* Strip parity emits (a,b,c), then (c,b,d), then (c,d,e): exactly the source
     * triples, including their third/provoking vertex. */
    qa_bound_texture primary = s->discrete || p->primary_client_enabled ? s->texture : immediate_texture(p, 0);
    qa_bound_texture secondary = !s->discrete && p->secondary_client_enabled ? s->secondary : immediate_texture(p, 1);
    if (!s->has_invariants) { batch_setup_constants(p, s->batch, &primary, &secondary, &s->invariants); s->has_invariants = true; }
    draw_triangle(s->r, s->even ? &s->first : &s->second, s->even ? &s->second : &s->first, vertex, s->batch, &primary, &secondary, &s->invariants);
    s->first = s->second; s->second = *vertex; s->even = !s->even;
  }
}
static void source_draw(void *ctx, double primitives) {
  source_prep *q = (source_prep *)ctx; qa_software_renderer *r = q->r; assert_open(r); rimpl *p = R(r);
  const qa_draw_batch *batch = &q->owned.batch; bool has_source = q->source_kind != NULL;
  if (q->phase != (!has_source ? PHASE_BEGUN : PHASE_TEXTURED) || q->pending_unit != -1 || q->next_unit != (batch->texturing_is_pair ? 2 : 1))
    qa_range_error("CPU prepared draw has unapplied texture slots");
  qa_source_primitive_mode mode = qa_source_primitive_mode_for(primitives, false);
  bool discrete = streq(mode, "discrete-strips"), none = streq(mode, "none");
  qa_render_state state = raster_state(p);
  qa_cpu_vertex *source_vertices = NULL; qa_render_vertex *single = NULL; qa_multitexture_vertex *pair = NULL;
  qa_draw_batch draw = *batch;
  size_t n = batch_vertex_count(batch);
  if (has_source) {
    if (!none && !discrete && batch->indices.count != 0) validate_client_coordinates(p, &batch->indices);
    bool has_retained = false; qa_vec4 retained_color; memset(&retained_color, 0, sizeof retained_color);
    if (!discrete && !p->color_client_enabled && !none && batch->indices.count != 0) {
      if (!p->current_color.known) qa_range_error("CPU source draw color is source-indeterminate");
      has_retained = true; retained_color = p->current_color.value;
    }
    const client_coords *primary = !discrete && p->primary_client_enabled && p->client_tex_coords[0].present ? &p->client_tex_coords[0] : NULL;
    const client_coords *secondary = !discrete && p->secondary_client_enabled && p->client_tex_coords[1].present ? &p->client_tex_coords[1] : NULL;
    source_vertices = (qa_cpu_vertex *)xmalloc(n * sizeof *source_vertices);
    for (size_t i = 0; i < n; i++) {
      if (q->scratch == NULL || i >= q->scratch_count) qa_range_error("CPU source scratch vertex is missing");
      const qa_source_stage_cell *cell = &q->scratch[i];
      const qa_render_vertex *vertex = batch->texturing_is_pair ? &batch->pair_vertices[i].vertex : &batch->vertices[i];
      qa_cpu_world_attributes attributes = qa_cpu_world_attributes_for(batch->lighting, (double)i);
      qa_cpu_vertex *o = &source_vertices[i];
      o->position = vertex->position; o->world_position = attributes.world_position; o->world_normal = attributes.world_normal;
      /* Only indexed vertices consume retained array slots. Unreferenced
       * allocation tails keep their unused coordinates. */
      if (discrete) {
        o->color.x = qa_cpu_byte(cell->color.x) / 255; o->color.y = qa_cpu_byte(cell->color.y) / 255; o->color.z = qa_cpu_byte(cell->color.z) / 255; o->color.w = qa_cpu_byte(cell->color.w) / 255;
        o->tex_coord.x = f32(cell->tex_coord.x); o->tex_coord.y = f32(cell->tex_coord.y);
      } else {
        o->color = has_retained ? retained_color : vertex->color;
        o->tex_coord = primary != NULL && i < primary->count ? primary->values[i] : vertex->tex_coord;
      }
      o->tex_coord2 = secondary != NULL && i < secondary->count ? secondary->values[i] : cell->tex_coord2;
    }
    memset(&draw, 0, sizeof draw);
    draw.indices = batch->indices; draw.lighting = batch->lighting; draw.fog = batch->fog; draw.state = state;
    draw.texture.kind = QA_TEXTURE_BINDING_RETAIN_CURRENT;
    if (p->secondary_enabled) {
      pair = (qa_multitexture_vertex *)xmalloc(n * sizeof *pair);
      for (size_t i = 0; i < n; i++) { pair[i].vertex.position = source_vertices[i].position; pair[i].vertex.color = source_vertices[i].color; pair[i].vertex.tex_coord = source_vertices[i].tex_coord; pair[i].tex_coord2 = source_vertices[i].tex_coord2; }
      draw.texturing_is_pair = true; draw.pair_vertices = pair; draw.pair_vertex_count = n;
      draw.second_texture.binding.kind = QA_TEXTURE_BINDING_RETAIN_CURRENT; draw.second_texture.environment = p->secondary_environment;
    } else {
      single = (qa_render_vertex *)xmalloc(n * sizeof *single);
      for (size_t i = 0; i < n; i++) { single[i].position = source_vertices[i].position; single[i].color = source_vertices[i].color; single[i].tex_coord = source_vertices[i].tex_coord; }
      draw.vertices = single; draw.vertex_count = n;
    }
  } else { draw.state = state; q->owned.batch.state = state; }
  if (draw.indices.count != 0 && !none) {
    if (streq(mode, "elements")) {
      qa_bound_texture texture, secondary;
      if (!has_source) { texture = enabled_texture(p, 0); secondary = enabled_texture(p, 1); }
      else {
        texture = p->primary_client_enabled ? enabled_texture(p, 0) : immediate_texture(p, 0);
        secondary = p->secondary_client_enabled ? enabled_texture(p, 1) : immediate_texture(p, 1);
      }
      draw_batch(r, &draw, &texture, &secondary);
      if (!has_source || p->color_client_enabled) p->current_color.known = false;
      if (!has_source || p->primary_client_enabled) p->current_tex_coords[0].known = false;
      if (!has_source ? draw.texturing_is_pair : p->secondary_client_enabled) p->current_tex_coords[1].known = false;
    } else {
      if (source_vertices == NULL) qa_range_error("CPU source strips require stage scratch");
      strip_ctx *s = (strip_ctx *)xmalloc(sizeof *s);
      s->r = r; s->batch = &draw; s->vertices = source_vertices; s->vertex_count = n; s->discrete = discrete;
      s->texture = enabled_texture(p, 0); s->secondary = enabled_texture(p, 1);
      s->has_first = false; s->has_second = false; s->even = true; s->has_invariants = false;
      qa_source_strip_emitter emit; emit.ctx = s; emit.begin = strip_begin; emit.element = strip_element; emit.end = strip_end;
      qa_emit_source_triangle_strips(draw.indices.values, draw.indices.count, emit);
      free(s);
    }
  }
  free(source_vertices); free(single); free(pair);
  q->phase = PHASE_DRAWN;
}
static void source_cleanup(void *ctx) {
  source_prep *q = (source_prep *)ctx; assert_open(q->r); rimpl *p = R(q->r); const char *kind = q->source_kind;
  if (q->phase != PHASE_DRAWN) qa_range_error("CPU prepared draw has not completed");
  if (q->paired) {
    if (kind == NULL) p->current_unit = 1;
    if (p->current_unit == 0) {
      p->primary_enabled = false;
      if (!streq(kind, "generic-pair")) p->primary_client_enabled = false;
    } else {
      p->secondary_enabled = false;
      if (!streq(kind, "generic-pair")) p->secondary_client_enabled = false;
    }
    p->current_unit = 0;
  }
  if (kind == NULL && q->owned.batch.state.polygon_offset != NULL) p->retained_state.polygon_offset = NULL;
  if (q->owned.batch.primitive_is_lines) p->line_width = 1;
  q->phase = PHASE_CLEANED;
  qa_texture_resolver_destroy(&q->resolver); free_owned(&q->owned); free(q->scratch); free(q);
}
static bool float32_finite(double v) { return isfinite(f32(v)); }
qa_prepared_source_draw qa_software_renderer_prepare_source_geometry(qa_software_renderer *r, const qa_source_stage_data *stage) {
  assert_open(r); rimpl *p = R(r);
  source_prep *q = (source_prep *)xmalloc(sizeof *q);
  memset(q, 0, sizeof *q);
  q->r = r; q->source_batch = &stage->batch; q->has_source = true; q->state_bits = qa_js_to_uint32(stage->state_bits); /* TS applies ToInt32 in each & / ^ */ q->source_kind = stage->kind;
  if (!qa_texture_resolver_init(&q->resolver, resolver_apply, r)) qa_range_error("CPU renderer allocation failed");
  /* snapshotSourceBatch: triangles with the retained state. */
  qa_draw_batch input = stage->batch;
  input.primitive_is_lines = false; input.space = NULL; input.texture_effect = NULL;
  own_batch(&q->owned, input, p->retained_state, NULL);
  validate_batch(p, &q->owned.batch);
  q->paired = q->owned.batch.texturing_is_pair;
  q->scratch_count = stage->scratch_count;
  q->scratch = (qa_source_stage_cell *)xmalloc(stage->scratch_count * sizeof *q->scratch);
  if (stage->scratch_count) memcpy(q->scratch, stage->scratch, stage->scratch_count * sizeof *q->scratch);
  if (q->scratch_count != batch_vertex_count(&q->owned.batch)) qa_range_error("CPU source scratch must match published vertices");
  for (size_t i = 0; i < q->scratch_count; i++) {
    const qa_source_stage_cell *cell = &q->scratch[i];
    if (!finite4(cell->color) || !float32_finite(cell->tex_coord.x) || !float32_finite(cell->tex_coord.y) || !float32_finite(cell->tex_coord2.x) || !float32_finite(cell->tex_coord2.y))
      qa_range_error("CPU source scratch attributes must be finite float32 values");
  }
  q->state_before_binding = streq(q->source_kind, "generic-pair") || streq(q->source_kind, "lightmapped-pair");
  q->phase = PHASE_PREPARED; q->next_unit = 0; q->pending_unit = -1;
  qa_prepared_source_draw o; memset(&o, 0, sizeof o);
  o.ctx = q; o.begin = source_begin; o.prepare_texture = source_prepare_texture; o.apply_texture = source_apply_texture;
  o.finish_textures = source_finish_textures; o.draw = source_draw; o.cleanup = source_cleanup;
  return o;
}

/* Backend adapter (contracts qa_renderer_backend). */
static void be_image(void *ctx,qa_image_resource_operation op){ qa_software_renderer_apply_image_resource((qa_software_renderer*)ctx,op); }
static void be_buffer(void *ctx,qa_renderer_draw_buffer b,bool c){ qa_software_renderer_select_draw_buffer((qa_software_renderer*)ctx,b,c); }
static void be_overdraw(void *ctx,bool e){ qa_software_renderer_set_overdraw_measurement((qa_software_renderer*)ctx,e); }
static void be_read_stencil(void *ctx,uint8_t *d,size_t n){ qa_software_renderer_read_stencil_overdraw((qa_software_renderer*)ctx,d,n); }
static double be_depth(void *ctx,double x,double y){ return qa_software_renderer_read_depth_pixel((qa_software_renderer*)ctx,x,y); }
static void be_view(void *ctx,qa_render_view_state v){ qa_software_renderer_begin_view((qa_software_renderer*)ctx,v); }
static void be_opacity(void *ctx,double o,void (*draw)(void *),void *dctx){ qa_software_renderer_with_object_opacity((qa_software_renderer*)ctx,o,draw,dctx); }
static void be_immediate(void *ctx,qa_render_operation op){ qa_software_renderer_draw_immediate((qa_software_renderer*)ctx,op); }
static qa_prepared_backend_draw be_prepare(void *ctx,qa_draw_batch b){ return qa_software_renderer_prepare_geometry((qa_software_renderer*)ctx,b); }
static void be_clear(void *ctx){ qa_software_renderer_clear_color_buffer((qa_software_renderer*)ctx); }
static void be_show(void *ctx,qa_renderer_image im,qa_rect rc,bool pr){ qa_software_renderer_draw_show_image((qa_software_renderer*)ctx,im,rc,pr); }
static void be_finish(void *ctx){ qa_software_renderer_finish((qa_software_renderer*)ctx); }
static void be_close(void *ctx){ qa_software_renderer_close((qa_software_renderer*)ctx); }
qa_renderer_backend qa_software_renderer_backend(qa_software_renderer *r){ qa_renderer_backend b; memset(&b,0,sizeof b);
  b.ctx=r;b.owner=r->owner;b.width=r->width;b.height=r->height;b.stencil_bits=r->stencil_bits;
  b.apply_image_resource=be_image;b.select_draw_buffer=be_buffer;b.set_overdraw_measurement=be_overdraw;b.read_stencil_overdraw=be_read_stencil;
  b.read_depth_pixel=be_depth;b.begin_view=be_view;b.with_object_opacity=be_opacity;b.draw_immediate=be_immediate;b.prepare_geometry=be_prepare;
  b.clear_color_buffer=be_clear;b.draw_show_image=be_show;b.finish=be_finish;b.close=be_close; return b; }
