/* Port of src/render/gl/depth-atlas.ts. GPL-2.0-or-later. */
#include "qa/render/gl/depth_atlas.h"
#include "qa/foundation/support.h"
#include "qa/platform/gl_framebuffers.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERTEX_CAPACITY 65536u
#define INDEX_CAPACITY 196608u

typedef struct dimpl {
  qa_gl *gl; qa_gl_stage_program *program; qa_gl_framebuffers library;
  uint32_t handle[1], name;
  float *positions; uint32_t *indices;
  bool primitive_restart; int32_t texture_coordinates;
} dimpl;

bool qa_depth_atlas_target_init(qa_depth_atlas_target *t, const qa_gl_context *context, qa_gl *gl, qa_gl_stage_program *program) {
  dimpl *p = (dimpl *)calloc(1, sizeof *p); if (p == NULL) return false;
  p->positions = (float *)malloc(VERTEX_CAPACITY * 4 * sizeof *p->positions);
  p->indices = (uint32_t *)malloc(INDEX_CAPACITY * sizeof *p->indices);
  if (p->positions == NULL || p->indices == NULL) { free(p->positions); free(p->indices); free(p); return false; }
  p->gl = gl; p->program = program;
  if (!qa_load_gl_framebuffers(context, &p->library)) { free(p->positions); free(p->indices); free(p); return false; }
  /* /^(\d+)\.(\d+)/ over String(glGetString(GL_VERSION)); no match reads as 0.0. */
  const char *version = gl->glGetString(0x1f02);
  int major = 0, minor = 0;
  if (version != NULL) {
    const char *c = version; int a = 0, b = 0; bool has_a = false, has_b = false;
    while (*c >= '0' && *c <= '9') { a = a * 10 + (*c++ - '0'); has_a = true; }
    if (has_a && *c == '.') {
      c++;
      while (*c >= '0' && *c <= '9') { b = b * 10 + (*c++ - '0'); has_b = true; }
      if (has_b) { major = a; minor = b; }
    }
  }
  p->primitive_restart = major > 3 || (major == 3 && minor >= 1);
  gl->glGetIntegerv(0x8871, &p->texture_coordinates);
  p->library.glGenFramebuffers(1, p->handle);
  p->name = p->handle[0];
  if (p->name == 0) {
    qa_gl_framebuffers_close(&p->library);
    qa_range_error("OpenGL could not allocate a shadow framebuffer");
  }
  t->impl = p;
  return true;
}

static bool is_integer(double v) { return isfinite(v) && floor(v) == v; }
static void validate_draw(const qa_depth_atlas_draw *draw) {
  if (draw->indices.count % 3 != 0 || draw->position_count % 4 != 0) qa_range_error("OpenGL depth atlas triangles are incomplete");
  /* The running maximum magnitude stays under the float32 overflow midpoint
   * exactly when every component rounds to a finite float32. */
  double worst = 0;
  for (size_t offset = 0; offset < draw->position_count; offset++) {
    double magnitude = fabs(draw->positions[offset]);
    if (worst == worst && !(magnitude <= worst)) worst = magnitude;
  }
  if (!(worst < 3.4028235677973366e38)) qa_range_error("OpenGL depth atlas vertices must be finite float32");
  double vertex_count = (double)(draw->position_count / 4);
  for (size_t offset = 0; offset < draw->indices.count; offset++) {
    double index = draw->indices.values[offset];
    /* (index | 0) !== index: not a signed 32-bit integer. */
    if (!(index >= 0) || index >= vertex_count || index > 2147483647.0 || floor(index) != index)
      qa_range_error("OpenGL depth atlas vertex index is invalid");
  }
}
/* Object.is over two numbers. */
static bool same_number(double a, double b) { return memcmp(&a, &b, sizeof a) == 0 || (a != a && b != b); }

typedef struct run { dimpl *p; size_t vertex_count, index_count; } run;
static void flush_run(run *r) {
  if (r->index_count != 0) r->p->gl->glDrawElements(4, (int32_t)r->index_count, 0x1405, r->p->indices);
  r->vertex_count = 0; r->index_count = 0;
}

