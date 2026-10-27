/*
 * Port of src/platform/sdl.ts: SDL2 window/context and event duties
 * (linux_glimp.c window/HandleEvents replacement; clipboard delimiter
 * handling from win_main.c Sys_GetClipboardData). C accommodations:
 * Bun FFI thunks become direct SDL2 calls; TS `number` option fields
 * stay double where validation is observable; event lists are
 * malloc'd arrays the caller frees; SdlInputLease/SdlGammaLease are
 * heap leases freed by their _destroy after _close; the rendered
 * context lease is embedded in the window. C strings cannot carry an
 * embedded NUL, so the TS cString NUL rejection is unobservable here.
 * GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_SDL_H
#define QA_PLATFORM_SDL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#include "qa/platform/sdl_render_context.h"

void qa_open_sdl_url(const char *url);

/* Malloc'd copy (length+1 bytes, trailing zero); caller frees. */
uint8_t *qa_source_clipboard_bytes(const uint8_t *bytes, size_t length, size_t *out_length);
void qa_write_sdl_clipboard(const char *text);
/* Malloc'd bytes, or NULL when SDL has no clipboard text. Caller frees. */
uint8_t *qa_read_sdl_clipboard(size_t *out_length);

typedef enum qa_sdl_backend { QA_SDL_BACKEND_CPU, QA_SDL_BACKEND_GL } qa_sdl_backend;

typedef struct qa_sdl_size {
  int32_t width;
  int32_t height;
} qa_sdl_size;

typedef struct qa_sdl_point {
  int32_t x;
  int32_t y;
} qa_sdl_point;

typedef struct qa_sdl_presentation_display_mode {
  uint32_t format;
  int32_t width;
  int32_t height;
  int32_t refresh_rate;
} qa_sdl_presentation_display_mode;

typedef enum qa_sdl_fullscreen_kind {
  QA_SDL_FULLSCREEN_WINDOWED,
  QA_SDL_FULLSCREEN_DESKTOP,
  QA_SDL_FULLSCREEN_EXCLUSIVE
} qa_sdl_fullscreen_kind;

typedef struct qa_sdl_window_presentation {
  qa_sdl_size size;
  qa_sdl_point position;
  int32_t display_index;
  qa_sdl_presentation_display_mode display_mode;
  qa_sdl_fullscreen_kind fullscreen;
  bool visible;
  bool maximized;
  bool minimized;
  bool focused;
} qa_sdl_window_presentation;

/*
 * Zero-initialized options match TS absent fields, except
 * allow_software_gl which is consulted only with has_allow_software_gl.
 * display_index zero and absent (-1) both resolve to display 0 through
 * qa_sdl_display_index. position is relative to the display origin.
 */
typedef struct qa_sdl_window_options {
  const char *title;
  double width;
  double height;
  bool hidden;
  bool resizable;
  bool fullscreen;
  double display_refresh;
  double display_index;
  double min_display_refresh;
  double max_display_refresh;
  bool has_position;
  double position_x;
  double position_y;
  qa_sdl_backend backend;
  bool stereo;          /* gl only */
  double stencil_bits;  /* gl only */
  double color_bits;    /* gl only */
  double depth_bits;    /* gl only */
  const char *driver;   /* gl only; NULL is TS absent */
  bool has_allow_software_gl;
  bool allow_software_gl;
} qa_sdl_window_options;

int32_t qa_sdl_display_index(double requested, double count);

typedef struct qa_sdl_display_mode {
  int32_t width;
  int32_t height;
  int32_t color_bits;
  int32_t refresh_rate;
} qa_sdl_display_mode;

typedef struct qa_sdl_display_mode_request {
  int32_t width;
  int32_t height;
  int32_t color_bits;
  int32_t min_display_refresh;
  int32_t max_display_refresh;
} qa_sdl_display_mode_request;

/* Index of the last exact matching mode, or -1 for TS null. */
int64_t qa_sdl_matching_display_mode(const qa_sdl_display_mode *modes, size_t count,
                                     qa_sdl_display_mode_request request);

typedef struct qa_sdl_rect {
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
} qa_sdl_rect;

typedef struct qa_sdl_display {
  int32_t index;
  char *name; /* malloc'd; free with qa_sdl_display_free */
  int32_t count;
  int32_t refresh_rate;
  qa_sdl_rect bounds;
} qa_sdl_display;

void qa_sdl_display_free(qa_sdl_display *display);

typedef enum qa_sdl_gamma_capability_kind {
  QA_SDL_GAMMA_API_ACCEPTED,
  QA_SDL_GAMMA_UNSUPPORTED,
  QA_SDL_GAMMA_UNAVAILABLE,
  QA_SDL_GAMMA_RETIRED
} qa_sdl_gamma_capability_kind;

/* String fields borrow lease storage (TS returns a shallow copy). */
typedef struct qa_sdl_gamma_capability {
  qa_sdl_gamma_capability_kind kind;
  int32_t display_index;     /* api-accepted only */
  const char *display_name;  /* api-accepted only */
  const char *reason;        /* other kinds only */
} qa_sdl_gamma_capability;

