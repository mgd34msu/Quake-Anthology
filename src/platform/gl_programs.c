/* Port of src/platform/gl-programs.ts. GPL-2.0-or-later. */
#include "qa/platform/gl_programs.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* TS `throw new Error(msg)`, as in src/text/atlas.c. */
static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

/* See load_symbol in src/platform/gl.c. */
static void load_symbol(void *slot, const qa_gl_context *context, const char *name) {
  void *address = context->get_gl_proc_address(context->ctx, name);
  memcpy(slot, &address, sizeof address);
}

#define LOAD(field) load_symbol(&out->field, context, #field)

bool qa_load_gl_programs(const qa_gl_context *context, qa_gl_programs *out) {
  if (sizeof(void *) != 8)
    qa_error("GL shader source requires a 64-bit pointer ABI");
  memset(out, 0, sizeof *out);
  LOAD(glCreateShader);
  LOAD(glShaderSource);
  LOAD(glCompileShader);
  LOAD(glGetShaderiv);
  LOAD(glGetShaderInfoLog);
  LOAD(glDeleteShader);
  LOAD(glCreateProgram);
  LOAD(glAttachShader);
  LOAD(glLinkProgram);
  LOAD(glGetProgramiv);
  LOAD(glGetProgramInfoLog);
  LOAD(glDeleteProgram);
  LOAD(glUseProgram);
  LOAD(glGetUniformLocation);
  LOAD(glUniform1i);
  LOAD(glUniform1f);
  LOAD(glUniform3f);
  LOAD(glUniform4f);
  LOAD(glUniformMatrix4fv);
  if (!context->retain_procedures(context->ctx, &out->release, &out->release_ctx))
    return false;
  out->context = *context;
  out->closed = false;
  return true;
}

void qa_gl_programs_shader_source(qa_gl_programs *table, uint32_t shader,
                                  const char *source) {
  if (table->closed) qa_error("GL program procedure table is closed");
  if (shader == 0) qa_range_error("GL shader must be a nonzero uint32");
  size_t length = strlen(source);
  if (length > 0x7fffffff)
    qa_range_error("GL shader source exceeds signed 32-bit length");
  const char *pointers[1] = {source};
  int32_t lengths[1] = {(int32_t)length};
  table->context.make_current(table->context.ctx);
  table->glShaderSource(shader, 1, pointers, lengths);
}

void qa_gl_programs_close(qa_gl_programs *table) {
  if (table->closed) return;
  table->closed = true;
  if (table->release != NULL) table->release(table->release_ctx);
}
