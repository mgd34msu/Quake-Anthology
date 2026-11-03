/* Port of src/render/gl/output-gamma.ts. GPL-2.0-or-later. */
#include "qa/render/gl/output_gamma.h"
#include "qa/foundation/support.h"
#include "qa/platform/gl_framebuffers.h"
#include "qa/platform/gl_programs.h"
#include "qa/render/gl/programs.h"
#include "qa/render/gl/textures.h"
#include <stdlib.h>

static const char VERTEX[] = "#version 120\n"
  "varying vec2 tc;\n"
  "void main() { tc = gl_MultiTexCoord0.xy; gl_Position = gl_Vertex; }\n";
static const char FRAGMENT[] = "#version 120\n"
  "uniform sampler2D rawColor;\n"
  "uniform sampler2D gammaTable;\n"
  "varying vec2 tc;\n"
  "float corrected(float value) {\n"
  "  return texture2D(gammaTable, vec2((floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5) + 0.5) / 256.0, 0.5)).r;\n"
  "}\n"
  "void main() {\n"
  "  vec4 color = texture2D(rawColor, tc);\n"
  "  gl_FragColor = vec4(corrected(color.r), corrected(color.g), corrected(color.b), color.a);\n"
  "}\n";

/* TS Map<number, Uint32Array>: insertion order is observable in finish/restore. */
typedef struct gcolor { double buffer; uint32_t name; } gcolor;
typedef struct gimpl {
  qa_gl *gl; qa_gl_precision precision;
  qa_gl_framebuffers fbo; qa_gl_programs shaders;
  uint32_t target[1], depth[1], table[1], program;
  gcolor *colors; size_t color_count, color_cap;
  int32_t raw_uniform, table_uniform, depth_internal;
  uint32_t depth_format, depth_type, mask;
  double width, height;
} gimpl;
static gimpl *G(qa_gl_output_gamma *g) { return (gimpl *)g->impl; }
static int32_t integer1(gimpl *p, uint32_t name) { int32_t v = 0; p->gl->glGetIntegerv(name, &v); return v; }
static int32_t uniform_of(gimpl *p, const char *name) {
  int32_t location = p->shaders.glGetUniformLocation(p->program, name);
  if (location < 0) qa_range_error("OpenGL gamma uniform is missing: %s", name);
  return location;
}
typedef void (*texture_action)(gimpl *p, void *ctx);
static void with_texture(gimpl *p, uint32_t name, texture_action action, void *ctx) {
  qa_gl *gl = p->gl; int32_t active = integer1(p, 0x84e0); gl->glActiveTexture(0x84c0);
  int32_t previous = integer1(p, 0x8069);
  gl->glBindTexture(0xde1, name);
  gl->glTexParameteri(0xde1, 0x2801, 0x2600); gl->glTexParameteri(0xde1, 0x2800, 0x2600);
  gl->glTexParameteri(0xde1, 0x2802, 0x812f); gl->glTexParameteri(0xde1, 0x2803, 0x812f);
  action(p, ctx);
  gl->glBindTexture(0xde1, (uint32_t)previous); gl->glActiveTexture((uint32_t)active);
}
typedef struct upload_ctx { gimpl *p; const uint8_t *table; } upload_ctx;
static void upload_table(void *ctx) {
  upload_ctx *c = (upload_ctx *)ctx;
  c->p->gl->glTexImage2D(0xde1, 0, 0x8040, 256, 1, 0, 0x1909, 0x1401, c->table);
}
static void upload_action(gimpl *p, void *ctx) { qa_gl_with_pixel_store(p->gl, "unpack", upload_table, ctx); }
static void depth_action(gimpl *p, void *ctx) {
  (void)ctx;
  p->fbo.glTexImage2D(0xde1, 0, p->depth_internal, (int32_t)p->width, (int32_t)p->height, 0, p->depth_format, p->depth_type, NULL);
}
static void color_action(gimpl *p, void *ctx) {
  (void)ctx;
  p->fbo.glTexImage2D(0xde1, 0, p->precision.alpha_bits > 0 ? 0x8058 : p->precision.color_bits <= 16 ? 0x8d62 : 0x8051,
    (int32_t)p->width, (int32_t)p->height, 0, 0x1908, 0x1401, NULL);
}
void qa_gl_output_gamma_update(qa_gl_output_gamma *g, const uint8_t table[256]) {
  gimpl *p = G(g); upload_ctx c; c.p = p; c.table = table;
  with_texture(p, p->table[0], upload_action, &c);
}
static void blit(gimpl *p, uint32_t mask) {
  qa_gl *gl = p->gl; bool scissor = gl->glIsEnabled(0xc11) != 0;
  gl->glDisable(0xc11);
  int32_t w = (int32_t)p->width, h = (int32_t)p->height;
  p->fbo.glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, mask, 0x2600);
  if (scissor) gl->glEnable(0xc11);
}

