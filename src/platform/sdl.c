/* Port of src/platform/sdl.ts. GPL-2.0-or-later. */
#include "qa/platform/sdl.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/platform/native_libraries.h"

static const uint32_t video_subsystem = 0x20;

static void ensure_main_ready(void) {
  static bool ready = false;
  if (!ready) {
    SDL_SetMainReady();
    ready = true;
  }
}

static void checked(int result, const char *operation) {
  if (result < 0) qa_range_error("%s: %s", operation, SDL_GetError());
}

static void *handle(void *result, const char *operation) {
  if (result == NULL) qa_range_error("%s: %s", operation, SDL_GetError());
  return result;
}

static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

/* TS String(cstring) renders a NULL SDL string as "null". */
static char *sdl_string(const char *text) {
  return xstrdup(text == NULL ? "null" : text);
}

static bool streq_nullable(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

static bool ascii_iequals(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    a++;
    b++;
  }
  return *a == *b;
}

static void initialize_subsystem(uint32_t subsystem, const char *operation) {
  ensure_main_ready();
  if (SDL_SetHint("SDL_NO_SIGNAL_HANDLERS", "1") != 1) {
    qa_range_error("SDL must leave signal handling to the Unix signal owner");
  }
  checked(SDL_InitSubSystem(subsystem), operation);
}

void qa_open_sdl_url(const char *url) {
  ensure_main_ready();
  if (SDL_OpenURL(url == NULL ? "" : url) != 0) {
    qa_range_error("Could not open the sign-in browser.");
  }
}

/** Win32 Sys_GetClipboardData's strtok(data, "\n\r\b") keeps leading delimiters. */
uint8_t *qa_source_clipboard_bytes(const uint8_t *bytes, size_t length,
                                   size_t *out_length) {
  size_t kept = 0;
  bool token = false;
  for (size_t i = 0; i < length; i++) {
    uint8_t byte = bytes[i];
    if (byte == 0) break;
    bool delimiter = byte == 10 || byte == 13 || byte == 8;
    if (delimiter && token) break;
    if (!delimiter) token = true;
    kept++;
  }
  uint8_t *result = (uint8_t *)calloc(kept + 1, 1);
  if (result == NULL) {
    *out_length = 0;
    return NULL;
  }
  memcpy(result, bytes, kept);
  *out_length = kept + 1;
  return result;
}

void qa_write_sdl_clipboard(const char *text) {
  ensure_main_ready();
  checked(SDL_SetClipboardText(text == NULL ? "" : text), "SDL_SetClipboardText");
}

uint8_t *qa_read_sdl_clipboard(size_t *out_length) {
  ensure_main_ready();
  char *allocation = SDL_GetClipboardText();
  if (allocation == NULL) {
    *out_length = 0;
    return NULL;
  }
  /* SDL owns the NUL-terminated allocation. Copy without decoding. */
  uint8_t *result = qa_source_clipboard_bytes((const uint8_t *)allocation,
                                              strlen(allocation), out_length);
  SDL_free(allocation);
  return result;
}

static void native_integer(double value, const char *name) {
  if (!isfinite(value) || trunc(value) != value || value < -2147483648.0 ||
      value > 2147483647.0) {
    qa_range_error("%s must be a signed 32-bit integer", name);
  }
}

/** Linux tests the stored cvar float for zero before assigning it to an int. */
static int32_t source_visual_precision(double value, const char *name) {
  float numeric = (float)value; /* Math.fround staging */
  if (numeric == 0.0f) return 24;
  double integer = trunc((double)numeric);
  native_integer(integer, name);
  return (int32_t)integer;
}

/** macosx_input.m Sys_DisplayToUse falls back to the main display. */
int32_t qa_sdl_display_index(double requested, double count) {
  native_integer(requested, "SDL display index");
  if (!isfinite(count) || trunc(count) != count || count <= 0) {
    qa_range_error("SDL has no video displays");
  }
  return requested < 0 || requested >= count ? 0 : (int32_t)requested;
}

static void validate_refresh_limits(double min, double max) {
  native_integer(min, "Minimum display refresh");
  native_integer(max, "Maximum display refresh");
  if (min != 0 && max != 0 && min > max) {
    qa_range_error("r_minDisplayRefresh must be less than or equal to r_maxDisplayRefresh");
  }
}

/** macosx_display.m Sys_GetMatchingDisplayMode retains the last exact match. */
int64_t qa_sdl_matching_display_mode(const qa_sdl_display_mode *modes, size_t count,
                                     qa_sdl_display_mode_request request) {
  validate_refresh_limits(request.min_display_refresh, request.max_display_refresh);
  int64_t selected = -1;
  for (size_t i = 0; i < count; i++) {
    const qa_sdl_display_mode *mode = &modes[i];
    if (mode->width != request.width || mode->height != request.height ||
        mode->color_bits != request.color_bits) {
      continue;
    }
    if (request.min_display_refresh != 0 && mode->refresh_rate < request.min_display_refresh) {
      continue;
    }
    if (request.max_display_refresh != 0 && mode->refresh_rate > request.max_display_refresh) {
      continue;
    }
    selected = (int64_t)i;
  }
  return selected;
}

static qa_sdl_display_mode display_mode_of(const SDL_DisplayMode *mode) {
  qa_sdl_display_mode out;
  out.color_bits = (int32_t)((mode->format >> 8) & 255u);
  out.width = mode->w;
  out.height = mode->h;
  out.refresh_rate = mode->refresh_rate;
  return out;
}

void qa_sdl_display_free(qa_sdl_display *display) {
  free(display->name);
  memset(display, 0, sizeof *display);
}

/* Host-order field reads, matching the TS DataView with host endianness. */
static uint32_t read_u32(const uint8_t *bytes, size_t length, size_t offset) {
  if (offset + 4 > length) qa_range_error("SDL event field is outside the record");
  uint32_t value;
  memcpy(&value, bytes + offset, sizeof value);
  return value;
}

static int32_t read_i32(const uint8_t *bytes, size_t length, size_t offset) {
  uint32_t bits = read_u32(bytes, length, offset);
  int32_t value;
  memcpy(&value, &bits, sizeof value);
  return value;
}

static uint8_t read_u8(const uint8_t *bytes, size_t length, size_t offset) {
  if (offset + 1 > length) qa_range_error("SDL event field is outside the record");
  return bytes[offset];
}

static uint16_t read_u16(const uint8_t *bytes, size_t length, size_t offset) {
  if (offset + 2 > length) qa_range_error("SDL event field is outside the record");
  uint16_t value;
  memcpy(&value, bytes + offset, sizeof value);
  return value;
}

static int16_t read_i16(const uint8_t *bytes, size_t length, size_t offset) {
  uint16_t bits = read_u16(bytes, length, offset);
  int16_t value;
  memcpy(&value, &bits, sizeof value);
  return value;
}

static float read_f32(const uint8_t *bytes, size_t length, size_t offset) {
  uint32_t bits = read_u32(bytes, length, offset);
  float value;
  memcpy(&value, &bits, sizeof value);
  return value;
}

/* TS decodes text with a fatal UTF-8 TextDecoder. */
static void validate_utf8(const uint8_t *bytes, size_t length) {
  size_t i = 0;
  while (i < length) {
    uint8_t lead = bytes[i];
    size_t extra = lead < 0x80 ? 0 : lead >= 0xc2 && lead < 0xe0 ? 1
                   : lead >= 0xe0 && lead < 0xf0                ? 2
                   : lead >= 0xf0 && lead < 0xf5                ? 3
                                                                : SIZE_MAX;
    if (extra == SIZE_MAX || i + 1 + extra > length) {
      qa_range_error("SDL text event is not valid UTF-8");
    }
    for (size_t k = 1; k <= extra; k++) {
      if ((bytes[i + k] & 0xc0) != 0x80) {
        qa_range_error("SDL text event is not valid UTF-8");
      }
    }
    if (extra == 2 && lead == 0xe0 && bytes[i + 1] < 0xa0) {
      qa_range_error("SDL text event is not valid UTF-8");
    }
    if (extra == 2 && lead == 0xed && bytes[i + 1] > 0x9f) {
      qa_range_error("SDL text event is not valid UTF-8");
    }
    if (extra == 3 && lead == 0xf0 && bytes[i + 1] < 0x90) {
      qa_range_error("SDL text event is not valid UTF-8");
    }
    if (extra == 3 && lead == 0xf4 && bytes[i + 1] > 0x8f) {
      qa_range_error("SDL text event is not valid UTF-8");
    }
    i += 1 + extra;
  }
}

