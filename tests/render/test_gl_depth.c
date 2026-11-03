/*
 * GL logic checks for src/render/gl/depth-atlas.ts, object-opacity.ts,
 * output-gamma.ts and fog.ts. The TS tests/render/gl/smoke.test.ts runs these
 * against a live context only under QUAKE_GL_SMOKE=1. Here a recording fake GL
 * carries the same assertions: draw call counts and vertex/index payloads, and
 * full state restoration, plus branch and error paths. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/core/math.h"
#include "qa/platform/gl.h"
#include "qa/render/gl/depth_atlas.h"
#include "qa/render/gl/fog.h"
#include "qa/render/gl/object_opacity.h"
#include "qa/render/gl/output_gamma.h"
#include "qa/render/gl/programs.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
/* Aborting child whose stderr must contain the TS message. */
#define CHECK_THROWS(part, ...) do { checks++; int pipe_[2]; if (pipe(pipe_) != 0) abort(); pid_t pid_ = fork(); \
  if (pid_ == 0) { (void)prctl(PR_SET_DUMPABLE, 0); close(pipe_[0]); dup2(pipe_[1], 2); __VA_ARGS__; _exit(0); } \
  close(pipe_[1]); char text_[2048]; size_t n_ = 0; ssize_t got_; while (n_ + 1 < sizeof text_ && (got_ = read(pipe_[0], text_ + n_, sizeof text_ - 1 - n_)) > 0) n_ += (size_t)got_; text_[n_] = 0; close(pipe_[0]); \
  int status_ = 0; waitpid(pid_, &status_, 0); if (!WIFSIGNALED(status_) || strstr(text_, part) == NULL) { failures++; fprintf(stderr, "FAIL %s:%d: expected throw \"%s\", got \"%s\"\n", __FILE__, __LINE__, part, text_); } } while (0)

/* --- recording fake GL: function pointers have no ctx, so state is static --- */
#define MAX_KEYS 128
typedef struct fake_state {
  uint32_t int_keys[MAX_KEYS]; int32_t ints[MAX_KEYS][4]; int int_count;
  uint32_t float_keys[MAX_KEYS]; float floats[MAX_KEYS][4]; int float_count;
  uint32_t on_keys[MAX_KEYS]; int on_count;
  int32_t texture_binding[8];
  double clear_depth;
} fake_state;
static fake_state S;
static char version_string[32];
static uint32_t next_name;
static uint32_t pending_error, status_result;
static int32_t uniform_result;
/* trace */
static int draw_calls, clears, begins, blits, copies, deleted_textures, deleted_programs, deleted_framebuffers, tex_images, gen_fail;
static int last_draw_count; static float *vertex_pointer; static const uint32_t *last_indices;
static uint32_t last_blit_mask, draw_buffer_log[32]; static int draw_buffer_log_count;
static float last_uniform4[32][4], last_uniform1[32]; static int32_t uniform_ids;
static char uniform_names[32][32];
static int compiled_programs;
static float first_vertex[4], last_vertex[4];
static uint32_t last_tex_internal, last_tex_format, last_tex_type;
static void *fake_ctx_seen;
static int retains, releases;