void qa_depth_atlas_target_draw(qa_depth_atlas_target *t, double texture, double width, double height, const qa_depth_atlas_pass *passes, size_t pass_count) {
  dimpl *p = (dimpl *)t->impl; qa_gl *gl = p->gl; qa_gl_framebuffers *fbo = &p->library;
  for (size_t i = 0; i < pass_count; i++) {
    qa_rect v = passes[i].viewport;
    if (!is_integer(v.x) || !is_integer(v.y) || !is_integer(v.width) || !is_integer(v.height)
      || v.x < 0 || v.y < 0 || v.width <= 0 || v.height <= 0
      || v.x + v.width > width || v.y + v.height > height)
      qa_range_error("OpenGL depth atlas viewport is outside its image");
    if (passes[i].has_clear_depth && (!isfinite(passes[i].clear_depth) || passes[i].clear_depth < 0 || passes[i].clear_depth > 1))
      qa_range_error("OpenGL depth atlas clear must be in 0..1");
    for (size_t j = 0; j < passes[i].draw_count; j++) validate_draw(&passes[i].draws[j]);
  }
  int32_t old_target = 0, old_read_target = 0, old_draw_buffer = 0, old_read_buffer = 0;
  gl->glGetIntegerv(0x8ca6, &old_target); gl->glGetIntegerv(0x8caa, &old_read_target);
  gl->glGetIntegerv(0xc01, &old_draw_buffer); gl->glGetIntegerv(0xc02, &old_read_buffer);
  int32_t viewport[4] = {0, 0, 0, 0}, scissor[4] = {0, 0, 0, 0}, color_mask[4] = {0, 0, 0, 0}, polygon_mode[2] = {0, 0};
  gl->glGetIntegerv(0xba2, viewport); gl->glGetIntegerv(0xc10, scissor); gl->glGetIntegerv(0xc23, color_mask);
  float depth_range[2] = {0, 0}, depth_clear = 0, offset_factor = 0, offset_units = 0;
  gl->glGetFloatv(0xb70, depth_range); gl->glGetFloatv(0xb73, &depth_clear);
  int32_t depth_mask = 0, depth_function = 0, cull = 0, program = 0, stack = 0, stack_max = 0;
  gl->glGetIntegerv(0xb72, &depth_mask); gl->glGetIntegerv(0xb74, &depth_function);
  gl->glGetIntegerv(0xb45, &cull); gl->glGetIntegerv(0xb40, polygon_mode);
  gl->glGetFloatv(0x8038, &offset_factor); gl->glGetFloatv(0x2a00, &offset_units);
  gl->glGetIntegerv(0x8b8d, &program);
  static const uint32_t enable_names[9] = {0xb71, 0xb44, 0xbe2, 0xb90, 0xbc0, 0x3000, 0xc11, 0x8037, 0x8f9d};
  int enable_count = p->primitive_restart ? 9 : 8;
  bool enabled[9];
  for (int i = 0; i < enable_count; i++) enabled[i] = gl->glIsEnabled(enable_names[i]) != 0;
  gl->glGetIntegerv(0xbb1, &stack); gl->glGetIntegerv(0xd3b, &stack_max);
  if (stack >= stack_max) qa_range_error("OpenGL client attribute stack is full");
  gl->glPushClientAttrib(2);
  gl->glBindBuffer(0x8892, 0); gl->glBindBuffer(0x8893, 0);
  static const uint32_t client_states[6] = {0x8075, 0x8076, 0x8077, 0x8079, 0x8457, 0x845e};
  for (int i = 0; i < 6; i++) gl->glDisableClientState(client_states[i]);
  for (int32_t unit = 0; unit < p->texture_coordinates; unit++) {
    gl->glClientActiveTexture(0x84c0u + (uint32_t)unit); gl->glDisableClientState(0x8078);
  }
  gl->glEnableClientState(0x8074); gl->glVertexPointer(4, 0x1406, 0, p->positions);
  if (p->primitive_restart) gl->glDisable(0x8f9d);
  fbo->glBindFramebuffer(0x8d40, p->name);
  fbo->glFramebufferTexture2D(0x8d40, 0x8d00, 0xde1, (uint32_t)texture, 0);
  gl->glDrawBuffer(0); fbo->glReadBuffer(0);
  uint32_t status = fbo->glCheckFramebufferStatus(0x8d40);
  if (status != 0x8cd5) qa_range_error("OpenGL shadow framebuffer is incomplete: 0x%x", status);
  qa_gl_stage_program_use_depth(p->program);
  gl->glEnable(0xb71); gl->glDepthMask(1); gl->glDepthFunc(0x203); gl->glDepthRange(0, 1);
  gl->glEnable(0xc11); gl->glColorMask(0, 0, 0, 0); gl->glPolygonMode(0x408, 0x1b02);
  static const uint32_t off_names[4] = {0xbe2, 0xb90, 0xbc0, 0x3000};
  for (int i = 0; i < 4; i++) gl->glDisable(off_names[i]);
  for (size_t i = 0; i < pass_count; i++) {
    const qa_depth_atlas_pass *pass = &passes[i];
    qa_rect rect = pass->viewport;
    gl->glViewport((int32_t)rect.x, (int32_t)rect.y, (int32_t)rect.width, (int32_t)rect.height);
    gl->glScissor((int32_t)rect.x, (int32_t)rect.y, (int32_t)rect.width, (int32_t)rect.height);
    if (pass->has_clear_depth) { gl->glClearDepth(pass->clear_depth); gl->glClear(0x100); }
    const qa_depth_atlas_draw *previous = NULL;
    run r; r.p = p; r.vertex_count = 0; r.index_count = 0;
    for (size_t j = 0; j < pass->draw_count; j++) {
      const qa_depth_atlas_draw *draw = &pass->draws[j];
      bool same = previous != NULL && strcmp(previous->cull, draw->cull) == 0
        && (previous->polygon_offset == NULL ? draw->polygon_offset == NULL
            : draw->polygon_offset != NULL && same_number(previous->polygon_offset->factor, draw->polygon_offset->factor)
              && same_number(previous->polygon_offset->units, draw->polygon_offset->units));
      if (!same) {
        flush_run(&r);
        if (strcmp(draw->cull, "none") == 0) gl->glDisable(0xb44);
        else { gl->glEnable(0xb44); gl->glCullFace(strcmp(draw->cull, "front") == 0 ? 0x404 : 0x405); }
        if (draw->polygon_offset == NULL) gl->glDisable(0x8037);
        else { gl->glEnable(0x8037); gl->glPolygonOffset((float)draw->polygon_offset->factor, (float)draw->polygon_offset->units); }
        previous = draw;
      }
      size_t draw_vertices = draw->position_count / 4;
      if (draw_vertices <= VERTEX_CAPACITY && draw->indices.count <= INDEX_CAPACITY) {
        if (r.vertex_count + draw_vertices > VERTEX_CAPACITY || r.index_count + draw->indices.count > INDEX_CAPACITY) flush_run(&r);
        size_t base = r.vertex_count;
        for (size_t k = 0; k < draw->position_count; k++) p->positions[base * 4 + k] = (float)draw->positions[k];
        r.vertex_count += draw_vertices;
        for (size_t k = 0; k < draw->indices.count; k++) p->indices[r.index_count++] = (uint32_t)(base + (size_t)draw->indices.values[k]);
      } else {
        flush_run(&r);
        for (size_t triangle = 0; triangle < draw->indices.count; triangle += 3) {
          if (r.vertex_count + 3 > VERTEX_CAPACITY || r.index_count + 3 > INDEX_CAPACITY) flush_run(&r);
          for (size_t corner = 0; corner < 3; corner++) {
            size_t source = (size_t)draw->indices.values[triangle + corner] * 4;
            p->indices[r.index_count++] = (uint32_t)r.vertex_count;
            size_t offset = r.vertex_count++ * 4;
            for (size_t c = 0; c < 4; c++) p->positions[offset + c] = (float)draw->positions[source + c];
          }
        }
      }
    }
    flush_run(&r);
  }
  uint32_t error = gl->glGetError();
  if (error != 0) qa_range_error("OpenGL shadow atlas draw failed: 0x%x", error);
  gl->glPopClientAttrib();
  fbo->glFramebufferTexture2D(0x8d40, 0x8d00, 0xde1, 0, 0);
  fbo->glBindFramebuffer(0x8ca9, (uint32_t)old_target);
  fbo->glBindFramebuffer(0x8ca8, (uint32_t)old_read_target);
  gl->glDrawBuffer((uint32_t)old_draw_buffer); fbo->glReadBuffer((uint32_t)old_read_buffer);
  gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  gl->glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
  gl->glColorMask((uint8_t)color_mask[0], (uint8_t)color_mask[1], (uint8_t)color_mask[2], (uint8_t)color_mask[3]);
  gl->glDepthRange(depth_range[0], depth_range[1]); gl->glClearDepth(depth_clear);
  gl->glDepthMask((uint8_t)depth_mask); gl->glDepthFunc((uint32_t)depth_function); gl->glCullFace((uint32_t)cull);
  gl->glPolygonMode(0x404, (uint32_t)polygon_mode[0]); gl->glPolygonMode(0x405, (uint32_t)polygon_mode[1]);
  gl->glPolygonOffset(offset_factor, offset_units);
  for (int i = 0; i < enable_count; i++) { if (enabled[i]) gl->glEnable(enable_names[i]); else gl->glDisable(enable_names[i]); }
  qa_gl_stage_program_restore(p->program, program);
}

void qa_depth_atlas_target_close(qa_depth_atlas_target *t) {
  dimpl *p = (dimpl *)t->impl; if (p == NULL) return;
  p->library.glDeleteFramebuffers(1, p->handle); qa_gl_framebuffers_close(&p->library);
  free(p->positions); free(p->indices); free(p); t->impl = NULL;
}