qa_sdl_event qa_decode_sdl_event(const uint8_t *bytes, size_t length) {
  qa_sdl_event event;
  memset(&event, 0, sizeof event);
  uint32_t type = read_u32(bytes, length, 0);
  event.timestamp = read_u32(bytes, length, 4);
  switch (type) {
    case 0x100: event.kind = QA_SDL_EVENT_QUIT; break;
    case 0x200:
      event.kind = QA_SDL_EVENT_WINDOW;
      event.as.window.event = read_u8(bytes, length, 12);
      event.as.window.data1 = read_i32(bytes, length, 16);
      event.as.window.data2 = read_i32(bytes, length, 20);
      break;
    case 0x300:
    case 0x301:
      event.kind = QA_SDL_EVENT_KEY;
      event.as.key.down = type == 0x300;
      event.as.key.repeat = read_u8(bytes, length, 13) != 0;
      event.as.key.scancode = read_i32(bytes, length, 16);
      event.as.key.keycode = read_i32(bytes, length, 20);
      event.as.key.modifiers = read_u16(bytes, length, 24);
      break;
    case 0x303: {
      event.kind = QA_SDL_EVENT_TEXT;
      size_t available = length > 12 ? length - 12 : 0;
      if (available > 32) available = 32;
      size_t end = 0;
      while (end < available && bytes[12 + end] != 0) end++;
      validate_utf8(bytes + 12, end);
      memcpy(event.as.text.text, bytes + 12, end);
      event.as.text.text[end] = '\0';
      break;
    }
    case 0x400:
      event.kind = QA_SDL_EVENT_MOUSE_MOTION;
      event.as.mouse_motion.buttons = read_u32(bytes, length, 16);
      event.as.mouse_motion.x = read_i32(bytes, length, 20);
      event.as.mouse_motion.y = read_i32(bytes, length, 24);
      event.as.mouse_motion.dx = read_i32(bytes, length, 28);
      event.as.mouse_motion.dy = read_i32(bytes, length, 32);
      break;
    case 0x401:
    case 0x402:
      event.kind = QA_SDL_EVENT_MOUSE_BUTTON;
      event.as.mouse_button.down = type == 0x401;
      event.as.mouse_button.button = read_u8(bytes, length, 16);
      event.as.mouse_button.clicks = read_u8(bytes, length, 18);
      event.as.mouse_button.x = read_i32(bytes, length, 20);
      event.as.mouse_button.y = read_i32(bytes, length, 24);
      break;
    case 0x403:
      event.kind = QA_SDL_EVENT_MOUSE_WHEEL;
      event.as.mouse_wheel.x = read_i32(bytes, length, 16);
      event.as.mouse_wheel.y = read_i32(bytes, length, 20);
      event.as.mouse_wheel.precise_x = read_f32(bytes, length, 28);
      event.as.mouse_wheel.precise_y = read_f32(bytes, length, 32);
      event.as.mouse_wheel.flipped = read_u32(bytes, length, 24) == 1;
      break;
    case 0x600:
      event.kind = QA_SDL_EVENT_JOYSTICK_AXIS;
      event.as.joystick_axis.instance = read_i32(bytes, length, 8);
      event.as.joystick_axis.axis = read_u8(bytes, length, 12);
      event.as.joystick_axis.value = read_i16(bytes, length, 16);
      break;
    case 0x602:
      event.kind = QA_SDL_EVENT_JOYSTICK_HAT;
      event.as.joystick_hat.instance = read_i32(bytes, length, 8);
      event.as.joystick_hat.hat = read_u8(bytes, length, 12);
      event.as.joystick_hat.value = read_u8(bytes, length, 13);
      break;
    case 0x603:
    case 0x604:
      event.kind = QA_SDL_EVENT_JOYSTICK_BUTTON;
      event.as.joystick_button.instance = read_i32(bytes, length, 8);
      event.as.joystick_button.button = read_u8(bytes, length, 12);
      event.as.joystick_button.down = type == 0x603;
      break;
    case 0x606:
      event.kind = QA_SDL_EVENT_JOYSTICK_REMOVED;
      event.as.joystick_removed.instance = read_i32(bytes, length, 8);
      break;
    default:
      event.kind = QA_SDL_EVENT_UNSUPPORTED;
      event.as.unsupported.type = type;
      break;
  }
  return event;
}

static qa_sdl_window **g_windows = NULL;
static size_t g_window_count = 0;
static size_t g_window_capacity = 0;
static qa_sdl_input_lease *g_input_lease = NULL;
static qa_sdl_gamma_lease *g_gamma_lease = NULL;
static qa_sdl_joystick **g_joysticks = NULL;
static size_t g_joystick_count = 0;
static size_t g_joystick_capacity = 0;

static bool registry_add_window(qa_sdl_window *window) {
  if (g_window_count == g_window_capacity) {
    size_t grown = g_window_capacity == 0 ? 4 : g_window_capacity * 2;
    qa_sdl_window **next =
        (qa_sdl_window **)realloc(g_windows, grown * sizeof *next);
    if (next == NULL) return false;
    g_windows = next;
    g_window_capacity = grown;
  }
  g_windows[g_window_count++] = window;
  return true;
}

static void registry_remove_window(const qa_sdl_window *window) {
  for (size_t i = 0; i < g_window_count; i++) {
    if (g_windows[i] == window) {
      memmove(&g_windows[i], &g_windows[i + 1],
              (g_window_count - i - 1) * sizeof *g_windows);
      g_window_count--;
      return;
    }
  }
}

static qa_sdl_window *registry_by_id(uint32_t id) {
  for (size_t i = 0; i < g_window_count; i++) {
    if (g_windows[i]->id == id) return g_windows[i];
  }
  return NULL;
}

static bool pending_push(qa_sdl_event **pending, size_t *count, size_t *capacity,
                         const qa_sdl_event *event) {
  if (*count == *capacity) {
    size_t grown = *capacity == 0 ? 8 : *capacity * 2;
    qa_sdl_event *next = (qa_sdl_event *)realloc(*pending, grown * sizeof *next);
    if (next == NULL) return false;
    *pending = next;
    *capacity = grown;
  }
  (*pending)[(*count)++] = *event;
  return true;
}

static SDL_Window *opened(const qa_sdl_window *window) {
  if (window->sdl_window == NULL) qa_range_error("SDL window is closed");
  return window->sdl_window;
}

static void require_window_list_ownership(void) {
  for (size_t i = 0; i < g_window_count; i++) {
    qa_sdl_window *window = g_windows[i];
    if (window->has_render_lease &&
        !qa_sdl_render_context_lease_parked(&window->render_lease)) {
      qa_range_error("SDL window list is reserved for the render worker");
    }
  }
}

/* TS fullscreen(): returns a malloc'd failure string, or NULL on entry. */
static char *apply_fullscreen(SDL_Window *window, const qa_sdl_window_options *options) {
  if (!options->fullscreen) return NULL;
  int32_t refresh = (int32_t)options->display_refresh;
  int index = SDL_GetWindowDisplayIndex(window);
  checked(index, "SDL_GetWindowDisplayIndex");
  SDL_DisplayMode closest;
  memset(&closest, 0, sizeof closest);
  int32_t min = (int32_t)options->min_display_refresh;
  int32_t max = (int32_t)options->max_display_refresh;
  bool found;
  if (min != 0 || max != 0) {
    int count = SDL_GetNumDisplayModes(index);
    checked(count, "SDL_GetNumDisplayModes");
    SDL_DisplayMode desktop;
    checked(SDL_GetCurrentDisplayMode(index, &desktop), "SDL_GetCurrentDisplayMode");
    int32_t requested_color =
        options->backend == QA_SDL_BACKEND_GL ? (int32_t)trunc(options->color_bits) : 0;
    int32_t color_bits =
        requested_color < 16 ? display_mode_of(&desktop).color_bits : requested_color;
    SDL_DisplayMode *native_modes =
        (SDL_DisplayMode *)calloc((size_t)(count > 0 ? count : 1), sizeof *native_modes);
    qa_sdl_display_mode *modes =
        (qa_sdl_display_mode *)calloc((size_t)(count > 0 ? count : 1), sizeof *modes);
    if (native_modes == NULL || modes == NULL) {
      free(native_modes);
      free(modes);
      qa_range_error("Selected SDL display mode disappeared");
    }
    for (int i = 0; i < count; i++) {
      checked(SDL_GetDisplayMode(index, i, &native_modes[i]), "SDL_GetDisplayMode");
      modes[i] = display_mode_of(&native_modes[i]);
    }
    qa_sdl_display_mode_request request = {(int32_t)options->width,
                                           (int32_t)options->height, color_bits, min, max};
    int64_t selected = qa_sdl_matching_display_mode(modes, (size_t)count, request);
    found = selected >= 0;
    if (found) closest = native_modes[selected];
    free(native_modes);
    free(modes);
  } else {
    SDL_DisplayMode requested;
    memset(&requested, 0, sizeof requested);
    requested.w = (int)options->width;
    requested.h = (int)options->height;
    requested.refresh_rate = refresh;
    found = SDL_GetClosestDisplayMode(index, &requested, &closest) != NULL;
  }
  char failure[512];
  if (!found) {
    if (min != 0 || max != 0) {
      snprintf(failure, sizeof failure,
               "No suitable display mode available within the refresh limits.");
    } else {
      snprintf(failure, sizeof failure, "SDL_GetClosestDisplayMode: %s", SDL_GetError());
    }
    if ((SDL_GetWindowFlags(window) & 1u) != 0) {
      qa_range_error("%s; SDL's newly created window is unexpectedly fullscreen", failure);
    }
    return xstrdup(failure);
  }
  if (SDL_SetWindowDisplayMode(window, &closest) < 0) {
    snprintf(failure, sizeof failure, "SDL_SetWindowDisplayMode: %s", SDL_GetError());
  } else if (SDL_SetWindowFullscreen(window, 1) < 0) {
    snprintf(failure, sizeof failure, "SDL_SetWindowFullscreen: %s", SDL_GetError());
  } else if ((SDL_GetWindowFlags(window) & 1u) != 0) {
    return NULL;
  } else {
    snprintf(failure, sizeof failure, "SDL did not enter the requested fullscreen mode");
  }
  /* Continue only after restoring the actual window. */
  char operation[640];
  snprintf(operation, sizeof operation,
           "SDL_SetWindowFullscreen windowed fallback after %s", failure);
  checked(SDL_SetWindowFullscreen(window, 0), operation);
  if ((SDL_GetWindowFlags(window) & 1u) != 0) {
    qa_range_error("%s; SDL did not restore windowed mode", failure);
  }
  return xstrdup(failure);
}