static int32_t *ints_of(uint32_t key) {
  for (int i = 0; i < S.int_count; i++) if (S.int_keys[i] == key) return S.ints[i];
  S.int_keys[S.int_count] = key; memset(S.ints[S.int_count], 0, sizeof S.ints[0]); return S.ints[S.int_count++];
}
static float *floats_of(uint32_t key) {
  for (int i = 0; i < S.float_count; i++) if (S.float_keys[i] == key) return S.floats[i];
  S.float_keys[S.float_count] = key; memset(S.floats[S.float_count], 0, sizeof S.floats[0]); return S.floats[S.float_count++];
}
static bool enabled_now(uint32_t cap) { for (int i = 0; i < S.on_count; i++) if (S.on_keys[i] == cap) return true; return false; }
static void set_cap(uint32_t cap, bool on) {
  for (int i = 0; i < S.on_count; i++) if (S.on_keys[i] == cap) { if (!on) S.on_keys[i] = S.on_keys[--S.on_count]; return; }
  if (on) S.on_keys[S.on_count++] = cap;
}
static int unit_index(void) { return ints_of(0x84e0)[0] - 0x84c0; }
static void f_get_integerv(uint32_t name, int32_t *out) {
  if (name == 0x8069) { out[0] = S.texture_binding[unit_index() >= 0 ? unit_index() : 0]; return; }
  int32_t *v = ints_of(name);
  int n = (name == 0xba2 || name == 0xc10 || name == 0xc23) ? 4 : name == 0xb40 ? 2 : 1;
  for (int i = 0; i < n; i++) out[i] = v[i];
}
static void f_get_floatv(uint32_t name, float *out) {
  float *v = floats_of(name); int n = name == 0xb00 ? 4 : name == 0xb70 ? 2 : 1;
  for (int i = 0; i < n; i++) out[i] = v[i];
}
static uint8_t f_is_enabled(uint32_t cap) { return enabled_now(cap) ? 1 : 0; }
static void f_enable(uint32_t cap) { set_cap(cap, true); }
static int restart_disables;
static void f_disable(uint32_t cap) { if (cap == 0x8f9d) restart_disables++; set_cap(cap, false); }
static const char *f_get_string(uint32_t name) { (void)name; return version_string[0] ? version_string : NULL; }
static uint32_t f_get_error(void) { uint32_t e = pending_error; pending_error = 0; return e; }
static void f_active_texture(uint32_t t) { ints_of(0x84e0)[0] = (int32_t)t; }
static void f_client_active(uint32_t t) { ints_of(0x84e1)[0] = (int32_t)t; }
static void f_draw_buffer(uint32_t b) { ints_of(0xc01)[0] = (int32_t)b; if (draw_buffer_log_count < 32) draw_buffer_log[draw_buffer_log_count++] = b; }
static void f_viewport(int32_t x, int32_t y, int32_t w, int32_t h) { int32_t *v = ints_of(0xba2); v[0] = x; v[1] = y; v[2] = w; v[3] = h; }
static void f_scissor(int32_t x, int32_t y, int32_t w, int32_t h) { int32_t *v = ints_of(0xc10); v[0] = x; v[1] = y; v[2] = w; v[3] = h; }
static void f_clear_depth(double d) { S.clear_depth = d; floats_of(0xb73)[0] = (float)d; }
static void f_clear(uint32_t m) { (void)m; clears++; }
static void f_depth_func(uint32_t f) { ints_of(0xb74)[0] = (int32_t)f; }
static void f_depth_mask(uint8_t m) { ints_of(0xb72)[0] = m; }
static void f_color_mask(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { int32_t *v = ints_of(0xc23); v[0] = a; v[1] = b; v[2] = c; v[3] = d; }
static void f_depth_range(double n, double f) { float *v = floats_of(0xb70); v[0] = (float)n; v[1] = (float)f; }
static void f_polygon_mode(uint32_t face, uint32_t mode) { int32_t *v = ints_of(0xb40); if (face == 0x404 || face == 0x408) v[0] = (int32_t)mode; if (face == 0x405 || face == 0x408) v[1] = (int32_t)mode; }
static void f_polygon_offset(float a, float b) { floats_of(0x8038)[0] = a; floats_of(0x2a00)[0] = b; }
static void f_blend_func(uint32_t a, uint32_t b) { ints_of(0xbe1)[0] = (int32_t)a; ints_of(0xbe0)[0] = (int32_t)b; }
static void f_cull_face(uint32_t m) { ints_of(0xb45)[0] = (int32_t)m; }
static void f_begin(uint32_t m) { (void)m; begins++; }
static void f_end(void) {}
static void f_color4f(float a, float b, float c, float d) { float *v = floats_of(0xb00); v[0] = a; v[1] = b; v[2] = c; v[3] = d; }
static void f_tex_coord2f(float a, float b) { (void)a; (void)b; }
static void f_vertex2f(float a, float b) { (void)a; (void)b; }
static void f_vertex4f(float a, float b, float c, float d) { (void)a; (void)b; (void)c; (void)d; }
static void f_enable_client(uint32_t a) { set_cap(0x100000u | a, true); }
static void f_disable_client(uint32_t a) { set_cap(0x100000u | a, false); }
static void f_push_client(uint32_t m) { (void)m; ints_of(0xbb1)[0]++; }
static void f_pop_client(void) { ints_of(0xbb1)[0]--; }
static void f_bind_buffer(uint32_t t, uint32_t b) { ints_of(t == 0x8892 ? 0x8894 : 0x8895)[0] = (int32_t)b; }
static void f_vertex_pointer(int32_t a, uint32_t b, int32_t c, const void *p) { (void)a; (void)b; (void)c; vertex_pointer = (float *)(uintptr_t)p; }
static void f_draw_elements(uint32_t m, int32_t n, uint32_t t, const void *i) {
  (void)m; (void)t; draw_calls++; last_draw_count = n; last_indices = (const uint32_t *)i;
  memcpy(first_vertex, vertex_pointer + last_indices[0] * 4, sizeof first_vertex);
  memcpy(last_vertex, vertex_pointer + last_indices[n - 1] * 4, sizeof last_vertex);
}
static void f_gen_textures(int32_t n, uint32_t *o) { for (int i = 0; i < n; i++) o[i] = gen_fail ? 0 : ++next_name; }
static void f_delete_textures(int32_t n, const uint32_t *o) { (void)o; deleted_textures += n; }
static void f_bind_texture(uint32_t t, uint32_t n) { (void)t; S.texture_binding[unit_index() >= 0 ? unit_index() : 0] = (int32_t)n; }
static void f_tex_parameteri(uint32_t a, uint32_t b, int32_t c) { (void)a; (void)b; (void)c; }
static void f_tex_image2d(uint32_t t, int32_t l, int32_t i, int32_t w, int32_t h, int32_t b, uint32_t f, uint32_t ty, const void *p) { (void)t; (void)l; (void)w; (void)h; (void)b; (void)p; tex_images++; last_tex_internal = (uint32_t)i; last_tex_format = f; last_tex_type = ty; }
static void f_copy_tex_image2d(uint32_t t, int32_t l, uint32_t i, int32_t x, int32_t y, int32_t w, int32_t h, int32_t b) { (void)t; (void)l; (void)i; (void)x; (void)y; (void)w; (void)h; (void)b; copies++; }
static void f_pixel_storei(uint32_t n, int32_t v) { ints_of(n)[0] = v; }
/* framebuffers */
static void f_gen_framebuffers(int32_t n, uint32_t *o) { for (int i = 0; i < n; i++) o[i] = gen_fail ? 0 : ++next_name; }
static void f_delete_framebuffers(int32_t n, const uint32_t *o) { (void)o; deleted_framebuffers += n; }
static void f_bind_framebuffer(uint32_t t, uint32_t n) {
  if (t == 0x8d40 || t == 0x8ca9) ints_of(0x8ca6)[0] = (int32_t)n;
  if (t == 0x8d40 || t == 0x8ca8) ints_of(0x8caa)[0] = (int32_t)n;
}
static void f_framebuffer_tex(uint32_t a, uint32_t b, uint32_t c, uint32_t d, int32_t e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
static uint32_t f_check_status(uint32_t t) { (void)t; return status_result; }
static void f_blit(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f, int32_t g, int32_t h, uint32_t m, uint32_t fi) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)fi; blits++; last_blit_mask = m; }
static int32_t attachment_component = 0x8c17;
static void f_fb_attachment(uint32_t a, uint32_t b, uint32_t c, int32_t *o) { (void)a; (void)b; (void)c; o[0] = attachment_component; }
static void f_read_buffer(uint32_t b) { ints_of(0xc02)[0] = (int32_t)b; }
/* programs */
static uint32_t f_create_shader(uint32_t t) { (void)t; return ++next_name; }
static void f_shader_source(uint32_t s, int32_t c, const char *const *t, const int32_t *l) { (void)s; (void)c; (void)t; (void)l; }
static void f_compile_shader(uint32_t s) { (void)s; }
static void f_get_shaderiv(uint32_t s, uint32_t p, int32_t *o) { (void)s; (void)p; o[0] = 1; }
static void f_shader_log(uint32_t s, int32_t n, int32_t *l, char *o) { (void)s; (void)n; (void)l; (void)o; }
static void f_delete_shader(uint32_t s) { (void)s; }
static uint32_t f_create_program(void) { compiled_programs++; return ++next_name; }
static void f_attach(uint32_t p, uint32_t s) { (void)p; (void)s; }
static void f_link(uint32_t p) { (void)p; }
static void f_get_programiv(uint32_t p, uint32_t n, int32_t *o) { (void)p; (void)n; o[0] = 1; }
static void f_delete_program(uint32_t p) { (void)p; deleted_programs++; }
static void f_use_program(uint32_t p) { ints_of(0x8b8d)[0] = (int32_t)p; }
static int32_t f_get_uniform(uint32_t p, const char *n) {
  (void)p;
  if (uniform_result < 0 && strcmp(n, uniform_names[31]) == 0) return -1;
  for (int i = 0; i < uniform_ids; i++) if (strcmp(uniform_names[i], n) == 0) return i;
  if (uniform_ids >= 31) return 30;
  snprintf(uniform_names[uniform_ids], sizeof uniform_names[0], "%s", n);
  return uniform_ids++;
}
static void f_uniform1i(int32_t l, int32_t v) { (void)l; (void)v; }
static void f_uniform1f(int32_t l, float v) { last_uniform1[l] = v; }
static void f_uniform3f(int32_t l, float a, float b, float c) { last_uniform4[l][0] = a; last_uniform4[l][1] = b; last_uniform4[l][2] = c; }
static void f_uniform4f(int32_t l, float a, float b, float c, float d) { last_uniform4[l][0] = a; last_uniform4[l][1] = b; last_uniform4[l][2] = c; last_uniform4[l][3] = d; }
static void f_uniform_matrix(int32_t l, int32_t c, uint8_t t, const float *v) { (void)l; (void)c; (void)t; (void)v; }

