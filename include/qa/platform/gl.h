/*
 * Port of src/platform/gl.ts: typed system OpenGL entry points
 * resolved through the owning SDL GL context, replacing
 * code/unix/linux_qgl.c loading. GL enum/bitfield/object types are
 * spelled with their ABI-identical <stdint.h> types so this header
 * needs no GL headers. A symbol the context cannot supply stays
 * NULL. GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_GL_H
#define QA_PLATFORM_GL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/platform/sdl.h"
#include "qa/platform/sdl_render_context.h"

/*
 * The procedure-retention slice of SdlRenderContext
 * (src/platform/sdl-render-context.ts; C port in
 * qa/platform/sdl_render_context.h, where the interface is type-only
 * and qa_sdl_window plus qa_sdl_worker_render_context provide it).
 * This fn+ctx form accepts either provider, as the TypeScript Pick
 * does; the from_* adapters below build it. retain_procedures
 * returns the release closure the TypeScript returns, as fn+ctx;
 * false mirrors a retain throw.
 */
typedef void *(*qa_gl_get_proc_address_fn)(void *ctx, const char *name);
typedef void (*qa_gl_release_fn)(void *ctx);
typedef bool (*qa_gl_retain_procedures_fn)(void *ctx, qa_gl_release_fn *out_release,
                                           void **out_release_ctx);
typedef void (*qa_gl_make_current_fn)(void *ctx);

typedef struct qa_gl_context {
  void *ctx;
  qa_gl_get_proc_address_fn get_gl_proc_address;
  qa_gl_retain_procedures_fn retain_procedures;
  qa_gl_make_current_fn make_current; /* only the programs table needs it */
} qa_gl_context;

typedef struct qa_gl_compiled_vertex_arrays {
  void (*glLockArraysEXT)(int32_t first, int32_t count);
  void (*glUnlockArraysEXT)(void);
  qa_gl_release_fn release;
  void *release_ctx;
  bool closed;
} qa_gl_compiled_vertex_arrays;