static int32_t reduce_bits(int32_t bits) {
  return bits == 24 ? 16 : bits == 16 ? 8 : bits;
}

/* One GLW_SetMode visual attempt; false cleans up and tries the next. */
static bool visual_attempt(const char *title, int x, int y, int width, int height,
                           uint32_t flags, int32_t component, int32_t depth,
                           int32_t stencil, SDL_Window **out_window,
                           SDL_GLContext *out_context) {
  *out_window = NULL;
  *out_context = NULL;
  for (int attribute = 0; attribute < 3; attribute++) {
    if (SDL_GL_SetAttribute((SDL_GLattr)attribute, component) < 0) return false;
  }
  if (SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depth) < 0) return false;
  if (SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, stencil) < 0) return false;
  SDL_Window *window = SDL_CreateWindow(title, x, y, width, height, flags);
  if (window == NULL) return false;
  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (context == NULL || SDL_GL_MakeCurrent(window, context) < 0) {
    if (context != NULL) SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    return false;
  }
  const int attributes[6] = {0, 1, 2, 3, 6, 7};
  const int32_t minima[6] = {component, component, component, 0, depth, stencil};
  for (int i = 0; i < 6; i++) {
    int actual = -1;
    if (SDL_GL_GetAttribute((SDL_GLattr)attributes[i], &actual) < 0 || actual < 0 ||
        actual < minima[i]) {
      SDL_GL_DeleteContext(context);
      SDL_DestroyWindow(window);
      return false;
    }
  }
  *out_window = window;
  *out_context = context;
  return true;
}

bool qa_sdl_window_open(const qa_sdl_window_options *options, qa_sdl_window *out) {
  require_window_list_ownership();
  const double dimensions[2] = {options->width, options->height};
  for (int i = 0; i < 2; i++) {
    if (!isfinite(dimensions[i]) || trunc(dimensions[i]) != dimensions[i] ||
        dimensions[i] <= 0 || dimensions[i] > 16384) {
      qa_range_error("SDL window dimensions must be integers in 1..16384");
    }
  }
  native_integer(options->display_refresh, "SDL display refresh");
  native_integer(options->display_index, "SDL display index");
  validate_refresh_limits(options->min_display_refresh, options->max_display_refresh);
  if (options->has_position) {
    native_integer(options->position_x, "SDL window x");
    native_integer(options->position_y, "SDL window y");
  }
  bool gl = options->backend == QA_SDL_BACKEND_GL;
  int32_t color_bits = gl ? source_visual_precision(options->color_bits,
                                                   "SDL color precision")
                          : 0;
  int32_t depth_bits = gl ? source_visual_precision(options->depth_bits,
                                                   "SDL depth precision")
                          : 0;
  if (gl) {
    double stencil = options->stencil_bits;
    if (!isfinite(stencil) || trunc(stencil) != stencil || stencil < 0 || stencil > 32) {
      qa_range_error("SDL stencil precision must be an integer in 0..32");
    }
  }
  const char *title = options->title == NULL ? "" : options->title;
  const char *driver = gl ? options->driver : NULL;
  bool sharing_driver = false;
  if (driver != NULL && driver[0] == '\0') {
    qa_range_error("SDL GL driver name is empty");
  }
  if (gl) {
    for (size_t i = 0; i < g_window_count; i++) {
      qa_sdl_window *existing = g_windows[i];
      if (existing->sdl_window == NULL || existing->backend != QA_SDL_BACKEND_GL) {
        continue;
      }
      if (!streq_nullable(existing->driver, driver)) {
        qa_range_error("SDL cannot select different GL libraries for concurrent windows");
      }
      sharing_driver = true;
    }
  }
  /* SDL's loader can accept a path without loading it; only the system
   * GL library is supported, not legacy vendor drivers. */
  if (driver != NULL && !ascii_iequals(driver, qa_default_open_gl_driver(NULL))) {
    qa_range_error("Unsupported SDL system OpenGL driver: %s", driver);
  }
  initialize_subsystem(video_subsystem, "SDL_InitSubSystem");
  SDL_Window *window = NULL;
  SDL_Renderer *renderer = NULL;
  SDL_Texture *texture = NULL;
  SDL_GLContext context = NULL;
  bool driver_loaded = false;
  char *driver_copy = NULL;
  char *fullscreen_failure = NULL;
  /* Fatal failures below abort (the TS throw reaches the crash handler);
   * only the recoverable GL visual attempts and OOM clean up here. */
  int32_t display_index = qa_sdl_display_index(options->display_index,
                                               SDL_GetNumVideoDisplays());
  SDL_Rect bounds;
  checked(SDL_GetDisplayBounds(display_index, &bounds), "SDL_GetDisplayBounds");
  int32_t origin_x = bounds.x, origin_y = bounds.y;
  int64_t x, y;
  if (options->fullscreen || !options->has_position) {
    x = 0x1fff0000 | display_index;
    y = 0x1fff0000 | display_index;
  } else {
    x = (int64_t)origin_x + (int64_t)options->position_x;
    y = (int64_t)origin_y + (int64_t)options->position_y;
  }
  native_integer((double)x, "SDL window x");
  native_integer((double)y, "SDL window y");
  /* These event types own native strings, outside the game input contract. */
  SDL_EventState(0x1000, SDL_DISABLE);
  SDL_EventState(0x1001, SDL_DISABLE);
  SDL_EventState(0x305, SDL_DISABLE);
  uint32_t flags = (options->hidden ? 0x8u : 0x4u) | (gl ? 0x2u : 0u) |
                   (options->resizable ? 0x20u : 0u);
  int width = (int)options->width, height = (int)options->height;
  if (gl) {
    if (driver != NULL) {
      checked(SDL_GL_LoadLibrary(sharing_driver ? NULL : driver), "SDL_GL_LoadLibrary");
      driver_loaded = true;
    }
    checked(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2),
            "SDL_GL_CONTEXT_MAJOR_VERSION");
    checked(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1),
            "SDL_GL_CONTEXT_MINOR_VERSION");
    checked(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 2),
            "SDL_GL_CONTEXT_PROFILE_COMPATIBILITY");
    checked(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1), "SDL_GL_DOUBLEBUFFER");
    checked(SDL_GL_SetAttribute(SDL_GL_STEREO, options->stereo ? 1 : 0), "SDL_GL_STEREO");
    checked(SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0), "SDL_GL_ALPHA_SIZE");
    /* GLW_SetMode's sixteen visual attempts. */
    int32_t color = color_bits, depth = depth_bits;
    int32_t stencil = (int32_t)options->stencil_bits;
    for (int index = 0; index < 16 && window == NULL; index++) {
      if (index == 4) {
        depth = reduce_bits(depth);
        stencil = reduce_bits(stencil);
      }
      if (index == 8 && color == 24) color = 16;
      if (index == 12) stencil = reduce_bits(stencil);
      int32_t candidate_color = index % 4 == 3 && color == 24 ? 16 : color;
      int32_t component = candidate_color == 24 ? 8 : 4;
      int32_t attempt_depth = index % 4 == 2 ? reduce_bits(depth) : depth;
      int32_t attempt_stencil =
          index % 4 == 1 ? (stencil == 24 ? 16 : stencil == 16 ? 8 : 0) : stencil;
      visual_attempt(title, (int)x, (int)y, width, height, flags, component,
                     attempt_depth, attempt_stencil, &window, &context);
    }
    if (window == NULL || context == NULL) {
      if (driver_loaded) SDL_GL_UnloadLibrary();
      SDL_QuitSubSystem(video_subsystem);
      qa_range_error("SDL could not create any source GL visual candidate");
    }
    if (options->has_allow_software_gl && !options->allow_software_gl) {
      typedef const unsigned char *(*gl_get_string_fn)(uint32_t);
      void *proc = handle(SDL_GL_GetProcAddress("glGetString"),
                          "SDL_GL_GetProcAddress glGetString");
      gl_get_string_fn gl_get_string;
      memcpy(&gl_get_string, &proc, sizeof gl_get_string);
      const unsigned char *renderer_name = gl_get_string(0x1f01);
      const char *name = renderer_name == NULL ? "" : (const char *)renderer_name;
      /* GLW_SetMode rejects only these two source tokens. */
      if (ascii_iequals(name, "mesa x11") || ascii_iequals(name, "mesa glx indirect")) {
        qa_range_error("You are using software Mesa; add +set r_allowSoftwareGL 1 "
                       "to allow this driver");
      }
    }
    driver_copy = driver == NULL ? NULL : xstrdup(driver);
    if (driver != NULL && driver_copy == NULL) goto oom;
  } else {
    window = (SDL_Window *)handle(
        SDL_CreateWindow(title, (int)x, (int)y, width, height, flags), "SDL_CreateWindow");
  }
  fullscreen_failure = apply_fullscreen(window, options);
  if (options->fullscreen && fullscreen_failure == NULL) {
    /* Entered fullscreen, or xstrdup failed; both read as TS null here. */
  }
  int drawable_width = 0, drawable_height = 0;
  memset(out, 0, sizeof *out);
  out->backend = options->backend;
  out->sdl_window = window;
  out->render_enabled = true;
  if (!gl) {
    renderer = (SDL_Renderer *)handle(SDL_CreateRenderer(window, -1, 1),
                                      "SDL_CreateRenderer SOFTWARE");
    checked(SDL_GetRendererOutputSize(renderer, &drawable_width, &drawable_height),
            "SDL_GetRendererOutputSize");
    if (drawable_width <= 0 || drawable_height <= 0) {
      qa_range_error("SDL returned invalid drawable dimensions");
    }
    texture = (SDL_Texture *)handle(
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, 1, drawable_width,
                          drawable_height),
        "SDL_CreateTexture");
    checked(SDL_SetTextureBlendMode(texture, 0), "SDL_SetTextureBlendMode NONE");
    out->renderer = renderer;
    out->texture = texture;
    out->texture_width = drawable_width;
    out->texture_height = drawable_height;
  } else {
    if (context == NULL) qa_range_error("SDL GL visual has no context");
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    out->gl_context = context;
    out->driver = driver_copy;
  }
  uint32_t id = SDL_GetWindowID(window);
  if (id == 0) qa_range_error("SDL_GetWindowID: %s", SDL_GetError());
  if (drawable_width <= 0 || drawable_height <= 0) {
    qa_range_error("SDL returned invalid drawable dimensions");
  }
  out->id = id;
  out->fullscreen_failure = fullscreen_failure;
  out->position_origin_x = origin_x;
  out->position_origin_y = origin_y;
  if (!registry_add_window(out)) {
    registry_remove_window(out);
    if (texture != NULL) SDL_DestroyTexture(texture);
    if (renderer != NULL) SDL_DestroyRenderer(renderer);
    if (context != NULL) SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    if (driver_loaded) SDL_GL_UnloadLibrary();
    free(driver_copy);
    free(fullscreen_failure);
    SDL_QuitSubSystem(video_subsystem);
    memset(out, 0, sizeof *out);
    return false;
  }
  return true;
