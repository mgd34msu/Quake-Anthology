/*
 * Port of src/platform/gl-framebuffers.ts: Q2 ref_gl/qgl framebuffer
 * ABI resolved through the owning SDL GL context. The renderer owns
 * framebuffer objects and selects its context before symbol calls.
 * GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_GL_FRAMEBUFFERS_H
#define QA_PLATFORM_GL_FRAMEBUFFERS_H

#include "qa/platform/gl.h"

typedef struct qa_gl_framebuffers {
  void (*glGenFramebuffers)(int32_t n, uint32_t *ids);
  void (*glDeleteFramebuffers)(int32_t n, const uint32_t *ids);
  void (*glBindFramebuffer)(uint32_t target, uint32_t framebuffer);
  void (*glFramebufferTexture2D)(uint32_t target, uint32_t attachment,
                                 uint32_t textarget, uint32_t texture,
                                 int32_t level);
  uint32_t (*glCheckFramebufferStatus)(uint32_t target);
  void (*glTexImage2D)(uint32_t target, int32_t level, int32_t internalformat,
                       int32_t width, int32_t height, int32_t border,
                       uint32_t format, uint32_t type, const void *pixels);
  void (*glBlitFramebuffer)(int32_t src_x0, int32_t src_y0, int32_t src_x1,
                            int32_t src_y1, int32_t dst_x0, int32_t dst_y0,
                            int32_t dst_x1, int32_t dst_y1, uint32_t mask,
                            uint32_t filter);
  void (*glGetFramebufferAttachmentParameteriv)(uint32_t target,
                                                uint32_t attachment,
                                                uint32_t pname, int32_t *params);
  void (*glReadBuffer)(uint32_t src);
  qa_gl_release_fn release;
  void *release_ctx;
  bool closed;
} qa_gl_framebuffers;

/* False when retaining the context's procedures fails. */
bool qa_load_gl_framebuffers(const qa_gl_context *context, qa_gl_framebuffers *out);
void qa_gl_framebuffers_close(qa_gl_framebuffers *table);

#endif
