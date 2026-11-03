/* Port of src/render/gl/fog.ts. GPL-2.0-or-later. */
#include "qa/render/gl/fog.h"
#include "qa/foundation/support.h"
#include "qa/platform/gl_programs.h"
#include "qa/render/gl/fog_shader.h"
#include "qa/render/gl/programs.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FOG_UNIFORM_LIMIT 24
typedef struct fog_program {
  bool compiled; uint32_t name;
  const char *const *names; int32_t locations[FOG_UNIFORM_LIMIT]; size_t count;
} fog_program;
typedef struct fimpl { qa_gl *gl; qa_gl_programs library; fog_program programs[3]; uint32_t texture[1], texture_name; } fimpl;

bool qa_q2_fog_pass_init(qa_q2_fog_pass *pass, const qa_gl_context *context, qa_gl *gl) {
  fimpl *p = (fimpl *)calloc(1, sizeof *p); if (p == NULL) return false;
  p->gl = gl;
  if (!qa_load_gl_programs(context, &p->library)) { free(p); return false; }
  gl->glGenTextures(1, p->texture);
  p->texture_name = p->texture[0];
  if (p->texture_name == 0) {
    qa_gl_programs_close(&p->library);
    qa_range_error("OpenGL could not allocate the fog depth texture");
  }
  pass->impl = p;
  return true;
}

static fog_program *program_for(fimpl *p, int kind) {
  fog_program *cached = &p->programs[kind];
  if (cached->compiled) return cached;
  char *vertex = qa_build_fog_vertex_shader_source(kind), *fragment = qa_build_fog_fragment_shader_source(kind);
  if (vertex == NULL || fragment == NULL) qa_range_error("OpenGL fog shader allocation failed");
  uint32_t name = qa_gl_compile_program(&p->library, vertex, fragment);
  free(vertex); free(fragment);
  size_t count = 0;
  const char *const *names = qa_fog_uniforms_for(kind, &count);
  if (count > FOG_UNIFORM_LIMIT) qa_range_error("OpenGL fog uniform table overflow");
  for (size_t i = 0; i < count; i++) {
    int32_t location = p->library.glGetUniformLocation(name, names[i]);
    if (location < 0) { p->library.glDeleteProgram(name); qa_range_error("OpenGL fog uniform is missing: %s", names[i]); }
    cached->locations[i] = location;
  }
  cached->name = name; cached->names = names; cached->count = count; cached->compiled = true;
  return cached;
}
static int32_t uniform_of(const fog_program *program, const char *name) {
  for (size_t i = 0; i < program->count; i++) if (strcmp(program->names[i], name) == 0) return program->locations[i];
  qa_range_error("OpenGL fog uniform was not linked: %s", name);
}
static bool is_integer(double v) { return isfinite(v) && floor(v) == v; }
static void draw_quad(qa_gl *gl) {
  gl->glBegin(7);
  gl->glTexCoord2f(0, 0); gl->glVertex2f(-1, -1);
  gl->glTexCoord2f(1, 0); gl->glVertex2f(1, -1);
  gl->glTexCoord2f(1, 1); gl->glVertex2f(1, 1);
  gl->glTexCoord2f(0, 1); gl->glVertex2f(-1, 1);
  gl->glEnd();
}