oom:
  if (context != NULL) SDL_GL_DeleteContext(context);
  if (window != NULL) SDL_DestroyWindow(window);
  if (driver_loaded) SDL_GL_UnloadLibrary();
  SDL_QuitSubSystem(video_subsystem);
  return false;
}

void qa_sdl_window_move(qa_sdl_window *destination, qa_sdl_window *source) {
  if (destination == source) return;
  *destination = *source;
  for (size_t i = 0; i < g_window_count; i++) {
    if (g_windows[i] == source) g_windows[i] = destination;
  }
  if (destination->input != NULL) destination->input->window = destination;
  if (destination->gamma != NULL) destination->gamma->window = destination;
  memset(source, 0, sizeof *source);
}

void qa_sdl_window_set_visible(qa_sdl_window *window, bool visible) {
  if (visible) {
    SDL_ShowWindow(opened(window));
  } else {
    SDL_HideWindow(opened(window));
  }
}

uint32_t qa_sdl_window_flags(qa_sdl_window *window) {
  return SDL_GetWindowFlags(opened(window));
}

uint32_t qa_sdl_window_ticks(qa_sdl_window *window) {
  opened(window);
  return SDL_GetTicks();
}

bool qa_sdl_window_closed(const qa_sdl_window *window) {
  return window->sdl_window == NULL;
}

bool qa_sdl_window_relative_mouse(qa_sdl_window *window) {
  return window->input != NULL && g_input_lease == window->input &&
         SDL_GetRelativeMouseMode() != 0;
}

qa_sdl_backend qa_sdl_window_backend(qa_sdl_window *window) {
  opened(window);
  return window->backend;
}

qa_sdl_size qa_sdl_window_logical_size(qa_sdl_window *window) {
  int width = 0, height = 0;
  SDL_GetWindowSize(opened(window), &width, &height);
  if (width <= 0 || height <= 0) {
    qa_range_error("SDL returned invalid window dimensions");
  }
  qa_sdl_size size = {width, height};
  return size;
}

qa_sdl_size qa_sdl_window_drawable_size(qa_sdl_window *window) {
  SDL_Window *native = opened(window);
  int width = 0, height = 0;
  if (window->backend == QA_SDL_BACKEND_CPU) {
    /* The software renderer caches its surface until presentation. */
    SDL_GetWindowSizeInPixels(native, &width, &height);
  } else {
    SDL_GL_GetDrawableSize(native, &width, &height);
  }
  if (width <= 0 || height <= 0) {
    qa_range_error("SDL returned invalid drawable dimensions");
  }
  qa_sdl_size size = {width, height};
  return size;
}

int32_t qa_sdl_window_width(qa_sdl_window *window) {
  return qa_sdl_window_drawable_size(window).width;
}

int32_t qa_sdl_window_height(qa_sdl_window *window) {
  return qa_sdl_window_drawable_size(window).height;
}

void qa_sdl_window_set_size(qa_sdl_window *window, double width, double height) {
  const double dimensions[2] = {width, height};
  for (int i = 0; i < 2; i++) {
    if (!isfinite(dimensions[i]) || trunc(dimensions[i]) != dimensions[i] ||
        dimensions[i] <= 0 || dimensions[i] > 16384) {
      qa_range_error("SDL window dimensions must be integers in 1..16384");
    }
  }
  if (window->has_render_lease &&
      !qa_sdl_render_context_lease_parked(&window->render_lease)) {
    qa_range_error("SDL render context is reserved for the worker");
  }
  SDL_SetWindowSize(opened(window), (int)width, (int)height);
}

bool qa_sdl_window_fullscreen(qa_sdl_window *window) {
  return (qa_sdl_window_flags(window) & 1u) != 0;
}

void qa_sdl_window_set_fullscreen(qa_sdl_window *window, bool enabled) {
  if (window->has_render_lease &&
      !qa_sdl_render_context_lease_parked(&window->render_lease)) {
    qa_range_error("SDL render context is reserved for the worker");
  }
  checked(SDL_SetWindowFullscreen(opened(window), enabled ? 0x1001u : 0u),
          "SDL_SetWindowFullscreen");
}

bool qa_sdl_window_display(qa_sdl_window *window, qa_sdl_display *out) {
  memset(out, 0, sizeof *out);
  int index = SDL_GetWindowDisplayIndex(opened(window));
  int count = SDL_GetNumVideoDisplays();
  checked(index, "SDL_GetWindowDisplayIndex");
  checked(count, "SDL_GetNumVideoDisplays");
  SDL_DisplayMode mode;
  checked(SDL_GetCurrentDisplayMode(index, &mode), "SDL_GetCurrentDisplayMode");
  SDL_Rect bounds;
  checked(SDL_GetDisplayBounds(index, &bounds), "SDL_GetDisplayBounds");
  if (bounds.w <= 0 || bounds.h <= 0) {
    qa_range_error("SDL returned invalid display bounds");
  }
  out->name = sdl_string(SDL_GetDisplayName(index));
  if (out->name == NULL) return false;
  out->index = index;
  out->count = count;
  out->refresh_rate = mode.refresh_rate;
  out->bounds.x = bounds.x;
  out->bounds.y = bounds.y;
  out->bounds.width = bounds.w;
  out->bounds.height = bounds.h;
  return true;
}

