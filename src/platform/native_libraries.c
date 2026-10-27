/* Port of src/platform/native-libraries.ts. GPL-2.0-or-later. */
#include "qa/platform/native_libraries.h"

#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#endif

#include "qa/foundation/support.h"

static const char *const kind_names[] = {"sdl2", "sdl3", "gl", "vorbisfile", "theoradec", "freetype"};

static const char *const variables[] = {
    "QUAKE_SDL2_LIBRARY", "QUAKE_SDL3_LIBRARY", "QUAKE_GL_LIBRARY",
    "QUAKE_VORBISFILE_LIBRARY", "QUAKE_THEORA_LIBRARY", "QUAKE_FREETYPE_LIBRARY"};

static const char *const linux_names[][2] = {
    {"libSDL2-2.0.so.0", "libSDL2.so"},
    {"libSDL3.so.0", "libSDL3.so"},
    {"libGL.so.1", "libGL.so"},
    {"libvorbisfile.so.3", "libvorbisfile.so"},
    {"libtheoradec.so.2", "libtheoradec.so"},
    {"libfreetype.so.6", "libfreetype.so"}};

const char *qa_native_library_kind_name(qa_native_library kind) {
  return kind_names[(size_t)kind];
}

static const char *host_platform(void) {
#if defined(_WIN32)
  return "win32";
#elif defined(__APPLE__)
  return "darwin";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

const char *qa_default_open_gl_driver(const char *platform) {
  const char *name = platform == NULL ? host_platform() : platform;
  if (strcmp(name, "linux") == 0) return "libGL.so.1";
  if (strcmp(name, "darwin") == 0) {
    return "/System/Library/Frameworks/OpenGL.framework/Libraries/libGL.dylib";
  }
  if (strcmp(name, "win32") == 0) return "opengl32.dll";
  qa_range_error("OpenGL is unsupported on %s", name);
}

static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

/* Node path dirname for the separator of the target platform. */
static char *path_dirname(const char *path, bool windows) {
  size_t length = strlen(path);
  while (length > 1 && (path[length - 1] == '/' || (windows && path[length - 1] == '\\'))) {
    length--;
  }
  size_t cut = length;
  while (cut > 0 && path[cut - 1] != '/' && !(windows && path[cut - 1] == '\\')) cut--;
  if (cut == 0) return xstrdup(".");
  size_t keep = cut - 1;
  while (keep > 1 && (path[keep - 1] == '/' || (windows && path[keep - 1] == '\\'))) keep--;
  if (keep == 0) keep = 1;
  char *out = (char *)malloc(keep + 1);
  if (out == NULL) return NULL;
  memcpy(out, path, keep);
  out[keep] = '\0';
  return out;
}

/* Node path join: a plain concatenation (no absolute reset). */
static char *path_join(const char *directory, const char *name, bool windows) {
  char separator = windows ? '\\' : '/';
  size_t dir_length = strlen(directory);
  size_t name_length = strlen(name);
  bool needs_separator = dir_length > 0 && directory[dir_length - 1] != '/' &&
                         directory[dir_length - 1] != separator;
  char *out = (char *)malloc(dir_length + (needs_separator ? 1 : 0) + name_length + 1);
  if (out == NULL) return NULL;
  memcpy(out, directory, dir_length);
  size_t at = dir_length;
  if (needs_separator) out[at++] = separator;
  memcpy(out + at, name, name_length + 1);
  return out;
}

static const char *env_lookup(const qa_native_library_options *options, const char *variable) {
  if (options != NULL && options->has_environment) {
    for (size_t i = 0; i < options->environment_count; i++) {
      if (strcmp(options->environment[i].name, variable) == 0) {
        return options->environment[i].value;
      }
    }
    return NULL;
  }
  return getenv(variable);
}

static char *process_exec_path(void) {
#if defined(__linux__)
  char buffer[PATH_MAX];
  ssize_t length = readlink("/proc/self/exe", buffer, sizeof buffer - 1);
  if (length > 0) {
    buffer[length] = '\0';
    return xstrdup(buffer);
  }
#elif defined(__APPLE__)
  char buffer[PATH_MAX];
  uint32_t size = sizeof buffer;
  if (_NSGetExecutablePath(buffer, &size) == 0) return xstrdup(buffer);
#endif
  return xstrdup(".");
}

typedef struct string_list {
  char **items;
  size_t count;
  size_t capacity;
} string_list;

static bool list_push(string_list *list, char *item) {
  if (item == NULL) return false;
  if (list->count == list->capacity) {
    size_t grown = list->capacity == 0 ? 8 : list->capacity * 2;
    char **items = (char **)realloc(list->items, grown * sizeof *items);
    if (items == NULL) {
      free(item);
      return false;
    }
    list->items = items;
    list->capacity = grown;
  }
  list->items[list->count++] = item;
  return true;
}

static void list_clear(string_list *list) {
  for (size_t i = 0; i < list->count; i++) free(list->items[i]);
  free(list->items);
  memset(list, 0, sizeof *list);
}

bool qa_native_library_candidates_of(qa_native_library kind,
                                     const qa_native_library_options *options,
                                     qa_native_library_candidates *out) {
  memset(out, 0, sizeof *out);
  const char *variable = variables[(size_t)kind];
  const char *override = env_lookup(options, variable);
  if (override != NULL) {
    /* A C string cannot contain NUL; the nonempty half is checked. */
    if (override[0] == '\0') {
      qa_range_error("%s must be nonempty and contain no NUL", variable);
    }
    char **items = (char **)malloc(sizeof *items);
    if (items == NULL) return false;
    items[0] = xstrdup(override);
    if (items[0] == NULL) {
      free(items);
      return false;
    }
    out->items = items;
    out->count = 1;
    return true;
  }
  const char *platform = options != NULL && options->platform != NULL
                             ? options->platform
                             : host_platform();
  bool windows = strcmp(platform, "win32") == 0;
  const char *exec_given = options != NULL ? options->exec_path : NULL;
  char *exec_owned = NULL;
  if (exec_given == NULL) {
    exec_owned = process_exec_path();
    exec_given = exec_owned;
  }
  char *directory = exec_given == NULL ? NULL : path_dirname(exec_given, windows);
  free(exec_owned);
  if (directory == NULL) return false;

  const char *names[3];
  size_t name_count = 0;
  string_list installed = {NULL, 0, 0};
  bool ok = true;
  if (strcmp(platform, "linux") == 0) {
    names[0] = linux_names[(size_t)kind][0];
    names[1] = linux_names[(size_t)kind][1];
    name_count = 2;
  } else if (windows) {
    switch (kind) {
      case QA_NATIVE_LIBRARY_SDL3: names[0] = "SDL3.dll"; name_count = 1; break;
      case QA_NATIVE_LIBRARY_SDL2: names[0] = "SDL2.dll"; name_count = 1; break;
      case QA_NATIVE_LIBRARY_GL: names[0] = "opengl32.dll"; name_count = 1; break;
      case QA_NATIVE_LIBRARY_VORBISFILE:
        names[0] = "libvorbisfile-3.dll"; names[1] = "vorbisfile.dll"; name_count = 2; break;
      case QA_NATIVE_LIBRARY_THEORADEC:
        names[0] = "libtheoradec-1.dll"; names[1] = "theoradec.dll"; name_count = 2; break;
      case QA_NATIVE_LIBRARY_FREETYPE:
        names[0] = "freetype.dll"; names[1] = "libfreetype-6.dll";
        names[2] = "freetype6.dll"; name_count = 3; break;
    }
  } else if (strcmp(platform, "darwin") == 0) {
    switch (kind) {
      case QA_NATIVE_LIBRARY_SDL3:
        names[0] = "libSDL3.0.dylib"; names[1] = "libSDL3.dylib"; name_count = 2; break;
      case QA_NATIVE_LIBRARY_SDL2:
        names[0] = "libSDL2-2.0.0.dylib"; names[1] = "libSDL2.dylib"; name_count = 2; break;
      case QA_NATIVE_LIBRARY_GL:
        names[0] = qa_default_open_gl_driver(platform); name_count = 1; break;
      case QA_NATIVE_LIBRARY_VORBISFILE:
        names[0] = "libvorbisfile.3.dylib"; names[1] = "libvorbisfile.dylib"; name_count = 2; break;
      case QA_NATIVE_LIBRARY_THEORADEC:
        names[0] = "libtheoradec.2.dylib"; names[1] = "libtheoradec.dylib"; name_count = 2; break;
      case QA_NATIVE_LIBRARY_FREETYPE:
        names[0] = "libfreetype.6.dylib"; names[1] = "libfreetype.dylib"; name_count = 2; break;
    }
    static const char *const lib_dirs[] = {"/opt/homebrew/lib", "/usr/local/lib"};
    for (size_t d = 0; ok && d < 2; d++) {
      for (size_t n = 0; ok && n < name_count; n++) {
        ok = list_push(&installed, path_join(lib_dirs[d], names[n], false));
      }
    }
    if (ok && (kind == QA_NATIVE_LIBRARY_SDL2 || kind == QA_NATIVE_LIBRARY_SDL3)) {
      const char *framework = kind == QA_NATIVE_LIBRARY_SDL3 ? "SDL3.framework/SDL3"
                                                             : "SDL2.framework/SDL2";
      const char *home = options != NULL && options->home_directory != NULL
                             ? options->home_directory
                             : getenv("HOME");
      char *home_frameworks = home == NULL ? NULL : path_join(home, "Library/Frameworks", false);
      if (home != NULL && home_frameworks == NULL) ok = false;
      const char *dirs[3] = {directory, home_frameworks, "/Library/Frameworks"};
      for (size_t d = 0; ok && d < 3; d++) {
        if (dirs[d] == NULL) continue;
        ok = list_push(&installed, path_join(dirs[d], framework, false));
      }
      free(home_frameworks);
    }
  } else {
    free(directory);
    qa_range_error("Native %s libraries are unsupported on %s",
                   qa_native_library_kind_name(kind), platform);
  }
  string_list result = {NULL, 0, 0};
  for (size_t n = 0; ok && n < name_count; n++) {
    ok = list_push(&result, path_join(directory, names[n], windows));
  }
  for (size_t i = 0; ok && i < installed.count; i++) {
    char *copy = xstrdup(installed.items[i]);
    ok = list_push(&result, copy);
  }
  for (size_t n = 0; ok && n < name_count; n++) ok = list_push(&result, xstrdup(names[n]));
  list_clear(&installed);
  free(directory);
  if (!ok) {
    list_clear(&result);
    return false;
  }
  out->items = result.items;
  out->count = result.count;
  return true;
}

void qa_native_library_candidates_free(qa_native_library_candidates *candidates) {
  for (size_t i = 0; i < candidates->count; i++) free(candidates->items[i]);
  free(candidates->items);
  memset(candidates, 0, sizeof *candidates);
}

bool qa_open_native_library(qa_native_library kind, qa_native_library_open_fn open,
                            void *ctx, const qa_native_library_options *options) {
  qa_native_library_candidates candidates;
  if (!qa_native_library_candidates_of(kind, options, &candidates)) return false;
  for (size_t i = 0; i < candidates.count; i++) {
    if (open(candidates.items[i], ctx)) {
      qa_native_library_candidates_free(&candidates);
      return true;
    }
  }
  /* TS throws AggregateError with the joined candidate list. */
  size_t joined_length = 1;
  for (size_t i = 0; i < candidates.count; i++) {
    joined_length += strlen(candidates.items[i]) + 2;
  }
  char *joined = (char *)malloc(joined_length);
  if (joined != NULL) {
    joined[0] = '\0';
    for (size_t i = 0; i < candidates.count; i++) {
      if (i > 0) strcat(joined, ", ");
      strcat(joined, candidates.items[i]);
    }
  }
  qa_native_library_candidates_free(&candidates);
  qa_range_error("Could not load %s; attempted %s", qa_native_library_kind_name(kind),
                 joined == NULL ? "" : joined);
}