typedef struct qa_gl {
  void (*glCallList)(uint32_t list);
  void (*glNewList)(uint32_t list, uint32_t mode);
  void (*glEndList)(void);
  void (*glDeleteLists)(uint32_t list, int32_t range);
  const char *(*glGetString)(uint32_t name);
  uint32_t (*glGetError)(void);
  void (*glHint)(uint32_t target, uint32_t mode);
  uint8_t (*glIsEnabled)(uint32_t cap);
  void (*glGetIntegerv)(uint32_t pname, int32_t *params);
  void (*glGetFloatv)(uint32_t pname, float *params);
  void (*glGetTexParameterfv)(uint32_t target, uint32_t pname, float *params);
  void (*glGetTexLevelParameteriv)(uint32_t target, int32_t level,
                                   uint32_t pname, int32_t *params);
  void (*glGetTexImage)(uint32_t target, int32_t level, uint32_t format,
                        uint32_t type, void *pixels);
  void (*glActiveTexture)(uint32_t texture);
  void (*glClientActiveTexture)(uint32_t texture);
  void (*glDrawBuffer)(uint32_t buf);
  void (*glViewport)(int32_t x, int32_t y, int32_t width, int32_t height);
  void (*glScissor)(int32_t x, int32_t y, int32_t width, int32_t height);
  void (*glClearColor)(float red, float green, float blue, float alpha);
  void (*glClearDepth)(double depth);
  void (*glClearStencil)(int32_t s);
  void (*glClear)(uint32_t mask);
  void (*glEnable)(uint32_t cap);
  void (*glDisable)(uint32_t cap);
  void (*glClipPlane)(uint32_t plane, const double *equation);
  void (*glDepthFunc)(uint32_t func);
  void (*glDepthMask)(uint8_t flag);
  void (*glColorMask)(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
  void (*glStencilFunc)(uint32_t func, int32_t ref, uint32_t mask);
  void (*glStencilOp)(uint32_t fail, uint32_t zfail, uint32_t zpass);
  void (*glStencilMask)(uint32_t mask);
  void (*glDepthRange)(double near, double far);
  void (*glPolygonMode)(uint32_t face, uint32_t mode);
  void (*glShadeModel)(uint32_t mode);
  void (*glPolygonOffset)(float factor, float units);
  void (*glLineWidth)(float width);
  void (*glBlendFunc)(uint32_t sfactor, uint32_t dfactor);
  void (*glAlphaFunc)(uint32_t func, float ref);
  void (*glCullFace)(uint32_t mode);
  void (*glFrontFace)(uint32_t mode);
  void (*glMatrixMode)(uint32_t mode);
  void (*glLoadIdentity)(void);
  void (*glLoadMatrixf)(const float *m);
  void (*glOrtho)(double left, double right, double bottom, double top,
                  double near, double far);
  void (*glBegin)(uint32_t mode);
  void (*glEnd)(void);
  void (*glColor3f)(float red, float green, float blue);
  void (*glColor4f)(float red, float green, float blue, float alpha);
  void (*glColor4b)(int8_t red, int8_t green, int8_t blue, int8_t alpha);
  void (*glColor4ub)(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
  void (*glTexCoord2f)(float s, float t);
  void (*glVertex2f)(float x, float y);
  void (*glVertex4f)(float x, float y, float z, float w);
  void (*glEnableClientState)(uint32_t array);
  void (*glDisableClientState)(uint32_t array);
  void (*glPushClientAttrib)(uint32_t mask);
  void (*glPopClientAttrib)(void);
  void (*glBindBuffer)(uint32_t target, uint32_t buffer);
  void (*glGenBuffers)(int32_t n, uint32_t *buffers);
  void (*glDeleteBuffers)(int32_t n, const uint32_t *buffers);
  void (*glGetPointerv)(uint32_t pname, void **params);
  void (*glVertexPointer)(int32_t size, uint32_t type, int32_t stride,
                          const void *pointer);
  void (*glColorPointer)(int32_t size, uint32_t type, int32_t stride,
                         const void *pointer);
  void (*glTexCoordPointer)(int32_t size, uint32_t type, int32_t stride,
                             const void *pointer);
  void (*glDrawElements)(uint32_t mode, int32_t count, uint32_t type,
                         const void *indices);
  void (*glArrayElement)(int32_t i);
  void (*glGenTextures)(int32_t n, uint32_t *textures);
  void (*glDeleteTextures)(int32_t n, const uint32_t *textures);
  void (*glBindTexture)(uint32_t target, uint32_t texture);
  void (*glTexParameteri)(uint32_t target, uint32_t pname, int32_t param);
  void (*glTexParameterfv)(uint32_t target, uint32_t pname, const float *params);
  void (*glTexEnvi)(uint32_t target, uint32_t pname, int32_t param);
  void (*glTexEnvf)(uint32_t target, uint32_t pname, float param);
  void (*glTexImage2D)(uint32_t target, int32_t level, int32_t internalformat,
                       int32_t width, int32_t height, int32_t border,
                       uint32_t format, uint32_t type, const void *pixels);
  void (*glCopyTexImage2D)(uint32_t target, int32_t level,
                           uint32_t internalformat, int32_t x, int32_t y,
                           int32_t width, int32_t height, int32_t border);
  void (*glTexSubImage2D)(uint32_t target, int32_t level, int32_t xoffset,
                          int32_t yoffset, int32_t width, int32_t height,
                          uint32_t format, uint32_t type, const void *pixels);
  void (*glFinish)(void);
  void (*glPixelStorei)(uint32_t pname, int32_t param);
  void (*glReadPixels)(int32_t x, int32_t y, int32_t width, int32_t height,
                       uint32_t format, uint32_t type, void *pixels);
  qa_gl_release_fn release;
  void *release_ctx;
  bool closed;
} qa_gl;

qa_gl_context qa_gl_context_from_window(qa_sdl_window *window);
qa_gl_context qa_gl_context_from_worker(qa_sdl_worker_render_context *context);

/* False when retaining the context's procedures fails. */
bool qa_load_gl_compiled_vertex_arrays(const qa_gl_context *window,
                                       qa_gl_compiled_vertex_arrays *out);
void qa_gl_compiled_vertex_arrays_close(qa_gl_compiled_vertex_arrays *table);

bool qa_load_gl(const qa_gl_context *window, qa_gl *out);
void qa_gl_close(qa_gl *table);

#endif