typedef enum qa_sdl_event_kind {
  QA_SDL_EVENT_QUIT,
  QA_SDL_EVENT_KEY,
  QA_SDL_EVENT_TEXT,
  QA_SDL_EVENT_MOUSE_MOTION,
  QA_SDL_EVENT_MOUSE_BUTTON,
  QA_SDL_EVENT_MOUSE_WHEEL,
  QA_SDL_EVENT_WINDOW,
  QA_SDL_EVENT_JOYSTICK_AXIS,
  QA_SDL_EVENT_JOYSTICK_HAT,
  QA_SDL_EVENT_JOYSTICK_BUTTON,
  QA_SDL_EVENT_JOYSTICK_REMOVED,
  QA_SDL_EVENT_UNSUPPORTED
} qa_sdl_event_kind;

typedef struct qa_sdl_event {
  qa_sdl_event_kind kind;
  uint32_t timestamp;
  union {
    struct {
      bool down;
      bool repeat;
      int32_t scancode;
      int32_t keycode;
      uint16_t modifiers;
    } key;
    struct {
      char text[33]; /* UTF-8, NUL-terminated */
    } text;
    struct {
      uint32_t buttons;
      int32_t x;
      int32_t y;
      int32_t dx;
      int32_t dy;
    } mouse_motion;
    struct {
      bool down;
      uint8_t button;
      uint8_t clicks;
      int32_t x;
      int32_t y;
    } mouse_button;
    struct {
      int32_t x;
      int32_t y;
      float precise_x;
      float precise_y;
      bool flipped;
    } mouse_wheel;
    struct {
      uint8_t event;
      int32_t data1;
      int32_t data2;
    } window;
    struct {
      int32_t instance;
      uint8_t axis;
      int16_t value;
    } joystick_axis;
    struct {
      int32_t instance;
      uint8_t hat;
      uint8_t value;
    } joystick_hat;
    struct {
      int32_t instance;
      uint8_t button;
      bool down;
    } joystick_button;
    struct {
      int32_t instance;
    } joystick_removed;
    struct {
      uint32_t type;
    } unsupported;
  } as;
} qa_sdl_event;

/* Decodes one 56-byte SDL_Event record (host byte order, as in TS). */
qa_sdl_event qa_decode_sdl_event(const uint8_t *bytes, size_t length);

typedef struct qa_sdl_window qa_sdl_window;

typedef struct qa_sdl_input_lease {
  qa_sdl_window *window;
  bool relative;
} qa_sdl_input_lease;

bool qa_sdl_input_lease_closed(const qa_sdl_input_lease *lease);
void qa_sdl_input_lease_set_relative_mouse(qa_sdl_input_lease *lease, bool enabled);
void qa_sdl_input_lease_close(qa_sdl_input_lease *lease);
void qa_sdl_input_lease_destroy(qa_sdl_input_lease *lease);

typedef struct qa_sdl_gamma_lease {
  qa_sdl_window *window;
  qa_sdl_gamma_capability_kind capability_kind;
  int32_t capability_display_index;
  char *capability_display_name;
  char *capability_reason;
  int32_t display_index;
  char *display_name;
  bool has_original;
  uint16_t original[3][256];
} qa_sdl_gamma_lease;

/* The TS capability getter synchronizes before returning a copy. */
qa_sdl_gamma_capability qa_sdl_gamma_lease_capability(qa_sdl_gamma_lease *lease);
bool qa_sdl_gamma_lease_closed(const qa_sdl_gamma_lease *lease);
void qa_sdl_gamma_lease_synchronize(qa_sdl_gamma_lease *lease);
void qa_sdl_gamma_lease_apply(qa_sdl_gamma_lease *lease, double gamma);
void qa_sdl_gamma_lease_close(qa_sdl_gamma_lease *lease);
void qa_sdl_gamma_lease_destroy(qa_sdl_gamma_lease *lease);

typedef struct qa_sdl_procedure_lease {
  qa_sdl_window *window;
  bool released;
} qa_sdl_procedure_lease;

void qa_sdl_procedure_lease_release(qa_sdl_procedure_lease *lease);

struct qa_sdl_window {
  qa_sdl_backend backend;
  SDL_Window *sdl_window; /* NULL once closed */
  SDL_Renderer *renderer; /* cpu */
  SDL_Texture *texture;   /* cpu */
  int32_t texture_width;  /* cpu */
  int32_t texture_height; /* cpu */
  SDL_GLContext gl_context; /* gl */
  char *driver;             /* gl; NULL is TS absent */
  uint32_t id;
  char *fullscreen_failure; /* NULL when fullscreen entered or not asked */
  int32_t position_origin_x;
  int32_t position_origin_y;
  bool has_render_lease;
  qa_sdl_render_context_lease render_lease;
  bool render_enabled;
  qa_sdl_event *pending;
  size_t pending_count;
  size_t pending_capacity;
  bool has_frame;
  qa_sdl_input_lease *input;
  qa_sdl_gamma_lease *gamma;
  int procedure_leases;
};

