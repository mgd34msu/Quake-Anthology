/*
 * Port of src/platform/gl-programs.ts: Q2 ref_gl/qgl shader ABI with
 * single-string shaderSource indirection and explicit buffer
 * ownership. The renderer owns program/shader objects and selects
 * its context before raw symbol calls. GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_GL_PROGRAMS_H
#define QA_PLATFORM_GL_PROGRAMS_H

#include "qa/platform/gl.h"

typedef struct qa_gl_programs {
  uint32_t (*glCreateShader)(uint32_t type);
  void (*glShaderSource)(uint32_t shader, int32_t count,
                         const char *const *string, const int32_t *length);
  void (*glCompileShader)(uint32_t shader);
  void (*glGetShaderiv)(uint32_t shader, uint32_t pname, int32_t *params);
  void (*glGetShaderInfoLog)(uint32_t shader, int32_t buf_size, int32_t *length,
                             char *info_log);
  void (*glDeleteShader)(uint32_t shader);
  uint32_t (*glCreateProgram)(void);
  void (*glAttachShader)(uint32_t program, uint32_t shader);
  void (*glLinkProgram)(uint32_t program);
  void (*glGetProgramiv)(uint32_t program, uint32_t pname, int32_t *params);
  void (*glGetProgramInfoLog)(uint32_t program, int32_t buf_size,
                               int32_t *length, char *info_log);
  void (*glDeleteProgram)(uint32_t program);
  void (*glUseProgram)(uint32_t program);
  int32_t (*glGetUniformLocation)(uint32_t program, const char *name);
  void (*glUniform1i)(int32_t location, int32_t v0);
  void (*glUniform1f)(int32_t location, float v0);
  void (*glUniform3f)(int32_t location, float v0, float v1, float v2);
  void (*glUniform4f)(int32_t location, float v0, float v1, float v2, float v3);
  void (*glUniformMatrix4fv)(int32_t location, int32_t count, uint8_t transpose,
                             const float *value);
  qa_gl_context context; /* retained for makeCurrent in shaderSource */
  qa_gl_release_fn release;
  void *release_ctx;
  bool closed;
} qa_gl_programs;

/* False when retaining the context's procedures fails. */
bool qa_load_gl_programs(const qa_gl_context *context, qa_gl_programs *out);
/*
 * One-string glShaderSource: makes the context current, then passes
 * the source pointer plus its explicit signed length. (The
 * TypeScript also rejects an embedded NUL and keeps the bytes alive
 * against GC during the call; a C string needs neither.)
 */
void qa_gl_programs_shader_source(qa_gl_programs *table, uint32_t shader,
                                  const char *source);
void qa_gl_programs_close(qa_gl_programs *table);

#endif