#define SENTINEL_CTX ((void *)&S)
typedef void (*any_fn)(void);
typedef struct named { const char *name; any_fn fn; } named;
#define E(n, f) {n, (any_fn)f}
static const named TABLE[] = {
  E("glGetIntegerv", f_get_integerv), E("glGetFloatv", f_get_floatv), E("glIsEnabled", f_is_enabled), E("glEnable", f_enable), E("glDisable", f_disable),
  E("glGetString", f_get_string), E("glGetError", f_get_error), E("glActiveTexture", f_active_texture), E("glClientActiveTexture", f_client_active),
  E("glDrawBuffer", f_draw_buffer), E("glViewport", f_viewport), E("glScissor", f_scissor), E("glClearDepth", f_clear_depth), E("glClear", f_clear),
  E("glDepthFunc", f_depth_func), E("glDepthMask", f_depth_mask), E("glColorMask", f_color_mask), E("glDepthRange", f_depth_range), E("glPolygonMode", f_polygon_mode),
  E("glPolygonOffset", f_polygon_offset), E("glBlendFunc", f_blend_func), E("glCullFace", f_cull_face), E("glBegin", f_begin), E("glEnd", f_end), E("glColor4f", f_color4f),
  E("glTexCoord2f", f_tex_coord2f), E("glVertex2f", f_vertex2f), E("glVertex4f", f_vertex4f), E("glEnableClientState", f_enable_client), E("glDisableClientState", f_disable_client),
  E("glPushClientAttrib", f_push_client), E("glPopClientAttrib", f_pop_client), E("glBindBuffer", f_bind_buffer), E("glVertexPointer", f_vertex_pointer), E("glDrawElements", f_draw_elements),
  E("glGenTextures", f_gen_textures), E("glDeleteTextures", f_delete_textures), E("glBindTexture", f_bind_texture), E("glTexParameteri", f_tex_parameteri), E("glTexImage2D", f_tex_image2d),
  E("glCopyTexImage2D", f_copy_tex_image2d), E("glPixelStorei", f_pixel_storei),
  E("glGenFramebuffers", f_gen_framebuffers), E("glDeleteFramebuffers", f_delete_framebuffers), E("glBindFramebuffer", f_bind_framebuffer), E("glFramebufferTexture2D", f_framebuffer_tex),
  E("glCheckFramebufferStatus", f_check_status), E("glBlitFramebuffer", f_blit), E("glGetFramebufferAttachmentParameteriv", f_fb_attachment), E("glReadBuffer", f_read_buffer),
  E("glCreateShader", f_create_shader), E("glShaderSource", f_shader_source), E("glCompileShader", f_compile_shader), E("glGetShaderiv", f_get_shaderiv), E("glGetShaderInfoLog", f_shader_log),
  E("glDeleteShader", f_delete_shader), E("glCreateProgram", f_create_program), E("glAttachShader", f_attach), E("glLinkProgram", f_link), E("glGetProgramiv", f_get_programiv),
  E("glGetProgramInfoLog", f_shader_log), E("glDeleteProgram", f_delete_program), E("glUseProgram", f_use_program), E("glGetUniformLocation", f_get_uniform),
  E("glUniform1i", f_uniform1i), E("glUniform1f", f_uniform1f), E("glUniform3f", f_uniform3f), E("glUniform4f", f_uniform4f), E("glUniformMatrix4fv", f_uniform_matrix),
};
static void *fake_proc(void *ctx, const char *name) {
  fake_ctx_seen = ctx;
  CHECK(ctx == SENTINEL_CTX);
  for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++) if (strcmp(TABLE[i].name, name) == 0) { void *p; memcpy(&p, &TABLE[i].fn, sizeof p); return p; }
  return NULL;
}
static void fake_release(void *ctx) { CHECK(ctx == SENTINEL_CTX); releases++; }
static bool fake_retain(void *ctx, qa_gl_release_fn *out, void **out_ctx) { CHECK(ctx == SENTINEL_CTX); retains++; *out = fake_release; *out_ctx = ctx; return true; }
static void fake_current(void *ctx) { CHECK(ctx == SENTINEL_CTX); }
static qa_gl_context fake_context(void) { qa_gl_context c; c.ctx = SENTINEL_CTX; c.get_gl_proc_address = fake_proc; c.retain_procedures = fake_retain; c.make_current = fake_current; return c; }

