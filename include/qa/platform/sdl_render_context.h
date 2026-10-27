/*
 * Port of src/platform/sdl-render-context.ts: SDL replacement for the
 * linux_glimp.c GLimp SMP context handoff. C accommodations: the
 * SharedArrayBuffer ownership word is a heap _Atomic int32_t shared by
 * pointer (Atomics.notify has no waiter to wake in this port); Bun
 * main/worker thread checks have no C counterpart, so thread affinity
 * is the caller's duty (detach/restore on the window-owning thread,
 * adopt on the render thread). The TS SdlRenderContext interface is
 * type-only; qa_sdl_window (qa/platform/sdl.h) and
 * qa_sdl_worker_render_context both provide its operations.
 * GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_SDL_RENDER_CONTEXT_H
#define QA_PLATFORM_SDL_RENDER_CONTEXT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include <SDL2/SDL.h>

/* Ownership word values, in TS order. */
enum {
  QA_SDL_CONTEXT_OFFERED = 0,
  QA_SDL_CONTEXT_ADOPTED = 1,
  QA_SDL_CONTEXT_RELEASED = 2,
  QA_SDL_CONTEXT_RESTORING = 3,
  QA_SDL_CONTEXT_RESTORED = 4,
  QA_SDL_CONTEXT_PARKED = 5
};

/** Same-process worker token. Native pointers stay in SDL's window data. */
typedef struct qa_sdl_render_context_transfer {
  uint32_t window_id;
  char key[64]; /* "quake-render-" plus 36 [0-9a-f-] chars */
  _Atomic int32_t *ownership; /* heap word owned by the lease */
} qa_sdl_render_context_transfer;

/** The window owner keeps this lease until cancellation or worker release. */
typedef struct qa_sdl_render_context_lease {
  SDL_Window *window;
  SDL_GLContext context;
  qa_sdl_render_context_transfer transfer;
} qa_sdl_render_context_lease;

/* False only on OOM. The context must be current before detach. */
bool qa_sdl_render_context_lease_detach(SDL_Window *window, SDL_GLContext context,
                                        uint32_t window_id,
                                        qa_sdl_render_context_lease *out);
bool qa_sdl_render_context_lease_parked(const qa_sdl_render_context_lease *lease);
/** Cancels an unadopted token, or restores after the worker cleared current. */
void qa_sdl_render_context_lease_restore(qa_sdl_render_context_lease *lease);
/* Frees the ownership word; call only after restore. */
void qa_sdl_render_context_lease_destroy(qa_sdl_render_context_lease *lease);

typedef struct qa_sdl_worker_render_context qa_sdl_worker_render_context;

typedef struct qa_sdl_worker_procedure_lease {
  qa_sdl_worker_render_context *context;
  bool released;
} qa_sdl_worker_procedure_lease;

/** Borrows the main thread's existing window and context, never SDL video lifetime. */
struct qa_sdl_worker_render_context {
  SDL_Window *window;
  SDL_GLContext context;
  _Atomic int32_t *state;
  bool closed;
  bool render_enabled;
  int procedure_leases;
};

bool qa_sdl_worker_render_context_adopt(const qa_sdl_render_context_transfer *transfer,
                                        qa_sdl_worker_render_context *out);
void qa_sdl_worker_render_context_make_current(qa_sdl_worker_render_context *context);
void qa_sdl_worker_render_context_park(qa_sdl_worker_render_context *context);
void qa_sdl_worker_render_context_resume(qa_sdl_worker_render_context *context);
bool qa_sdl_worker_render_context_rendering_enabled(const qa_sdl_worker_render_context *context);
void qa_sdl_worker_render_context_set_rendering_enabled(qa_sdl_worker_render_context *context,
                                                        bool enabled);
void qa_sdl_worker_render_context_drawable_size(qa_sdl_worker_render_context *context,
                                                int32_t *out_width, int32_t *out_height);
void *qa_sdl_worker_render_context_get_gl_proc_address(qa_sdl_worker_render_context *context,
                                                       const char *name);
qa_sdl_worker_procedure_lease qa_sdl_worker_render_context_retain_procedures(
    qa_sdl_worker_render_context *context);
void qa_sdl_worker_procedure_lease_release(qa_sdl_worker_procedure_lease *lease);
void qa_sdl_worker_render_context_swap(qa_sdl_worker_render_context *context);
void qa_sdl_worker_render_context_set_swap_interval(qa_sdl_worker_render_context *context,
                                                    int32_t interval);
int32_t qa_sdl_worker_render_context_swap_interval(qa_sdl_worker_render_context *context);
void qa_sdl_worker_render_context_release(qa_sdl_worker_render_context *context);

#endif
