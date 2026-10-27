/* Port of src/platform/sdl-render-context.ts. GPL-2.0-or-later. */
#include "qa/platform/sdl_render_context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void checked(int result, const char *operation) {
  if (result < 0) qa_range_error("%s: %s", operation, SDL_GetError());
}

static void *pointer(void *value, const char *operation) {
  if (value == NULL) qa_range_error("%s: %s", operation, SDL_GetError());
  return value;
}

static void make_key(char *out, size_t out_size) {
  static const char hex[] = "0123456789abcdef";
  unsigned char bytes[16];
  bool filled = false;
  FILE *random = fopen("/dev/urandom", "rb");
  if (random != NULL) {
    filled = fread(bytes, 1, sizeof bytes, random) == sizeof bytes;
    fclose(random);
  }
  if (!filled) {
    static unsigned int counter = 0;
    unsigned int seed = SDL_GetTicks() ^ (++counter * 2654435761u);
    for (size_t i = 0; i < sizeof bytes; i++) {
      seed = seed * 1664525u + 1013904223u;
      bytes[i] = (unsigned char)(seed >> 24);
    }
  }
  /* crypto.randomUUID shape: 8-4-4-4-12 hex digits. */
  char uuid[37];
  size_t at = 0;
  for (size_t i = 0; i < 16; i++) {
    if (i == 4 || i == 6 || i == 8 || i == 10) uuid[at++] = '-';
    uuid[at++] = hex[bytes[i] >> 4];
    uuid[at++] = hex[bytes[i] & 15];
  }
  uuid[at] = '\0';
  snprintf(out, out_size, "quake-render-%s", uuid);
}

bool qa_sdl_render_context_lease_detach(SDL_Window *window, SDL_GLContext context,
                                        uint32_t window_id,
                                        qa_sdl_render_context_lease *out) {
#if defined(__APPLE__)
  (void)window;
  (void)context;
  (void)window_id;
  (void)out;
  /* Cocoa may dispatch drawable updates onto the main queue, which the
   * frontend's synchronous worker waits cannot service. */
  qa_range_error("SDL OpenGL render workers require main-queue dispatch support "
                 "on macOS; use the serial renderer");
#else
  if (SDL_GL_GetCurrentContext() != context) {
    qa_range_error("SDL context must be current before detach");
  }
  memset(out, 0, sizeof *out);
  _Atomic int32_t *ownership = (_Atomic int32_t *)malloc(sizeof *ownership);
  if (ownership == NULL) return false;
  atomic_init(ownership, 0);
  out->window = window;
  out->context = context;
  out->transfer.window_id = window_id;
  make_key(out->transfer.key, sizeof out->transfer.key);
  out->transfer.ownership = ownership;
  char owner_key[96];
  snprintf(owner_key, sizeof owner_key, "%s-owner", out->transfer.key);
  SDL_SetWindowData(window, out->transfer.key, context);
  SDL_SetWindowData(window, owner_key, (void *)ownership);
  if (SDL_GetWindowData(window, out->transfer.key) != (void *)context ||
      SDL_GetWindowData(window, owner_key) != (void *)ownership) {
    SDL_SetWindowData(window, out->transfer.key, NULL);
    SDL_SetWindowData(window, owner_key, NULL);
    free(ownership);
    qa_range_error("SDL context transfer registration failed");
  }
  if (SDL_GL_MakeCurrent(window, NULL) < 0) {
    const char *message = SDL_GetError();
    SDL_SetWindowData(window, out->transfer.key, NULL);
    SDL_SetWindowData(window, owner_key, NULL);
    free(ownership);
    qa_range_error("SDL_GL_MakeCurrent detach: %s", message);
  }
  atomic_store(ownership, QA_SDL_CONTEXT_OFFERED);
  return true;
#endif
}

bool qa_sdl_render_context_lease_parked(const qa_sdl_render_context_lease *lease) {
  return atomic_load(lease->transfer.ownership) == QA_SDL_CONTEXT_PARKED;
}

void qa_sdl_render_context_lease_restore(qa_sdl_render_context_lease *lease) {
  _Atomic int32_t *state = lease->transfer.ownership;
  int32_t current = atomic_load(state);
  if (current == QA_SDL_CONTEXT_RESTORED) return;
  if ((current != QA_SDL_CONTEXT_OFFERED && current != QA_SDL_CONTEXT_RELEASED) ||
      !atomic_compare_exchange_strong(state, &current, QA_SDL_CONTEXT_RESTORING)) {
    qa_range_error("SDL render context is still owned by the worker");
  }
  if (SDL_GL_MakeCurrent(lease->window, lease->context) < 0) {
    const char *message = SDL_GetError();
    atomic_store(state, QA_SDL_CONTEXT_RELEASED);
    qa_range_error("SDL_GL_MakeCurrent restore: %s", message);
  }
  char owner_key[96];
  snprintf(owner_key, sizeof owner_key, "%s-owner", lease->transfer.key);
  SDL_SetWindowData(lease->window, lease->transfer.key, NULL);
  SDL_SetWindowData(lease->window, owner_key, NULL);
  atomic_store(state, QA_SDL_CONTEXT_RESTORED);
}