static void reset_fake(const char *version) {
  memset(&S, 0, sizeof S);
  snprintf(version_string, sizeof version_string, "%s", version);
  next_name = 100; pending_error = 0; status_result = 0x8cd5; uniform_result = 0; uniform_ids = 0; gen_fail = 0;
  draw_calls = clears = begins = blits = copies = deleted_textures = deleted_programs = deleted_framebuffers = tex_images = 0;
  draw_buffer_log_count = 0; compiled_programs = 0; retains = releases = 0;
  memset(uniform_names, 0, sizeof uniform_names);
  /* Distinct non-default state so a missing restore is visible. */
  ints_of(0x8871)[0] = 8;
  ints_of(0xd3b)[0] = 16;
  ints_of(0x84e0)[0] = 0x84c2;
  ints_of(0xc01)[0] = 0x405; ints_of(0xc02)[0] = 0x404;
  ints_of(0x8ca6)[0] = 7; ints_of(0x8caa)[0] = 9;
  int32_t *v = ints_of(0xba2); v[0] = 1; v[1] = 2; v[2] = 300; v[3] = 200;
  v = ints_of(0xc10); v[0] = 3; v[1] = 4; v[2] = 100; v[3] = 90;
  v = ints_of(0xc23); v[0] = 1; v[1] = 0; v[2] = 1; v[3] = 0;
  ints_of(0xb72)[0] = 1; ints_of(0xb74)[0] = 0x202; ints_of(0xb45)[0] = 0x405;
  v = ints_of(0xb40); v[0] = 0x1b01; v[1] = 0x1b02;
  ints_of(0x8b8d)[0] = 55; ints_of(0xbe1)[0] = 0x302; ints_of(0xbe0)[0] = 1;
  float *f = floats_of(0xb70); f[0] = 0.25f; f[1] = 0.75f;
  floats_of(0xb73)[0] = 0.5f; floats_of(0x8038)[0] = 2; floats_of(0x2a00)[0] = 3;
  f = floats_of(0xb00); f[0] = 0.2f; f[1] = 0.4f; f[2] = 0.6f; f[3] = 0.8f;
  S.texture_binding[0] = 11; S.texture_binding[1] = 12; S.texture_binding[2] = 13;
  set_cap(0xb71, true); set_cap(0xc11, true); set_cap(0xbe2, true); set_cap(0x8037, true); set_cap(0xb44, true);
}
/* Compare everything but the keys a GL call legitimately leaves behind. */
static bool state_equal(const fake_state *a, const fake_state *b, const uint32_t *ignored, size_t ignored_count) {
  for (int i = 0; i < a->int_count; i++) {
    uint32_t key = a->int_keys[i]; bool skip = false;
    for (size_t k = 0; k < ignored_count; k++) if (ignored[k] == key) skip = true;
    if (skip) continue;
    for (int j = 0; j < b->int_count; j++) if (b->int_keys[j] == key && memcmp(a->ints[i], b->ints[j], sizeof a->ints[0]) != 0) { fprintf(stderr, "int state 0x%x changed: %d -> %d\n", key, a->ints[i][0], b->ints[j][0]); return false; }
  }
  for (int i = 0; i < a->float_count; i++) for (int j = 0; j < b->float_count; j++)
    if (b->float_keys[j] == a->float_keys[i] && memcmp(a->floats[i], b->floats[j], sizeof a->floats[0]) != 0) { fprintf(stderr, "float state 0x%x changed\n", a->float_keys[i]); return false; }
  for (int i = 0; i < a->on_count; i++) if (a->on_keys[i] < 0x100000u) { bool found = false; for (int j = 0; j < b->on_count; j++) if (b->on_keys[j] == a->on_keys[i]) found = true; if (!found) { fprintf(stderr, "cap 0x%x lost\n", a->on_keys[i]); return false; } }
  for (int j = 0; j < b->on_count; j++) if (b->on_keys[j] < 0x100000u) { bool found = false; for (int i = 0; i < a->on_count; i++) if (a->on_keys[i] == b->on_keys[j]) found = true; if (!found) { fprintf(stderr, "cap 0x%x gained\n", b->on_keys[j]); return false; } }
  for (int i = 0; i < 8; i++) if (a->texture_binding[i] != b->texture_binding[i]) { fprintf(stderr, "texture unit %d changed\n", i); return false; }
  return true;
}
static bool expect_restored(const fake_state *before) {
  /* client attributes are popped by the driver in real GL; the fake leaves them. */
  static const uint32_t ignored[] = {0x84e1, 0x8894, 0x8895};
  return state_equal(before, &S, ignored, 3);
}

typedef struct gl_set { qa_gl gl; qa_gl_context context; } gl_set;
static void gl_open(gl_set *s) { s->context = fake_context(); CHECK(qa_load_gl(&s->context, &s->gl)); }

