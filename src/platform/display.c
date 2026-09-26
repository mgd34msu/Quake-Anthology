#include "qa/display.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct qa_display {
    qa_display_backend backend;
    SDL_Window *window;
    union {
        struct {
            SDL_Renderer *renderer;
            SDL_Texture *texture;
            uint32_t width, height;
            bool has_frame;
        } cpu;
        struct {
            SDL_GLContext context;
            bool library_loaded;
        } gl;
    } native;
    char fullscreen_failure[256];
};

static bool display_error(qa_error *error, qa_status status, const char *operation)
{
    qa_error_set(error, status, 0, "%s: %s", operation, SDL_GetError());
    return false;
}

static bool valid_dimensions(uint32_t width, uint32_t height, qa_error *error)
{
    if (width == 0 || height == 0 || width > 16384 || height > 16384 ||
        width > INT_MAX || height > INT_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "SDL display dimensions must be in 1..16384");
        return false;
    }
    return true;
}

void qa_display_options_default(qa_display_options *options)
{
    if (options == NULL) return;
    *options = (qa_display_options){
        .title = "Quake Anthology",
        .width = 640,
        .height = 480,
        .backend = QA_DISPLAY_OPENGL,
        .resizable = true,
        .high_dpi = true,
        .display_index = -1,
        .fullscreen = QA_DISPLAY_WINDOWED,
        .color_bits = 24,
        .depth_bits = 24,
        .stencil_bits = 8,
        .allow_software_gl = false,
        .allow_fullscreen_fallback = true
    };
}

static int selected_display(int requested, qa_error *error)
{
    int count = SDL_GetNumVideoDisplays();
    if (count < 1) {
        display_error(error, QA_ERROR_IO, "SDL_GetNumVideoDisplays");
        return -1;
    }
    return requested < 0 || requested >= count ? 0 : requested;
}

static unsigned reduced_bits(unsigned bits)
{
    if (bits == 24) return 16;
    if (bits == 16) return 8;
    return bits;
}

static bool set_gl_attribute(SDL_GLattr attribute, int value, qa_error *error)
{
    if (SDL_GL_SetAttribute(attribute, value) < 0)
        return display_error(error, QA_ERROR_IO, "SDL_GL_SetAttribute");
    return true;
}

static bool set_context_attributes(const qa_display_options *options,
                                   qa_error *error)
{
    if (!set_gl_attribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2, error) ||
        !set_gl_attribute(SDL_GL_CONTEXT_MINOR_VERSION, 1, error) ||
        !set_gl_attribute(SDL_GL_CONTEXT_PROFILE_MASK,
                          SDL_GL_CONTEXT_PROFILE_COMPATIBILITY, error) ||
        !set_gl_attribute(SDL_GL_DOUBLEBUFFER, 1, error) ||
        !set_gl_attribute(SDL_GL_STEREO, options->stereo ? 1 : 0, error) ||
        !set_gl_attribute(SDL_GL_ALPHA_SIZE, 0, error)) return false;
    return true;
}

static bool set_visual_attributes(unsigned component, unsigned depth,
                                  unsigned stencil, qa_error *error)
{
    if (component > INT_MAX || depth > INT_MAX || stencil > INT_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "SDL OpenGL visual precision exceeds signed int");
        return false;
    }
    return set_gl_attribute(SDL_GL_RED_SIZE, (int)component, error) &&
           set_gl_attribute(SDL_GL_GREEN_SIZE, (int)component, error) &&
           set_gl_attribute(SDL_GL_BLUE_SIZE, (int)component, error) &&
           set_gl_attribute(SDL_GL_DEPTH_SIZE, (int)depth, error) &&
           set_gl_attribute(SDL_GL_STENCIL_SIZE, (int)stencil, error);
}

static bool visual_satisfies(unsigned component, unsigned depth,
                             unsigned stencil)
{
    const SDL_GLattr attributes[] = {SDL_GL_RED_SIZE, SDL_GL_GREEN_SIZE,
                                     SDL_GL_BLUE_SIZE, SDL_GL_DEPTH_SIZE,
                                     SDL_GL_STENCIL_SIZE};
    const unsigned minimum[] = {component, component, component, depth, stencil};
    for (size_t i = 0; i < sizeof(attributes) / sizeof(attributes[0]); ++i) {
        int actual = 0;
        if (SDL_GL_GetAttribute(attributes[i], &actual) < 0 || actual < 0 ||
            (unsigned)actual < minimum[i]) return false;
    }
    return true;
}

