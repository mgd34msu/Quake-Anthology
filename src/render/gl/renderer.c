/* Port of src/render/gl/renderer.ts. Q3 tr_backend.c, tr_shade.c, tr_sky.c and
 * tr_shadows.c backend operations. Copyright (C) 1999-2005 Id Software, Inc.
 * GPL-2.0-or-later. */
#include "qa/render/gl/renderer.h"
#include "qa/render/commands/dynamic_texture.h"
#include "qa/render/gl/buffers.h"
#include "qa/render/gl/depth_atlas.h"
#include "qa/render/gl/fog.h"
#include "qa/render/gl/object_opacity.h"
#include "qa/render/gl/output_gamma.h"
#include "qa/render/gl/programs.h"
#include "qa/render/gl/textures.h"
#include "qa/render/output_gamma.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct gprep gprep;
typedef struct gimpl {
  qa_gl_render_context window; qa_gl gl; qa_gl_precision precision;
  qa_gl_stage_program program;
  qa_gl_textures textures;
  const qa_gl_geometry_arrays *active_arrays;
  qa_gl_geometry_buffer *idle_geometry;
  qa_depth_atlas_target depth_atlas; bool has_depth_atlas;
  qa_q2_fog_pass fog; bool has_fog;
  qa_gl_object_opacity object_opacity; bool has_object_opacity;
  bool opacity_active;
  qa_gl_output_gamma output_gamma; bool has_output_gamma;
  double gamma; bool gamma_finished;
  uint32_t draw_buffer;
  const char *alpha_test;
  int texture_unit; /* -1 = TS null */
  bool matrices_identity;
  bool has_vp; float view_projection[16];
  const char *current_cull; /* NULL = TS undefined */
  bool has_range; double range_near, range_far;
  int offset_enabled; /* -1 = TS undefined */
  double offset_factor, offset_units;
  bool closed;
  gprep *retired_preps;
  void *active_key; /* context in the activeContexts set */
} gimpl;
static gimpl *G(qa_gl_renderer *r) { return (gimpl *)r->impl; }

/* TS activeContexts WeakSet, keyed by the render context's ctx. */
static pthread_mutex_t active_lock = PTHREAD_MUTEX_INITIALIZER;
static void **active_contexts; static size_t active_count, active_cap;
static bool active_add(void *key) {
  bool added = false;
  pthread_mutex_lock(&active_lock);
  bool present = false;
  for (size_t i = 0; i < active_count; i++) if (active_contexts[i] == key) present = true;
  if (!present) {
    if (active_count == active_cap) {
      size_t cap = active_cap ? active_cap * 2 : 4;
      void **grown = (void **)realloc(active_contexts, cap * sizeof *grown);
      if (grown == NULL) { pthread_mutex_unlock(&active_lock); qa_range_error("OpenGL renderer allocation failed"); }
      active_contexts = grown; active_cap = cap;
    }
    active_contexts[active_count++] = key; added = true;
  }
  pthread_mutex_unlock(&active_lock);
  return added;
}
static void active_remove(void *key) {
  pthread_mutex_lock(&active_lock);
  for (size_t i = 0; i < active_count; i++) if (active_contexts[i] == key) { active_contexts[i] = active_contexts[--active_count]; break; }
  pthread_mutex_unlock(&active_lock);
}

static bool finite32(double value) { return isfinite((float)value); }
static bool is_int32(double v) { return v == floor(v) && v >= -2147483648.0 && v <= 2147483647.0; }
static double get_integer(gimpl *p, uint32_t name) {
  int32_t value = 0; p->gl.glGetIntegerv(name, &value);
  if (value < 0) qa_range_error("OpenGL returned an invalid capability");
  return value;
}
double qa_gl_renderer_width(qa_gl_renderer *r) { gimpl *p = G(r); double w = 0, h = 0; p->window.drawable_size(p->window.ctx, &w, &h); return w; }
double qa_gl_renderer_height(qa_gl_renderer *r) { gimpl *p = G(r); double w = 0, h = 0; p->window.drawable_size(p->window.ctx, &w, &h); return h; }

static void invalidate_state(gimpl *p) {
  p->texture_unit = -1; p->matrices_identity = false; p->current_cull = NULL;
  p->has_range = false; p->offset_enabled = -1;
}
static void make_current_body(void *ctx) { gimpl *p = (gimpl *)ctx; p->window.make_current(p->window.ctx); }
static void opened(qa_gl_renderer *r) {
  gimpl *p = G(r);
  if (p == NULL || p->closed) qa_range_error("OpenGL renderer is closed");
  char message[1024];
  if (!qa_range_error_catch(make_current_body, p, message, sizeof message)) { invalidate_state(p); qa_range_error("%s", message); }
}
static void select_texture(gimpl *p, int unit) {
  if (p->texture_unit == unit) return;
  p->texture_unit = -1;
  p->gl.glActiveTexture(0x84c0u + (uint32_t)unit);
  p->gl.glClientActiveTexture(0x84c0u + (uint32_t)unit);
  p->texture_unit = unit;
}
static void identity_matrices(gimpl *p) {
  if (p->matrices_identity) return;
  p->gl.glMatrixMode(0x1700); p->gl.glLoadIdentity();
  p->gl.glMatrixMode(0x1701); p->gl.glLoadIdentity();
  p->matrices_identity = true;
}
/* World-space batches are transformed by the GPU with the view's matrix. */
static void world_matrices(gimpl *p) {
  if (!p->has_vp) qa_range_error("OpenGL world-space batch requires a view projection");
  p->gl.glMatrixMode(0x1701); p->gl.glLoadMatrixf(p->view_projection);
  p->gl.glMatrixMode(0x1700); p->gl.glLoadIdentity();
  p->matrices_identity = false;
}
static void disable_arrays(gimpl *p) {
  p->gl.glDisableClientState(0x8074);
  p->gl.glDisableClientState(0x8076);
  for (int unit = 3; unit >= 0; unit--) { select_texture(p, unit); p->gl.glDisableClientState(0x8078); }
  p->active_arrays = NULL;
}
static void draw_target(qa_gl_renderer *r, bool rendering) {
  gimpl *p = G(r);
  if (p->has_object_opacity && qa_gl_object_opacity_bind(&p->object_opacity)) return;
  if (p->has_output_gamma) {
    double width = qa_gl_renderer_width(r), height = qa_gl_renderer_height(r);
    if (width > r->max_texture_size || height > r->max_texture_size) qa_range_error("OpenGL gamma target exceeds maximum texture size");
    bool changed = qa_gl_output_gamma_bind(&p->output_gamma, p->draw_buffer, width, height);
    if (rendering || changed) p->gamma_finished = false;
  }
}