qa_sdl_window_presentation qa_sdl_window_capture_presentation(qa_sdl_window *window) {
  SDL_Window *native = opened(window);
  uint32_t flags = qa_sdl_window_flags(window);
  qa_sdl_display display;
  if (!qa_sdl_window_display(window, &display)) {
    qa_range_error("SDL returned invalid window position");
  }
  int left = 0, top = 0;
  SDL_GetWindowPosition(native, &left, &top);
  SDL_DisplayMode mode;
  checked(SDL_GetWindowDisplayMode(native, &mode), "SDL_GetWindowDisplayMode");
  qa_sdl_window_presentation out;
  out.size = qa_sdl_window_logical_size(window);
  out.position.x = left - display.bounds.x;
  out.position.y = top - display.bounds.y;
  out.display_index = display.index;
  out.display_mode.format = mode.format;
  out.display_mode.width = mode.w;
  out.display_mode.height = mode.h;
  out.display_mode.refresh_rate = mode.refresh_rate;
  out.fullscreen = (flags & 0x1001u) == 0x1001u ? QA_SDL_FULLSCREEN_DESKTOP
                   : (flags & 1u) != 0          ? QA_SDL_FULLSCREEN_EXCLUSIVE
                                                 : QA_SDL_FULLSCREEN_WINDOWED;
  out.visible = (flags & 4u) != 0 && (flags & 8u) == 0;
  out.maximized = (flags & 0x80u) != 0;
  out.minimized = (flags & 0x40u) != 0;
  out.focused = (flags & 0x200u) != 0;
  qa_sdl_display_free(&display);
  return out;
}

void qa_sdl_window_restore_presentation(qa_sdl_window *window,
                                        const qa_sdl_window_presentation *state) {
  SDL_Window *native = opened(window);
  SDL_Rect bounds;
  checked(SDL_GetDisplayBounds(state->display_index, &bounds), "SDL_GetDisplayBounds");
  SDL_RestoreWindow(native);
  checked(SDL_SetWindowFullscreen(native, 0), "SDL_SetWindowFullscreen windowed");
  SDL_SetWindowPosition(native, bounds.x + state->position.x,
                        bounds.y + state->position.y);
  qa_sdl_window_set_size(window, state->size.width, state->size.height);
  SDL_DisplayMode mode;
  memset(&mode, 0, sizeof mode);
  mode.format = state->display_mode.format;
  mode.w = state->display_mode.width;
  mode.h = state->display_mode.height;
  mode.refresh_rate = state->display_mode.refresh_rate;
  checked(SDL_SetWindowDisplayMode(native, &mode), "SDL_SetWindowDisplayMode");
  uint32_t fullscreen = state->fullscreen == QA_SDL_FULLSCREEN_EXCLUSIVE ? 1u
                        : state->fullscreen == QA_SDL_FULLSCREEN_DESKTOP ? 0x1001u
                                                                          : 0u;
  checked(SDL_SetWindowFullscreen(native, fullscreen),
          "SDL_SetWindowFullscreen restore");
  if (state->maximized) SDL_MaximizeWindow(native);
  if (state->minimized) SDL_MinimizeWindow(native);
  qa_sdl_window_set_visible(window, state->visible);
  if (state->visible && state->focused && !state->minimized) SDL_RaiseWindow(native);
}

qa_sdl_display_mode *qa_sdl_window_display_modes(qa_sdl_window *window,
                                                 size_t *out_count) {
  qa_sdl_display display;
  if (!qa_sdl_window_display(window, &display)) {
    *out_count = 0;
    return NULL;
  }
  int index = display.index;
  qa_sdl_display_free(&display);
  int count = SDL_GetNumDisplayModes(index);
  checked(count, "SDL_GetNumDisplayModes");
  qa_sdl_display_mode *modes =
      (qa_sdl_display_mode *)calloc((size_t)count + 1, sizeof *modes);
  if (modes == NULL) {
    *out_count = 0;
    return NULL;
  }
  for (int i = 0; i < count; i++) {
    SDL_DisplayMode mode;
    checked(SDL_GetDisplayMode(index, i, &mode), "SDL_GetDisplayMode");
    modes[i] = display_mode_of(&mode);
  }
  SDL_DisplayMode desktop;
  checked(SDL_GetCurrentDisplayMode(index, &desktop), "SDL_GetCurrentDisplayMode");
  modes[count] = display_mode_of(&desktop);
  *out_count = (size_t)count + 1;
  return modes;
}

void qa_sdl_window_make_current(qa_sdl_window *window) {
  SDL_Window *native = opened(window);
  if (window->backend != QA_SDL_BACKEND_GL) {
    qa_range_error("makeCurrent requires a GL window");
  }
  if (window->has_render_lease) {
    qa_range_error("SDL render context is reserved for the worker");
  }
  checked(SDL_GL_MakeCurrent(native, window->render_enabled ? window->gl_context : NULL),
          "SDL_GL_MakeCurrent");
}

qa_sdl_procedure_lease qa_sdl_window_retain_procedures(qa_sdl_window *window) {
  qa_sdl_window_make_current(window);
  window->procedure_leases++;
  qa_sdl_procedure_lease lease = {window, false};
  return lease;
}

void qa_sdl_procedure_lease_release(qa_sdl_procedure_lease *lease) {
  if (lease->released) return;
  lease->released = true;
  lease->window->procedure_leases--;
}

bool qa_sdl_window_rendering_enabled(const qa_sdl_window *window) {
  return window->render_enabled;
}

void qa_sdl_window_set_rendering_enabled(qa_sdl_window *window, bool enabled) {
  SDL_Window *native = opened(window);
  if (window->backend != QA_SDL_BACKEND_GL) {
    qa_range_error("setRenderingEnabled requires a GL window");
  }
  if (window->has_render_lease) {
    qa_range_error("SDL render context is reserved for the worker");
  }
  checked(SDL_GL_MakeCurrent(native, enabled ? window->gl_context : NULL),
          "SDL_GL_MakeCurrent diagnostic");
  window->render_enabled = enabled;
}

qa_sdl_render_context_transfer qa_sdl_window_detach_render_context(
    qa_sdl_window *window) {
  SDL_Window *native = opened(window);
  if (window->backend != QA_SDL_BACKEND_GL) {
    qa_range_error("detachRenderContext requires a GL window");
  }
  if (window->has_render_lease) {
    qa_range_error("SDL render context is reserved for the worker");
  }
  if (window->procedure_leases != 0) {
    qa_range_error("Close GL procedure tables before transferring the context");
  }
  checked(SDL_GL_MakeCurrent(native, window->gl_context),
          "SDL_GL_MakeCurrent transfer");
  if (!qa_sdl_render_context_lease_detach(native, window->gl_context, window->id,
                                          &window->render_lease)) {
    qa_range_error("SDL context transfer registration failed");
  }
  window->has_render_lease = true;
  return window->render_lease.transfer;
}

void qa_sdl_window_restore_render_context(qa_sdl_window *window) {
  opened(window);
  if (window->has_render_lease) {
    qa_sdl_render_context_lease_restore(&window->render_lease);
    qa_sdl_render_context_lease_destroy(&window->render_lease);
    window->has_render_lease = false;
  }
  if (!window->render_enabled) qa_sdl_window_make_current(window);
}

void *qa_sdl_window_get_gl_proc_address(qa_sdl_window *window, const char *name) {
  if (name == NULL || name[0] == '\0') {
    qa_range_error("GL procedure name is empty");
  }
  qa_sdl_window_make_current(window);
  char operation[256];
  snprintf(operation, sizeof operation, "SDL_GL_GetProcAddress %s", name);
  return handle(SDL_GL_GetProcAddress(name), operation);
}

void qa_sdl_window_swap(qa_sdl_window *window) {
  SDL_Window *native = opened(window);
  qa_sdl_window_make_current(window);
  if (window->backend != QA_SDL_BACKEND_GL) {
    qa_range_error("swap requires a GL window");
  }
  /* SDL requires a current window to swap; Mac flushBuffer did not. */
  if (!window->render_enabled) {
    checked(SDL_GL_MakeCurrent(native, window->gl_context), "SDL_GL_MakeCurrent swap");
  }
  SDL_GL_SwapWindow(native);
  if (!window->render_enabled) {
    checked(SDL_GL_MakeCurrent(native, NULL), "SDL_GL_MakeCurrent restore diagnostic");
  }
}

int32_t qa_sdl_window_swap_interval(qa_sdl_window *window) {
  qa_sdl_window_make_current(window);
  return SDL_GL_GetSwapInterval();
}

void qa_sdl_window_set_swap_interval(qa_sdl_window *window, int32_t interval) {
  if (interval != -1 && interval != 0 && interval != 1) {
    qa_range_error("SDL swap interval must be -1, 0, or 1");
  }
  qa_sdl_window_make_current(window);
  checked(SDL_GL_SetSwapInterval(interval), "SDL_GL_SetSwapInterval");
}

