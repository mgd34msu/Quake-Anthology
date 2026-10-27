/* Port of src/platform/gl.ts. GPL-2.0-or-later. */
#include "qa/platform/gl.h"

#include <stdlib.h>
#include <string.h>

/* memcpy avoids the object/function pointer cast the C standard
 * does not define; SDL_GL_GetProcAddress supplies both. */
static void load_symbol(void *slot, const qa_gl_context *window, const char *name) {
  void *address = window->get_gl_proc_address(window->ctx, name);
  memcpy(slot, &address, sizeof address);
}

#define LOAD(table, field) load_symbol(&(table)->field, window, #field)

static bool retain(const qa_gl_context *window, qa_gl_release_fn *out_release,
                   void **out_ctx) {
  return window->retain_procedures(window->ctx, out_release, out_ctx);
}

bool qa_load_gl_compiled_vertex_arrays(const qa_gl_context *window,
                                       qa_gl_compiled_vertex_arrays *out) {
  memset(out, 0, sizeof *out);
  LOAD(out, glLockArraysEXT);
  LOAD(out, glUnlockArraysEXT);
  if (!retain(window, &out->release, &out->release_ctx)) return false;
  out->closed = false;
  return true;
}

void qa_gl_compiled_vertex_arrays_close(qa_gl_compiled_vertex_arrays *table) {
  if (table->closed) return;
  table->closed = true;
  if (table->release != NULL) table->release(table->release_ctx);
}

bool qa_load_gl(const qa_gl_context *window, qa_gl *out) {
  memset(out, 0, sizeof *out);
  LOAD(out, glCallList);
  LOAD(out, glNewList);
  LOAD(out, glEndList);
  LOAD(out, glDeleteLists);
  LOAD(out, glGetString);
  LOAD(out, glGetError);
  LOAD(out, glHint);
  LOAD(out, glIsEnabled);
  LOAD(out, glGetIntegerv);
  LOAD(out, glGetFloatv);
  LOAD(out, glGetTexParameterfv);
  LOAD(out, glGetTexLevelParameteriv);
  LOAD(out, glGetTexImage);
  LOAD(out, glActiveTexture);
  LOAD(out, glClientActiveTexture);
  LOAD(out, glDrawBuffer);
  LOAD(out, glViewport);
  LOAD(out, glScissor);
  LOAD(out, glClearColor);
  LOAD(out, glClearDepth);
  LOAD(out, glClearStencil);
  LOAD(out, glClear);
  LOAD(out, glEnable);
  LOAD(out, glDisable);
  LOAD(out, glClipPlane);
  LOAD(out, glDepthFunc);
  LOAD(out, glDepthMask);
  LOAD(out, glColorMask);
  LOAD(out, glStencilFunc);
  LOAD(out, glStencilOp);
  LOAD(out, glStencilMask);
  LOAD(out, glDepthRange);
  LOAD(out, glPolygonMode);
  LOAD(out, glShadeModel);
  LOAD(out, glPolygonOffset);
  LOAD(out, glLineWidth);
  LOAD(out, glBlendFunc);
  LOAD(out, glAlphaFunc);
  LOAD(out, glCullFace);
  LOAD(out, glFrontFace);
  LOAD(out, glMatrixMode);
  LOAD(out, glLoadIdentity);
  LOAD(out, glLoadMatrixf);
  LOAD(out, glOrtho);
  LOAD(out, glBegin);
  LOAD(out, glEnd);
  LOAD(out, glColor3f);
  LOAD(out, glColor4f);
  LOAD(out, glColor4b);
  LOAD(out, glColor4ub);
  LOAD(out, glTexCoord2f);
  LOAD(out, glVertex2f);
  LOAD(out, glVertex4f);
  LOAD(out, glEnableClientState);
  LOAD(out, glDisableClientState);
  LOAD(out, glPushClientAttrib);
  LOAD(out, glPopClientAttrib);
  LOAD(out, glBindBuffer);
  LOAD(out, glGenBuffers);
  LOAD(out, glDeleteBuffers);
  LOAD(out, glGetPointerv);
  LOAD(out, glVertexPointer);
  LOAD(out, glColorPointer);
  LOAD(out, glTexCoordPointer);
  LOAD(out, glDrawElements);
  LOAD(out, glArrayElement);
  LOAD(out, glGenTextures);
  LOAD(out, glDeleteTextures);
  LOAD(out, glBindTexture);
  LOAD(out, glTexParameteri);
  LOAD(out, glTexParameterfv);
  LOAD(out, glTexEnvi);
  LOAD(out, glTexEnvf);
  LOAD(out, glTexImage2D);
  LOAD(out, glCopyTexImage2D);
  LOAD(out, glTexSubImage2D);
  LOAD(out, glFinish);
  LOAD(out, glPixelStorei);
  LOAD(out, glReadPixels);
  if (!retain(window, &out->release, &out->release_ctx)) return false;
  out->closed = false;
  return true;
}

void qa_gl_close(qa_gl *table) {
  if (table->closed) return;
  table->closed = true;
  if (table->release != NULL) table->release(table->release_ctx);
}

/* Adapters over the two SdlRenderContext providers. Their retain
 * calls abort on a closed provider and hand back a small lease
 * struct; the heap copy is the release closure's context. */
static void *window_proc(void *ctx, const char *name) {
  return qa_sdl_window_get_gl_proc_address((qa_sdl_window *)ctx, name);
}
static void window_current(void *ctx) {
  qa_sdl_window_make_current((qa_sdl_window *)ctx);
}
static void window_release(void *ctx) {
  qa_sdl_procedure_lease_release((qa_sdl_procedure_lease *)ctx);
  free(ctx);
}
static bool window_retain(void *ctx, qa_gl_release_fn *out_release, void **out_ctx) {
  qa_sdl_procedure_lease *lease =
      (qa_sdl_procedure_lease *)malloc(sizeof *lease);
  if (lease == NULL) return false;
  *lease = qa_sdl_window_retain_procedures((qa_sdl_window *)ctx);
  *out_release = window_release;
  *out_ctx = lease;
  return true;
}

qa_gl_context qa_gl_context_from_window(qa_sdl_window *window) {
  qa_gl_context context;
  context.ctx = window;
  context.get_gl_proc_address = window_proc;
  context.retain_procedures = window_retain;
  context.make_current = window_current;
  return context;
}

static void *worker_proc(void *ctx, const char *name) {
  return qa_sdl_worker_render_context_get_gl_proc_address(
      (qa_sdl_worker_render_context *)ctx, name);
}
static void worker_current(void *ctx) {
  qa_sdl_worker_render_context_make_current((qa_sdl_worker_render_context *)ctx);
}
static void worker_release(void *ctx) {
  qa_sdl_worker_procedure_lease_release((qa_sdl_worker_procedure_lease *)ctx);
  free(ctx);
}
static bool worker_retain(void *ctx, qa_gl_release_fn *out_release, void **out_ctx) {
  qa_sdl_worker_procedure_lease *lease =
      (qa_sdl_worker_procedure_lease *)malloc(sizeof *lease);
  if (lease == NULL) return false;
  *lease = qa_sdl_worker_render_context_retain_procedures(
      (qa_sdl_worker_render_context *)ctx);
  *out_release = worker_release;
  *out_ctx = lease;
  return true;
}

qa_gl_context qa_gl_context_from_worker(qa_sdl_worker_render_context *context) {
  qa_gl_context result;
  result.ctx = context;
  result.get_gl_proc_address = worker_proc;
  result.retain_procedures = worker_retain;
  result.make_current = worker_current;
  return result;
}