typedef struct init_scope { qa_gl_renderer *r; gimpl *p; qa_renderer_resource_owner owner; bool has_program; } init_scope;
static void init_body(void *ctx) {
  init_scope *s = (init_scope *)ctx; qa_gl_renderer *r = s->r; gimpl *p = s->p; qa_gl *gl = &p->gl;
  r->driver.vendor = gl->glGetString(0x1f00); r->driver.renderer = gl->glGetString(0x1f01);
  r->driver.version = gl->glGetString(0x1f02); r->driver.shading_language = gl->glGetString(0x8b8c);
  r->stencil_bits = get_integer(p, 0xd57);
  r->depth_bits = get_integer(p, 0xd56);
  double red = get_integer(p, 0xd52), green = get_integer(p, 0xd53), blue = get_integer(p, 0xd54);
  r->color_bits = red + green + blue;
  r->alpha_bits = get_integer(p, 0xd55);
  r->max_texture_size = get_integer(p, 0xd33);
  double coords = get_integer(p, 0x8872), units = get_integer(p, 0x8871);
  r->texture_units = coords < units ? coords : units;
  r->stereo_enabled = get_integer(p, 0xc33) != 0;
  if (r->texture_units < 4 || r->max_texture_size < 1 || r->depth_bits < 1)
    qa_range_error("OpenGL renderer requires four texture coordinate units and a depth framebuffer");
  p->precision = (qa_gl_precision){red, green, blue, r->alpha_bits, r->depth_bits, r->stencil_bits, r->color_bits};
  if (!qa_gl_stage_program_init(&p->program, &p->window.gl_context)) qa_range_error("OpenGL stage program allocation failed");
  s->has_program = true;
  if (!qa_gl_textures_init(&p->textures, gl, s->owner, r->max_texture_size)) qa_range_error("OpenGL texture allocation failed");
  identity_matrices(p);
  gl->glFrontFace(0x901);
  gl->glShadeModel(0x1d01);
  gl->glPolygonMode(0x408, 0x1b02);
  gl->glDisable(0xb50); gl->glDisable(0xb60); gl->glDisable(0xbc0);
  gl->glDisable(0xb90); gl->glDisable(0x3000); gl->glDisable(0x8037);
  gl->glColorMask(1, 1, 1, 1);
  gl->glDepthMask(1);
  gl->glClearStencil(0);
  gl->glStencilMask(0xffffffffu);
  gl->glColor4f(1, 1, 1, 1);
  disable_arrays(p);
}
bool qa_gl_renderer_init(qa_gl_renderer *r, qa_gl_render_context window, qa_renderer_resource_owner owner) {
  if (!active_add(window.ctx)) qa_range_error("SDL context already has an active OpenGL renderer");
  gimpl *p = (gimpl *)calloc(1, sizeof *p);
  if (p == NULL) { active_remove(window.ctx); return false; }
  r->impl = p; r->owner = owner; p->window = window; p->active_key = window.ctx;
  p->draw_buffer = 0x405; p->alpha_test = "none"; p->gamma = 1; p->texture_unit = -1; p->offset_enabled = -1;
  init_scope scope = {r, p, owner, false};
  char message[1024];
  /* TS constructor catch: release what opened, forget the context, rethrow. */
  window.set_rendering_enabled(window.ctx, true);
  window.make_current(window.ctx);
  if (!qa_load_gl(&p->window.gl_context, &p->gl)) { free(p); r->impl = NULL; active_remove(window.ctx); return false; }
  if (!qa_range_error_catch(init_body, &scope, message, sizeof message)) {
    if (scope.has_program) qa_gl_stage_program_close(&p->program);
    qa_gl_close(&p->gl); free(p); r->impl = NULL; active_remove(window.ctx);
    qa_range_error("%s", message);
  }
  return true;
}
void qa_gl_renderer_set_output_gamma(qa_gl_renderer *r, double gamma) {
  uint8_t table[256]; bool has_table = qa_output_gamma_table(gamma, table);
  opened(r); gimpl *p = G(r);
  if (gamma == p->gamma) return;
  if (p->active_arrays != NULL) qa_range_error("OpenGL gamma cannot interrupt a prepared draw");
  invalidate_state(p);
  if (!has_table) {
    if (p->has_output_gamma) {
      qa_gl_output_gamma_restore(&p->output_gamma, p->draw_buffer);
      qa_gl_output_gamma_close(&p->output_gamma);
      p->has_output_gamma = false;
    }
  } else if (!p->has_output_gamma) {
    qa_gl_output_gamma pass;
    if (!qa_gl_output_gamma_init(&pass, &p->window.gl_context, &p->gl, p->precision, table)) qa_range_error("OpenGL gamma pass allocation failed");
    qa_gl_output_gamma * gamma2 = &pass;
    double buffer = p->draw_buffer;
    double width = qa_gl_renderer_width(r);
    double height = qa_gl_renderer_height(r);
    (void)qa_gl_output_gamma_bind(gamma2, buffer, width, height);
    p->output_gamma = pass; p->has_output_gamma = true;
  } else qa_gl_output_gamma_update(&p->output_gamma, table);
  p->gamma = gamma; p->gamma_finished = false;
}
static uint32_t blend_factor(const char *f) {
  if (!strcmp(f, "zero")) return 0;
  if (!strcmp(f, "one")) return 1;
  if (!strcmp(f, "src-color")) return 0x300;
  if (!strcmp(f, "one-minus-src-color")) return 0x301;
  if (!strcmp(f, "src-alpha")) return 0x302;
  if (!strcmp(f, "one-minus-src-alpha")) return 0x303;
  if (!strcmp(f, "dst-alpha")) return 0x304;
  if (!strcmp(f, "one-minus-dst-alpha")) return 0x305;
  if (!strcmp(f, "dst-color")) return 0x306;
  if (!strcmp(f, "one-minus-dst-color")) return 0x307;
  return 0x308;
}
static void apply_cull(gimpl *p, const char *cull) {
  if (p->current_cull != NULL && !strcmp(p->current_cull, cull)) return;
  p->current_cull = NULL;
  if (!strcmp(cull, "none")) p->gl.glDisable(0xb44);
  else { p->gl.glEnable(0xb44); p->gl.glCullFace(!strcmp(cull, "back") ? 0x405 : 0x404); }
  p->current_cull = !strcmp(cull, "none") ? "none" : !strcmp(cull, "back") ? "back" : "front";
}
static void apply_offset(gimpl *p, const qa_polygon_offset *value) {
  if (value == NULL) {
    if (p->offset_enabled == 0) return;
    p->offset_enabled = -1;
    p->gl.glDisable(0x8037);
    p->offset_enabled = 0;
  } else {
    if (!finite32(value->factor) || !finite32(value->units)) qa_range_error("OpenGL polygon offset must be finite");
    if (p->offset_enabled == 1 && p->offset_factor == value->factor && p->offset_units == value->units) return;
    p->offset_enabled = -1;
    p->gl.glEnable(0x8037);
    p->gl.glPolygonOffset((float)value->factor, (float)value->units);
    p->offset_factor = value->factor; p->offset_units = value->units;
    p->offset_enabled = 1;
  }
}
static void apply_range(gimpl *p, const double range[2]) {
  if (!isfinite(range[0]) || !isfinite(range[1])) qa_range_error("OpenGL depth range must be finite");
  if (p->has_range && p->range_near == range[0] && p->range_far == range[1]) return;
  p->has_range = false;
  p->gl.glDepthRange(range[0], range[1]);
  p->range_near = range[0]; p->range_far = range[1]; p->has_range = true;
}
static void apply_state(gimpl *p, const qa_render_state *state) {
  qa_gl *gl = &p->gl;
  if (!strcmp(state->blend.destination, "src-alpha-saturate")) qa_range_error("OpenGL destination blend cannot use source alpha saturate");
  gl->glEnable(0xb71);
  gl->glDepthFunc(!strcmp(state->depth_test, "less-equal") ? 0x203 : !strcmp(state->depth_test, "equal") ? 0x202 : 0x207);
  gl->glDepthMask(state->depth_write ? 1 : 0);
  if (!strcmp(state->blend.source, "one") && !strcmp(state->blend.destination, "zero")) gl->glDisable(0xbe2);
  else { gl->glEnable(0xbe2); gl->glBlendFunc(blend_factor(state->blend.source), blend_factor(state->blend.destination)); }
  gl->glDisable(0xbc0);
  p->alpha_test = state->alpha_test;
  apply_cull(p, state->cull);
  apply_range(p, state->depth_range);
  apply_offset(p, state->polygon_offset);
}
typedef struct image_scope { gimpl *p; qa_image_resource_operation op; } image_scope;
static void image_body(void *ctx) { image_scope *s = (image_scope *)ctx; qa_gl_textures_apply(&s->p->textures, s->op); }
void qa_gl_renderer_apply_image_resource(qa_gl_renderer *r, qa_image_resource_operation op) {
  opened(r); gimpl *p = G(r);
  select_texture(p, 0);
  int32_t previous = 0; p->gl.glGetIntegerv(0x8069, &previous);
  bool is_release = op.kind == QA_IMAGE_OP_RELEASE;
  uint32_t released = is_release ? qa_gl_textures_registered(&p->textures, op.image).name : 0;
  image_scope scope = {p, op};
  char message[1024];
  bool ok = qa_range_error_catch(image_body, &scope, message, sizeof message);
  p->gl.glBindTexture(0xde1, is_release && (uint32_t)previous == released ? 0 : (uint32_t)previous);
  if (!ok) qa_range_error("%s", message);
}
void qa_gl_renderer_select_draw_buffer(qa_gl_renderer *r, const char *buffer, bool clear) {
  opened(r); gimpl *p = G(r);
  if (!strcmp(buffer, "back-right") && !r->stereo_enabled) qa_range_error("OpenGL stereo draw buffer requires a stereo context");
  p->draw_buffer = !strcmp(buffer, "front") ? 0x404 : !strcmp(buffer, "back-left") ? 0x402 : !strcmp(buffer, "back-right") ? 0x403 : 0x405;
  if (!p->has_output_gamma) p->gl.glDrawBuffer(p->draw_buffer); else draw_target(r, true);
  if (clear) { p->gl.glClearColor(1, 0, 0.5f, 1); p->gl.glClear(0x4000 | 0x100); }
}
void qa_gl_renderer_begin_view(qa_gl_renderer *r, qa_render_view_state view) {
  gimpl *p = G(r);
  if (p != NULL) invalidate_state(p);
  opened(r); draw_target(r, true);
  qa_gl *gl = &p->gl;
  qa_rect vp = view.viewport;
  double bottom = qa_gl_renderer_height(r) - vp.y - vp.height;
  if (!is_int32(vp.x) || !is_int32(vp.y) || !is_int32(vp.width) || !is_int32(vp.height) || !is_int32(bottom) || vp.width < 1 || vp.height < 1)
    qa_range_error("OpenGL viewport requires positive int32 dimensions");
  p->has_vp = view.has_view_projection;
  if (view.has_view_projection) for (int i = 0; i < 16; i++) p->view_projection[i] = (float)view.view_projection.values[i];
  if (view.has_view_projection) for (int i = 0; i < 16; i++) if (!isfinite(p->view_projection[i])) qa_range_error("OpenGL view projection must be finite");
  gl->glViewport((int32_t)vp.x, (int32_t)bottom, (int32_t)vp.width, (int32_t)vp.height);
  gl->glEnable(0xc11);
  gl->glScissor((int32_t)vp.x, (int32_t)bottom, (int32_t)vp.width, (int32_t)vp.height);
  identity_matrices(p);
  if (view.clip_plane == NULL) gl->glDisable(0x3000);
  else {
    qa_vec4 plane = *view.clip_plane;
    if (!isfinite(plane.x) || !isfinite(plane.y) || !isfinite(plane.z) || !isfinite(plane.w)) qa_range_error("OpenGL clip plane must be finite");
    double equation[4] = {plane.x, plane.y, plane.z, plane.w};
    gl->glClipPlane(0x3000, equation);
    gl->glEnable(0x3000);
  }
  if (view.clear != NULL) {
    const qa_vec4 *color = view.clear->color;
    if (!isfinite(view.clear->depth) || (color != NULL && !(finite32(color->x) && finite32(color->y) && finite32(color->z) && finite32(color->w))))
      qa_range_error("OpenGL clear values must be finite");
    gl->glDepthMask(1);
    gl->glClearDepth(view.clear->depth);
    if (color != NULL) gl->glClearColor((float)color->x, (float)color->y, (float)color->z, (float)color->w);
    if (view.clear->stencil) { gl->glStencilMask(0xffffffffu); gl->glClearStencil(0); }
    gl->glClear(0x100u | (color == NULL ? 0u : 0x4000u) | (view.clear->stencil ? 0x400u : 0u));
  }
}

