#ifndef QA_DISPLAY_H
#define QA_DISPLAY_H

#include "qa/common.h"

typedef struct qa_display qa_display;
typedef struct qa_display_gamma qa_display_gamma;
typedef struct qa_display_gamma_ticket qa_display_gamma_ticket;
typedef enum qa_display_gamma_kind {
    QA_DISPLAY_GAMMA_ACCEPTED, QA_DISPLAY_GAMMA_UNSUPPORTED,
    QA_DISPLAY_GAMMA_UNAVAILABLE, QA_DISPLAY_GAMMA_RETIRED
} qa_display_gamma_kind;
typedef struct qa_display_gamma_capability {
    qa_display_gamma_kind kind;
    int32_t display_index;
    char display_name[256], reason[256];
} qa_display_gamma_capability;

typedef enum qa_display_backend {
    QA_DISPLAY_CPU,
    QA_DISPLAY_OPENGL
} qa_display_backend;

typedef enum qa_display_fullscreen {
    QA_DISPLAY_WINDOWED,
    QA_DISPLAY_DESKTOP,
    QA_DISPLAY_EXCLUSIVE
} qa_display_fullscreen;

typedef struct qa_display_options {
    const char *title;
    uint32_t width, height;
    qa_display_backend backend;
    bool hidden, resizable, high_dpi;
    bool positioned;
    int32_t x, y;
    int display_index;
    qa_display_fullscreen fullscreen;
    int refresh_rate, minimum_refresh, maximum_refresh;
    unsigned color_bits, depth_bits, stencil_bits;
    bool stereo, allow_software_gl, allow_fullscreen_fallback;
    /* NULL selects SDL's system OpenGL library. */
    const char *gl_library;
} qa_display_options;

typedef struct qa_display_info {
    uint32_t window_id;
    uint32_t logical_width, logical_height;
    uint32_t drawable_width, drawable_height;
    int display_index, refresh_rate;
    qa_display_backend backend;
    qa_display_fullscreen fullscreen;
    bool visible, focused, minimized, maximized;
} qa_display_info;

void qa_display_options_default(qa_display_options *options);
/* The application owns SDL_INIT_VIDEO and SDL_Quit. A display owns only its
 * SDL window, CPU presentation objects, and optional OpenGL context. */
qa_display *qa_display_create(const qa_display_options *options, qa_error *error);
void qa_display_destroy(qa_display *display);
bool qa_display_info_get(const qa_display *display, qa_display_info *out,
                         qa_error *error);
bool qa_display_set_visible(qa_display *display, bool visible, qa_error *error);
bool qa_display_set_size(qa_display *display, uint32_t width, uint32_t height,
                         qa_error *error);
bool qa_display_set_fullscreen(qa_display *display, qa_display_fullscreen mode,
                               qa_error *error);
/* Empty when the requested mode was entered. A permitted exclusive-mode
 * fallback records its diagnostic here while leaving a usable window. */
const char *qa_display_fullscreen_failure(const qa_display *display);

bool qa_display_make_current(qa_display *display, qa_error *error);
void *qa_display_gl_proc(qa_display *display, const char *name, qa_error *error);
bool qa_display_swap(qa_display *display, qa_error *error);
bool qa_display_set_swap_interval(qa_display *display, int interval,
                                  qa_error *error);
bool qa_display_swap_interval(qa_display *display, int *out, qa_error *error);

/* CPU pixels are tightly packed RGBA8 rows from top to bottom. Presentation
 * retains the last texture, so capture remains valid after SDL invalidates its
 * backbuffer during SDL_RenderPresent. */
bool qa_display_present_rgba(qa_display *display, qa_bytes rgba,
                             uint32_t width, uint32_t height, qa_error *error);
bool qa_display_present_cpu(void *display, qa_bytes rgba, uint32_t width,
                            uint32_t height, qa_error *error);
bool qa_display_capture_cpu(qa_display *display, qa_buffer *out,
                            uint32_t *width, uint32_t *height, qa_error *error);

/* One actual native gamma window in the single-display/focused profile.
 * Acceptance is Get+Set success, not a claim of physical hardware readback.
 * Failed checked release retains the lease and its display for retry. */
bool qa_display_gamma_begin(qa_display *, bool ignore_hardware, qa_display_gamma **, qa_error *);
bool qa_display_gamma_read(qa_display_gamma *, qa_display_gamma_capability *, qa_error *);
bool qa_display_gamma_capability_read(const qa_display_gamma *, qa_display_gamma_capability *);
bool qa_display_gamma_apply(qa_display_gamma *, float gamma, qa_error *);
bool qa_display_gamma_release(qa_display_gamma **, qa_error *);
/* Actual native-window aliases used by a detached display continuation. The
 * installed native owner keeps the lease alive through checked handoff. */
qa_display_gamma *qa_display_gamma_borrow(const qa_display *);
bool qa_display_gamma_parent_is(const qa_display_gamma *, const qa_display *);
/* Pure proof of an actually applied native ramp on this exact window lease. */
bool qa_display_gamma_applied_is(const qa_display *);
bool qa_display_gamma_prepare(qa_display_gamma *, qa_display *target, float gamma,
    qa_display_gamma_ticket **, qa_error *);
bool qa_display_gamma_ready(qa_display_gamma_ticket *, qa_error *);
bool qa_display_gamma_ready_is(const qa_display_gamma_ticket *);
void qa_display_gamma_publish(qa_display_gamma_ticket *);
bool qa_display_gamma_abort(qa_display_gamma_ticket **, qa_error *);
bool qa_display_gamma_finish(qa_display_gamma_ticket **, qa_error *);

#endif
