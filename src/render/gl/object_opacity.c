/* Port of src/render/gl/object-opacity.ts. GPL-2.0-or-later. */
#include "qa/render/gl/object_opacity.h"
#include "qa/foundation/support.h"
#include "qa/platform/gl_framebuffers.h"
#include "qa/platform/gl_programs.h"
#include "qa/render/gl/programs.h"
#include <math.h>
#include <stdlib.h>
#include "qa/foundation/js.h"

static const char VERTEX[] = "#version 120\n"
  "varying vec2 tc;\n"
  "void main() { tc = gl_Vertex.xy * 0.5 + 0.5; gl_Position = gl_Vertex; }\n";
static const char FRAGMENT[] = "#version 120\n"
  "uniform sampler2D backdrop;\n"
  "uniform sampler2D result;\n"
  "uniform float opacity;\n"
  "varying vec2 tc;\n"
  "void main() {\n"
  "  vec4 b = texture2D(backdrop, tc);\n"
  "  gl_FragColor = b + opacity * (texture2D(result, tc) - b);\n"
  "}\n";

typedef struct oimpl {
  qa_gl *gl; qa_gl_precision precision;
  qa_gl_framebuffers fbo; qa_gl_programs shaders;
  uint32_t targets[2], colors[2], depth[1], program;
  int32_t backdrop_uniform, result_uniform, opacity_uniform;
  /* TS dimensions string `${width}:${height}:${floating}`. */
  bool has_dimensions, dim_floating; double dim_width, dim_height;
  bool active;
} oimpl;
static oimpl *O(qa_gl_object_opacity *o) { return (oimpl *)o->impl; }

static int32_t integer1(oimpl *p, uint32_t name) { int32_t v = 0; p->gl->glGetIntegerv(name, &v); return v; }
static int32_t uniform_of(oimpl *p, const char *name) {
  int32_t location = p->shaders.glGetUniformLocation(p->program, name);
  if (location < 0) qa_range_error("OpenGL opacity uniform missing: %s", name);
  return location;
}
static void check_status(oimpl *p) {
  uint32_t status = p->fbo.glCheckFramebufferStatus(0x8d40);
  if (status != 0x8cd5) qa_range_error("OpenGL opacity framebuffer incomplete: 0x%x", status);
}
static void allocate_texture(oimpl *p, uint32_t name, int32_t internal, uint32_t format, uint32_t type, int32_t width, int32_t height) {
  qa_gl *gl = p->gl;
  gl->glBindTexture(0xde1, name);
  gl->glTexParameteri(0xde1, 0x2801, 0x2600); gl->glTexParameteri(0xde1, 0x2800, 0x2600);
  gl->glTexParameteri(0xde1, 0x2802, 0x812f); gl->glTexParameteri(0xde1, 0x2803, 0x812f);
  p->fbo.glTexImage2D(0xde1, 0, internal, width, height, 0, format, type, NULL);
}
static void allocate(oimpl *p, double width, double height) {
  qa_gl *gl = p->gl;
  int32_t component = 0;
  p->fbo.glGetFramebufferAttachmentParameteriv(0x8ca9, integer1(p, 0x8ca6) == 0 ? 0x1801 : 0x8d00, 0x8211, &component);
  bool floating = component == 0x1406;
  if (p->has_dimensions && p->dim_width == width && p->dim_height == height && p->dim_floating == floating) return;
  int32_t active = integer1(p, 0x84e0); gl->glActiveTexture(0x84c0);
  int32_t previous = integer1(p, 0x8069);
  int32_t w = (int32_t)width, h = (int32_t)height;
  for (int i = 0; i < 2; i++)
    allocate_texture(p, p->colors[i], p->precision.alpha_bits > 0 ? 0x8058 : p->precision.color_bits <= 16 ? 0x8d62 : 0x8051, 0x1908, 0x1401, w, h);
  bool stencil = p->precision.stencil_bits > 0;
  allocate_texture(p, p->depth[0],
    stencil ? (floating ? 0x8cad : 0x88f0) : floating ? 0x8cac : p->precision.depth_bits <= 16 ? 0x81a5 : p->precision.depth_bits <= 24 ? 0x81a6 : 0x81a7,
    stencil ? 0x84f9 : 0x1902, stencil ? (floating ? 0x8dad : 0x84fa) : floating ? 0x1406 : 0x1405, w, h);
  for (int index = 0; index < 2; index++) {
    p->fbo.glBindFramebuffer(0x8d40, p->targets[index]);
    p->fbo.glFramebufferTexture2D(0x8d40, 0x8ce0, 0xde1, p->colors[index], 0);
    if (index == 1) p->fbo.glFramebufferTexture2D(0x8d40, stencil ? 0x821a : 0x8d00, 0xde1, p->depth[0], 0);
    gl->glDrawBuffer(0x8ce0); p->fbo.glReadBuffer(0x8ce0); check_status(p);
  }
  p->has_dimensions = true; p->dim_width = width; p->dim_height = height; p->dim_floating = floating;
  gl->glBindTexture(0xde1, (uint32_t)previous); gl->glActiveTexture((uint32_t)active);
}