/* --- prepared geometry --- */
struct gprep {
  qa_gl_renderer *r; qa_draw_batch batch; qa_render_state state; qa_polygon_offset offset;
  qa_gl_geometry_buffer *buffer; qa_gl_geometry_arrays arrays;
  qa_texture_resolver resolver; bool has_resolver;
  bool paired; const char *environment; uint32_t mode; float line_width;
  enum { PHASE_PREPARED, PHASE_ACTIVE, PHASE_DRAWN, PHASE_CLEANED } phase;
  int next_unit;
  gprep *next;
};
static void resolver_apply(void *ctx, qa_image_resource_operation op) { qa_gl_renderer_apply_image_resource((qa_gl_renderer *)ctx, op); }
static void buffer_free(qa_gl_geometry_buffer *buffer) { if (buffer != NULL) { qa_gl_geometry_buffer_destroy(buffer); free(buffer); } }
/* TS lets the GC reclaim cleaned closures; C reclaims them at the next prepare. */
static void reap_preps(gimpl *p) {
  while (p->retired_preps != NULL) { gprep *next = p->retired_preps->next; free(p->retired_preps); p->retired_preps = next; }
}
static void gprep_begin(void *ctx) {
  gprep *pr = (gprep *)ctx; opened(pr->r); gimpl *p = G(pr->r); qa_gl *gl = &p->gl;
  if (pr->phase != PHASE_PREPARED || p->active_arrays != NULL) qa_range_error("OpenGL prepared draw is already active");
  const qa_batch_lighting *lighting = &pr->batch.lighting;
  bool has_atlas = false; uint32_t atlas_name = 0;
  if (strcmp(lighting->kind, "vertex") != 0) {
    const qa_q2_shadow_atlas *atlas = !strcmp(lighting->kind, "q2-model-shadow") ? &lighting->atlas_value : lighting->atlas;
    if (atlas != NULL) {
      qa_gl_texture_info info = qa_gl_textures_registered(&p->textures, atlas->image);
      if (info.content_kind != QA_RENDER_IMAGE_DEPTH32F) qa_range_error("Q2 shadow atlas requires a depth32f image");
      has_atlas = true; atlas_name = info.name;
    }
  }
  draw_target(pr->r, true);
  apply_state(p, &pr->state);
  if (pr->batch.space != NULL && !strcmp(pr->batch.space, "world")) world_matrices(p); else identity_matrices(p);
  bool luminance = pr->batch.texture_effect != NULL && !strcmp(pr->batch.texture_effect, "luminance-alpha");
  qa_gl_stage_program_use(&p->program, pr->environment, pr->state.alpha_test, pr->batch.lighting, luminance, pr->batch.fog);
  p->active_arrays = &pr->arrays;
  pr->phase = PHASE_ACTIVE;
  const qa_gl_geometry_arrays *a = &pr->arrays;
  gl->glEnableClientState(0x8074);
  gl->glEnableClientState(0x8076);
  if (a->position_count > 0) {
    gl->glVertexPointer(4, 0x1406, 0, a->positions);
    gl->glColorPointer(4, 0x1406, 0, a->colors);
  }
  select_texture(p, 2);
  if (a->world_position_count > 0) { gl->glEnableClientState(0x8078); gl->glTexCoordPointer(3, 0x1406, 0, a->world_positions); }
  else gl->glDisableClientState(0x8078);
  select_texture(p, 3);
  if (a->normal_count > 0) { gl->glEnableClientState(0x8078); gl->glTexCoordPointer(3, 0x1406, 0, a->normals); }
  else gl->glDisableClientState(0x8078);
  if (has_atlas) { select_texture(p, 2); gl->glBindTexture(0xde1, atlas_name); }
  select_texture(p, 0);
  gl->glLineWidth(pr->line_width);
}
static void gprep_apply_texture(void *ctx, double unit, qa_texture_binding operation) {
  gprep *pr = (gprep *)ctx; opened(pr->r); gimpl *p = G(pr->r);
  if (pr->phase != PHASE_ACTIVE || unit != pr->next_unit || (unit != 0 && unit != 1) || (unit == 1 && !pr->paired))
    qa_range_error("OpenGL prepared texture order is invalid");
  qa_texture_binding binding = pr->has_resolver ? qa_texture_resolver_resolve(&pr->resolver, operation) : operation;
  select_texture(p, (int)unit);
  qa_gl_textures_bind(&p->textures, binding);
  const float *coordinates = unit == 0 ? pr->arrays.coordinates : pr->arrays.coordinates2;
  size_t count = unit == 0 ? pr->arrays.coordinate_count : pr->arrays.coordinate2_count;
  p->gl.glEnableClientState(0x8078);
  if (count > 0) p->gl.glTexCoordPointer(2, 0x1406, 0, coordinates);
  pr->next_unit++;
}
static void gprep_draw(void *ctx) {
  gprep *pr = (gprep *)ctx; opened(pr->r); gimpl *p = G(pr->r);
  if (pr->phase != PHASE_ACTIVE || pr->next_unit != (pr->paired ? 2 : 1)) qa_range_error("OpenGL prepared draw has unapplied texture slots");
  if (pr->arrays.index_count > 0) p->gl.glDrawElements(pr->mode, (int32_t)pr->arrays.index_count, 0x1405, pr->arrays.indices);
  pr->phase = PHASE_DRAWN;
}
static void gprep_release(gprep *pr, gimpl *p) {
  if (pr->has_resolver) { qa_texture_resolver_destroy(&pr->resolver); pr->has_resolver = false; }
  pr->phase = PHASE_CLEANED;
  pr->next = p->retired_preps; p->retired_preps = pr;
}
static void gprep_cleanup(void *ctx) {
  gprep *pr = (gprep *)ctx;
  if (pr->phase == PHASE_CLEANED) return;
  gimpl *p = G(pr->r);
  if (p->closed) { buffer_free(pr->buffer); pr->buffer = NULL; gprep_release(pr, p); return; }
  opened(pr->r);
  if (pr->phase != PHASE_PREPARED) {
    disable_arrays(p);
    p->gl.glLineWidth(1);
  }
  buffer_free(p->idle_geometry);
  p->idle_geometry = pr->buffer; pr->buffer = NULL;
  gprep_release(pr, p);
}
static void pack_body(void *ctx) { gprep *pr = (gprep *)ctx; pr->arrays = qa_gl_geometry_buffer_pack(pr->buffer, pr->batch); }
qa_prepared_backend_draw qa_gl_renderer_prepare_geometry(qa_gl_renderer *r, qa_draw_batch batch) {
  opened(r); gimpl *p = G(r);
  reap_preps(p);
  gprep *pr = (gprep *)calloc(1, sizeof *pr);
  if (pr == NULL) qa_range_error("OpenGL renderer allocation failed");
  bool dynamic = batch.texture.kind == QA_TEXTURE_BINDING_DYNAMIC_IMAGE || (batch.texturing_is_pair && batch.second_texture.binding.kind == QA_TEXTURE_BINDING_DYNAMIC_IMAGE);
  if (dynamic) {
    if (!qa_texture_resolver_init(&pr->resolver, resolver_apply, r)) qa_range_error("OpenGL renderer allocation failed");
    pr->has_resolver = true;
  }
  if (p->idle_geometry != NULL) { pr->buffer = p->idle_geometry; p->idle_geometry = NULL; }
  else {
    pr->buffer = (qa_gl_geometry_buffer *)calloc(1, sizeof *pr->buffer);
    if (pr->buffer == NULL || !qa_gl_geometry_buffer_init(pr->buffer)) qa_range_error("OpenGL geometry allocation failed");
  }
  pr->r = r; pr->batch = batch;
  char message[1024];
  if (!qa_range_error_catch(pack_body, pr, message, sizeof message)) {
    p->idle_geometry = pr->buffer;
    if (pr->has_resolver) qa_texture_resolver_destroy(&pr->resolver);
    free(pr);
    qa_range_error("%s", message);
  }
  pr->state = batch.state; /* TS snapshots the state: mutating the batch later has no effect. */
  if (batch.state.polygon_offset != NULL) { pr->offset = *batch.state.polygon_offset; pr->state.polygon_offset = &pr->offset; }
  pr->paired = batch.texturing_is_pair;
  pr->environment = pr->paired ? batch.second_texture.environment : NULL;
  pr->mode = batch.primitive_is_lines ? 1 : 4;
  pr->line_width = batch.primitive_is_lines ? (float)batch.line_width : 1.0f;
  qa_prepared_backend_draw o; memset(&o, 0, sizeof o);
  o.ctx = pr; o.begin = gprep_begin; o.apply_texture = gprep_apply_texture; o.draw = gprep_draw; o.cleanup = gprep_cleanup;
  return o;
}