static bool create_gl_window(const qa_display_options *options, const char *title,
                             int x, int y, Uint32 flags, SDL_Window **window,
                             SDL_GLContext *context, qa_error *error)
{
    unsigned color = options->color_bits == 0 ? 24 : options->color_bits;
    unsigned depth = options->depth_bits == 0 ? 24 : options->depth_bits;
    unsigned stencil = options->stencil_bits;
    if (color > 32 || depth > 32 || stencil > 32) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "SDL OpenGL visual precision must be at most 32 bits");
        return false;
    }
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        if (attempt == 4) {
            depth = reduced_bits(depth);
            stencil = reduced_bits(stencil);
        }
        if (attempt == 8 && color == 24) color = 16;
        if (attempt == 12) stencil = reduced_bits(stencil);
        unsigned candidate_color = attempt % 4 == 3 && color == 24 ? 16 : color;
        unsigned component = candidate_color == 24 ? 8 : 4;
        unsigned candidate_depth = attempt % 4 == 2 ? reduced_bits(depth) : depth;
        unsigned candidate_stencil = stencil;
        if (attempt % 4 == 1)
            candidate_stencil = stencil == 24 ? 16 : stencil == 16 ? 8 : 0;
        if (!set_visual_attributes(component, candidate_depth, candidate_stencil,
                                   error)) return false;
        SDL_Window *candidate_window = SDL_CreateWindow(
            title, x, y, (int)options->width, (int)options->height, flags);
        if (candidate_window == NULL) continue;
        SDL_GLContext candidate_context = SDL_GL_CreateContext(candidate_window);
        if (candidate_context != NULL &&
            SDL_GL_MakeCurrent(candidate_window, candidate_context) == 0 &&
            visual_satisfies(component, candidate_depth, candidate_stencil)) {
            *window = candidate_window;
            *context = candidate_context;
            return true;
        }
        if (candidate_context != NULL) SDL_GL_DeleteContext(candidate_context);
        SDL_DestroyWindow(candidate_window);
    }
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                 "SDL could not create a compatible OpenGL 2.1 visual: %s",
                 SDL_GetError());
    return false;
}

static bool reject_software_gl(const qa_display_options *options, qa_error *error)
{
    if (options->allow_software_gl) return true;
    typedef const GLubyte *(APIENTRY *get_string_proc)(GLenum);
    _Static_assert(sizeof(get_string_proc) == sizeof(void *),
                   "SDL GL procedure pointers must fit in void pointers");
    void *address = SDL_GL_GetProcAddress("glGetString");
    get_string_proc get_string = NULL;
    if (address == NULL) return display_error(error, QA_ERROR_UNSUPPORTED,
                                               "SDL_GL_GetProcAddress glGetString");
    memcpy(&get_string, &address, sizeof(get_string));
    const GLubyte *value = get_string(GL_RENDERER);
    if (value == NULL) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                     "OpenGL did not report a renderer");
        return false;
    }
    const char *renderer = (const char *)value;
    if (strcasecmp(renderer, "Mesa X11") == 0 ||
        strcasecmp(renderer, "Mesa GLX Indirect") == 0) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                     "The selected legacy Mesa software OpenGL driver is disabled");
        return false;
    }
    return true;
}

static void record_fullscreen_failure(qa_display *display, const char *message)
{
    snprintf(display->fullscreen_failure, sizeof(display->fullscreen_failure),
             "%s", message == NULL ? "SDL fullscreen transition failed" : message);
}

static bool restore_windowed(qa_display *display, qa_error *error)
{
    if (SDL_SetWindowFullscreen(display->window, 0) < 0)
        return display_error(error, QA_ERROR_IO,
                             "SDL_SetWindowFullscreen windowed recovery");
    if ((SDL_GetWindowFlags(display->window) & SDL_WINDOW_FULLSCREEN) != 0) {
        qa_error_set(error, QA_ERROR_IO, 0,
                     "SDL did not restore windowed mode");
        return false;
    }
    return true;
}