void qa_sdl_render_context_lease_destroy(qa_sdl_render_context_lease *lease) {
  free(lease->transfer.ownership);
  memset(lease, 0, sizeof *lease);
}

static bool valid_key(const char *key) {
  const char *prefix = "quake-render-";
  size_t prefix_length = strlen(prefix);
  if (strncmp(key, prefix, prefix_length) != 0) return false;
  const char *rest = key + prefix_length;
  if (strlen(rest) != 36) return false;
  for (size_t i = 0; i < 36; i++) {
    char c = rest[i];
    if (!(c == '-' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

static qa_sdl_render_context_transfer parse_transfer(
    const qa_sdl_render_context_transfer *value) {
  if (value == NULL || value->window_id == 0 || !valid_key(value->key) ||
      value->ownership == NULL) {
    qa_range_error("Invalid SDL render context transfer");
  }
  return *value;
}

bool qa_sdl_worker_render_context_adopt(const qa_sdl_render_context_transfer *value,
                                        qa_sdl_worker_render_context *out) {
  qa_sdl_render_context_transfer transfer = parse_transfer(value);
  _Atomic int32_t *state = transfer.ownership;
  int32_t expected = QA_SDL_CONTEXT_OFFERED;
  if (!atomic_compare_exchange_strong(state, &expected, QA_SDL_CONTEXT_ADOPTED)) {
    qa_range_error("SDL render context transfer was already consumed");
  }
  /* On failure the TS stores released before rethrowing; checked() and
   * pointer() abort instead, so validate before each checked call. */
  if (SDL_GL_GetCurrentContext() != NULL) {
    atomic_store(state, QA_SDL_CONTEXT_RELEASED);
    qa_range_error("Worker already has a current SDL context");
  }
  SDL_Window *window = SDL_GetWindowFromID(transfer.window_id);
  if (window == NULL) {
    const char *message = SDL_GetError();
    atomic_store(state, QA_SDL_CONTEXT_RELEASED);
    qa_range_error("SDL_GetWindowFromID: %s", message);
  }
  char owner_key[96];
  snprintf(owner_key, sizeof owner_key, "%s-owner", transfer.key);
  if (SDL_GetWindowData(window, owner_key) != (void *)transfer.ownership) {
    atomic_store(state, QA_SDL_CONTEXT_RELEASED);
    qa_range_error("SDL render context ownership does not match the registered transfer");
  }
  void *context = SDL_GetWindowData(window, transfer.key);
  if (context == NULL) {
    const char *message = SDL_GetError();
    atomic_store(state, QA_SDL_CONTEXT_RELEASED);
    qa_range_error("SDL_GetWindowData context: %s", message);
  }
  if (SDL_GL_MakeCurrent(window, context) < 0) {
    const char *message = SDL_GetError();
    atomic_store(state, QA_SDL_CONTEXT_RELEASED);
    qa_range_error("SDL_GL_MakeCurrent adopt: %s", message);
  }
  memset(out, 0, sizeof *out);
  out->window = window;
  out->context = context;
  out->state = state;
  out->render_enabled = true;
  return true;
}

void qa_sdl_worker_render_context_make_current(qa_sdl_worker_render_context *context) {
  if (context->closed || atomic_load(context->state) != QA_SDL_CONTEXT_ADOPTED) {
    qa_range_error("SDL worker render context is released");
  }
  checked(SDL_GL_MakeCurrent(context->window,
                             context->render_enabled ? context->context : NULL),
          "SDL_GL_MakeCurrent worker");
}

void qa_sdl_worker_render_context_park(qa_sdl_worker_render_context *context) {
  if (context->closed) qa_range_error("SDL worker render context is released");
  if (atomic_load(context->state) == QA_SDL_CONTEXT_PARKED) return;
  if (atomic_load(context->state) != QA_SDL_CONTEXT_ADOPTED) {
    qa_range_error("SDL worker render context is not adopted");
  }
  checked(SDL_GL_MakeCurrent(context->window, NULL), "SDL_GL_MakeCurrent worker park");
  atomic_store(context->state, QA_SDL_CONTEXT_PARKED);
}

void qa_sdl_worker_render_context_resume(qa_sdl_worker_render_context *context) {
  if (context->closed) qa_range_error("SDL worker render context is released");
  if (atomic_load(context->state) == QA_SDL_CONTEXT_ADOPTED) return;
  int32_t expected = QA_SDL_CONTEXT_PARKED;
  if (!atomic_compare_exchange_strong(context->state, &expected, QA_SDL_CONTEXT_ADOPTED)) {
    qa_range_error("SDL worker render context is not parked");
  }
  if (SDL_GL_MakeCurrent(context->window,
                         context->render_enabled ? context->context : NULL) < 0) {
    const char *message = SDL_GetError();
    atomic_store(context->state, QA_SDL_CONTEXT_PARKED);
    qa_range_error("SDL_GL_MakeCurrent worker resume: %s", message);
  }
}

bool qa_sdl_worker_render_context_rendering_enabled(
    const qa_sdl_worker_render_context *context) {
  return context->render_enabled;
}

void qa_sdl_worker_render_context_set_rendering_enabled(
    qa_sdl_worker_render_context *context, bool enabled) {
  if (context->closed || atomic_load(context->state) != QA_SDL_CONTEXT_ADOPTED) {
    qa_range_error("SDL worker render context is released");
  }
  checked(SDL_GL_MakeCurrent(context->window, enabled ? context->context : NULL),
          "SDL_GL_MakeCurrent worker diagnostic");
  context->render_enabled = enabled;
}

void qa_sdl_worker_render_context_drawable_size(qa_sdl_worker_render_context *context,
                                                int32_t *out_width, int32_t *out_height) {
  qa_sdl_worker_render_context_make_current(context);
  int width = 0, height = 0;
  SDL_GL_GetDrawableSize(context->window, &width, &height);
  if (width <= 0 || height <= 0) {
    qa_range_error("SDL returned invalid drawable dimensions");
  }
  *out_width = width;
  *out_height = height;
}

void *qa_sdl_worker_render_context_get_gl_proc_address(
    qa_sdl_worker_render_context *context, const char *name) {
  if (name == NULL || name[0] == '\0') qa_range_error("Invalid GL procedure name");
  qa_sdl_worker_render_context_make_current(context);
  char operation[256];
  snprintf(operation, sizeof operation, "SDL_GL_GetProcAddress %s", name);
  return pointer(SDL_GL_GetProcAddress(name), operation);
}

qa_sdl_worker_procedure_lease qa_sdl_worker_render_context_retain_procedures(
    qa_sdl_worker_render_context *context) {
  qa_sdl_worker_render_context_make_current(context);
  context->procedure_leases++;
  qa_sdl_worker_procedure_lease lease = {context, false};
  return lease;
}

void qa_sdl_worker_procedure_lease_release(qa_sdl_worker_procedure_lease *lease) {
  if (lease->released) return;
  lease->released = true;
  lease->context->procedure_leases--;
}

void qa_sdl_worker_render_context_swap(qa_sdl_worker_render_context *context) {
  qa_sdl_worker_render_context_make_current(context);
  /* SDL requires a current window to swap; Mac flushBuffer did not. */
  if (!context->render_enabled) {
    checked(SDL_GL_MakeCurrent(context->window, context->context),
            "SDL_GL_MakeCurrent worker swap");
  }
  SDL_GL_SwapWindow(context->window);
  if (!context->render_enabled) {
    checked(SDL_GL_MakeCurrent(context->window, NULL),
            "SDL_GL_MakeCurrent worker restore diagnostic");
  }
}

void qa_sdl_worker_render_context_set_swap_interval(
    qa_sdl_worker_render_context *context, int32_t interval) {
  qa_sdl_worker_render_context_make_current(context);
  checked(SDL_GL_SetSwapInterval(interval), "SDL_GL_SetSwapInterval worker");
}

int32_t qa_sdl_worker_render_context_swap_interval(
    qa_sdl_worker_render_context *context) {
  qa_sdl_worker_render_context_make_current(context);
  int32_t value = SDL_GL_GetSwapInterval();
  if (value != -1 && value != 0 && value != 1) {
    qa_range_error("Unsupported SDL swap interval: %d", (int)value);
  }
  return value;
}

void qa_sdl_worker_render_context_release(qa_sdl_worker_render_context *context) {
  if (context->closed) return;
  if (context->procedure_leases != 0) {
    qa_range_error("Close GL procedure tables before releasing the worker context");
  }
  checked(SDL_GL_MakeCurrent(context->window, NULL),
          "SDL_GL_MakeCurrent worker release");
  context->closed = true;
  atomic_store(context->state, QA_SDL_CONTEXT_RELEASED);
}