/* --- scopes --- */
typedef struct opacity_scope { gimpl *p; qa_gl_draw_fn draw; void *ctx; double opacity, width, height; } opacity_scope;
static void opacity_child(void *ctx) { opacity_scope *s = (opacity_scope *)ctx; invalidate_state(s->p); s->draw(s->ctx); }
static void opacity_body(void *ctx) {
  opacity_scope *s = (opacity_scope *)ctx;
  qa_gl_object_opacity_draw(&s->p->object_opacity, s->opacity, s->width, s->height, opacity_child, s);
}
void qa_gl_renderer_with_object_opacity(qa_gl_renderer *r, double opacity, qa_gl_draw_fn draw, void *ctx) {
  opened(r); gimpl *p = G(r);
  if (!isfinite(opacity) || opacity < 0 || opacity > 1) qa_range_error("Object opacity must be in 0..1");
  if (p->opacity_active) qa_range_error("Nested OpenGL object opacity is unsupported");
  if (opacity == 0) return;
  if (opacity == 1) { draw(ctx); return; }
  if (p->active_arrays != NULL) qa_range_error("Object opacity cannot interrupt a prepared draw");
  double width = qa_gl_renderer_width(r), height = qa_gl_renderer_height(r);
  if (width > r->max_texture_size || height > r->max_texture_size) qa_range_error("OpenGL opacity target exceeds maximum texture size");
  draw_target(r, true);
  invalidate_state(p);
  if (!p->has_object_opacity) {
    if (!qa_gl_object_opacity_init(&p->object_opacity, &p->window.gl_context, &p->gl, p->precision)) qa_range_error("OpenGL opacity pass allocation failed");
    p->has_object_opacity = true;
  }
  p->opacity_active = true;
  opacity_scope scope = {p, draw, ctx, opacity, width, height};
  char message[1024];
  bool ok = qa_range_error_catch(opacity_body, &scope, message, sizeof message);
  p->opacity_active = false; invalidate_state(p); /* TS finally */
  if (!ok) qa_range_error("%s", message);
}