static bool fullscreen_failure(qa_display *display,
                               const qa_display_options *options,
                               const char *operation, qa_error *error)
{
    char message[256];
    snprintf(message, sizeof(message), "%s: %s", operation, SDL_GetError());
    if (!restore_windowed(display, error)) return false;
    if (!options->allow_fullscreen_fallback) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "%s", message);
        return false;
    }
    record_fullscreen_failure(display, message);
    return true;
}

static bool fullscreen_state_failure(qa_display *display,
                                     const qa_display_options *options,
                                     const char *message, qa_error *error)
{
    if (!restore_windowed(display, error)) return false;
    if (!options->allow_fullscreen_fallback) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "%s", message);
        return false;
    }
    record_fullscreen_failure(display, message);
    return true;
}

static bool enter_exclusive(qa_display *display,
                            const qa_display_options *options,
                            qa_error *error)
{
    int index = SDL_GetWindowDisplayIndex(display->window);
    if (index < 0) return display_error(error, QA_ERROR_IO,
                                        "SDL_GetWindowDisplayIndex");
    SDL_DisplayMode selected = {0};
    bool found = false;
    if (options->minimum_refresh != 0 || options->maximum_refresh != 0) {
        int count = SDL_GetNumDisplayModes(index);
        if (count < 0) return display_error(error, QA_ERROR_IO,
                                            "SDL_GetNumDisplayModes");
        SDL_DisplayMode desktop;
        if (SDL_GetCurrentDisplayMode(index, &desktop) < 0)
            return display_error(error, QA_ERROR_IO,
                                 "SDL_GetCurrentDisplayMode");
        unsigned wanted_color = options->color_bits < 16
                                    ? (unsigned)SDL_BITSPERPIXEL(desktop.format)
                                    : options->color_bits;
        for (int i = 0; i < count; ++i) {
            SDL_DisplayMode mode;
            if (SDL_GetDisplayMode(index, i, &mode) < 0)
                return display_error(error, QA_ERROR_IO, "SDL_GetDisplayMode");
            if (mode.w != (int)options->width || mode.h != (int)options->height ||
                (unsigned)SDL_BITSPERPIXEL(mode.format) != wanted_color ||
                (options->minimum_refresh != 0 &&
                 mode.refresh_rate < options->minimum_refresh) ||
                (options->maximum_refresh != 0 &&
                 mode.refresh_rate > options->maximum_refresh)) continue;
            selected = mode;
            found = true;
        }
    } else {
        SDL_DisplayMode requested = {.w = (int)options->width,
                                     .h = (int)options->height,
                                     .refresh_rate = options->refresh_rate};
        found = SDL_GetClosestDisplayMode(index, &requested, &selected) != NULL;
    }
    if (!found) {
        if (!options->allow_fullscreen_fallback) {
            qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                         "No suitable exclusive SDL display mode was found");
            return false;
        }
        record_fullscreen_failure(display,
                                  "No suitable exclusive SDL display mode was found");
        return true;
    }
    if (SDL_SetWindowDisplayMode(display->window, &selected) < 0)
        return fullscreen_failure(display, options, "SDL_SetWindowDisplayMode",
                                  error);
    if (SDL_SetWindowFullscreen(display->window, SDL_WINDOW_FULLSCREEN) < 0)
        return fullscreen_failure(display, options, "SDL_SetWindowFullscreen",
                                  error);
    if ((SDL_GetWindowFlags(display->window) & SDL_WINDOW_FULLSCREEN) == 0)
        return fullscreen_state_failure(display, options,
            "SDL did not enter the requested exclusive fullscreen mode", error);
    return true;
}

static bool enter_initial_fullscreen(qa_display *display,
                                     const qa_display_options *options,
                                     qa_error *error)
{
    if (options->fullscreen == QA_DISPLAY_WINDOWED) return true;
    if (options->fullscreen == QA_DISPLAY_EXCLUSIVE)
        return enter_exclusive(display, options, error);
    if (SDL_SetWindowFullscreen(display->window,
                                SDL_WINDOW_FULLSCREEN_DESKTOP) < 0)
        return fullscreen_failure(display, options,
                                  "SDL_SetWindowFullscreen desktop", error);
    if ((SDL_GetWindowFlags(display->window) & SDL_WINDOW_FULLSCREEN_DESKTOP) !=
        SDL_WINDOW_FULLSCREEN_DESKTOP)
        return fullscreen_state_failure(display, options,
            "SDL did not enter the requested desktop fullscreen mode", error);
    return true;
}