/* --- depth atlas: "indexed depth atlas retains client state, ordered runs and oversized triangles" --- */
static const double QUAD_POS[16] = {-1, -1, -0.5, 1, 1, -1, -0.5, 1, 1, 1, -0.5, 1, -1, 1, -0.5, 1};
static const double QUAD_IDX[6] = {0, 1, 2, 0, 2, 3};
static qa_depth_atlas_draw quad_draw(void) {
  qa_depth_atlas_draw d; memset(&d, 0, sizeof d);
  d.positions = QUAD_POS; d.position_count = 16; d.indices.values = QUAD_IDX; d.indices.count = 6; d.cull = "none";
  return d;
}
static qa_rect R4(double x, double y, double w, double h) { qa_rect r; r.x = x; r.y = y; r.width = w; r.height = h; return r; }
static void test_depth_atlas(void) {
  reset_fake("4.6 Mesa"); gl_set g; gl_open(&g);
  qa_gl_stage_program program; CHECK(qa_gl_stage_program_init(&program, &g.context));
  ints_of(0x8b8d)[0] = 55;
  qa_depth_atlas_target target; CHECK(qa_depth_atlas_target_init(&target, &g.context, &g.gl, &program));
  set_cap(0x8f9d, true);
  fake_state before = S;
  qa_depth_atlas_draw quad = quad_draw(), back = quad_draw(), offset = quad_draw();
  back.cull = "back"; qa_polygon_offset po = {1, 2}; offset.polygon_offset = &po;
  qa_depth_atlas_draw draws[4] = {quad, quad, back, offset};
  qa_depth_atlas_pass passes[2]; memset(passes, 0, sizeof passes);
  passes[0].viewport = R4(0, 0, 16, 16); passes[0].has_clear_depth = true; passes[0].clear_depth = 1; passes[0].draws = draws; passes[0].draw_count = 4;
  passes[1].viewport = R4(0, 0, 8, 8); passes[1].has_clear_depth = true; passes[1].clear_depth = 0.5;
  draw_calls = 0;
  qa_depth_atlas_target_draw(&target, 5, 16, 16, passes, 2);
  CHECK(draw_calls == 3); /* two equal draws merge; cull and offset each flush */
  CHECK(clears == 2);
  CHECK(expect_restored(&before));
  CHECK(ints_of(0xbb1)[0] == 0);
  /* float32 rounding of the packed positions */
  CHECK(first_vertex[0] == -1.0f && first_vertex[2] == -0.5f);

  /* large draws: exactly at capacity stay indexed, one flush per draw */
  size_t large_count = 65536u * 4;
  double *large = (double *)malloc(large_count * sizeof *large);
  for (size_t i = 0; i < 65536; i++) memcpy(large + i * 4, QUAD_POS + (i % 4) * 4, 4 * sizeof *large);
  qa_depth_atlas_draw big = quad_draw(); big.positions = large; big.position_count = large_count;
  qa_depth_atlas_draw two[2] = {big, big};
  qa_depth_atlas_pass pass; memset(&pass, 0, sizeof pass);
  pass.viewport = R4(0, 0, 16, 16); pass.has_clear_depth = true; pass.clear_depth = 1; pass.draws = two; pass.draw_count = 2;
  draw_calls = 0;
  qa_depth_atlas_target_draw(&target, 5, 16, 16, &pass, 1);
  CHECK(draw_calls == 2);
  /* base-relative second draw */
  CHECK(last_indices[0] == 0);
  /* one vertex over capacity expands triangles */
  double *plus = (double *)malloc((large_count + 4) * sizeof *plus);
  memcpy(plus, large, large_count * sizeof *plus);
  plus[large_count] = 0; plus[large_count + 1] = 0; plus[large_count + 2] = 0; plus[large_count + 3] = 1;
  qa_depth_atlas_draw plus_draw = quad_draw(); plus_draw.positions = plus; plus_draw.position_count = large_count + 4;
  pass.draws = &plus_draw; pass.draw_count = 1;
  draw_calls = 0;
  qa_depth_atlas_target_draw(&target, 5, 16, 16, &pass, 1);
  CHECK(draw_calls == 1 && last_draw_count == 6);
  /* oversized indices: 65537 triangles split at the 65536 vertex capacity */
  static const double oversized_pos[24] = {-2, -2, 0, 1, -2, -2, 0, 1, -2, -2, 0, 1, -1, -1, -0.5, 1, 3, -1, -0.5, 1, -1, 3, -0.5, 1};
  size_t over_count = 196611;
  double *over = (double *)malloc(over_count * sizeof *over);
  for (size_t i = 0; i < over_count; i++) over[i] = i < 196608 ? (double)(i % 3) : (double)(i % 3 + 3);
  qa_depth_atlas_draw oversized = quad_draw(); oversized.positions = oversized_pos; oversized.position_count = 24; oversized.indices.values = over; oversized.indices.count = over_count;
  pass.draws = &oversized;
  draw_calls = 0;
  qa_depth_atlas_target_draw(&target, 5, 16, 16, &pass, 1);
  CHECK(draw_calls == 4);
  CHECK(last_vertex[0] == -1.0f && last_vertex[1] == 3.0f); /* final triangle is the real one */
  CHECK(expect_restored(&before));
  free(large); free(plus); free(over);

  /* validation precedes any GL mutation */
  double bad_idx[3] = {0, 1, 99}; qa_depth_atlas_draw bad = quad_draw(); bad.indices.values = bad_idx; bad.indices.count = 3;
  qa_depth_atlas_pass good; memset(&good, 0, sizeof good); good.viewport = R4(0, 0, 16, 16); good.has_clear_depth = true; good.clear_depth = 1; good.draws = &quad; good.draw_count = 1;
  qa_depth_atlas_pass with_bad[2] = {good, good}; with_bad[1].draws = &bad;
  CHECK_THROWS("vertex index is invalid", qa_depth_atlas_target_draw(&target, 5, 16, 16, with_bad, 2));
  bad = quad_draw(); bad.indices.count = 5;
  CHECK_THROWS("triangles are incomplete", qa_depth_atlas_target_draw(&target, 5, 16, 16, with_bad, 2));
  bad = quad_draw(); bad.position_count = 15;
  CHECK_THROWS("triangles are incomplete", qa_depth_atlas_target_draw(&target, 5, 16, 16, with_bad, 2));
  static const double huge[4] = {1e39, 0, 0, 1}; static const double nan_pos[4] = {NAN, 0, 0, 1}; static const double one_idx[3] = {0, 0, 0};
  bad = quad_draw(); bad.positions = huge; bad.position_count = 4; bad.indices.values = one_idx; bad.indices.count = 3;
  CHECK_THROWS("finite float32", qa_depth_atlas_target_draw(&target, 5, 16, 16, with_bad, 2));
  bad.positions = nan_pos;
  CHECK_THROWS("finite float32", qa_depth_atlas_target_draw(&target, 5, 16, 16, with_bad, 2));
  double frac_idx[3] = {0.5, 0, 0}; bad.positions = QUAD_POS; bad.position_count = 16; bad.indices.values = frac_idx;
  CHECK_THROWS("vertex index is invalid", qa_depth_atlas_target_draw(&target, 5, 16, 16, with_bad, 2));
  qa_depth_atlas_pass outside = good; outside.viewport = R4(8, 8, 9, 8);
  CHECK_THROWS("outside its image", qa_depth_atlas_target_draw(&target, 5, 16, 16, &outside, 1));
  outside.viewport = R4(0, 0, 0.5, 8);
  CHECK_THROWS("outside its image", qa_depth_atlas_target_draw(&target, 5, 16, 16, &outside, 1));
  qa_depth_atlas_pass clear_bad = good; clear_bad.clear_depth = 1.5;
  CHECK_THROWS("clear must be in 0..1", qa_depth_atlas_target_draw(&target, 5, 16, 16, &clear_bad, 1));
  /* runtime failures */
  ints_of(0xbb1)[0] = 16;
  CHECK_THROWS("client attribute stack is full", qa_depth_atlas_target_draw(&target, 5, 16, 16, &good, 1));
  ints_of(0xbb1)[0] = 0;
  status_result = 0x8cd6;
  CHECK_THROWS("shadow framebuffer is incomplete: 0x8cd6", qa_depth_atlas_target_draw(&target, 5, 16, 16, &good, 1));
  status_result = 0x8cd5; pending_error = 0x502;
  CHECK_THROWS("shadow atlas draw failed: 0x502", qa_depth_atlas_target_draw(&target, 5, 16, 16, &good, 1));
  pending_error = 0;
  qa_depth_atlas_target_close(&target);
  CHECK(deleted_framebuffers == 1);
  qa_gl_stage_program_close(&program);
  qa_gl_close(&g.gl);
  CHECK(retains == releases);
}
static void test_depth_atlas_versions(void) {
  static const struct { const char *version; bool restart; } cases[] = {{"3.1 x", true}, {"3.0", false}, {"2.1 Mesa", false}, {"4.0", true}, {"bad", false}, {"", false}, {"3", false}};
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    reset_fake(cases[i].version); gl_set g; gl_open(&g);
    qa_gl_stage_program program; CHECK(qa_gl_stage_program_init(&program, &g.context));
    qa_depth_atlas_target target; CHECK(qa_depth_atlas_target_init(&target, &g.context, &g.gl, &program));
    set_cap(0x8f9d, true); restart_disables = 0;
    qa_depth_atlas_draw quad = quad_draw();
    qa_depth_atlas_pass pass; memset(&pass, 0, sizeof pass); pass.viewport = R4(0, 0, 16, 16); pass.draws = &quad; pass.draw_count = 1;
    qa_depth_atlas_target_draw(&target, 5, 16, 16, &pass, 1);
    CHECK(clears == 0); /* null clearDepth skips glClear */
    /* restart capable contexts restore the cap after disabling it; others never touch it */
    CHECK(enabled_now(0x8f9d));
    CHECK(restart_disables == (cases[i].restart ? 1 : 0));
    qa_depth_atlas_target_close(&target); qa_gl_stage_program_close(&program); qa_gl_close(&g.gl);
  }
  reset_fake("3.3"); gl_set g; gl_open(&g);
  qa_gl_stage_program program; CHECK(qa_gl_stage_program_init(&program, &g.context));
  qa_depth_atlas_target target; gen_fail = 1;
  CHECK_THROWS("could not allocate a shadow framebuffer", (void)qa_depth_atlas_target_init(&target, &g.context, &g.gl, &program));
  gen_fail = 0;
  qa_gl_stage_program_close(&program); qa_gl_close(&g.gl);
}