static const char *gamma_kind_name(qa_sdl_gamma_capability_kind kind) {
  switch (kind) {
    case QA_SDL_GAMMA_API_ACCEPTED: return "api-accepted";
    case QA_SDL_GAMMA_UNSUPPORTED: return "unsupported";
    case QA_SDL_GAMMA_UNAVAILABLE: return "unavailable";
    case QA_SDL_GAMMA_RETIRED: return "retired";
  }
  return "unsupported";
}

static bool gamma_same_display(const qa_sdl_gamma_lease *lease,
                               const qa_sdl_display *current) {
  return current->index == lease->display_index &&
         strcmp(current->name, lease->display_name) == 0;
}

static void gamma_restore(qa_sdl_gamma_lease *lease) {
  if (!lease->has_original) return;
  qa_sdl_display current;
  if (!qa_sdl_window_display(lease->window, &current)) return;
  bool same = gamma_same_display(lease, &current);
  qa_sdl_display_free(&current);
  if (!same) return;
  checked(SDL_SetWindowGammaRamp(lease->window->sdl_window, lease->original[0],
                                 lease->original[1], lease->original[2]),
          "SDL_SetWindowGammaRamp restore");
  lease->has_original = false;
}

void qa_sdl_gamma_lease_synchronize(qa_sdl_gamma_lease *lease) {
  if (g_gamma_lease != lease) return;
  if (lease->capability_kind != QA_SDL_GAMMA_API_ACCEPTED) return;
  qa_sdl_display current;
  if (!qa_sdl_window_display(lease->window, &current)) return;
  bool keep = current.count == 1 && gamma_same_display(lease, &current);
  qa_sdl_display_free(&current);
  if (keep) return;
  lease->capability_kind = QA_SDL_GAMMA_RETIRED;
  free(lease->capability_reason);
  lease->capability_reason = xstrdup(
      "SDL display topology changed; hardware continuity and cross-display "
      "restoration are unsupported");
  gamma_restore(lease);
}

qa_sdl_gamma_capability qa_sdl_gamma_lease_capability(qa_sdl_gamma_lease *lease) {
  qa_sdl_gamma_lease_synchronize(lease);
  qa_sdl_gamma_capability capability;
  capability.kind = lease->capability_kind;
  capability.display_index = lease->capability_display_index;
  capability.display_name = lease->capability_display_name;
  capability.reason = lease->capability_reason;
  return capability;
}

bool qa_sdl_gamma_lease_closed(const qa_sdl_gamma_lease *lease) {
  return g_gamma_lease != lease;
}

void qa_sdl_gamma_lease_apply(qa_sdl_gamma_lease *lease, double gamma) {
  if (g_gamma_lease != lease) qa_range_error("SDL gamma lease is closed");
  if (!isfinite(gamma) || gamma < 0.5 || gamma > 3) {
    qa_range_error("Gamma requires a finite value in 0.5..3");
  }
  qa_sdl_gamma_lease_synchronize(lease);
  if (lease->capability_kind != QA_SDL_GAMMA_API_ACCEPTED) {
    qa_range_error("SDL gamma is %s: %s", gamma_kind_name(lease->capability_kind),
                   lease->capability_reason == NULL ? "" : lease->capability_reason);
  }
  uint16_t ramp[256];
  /* Linux GLimp_SetGamma consumes r_gamma itself (Math.fround staging). */
  SDL_CalculateGammaRamp((float)gamma, ramp);
  checked(SDL_SetWindowGammaRamp(lease->window->sdl_window, ramp, ramp, ramp),
          "SDL_SetWindowGammaRamp");
}

void qa_sdl_gamma_lease_close(qa_sdl_gamma_lease *lease) {
  if (g_gamma_lease != lease) return;
  gamma_restore(lease);
  g_gamma_lease = NULL;
}

void qa_sdl_gamma_lease_destroy(qa_sdl_gamma_lease *lease) {
  qa_sdl_gamma_lease_close(lease);
  free(lease->capability_display_name);
  free(lease->capability_reason);
  free(lease->display_name);
  free(lease);
}

qa_sdl_gamma_lease *qa_sdl_window_begin_gamma(qa_sdl_window *window) {
  SDL_Window *native = opened(window);
  if (g_gamma_lease != NULL) {
    qa_range_error("SDL display gamma already has an owner");
  }
  qa_sdl_gamma_lease *lease = (qa_sdl_gamma_lease *)calloc(1, sizeof *lease);
  if (lease == NULL) return NULL;
  lease->window = window;
  qa_sdl_display display;
  if (!qa_sdl_window_display(window, &display)) {
    free(lease);
    return NULL;
  }
  lease->display_index = display.index;
  lease->display_name = xstrdup(display.name);
  if (lease->display_name == NULL) {
    qa_sdl_display_free(&display);
    free(lease);
    return NULL;
  }
  if (display.count != 1) {
    lease->capability_kind = QA_SDL_GAMMA_UNAVAILABLE;
    lease->capability_reason =
        xstrdup("Gamma requires the single-display ownership profile");
  } else if ((qa_sdl_window_flags(window) & 0x200u) == 0) {
    lease->capability_kind = QA_SDL_GAMMA_UNAVAILABLE;
    lease->capability_reason =
        xstrdup("Gamma acquisition requires actual native input focus");
  } else {
    uint16_t ramp[3][256];
    if (SDL_GetWindowGammaRamp(native, ramp[0], ramp[1], ramp[2]) < 0) {
      lease->capability_kind = QA_SDL_GAMMA_UNSUPPORTED;
      char reason[512];
      snprintf(reason, sizeof reason, "SDL_GetWindowGammaRamp: %s", SDL_GetError());
      lease->capability_reason = xstrdup(reason);
    } else if (SDL_SetWindowGammaRamp(native, ramp[0], ramp[1], ramp[2]) < 0) {
      lease->capability_kind = QA_SDL_GAMMA_UNSUPPORTED;
      char reason[512];
      snprintf(reason, sizeof reason, "SDL_SetWindowGammaRamp: %s", SDL_GetError());
      lease->capability_reason = xstrdup(reason);
    } else {
      memcpy(lease->original, ramp, sizeof ramp);
      lease->has_original = true;
      lease->capability_kind = QA_SDL_GAMMA_API_ACCEPTED;
      lease->capability_display_index = display.index;
      lease->capability_display_name = xstrdup(display.name);
    }
  }
  qa_sdl_display_free(&display);
  g_gamma_lease = lease;
  window->gamma = lease;
  return lease;
}

bool qa_sdl_input_lease_closed(const qa_sdl_input_lease *lease) {
  return g_input_lease != lease;
}

void qa_sdl_input_lease_set_relative_mouse(qa_sdl_input_lease *lease, bool enabled) {
  opened(lease->window);
  if (g_input_lease != lease) qa_range_error("SDL input lease is closed");
  if (lease->relative == enabled &&
      qa_sdl_window_relative_mouse(lease->window) == enabled) {
    return;
  }
  checked(SDL_SetRelativeMouseMode(enabled ? 1 : 0), "SDL_SetRelativeMouseMode");
  lease->relative = enabled;
}

void qa_sdl_input_lease_close(qa_sdl_input_lease *lease) {
  if (g_input_lease != lease) return;
  bool failed = false;
  if (lease->relative && SDL_SetRelativeMouseMode(0) < 0) failed = true;
  SDL_StopTextInput();
  g_input_lease = NULL;
  if (failed) qa_range_error("SDL input release failed");
}

void qa_sdl_input_lease_destroy(qa_sdl_input_lease *lease) {
  qa_sdl_input_lease_close(lease);
  free(lease);
}

/** SDL text and relative mouse modes are process-global; one owner leases both. */
qa_sdl_input_lease *qa_sdl_window_begin_input(qa_sdl_window *window) {
  opened(window);
  if (g_input_lease != NULL) {
    qa_range_error("SDL gameplay input already has an owner");
  }
  qa_sdl_input_lease *lease = (qa_sdl_input_lease *)calloc(1, sizeof *lease);
  if (lease == NULL) return NULL;
  lease->window = window;
  SDL_StartTextInput();
  g_input_lease = lease;
  window->input = lease;
  return lease;
}