static bool cpu_surface_create(qa_display *display, qa_error *error)
{
    SDL_Renderer *renderer = SDL_CreateRenderer(
        display->window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == NULL)
        return display_error(error, QA_ERROR_IO, "SDL_CreateRenderer");
    int width = 0, height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) < 0 ||
        width <= 0 || height <= 0) {
        SDL_DestroyRenderer(renderer);
        return display_error(error, QA_ERROR_IO, "SDL_GetRendererOutputSize");
    }
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              width, height);
    if (texture == NULL) {
        SDL_DestroyRenderer(renderer);
        return display_error(error, QA_ERROR_IO, "SDL_CreateTexture");
    }
    if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) < 0) {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        return display_error(error, QA_ERROR_IO, "SDL_SetTextureBlendMode");
    }
    display->native.cpu.renderer = renderer;
    display->native.cpu.texture = texture;
    display->native.cpu.width = (uint32_t)width;
    display->native.cpu.height = (uint32_t)height;
    return true;
}

qa_display *qa_display_create(const qa_display_options *input, qa_error *error)
{
    qa_display_options defaults;
    qa_display_options_default(&defaults);
    const qa_display_options *options = input == NULL ? &defaults : input;
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) {
        qa_error_set(error, QA_ERROR_IO, 0,
                     "The application has not initialized SDL video");
        return NULL;
    }
    if (!valid_dimensions(options->width, options->height, error) ||
        options->title == NULL ||
        (unsigned)options->backend > QA_DISPLAY_OPENGL ||
        (unsigned)options->fullscreen > QA_DISPLAY_EXCLUSIVE ||
        options->minimum_refresh < 0 || options->maximum_refresh < 0 ||
        options->refresh_rate < 0 ||
        (options->minimum_refresh != 0 && options->maximum_refresh != 0 &&
         options->minimum_refresh > options->maximum_refresh)) {
        if (options->title == NULL || (unsigned)options->backend > QA_DISPLAY_OPENGL ||
            (unsigned)options->fullscreen > QA_DISPLAY_EXCLUSIVE ||
            options->minimum_refresh < 0 || options->maximum_refresh < 0 ||
            options->refresh_rate < 0 ||
            (options->minimum_refresh != 0 && options->maximum_refresh != 0 &&
             options->minimum_refresh > options->maximum_refresh))
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "Invalid SDL display options");
        return NULL;
    }
    int display_index = selected_display(options->display_index, error);
    if (display_index < 0) return NULL;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display_index);
    int y = SDL_WINDOWPOS_CENTERED_DISPLAY(display_index);
    if (options->positioned) {
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(display_index, &bounds) < 0) {
            display_error(error, QA_ERROR_IO, "SDL_GetDisplayBounds");
            return NULL;
        }
        int64_t px = (int64_t)bounds.x + options->x;
        int64_t py = (int64_t)bounds.y + options->y;
        if (px < INT_MIN || px > INT_MAX || py < INT_MIN || py > INT_MAX) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "SDL window position exceeds signed int");
            return NULL;
        }
        x = (int)px;
        y = (int)py;
    }
    qa_display *display = calloc(1, sizeof(*display));
    if (display == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating SDL display");
        return NULL;
    }
    display->backend = options->backend;
    Uint32 flags = options->hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
    if (options->resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (options->high_dpi) flags |= SDL_WINDOW_ALLOW_HIGHDPI;
    bool ok = false;
    if (options->backend == QA_DISPLAY_OPENGL) {
        flags |= SDL_WINDOW_OPENGL;
        if (options->gl_library != NULL) {
            if (options->gl_library[0] == '\0') {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                             "SDL OpenGL library name is empty");
                goto done;
            }
            if (SDL_GL_LoadLibrary(options->gl_library) < 0) {
                display_error(error, QA_ERROR_IO, "SDL_GL_LoadLibrary");
                goto done;
            }
            display->native.gl.library_loaded = true;
        }
        if (!set_context_attributes(options, error) ||
            !create_gl_window(options, options->title, x, y, flags,
                              &display->window, &display->native.gl.context,
                              error) ||
            !reject_software_gl(options, error)) goto done;
    } else {
        display->window = SDL_CreateWindow(options->title, x, y,
                                           (int)options->width,
                                           (int)options->height, flags);
        if (display->window == NULL) {
            display_error(error, QA_ERROR_IO, "SDL_CreateWindow");
            goto done;
        }
    }
    if (!enter_initial_fullscreen(display, options, error)) goto done;
    if (options->backend == QA_DISPLAY_CPU &&
        !cpu_surface_create(display, error)) goto done;
    if (SDL_GetWindowID(display->window) == 0) {
        display_error(error, QA_ERROR_IO, "SDL_GetWindowID");
        goto done;
    }
    if (options->backend == QA_DISPLAY_OPENGL) {
        int drawable_width = 0, drawable_height = 0;
        SDL_GL_GetDrawableSize(display->window, &drawable_width,
                               &drawable_height);
        if (drawable_width <= 0 || drawable_height <= 0) {
            qa_error_set(error, QA_ERROR_IO, 0,
                         "SDL returned invalid OpenGL drawable dimensions");
            goto done;
        }
    }
    ok = true;
