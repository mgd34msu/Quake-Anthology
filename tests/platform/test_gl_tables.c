/*
 * Oracle check for src/platform/gl.ts and gl-framebuffers.ts: every procedure
 * name the TS links is requested, retention and release pair exactly once with
 * the caller's ctx, and a failed retain reports failure. Names generated from
 * the TypeScript. tests/platform/window-*.test.ts need a live SDL window and stay
 * covered by test_platform_b. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "qa/platform/gl.h"
#include "qa/platform/gl_framebuffers.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char *const GL_NAMES[] = {
  "glCallList",
  "glNewList",
  "glEndList",
  "glDeleteLists",
  "glGetString",
  "glGetError",
  "glHint",
  "glIsEnabled",
  "glGetIntegerv",
  "glGetFloatv",
  "glGetTexParameterfv",
  "glGetTexLevelParameteriv",
  "glGetTexImage",
  "glActiveTexture",
  "glClientActiveTexture",
  "glDrawBuffer",
  "glViewport",
  "glScissor",
  "glClearColor",
  "glClearDepth",
  "glClearStencil",
  "glClear",
  "glEnable",
  "glDisable",
  "glClipPlane",
  "glDepthFunc",
  "glDepthMask",
  "glColorMask",
  "glStencilFunc",
  "glStencilOp",
  "glStencilMask",
  "glDepthRange",
  "glPolygonMode",
  "glShadeModel",
  "glPolygonOffset",
  "glLineWidth",
  "glBlendFunc",
  "glAlphaFunc",
  "glCullFace",
  "glFrontFace",
  "glMatrixMode",
  "glLoadIdentity",
  "glLoadMatrixf",
  "glOrtho",
  "glBegin",
  "glEnd",
  "glColor3f",
  "glColor4f",
  "glColor4b",
  "glColor4ub",
  "glTexCoord2f",
  "glVertex2f",
  "glVertex4f",
  "glEnableClientState",
  "glDisableClientState",
  "glPushClientAttrib",
  "glPopClientAttrib",
  "glBindBuffer",
  "glGenBuffers",
  "glDeleteBuffers",
  "glGetPointerv",
  "glVertexPointer",
  "glColorPointer",
  "glTexCoordPointer",
  "glDrawElements",
  "glArrayElement",
  "glGenTextures",
  "glDeleteTextures",
  "glBindTexture",
  "glTexParameteri",
  "glTexParameterfv",
  "glTexEnvi",
  "glTexEnvf",
  "glTexImage2D",
  "glCopyTexImage2D",
  "glTexSubImage2D",
  "glFinish",
  "glPixelStorei",
  "glReadPixels",
};
static const char *const FB_NAMES[] = {
  "glGenFramebuffers",
  "glDeleteFramebuffers",
  "glBindFramebuffer",
  "glFramebufferTexture2D",
  "glCheckFramebufferStatus",
  "glTexImage2D",
  "glBlitFramebuffer",
  "glGetFramebufferAttachmentParameteriv",
  "glReadBuffer",
};
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static char sentinel;
static const char *seen[256]; static size_t seen_count; static int retains, releases; static bool retain_ok;
/* A non-NULL fake address per name, so a table slot left unloaded is visible. */
static char addresses[256];
static void *proc(void *ctx, const char *name) {
  CHECK(ctx == &sentinel);
  seen[seen_count] = name;
  return &addresses[seen_count++];
}
static void release_fn(void *ctx) { CHECK(ctx == &sentinel); releases++; }
static bool retain_fn(void *ctx, qa_gl_release_fn *out, void **out_ctx) { CHECK(ctx == &sentinel); retains++; if (!retain_ok) return false; *out = release_fn; *out_ctx = ctx; return true; }
static bool requested(const char *name) { for (size_t i = 0; i < seen_count; i++) if (strcmp(seen[i], name) == 0) return true; return false; }
static qa_gl_context context(void) { qa_gl_context c; memset(&c, 0, sizeof c); c.ctx = &sentinel; c.get_gl_proc_address = proc; c.retain_procedures = retain_fn; return c; }

int main(void) {
  qa_gl_context c = context();
  retain_ok = true; seen_count = 0; retains = releases = 0;
  qa_gl gl; CHECK(qa_load_gl(&c, &gl));
  CHECK(seen_count == COUNT(GL_NAMES));
  for (size_t i = 0; i < COUNT(GL_NAMES); i++) CHECK(requested(GL_NAMES[i]));
  CHECK(retains == 1 && releases == 0);
  void *slot; memcpy(&slot, &gl.glReadPixels, sizeof slot); CHECK(slot != NULL);
  qa_gl_close(&gl); qa_gl_close(&gl);
  CHECK(releases == 1);

  seen_count = 0; retains = releases = 0;
  qa_gl_compiled_vertex_arrays cva; CHECK(qa_load_gl_compiled_vertex_arrays(&c, &cva));
  CHECK(seen_count == 2 && requested("glLockArraysEXT") && requested("glUnlockArraysEXT"));
  qa_gl_compiled_vertex_arrays_close(&cva); qa_gl_compiled_vertex_arrays_close(&cva);
  CHECK(releases == 1);

  seen_count = 0; retains = releases = 0;
  qa_gl_framebuffers fb; CHECK(qa_load_gl_framebuffers(&c, &fb));
  CHECK(seen_count == COUNT(FB_NAMES));
  for (size_t i = 0; i < COUNT(FB_NAMES); i++) CHECK(requested(FB_NAMES[i]));
  qa_gl_framebuffers_close(&fb); qa_gl_framebuffers_close(&fb);
  CHECK(releases == 1);

  retain_ok = false; seen_count = 0; retains = releases = 0;
  CHECK(!qa_load_gl(&c, &gl)); CHECK(!qa_load_gl_framebuffers(&c, &fb)); CHECK(!qa_load_gl_compiled_vertex_arrays(&c, &cva));
  CHECK(releases == 0);
  printf("platform gl tables: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
