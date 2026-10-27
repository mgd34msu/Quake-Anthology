/*
 * Port of src/platform/native-libraries.ts: typed native-library
 * discovery (Q3 discovery with Q1's approved OpenGL/Vorbis families).
 * Candidate lists are ordered loader inputs, not evidence a library
 * exists. C accommodations: the option record's environment is an
 * explicit name/value span (has_environment false reads the process
 * environment); the generic openNativeLibrary<T> callback reports
 * success/failure and keeps its result in its ctx. A NULL platform
 * string means the host platform ("linux"/"darwin"/"win32"), a NULL
 * exec_path the running executable, a NULL home_directory the home dir.
 * GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_NATIVE_LIBRARIES_H
#define QA_PLATFORM_NATIVE_LIBRARIES_H

#include <stdbool.h>
#include <stddef.h>

typedef enum qa_native_library {
  QA_NATIVE_LIBRARY_SDL2,
  QA_NATIVE_LIBRARY_SDL3,
  QA_NATIVE_LIBRARY_GL,
  QA_NATIVE_LIBRARY_VORBISFILE,
  QA_NATIVE_LIBRARY_THEORADEC,
  QA_NATIVE_LIBRARY_FREETYPE
} qa_native_library;

typedef struct qa_native_library_env {
  const char *name;
  const char *value; /* NULL is a TS undefined entry */
} qa_native_library_env;

typedef struct qa_native_library_options {
  const char *platform;
  const char *exec_path;
  const char *home_directory;
  const qa_native_library_env *environment;
  size_t environment_count;
  bool has_environment; /* false reads the process environment */
} qa_native_library_options;

typedef struct qa_native_library_candidates {
  char **items; /* malloc'd strings; free with _free below */
  size_t count;
} qa_native_library_candidates;

/* TS kind string ("sdl2", "gl", ...), as used in error messages. */
const char *qa_native_library_kind_name(qa_native_library kind);

/* Ordered candidates. Options may be NULL. False only on OOM. */
bool qa_native_library_candidates_of(qa_native_library kind,
                                     const qa_native_library_options *options,
                                     qa_native_library_candidates *out);
void qa_native_library_candidates_free(qa_native_library_candidates *candidates);

/* True return means the candidate loaded; the result stays in ctx. */
typedef bool (*qa_native_library_open_fn)(const char *path, void *ctx);

/* Tries candidates in order; aborts when every candidate fails.
 * Returns false only on OOM while building the candidate list. */
bool qa_open_native_library(qa_native_library kind, qa_native_library_open_fn open,
                            void *ctx, const qa_native_library_options *options);

/* platform may be NULL for the host platform. */
const char *qa_default_open_gl_driver(const char *platform);

#endif