/* --- object opacity --- */
typedef struct opacity_ctx { int sentinel; qa_gl_object_opacity *o; int calls; int32_t target_inside; bool rebind; } opacity_ctx;
static void opacity_child(void *ctx) {
  opacity_ctx *c = (opacity_ctx *)ctx; CHECK(c->sentinel == 0x51);
  c->calls++; c->target_inside = ints_of(0x8ca6)[0];
  CHECK(ints_of(0xc01)[0] == 0x8ce0);
  c->rebind = qa_gl_object_opacity_bind(c->o);
  /* child work disturbs state composite must snapshot, then restore. */
  ints_of(0x8b8d)[0] = 77;
  CHECK_THROWS("Nested OpenGL object opacity", qa_gl_object_opacity_draw(c->o, 0.5, 64, 32, opacity_child, ctx));
}
static void test_object_opacity(void) {
  reset_fake("4.6"); gl_set g; gl_open(&g);
  qa_gl_precision precision; memset(&precision, 0, sizeof precision);
  precision.depth_bits = 24; precision.stencil_bits = 8; precision.color_bits = 24; precision.alpha_bits = 8;
  qa_gl_object_opacity o; CHECK(qa_gl_object_opacity_init(&o, &g.context, &g.gl, precision));
  CHECK(compiled_programs == 1);
  CHECK(qa_gl_object_opacity_bind(&o) == false);
  fake_state before = S;
  opacity_ctx c = {0x51, &o, 0, 0, false};
  blits = 0; begins = 0;
  qa_gl_object_opacity_draw(&o, 0.25, 64, 32, opacity_child, &c);
  CHECK(c.calls == 1 && c.rebind);
  CHECK(c.target_inside != 7);
  CHECK(blits == 2);
  CHECK(last_blit_mask == (0x4000u | 0x100u | 0x400u));
  CHECK(begins == 1);
  CHECK(fabsf(last_uniform1[2] - 0.25f) < 1e-6f); /* opacity uniform */
  /* program restored to what the child left, the rest to the original */
  ints_of(0x8b8d)[0] = 55;
  CHECK(expect_restored(&before));
  CHECK(qa_gl_object_opacity_bind(&o) == false);
  /* allocation is keyed by dimensions: no reallocation at the same size */
  int images = tex_images;
  qa_gl_object_opacity_draw(&o, 0.5, 64, 32, opacity_child, &c);
  CHECK(tex_images == images);
  qa_gl_object_opacity_draw(&o, 0.5, 128, 32, opacity_child, &c);
  CHECK(tex_images == images + 3);
  /* depth format follows precision and floating attachment */
  CHECK(last_tex_internal == 0x88f0 && last_tex_format == 0x84f9 && last_tex_type == 0x84fa);
  attachment_component = 0x1406;
  qa_gl_object_opacity_draw(&o, 0.5, 128, 32, opacity_child, &c);
  CHECK(last_tex_internal == 0x8cad && last_tex_type == 0x8dad);
  attachment_component = 0x8c17;
  /* framebuffers stay valid; errors */
  ints_of(0x80a9)[0] = 4;
  CHECK_THROWS("single-sample framebuffer", qa_gl_object_opacity_draw(&o, 0.5, 128, 32, opacity_child, &c));
  ints_of(0x80a9)[0] = 0;
  pending_error = 0x500;
  CHECK_THROWS("backdrop copy failed: 0x500", qa_gl_object_opacity_draw(&o, 0.5, 128, 32, opacity_child, &c));
  pending_error = 0;
  status_result = 0x8cd6;
  CHECK_THROWS("framebuffer incomplete: 0x8cd6", qa_gl_object_opacity_draw(&o, 0.5, 256, 32, opacity_child, &c));
  status_result = 0x8cd5;
  qa_gl_object_opacity_close(&o);
  CHECK(deleted_textures == 3 && deleted_framebuffers == 2 && deleted_programs == 1);
  /* no-stencil precision uses depth attachment and blit mask */
  reset_fake("4.6"); gl_open(&g);
  precision.stencil_bits = 0; precision.depth_bits = 16; precision.alpha_bits = 0; precision.color_bits = 16;
  CHECK(qa_gl_object_opacity_init(&o, &g.context, &g.gl, precision));
  c.o = &o;
  qa_gl_object_opacity_draw(&o, 0.5, 16, 16, opacity_child, &c);
  CHECK(last_blit_mask == (0x4000u | 0x100u));
  CHECK(last_tex_internal == 0x81a5 && last_tex_format == 0x1902 && last_tex_type == 0x1405);
  gen_fail = 1;
  qa_gl_object_opacity bad;
  CHECK_THROWS("opacity allocation failed", (void)qa_gl_object_opacity_init(&bad, &g.context, &g.gl, precision));
  gen_fail = 0;
  qa_gl_object_opacity_close(&o);
  qa_gl_close(&g.gl);
  CHECK(retains == releases);
}