/* False only on OOM; invalid arguments and SDL failures abort. */
bool qa_sdl_window_open(const qa_sdl_window_options *options, qa_sdl_window *out);
/* C accommodation for moving an open window struct: retargets the
 * window registry and live lease back-pointers, empties the source.
 * Requires no outstanding procedure leases. */
void qa_sdl_window_move(qa_sdl_window *destination, qa_sdl_window *source);
void qa_sdl_window_set_visible(qa_sdl_window *window, bool visible);
qa_sdl_window_presentation qa_sdl_window_capture_presentation(qa_sdl_window *window);
void qa_sdl_window_restore_presentation(qa_sdl_window *window,
                                        const qa_sdl_window_presentation *state);
uint32_t qa_sdl_window_flags(qa_sdl_window *window);
uint32_t qa_sdl_window_ticks(qa_sdl_window *window);
bool qa_sdl_window_closed(const qa_sdl_window *window);
bool qa_sdl_window_relative_mouse(qa_sdl_window *window);
qa_sdl_backend qa_sdl_window_backend(qa_sdl_window *window);
int32_t qa_sdl_window_width(qa_sdl_window *window);
int32_t qa_sdl_window_height(qa_sdl_window *window);
qa_sdl_size qa_sdl_window_logical_size(qa_sdl_window *window);
void qa_sdl_window_set_size(qa_sdl_window *window, double width, double height);
bool qa_sdl_window_fullscreen(qa_sdl_window *window);
void qa_sdl_window_set_fullscreen(qa_sdl_window *window, bool enabled);
qa_sdl_procedure_lease qa_sdl_window_retain_procedures(qa_sdl_window *window);
/* False only on OOM copying the display name. */
bool qa_sdl_window_display(qa_sdl_window *window, qa_sdl_display *out);
/* Malloc'd mode list (includes the current mode last); caller frees. */
qa_sdl_display_mode *qa_sdl_window_display_modes(qa_sdl_window *window, size_t *out_count);
/* Heap lease, or NULL on OOM. Caller closes and destroys it. */
qa_sdl_gamma_lease *qa_sdl_window_begin_gamma(qa_sdl_window *window);
qa_sdl_input_lease *qa_sdl_window_begin_input(qa_sdl_window *window);
qa_sdl_size qa_sdl_window_drawable_size(qa_sdl_window *window);
/* Malloc'd events for this window; caller frees (NULL when empty). */
qa_sdl_event *qa_sdl_window_poll_events(qa_sdl_window *window, size_t *out_count);
void qa_sdl_window_present(qa_sdl_window *window, const uint8_t *rgba, size_t length);
/* Malloc'd drawable pixels; caller frees. NULL only on OOM. */
uint8_t *qa_sdl_window_read_pixels(qa_sdl_window *window, size_t *out_length);
void qa_sdl_window_make_current(qa_sdl_window *window);
bool qa_sdl_window_rendering_enabled(const qa_sdl_window *window);
void qa_sdl_window_set_rendering_enabled(qa_sdl_window *window, bool enabled);
qa_sdl_render_context_transfer qa_sdl_window_detach_render_context(qa_sdl_window *window);
void qa_sdl_window_restore_render_context(qa_sdl_window *window);
void *qa_sdl_window_get_gl_proc_address(qa_sdl_window *window, const char *name);
void qa_sdl_window_swap(qa_sdl_window *window);
int32_t qa_sdl_window_swap_interval(qa_sdl_window *window);
void qa_sdl_window_set_swap_interval(qa_sdl_window *window, int32_t interval);
/* Accepts only the TS SdlInjectedEvent kinds. */
void qa_sdl_window_push_event(qa_sdl_window *window, const qa_sdl_event *event);
void qa_sdl_window_close(qa_sdl_window *window);

typedef enum qa_sdl_joystick_profile {
  QA_SDL_JOYSTICK_LINUX,
  QA_SDL_JOYSTICK_WINDOWS
} qa_sdl_joystick_profile;

typedef void (*qa_sdl_joystick_print_fn)(const char *text, void *ctx);

typedef struct qa_sdl_joystick {
  SDL_Joystick *pointer; /* NULL once closed */
  int32_t instance;
  char *name;
  int32_t axes;
  int32_t buttons;
  int32_t hats;
  qa_sdl_event *pending;
  size_t pending_count;
  size_t pending_capacity;
} qa_sdl_joystick;

/* True when the event belongs to a joystick and was consumed. */
bool qa_sdl_joystick_queue_event(const qa_sdl_event *event);
/* False is TS null (no joystick opened) or OOM. print may be NULL. */
bool qa_sdl_joystick_open_first(qa_sdl_joystick_print_fn print, void *print_ctx,
                                qa_sdl_joystick_profile profile, qa_sdl_joystick *out);
/* Malloc'd events; caller frees (NULL when empty or OOM). */
qa_sdl_event *qa_sdl_joystick_poll_events(qa_sdl_joystick *joystick,
                                          qa_sdl_joystick_profile profile,
                                          size_t *out_count);
void qa_sdl_joystick_close(qa_sdl_joystick *joystick);

#endif