bool qa_gl_object_opacity_init(qa_gl_object_opacity *o, const qa_gl_context *context, qa_gl *gl, qa_gl_precision precision) {
  oimpl *p = (oimpl *)calloc(1, sizeof *p); if (p == NULL) return false;
  p->gl = gl; p->precision = precision;
  if (!qa_load_gl_framebuffers(context, &p->fbo)) { free(p); return false; }
  if (!qa_load_gl_programs(context, &p->shaders)) { qa_gl_framebuffers_close(&p->fbo); free(p); return false; }
  o->impl = p;
  p->fbo.glGenFramebuffers(2, p->targets);
  gl->glGenTextures(2, p->colors); gl->glGenTextures(1, p->depth);
  if (p->targets[0] == 0 || p->targets[1] == 0 || p->colors[0] == 0 || p->colors[1] == 0 || p->depth[0] == 0) {
    p->fbo.glDeleteFramebuffers(2, p->targets);
    gl->glDeleteTextures(2, p->colors); gl->glDeleteTextures(1, p->depth);
    qa_gl_programs_close(&p->shaders); qa_gl_framebuffers_close(&p->fbo);
    qa_range_error("OpenGL opacity allocation failed");
  }
  p->program = qa_gl_compile_program(&p->shaders, VERTEX, FRAGMENT);
  p->backdrop_uniform = uniform_of(p, "backdrop");
  p->result_uniform = uniform_of(p, "result");
  p->opacity_uniform = uniform_of(p, "opacity");
  return true;
}

bool qa_gl_object_opacity_bind(qa_gl_object_opacity *o) {
  oimpl *p = O(o);
  if (!p->active) return false;
  p->fbo.glBindFramebuffer(0x8d40, p->targets[1]);
  p->gl->glDrawBuffer(0x8ce0); p->fbo.glReadBuffer(0x8ce0);
  return true;
}

static void composite(oimpl *p, double opacity, double width, double height, const int32_t viewport[4], const int32_t scissor[4], bool clipped) {
  qa_gl *gl = p->gl; qa_gl_programs *shader = &p->shaders;
  /* Snapshot after child uploads: old names may have been released while drawing. */
  int32_t program = integer1(p, 0x8b8d), active = integer1(p, 0x84e0);
  int32_t color_mask[4] = {0, 0, 0, 0}, modes[2] = {0, 0};
  gl->glGetIntegerv(0xc23, color_mask);
  int32_t depth_mask = integer1(p, 0xb72); gl->glGetIntegerv(0xb40, modes);
  static const uint32_t names[9] = {0xb71, 0xb44, 0xbc0, 0xb90, 0x3000, 0xc11, 0xbe2, 0xbd0, 0x8037};
  bool enabled[9];
  for (int i = 0; i < 9; i++) enabled[i] = gl->glIsEnabled(names[i]) != 0;
  gl->glActiveTexture(0x84c0); int32_t texture0 = integer1(p, 0x8069);
  gl->glActiveTexture(0x84c1); int32_t texture1 = integer1(p, 0x8069);
  gl->glViewport(0, 0, (int32_t)width, (int32_t)height); gl->glDepthMask(0); gl->glColorMask(1, 1, 1, 1); gl->glPolygonMode(0x408, 0x1b02);
  for (int i = 0; i < 9; i++) gl->glDisable(names[i]);
  double x = qa_js_max(qa_js_max(0, viewport[0]), clipped ? scissor[0] : 0);
  double y = qa_js_max(qa_js_max(0, viewport[1]), clipped ? scissor[1] : 0);
  double right = qa_js_min(qa_js_min(width, (double)viewport[0] + viewport[2]), clipped ? (double)scissor[0] + scissor[2] : width);
  double top = qa_js_min(qa_js_min(height, (double)viewport[1] + viewport[3]), clipped ? (double)scissor[1] + scissor[3] : height);
  gl->glEnable(0xc11); gl->glScissor((int32_t)x, (int32_t)y, (int32_t)qa_js_max(0, right - x), (int32_t)qa_js_max(0, top - y));
  shader->glUseProgram(p->program); shader->glUniform1i(p->backdrop_uniform, 0); shader->glUniform1i(p->result_uniform, 1); shader->glUniform1f(p->opacity_uniform, (float)opacity);
  gl->glBindTexture(0xde1, p->colors[1]); gl->glActiveTexture(0x84c0); gl->glBindTexture(0xde1, p->colors[0]);
  gl->glBegin(7);
  gl->glVertex4f(-1, -1, 0, 1);
  gl->glVertex4f(1, -1, 0, 1);
  gl->glVertex4f(1, 1, 0, 1);
  gl->glVertex4f(-1, 1, 0, 1); gl->glEnd();
  uint32_t error = gl->glGetError();
  if (error != 0) qa_range_error("OpenGL opacity composite failed: 0x%x", error);
  gl->glBindTexture(0xde1, (uint32_t)texture0); gl->glActiveTexture(0x84c1); gl->glBindTexture(0xde1, (uint32_t)texture1); gl->glActiveTexture((uint32_t)active);
  shader->glUseProgram((uint32_t)program); gl->glDepthMask((uint8_t)depth_mask); gl->glColorMask((uint8_t)color_mask[0], (uint8_t)color_mask[1], (uint8_t)color_mask[2], (uint8_t)color_mask[3]);
  gl->glPolygonMode(0x404, (uint32_t)modes[0]); gl->glPolygonMode(0x405, (uint32_t)modes[1]);
  gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  for (int i = 0; i < 9; i++) { if (enabled[i]) gl->glEnable(names[i]); else gl->glDisable(names[i]); }
}