qa_sdl_event *qa_sdl_window_poll_events(qa_sdl_window *window, size_t *out_count) {
  opened(window);
  if (g_gamma_lease != NULL) qa_sdl_gamma_lease_synchronize(g_gamma_lease);
  SDL_PumpEvents();
  /* Controller events have a separate owner; polling must not consume them. */
  static const uint32_t ranges[2][2] = {{0, 0x64f}, {0x670, 0xffff}};
  for (int range = 0; range < 2; range++) {
    for (;;) {
      SDL_Event native;
      int count = SDL_PeepEvents(&native, 1, SDL_GETEVENT, ranges[range][0],
                                 ranges[range][1]);
      checked(count, "SDL_PeepEvents window");
      if (count == 0) break;
      uint8_t bytes[56];
      memset(bytes, 0, sizeof bytes);
      memcpy(bytes, &native, sizeof bytes < sizeof native ? sizeof bytes
                                                           : sizeof native);
      qa_sdl_event event = qa_decode_sdl_event(bytes, sizeof bytes);
      if (qa_sdl_joystick_queue_event(&event)) continue;
      uint32_t type = read_u32(bytes, sizeof bytes, 0);
      bool targeted = type == 0x200 || (type >= 0x300 && type <= 0x305) ||
                      (type >= 0x400 && type <= 0x403);
      if (targeted) {
        qa_sdl_window *target =
            registry_by_id(read_u32(bytes, sizeof bytes, 8));
        if (target != NULL) {
          pending_push(&target->pending, &target->pending_count,
                       &target->pending_capacity, &event);
        }
      } else {
        for (size_t i = 0; i < g_window_count; i++) {
          pending_push(&g_windows[i]->pending, &g_windows[i]->pending_count,
                       &g_windows[i]->pending_capacity, &event);
        }
      }
    }
  }
  qa_sdl_event *events = window->pending;
  *out_count = window->pending_count;
  window->pending = NULL;
  window->pending_count = 0;
  window->pending_capacity = 0;
  if (g_gamma_lease != NULL) qa_sdl_gamma_lease_synchronize(g_gamma_lease);
  return events;
}

void qa_sdl_window_present(qa_sdl_window *window, const uint8_t *rgba, size_t length) {
  opened(window);
  if (window->backend != QA_SDL_BACKEND_CPU) {
    qa_range_error("present requires a CPU window");
  }
  qa_sdl_size size = qa_sdl_window_drawable_size(window);
  if (length != (size_t)size.width * (size_t)size.height * 4u) {
    qa_range_error("RGBA framebuffer size does not match the window");
  }
  if (window->texture_width != size.width || window->texture_height != size.height) {
    SDL_Texture *texture = (SDL_Texture *)handle(
        SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_RGBA32, 1, size.width,
                          size.height),
        "SDL_CreateTexture resize");
    if (SDL_SetTextureBlendMode(texture, 0) < 0) {
      const char *message = SDL_GetError();
      SDL_DestroyTexture(texture);
      qa_range_error("SDL_SetTextureBlendMode resize: %s", message);
    }
    SDL_DestroyTexture(window->texture);
    window->texture = texture;
    window->texture_width = size.width;
    window->texture_height = size.height;
    window->has_frame = false;
  }
  checked(SDL_UpdateTexture(window->texture, NULL, rgba, size.width * 4),
          "SDL_UpdateTexture");
  checked(SDL_RenderCopy(window->renderer, window->texture, NULL, NULL),
          "SDL_RenderCopy");
  SDL_RenderPresent(window->renderer);
  window->has_frame = true;
}

uint8_t *qa_sdl_window_read_pixels(qa_sdl_window *window, size_t *out_length) {
  opened(window);
  if (window->backend != QA_SDL_BACKEND_CPU) {
    qa_range_error("readPixels requires a CPU window");
  }
  if (!window->has_frame) qa_range_error("No framebuffer has been presented");
  qa_sdl_size size = qa_sdl_window_drawable_size(window);
  size_t length = (size_t)size.width * (size_t)size.height * 4u;
  uint8_t *rgba = (uint8_t *)malloc(length);
  if (rgba == NULL) {
    *out_length = 0;
    return NULL;
  }
  /* SDL invalidates the backbuffer on present; replay the texture first. */
  checked(SDL_RenderCopy(window->renderer, window->texture, NULL, NULL),
          "SDL_RenderCopy readback");
  checked(SDL_RenderReadPixels(window->renderer, NULL, SDL_PIXELFORMAT_RGBA32, rgba,
                               size.width * 4),
          "SDL_RenderReadPixels");
  *out_length = length;
  return rgba;
}

static void write_u32(uint8_t *bytes, size_t offset, uint32_t value) {
  memcpy(bytes + offset, &value, sizeof value);
}

static void write_i32(uint8_t *bytes, size_t offset, int32_t value) {
  memcpy(bytes + offset, &value, sizeof value);
}

void qa_sdl_window_push_event(qa_sdl_window *window, const qa_sdl_event *event) {
  opened(window);
  uint8_t bytes[56];
  memset(bytes, 0, sizeof bytes);
  write_u32(bytes, 4, event->timestamp);
  write_u32(bytes, 8, window->id);
  switch (event->kind) {
    case QA_SDL_EVENT_QUIT: write_u32(bytes, 0, 0x100); break;
    case QA_SDL_EVENT_KEY:
      write_u32(bytes, 0, event->as.key.down ? 0x300u : 0x301u);
      bytes[12] = event->as.key.down ? 1 : 0;
      bytes[13] = event->as.key.repeat ? 1 : 0;
      write_i32(bytes, 16, event->as.key.scancode);
      write_i32(bytes, 20, event->as.key.keycode);
      memcpy(bytes + 24, &event->as.key.modifiers, sizeof event->as.key.modifiers);
      break;
    case QA_SDL_EVENT_MOUSE_MOTION:
      write_u32(bytes, 0, 0x400);
      write_u32(bytes, 16, event->as.mouse_motion.buttons);
      write_i32(bytes, 20, event->as.mouse_motion.x);
      write_i32(bytes, 24, event->as.mouse_motion.y);
      write_i32(bytes, 28, event->as.mouse_motion.dx);
      write_i32(bytes, 32, event->as.mouse_motion.dy);
      break;
    case QA_SDL_EVENT_MOUSE_BUTTON:
      write_u32(bytes, 0, event->as.mouse_button.down ? 0x401u : 0x402u);
      bytes[16] = event->as.mouse_button.button;
      bytes[17] = event->as.mouse_button.down ? 1 : 0;
      bytes[18] = event->as.mouse_button.clicks;
      write_i32(bytes, 20, event->as.mouse_button.x);
      write_i32(bytes, 24, event->as.mouse_button.y);
      break;
    case QA_SDL_EVENT_WINDOW:
      write_u32(bytes, 0, 0x200);
      bytes[12] = event->as.window.event;
      write_i32(bytes, 16, event->as.window.data1);
      write_i32(bytes, 20, event->as.window.data2);
      break;
    default:
      qa_range_error("Unsupported SDL event: [object Object]");
  }
  SDL_Event native;
  memset(&native, 0, sizeof native);
  memcpy(&native, bytes, sizeof bytes < sizeof native ? sizeof bytes : sizeof native);
  int result = SDL_PushEvent(&native);
  checked(result, "SDL_PushEvent");
  if (result == 0) qa_range_error("SDL event was filtered");
}

void qa_sdl_window_close(qa_sdl_window *window) {
  if (window->sdl_window == NULL) return;
  if (window->procedure_leases != 0) {
    qa_range_error("Close GL procedure tables before closing the window");
  }
  qa_sdl_window_restore_render_context(window);
  require_window_list_ownership();
  SDL_Window *native = window->sdl_window;
  SDL_Renderer *renderer = window->renderer;
  SDL_Texture *texture = window->texture;
  SDL_GLContext context = window->gl_context;
  qa_sdl_backend backend = window->backend;
  bool has_driver = window->driver != NULL;
  if (window->gamma != NULL) qa_sdl_gamma_lease_close(window->gamma);
  if (window->input != NULL) qa_sdl_input_lease_close(window->input);
  window->sdl_window = NULL;
  window->renderer = NULL;
  window->texture = NULL;
  window->gl_context = NULL;
  registry_remove_window(window);
  free(window->pending);
  window->pending = NULL;
  window->pending_count = 0;
  window->pending_capacity = 0;
  free(window->driver);
  window->driver = NULL;
  free(window->fullscreen_failure);
  window->fullscreen_failure = NULL;
  if (backend == QA_SDL_BACKEND_CPU) {
    if (texture != NULL) SDL_DestroyTexture(texture);
    if (renderer != NULL) SDL_DestroyRenderer(renderer);
  } else if (context != NULL) {
    SDL_GL_DeleteContext(context);
  }
  SDL_DestroyWindow(native);
  if (backend == QA_SDL_BACKEND_GL && has_driver) SDL_GL_UnloadLibrary();
  SDL_QuitSubSystem(video_subsystem);
}

static bool is_joystick_kind(qa_sdl_event_kind kind) {
  return kind == QA_SDL_EVENT_JOYSTICK_AXIS || kind == QA_SDL_EVENT_JOYSTICK_HAT ||
         kind == QA_SDL_EVENT_JOYSTICK_BUTTON ||
         kind == QA_SDL_EVENT_JOYSTICK_REMOVED;
}