/* --- immediate operations --- */
static const qa_batch_lighting VERTEX_LIGHTING = {.kind = "vertex"};
static void bind_image(gimpl *p, qa_renderer_image image) {
  qa_texture_binding binding; memset(&binding, 0, sizeof binding);
  binding.kind = QA_TEXTURE_BINDING_BIND_IMAGE; binding.image = image;
  qa_gl_textures_bind(&p->textures, binding);
}
static bool vec4_finite32(qa_vec4 v) { return finite32(v.x) && finite32(v.y) && finite32(v.z) && finite32(v.w); }
static void draw_sky(gimpl *p, const qa_render_operation *op) {
  qa_gl *gl = &p->gl;
  for (size_t s = 0; s < op->strip_count; s++) {
    const qa_sky_strip *strip = &op->strips[s];
    if (strip->vertex_count < 2 || strip->vertex_count % 2 != 0) qa_range_error("OpenGL sky strips require paired row vertices");
    for (size_t v = 0; v < strip->vertex_count; v++) {
      const qa_vertex_position_tex_coord *vertex = &strip->vertices[v];
      if (!vec4_finite32(vertex->position) || !finite32(vertex->tex_coord.x) || !finite32(vertex->tex_coord.y)) qa_range_error("OpenGL sky vertices must be finite");
    }
  }
  identity_matrices(p);
  /* Sky boxes are opaque; do not inherit blend or alpha test from the previous batch. */
  gl->glDisable(0xbe2);
  p->alpha_test = "none";
  qa_gl_stage_program_use(&p->program, NULL, p->alpha_test, VERTEX_LIGHTING, false, NULL);
  select_texture(p, 0);
  bind_image(p, op->image);
  gl->glColor4f((float)op->color.x, (float)op->color.y, (float)op->color.z, (float)op->color.w);
  for (size_t s = 0; s < op->strip_count; s++) {
    const qa_sky_strip *strip = &op->strips[s];
    gl->glBegin(5);
    for (size_t v = 0; v < strip->vertex_count; v++) {
      const qa_vertex_position_tex_coord *vertex = &strip->vertices[v];
      gl->glTexCoord2f((float)vertex->tex_coord.x, (float)vertex->tex_coord.y);
      gl->glVertex4f((float)vertex->position.x, (float)vertex->position.y, (float)vertex->position.z, (float)vertex->position.w);
    }
    gl->glEnd();
  }
}
static void shadow_vertices(gimpl *p, const qa_render_operation *op, uint32_t mode) {
  qa_gl *gl = &p->gl;
  gl->glBegin(mode);
  if (op->kind == QA_RENDER_OP_SHADOW_FINISH) {
    for (int i = 0; i < 4; i++) { qa_vec4 v = op->finish_positions[i]; gl->glVertex4f((float)v.x, (float)v.y, (float)v.z, (float)v.w); }
  } else {
    for (size_t i = 0; i < op->indices.count; i++) { qa_vec4 v = op->positions[(size_t)op->indices.values[i]]; gl->glVertex4f((float)v.x, (float)v.y, (float)v.z, (float)v.w); }
  }
  gl->glEnd();
}
static void draw_shadow(qa_gl_renderer *r, const qa_render_operation *op) {
  gimpl *p = G(r); qa_gl *gl = &p->gl;
  /* RB_ShadowTessEnd and RB_ShadowFinish draw nothing without a stencil buffer. */
  if (r->stencil_bits < 4) return;
  bool finish = op->kind == QA_RENDER_OP_SHADOW_FINISH;
  if (finish) { for (int i = 0; i < 4; i++) if (!vec4_finite32(op->finish_positions[i])) qa_range_error("OpenGL shadow positions must be finite"); }
  else {
    for (size_t i = 0; i < op->position_count; i++) if (!vec4_finite32(op->positions[i])) qa_range_error("OpenGL shadow positions must be finite");
    for (size_t i = 0; i < op->indices.count; i++) {
      double index = op->indices.values[i];
      if (index != floor(index) || index < 0 || index >= (double)op->position_count) qa_range_error("OpenGL shadow vertex index is invalid");
    }
    if (op->indices.count % 3 != 0) qa_range_error("OpenGL shadow triangles are incomplete");
  }
  select_texture(p, 0);
  bind_image(p, op->white_image);
  identity_matrices(p);
  qa_gl_stage_program_use(&p->program, NULL, "none", VERTEX_LIGHTING, false, NULL);
  p->alpha_test = "none";
  invalidate_state(p);
  gl->glEnable(0xb71);
  gl->glDepthFunc(0x203);
  gl->glEnable(0xb90);
  if (finish) {
    gl->glStencilFunc(0x205, 0, 255);
    gl->glDisable(0x3000);
    gl->glDisable(0xb44);
    gl->glEnable(0xbe2);
    gl->glBlendFunc(0x306, 0);
    gl->glDepthMask(1);
    gl->glColor3f(0.6f, 0.6f, 0.6f);
    shadow_vertices(p, op, 7);
    gl->glColor3f(1, 1, 1);
    gl->glDisable(0xb90);
  } else {
    gl->glEnable(0xb44);
    gl->glDisable(0xbe2);
    gl->glDepthMask(0);
    gl->glColor3f(0.2f, 0.2f, 0.2f);
    gl->glColorMask(0, 0, 0, 0);
    gl->glStencilFunc(0x207, 1, 255);
    gl->glCullFace(op->mirror ? 0x404 : 0x405);
    gl->glStencilOp(0x1e00, 0x1e00, 0x1e02); shadow_vertices(p, op, 4);
    gl->glCullFace(op->mirror ? 0x405 : 0x404);
    gl->glStencilOp(0x1e00, 0x1e00, 0x1e03); shadow_vertices(p, op, 4);
    gl->glColorMask(1, 1, 1, 1);
  }
}
void qa_gl_renderer_draw_immediate(qa_gl_renderer *r, qa_render_operation op) {
  opened(r); draw_target(r, true);
  gimpl *p = G(r);
  switch (op.kind) {
  case QA_RENDER_OP_Q2_FOG:
    if (p->active_arrays != NULL) qa_range_error("OpenGL fog cannot interrupt a prepared draw");
    invalidate_state(p);
    if (!p->has_fog) {
      if (!qa_q2_fog_pass_init(&p->fog, &p->window.gl_context, &p->gl)) qa_range_error("OpenGL fog pass allocation failed");
      p->has_fog = true;
    }
    qa_q2_fog_pass * pass2 = &p->fog;
    qa_q2_fog_operation operation2 = op.fog;
    double target_width = qa_gl_renderer_width(r);
    double target_height = qa_gl_renderer_height(r);
    qa_q2_fog_pass_draw(pass2, operation2, target_width, target_height);
    return;
  case QA_RENDER_OP_DEPTH_ATLAS: {
    if (p->active_arrays != NULL) qa_range_error("OpenGL depth atlas cannot interrupt a prepared draw");
    qa_gl_texture_info texture = qa_gl_textures_registered(&p->textures, op.image);
    if (texture.content_kind != QA_RENDER_IMAGE_DEPTH32F) qa_range_error("OpenGL depth atlas target requires a depth32f image");
    invalidate_state(p);
    if (!p->has_depth_atlas) {
      if (!qa_depth_atlas_target_init(&p->depth_atlas, &p->window.gl_context, &p->gl, &p->program)) qa_range_error("OpenGL depth atlas allocation failed");
      p->has_depth_atlas = true;
    }
    qa_depth_atlas_target_draw(&p->depth_atlas, texture.name, op.image.width, op.image.height, op.passes, op.pass_count);
    return;
  }
  case QA_RENDER_OP_DEPTH_RANGE: apply_range(p, op.range); return;
  case QA_RENDER_OP_CULL: apply_cull(p, op.cull); return;
  case QA_RENDER_OP_POLYGON_OFFSET: apply_offset(p, op.polygon_offset); return;
  case QA_RENDER_OP_DISABLE_PORTAL_CLIP: p->gl.glDisable(0x3000); return;
  case QA_RENDER_OP_SKY_SIDE: draw_sky(p, &op); return;
  case QA_RENDER_OP_SHADOW_VOLUME: case QA_RENDER_OP_SHADOW_FINISH: draw_shadow(r, &op); return;
  case QA_RENDER_OP_DRAW: case QA_RENDER_OP_OBJECT_OPACITY: break;
  }
  qa_range_error("Invalid OpenGL immediate operation");
}
void qa_gl_renderer_clear_color_buffer(qa_gl_renderer *r) { opened(r); draw_target(r, true); G(r)->gl.glClear(0x4000); }
void qa_gl_renderer_draw_show_image(qa_gl_renderer *r, qa_renderer_image image, qa_rect rect, bool proportional) {
  opened(r); draw_target(r, true);
  gimpl *p = G(r); qa_gl *gl = &p->gl;
  (void)qa_gl_textures_registered(&p->textures, image);
  double width = (float)(rect.width * (proportional ? image.width / 512 : 1));
  double height = (float)(rect.height * (proportional ? image.height / 512 : 1));
  if (!finite32(rect.x) || !finite32(rect.y) || !finite32(width) || !finite32(height) || !finite32(rect.x + width) || !finite32(rect.y + height))
    qa_range_error("OpenGL image grid rectangle must be finite");
  p->matrices_identity = false;
  gl->glMatrixMode(0x1701); gl->glLoadIdentity(); double value = qa_gl_renderer_width(r); double value2 = qa_gl_renderer_height(r); gl->glOrtho(0, value, value2, 0, 0, 1);
  gl->glMatrixMode(0x1700); gl->glLoadIdentity();
  qa_gl_stage_program_use(&p->program, NULL, p->alpha_test, VERTEX_LIGHTING, false, NULL);
  select_texture(p, 0);
  bind_image(p, image);
  gl->glColor4f(1, 1, 1, 1);
  gl->glBegin(7);
  gl->glTexCoord2f(0, 0); gl->glVertex2f((float)rect.x, (float)rect.y);
  gl->glTexCoord2f(1, 0); gl->glVertex2f((float)(rect.x + width), (float)rect.y);
  gl->glTexCoord2f(1, 1); gl->glVertex2f((float)(rect.x + width), (float)(rect.y + height));
  gl->glTexCoord2f(0, 1); gl->glVertex2f((float)rect.x, (float)(rect.y + height));
  gl->glEnd();
}
void qa_gl_renderer_set_overdraw_measurement(qa_gl_renderer *r, bool enabled) {
  opened(r); draw_target(r, true);
  qa_gl *gl = &G(r)->gl;
  if (!enabled) { gl->glDisable(0xb90); return; }
  if (r->stencil_bits == 0) qa_range_error("OpenGL overdraw measurement requires a stencil framebuffer");
  gl->glEnable(0xb90);
  gl->glStencilMask(0xffffffffu);
  gl->glClearStencil(0);
  gl->glStencilFunc(0x207, 0, 0xffffffffu);
  gl->glStencilOp(0x1e00, 0x1e02, 0x1e02);
}
typedef struct read_scope { qa_gl *gl; int32_t x, y, w, h; uint32_t format, type; void *out; } read_scope;
static void read_body(void *ctx) { read_scope *s = (read_scope *)ctx; s->gl->glReadPixels(s->x, s->y, s->w, s->h, s->format, s->type, s->out); }
void qa_gl_renderer_read_stencil_overdraw(qa_gl_renderer *r, uint8_t *destination, size_t length) {
  opened(r); draw_target(r, false);
  double width = qa_gl_renderer_width(r), height = qa_gl_renderer_height(r);
  if ((double)length < width * height) qa_range_error("OpenGL stencil destination is too small");
  read_scope scope = {&G(r)->gl, 0, 0, (int32_t)width, (int32_t)height, 0x1901, 0x1401, destination};
  qa_gl_with_pixel_store(&G(r)->gl, "pack", read_body, &scope);
}
/* Absolute window coordinates use OpenGL's bottom-left origin, matching Q3 flares. */
double qa_gl_renderer_read_depth_pixel(qa_gl_renderer *r, double x, double y) {
  opened(r); draw_target(r, false);
  if (x != floor(x) || y != floor(y) || x < 0 || y < 0 || x >= qa_gl_renderer_width(r) || y >= qa_gl_renderer_height(r))
    qa_range_error("OpenGL depth coordinates are outside the framebuffer");
  float pixel = 0;
  read_scope scope = {&G(r)->gl, (int32_t)x, (int32_t)y, 1, 1, 0x1902, 0x1406, &pixel};
  qa_gl_with_pixel_store(&G(r)->gl, "pack", read_body, &scope);
  return (double)pixel;
}
static void resolve_output(qa_gl_renderer *r) {
  gimpl *p = G(r);
  if (p->has_output_gamma) {
    if (p->active_arrays != NULL) qa_range_error("OpenGL gamma cannot interrupt a prepared draw");
    draw_target(r, false);
    if (!p->gamma_finished) {
      invalidate_state(p);
      qa_gl_output_gamma_finish(&p->output_gamma, p->draw_buffer); p->gamma_finished = true;
    } else qa_gl_output_gamma_select_default(&p->output_gamma, p->draw_buffer);
  }
}
/* Returned RGBA pixels use top-left origin for SDL and captures. */
uint8_t *qa_gl_renderer_read_pixels(qa_gl_renderer *r, size_t *out_length) {
  opened(r);
  resolve_output(r);
  size_t width = (size_t)qa_gl_renderer_width(r), height = (size_t)qa_gl_renderer_height(r), stride = width * 4, n = stride * height;
  uint8_t *pixels = (uint8_t *)malloc(n ? n : 1), *top_down = (uint8_t *)malloc(n ? n : 1);
  if (pixels == NULL || top_down == NULL) { free(pixels); free(top_down); if (out_length) *out_length = 0; return NULL; }
  read_scope scope = {&G(r)->gl, 0, 0, (int32_t)width, (int32_t)height, 0x1908, 0x1401, pixels};
  qa_gl_with_pixel_store(&G(r)->gl, "pack", read_body, &scope);
  for (size_t row = 0; row < height; row++) memcpy(top_down + (height - row - 1) * stride, pixels + row * stride, stride);
  free(pixels);
  if (out_length) *out_length = n;
  return top_down;
}
typedef struct depth_scope { qa_gl *gl; float *out; } depth_scope;
static void depth_read(void *ctx) { depth_scope *s = (depth_scope *)ctx; s->gl->glGetTexImage(0xde1, 0, 0x1902, 0x1406, s->out); }
typedef struct depth_image_scope { gimpl *p; uint32_t name; float *out; } depth_image_scope;
static void depth_image_body(void *ctx) {
  depth_image_scope *s = (depth_image_scope *)ctx;
  s->p->gl.glBindTexture(0xde1, s->name);
  depth_scope scope = {&s->p->gl, s->out};
  qa_gl_with_pixel_store(&s->p->gl, "pack", depth_read, &scope);
}
bool qa_gl_renderer_read_depth_image(qa_gl_renderer *r, qa_renderer_image image, qa_depth_image_level *out, float *storage, size_t storage_count) {
  opened(r); gimpl *p = G(r);
  qa_gl_texture_info texture = qa_gl_textures_registered(&p->textures, image);
  if (texture.content_kind != QA_RENDER_IMAGE_DEPTH32F) qa_range_error("OpenGL depth readback requires a depth32f image");
  select_texture(p, 2);
  double previous = get_integer(p, 0x8069);
  size_t need = (size_t)(image.width * image.height);
  if (storage_count < need) return false; /* TS allocates; C callers supply storage. */
  depth_image_scope scope = {p, texture.name, storage};
  char message[1024];
  bool ok = qa_range_error_catch(depth_image_body, &scope, message, sizeof message);
  p->gl.glBindTexture(0xde1, (uint32_t)previous); select_texture(p, 0); /* TS finally */
  if (!ok) qa_range_error("%s", message);
  out->width = image.width; out->height = image.height; out->pixels = storage; out->pixel_count = need;
  return true;
}
void qa_gl_renderer_finish(qa_gl_renderer *r) { opened(r); resolve_output(r); G(r)->gl.glFinish(); }
uint32_t qa_gl_renderer_get_error(qa_gl_renderer *r) { opened(r); return G(r)->gl.glGetError(); }
static void present_body(void *ctx) {
  qa_gl_renderer *r = (qa_gl_renderer *)ctx; gimpl *p = G(r);
  opened(r); resolve_output(r);
  if (p->window.swap == NULL) qa_range_error("OpenGL render context has no swap");
  p->window.swap(p->window.ctx);
}
void qa_gl_renderer_present(qa_gl_renderer *r) {
  char message[1024];
  bool ok = qa_range_error_catch(present_body, r, message, sizeof message);
  if (G(r) != NULL) invalidate_state(G(r)); /* TS finally */
  if (!ok) qa_range_error("%s", message);
}
void qa_gl_renderer_close(qa_gl_renderer *r) {
  gimpl *p = G(r);
  if (p == NULL || p->closed) return;
  invalidate_state(p);
  p->window.set_rendering_enabled(p->window.ctx, true);
  p->window.make_current(p->window.ctx);
  disable_arrays(p);
  buffer_free(p->idle_geometry); p->idle_geometry = NULL;
  if (p->has_output_gamma) { qa_gl_output_gamma_close(&p->output_gamma); p->has_output_gamma = false; }
  if (p->has_object_opacity) { qa_gl_object_opacity_close(&p->object_opacity); p->has_object_opacity = false; }
  if (p->has_depth_atlas) { qa_depth_atlas_target_close(&p->depth_atlas); p->has_depth_atlas = false; }
  if (p->has_fog) { qa_q2_fog_pass_close(&p->fog); p->has_fog = false; }
  invalidate_state(p);
  qa_gl_textures_close(&p->textures);
  for (int unit = 3; unit >= 0; unit--) { select_texture(p, unit); p->gl.glBindTexture(0xde1, 0); }
  qa_gl_stage_program_close(&p->program);
  qa_gl_close(&p->gl);
  p->closed = true;
  active_remove(p->active_key);
}
void qa_gl_renderer_destroy(qa_gl_renderer *r) {
  gimpl *p = G(r);
  if (p == NULL) return;
  qa_gl_renderer_close(r);
  reap_preps(p);
  free(p); r->impl = NULL;
}
static void be_image(void *ctx, qa_image_resource_operation op) { qa_gl_renderer_apply_image_resource((qa_gl_renderer *)ctx, op); }
static void be_buffer(void *ctx, qa_renderer_draw_buffer b, bool c) { qa_gl_renderer_select_draw_buffer((qa_gl_renderer *)ctx, b, c); }
static void be_overdraw(void *ctx, bool e) { qa_gl_renderer_set_overdraw_measurement((qa_gl_renderer *)ctx, e); }
static void be_stencil(void *ctx, uint8_t *d, size_t n) { qa_gl_renderer_read_stencil_overdraw((qa_gl_renderer *)ctx, d, n); }
static double be_depth(void *ctx, double x, double y) { return qa_gl_renderer_read_depth_pixel((qa_gl_renderer *)ctx, x, y); }
static void be_view(void *ctx, qa_render_view_state v) { qa_gl_renderer_begin_view((qa_gl_renderer *)ctx, v); }
static void be_opacity(void *ctx, double o, void (*draw)(void *), void *d) { qa_gl_renderer_with_object_opacity((qa_gl_renderer *)ctx, o, draw, d); }
static void be_immediate(void *ctx, qa_render_operation op) { qa_gl_renderer_draw_immediate((qa_gl_renderer *)ctx, op); }
static qa_prepared_backend_draw be_prepare(void *ctx, qa_draw_batch b) { return qa_gl_renderer_prepare_geometry((qa_gl_renderer *)ctx, b); }
static void be_clear(void *ctx) { qa_gl_renderer_clear_color_buffer((qa_gl_renderer *)ctx); }
static void be_show(void *ctx, qa_renderer_image im, qa_rect rc, bool pr) { qa_gl_renderer_draw_show_image((qa_gl_renderer *)ctx, im, rc, pr); }
static void be_finish(void *ctx) { qa_gl_renderer_finish((qa_gl_renderer *)ctx); }
static void be_close(void *ctx) { qa_gl_renderer_close((qa_gl_renderer *)ctx); }
qa_renderer_backend qa_gl_renderer_backend(qa_gl_renderer *r) {
  qa_renderer_backend b; memset(&b, 0, sizeof b);
  b.ctx = r; b.owner = r->owner; b.width = qa_gl_renderer_width(r); b.height = qa_gl_renderer_height(r); b.stencil_bits = r->stencil_bits;
  b.apply_image_resource = be_image; b.select_draw_buffer = be_buffer; b.set_overdraw_measurement = be_overdraw; b.read_stencil_overdraw = be_stencil;
  b.read_depth_pixel = be_depth; b.begin_view = be_view; b.with_object_opacity = be_opacity; b.draw_immediate = be_immediate; b.prepare_geometry = be_prepare;
  b.clear_color_buffer = be_clear; b.draw_show_image = be_show; b.finish = be_finish; b.close = be_close;
  return b;
}