done:
    if (!ok) {
        qa_display_destroy(display);
        return NULL;
    }
    return display;
}

void qa_display_destroy(qa_display *display)
{
    if (display == NULL) return;
    if (display->backend == QA_DISPLAY_CPU) {
        if (display->native.cpu.texture != NULL)
            SDL_DestroyTexture(display->native.cpu.texture);
        if (display->native.cpu.renderer != NULL)
            SDL_DestroyRenderer(display->native.cpu.renderer);
    } else {
        if (display->native.gl.context != NULL)
            SDL_GL_DeleteContext(display->native.gl.context);
    }
    if (display->window != NULL) SDL_DestroyWindow(display->window);
    if (display->backend == QA_DISPLAY_OPENGL &&
        display->native.gl.library_loaded) SDL_GL_UnloadLibrary();
    free(display);
}

bool qa_display_info_get(const qa_display *display, qa_display_info *out,
                         qa_error *error)
{
    if (display == NULL || display->window == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid SDL display query");
        return false;
    }
    int logical_width = 0, logical_height = 0, drawable_width = 0,
        drawable_height = 0;
    SDL_GetWindowSize(display->window, &logical_width, &logical_height);
    if (display->backend == QA_DISPLAY_OPENGL)
        SDL_GL_GetDrawableSize(display->window, &drawable_width, &drawable_height);
    else if (SDL_GetRendererOutputSize(display->native.cpu.renderer,
                                       &drawable_width, &drawable_height) < 0)
        return display_error(error, QA_ERROR_IO, "SDL_GetRendererOutputSize");
    int index = SDL_GetWindowDisplayIndex(display->window);
    SDL_DisplayMode mode;
    if (logical_width <= 0 || logical_height <= 0 || drawable_width <= 0 ||
        drawable_height <= 0 || index < 0 ||
        SDL_GetCurrentDisplayMode(index, &mode) < 0)
        return display_error(error, QA_ERROR_IO, "SDL display information");
    Uint32 flags = SDL_GetWindowFlags(display->window);
    qa_display_fullscreen fullscreen = QA_DISPLAY_WINDOWED;
    if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP)
        fullscreen = QA_DISPLAY_DESKTOP;
    else if ((flags & SDL_WINDOW_FULLSCREEN) != 0)
        fullscreen = QA_DISPLAY_EXCLUSIVE;
    *out = (qa_display_info){
        .window_id = SDL_GetWindowID(display->window),
        .logical_width = (uint32_t)logical_width,
        .logical_height = (uint32_t)logical_height,
        .drawable_width = (uint32_t)drawable_width,
        .drawable_height = (uint32_t)drawable_height,
        .display_index = index,
        .refresh_rate = mode.refresh_rate,
        .backend = display->backend,
        .fullscreen = fullscreen,
        .visible = (flags & SDL_WINDOW_SHOWN) != 0 &&
                   (flags & SDL_WINDOW_HIDDEN) == 0,
        .focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0,
        .minimized = (flags & SDL_WINDOW_MINIMIZED) != 0,
        .maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0
    };
    return true;
}

bool qa_display_set_visible(qa_display *display, bool visible, qa_error *error)
{
    if (display == NULL || display->window == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid SDL display visibility");
        return false;
    }
    if (visible) SDL_ShowWindow(display->window);
    else SDL_HideWindow(display->window);
    return true;
}