bool qa_gl_output_gamma_init(qa_gl_output_gamma *g, const qa_gl_context *context, qa_gl *gl, qa_gl_precision precision, const uint8_t table[256]) {
  gimpl *p = (gimpl *)calloc(1, sizeof *p); if (p == NULL) return false;
  p->gl = gl; p->precision = precision;
  if (!qa_load_gl_framebuffers(context, &p->fbo)) { free(p); return false; }
  if (!qa_load_gl_programs(context, &p->shaders)) { qa_gl_framebuffers_close(&p->fbo); free(p); return false; }
  g->impl = p;
  int32_t component = 0;
  p->fbo.glGetFramebufferAttachmentParameteriv(0x8d40, 0x1801, 0x8211, &component);
  bool floating = component == 0x1406, stencil = precision.stencil_bits > 0;
  p->depth_internal = stencil ? (floating ? 0x8cad : 0x88f0)
    : floating ? 0x8cac : precision.depth_bits <= 16 ? 0x81a5 : precision.depth_bits <= 24 ? 0x81a6 : 0x81a7;
  p->depth_format = stencil ? 0x84f9 : 0x1902;
  p->depth_type = stencil ? (floating ? 0x8dad : 0x84fa) : floating ? 0x1406 : 0x1405;
  p->mask = 0x4000u | 0x100u | (stencil ? 0x400u : 0u);
  p->fbo.glGenFramebuffers(1, p->target);
  gl->glGenTextures(1, p->depth); gl->glGenTextures(1, p->table);
  if (p->target[0] == 0 || p->depth[0] == 0 || p->table[0] == 0) {
    p->fbo.glDeleteFramebuffers(1, p->target);
    gl->glDeleteTextures(1, p->depth); gl->glDeleteTextures(1, p->table);
    qa_gl_programs_close(&p->shaders); qa_gl_framebuffers_close(&p->fbo);
    qa_range_error("OpenGL gamma allocation failed");
  }
  p->program = qa_gl_compile_program(&p->shaders, VERTEX, FRAGMENT);
  p->raw_uniform = uniform_of(p, "rawColor"); p->table_uniform = uniform_of(p, "gammaTable");
  qa_gl_output_gamma_update(g, table);
  return true;
}

bool qa_gl_output_gamma_bind(qa_gl_output_gamma *g, double buffer, double width, double height) {
  gimpl *p = G(g); bool changed = false;
  qa_gl *gl = p->gl; qa_gl_framebuffers *fbo = &p->fbo;
  if (width != p->width || height != p->height) {
    changed = true;
    for (size_t i = 0; i < p->color_count; i++) gl->glDeleteTextures(1, &p->colors[i].name);
    p->color_count = 0; p->width = width; p->height = height;
    with_texture(p, p->depth[0], depth_action, NULL);
  }
  gcolor *color = NULL;
  for (size_t i = 0; i < p->color_count; i++) if (p->colors[i].buffer == buffer) color = &p->colors[i];
  bool initialize_depth = p->color_count == 0;
  if (color == NULL) {
    changed = true;
    uint32_t name = 0; gl->glGenTextures(1, &name);
    if (name == 0) qa_range_error("OpenGL gamma color allocation failed");
    if (p->color_count == p->color_cap) {
      size_t cap = p->color_cap ? p->color_cap * 2 : 4;
      gcolor *grown = (gcolor *)realloc(p->colors, cap * sizeof *grown);
      if (grown == NULL) qa_range_error("OpenGL gamma color allocation failed");
      p->colors = grown; p->color_cap = cap;
    }
    color = &p->colors[p->color_count++]; color->buffer = buffer; color->name = name;
    with_texture(p, name, color_action, NULL);
    fbo->glBindFramebuffer(0x8d40, p->target[0]);
    fbo->glFramebufferTexture2D(0x8d40, 0x8ce0, 0xde1, name, 0);
    fbo->glFramebufferTexture2D(0x8d40, p->precision.stencil_bits > 0 ? 0x821a : 0x8d00, 0xde1, p->depth[0], 0);
    gl->glDrawBuffer(0x8ce0); fbo->glReadBuffer(0x8ce0);
    uint32_t status = fbo->glCheckFramebufferStatus(0x8d40);
    if (status != 0x8cd5) qa_range_error("OpenGL gamma framebuffer incomplete: 0x%x", status);
    fbo->glBindFramebuffer(0x8ca8, 0); fbo->glReadBuffer((uint32_t)buffer);
    blit(p, initialize_depth ? p->mask : 0x4000u);
  }
  fbo->glBindFramebuffer(0x8d40, p->target[0]);
  fbo->glFramebufferTexture2D(0x8d40, 0x8ce0, 0xde1, color->name, 0);
  gl->glDrawBuffer(0x8ce0); fbo->glReadBuffer(0x8ce0);
  return changed;
}

void qa_gl_output_gamma_select_default(qa_gl_output_gamma *g, double buffer) {
  gimpl *p = G(g);
  p->fbo.glBindFramebuffer(0x8d40, 0); p->gl->glDrawBuffer((uint32_t)buffer); p->fbo.glReadBuffer((uint32_t)buffer);
}