/* --- output gamma --- */
static void test_output_gamma(void) {
  reset_fake("4.6"); gl_set g; gl_open(&g);
  qa_gl_precision precision; memset(&precision, 0, sizeof precision);
  precision.depth_bits = 24; precision.stencil_bits = 8; precision.color_bits = 24; precision.alpha_bits = 8;
  uint8_t table[256]; for (int i = 0; i < 256; i++) table[i] = (uint8_t)i;
  qa_gl_output_gamma gamma; CHECK(qa_gl_output_gamma_init(&gamma, &g.context, &g.gl, precision, table));
  CHECK(compiled_programs == 1);
  CHECK(tex_images == 1 && last_tex_internal == 0x8040 && last_tex_format == 0x1909); /* table R8 upload */
  CHECK(S.texture_binding[2] == 13 && ints_of(0x84e0)[0] == 0x84c2); /* texture helper restores */
  CHECK(ints_of(0xcf5)[0] == 0); /* pixel store restored */
  blits = 0; tex_images = 0;
  CHECK(qa_gl_output_gamma_bind(&gamma, 0x405, 64, 32)); /* new size: depth + color */
  CHECK(tex_images == 2);
  CHECK(blits == 1 && last_blit_mask == (0x4000u | 0x100u | 0x400u));
  CHECK(ints_of(0xc01)[0] == 0x8ce0 && ints_of(0xc02)[0] == 0x8ce0);
  CHECK(!qa_gl_output_gamma_bind(&gamma, 0x405, 64, 32)); /* cached */
  CHECK(blits == 1);
  CHECK(qa_gl_output_gamma_bind(&gamma, 0x404, 64, 32)); /* second buffer copies only color */
  CHECK(blits == 2 && last_blit_mask == 0x4000u);
  status_result = 0x8cd6;
  CHECK_THROWS("gamma framebuffer incomplete: 0x8cd6", (void)qa_gl_output_gamma_bind(&gamma, 0x402, 64, 32));
  status_result = 0x8cd5;
  /* select default */
  qa_gl_output_gamma_select_default(&gamma, 0x405);
  CHECK(ints_of(0x8ca6)[0] == 0 && ints_of(0xc01)[0] == 0x405 && ints_of(0xc02)[0] == 0x405);
  /* finish draws one quad per raw buffer in insertion order, restores state */
  fake_state before = S;
  begins = 0; draw_buffer_log_count = 0;
  qa_gl_output_gamma_finish(&gamma, 0x405);
  CHECK(begins == 2);
  CHECK(draw_buffer_log[0] == 0x405 && draw_buffer_log[1] == 0x404);
  CHECK(expect_restored(&before));
  CHECK(ints_of(0xc01)[0] == 0x405 && ints_of(0xc02)[0] == 0x405);
  /* restore blits every raw buffer with the full mask */
  blits = 0; draw_buffer_log_count = 0;
  qa_gl_output_gamma_restore(&gamma, 0x404);
  CHECK(blits == 2 && last_blit_mask == (0x4000u | 0x100u | 0x400u));
  CHECK(draw_buffer_log[0] == 0x405 && draw_buffer_log[1] == 0x404);
  CHECK(ints_of(0xc01)[0] == 0x404 && ints_of(0x8ca6)[0] == 0);
  /* blit keeps scissor state */
  CHECK(enabled_now(0xc11));
  /* resize discards raw colors */
  deleted_textures = 0;
  CHECK(qa_gl_output_gamma_bind(&gamma, 0x405, 128, 32));
  CHECK(deleted_textures == 2);
  blits = 0;
  qa_gl_output_gamma_update(&gamma, table);
  CHECK(blits == 0);
  deleted_textures = 0; deleted_programs = 0;
  qa_gl_output_gamma_close(&gamma);
  CHECK(deleted_textures == 3 && deleted_framebuffers == 1 && deleted_programs == 1);
  /* formats */
  reset_fake("4.6"); gl_open(&g);
  precision.stencil_bits = 0; precision.depth_bits = 32; precision.alpha_bits = 0; precision.color_bits = 16;
  attachment_component = 0x1406;
  CHECK(qa_gl_output_gamma_init(&gamma, &g.context, &g.gl, precision, table));
  CHECK(qa_gl_output_gamma_bind(&gamma, 0x405, 8, 8));
  CHECK(last_tex_internal == 0x8d62);
  CHECK(last_blit_mask == (0x4000u | 0x100u));
  attachment_component = 0x8c17;
  gen_fail = 1;
  CHECK_THROWS("gamma color allocation failed", (void)qa_gl_output_gamma_bind(&gamma, 0x404, 8, 8));
  qa_gl_output_gamma bad;
  CHECK_THROWS("gamma allocation failed", (void)qa_gl_output_gamma_init(&bad, &g.context, &g.gl, precision, table));
  gen_fail = 0;
  qa_gl_output_gamma_close(&gamma);
  qa_gl_close(&g.gl);
  CHECK(retains == releases);
}