bool qa_display_set_size(qa_display *display, uint32_t width, uint32_t height,
                         qa_error *error)
{
    if (display == NULL || display->window == NULL ||
        !valid_dimensions(width, height, error)) {
        if (display == NULL || display->window == NULL)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid SDL display resize");
        return false;
    }
    SDL_SetWindowSize(display->window, (int)width, (int)height);
    return true;
}

bool qa_display_set_fullscreen(qa_display *display, qa_display_fullscreen mode,
                               qa_error *error)
{
    if (display == NULL || display->window == NULL ||
        (unsigned)mode > QA_DISPLAY_EXCLUSIVE) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Invalid SDL fullscreen transition");
        return false;
    }
    Uint32 flags = mode == QA_DISPLAY_WINDOWED ? 0 :
                   mode == QA_DISPLAY_DESKTOP ? SDL_WINDOW_FULLSCREEN_DESKTOP :
                                                SDL_WINDOW_FULLSCREEN;
    if (SDL_SetWindowFullscreen(display->window, flags) < 0)
        return display_error(error, QA_ERROR_IO, "SDL_SetWindowFullscreen");
    Uint32 actual = SDL_GetWindowFlags(display->window);
    bool entered = mode == QA_DISPLAY_WINDOWED
                       ? (actual & SDL_WINDOW_FULLSCREEN) == 0
                       : mode == QA_DISPLAY_DESKTOP
                             ? (actual & SDL_WINDOW_FULLSCREEN_DESKTOP) ==
                                   SDL_WINDOW_FULLSCREEN_DESKTOP
                             : (actual & SDL_WINDOW_FULLSCREEN) != 0 &&
                                   (actual & SDL_WINDOW_FULLSCREEN_DESKTOP) !=
                                       SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (!entered) {
        qa_error_set(error, QA_ERROR_IO, 0,
                     "SDL did not enter the requested fullscreen mode");
        return false;
    }
    display->fullscreen_failure[0] = '\0';
    return true;
}

const char *qa_display_fullscreen_failure(const qa_display *display)
{
    return display == NULL ? "Invalid SDL display" : display->fullscreen_failure;
}

bool qa_display_make_current(qa_display *display, qa_error *error)
{
    if (display == NULL || display->window == NULL ||
        display->backend != QA_DISPLAY_OPENGL ||
        display->native.gl.context == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "SDL display does not own an OpenGL context");
        return false;
    }
    if (SDL_GL_MakeCurrent(display->window, display->native.gl.context) < 0)
        return display_error(error, QA_ERROR_IO, "SDL_GL_MakeCurrent");
    return true;
}

void *qa_display_gl_proc(qa_display *display, const char *name, qa_error *error)
{
    if (name == NULL || name[0] == '\0' || !qa_display_make_current(display, error)) {
        if (name == NULL || name[0] == '\0')
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "OpenGL procedure name is empty");
        return NULL;
    }
    void *address = SDL_GL_GetProcAddress(name);
    if (address == NULL)
        display_error(error, QA_ERROR_UNSUPPORTED, "SDL_GL_GetProcAddress");
    return address;
}

bool qa_display_swap(qa_display *display, qa_error *error)
{
    if (!qa_display_make_current(display, error)) return false;
    SDL_GL_SwapWindow(display->window);
    return true;
}

bool qa_display_set_swap_interval(qa_display *display, int interval,
                                  qa_error *error)
{
    if ((interval != -1 && interval != 0 && interval != 1) ||
        !qa_display_make_current(display, error)) {
        if (interval != -1 && interval != 0 && interval != 1)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "SDL swap interval must be -1, 0, or 1");
        return false;
    }
    if (SDL_GL_SetSwapInterval(interval) < 0)
        return display_error(error, QA_ERROR_UNSUPPORTED,
                             "SDL_GL_SetSwapInterval");
    return true;
}

bool qa_display_swap_interval(qa_display *display, int *out, qa_error *error)
{
    if (out == NULL || !qa_display_make_current(display, error)) {
        if (out == NULL)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "Invalid SDL swap interval destination");
        return false;
    }
    *out = SDL_GL_GetSwapInterval();
    return true;
}

