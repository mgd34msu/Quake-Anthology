/* Recording fake GL for tests/render/test_*_top.c, the approach of test_gl_depth.c:
 * function pointers carry no ctx, so state is static. Every entry point of qa_gl,
 * qa_gl_programs and qa_gl_framebuffers is counted and logged; those with GL-visible
 * results also model it. Generated from the three headers. GPL-2.0-or-later. */
#ifndef TESTS_RENDER_FAKE_GL_TOP_H
#define TESTS_RENDER_FAKE_GL_TOP_H
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "qa/platform/gl.h"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define FAKE_KEYS 128
typedef struct fake_gl_state {
  uint32_t keys[FAKE_KEYS]; int32_t values[FAKE_KEYS][4]; int key_count;
  uint32_t caps[FAKE_KEYS]; int cap_count;
  int32_t bindings[8]; uint32_t next_name; uint32_t status; uint32_t pending_error;
  char uniforms[64][40]; int uniform_count;
  int32_t attachment_component;
  /* call trace */
  char log[1 << 18]; size_t log_used;
  struct { const char *name; int count; } counts[256]; int count_used;
  uint32_t last_args[4];
  pthread_t thread; bool thread_set, wrong_thread;
  float last_viewport_unused;
} fake_gl_state;
static fake_gl_state FG;
static int32_t *fake_int(uint32_t key) {
  for (int i = 0; i < FG.key_count; i++) if (FG.keys[i] == key) return FG.values[i];
  FG.keys[FG.key_count] = key; memset(FG.values[FG.key_count], 0, sizeof FG.values[0]); return FG.values[FG.key_count++];
}
static bool fake_enabled(uint32_t cap) { for (int i = 0; i < FG.cap_count; i++) if (FG.caps[i] == cap) return true; return false; }
static void fake_cap(uint32_t cap, bool on) {
  for (int i = 0; i < FG.cap_count; i++) if (FG.caps[i] == cap) { if (!on) FG.caps[i] = FG.caps[--FG.cap_count]; return; }
  if (on) FG.caps[FG.cap_count++] = cap;
}
static int fake_unit(void) { int u = fake_int(0x84e0)[0] - 0x84c0; return u >= 0 && u < 8 ? u : 0; }
static void fake_note(const char *name) {
  if (!FG.thread_set) { FG.thread = pthread_self(); FG.thread_set = true; }
  else if (!pthread_equal(FG.thread, pthread_self())) FG.wrong_thread = true;
  int i = 0;
  for (; i < FG.count_used; i++) if (FG.counts[i].name == name || strcmp(FG.counts[i].name, name) == 0) break;
  if (i == FG.count_used && i < 256) { FG.counts[i].name = name; FG.counts[i].count = 0; FG.count_used++; }
  if (i < 256) FG.counts[i].count++;
  size_t n = strlen(name);
  if (FG.log_used + n + 2 < sizeof FG.log) { memcpy(FG.log + FG.log_used, name, n); FG.log_used += n; FG.log[FG.log_used++] = ' '; FG.log[FG.log_used] = 0; }
}
static int fake_count(const char *name) { for (int i = 0; i < FG.count_used; i++) if (strcmp(FG.counts[i].name, name) == 0) return FG.counts[i].count; return 0; }
static void fake_clear_trace(void) { FG.log_used = 0; FG.log[0] = 0; FG.count_used = 0; }
static void fake_reset(void) {
  memset(&FG, 0, sizeof FG);
  FG.next_name = 100; FG.status = 0x8cd5; FG.attachment_component = 0x8c17;
  *fake_int(0xd57) = 8; *fake_int(0xd56) = 24; *fake_int(0xd52) = 8; *fake_int(0xd53) = 8; *fake_int(0xd54) = 8; *fake_int(0xd55) = 8;
  *fake_int(0xd33) = 1024; *fake_int(0x8872) = 8; *fake_int(0x8871) = 8; *fake_int(0xc33) = 0; *fake_int(0xd3b) = 16; *fake_int(0x84e0) = 0x84c0;
}
static void w_glCallList(uint32_t list) {
  fake_note("glCallList");
  
}
static void w_glNewList(uint32_t list, uint32_t mode) {
  fake_note("glNewList");
  
}
static void w_glEndList(void) {
  fake_note("glEndList");
  
}
static void w_glDeleteLists(uint32_t list, int32_t range) {
  fake_note("glDeleteLists");
  
}
static const char * w_glGetString(uint32_t name) {
  fake_note("glGetString");
  return name == 0x1f02 ? "4.6 fake" : "fake";
}
static uint32_t w_glGetError(void) {
  fake_note("glGetError");
  { uint32_t e = FG.pending_error; FG.pending_error = 0; return e; }
  return 0;
}
static void w_glHint(uint32_t target, uint32_t mode) {
  fake_note("glHint");
  
}
static uint8_t w_glIsEnabled(uint32_t cap) {
  fake_note("glIsEnabled");
  return fake_enabled(cap) ? 1 : 0;
}
static void w_glGetIntegerv(uint32_t pname, int32_t *params) {
  fake_note("glGetIntegerv");
  if (pname == 0x8069) { params[0] = FG.bindings[fake_unit()]; return; } { int32_t *v = fake_int(pname); int n = (pname == 0xba2 || pname == 0xc10 || pname == 0xc23) ? 4 : pname == 0xb40 ? 2 : 1; for (int i = 0; i < n; i++) params[i] = v[i]; }
}
static void w_glGetFloatv(uint32_t pname, float *params) {
  fake_note("glGetFloatv");
  
}
static void w_glGetTexParameterfv(uint32_t target, uint32_t pname, float *params) {
  fake_note("glGetTexParameterfv");
  
}
static void w_glGetTexLevelParameteriv(uint32_t target, int32_t level, uint32_t pname, int32_t *params) {
  fake_note("glGetTexLevelParameteriv");
  
}
static void w_glGetTexImage(uint32_t target, int32_t level, uint32_t format, uint32_t type, void *pixels) {
  fake_note("glGetTexImage");
  
}
static void w_glActiveTexture(uint32_t texture) {
  fake_note("glActiveTexture");
  fake_int(0x84e0)[0] = (int32_t)texture;
}
static void w_glClientActiveTexture(uint32_t texture) {
  fake_note("glClientActiveTexture");
  
}
static void w_glDrawBuffer(uint32_t buf) {
  fake_note("glDrawBuffer");
  fake_int(0xc01)[0] = (int32_t)buf;
}
static void w_glViewport(int32_t x, int32_t y, int32_t width, int32_t height) {
  fake_note("glViewport");
  { int32_t *v = fake_int(0xba2); v[0] = x; v[1] = y; v[2] = width; v[3] = height; }
}
static void w_glScissor(int32_t x, int32_t y, int32_t width, int32_t height) {
  fake_note("glScissor");
  { int32_t *v = fake_int(0xc10); v[0] = x; v[1] = y; v[2] = width; v[3] = height; }
}
static void w_glClearColor(float red, float green, float blue, float alpha) {
  fake_note("glClearColor");
  
}
static void w_glClearDepth(double depth) {
  fake_note("glClearDepth");
  
}
static void w_glClearStencil(int32_t s) {
  fake_note("glClearStencil");
  
}
static void w_glClear(uint32_t mask) {
  fake_note("glClear");
  
}
static void w_glEnable(uint32_t cap) {
  fake_note("glEnable");
  fake_cap(cap, true);
}
static void w_glDisable(uint32_t cap) {
  fake_note("glDisable");
  fake_cap(cap, false);
}
static void w_glClipPlane(uint32_t plane, const double *equation) {
  fake_note("glClipPlane");
  
}
static void w_glDepthFunc(uint32_t func) {
  fake_note("glDepthFunc");
  fake_int(0xb74)[0] = (int32_t)func;
}
static void w_glDepthMask(uint8_t flag) {
  fake_note("glDepthMask");
  fake_int(0xb72)[0] = flag;
}
static void w_glColorMask(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
  fake_note("glColorMask");
  { int32_t *v = fake_int(0xc23); v[0] = red; v[1] = green; v[2] = blue; v[3] = alpha; }
}
static void w_glStencilFunc(uint32_t func, int32_t ref, uint32_t mask) {
  fake_note("glStencilFunc");
  
}
static void w_glStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass) {
  fake_note("glStencilOp");
  
}
static void w_glStencilMask(uint32_t mask) {
  fake_note("glStencilMask");
  
}
static void w_glDepthRange(double near, double far) {
  fake_note("glDepthRange");
  
}
static void w_glPolygonMode(uint32_t face, uint32_t mode) {
  fake_note("glPolygonMode");
  { int32_t *v = fake_int(0xb40); v[0] = (int32_t)mode; v[1] = (int32_t)mode; }
}
static void w_glShadeModel(uint32_t mode) {
  fake_note("glShadeModel");
  
}
static void w_glPolygonOffset(float factor, float units) {
  fake_note("glPolygonOffset");
  
}
static void w_glLineWidth(float width) {
  fake_note("glLineWidth");
  
}
static void w_glBlendFunc(uint32_t sfactor, uint32_t dfactor) {
  fake_note("glBlendFunc");
  
}
static void w_glAlphaFunc(uint32_t func, float ref) {
  fake_note("glAlphaFunc");
  
}
static void w_glCullFace(uint32_t mode) {
  fake_note("glCullFace");
  FG.last_args[0] = mode;
}
static void w_glFrontFace(uint32_t mode) {
  fake_note("glFrontFace");
  
}
static void w_glMatrixMode(uint32_t mode) {
  fake_note("glMatrixMode");
  
}
static void w_glLoadIdentity(void) {
  fake_note("glLoadIdentity");
  
}
static void w_glLoadMatrixf(const float *m) {
  fake_note("glLoadMatrixf");
  
}
static void w_glOrtho(double left, double right, double bottom, double top, double near, double far) {
  fake_note("glOrtho");
  
}
static void w_glBegin(uint32_t mode) {
  fake_note("glBegin");
  
}
static void w_glEnd(void) {
  fake_note("glEnd");
  
}
static void w_glColor3f(float red, float green, float blue) {
  fake_note("glColor3f");
  
}
static void w_glColor4f(float red, float green, float blue, float alpha) {
  fake_note("glColor4f");
  
}
static void w_glColor4b(int8_t red, int8_t green, int8_t blue, int8_t alpha) {
  fake_note("glColor4b");
  
}
static void w_glColor4ub(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
  fake_note("glColor4ub");
  
}
static void w_glTexCoord2f(float s, float t) {
  fake_note("glTexCoord2f");
  
}
static void w_glVertex2f(float x, float y) {
  fake_note("glVertex2f");
  
}
static void w_glVertex4f(float x, float y, float z, float w) {
  fake_note("glVertex4f");
  
}
static void w_glEnableClientState(uint32_t array) {
  fake_note("glEnableClientState");
  
}
static void w_glDisableClientState(uint32_t array) {
  fake_note("glDisableClientState");
  
}
static void w_glPushClientAttrib(uint32_t mask) {
  fake_note("glPushClientAttrib");
  
}
static void w_glPopClientAttrib(void) {
  fake_note("glPopClientAttrib");
  
}
static void w_glBindBuffer(uint32_t target, uint32_t buffer) {
  fake_note("glBindBuffer");
  
}
static void w_glGenBuffers(int32_t n, uint32_t *buffers) {
  fake_note("glGenBuffers");
  
}
static void w_glDeleteBuffers(int32_t n, const uint32_t *buffers) {
  fake_note("glDeleteBuffers");
  
}
static void w_glGetPointerv(uint32_t pname, void **params) {
  fake_note("glGetPointerv");
  
}
static void w_glVertexPointer(int32_t size, uint32_t type, int32_t stride, const void *pointer) {
  fake_note("glVertexPointer");
  
}
static void w_glColorPointer(int32_t size, uint32_t type, int32_t stride, const void *pointer) {
  fake_note("glColorPointer");
  
}
static void w_glTexCoordPointer(int32_t size, uint32_t type, int32_t stride, const void *pointer) {
  fake_note("glTexCoordPointer");
  
}
static void w_glDrawElements(uint32_t mode, int32_t count, uint32_t type, const void *indices) {
  fake_note("glDrawElements");
  
}
static void w_glArrayElement(int32_t i) {
  fake_note("glArrayElement");
  
}
static void w_glGenTextures(int32_t n, uint32_t *textures) {
  fake_note("glGenTextures");
  for (int i = 0; i < n; i++) textures[i] = ++FG.next_name;
}
static void w_glDeleteTextures(int32_t n, const uint32_t *textures) {
  fake_note("glDeleteTextures");
  
}
static void w_glBindTexture(uint32_t target, uint32_t texture) {
  fake_note("glBindTexture");
  FG.bindings[fake_unit()] = (int32_t)texture; 
}
static void w_glTexParameteri(uint32_t target, uint32_t pname, int32_t param) {
  fake_note("glTexParameteri");
  
}
static void w_glTexParameterfv(uint32_t target, uint32_t pname, const float *params) {
  fake_note("glTexParameterfv");
  
}
static void w_glTexEnvi(uint32_t target, uint32_t pname, int32_t param) {
  fake_note("glTexEnvi");
  
}
static void w_glTexEnvf(uint32_t target, uint32_t pname, float param) {
  fake_note("glTexEnvf");
  
}
static void w_glTexImage2D(uint32_t target, int32_t level, int32_t internalformat, int32_t width, int32_t height, int32_t border, uint32_t format, uint32_t type, const void *pixels) {
  fake_note("glTexImage2D");
  
}
static void w_glCopyTexImage2D(uint32_t target, int32_t level, uint32_t internalformat, int32_t x, int32_t y, int32_t width, int32_t height, int32_t border) {
  fake_note("glCopyTexImage2D");
  
}
static void w_glTexSubImage2D(uint32_t target, int32_t level, int32_t xoffset, int32_t yoffset, int32_t width, int32_t height, uint32_t format, uint32_t type, const void *pixels) {
  fake_note("glTexSubImage2D");
  
}
static void w_glFinish(void) {
  fake_note("glFinish");
  
}
static void w_glPixelStorei(uint32_t pname, int32_t param) {
  fake_note("glPixelStorei");
  
}
static void w_glReadPixels(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format, uint32_t type, void *pixels) {
  fake_note("glReadPixels");
  if (format == 0x1908) { uint8_t *p = (uint8_t *)pixels; for (int32_t r = 0; r < height; r++) for (int32_t c = 0; c < width * 4; c++) p[r * width * 4 + c] = (uint8_t)r; } else if (format == 0x1902) ((float *)pixels)[0] = 0.5f; else memset(pixels, 7, (size_t)(width * height));
}
static uint32_t w_glCreateShader(uint32_t type) {
  fake_note("glCreateShader");
  return ++FG.next_name;
}
static void w_glShaderSource(uint32_t shader, int32_t count, const char *const *string, const int32_t *length) {
  fake_note("glShaderSource");
  
}
static void w_glCompileShader(uint32_t shader) {
  fake_note("glCompileShader");
  
}
static void w_glGetShaderiv(uint32_t shader, uint32_t pname, int32_t *params) {
  fake_note("glGetShaderiv");
  params[0] = 1;
}
static void w_glGetShaderInfoLog(uint32_t shader, int32_t buf_size, int32_t *length, char *info_log) {
  fake_note("glGetShaderInfoLog");
  
}
static void w_glDeleteShader(uint32_t shader) {
  fake_note("glDeleteShader");
  
}
static uint32_t w_glCreateProgram(void) {
  fake_note("glCreateProgram");
  return ++FG.next_name;
}
static void w_glAttachShader(uint32_t program, uint32_t shader) {
  fake_note("glAttachShader");
  
}
static void w_glLinkProgram(uint32_t program) {
  fake_note("glLinkProgram");
  
}
static void w_glGetProgramiv(uint32_t program, uint32_t pname, int32_t *params) {
  fake_note("glGetProgramiv");
  params[0] = 1;
}
static void w_glGetProgramInfoLog(uint32_t program, int32_t buf_size, int32_t *length, char *info_log) {
  fake_note("glGetProgramInfoLog");
  
}
static void w_glDeleteProgram(uint32_t program) {
  fake_note("glDeleteProgram");
  
}
static void w_glUseProgram(uint32_t program) {
  fake_note("glUseProgram");
  fake_int(0x8b8d)[0] = (int32_t)program;
}
static int32_t w_glGetUniformLocation(uint32_t program, const char *name) {
  fake_note("glGetUniformLocation");
  for (int i = 0; i < FG.uniform_count; i++) { if (strcmp(FG.uniforms[i], name) == 0) return i; }
  if (FG.uniform_count < 64) { snprintf(FG.uniforms[FG.uniform_count], sizeof FG.uniforms[0], "%s", name); return FG.uniform_count++; } return 63;
  return 0;
}
static void w_glUniform1i(int32_t location, int32_t v0) {
  fake_note("glUniform1i");
  
}
static void w_glUniform1f(int32_t location, float v0) {
  fake_note("glUniform1f");
  
}
static void w_glUniform3f(int32_t location, float v0, float v1, float v2) {
  fake_note("glUniform3f");
  
}
static void w_glUniform4f(int32_t location, float v0, float v1, float v2, float v3) {
  fake_note("glUniform4f");
  
}
static void w_glUniformMatrix4fv(int32_t location, int32_t count, uint8_t transpose, const float *value) {
  fake_note("glUniformMatrix4fv");
  
}
static void w_glGenFramebuffers(int32_t n, uint32_t *ids) {
  fake_note("glGenFramebuffers");
  for (int i = 0; i < n; i++) ids[i] = ++FG.next_name;
}
static void w_glDeleteFramebuffers(int32_t n, const uint32_t *ids) {
  fake_note("glDeleteFramebuffers");
  
}
static void w_glBindFramebuffer(uint32_t target, uint32_t framebuffer) {
  fake_note("glBindFramebuffer");
  if (target == 0x8d40 || target == 0x8ca9) { fake_int(0x8ca6)[0] = (int32_t)framebuffer; }
  if (target == 0x8d40 || target == 0x8ca8) { fake_int(0x8caa)[0] = (int32_t)framebuffer; }
}
static void w_glFramebufferTexture2D(uint32_t target, uint32_t attachment, uint32_t textarget, uint32_t texture, int32_t level) {
  fake_note("glFramebufferTexture2D");
  
}
static uint32_t w_glCheckFramebufferStatus(uint32_t target) {
  fake_note("glCheckFramebufferStatus");
  return FG.status;
}
static void w_glBlitFramebuffer(int32_t src_x0, int32_t src_y0, int32_t src_x1, int32_t src_y1, int32_t dst_x0, int32_t dst_y0, int32_t dst_x1, int32_t dst_y1, uint32_t mask, uint32_t filter) {
  fake_note("glBlitFramebuffer");
  
}
static void w_glGetFramebufferAttachmentParameteriv(uint32_t target, uint32_t attachment, uint32_t pname, int32_t *params) {
  fake_note("glGetFramebufferAttachmentParameteriv");
  params[0] = FG.attachment_component;
}
static void w_glReadBuffer(uint32_t src) {
  fake_note("glReadBuffer");
  fake_int(0xc02)[0] = (int32_t)src;
}
typedef void (*fake_any_fn)(void);
typedef struct fake_named { const char *name; fake_any_fn fn; } fake_named;
static const fake_named FAKE_TABLE[] = {
  {"glCallList", (fake_any_fn)w_glCallList},
  {"glNewList", (fake_any_fn)w_glNewList},
  {"glEndList", (fake_any_fn)w_glEndList},
  {"glDeleteLists", (fake_any_fn)w_glDeleteLists},
  {"glGetString", (fake_any_fn)w_glGetString},
  {"glGetError", (fake_any_fn)w_glGetError},
  {"glHint", (fake_any_fn)w_glHint},
  {"glIsEnabled", (fake_any_fn)w_glIsEnabled},
  {"glGetIntegerv", (fake_any_fn)w_glGetIntegerv},
  {"glGetFloatv", (fake_any_fn)w_glGetFloatv},
  {"glGetTexParameterfv", (fake_any_fn)w_glGetTexParameterfv},
  {"glGetTexLevelParameteriv", (fake_any_fn)w_glGetTexLevelParameteriv},
  {"glGetTexImage", (fake_any_fn)w_glGetTexImage},
  {"glActiveTexture", (fake_any_fn)w_glActiveTexture},
  {"glClientActiveTexture", (fake_any_fn)w_glClientActiveTexture},
  {"glDrawBuffer", (fake_any_fn)w_glDrawBuffer},
  {"glViewport", (fake_any_fn)w_glViewport},
  {"glScissor", (fake_any_fn)w_glScissor},
  {"glClearColor", (fake_any_fn)w_glClearColor},
  {"glClearDepth", (fake_any_fn)w_glClearDepth},
  {"glClearStencil", (fake_any_fn)w_glClearStencil},
  {"glClear", (fake_any_fn)w_glClear},
  {"glEnable", (fake_any_fn)w_glEnable},
  {"glDisable", (fake_any_fn)w_glDisable},
  {"glClipPlane", (fake_any_fn)w_glClipPlane},
  {"glDepthFunc", (fake_any_fn)w_glDepthFunc},
  {"glDepthMask", (fake_any_fn)w_glDepthMask},
  {"glColorMask", (fake_any_fn)w_glColorMask},
  {"glStencilFunc", (fake_any_fn)w_glStencilFunc},
  {"glStencilOp", (fake_any_fn)w_glStencilOp},
  {"glStencilMask", (fake_any_fn)w_glStencilMask},
  {"glDepthRange", (fake_any_fn)w_glDepthRange},
  {"glPolygonMode", (fake_any_fn)w_glPolygonMode},
  {"glShadeModel", (fake_any_fn)w_glShadeModel},
  {"glPolygonOffset", (fake_any_fn)w_glPolygonOffset},
  {"glLineWidth", (fake_any_fn)w_glLineWidth},
  {"glBlendFunc", (fake_any_fn)w_glBlendFunc},
  {"glAlphaFunc", (fake_any_fn)w_glAlphaFunc},
  {"glCullFace", (fake_any_fn)w_glCullFace},
  {"glFrontFace", (fake_any_fn)w_glFrontFace},
  {"glMatrixMode", (fake_any_fn)w_glMatrixMode},
  {"glLoadIdentity", (fake_any_fn)w_glLoadIdentity},
  {"glLoadMatrixf", (fake_any_fn)w_glLoadMatrixf},
  {"glOrtho", (fake_any_fn)w_glOrtho},
  {"glBegin", (fake_any_fn)w_glBegin},
  {"glEnd", (fake_any_fn)w_glEnd},
  {"glColor3f", (fake_any_fn)w_glColor3f},
  {"glColor4f", (fake_any_fn)w_glColor4f},
  {"glColor4b", (fake_any_fn)w_glColor4b},
  {"glColor4ub", (fake_any_fn)w_glColor4ub},
  {"glTexCoord2f", (fake_any_fn)w_glTexCoord2f},
  {"glVertex2f", (fake_any_fn)w_glVertex2f},
  {"glVertex4f", (fake_any_fn)w_glVertex4f},
  {"glEnableClientState", (fake_any_fn)w_glEnableClientState},
  {"glDisableClientState", (fake_any_fn)w_glDisableClientState},
  {"glPushClientAttrib", (fake_any_fn)w_glPushClientAttrib},
  {"glPopClientAttrib", (fake_any_fn)w_glPopClientAttrib},
  {"glBindBuffer", (fake_any_fn)w_glBindBuffer},
  {"glGenBuffers", (fake_any_fn)w_glGenBuffers},
  {"glDeleteBuffers", (fake_any_fn)w_glDeleteBuffers},
  {"glGetPointerv", (fake_any_fn)w_glGetPointerv},
  {"glVertexPointer", (fake_any_fn)w_glVertexPointer},
  {"glColorPointer", (fake_any_fn)w_glColorPointer},
  {"glTexCoordPointer", (fake_any_fn)w_glTexCoordPointer},
  {"glDrawElements", (fake_any_fn)w_glDrawElements},
  {"glArrayElement", (fake_any_fn)w_glArrayElement},
  {"glGenTextures", (fake_any_fn)w_glGenTextures},
  {"glDeleteTextures", (fake_any_fn)w_glDeleteTextures},
  {"glBindTexture", (fake_any_fn)w_glBindTexture},
  {"glTexParameteri", (fake_any_fn)w_glTexParameteri},
  {"glTexParameterfv", (fake_any_fn)w_glTexParameterfv},
  {"glTexEnvi", (fake_any_fn)w_glTexEnvi},
  {"glTexEnvf", (fake_any_fn)w_glTexEnvf},
  {"glTexImage2D", (fake_any_fn)w_glTexImage2D},
  {"glCopyTexImage2D", (fake_any_fn)w_glCopyTexImage2D},
  {"glTexSubImage2D", (fake_any_fn)w_glTexSubImage2D},
  {"glFinish", (fake_any_fn)w_glFinish},
  {"glPixelStorei", (fake_any_fn)w_glPixelStorei},
  {"glReadPixels", (fake_any_fn)w_glReadPixels},
  {"glCreateShader", (fake_any_fn)w_glCreateShader},
  {"glShaderSource", (fake_any_fn)w_glShaderSource},
  {"glCompileShader", (fake_any_fn)w_glCompileShader},
  {"glGetShaderiv", (fake_any_fn)w_glGetShaderiv},
  {"glGetShaderInfoLog", (fake_any_fn)w_glGetShaderInfoLog},
  {"glDeleteShader", (fake_any_fn)w_glDeleteShader},
  {"glCreateProgram", (fake_any_fn)w_glCreateProgram},
  {"glAttachShader", (fake_any_fn)w_glAttachShader},
  {"glLinkProgram", (fake_any_fn)w_glLinkProgram},
  {"glGetProgramiv", (fake_any_fn)w_glGetProgramiv},
  {"glGetProgramInfoLog", (fake_any_fn)w_glGetProgramInfoLog},
  {"glDeleteProgram", (fake_any_fn)w_glDeleteProgram},
  {"glUseProgram", (fake_any_fn)w_glUseProgram},
  {"glGetUniformLocation", (fake_any_fn)w_glGetUniformLocation},
  {"glUniform1i", (fake_any_fn)w_glUniform1i},
  {"glUniform1f", (fake_any_fn)w_glUniform1f},
  {"glUniform3f", (fake_any_fn)w_glUniform3f},
  {"glUniform4f", (fake_any_fn)w_glUniform4f},
  {"glUniformMatrix4fv", (fake_any_fn)w_glUniformMatrix4fv},
  {"glGenFramebuffers", (fake_any_fn)w_glGenFramebuffers},
  {"glDeleteFramebuffers", (fake_any_fn)w_glDeleteFramebuffers},
  {"glBindFramebuffer", (fake_any_fn)w_glBindFramebuffer},
  {"glFramebufferTexture2D", (fake_any_fn)w_glFramebufferTexture2D},
  {"glCheckFramebufferStatus", (fake_any_fn)w_glCheckFramebufferStatus},
  {"glBlitFramebuffer", (fake_any_fn)w_glBlitFramebuffer},
  {"glGetFramebufferAttachmentParameteriv", (fake_any_fn)w_glGetFramebufferAttachmentParameteriv},
  {"glReadBuffer", (fake_any_fn)w_glReadBuffer},
};
static void *fake_gl_proc_lookup(const char *name) {
  for (size_t i = 0; i < sizeof FAKE_TABLE / sizeof FAKE_TABLE[0]; i++) if (strcmp(FAKE_TABLE[i].name, name) == 0) { void *p; memcpy(&p, &FAKE_TABLE[i].fn, sizeof p); return p; }
  return NULL;
}
#endif