/* --- Q2 fog --- */
static qa_q2_fog_operation fog_operation(void) {
  qa_q2_fog_operation op; memset(&op, 0, sizeof op);
  op.camera.projection = qa_perspective_mat4(90, 1, 1, 128);
  op.camera.viewport = R4(2, 3, 16, 12);
  op.camera.origin.x = 1; op.camera.origin.y = 2; op.camera.origin.z = 3;
  op.camera.axis.vectors[0].z = 1; op.camera.axis.vectors[1].y = 1; op.camera.axis.vectors[2].x = -1;
  op.far_depth = 1 - 1e-6;
  op.fog.kind = "q2"; op.fog.color.z = 1; op.fog.density = 32; op.fog.sky_factor = 0.25;
  op.fog.height.start.color.x = 1; op.fog.height.end.color.y = 1; op.fog.height.end.distance = 128; op.fog.height.density = 0.02; op.fog.height.falloff = 1;
  return op;
}
static int find_uniform(const char *name) { for (int i = 0; i < uniform_ids; i++) if (strcmp(uniform_names[i], name) == 0) return i; return -1; }
static void test_fog(void) {
  reset_fake("4.6"); gl_set g; gl_open(&g);
  qa_q2_fog_pass pass; CHECK(qa_q2_fog_pass_init(&pass, &g.context, &g.gl));
  fake_state before = S;
  qa_q2_fog_operation op = fog_operation();
  /* nothing visible: no GL work */
  qa_q2_fog_operation empty = op; empty.fog.density = 0; empty.fog.height.density = 0; empty.sky_drawn = false;
  begins = 0; copies = 0;
  qa_q2_fog_pass_draw(&pass, empty, 32, 32);
  CHECK(begins == 0 && copies == 0 && compiled_programs == 0);
  empty.fog.sky_factor = 1; empty.sky_drawn = false; empty.fog.height.density = 1; empty.fog.height.falloff = 0;
  qa_q2_fog_pass_draw(&pass, empty, 32, 32);
  CHECK(begins == 0);
  /* global + height: sky gated off */
  qa_q2_fog_pass_draw(&pass, op, 32, 32);
  CHECK(copies == 1 && begins == 2 && compiled_programs == 2);
  int color = find_uniform("u_fog_color");
  CHECK(color >= 0);
  CHECK(last_uniform4[color][2] == 1.0f && last_uniform4[color][3] == 0.5f); /* density / 64 */
  int proj = find_uniform("u_proj");
  CHECK(proj >= 0 && last_uniform4[proj][0] == (float)-op.camera.projection.values[10] && last_uniform4[proj][1] == (float)-op.camera.projection.values[14]);
  int right = find_uniform("u_right");
  CHECK(right >= 0 && last_uniform4[right][1] == -1.0f); /* right negates left */
  int tan = find_uniform("u_tan");
  CHECK(tan >= 0 && last_uniform4[tan][0] == (float)(1 / op.camera.projection.values[0]));
  int start = find_uniform("u_hf_start");
  CHECK(start >= 0 && last_uniform4[start][0] == 1.0f && last_uniform4[start][3] == 0.0f);
  int density = find_uniform("u_hf_density");
  CHECK(density >= 0 && last_uniform1[density] == 0.02f);
  CHECK(expect_restored(&before));
  /* programs are cached per kind; sky draws when gated on */
  op.sky_drawn = true;
  begins = 0;
  qa_q2_fog_pass_draw(&pass, op, 32, 32);
  CHECK(begins == 3 && compiled_programs == 3);
  CHECK(last_uniform4[color][3] == 0.25f); /* sky factor */
  begins = 0;
  qa_q2_fog_pass_draw(&pass, op, 32, 32);
  CHECK(begins == 3 && compiled_programs == 3);
  CHECK(expect_restored(&before));
  /* errors */
  qa_q2_fog_operation bad = op; bad.camera.viewport = R4(20, 3, 16, 12);
  CHECK_THROWS("viewport is outside", qa_q2_fog_pass_draw(&pass, bad, 32, 32));
  bad = op; bad.camera.viewport = R4(0.5, 3, 16, 12);
  CHECK_THROWS("viewport is outside", qa_q2_fog_pass_draw(&pass, bad, 32, 32));
  bad = op; bad.camera.projection.values[8] = 0.1;
  CHECK_THROWS("symmetric perspective", qa_q2_fog_pass_draw(&pass, bad, 32, 32));
  bad = op; bad.camera.projection.values[11] = 1;
  CHECK_THROWS("symmetric perspective", qa_q2_fog_pass_draw(&pass, bad, 32, 32));
  pending_error = 0x503;
  CHECK_THROWS("fog pass failed: 0x503", qa_q2_fog_pass_draw(&pass, op, 32, 32));
  pending_error = 0;
  deleted_programs = 0; deleted_textures = 0;
  qa_q2_fog_pass_close(&pass);
  CHECK(deleted_programs == 3 && deleted_textures == 1);
  gen_fail = 1;
  qa_q2_fog_pass failed;
  CHECK_THROWS("could not allocate the fog depth texture", (void)qa_q2_fog_pass_init(&failed, &g.context, &g.gl));
  gen_fail = 0;
  qa_gl_close(&g.gl);
  CHECK(retains == releases);
}

int main(void) {
  test_depth_atlas();
  test_depth_atlas_versions();
  test_object_opacity();
  test_output_gamma();
  test_fog();
  (void)uniform_result; (void)fake_ctx_seen;
  printf("gl depth: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