void qa_gl_output_gamma_finish(qa_gl_output_gamma *g, double buffer) {
  gimpl *p = G(g); qa_gl *gl = p->gl; qa_gl_programs *shader = &p->shaders; qa_gl_framebuffers *fbo = &p->fbo;
  int32_t old_program = integer1(p, 0x8b8d), active = integer1(p, 0x84e0);
  int32_t viewport[4] = {0, 0, 0, 0}, color_mask[4] = {0, 0, 0, 0}, modes[2] = {0, 0};
  gl->glGetIntegerv(0xba2, viewport); gl->glGetIntegerv(0xc23, color_mask);
  int32_t depth_mask = integer1(p, 0xb72);
  gl->glGetIntegerv(0xb40, modes);
  static const uint32_t names[9] = {0xb71, 0xb44, 0xbc0, 0xb90, 0x3000, 0xc11, 0xbe2, 0xbd0, 0x8037};
  bool enabled[9];
  for (int i = 0; i < 9; i++) enabled[i] = gl->glIsEnabled(names[i]) != 0;
  gl->glActiveTexture(0x84c0); int32_t texture0 = integer1(p, 0x8069);
  gl->glActiveTexture(0x84c1); int32_t texture1 = integer1(p, 0x8069);
  fbo->glBindFramebuffer(0x8d40, 0);
  gl->glViewport(0, 0, (int32_t)p->width, (int32_t)p->height); gl->glDepthMask(0); gl->glColorMask(1, 1, 1, 1); gl->glPolygonMode(0x408, 0x1b02);
  for (int i = 0; i < 9; i++) gl->glDisable(names[i]);
  shader->glUseProgram(p->program); shader->glUniform1i(p->raw_uniform, 0); shader->glUniform1i(p->table_uniform, 1);
  gl->glBindTexture(0xde1, p->table[0]);
  gl->glActiveTexture(0x84c0);
  for (size_t i = 0; i < p->color_count; i++) {
    gl->glDrawBuffer((uint32_t)p->colors[i].buffer); gl->glBindTexture(0xde1, p->colors[i].name);
    gl->glBegin(7);
    gl->glTexCoord2f(0, 0); gl->glVertex4f(-1, -1, 0, 1);
    gl->glTexCoord2f(1, 0); gl->glVertex4f(1, -1, 0, 1);
    gl->glTexCoord2f(1, 1); gl->glVertex4f(1, 1, 0, 1);
    gl->glTexCoord2f(0, 1); gl->glVertex4f(-1, 1, 0, 1);
    gl->glEnd();
  }
  gl->glBindTexture(0xde1, (uint32_t)texture0); gl->glActiveTexture(0x84c1); gl->glBindTexture(0xde1, (uint32_t)texture1); gl->glActiveTexture((uint32_t)active);
  shader->glUseProgram((uint32_t)old_program); gl->glDepthMask((uint8_t)depth_mask); gl->glColorMask((uint8_t)color_mask[0], (uint8_t)color_mask[1], (uint8_t)color_mask[2], (uint8_t)color_mask[3]);
  gl->glPolygonMode(0x404, (uint32_t)modes[0]); gl->glPolygonMode(0x405, (uint32_t)modes[1]);
  gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  for (int i = 0; i < 9; i++) { if (enabled[i]) gl->glEnable(names[i]); else gl->glDisable(names[i]); }
  gl->glDrawBuffer((uint32_t)buffer); fbo->glReadBuffer((uint32_t)buffer);
}

void qa_gl_output_gamma_restore(qa_gl_output_gamma *g, double buffer) {
  gimpl *p = G(g); qa_gl_framebuffers *fbo = &p->fbo;
  for (size_t i = 0; i < p->color_count; i++) {
    fbo->glBindFramebuffer(0x8ca8, p->target[0]);
    fbo->glFramebufferTexture2D(0x8ca8, 0x8ce0, 0xde1, p->colors[i].name, 0); fbo->glReadBuffer(0x8ce0);
    fbo->glBindFramebuffer(0x8ca9, 0); p->gl->glDrawBuffer((uint32_t)p->colors[i].buffer); blit(p, p->mask);
  }
  fbo->glBindFramebuffer(0x8d40, 0); p->gl->glDrawBuffer((uint32_t)buffer); fbo->glReadBuffer((uint32_t)buffer);
}

void qa_gl_output_gamma_close(qa_gl_output_gamma *g) {
  gimpl *p = G(g); if (p == NULL) return;
  p->fbo.glBindFramebuffer(0x8d40, 0);
  for (size_t i = 0; i < p->color_count; i++) p->gl->glDeleteTextures(1, &p->colors[i].name);
  p->color_count = 0; p->gl->glDeleteTextures(1, p->depth); p->gl->glDeleteTextures(1, p->table);
  p->fbo.glDeleteFramebuffers(1, p->target);
  p->shaders.glDeleteProgram(p->program); qa_gl_programs_close(&p->shaders); qa_gl_framebuffers_close(&p->fbo);
  free(p->colors); free(p); g->impl = NULL;
}