void qa_gl_object_opacity_draw(qa_gl_object_opacity *o, double value, double width, double height, qa_gl_opacity_draw_fn draw, void *ctx) {
  oimpl *p = O(o);
  if (p->active) qa_range_error("Nested OpenGL object opacity is unsupported");
  qa_gl *gl = p->gl; qa_gl_framebuffers *fbo = &p->fbo;
  uint32_t destination = (uint32_t)integer1(p, 0x8ca6), read_target = (uint32_t)integer1(p, 0x8caa);
  uint32_t draw_buffer = (uint32_t)integer1(p, 0xc01), read_buffer = (uint32_t)integer1(p, 0xc02);
  int32_t viewport[4] = {0, 0, 0, 0}, scissor[4] = {0, 0, 0, 0};
  gl->glGetIntegerv(0xba2, viewport); gl->glGetIntegerv(0xc10, scissor);
  bool clipped = gl->glIsEnabled(0xc11) != 0;
  if (integer1(p, 0x80a9) != 0) qa_range_error("Object opacity requires a single-sample framebuffer");
  allocate(p, width, height);
  gl->glDisable(0xc11);
  int32_t w = (int32_t)width, h = (int32_t)height;
  for (int index = 0; index < 2; index++) {
    fbo->glBindFramebuffer(0x8ca8, destination); fbo->glReadBuffer(draw_buffer);
    fbo->glBindFramebuffer(0x8ca9, p->targets[index]); gl->glDrawBuffer(0x8ce0);
    fbo->glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
      0x4000u | (index == 0 ? 0u : 0x100u | (p->precision.stencil_bits > 0 ? 0x400u : 0u)), 0x2600);
  }
  uint32_t copy_error = gl->glGetError();
  if (copy_error != 0) qa_range_error("OpenGL opacity backdrop copy failed: 0x%x", copy_error);
  if (clipped) gl->glEnable(0xc11);
  p->active = true; (void)qa_gl_object_opacity_bind(o);
  draw(ctx);
  p->active = false;
  fbo->glBindFramebuffer(0x8ca9, destination); gl->glDrawBuffer(draw_buffer);
  composite(p, value, width, height, viewport, scissor, clipped);
  fbo->glBindFramebuffer(0x8ca9, destination); gl->glDrawBuffer(draw_buffer);
  fbo->glBindFramebuffer(0x8ca8, read_target); fbo->glReadBuffer(read_buffer);
  gl->glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
  if (clipped) gl->glEnable(0xc11); else gl->glDisable(0xc11);
}

void qa_gl_object_opacity_close(qa_gl_object_opacity *o) {
  oimpl *p = O(o); if (p == NULL) return;
  p->fbo.glDeleteFramebuffers(2, p->targets);
  p->gl->glDeleteTextures(2, p->colors); p->gl->glDeleteTextures(1, p->depth);
  p->shaders.glDeleteProgram(p->program); qa_gl_programs_close(&p->shaders); qa_gl_framebuffers_close(&p->fbo);
  free(p); o->impl = NULL;
}