static bool resize_cpu_texture(qa_display *display, uint32_t width,
                               uint32_t height, qa_error *error)
{
    if (display->native.cpu.width == width &&
        display->native.cpu.height == height) return true;
    SDL_Texture *texture = SDL_CreateTexture(display->native.cpu.renderer,
                                              SDL_PIXELFORMAT_RGBA32,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              (int)width, (int)height);
    if (texture == NULL)
        return display_error(error, QA_ERROR_IO, "SDL_CreateTexture resize");
    if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) < 0) {
        SDL_DestroyTexture(texture);
        return display_error(error, QA_ERROR_IO,
                             "SDL_SetTextureBlendMode resize");
    }
    SDL_DestroyTexture(display->native.cpu.texture);
    display->native.cpu.texture = texture;
    display->native.cpu.width = width;
    display->native.cpu.height = height;
    display->native.cpu.has_frame = false;
    return true;
}

bool qa_display_present_rgba(qa_display *display, qa_bytes rgba,
                             uint32_t width, uint32_t height, qa_error *error)
{
    if (display == NULL || display->backend != QA_DISPLAY_CPU ||
        rgba.data == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Invalid SDL CPU presentation pixels");
        return false;
    }
    if (!valid_dimensions(width, height, error)) return false;
    if ((size_t)width > SIZE_MAX / height ||
        (size_t)width * height > SIZE_MAX / 4) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "SDL CPU presentation dimensions overflow");
        return false;
    }
    size_t required = (size_t)width * height * 4;
    if (rgba.size < required) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "SDL CPU presentation pixels are truncated");
        return false;
    }
    int output_width = 0, output_height = 0;
    if (SDL_GetRendererOutputSize(display->native.cpu.renderer,
                                  &output_width, &output_height) < 0)
        return display_error(error, QA_ERROR_IO, "SDL_GetRendererOutputSize");
    if (output_width <= 0 || output_height <= 0 ||
        (uint32_t)output_width != width || (uint32_t)output_height != height) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "CPU frame dimensions differ from the SDL drawable");
        return false;
    }
    if (!resize_cpu_texture(display, width, height, error)) return false;
    if (SDL_UpdateTexture(display->native.cpu.texture, NULL, rgba.data,
                          (int)(width * 4)) < 0 ||
        SDL_RenderCopy(display->native.cpu.renderer,
                       display->native.cpu.texture, NULL, NULL) < 0)
        return display_error(error, QA_ERROR_IO,
                             "SDL CPU texture presentation");
    SDL_RenderPresent(display->native.cpu.renderer);
    display->native.cpu.has_frame = true;
    return true;
}

bool qa_display_present_cpu(void *display, qa_bytes rgba, uint32_t width,
                            uint32_t height, qa_error *error)
{
    return qa_display_present_rgba(display, rgba, width, height, error);
}

bool qa_display_capture_cpu(qa_display *display, qa_buffer *out,
                            uint32_t *width, uint32_t *height, qa_error *error)
{
    if (display == NULL || display->backend != QA_DISPLAY_CPU || out == NULL ||
        !display->native.cpu.has_frame) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "SDL CPU capture requires a presented frame");
        return false;
    }
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(display->native.cpu.renderer, &w, &h) < 0)
        return display_error(error, QA_ERROR_IO, "SDL_GetRendererOutputSize");
    if (w <= 0 || h <= 0 || (uint32_t)w != display->native.cpu.width ||
        (uint32_t)h != display->native.cpu.height ||
        (size_t)w > SIZE_MAX / (size_t)h ||
        (size_t)w * (size_t)h > SIZE_MAX / 4) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "SDL CPU capture drawable changed before presentation");
        return false;
    }
    size_t bytes = (size_t)w * (size_t)h * 4;
    uint8_t *pixels = malloc(bytes);
    if (pixels == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating SDL CPU capture");
        return false;
    }
    if (SDL_RenderCopy(display->native.cpu.renderer,
                       display->native.cpu.texture, NULL, NULL) < 0 ||
        SDL_RenderReadPixels(display->native.cpu.renderer, NULL,
                             SDL_PIXELFORMAT_RGBA32, pixels, w * 4) < 0) {
        free(pixels);
        return display_error(error, QA_ERROR_IO, "SDL_RenderReadPixels");
    }
    *out = (qa_buffer){pixels, bytes};
    if (width != NULL) *width = (uint32_t)w;
    if (height != NULL) *height = (uint32_t)h;
    return true;
}