static int32_t joystick_instance_of(const qa_sdl_event *event) {
  switch (event->kind) {
    case QA_SDL_EVENT_JOYSTICK_AXIS: return event->as.joystick_axis.instance;
    case QA_SDL_EVENT_JOYSTICK_HAT: return event->as.joystick_hat.instance;
    case QA_SDL_EVENT_JOYSTICK_BUTTON: return event->as.joystick_button.instance;
    case QA_SDL_EVENT_JOYSTICK_REMOVED: return event->as.joystick_removed.instance;
    default: return 0;
  }
}

bool qa_sdl_joystick_queue_event(const qa_sdl_event *event) {
  if (!is_joystick_kind(event->kind)) return false;
  int32_t instance = joystick_instance_of(event);
  for (size_t i = 0; i < g_joystick_count; i++) {
    if (g_joysticks[i]->instance == instance) {
      pending_push(&g_joysticks[i]->pending, &g_joysticks[i]->pending_count,
                   &g_joysticks[i]->pending_capacity, event);
    }
  }
  return true;
}

static bool registry_add_joystick(qa_sdl_joystick *joystick) {
  if (g_joystick_count == g_joystick_capacity) {
    size_t grown = g_joystick_capacity == 0 ? 4 : g_joystick_capacity * 2;
    qa_sdl_joystick **next =
        (qa_sdl_joystick **)realloc(g_joysticks, grown * sizeof *next);
    if (next == NULL) return false;
    g_joysticks = next;
    g_joystick_capacity = grown;
  }
  g_joysticks[g_joystick_count++] = joystick;
  return true;
}

static void registry_remove_joystick(const qa_sdl_joystick *joystick) {
  for (size_t i = 0; i < g_joystick_count; i++) {
    if (g_joysticks[i] == joystick) {
      memmove(&g_joysticks[i], &g_joysticks[i + 1],
              (g_joystick_count - i - 1) * sizeof *g_joysticks);
      g_joystick_count--;
      return;
    }
  }
}

bool qa_sdl_joystick_open_first(qa_sdl_joystick_print_fn print, void *print_ctx,
                                qa_sdl_joystick_profile profile, qa_sdl_joystick *out) {
  const uint32_t subsystem = 0x200;
  initialize_subsystem(subsystem, "SDL_InitSubSystem joystick");
  SDL_Joystick *pointer = NULL;
  int count = SDL_NumJoysticks();
  if (count < 0) {
    SDL_QuitSubSystem(subsystem);
    checked(count, "SDL_NumJoysticks");
  }
  int limit = count < 4 ? count : 4;
  for (int index = 0; index < limit; index++) {
    pointer = SDL_JoystickOpen(index);
    if (pointer != NULL) break;
    if (print != NULL) {
      char message[512];
      snprintf(message, sizeof message, "SDL_JoystickOpen %d: %s\n", index,
               SDL_GetError());
      print(message, print_ctx);
    }
  }
  if (pointer == NULL) {
    SDL_QuitSubSystem(subsystem);
    return false;
  }
  int32_t instance = SDL_JoystickInstanceID(pointer);
  int32_t axes = SDL_JoystickNumAxes(pointer);
  int32_t buttons = SDL_JoystickNumButtons(pointer);
  int32_t hats = SDL_JoystickNumHats(pointer);
  int32_t balls = SDL_JoystickNumBalls(pointer);
  const int32_t description[5] = {instance, axes, buttons, hats, balls};
  for (int i = 0; i < 5; i++) {
    if (description[i] < 0) {
      SDL_JoystickClose(pointer);
      SDL_QuitSubSystem(subsystem);
      checked(description[i], "SDL joystick description");
    }
  }
  if (print != NULL && profile == QA_SDL_JOYSTICK_LINUX && (hats != 0 || balls != 0)) {
    char message[256];
    snprintf(message, sizeof message,
             "SDL joystick has %d hats and %d balls; this Unix axis/button "
             "profile does not map them.\n",
             (int)hats, (int)balls);
    print(message, print_ctx);
  }
  if (print != NULL && profile == QA_SDL_JOYSTICK_WINDOWS && balls != 0) {
    char message[256];
    snprintf(message, sizeof message,
             "SDL joystick has %d relative balls; Windows source mouse "
             "motion uses absolute U/V axes.\n",
             (int)balls);
    print(message, print_ctx);
  }
  memset(out, 0, sizeof *out);
  out->name = sdl_string(SDL_JoystickName(pointer));
  if (out->name == NULL) {
    SDL_JoystickClose(pointer);
    SDL_QuitSubSystem(subsystem);
    return false;
  }
  out->pointer = pointer;
  out->instance = instance;
  out->axes = axes;
  out->buttons = buttons;
  out->hats = hats;
  if (!registry_add_joystick(out)) {
    free(out->name);
    memset(out, 0, sizeof *out);
    SDL_JoystickClose(pointer);
    SDL_QuitSubSystem(subsystem);
    return false;
  }
  return true;
}

qa_sdl_event *qa_sdl_joystick_poll_events(qa_sdl_joystick *joystick,
                                          qa_sdl_joystick_profile profile,
                                          size_t *out_count) {
  if (joystick->pointer == NULL) qa_range_error("SDL joystick is closed");
  SDL_JoystickUpdate();
  for (;;) {
    SDL_Event native;
    int count = SDL_PeepEvents(&native, 1, SDL_GETEVENT, 0x600, 0x606);
    checked(count, "SDL_PeepEvents joystick");
    if (count == 0) break;
    uint8_t bytes[56];
    memset(bytes, 0, sizeof bytes);
    memcpy(bytes, &native, sizeof bytes < sizeof native ? sizeof bytes : sizeof native);
    qa_sdl_event event = qa_decode_sdl_event(bytes, sizeof bytes);
    qa_sdl_joystick_queue_event(&event);
  }
  qa_sdl_event *events = joystick->pending;
  size_t event_count = joystick->pending_count;
  joystick->pending = NULL;
  joystick->pending_count = 0;
  joystick->pending_capacity = 0;
  if (profile == QA_SDL_JOYSTICK_WINDOWS) {
    for (size_t i = 0; i < event_count; i++) {
      if (events[i].kind == QA_SDL_EVENT_JOYSTICK_REMOVED) {
        qa_sdl_event removed = events[i];
        events[0] = removed;
        *out_count = 1;
        return events;
      }
    }
    /* joyGetPosEx polls current absolute values, including unchanged U/V. */
    free(events);
    qa_sdl_event *state = NULL;
    size_t state_count = 0, state_capacity = 0;
    uint32_t timestamp = SDL_GetTicks();
    int32_t button_limit = joystick->buttons < 32 ? joystick->buttons : 32;
    for (int32_t button = 0; button < button_limit; button++) {
      qa_sdl_event event;
      memset(&event, 0, sizeof event);
      event.kind = QA_SDL_EVENT_JOYSTICK_BUTTON;
      event.timestamp = timestamp;
      event.as.joystick_button.instance = joystick->instance;
      event.as.joystick_button.button = (uint8_t)button;
      event.as.joystick_button.down =
          SDL_JoystickGetButton(joystick->pointer, button) != 0;
      if (!pending_push(&state, &state_count, &state_capacity, &event)) {
        free(state);
        *out_count = 0;
        return NULL;
      }
    }
    int32_t axis_limit = joystick->axes < 6 ? joystick->axes : 6;
    for (int32_t axis = 0; axis < axis_limit; axis++) {
      qa_sdl_event event;
      memset(&event, 0, sizeof event);
      event.kind = QA_SDL_EVENT_JOYSTICK_AXIS;
      event.timestamp = timestamp;
      event.as.joystick_axis.instance = joystick->instance;
      event.as.joystick_axis.axis = (uint8_t)axis;
      event.as.joystick_axis.value = SDL_JoystickGetAxis(joystick->pointer, axis);
      if (!pending_push(&state, &state_count, &state_capacity, &event)) {
        free(state);
        *out_count = 0;
        return NULL;
      }
    }
    if (joystick->hats != 0) {
      qa_sdl_event event;
      memset(&event, 0, sizeof event);
      event.kind = QA_SDL_EVENT_JOYSTICK_HAT;
      event.timestamp = timestamp;
      event.as.joystick_hat.instance = joystick->instance;
      event.as.joystick_hat.hat = 0;
      event.as.joystick_hat.value = SDL_JoystickGetHat(joystick->pointer, 0);
      if (!pending_push(&state, &state_count, &state_capacity, &event)) {
        free(state);
        *out_count = 0;
        return NULL;
      }
    }
    *out_count = state_count;
    return state;
  }
  *out_count = event_count;
  return events;
}

void qa_sdl_joystick_close(qa_sdl_joystick *joystick) {
  SDL_Joystick *pointer = joystick->pointer;
  if (pointer == NULL) return;
  joystick->pointer = NULL;
  registry_remove_joystick(joystick);
  free(joystick->pending);
  joystick->pending = NULL;
  joystick->pending_count = 0;
  joystick->pending_capacity = 0;
  free(joystick->name);
  joystick->name = NULL;
  SDL_JoystickClose(pointer);
  SDL_QuitSubSystem(0x200);
}