void qa_q2_fog_pass_draw(qa_q2_fog_pass *pass, qa_q2_fog_operation operation, double target_width, double target_height) {
  fimpl *p = (fimpl *)pass->impl;
  const qa_scene_camera *camera = &operation.camera; const qa_scene_fog *fog = &operation.fog;
  double far_depth = operation.far_depth; bool sky_drawn = operation.sky_drawn;
  if (fog->density <= 0 && (fog->height.density <= 0 || fog->height.falloff <= 0) && (fog->sky_factor <= 0 || !sky_drawn)) return;
  qa_rect rect = camera->viewport; double bottom = target_height - rect.y - rect.height;
  if (!is_integer(rect.x) || !is_integer(rect.y) || !is_integer(rect.width) || !is_integer(rect.height) || rect.x < 0 || rect.y < 0
    || rect.width < 1 || rect.height < 1 || rect.x + rect.width > target_width || rect.y + rect.height > target_height)
    qa_range_error("OpenGL fog viewport is outside its render target");
  const double *projection = camera->projection.values;
  if (projection[1] != 0 || projection[2] != 0 || projection[3] != 0 || projection[4] != 0
    || projection[6] != 0 || projection[7] != 0 || projection[8] != 0 || projection[9] != 0
    || projection[12] != 0 || projection[13] != 0
    || projection[0] == 0 || projection[5] == 0 || projection[11] != -1 || projection[15] != 0)
    qa_range_error("Q2 fog requires the source symmetric perspective projection");
  double a = -projection[10], b = -projection[14], tan_x = 1 / projection[0], tan_y = 1 / projection[5];
  qa_gl *gl = p->gl; qa_gl_programs *shader = &p->library;
  int32_t old_program = 0, old_active = 0, old_viewport[4] = {0, 0, 0, 0}, old_scissor[4] = {0, 0, 0, 0}, old_depth_mask = 0, old_source = 0, old_destination = 0, old_texture = 0;
  gl->glGetIntegerv(0x8b8d, &old_program); gl->glGetIntegerv(0x84e0, &old_active);
  gl->glGetIntegerv(0xba2, old_viewport); gl->glGetIntegerv(0xc10, old_scissor);
  gl->glGetIntegerv(0xb72, &old_depth_mask);
  gl->glGetIntegerv(0xbe1, &old_source); gl->glGetIntegerv(0xbe0, &old_destination);
  float old_color[4] = {0, 0, 0, 0}; gl->glGetFloatv(0xb00, old_color);
  static const uint32_t names[7] = {0xb71, 0xb44, 0xbc0, 0xb90, 0x3000, 0xc11, 0xbe2};
  bool enabled[7];
  for (int i = 0; i < 7; i++) enabled[i] = gl->glIsEnabled(names[i]) != 0;
  gl->glActiveTexture(0x84c0);
  gl->glGetIntegerv(0x8069, &old_texture);
  int32_t x = (int32_t)rect.x, y = (int32_t)bottom, w = (int32_t)rect.width, h = (int32_t)rect.height;
  gl->glBindTexture(0xde1, p->texture_name);
  gl->glTexParameteri(0xde1, 0x2801, 0x2600); gl->glTexParameteri(0xde1, 0x2800, 0x2600);
  gl->glTexParameteri(0xde1, 0x2802, 0x812f); gl->glTexParameteri(0xde1, 0x2803, 0x812f);
  gl->glCopyTexImage2D(0xde1, 0, 0x81a6, x, y, w, h, 0);
  gl->glViewport(x, y, w, h); gl->glScissor(x, y, w, h);
  for (int i = 0; i < 5; i++) gl->glDisable(names[i]);
  gl->glEnable(0xc11); gl->glEnable(0xbe2); gl->glBlendFunc(0x302, 0x303);
  gl->glDepthMask(0); gl->glColor4f(1, 1, 1, 1);
  for (int kind = 0; kind < 3; kind++) {
    if ((kind == 0 && fog->density <= 0) || (kind == 1 && (fog->height.density <= 0 || fog->height.falloff <= 0))
      || (kind == 2 && (fog->sky_factor <= 0 || !sky_drawn))) continue;
    const fog_program *program = program_for(p, kind);
    shader->glUseProgram(program->name);
    shader->glUniform1i(uniform_of(program, "u_depth"), 0);
    shader->glUniform1f(uniform_of(program, "u_far_depth"), (float)far_depth);
    if (kind != 2) shader->glUniform4f(uniform_of(program, "u_proj"), (float)a, (float)b, 0, 0);
    if (kind == 0 || kind == 2) {
      shader->glUniform4f(uniform_of(program, "u_fog_color"), (float)fog->color.x, (float)fog->color.y, (float)fog->color.z, (float)(kind == 0 ? fog->density / 64 : fog->sky_factor));
    } else {
      qa_vec3 forward = camera->axis.vectors[0], left = camera->axis.vectors[1], up = camera->axis.vectors[2];
      const qa_q2_height_fog *height = &fog->height;
      shader->glUniform3f(uniform_of(program, "u_vieworg"), (float)camera->origin.x, (float)camera->origin.y, (float)camera->origin.z);
      shader->glUniform3f(uniform_of(program, "u_forward"), (float)forward.x, (float)forward.y, (float)forward.z);
      shader->glUniform3f(uniform_of(program, "u_right"), (float)-left.x, (float)-left.y, (float)-left.z);
      shader->glUniform3f(uniform_of(program, "u_up"), (float)up.x, (float)up.y, (float)up.z);
      shader->glUniform4f(uniform_of(program, "u_tan"), (float)tan_x, (float)tan_y, 0, 0);
      shader->glUniform4f(uniform_of(program, "u_hf_start"), (float)height->start.color.x, (float)height->start.color.y, (float)height->start.color.z, (float)height->start.distance);
      shader->glUniform4f(uniform_of(program, "u_hf_end"), (float)height->end.color.x, (float)height->end.color.y, (float)height->end.color.z, (float)height->end.distance);
      shader->glUniform1f(uniform_of(program, "u_hf_density"), (float)height->density);
      shader->glUniform1f(uniform_of(program, "u_hf_falloff"), (float)height->falloff);
    }
    draw_quad(gl);
  }
  uint32_t error = gl->glGetError();
  if (error != 0) qa_range_error("OpenGL Q2 fog pass failed: 0x%x", error);
  shader->glUseProgram((uint32_t)old_program);
  gl->glBindTexture(0xde1, (uint32_t)old_texture); gl->glActiveTexture((uint32_t)old_active);
  gl->glDepthMask((uint8_t)old_depth_mask); gl->glBlendFunc((uint32_t)old_source, (uint32_t)old_destination);
  gl->glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
  gl->glScissor(old_scissor[0], old_scissor[1], old_scissor[2], old_scissor[3]);
  gl->glColor4f(old_color[0], old_color[1], old_color[2], old_color[3]);
  for (int i = 0; i < 7; i++) { if (enabled[i]) gl->glEnable(names[i]); else gl->glDisable(names[i]); }
}

void qa_q2_fog_pass_close(qa_q2_fog_pass *pass) {
  fimpl *p = (fimpl *)pass->impl; if (p == NULL) return;
  for (int i = 0; i < 3; i++) if (p->programs[i].compiled) p->library.glDeleteProgram(p->programs[i].name);
  p->gl->glDeleteTextures(1, p->texture);
  qa_gl_programs_close(&p->library);
  free(p); pass->impl = NULL;
}
