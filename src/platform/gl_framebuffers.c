/* Port of src/platform/gl-framebuffers.ts. GPL-2.0-or-later. */
#include "qa/platform/gl_framebuffers.h"

#include <string.h>

/* See load_symbol in src/platform/gl.c. */
static void load_symbol(void *slot, const qa_gl_context *context, const char *name) {
  void *address = context->get_gl_proc_address(context->ctx, name);
  memcpy(slot, &address, sizeof address);
}

#define LOAD(field) load_symbol(&out->field, context, #field)

bool qa_load_gl_framebuffers(const qa_gl_context *context, qa_gl_framebuffers *out) {
  memset(out, 0, sizeof *out);
  LOAD(glGenFramebuffers);
  LOAD(glDeleteFramebuffers);
  LOAD(glBindFramebuffer);
  LOAD(glFramebufferTexture2D);
  LOAD(glCheckFramebufferStatus);
  LOAD(glTexImage2D);
  LOAD(glBlitFramebuffer);
  LOAD(glGetFramebufferAttachmentParameteriv);
  LOAD(glReadBuffer);
  if (!context->retain_procedures(context->ctx, &out->release, &out->release_ctx))
    return false;
  out->closed = false;
  return true;
}

void qa_gl_framebuffers_close(qa_gl_framebuffers *table) {
  if (table->closed) return;
  table->closed = true;
  if (table->release != NULL) table->release(table->release_ctx);
}
