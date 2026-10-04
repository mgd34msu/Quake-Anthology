#include "qa/display.h"
#include "qa/display_save.h"
#include "qa/display_settings.h"
#include "qa/source_save.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct display_native_lease {
    size_t references;
    qa_display_backend backend;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_GLContext context;
    bool library_loaded;
} display_native_lease;
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
    bool native_borrowed, capturing;
    display_native_lease *lease;
    qa_display_surface_ticket *surface_ticket;
    qa_display_restore_guard *pending_restore;
    const qa_display_restore_guard *restore_guard;
    const qa_display_surface_ticket *ready_surface;
    qa_display_endpoint ready_endpoint;
    uint64_t revision;
    bool destroy_pending;
    qa_display_gamma *gamma;
};

struct qa_display_gamma {
    qa_display *display;
    display_native_lease *lease;
    SDL_Window *window;
    qa_display_gamma_capability capability;
    Uint16 original[3][256];
    bool restore_needed;
    qa_display_gamma_ticket *ticket;
    float gamma;
    bool applied;
};
struct qa_display_gamma_ticket {
    qa_display_gamma *owner;
    qa_display *original_display, *target;
    SDL_Window *window;
    Uint16 previous[3][256], ramp[256];
    qa_display_endpoint endpoint;
    float gamma;
    bool prepared, attempted, applied, ready, published;
};
static qa_display_gamma *native_gamma_owner;
static bool gamma_target_owned(const qa_display_gamma_ticket *, bool);

struct qa_display_surface_ticket {
    qa_display *active, *candidate;
    display_native_lease *active_lease;
    qa_display_settings settings;
    qa_display_info original, staged;
    SDL_Window *previous_window;
    SDL_GLContext previous_context, context;
    SDL_DisplayMode original_mode;
    int x, y, original_interval;
    Uint32 previous_focus;
    int visual[11];
    bool interval_known, entered, staged_valid, native_restored;
};
static _Thread_local uint64_t native_revision = 1;
static void native_changed(void)
{
    if (!++native_revision) ++native_revision;
}
static void display_changed(qa_display *display)
{
    display->ready_surface = NULL;
    if (!++display->revision) ++display->revision;
    native_changed();
}

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
        display->window, -1, 0);
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
    int x = (int)SDL_WINDOWPOS_CENTERED_DISPLAY((Uint32)display_index);
    int y = (int)SDL_WINDOWPOS_CENTERED_DISPLAY((Uint32)display_index);
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
    display->revision = 1;
    display_changed(display);
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
    if (ok) {
        display->lease=calloc(1,sizeof(*display->lease));
        if (!display->lease) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating the native display lifetime lease"); ok=false;
        } else {
            display->lease->references=1; display->lease->backend=display->backend;
            display->lease->window=display->window;
            if (display->backend==QA_DISPLAY_CPU) display->lease->renderer=display->native.cpu.renderer;
            else { display->lease->context=display->native.gl.context; display->lease->library_loaded=display->native.gl.library_loaded; }
        }
    }
    if (!ok) {
        qa_display_destroy(display);
        return NULL;
    }
    return display;
}

void qa_display_destroy(qa_display *display)
{
    if (display == NULL) return;
    display_changed(display);
    if (display->surface_ticket || display->pending_restore || display->restore_guard || display->gamma) {
        display->destroy_pending = true; return;
    }
    if (display->lease) {
        if (display->backend==QA_DISPLAY_CPU && display->native.cpu.texture)
            SDL_DestroyTexture(display->native.cpu.texture);
        display_native_lease *lease=display->lease;
        if (--lease->references==0) {
            if (lease->backend==QA_DISPLAY_CPU && lease->renderer) SDL_DestroyRenderer(lease->renderer);
            if (lease->backend==QA_DISPLAY_OPENGL && lease->context) SDL_GL_DeleteContext(lease->context);
            if (lease->window) SDL_DestroyWindow(lease->window);
            if (lease->library_loaded) SDL_GL_UnloadLibrary();
            free(lease);
        }
        free(display); return;
    }
    if (display->backend == QA_DISPLAY_CPU) {
        if (display->native.cpu.texture != NULL)
            SDL_DestroyTexture(display->native.cpu.texture);
        if (!display->native_borrowed && display->native.cpu.renderer != NULL)
            SDL_DestroyRenderer(display->native.cpu.renderer);
    } else {
        if (!display->native_borrowed && display->native.gl.context != NULL)
            SDL_GL_DeleteContext(display->native.gl.context);
    }
    if (!display->native_borrowed && display->window != NULL) SDL_DestroyWindow(display->window);
    if (!display->native_borrowed && display->backend == QA_DISPLAY_OPENGL &&
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
    display_changed(display);
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
    display_changed(display);
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
    display_changed(display);
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
    display_changed(display);
    if (SDL_GL_GetCurrentWindow() == display->window &&
        SDL_GL_GetCurrentContext() == display->native.gl.context) return true;
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
    display_changed(display);
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
    display_changed(display);
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
    display_changed(display);
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

static bool gamma_owned(const qa_display_gamma *owner, bool retiring)
{
    return owner && native_gamma_owner == owner && owner->display &&
        owner->display->gamma == owner && owner->display->window == owner->window &&
        owner->display->lease == owner->lease && owner->lease &&
        owner->lease->window == owner->window &&
        (retiring || !owner->display->destroy_pending);
}

static bool gamma_display(qa_display_gamma *owner, int *count, int *index,
    const char **name, qa_error *error)
{
    *count = SDL_GetNumVideoDisplays();
    *index = SDL_GetWindowDisplayIndex(owner->window);
    if (*count < 1 || *index < 0)
        return display_error(error, QA_ERROR_IO, "Reading the retained native gamma display");
    *name = SDL_GetDisplayName(*index);
    if (!*name) return display_error(error, QA_ERROR_IO, "SDL_GetDisplayName gamma");
    return true;
}

static bool gamma_restore(qa_display_gamma *owner, qa_error *error)
{
    if (!owner->restore_needed) return true;
    int count, index; const char *name;
    if (!gamma_display(owner, &count, &index, &name, error)) return false;
    if (index != owner->capability.display_index || strcmp(name, owner->capability.display_name)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
            "Native gamma restoration requires its original physical display; cross-display restoration is unsupported");
        return false;
    }
    display_changed(owner->display);
    if (SDL_SetWindowGammaRamp(owner->window, owner->original[0], owner->original[1], owner->original[2]) < 0)
        return display_error(error, QA_ERROR_IO, "SDL_SetWindowGammaRamp restore");
    owner->restore_needed = false;
    return true;
}

bool qa_display_gamma_begin(qa_display *display, bool ignore_hardware,
    qa_display_gamma **out, qa_error *error)
{
    if (!display || !out || *out || native_gamma_owner || display->gamma || display->destroy_pending ||
        display->capturing || display->surface_ticket || display->native_borrowed ||
        !display->lease || display->lease->references != 1 || display->window != display->lease->window) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native gamma requires its exclusive actual display owner");
        return false;
    }
    qa_display_gamma *owner = calloc(1, sizeof(*owner));
    if (!owner) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining native gamma display"); return false; }
    owner->display = display; owner->lease = display->lease; owner->window = display->window;
    int count, index; const char *name;
    if (!gamma_display(owner, &count, &index, &name, error)) { free(owner); return false; }
    if (strlen(name) >= sizeof(owner->capability.display_name)) {
        free(owner); qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Native gamma display name exceeds the retained identity"); return false;
    }
    owner->capability.display_index = index;
    memcpy(owner->capability.display_name, name, strlen(name) + 1);
    const char *unavailable = ignore_hardware ? "Hardware gamma is disabled by the selected Source renderer" :
        count != 1 ? "Gamma requires the single-display ownership profile" :
        !(SDL_GetWindowFlags(display->window) & SDL_WINDOW_INPUT_FOCUS) ? "Gamma acquisition requires native input focus" : NULL;
    if (unavailable) {
        owner->capability.kind = QA_DISPLAY_GAMMA_UNAVAILABLE;
        snprintf(owner->capability.reason, sizeof(owner->capability.reason), "%s", unavailable);
    } else if (SDL_GetWindowGammaRamp(owner->window, owner->original[0], owner->original[1], owner->original[2]) < 0) {
        owner->capability.kind = QA_DISPLAY_GAMMA_UNSUPPORTED;
        snprintf(owner->capability.reason, sizeof(owner->capability.reason), "SDL_GetWindowGammaRamp: %s", SDL_GetError());
    } else {
        display_changed(display);
        if (SDL_SetWindowGammaRamp(owner->window, owner->original[0], owner->original[1], owner->original[2]) < 0) {
            owner->capability.kind = QA_DISPLAY_GAMMA_UNSUPPORTED;
            snprintf(owner->capability.reason, sizeof(owner->capability.reason), "SDL_SetWindowGammaRamp: %s", SDL_GetError());
        } else {
            owner->capability.kind = QA_DISPLAY_GAMMA_ACCEPTED;
            owner->restore_needed = true;
        }
    }
    display->gamma = native_gamma_owner = owner;
    *out = owner;
    return true;
}

bool qa_display_gamma_capability_read(const qa_display_gamma *owner,
    qa_display_gamma_capability *out)
{
    if (!out || !gamma_owned(owner, false)) return false;
    *out = owner->capability;
    return true;
}

qa_display_gamma *qa_display_gamma_borrow(const qa_display *display)
{
    qa_display_gamma *owner = native_gamma_owner;
    return gamma_owned(owner, false) && display && !display->destroy_pending &&
        display->lease == owner->lease && display->window == owner->window ? owner : NULL;
}

bool qa_display_gamma_parent_is(const qa_display_gamma *owner, const qa_display *display)
{
    return gamma_owned(owner, true) && owner->display == display;
}
bool qa_display_gamma_applied_is(const qa_display *display)
{
    const qa_display_gamma_ticket *ticket=native_gamma_owner?native_gamma_owner->ticket:NULL;
    if (ticket && ticket->target==display && ticket->applied && gamma_target_owned(ticket,false)) return true;
    const qa_display_gamma *owner = qa_display_gamma_borrow(display);
    return owner && owner->capability.kind == QA_DISPLAY_GAMMA_ACCEPTED && owner->applied;
}

bool qa_display_gamma_read(qa_display_gamma *owner,
    qa_display_gamma_capability *out, qa_error *error)
{
    if (!out || !gamma_owned(owner, false)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Gamma observation lost its retained native display"); return false;
    }
    if (owner->capability.kind == QA_DISPLAY_GAMMA_ACCEPTED) {
        int count, index; const char *name;
        if (!gamma_display(owner, &count, &index, &name, error)) return false;
        if (count != 1 || index != owner->capability.display_index || strcmp(name, owner->capability.display_name)) {
            owner->capability.kind = QA_DISPLAY_GAMMA_RETIRED;
            snprintf(owner->capability.reason, sizeof(owner->capability.reason),
                "Native display topology changed; gamma continuity is unsupported");
            if (!gamma_restore(owner, error)) return false;
        }
    }
    if (owner->capability.kind == QA_DISPLAY_GAMMA_RETIRED && !gamma_restore(owner, error)) return false;
    *out = owner->capability;
    return true;
}

bool qa_display_gamma_apply(qa_display_gamma *owner, float gamma, qa_error *error)
{
    qa_display_gamma_capability capability;
    if (!isfinite(gamma) || gamma < .5f || gamma > 3) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native gamma must be within 0.5..3"); return false;
    }
    if (!qa_display_gamma_read(owner, &capability, error)) return false;
    if (owner->ticket || owner->display->surface_ticket) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native gamma application retains a settings ticket"); return false;
    }
    if (capability.kind != QA_DISPLAY_GAMMA_ACCEPTED) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Native gamma cannot apply: %s", capability.reason); return false;
    }
    Uint16 ramp[256];
    SDL_CalculateGammaRamp(gamma, ramp);
    display_changed(owner->display);
    if (SDL_SetWindowGammaRamp(owner->window, ramp, ramp, ramp) < 0)
        return display_error(error, QA_ERROR_IO, "SDL_SetWindowGammaRamp Source gamma");
    owner->gamma = gamma; owner->applied = true;
    return true;
}

bool qa_display_gamma_release(qa_display_gamma **out, qa_error *error)
{
    if (!out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid native gamma release"); return false; }
    qa_display_gamma *owner = *out;
    if (!owner) return true;
    if (!gamma_owned(owner, true)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native gamma release lost its physical display owner"); return false;
    }
    if (owner->ticket || owner->lease->references != 1) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native gamma retirement retains a detached physical display continuation");
        return false;
    }
    if (!gamma_restore(owner, error)) return false;
    owner->display->gamma = NULL;
    native_gamma_owner = NULL;
    free(owner); *out = NULL;
    return true;
}

static bool gamma_target_owned(const qa_display_gamma_ticket *ticket, bool retiring)
{
    if (!ticket || !gamma_owned(ticket->owner, retiring) || ticket->owner->ticket != ticket ||
        !ticket->target || ticket->target->window != ticket->window ||
        (!retiring && ticket->target->destroy_pending)) return false;
    if (ticket->target == ticket->owner->display) return true;
    if (ticket->target->native_borrowed && ticket->target->lease==ticket->owner->lease &&
        ticket->target->window==ticket->owner->window) return true;
    const qa_display_surface_ticket *surface = ticket->original_display->surface_ticket;
    return surface && surface->active == ticket->original_display && surface->candidate == ticket->target &&
        (retiring || (surface->entered && surface->staged_valid && !surface->native_restored));
}

bool qa_display_gamma_prepare(qa_display_gamma *owner, qa_display *target, float gamma,
    qa_display_gamma_ticket **out, qa_error *error)
{
    if (!out || *out || !gamma_owned(owner, false) || owner->ticket || !target ||
        !target->window || target->destroy_pending || !isfinite(gamma) || gamma < .5f || gamma > 3) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native gamma preparation requires its retained target and valid brightness"); return false;
    }
    qa_display_gamma_ticket *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Preparing retained native gamma"); return false; }
    ticket->owner = owner; ticket->original_display = owner->display;
    ticket->target = target; ticket->window = target->window; ticket->gamma = gamma;
    owner->ticket = ticket; *out = ticket;
    if (!gamma_target_owned(ticket, false)) {
        owner->ticket = NULL; free(ticket); *out = NULL;
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native gamma target is not its actual retained display candidate"); return false;
    }
    int count = SDL_GetNumVideoDisplays(), index = SDL_GetWindowDisplayIndex(ticket->window);
    const char *name = index < 0 ? NULL : SDL_GetDisplayName(index);
    if (count < 1 || index < 0 || !name)
        return display_error(error, QA_ERROR_IO, "Reading the prepared native gamma display");
    if (owner->capability.kind != QA_DISPLAY_GAMMA_ACCEPTED || count != 1 ||
        index != owner->capability.display_index || strcmp(name, owner->capability.display_name)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Native gamma preparation requires the accepted original physical display"); return false;
    }
    if (SDL_GetWindowGammaRamp(ticket->window, ticket->previous[0], ticket->previous[1], ticket->previous[2]) < 0)
        return display_error(error, QA_ERROR_IO, "SDL_GetWindowGammaRamp prepare");
    ticket->prepared = true;
    SDL_CalculateGammaRamp(gamma, ticket->ramp);
    display_changed(target);
    ticket->attempted = true;
    if (SDL_SetWindowGammaRamp(ticket->window, ticket->ramp, ticket->ramp, ticket->ramp) < 0)
        return display_error(error, QA_ERROR_IO, "SDL_SetWindowGammaRamp prepare");
    ticket->applied=true;
    return qa_display_gamma_ready(ticket, error);
}

bool qa_display_gamma_ready(qa_display_gamma_ticket *ticket, qa_error *error)
{
    if (ticket) ticket->ready = false;
    if (!gamma_target_owned(ticket, false) || !ticket->prepared || !ticket->applied || ticket->published) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native gamma readiness lost its prepared physical target"); return false;
    }
    int count = SDL_GetNumVideoDisplays(), index = SDL_GetWindowDisplayIndex(ticket->window);
    const char *name = index < 0 ? NULL : SDL_GetDisplayName(index);
    if (count < 1 || index < 0 || !name)
        return display_error(error, QA_ERROR_IO, "Reading the ready native gamma display");
    if (count != 1 || index != ticket->owner->capability.display_index ||
        strcmp(name, ticket->owner->capability.display_name)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Prepared native gamma display topology changed"); return false;
    }
    Uint16 actual[3][256];
    if (SDL_GetWindowGammaRamp(ticket->window, actual[0], actual[1], actual[2]) < 0)
        return display_error(error, QA_ERROR_IO, "SDL_GetWindowGammaRamp ready");
    for (size_t i = 0; i < 3; ++i)
        if (memcmp(actual[i], ticket->ramp, sizeof(ticket->ramp))) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native gamma API cached ramp changed before publication"); return false;
        }
    if (!qa_display_endpoint_read(ticket->target, &ticket->endpoint)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prepared gamma lost its actual native endpoint"); return false;
    }
    ticket->ready = true;
    return true;
}

bool qa_display_gamma_ready_is(const qa_display_gamma_ticket *ticket)
{
    return gamma_target_owned(ticket, false) && ticket->ready && ticket->prepared &&
        ticket->applied && !ticket->published && qa_display_endpoint_is(ticket->target, &ticket->endpoint);
}

void qa_display_gamma_publish(qa_display_gamma_ticket *ticket)
{
    if (!ticket || ticket->published || !ticket->ready) return;
    ticket->owner->gamma = ticket->gamma; ticket->owner->applied = true;
    ticket->ready = false; ticket->published = true;
}

bool qa_display_gamma_abort(qa_display_gamma_ticket **out, qa_error *error)
{
    if (!out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid native gamma abort"); return false; }
    qa_display_gamma_ticket *ticket = *out;
    if (!ticket) return true;
    if (!gamma_target_owned(ticket, true) || ticket->published) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native gamma abort lost its unpublished retained target"); return false;
    }
    ticket->ready = false;
    if (ticket->attempted) {
        display_changed(ticket->target);
        if (SDL_SetWindowGammaRamp(ticket->window, ticket->previous[0], ticket->previous[1], ticket->previous[2]) < 0)
            return display_error(error, QA_ERROR_IO, "SDL_SetWindowGammaRamp abort");
        ticket->attempted = false;
        ticket->applied = false;
    }
    ticket->owner->ticket = NULL;
    free(ticket); *out = NULL;
    return true;
}

bool qa_display_gamma_finish(qa_display_gamma_ticket **out, qa_error *error)
{
    if (!out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid native gamma retirement"); return false; }
    qa_display_gamma_ticket *ticket = *out;
    if (!ticket) return true;
    if (!gamma_target_owned(ticket, true) || !ticket->published) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Native gamma retirement lost its published retained target"); return false;
    }
    ticket->owner->ticket = NULL;
    free(ticket); *out = NULL;
    return true;
}

typedef struct display_saved {
    qa_display_info info;
    int32_t x,y,swap_interval;
    uint32_t texture_width,texture_height;
    char *title;
    char fullscreen_failure[256];
    qa_buffer pixels;
    bool texture,frame;
} display_saved;
struct qa_display_restore_guard {
    qa_display *active,*candidate;
    SDL_Window *window;
    SDL_GLContext context;
    SDL_Renderer *renderer;
    display_saved saved,baseline,staged;
    bool attempted,prepared,transferred;
};
static bool display_save_error(qa_error *error,qa_status status,const char *message)
{ qa_error_set(error,status,0,"%s",message); return false; }
bool qa_display_restore_info(const qa_display_restore_guard *guard,const qa_display *candidate,qa_display_info *out)
{
    if (!guard || !candidate || !out || guard->transferred || guard->candidate!=candidate ||
        !guard->active || guard->active->destroy_pending || candidate->destroy_pending ||
        !candidate->native_borrowed || candidate->window!=guard->window ||
        guard->active->window!=guard->window || !candidate->lease || candidate->lease!=guard->active->lease) return false;
    *out=guard->saved.info;
    return true;
}
static void display_saved_free(display_saved *saved)
{ free(saved->title); qa_buffer_free(&saved->pixels); }
static bool display_save_fields(qa_source_save_io *io,display_saved *saved)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[4]={'Q','D','S','P'}; uint32_t backend=saved->info.backend,fullscreen=saved->info.fullscreen;
    int32_t index=saved->info.display_index,refresh=saved->info.refresh_rate;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QDSP",4) ||
        !qa_source_save_u32(io,&backend) || backend>QA_DISPLAY_OPENGL ||
        !qa_source_save_u32(io,&fullscreen) || fullscreen>QA_DISPLAY_EXCLUSIVE ||
        !qa_source_save_u32(io,&saved->info.logical_width) || !qa_source_save_u32(io,&saved->info.logical_height) ||
        !qa_source_save_u32(io,&saved->info.drawable_width) || !qa_source_save_u32(io,&saved->info.drawable_height) ||
        !valid_dimensions(saved->info.logical_width,saved->info.logical_height,io->error) ||
        !valid_dimensions(saved->info.drawable_width,saved->info.drawable_height,io->error) ||
        !qa_source_save_i32(io,&index) || index<0 || !qa_source_save_i32(io,&refresh) || refresh<0 ||
        !qa_source_save_bool(io,&saved->info.visible) || !qa_source_save_bool(io,&saved->info.focused) ||
        !qa_source_save_bool(io,&saved->info.minimized) || !qa_source_save_bool(io,&saved->info.maximized) ||
        !qa_source_save_i32(io,&saved->x) || !qa_source_save_i32(io,&saved->y) ||
        !qa_source_save_i32(io,&saved->swap_interval) || saved->swap_interval < -1 || saved->swap_interval>1) return false;
    saved->info.backend=(qa_display_backend)backend; saved->info.fullscreen=(qa_display_fullscreen)fullscreen;
    saved->info.display_index=index; saved->info.refresh_rate=refresh;
    size_t title=reading?0:strlen(saved->title);
    if (!qa_source_save_count(io,&title,reading?io->input.size-io->offset:SIZE_MAX-1) || title==SIZE_MAX) return false;
    if (reading) {
        saved->title=malloc(title+1);
        if (!saved->title) return display_save_error(io->error,QA_ERROR_MEMORY,"Restoring display title");
    }
    if (!qa_source_save_bytes(io,saved->title,title) || memchr(saved->title,0,title) ||
        !qa_source_save_bytes(io,saved->fullscreen_failure,sizeof(saved->fullscreen_failure)) ||
        !memchr(saved->fullscreen_failure,0,sizeof(saved->fullscreen_failure)) ||
        !qa_source_save_bool(io,&saved->texture) || !qa_source_save_bool(io,&saved->frame) ||
        !qa_source_save_u32(io,&saved->texture_width) || !qa_source_save_u32(io,&saved->texture_height)) return false;
    if (reading) saved->title[title]=0;
    if (backend==QA_DISPLAY_OPENGL) return !saved->texture && !saved->frame && !saved->texture_width && !saved->texture_height;
    if (saved->frame && !saved->texture) return false;
    if (!saved->texture) return !saved->texture_width && !saved->texture_height;
    if (!valid_dimensions(saved->texture_width,saved->texture_height,io->error)) return false;
    if (!saved->frame) return true;
    if (saved->texture_width!=saved->info.drawable_width || saved->texture_height!=saved->info.drawable_height ||
        (size_t)saved->texture_width>SIZE_MAX/saved->texture_height/4) return false;
    size_t size=(size_t)saved->texture_width*saved->texture_height*4;
    if (reading) {
        saved->pixels.data=malloc(size); saved->pixels.size=size;
        if (!saved->pixels.data) return display_save_error(io->error,QA_ERROR_MEMORY,"Restoring display's genuine presented texture pixels");
    }
    return saved->pixels.size==size && qa_source_save_bytes(io,saved->pixels.data,size);
}
static bool display_observe(qa_display *display,display_saved *saved,bool pixels,qa_error *error)
{
    if (!display || !display->window || display->capturing || !qa_display_info_get(display,&saved->info,error)) return false;
    int x=0,y=0; SDL_GetWindowPosition(display->window,&x,&y); saved->x=x; saved->y=y;
    const char *title=SDL_GetWindowTitle(display->window); size_t size=strlen(title);
    if (size==SIZE_MAX || !(saved->title=malloc(size+1))) return display_save_error(error,QA_ERROR_MEMORY,"Retaining display title");
    memcpy(saved->title,title,size+1); memcpy(saved->fullscreen_failure,display->fullscreen_failure,sizeof(saved->fullscreen_failure));
    if (display->backend==QA_DISPLAY_OPENGL) return qa_display_swap_interval(display,&saved->swap_interval,error);
    saved->texture=display->native.cpu.texture!=NULL; saved->frame=display->native.cpu.has_frame;
    saved->texture_width=display->native.cpu.width; saved->texture_height=display->native.cpu.height;
    if (pixels && saved->frame) return qa_display_capture_cpu(display,&saved->pixels,NULL,NULL,error);
    return true;
}
static bool display_current_equal(const display_saved *a,const display_saved *b)
{
    return a->info.backend==b->info.backend && a->info.logical_width==b->info.logical_width &&
        a->info.logical_height==b->info.logical_height && a->info.drawable_width==b->info.drawable_width &&
        a->info.drawable_height==b->info.drawable_height && a->info.display_index==b->info.display_index &&
        a->info.refresh_rate==b->info.refresh_rate && a->info.fullscreen==b->info.fullscreen &&
        a->info.visible==b->info.visible && a->info.focused==b->info.focused && a->info.minimized==b->info.minimized &&
        a->info.maximized==b->info.maximized && a->x==b->x && a->y==b->y && a->swap_interval==b->swap_interval &&
        !strcmp(a->title,b->title);
}
static bool window_settings_stage(qa_display *display,const qa_display_settings *settings,
    bool visible,bool focused,qa_error *error)
{
    if (!qa_display_set_fullscreen(display,settings->fullscreen,error)) return false;
    if (settings->fullscreen==QA_DISPLAY_WINDOWED) {
        int width=0,height=0; SDL_GetWindowSize(display->window,&width,&height);
        if (width!=(int)settings->width || height!=(int)settings->height) {
            SDL_RestoreWindow(display->window);
            if (!qa_display_set_size(display,settings->width,settings->height,error)) return false;
        }
    }
    if (!qa_display_set_visible(display,visible,error)) return false;
    if (focused && SDL_SetWindowInputFocus(display->window)<0)
        return display_error(error,QA_ERROR_IO,"Preparing candidate native focus");
    if (display->backend==QA_DISPLAY_OPENGL &&
        (!qa_display_set_swap_interval(display,settings->swap_interval,error) ||
        SDL_GL_GetSwapInterval()!=settings->swap_interval))
        return display_save_error(error,QA_ERROR_IO,"SDL did not enter the requested candidate swap interval");
    SDL_PumpEvents();
    if (display->backend==QA_DISPLAY_CPU &&
        (SDL_RenderSetViewport(display->native.cpu.renderer,NULL)<0 ||
        SDL_RenderFlush(display->native.cpu.renderer)<0 ||
        SDL_RenderSetViewport(display->native.cpu.renderer,NULL)<0 ||
        SDL_RenderFlush(display->native.cpu.renderer)<0))
        return display_error(error,QA_ERROR_IO,"Completing the actual resized CPU presentation surface");
    return true;
}
bool qa_display_checkpoint(qa_display *display,qa_buffer *out,qa_error *error)
{
    if (display && display->surface_ticket)
        return display_save_error(error,QA_ERROR_ARGUMENT,"Display checkpoint retains a surface settings ticket");
    if (!out || out->data || out->size) return display_save_error(error,QA_ERROR_ARGUMENT,"Display checkpoint output must be empty");
    display_saved saved={0}; qa_source_save_io io={0};
    bool ok=display_observe(display,&saved,true,error);
    if (ok) {
        display->capturing=true;
        ok=qa_source_save_writer(&io,NULL,error) && display_save_fields(&io,&saved) && qa_source_save_finish(&io,out);
        display->capturing=false;
    }
    qa_source_save_dispose(&io); display_saved_free(&saved);
    if (!ok && (!error || error->code==QA_OK)) display_save_error(error,QA_ERROR_FORMAT,"Invalid genuine display continuation");
    return ok;
}
bool qa_display_create_detached(qa_display *active,qa_display **out,
    qa_display_restore_guard **guard_out,qa_error *error)
{
    if (!out || *out || !guard_out || *guard_out)
        return display_save_error(error,QA_ERROR_ARGUMENT,"Fresh detached display requires empty owner/guard destinations");
    qa_buffer current={0};
    bool ok=qa_display_checkpoint(active,&current,error) &&
        qa_display_restore((qa_bytes){current.data,current.size},active,out,guard_out,error);
    qa_buffer_free(&current); return ok;
}
bool qa_display_restore(qa_bytes bytes,const qa_display *active,qa_display **out,qa_display_restore_guard **guard_out,qa_error *error)
{
    if (!active || active->surface_ticket || active->pending_restore || active->destroy_pending || active->native_borrowed ||
        !active->lease || active->lease->references==SIZE_MAX || !out || *out || !guard_out || *guard_out)
        return display_save_error(error,QA_ERROR_ARGUMENT,"Display restore requires separate active and detached native owners");
    qa_display_restore_guard *guard=calloc(1,sizeof(*guard)); qa_display *candidate=calloc(1,sizeof(*candidate));
    if (!guard || !candidate) { free(guard); free(candidate); return display_save_error(error,QA_ERROR_MEMORY,"Allocating detached display continuation"); }
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && display_save_fields(&io,&guard->saved) && qa_source_save_finish(&io,NULL) &&
        display_observe((qa_display *)active,&guard->baseline,true,error) &&
        guard->saved.info.backend==guard->baseline.info.backend;
    if (ok) {
        candidate->backend=active->backend; candidate->window=active->window; candidate->native_borrowed=true;
        candidate->revision=1;
        candidate->lease=active->lease; ++candidate->lease->references;
        memcpy(candidate->fullscreen_failure,guard->saved.fullscreen_failure,sizeof(candidate->fullscreen_failure));
        if (active->backend==QA_DISPLAY_CPU) candidate->native.cpu.renderer=active->native.cpu.renderer;
        else candidate->native.gl=active->native.gl;
        guard->active=(qa_display *)active; guard->candidate=candidate; guard->window=active->window;
        candidate->restore_guard=guard;
        if (active->backend==QA_DISPLAY_CPU) guard->renderer=active->native.cpu.renderer;
        else guard->context=active->native.gl.context;
        *out=candidate; *guard_out=guard; candidate=NULL; guard=NULL;
    }
    qa_source_save_dispose(&io); qa_display_destroy(candidate);
    qa_display_restore_guard_destroy(guard);
    if (!ok && (!error || error->code==QA_OK)) display_save_error(error,QA_ERROR_FORMAT,"Saved display differs from its current qualified native window");
    return ok;
}
static bool display_guard_owned(const qa_display_restore_guard *guard,bool retiring,qa_error *error)
{
    if (!guard || guard->transferred || !guard->active || !guard->candidate || guard->active==guard->candidate ||
        (!retiring && (guard->active->destroy_pending || guard->candidate->destroy_pending)) ||
        guard->active->native_borrowed || !guard->candidate->native_borrowed || guard->active->capturing || guard->candidate->capturing ||
        guard->active->window!=guard->window || guard->candidate->window!=guard->window ||
        !guard->active->lease || guard->active->lease!=guard->candidate->lease ||
        guard->active->backend!=guard->candidate->backend ||
        (guard->active->backend==QA_DISPLAY_CPU ? guard->active->native.cpu.renderer!=guard->renderer ||
            guard->candidate->native.cpu.renderer!=guard->renderer : guard->active->native.gl.context!=guard->context ||
            guard->candidate->native.gl.context!=guard->context))
        return display_save_error(error,QA_ERROR_ARGUMENT,"Display handoff lost its genuine native window/context owner");
    return true;
}
static bool display_guard_ready(const qa_display_restore_guard *guard,qa_error *error)
{
    if (!display_guard_owned(guard,false,error)) return false;
    display_saved current={0};
    const display_saved *expected=guard->prepared?&guard->staged:&guard->baseline;
    bool ok=display_observe(guard->active,&current,false,error) && display_current_equal(expected,&current);
    display_saved_free(&current);
    if (!ok && (!error || error->code==QA_OK)) display_save_error(error,QA_ERROR_ARGUMENT,"Display native state changed after saved-cut qualification");
    return ok;
}
bool qa_display_handoff_prepare(qa_display_restore_guard *guard,qa_error *error)
{
    if (!display_guard_ready(guard,error)) return false;
    if (guard->prepared) return true;
    if (guard->attempted)
        return display_save_error(error,QA_ERROR_ARGUMENT,"Failed display preparation requires checked native rollback");
    if (guard->candidate->restore_guard && guard->candidate->restore_guard!=guard)
        return display_save_error(error,QA_ERROR_ARGUMENT,"Display candidate belongs to another native restore guard");
    guard->candidate->restore_guard=guard;
    guard->attempted=true;
    display_changed(guard->active);
    qa_display_settings settings={.width=guard->saved.info.logical_width,.height=guard->saved.info.logical_height,
        .fullscreen=guard->saved.info.fullscreen,.swap_interval=guard->saved.swap_interval};
    if (!window_settings_stage(guard->candidate,&settings,guard->baseline.info.visible,
            guard->baseline.info.focused,error) ||
        !display_observe(guard->candidate,&guard->staged,false,error)) return false;
    if (guard->staged.info.backend!=guard->saved.info.backend ||
        guard->staged.info.fullscreen!=guard->saved.info.fullscreen ||
        guard->staged.info.drawable_width!=guard->saved.info.drawable_width ||
        guard->staged.info.drawable_height!=guard->saved.info.drawable_height)
        return display_save_error(error,QA_ERROR_UNSUPPORTED,"Saved renderer extent differs from its actual staged native drawable");
    if (guard->candidate->backend==QA_DISPLAY_CPU && guard->saved.texture) {
        if (!resize_cpu_texture(guard->candidate,guard->saved.texture_width,guard->saved.texture_height,error)) return false;
        if (guard->saved.frame && (SDL_UpdateTexture(guard->candidate->native.cpu.texture,NULL,guard->saved.pixels.data,
                (int)(guard->saved.texture_width*4))<0 ||
            SDL_RenderCopy(guard->renderer,guard->candidate->native.cpu.texture,NULL,NULL)<0))
            return display_error(error,QA_ERROR_IO,"Preparing saved display presentation texture");
        guard->candidate->native.cpu.has_frame=guard->saved.frame;
    }
    guard->prepared=true; return true;
}
bool qa_display_handoff_abort(qa_display_restore_guard *guard,qa_error *error)
{
    if (!guard || guard->transferred) return true;
    if (!guard->attempted) {
        if (guard->candidate && guard->candidate->restore_guard==guard) guard->candidate->restore_guard=NULL;
        return true;
    }
    if (!display_guard_owned(guard,true,error)) return false;
    qa_display_settings settings={.width=guard->baseline.info.logical_width,.height=guard->baseline.info.logical_height,
        .fullscreen=guard->baseline.info.fullscreen,.swap_interval=guard->baseline.swap_interval};
    if (!window_settings_stage(guard->active,&settings,guard->baseline.info.visible,
            guard->baseline.info.focused,error)) return false;
    SDL_SetWindowPosition(guard->window,guard->baseline.x,guard->baseline.y);
    if (guard->baseline.info.minimized) SDL_MinimizeWindow(guard->window);
    else if (guard->baseline.info.maximized) SDL_MaximizeWindow(guard->window);
    else SDL_RestoreWindow(guard->window);
    display_saved current={0};
    bool ok=display_observe(guard->active,&current,false,error) && display_current_equal(&guard->baseline,&current);
    display_saved_free(&current);
    if (!ok) return display_save_error(error,QA_ERROR_IO,"The original native display has not recovered; retain its restore guard");
    if (guard->active->backend==QA_DISPLAY_CPU && guard->baseline.frame) {
        if (!guard->active->native.cpu.texture ||
            guard->active->native.cpu.width!=guard->baseline.texture_width ||
            guard->active->native.cpu.height!=guard->baseline.texture_height ||
            SDL_UpdateTexture(guard->active->native.cpu.texture,NULL,guard->baseline.pixels.data,
                (int)(guard->baseline.texture_width*4))<0 ||
            SDL_RenderCopy(guard->renderer,guard->active->native.cpu.texture,NULL,NULL)<0)
            return display_error(error,QA_ERROR_IO,"Restoring original display presentation texture");
        SDL_RenderPresent(guard->renderer);
    }
    display_changed(guard->active); display_changed(guard->candidate);
    memcpy(guard->active->fullscreen_failure,guard->baseline.fullscreen_failure,sizeof(guard->baseline.fullscreen_failure));
    display_saved_free(&guard->staged); guard->staged=(display_saved){0};
    guard->attempted=guard->prepared=false;
    if (guard->candidate->restore_guard==guard) guard->candidate->restore_guard=NULL;
    return true;
}
bool qa_display_handoff_ready(const qa_display_restore_guard *guard,qa_error *error)
{
    if (!display_guard_ready(guard,error)) return false;
    if (!guard->prepared) return display_save_error(error,QA_ERROR_ARGUMENT,"Display publication requires native presentation preparation");
    return true;
}
bool qa_display_restore_checkpoint(const qa_display_restore_guard *guard,qa_buffer *out,qa_error *error)
{
    if (!out || out->data || out->size || !display_guard_ready(guard,error)) return false;
    display_saved saved=guard->saved; saved.pixels=(qa_buffer){0}; qa_source_save_io io={0};
    if (guard->prepared && saved.frame) {
        if (!qa_display_capture_cpu(guard->candidate,&saved.pixels,NULL,NULL,error)) return false;
    } else saved.pixels=guard->saved.pixels;
    bool ok=qa_source_save_writer(&io,NULL,error) && display_save_fields(&io,&saved) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (guard->prepared && saved.frame) qa_buffer_free(&saved.pixels);
    return ok;
}
void qa_display_handoff(qa_display_restore_guard *guard)
{
    display_changed(guard->active); display_changed(guard->candidate);
    if (guard->active->gamma) {
        qa_display_gamma *gamma = guard->active->gamma;
        gamma->display = guard->candidate;
        guard->candidate->gamma = gamma;
        guard->active->gamma = NULL;
    }
    guard->candidate->native_borrowed=false; guard->active->native_borrowed=true;
    guard->candidate->restore_guard=NULL;
    if (guard->candidate->backend==QA_DISPLAY_CPU && guard->saved.frame) SDL_RenderPresent(guard->renderer);
    guard->transferred=true;
}
void qa_display_restore_guard_destroy(qa_display_restore_guard *guard)
{
    if (!guard) return;
    if (!qa_display_handoff_abort(guard,NULL)) {
        guard->active->pending_restore=guard; guard->candidate->pending_restore=guard;
        return;
    }
    if (guard->active && guard->active->pending_restore==guard) guard->active->pending_restore=NULL;
    if (guard->candidate && guard->candidate->pending_restore==guard) guard->candidate->pending_restore=NULL;
    if (guard->candidate && guard->candidate->restore_guard==guard) guard->candidate->restore_guard=NULL;
    display_saved_free(&guard->saved); display_saved_free(&guard->baseline); display_saved_free(&guard->staged); free(guard);
}
bool qa_display_restore_cleanup(qa_display *display,qa_error *error)
{
    qa_display_restore_guard *guard=display?display->pending_restore:NULL;
    if (!guard) return true;
    if (!qa_display_handoff_abort(guard,error)) return false;
    qa_display_restore_guard_destroy(guard); return true;
}

static bool surface_owned(const qa_display_surface_ticket *ticket)
{
    return ticket && ticket->active && ticket->active_lease &&
        ticket->active->surface_ticket == ticket &&
        (!ticket->candidate || ticket->candidate->surface_ticket == ticket) &&
        ticket->active->lease == ticket->active_lease &&
        ticket->active_lease->references == 1 && !ticket->active->native_borrowed &&
        !ticket->active->capturing && ticket->active->window == ticket->active_lease->window &&
        (ticket->active->backend != QA_DISPLAY_OPENGL ||
         (ticket->context == ticket->active->native.gl.context &&
          ticket->context == ticket->active_lease->context));
}
static bool surface_owner(const qa_display_surface_ticket *ticket, qa_error *error)
{
    if (!surface_owned(ticket))
        return display_save_error(error, QA_ERROR_ARGUMENT,
            "Surface settings require the retained exclusive native display owner");
    return true;
}
static bool display_endpoint_owned(const qa_display *display)
{
    if (!display || display->destroy_pending || display->capturing || !display->revision ||
        !display->lease || !display->window ||
        display->lease->window != display->window || display->lease->backend != display->backend)
        return false;
    const qa_display_restore_guard *guard=display->restore_guard;
    bool restored=guard && guard->candidate==display && guard->prepared && !guard->transferred &&
        !display->pending_restore && guard->active && !guard->active->pending_restore &&
        display->lease->references>=2 && display_guard_owned(guard,false,NULL);
    if ((guard && !restored) || (!restored && display->lease->references!=1)) return false;
    if (display->surface_ticket && !surface_owned(display->surface_ticket)) return false;
    if (display->backend == QA_DISPLAY_CPU)
        return (!display->native_borrowed || restored) && display->native.cpu.renderer && display->native.cpu.texture &&
            display->native.cpu.renderer == display->lease->renderer &&
            display->native.cpu.width && display->native.cpu.height &&
            (!restored || (display->native.cpu.width==guard->staged.info.drawable_width &&
                display->native.cpu.height==guard->staged.info.drawable_height));
    if (display->backend != QA_DISPLAY_OPENGL || !display->native.gl.context) return false;
    if (!display->native_borrowed || restored)
        return display->native.gl.context == display->lease->context &&
            display->native.gl.library_loaded == display->lease->library_loaded;
    const qa_display_surface_ticket *ticket = display->surface_ticket;
    return ticket && ticket->candidate == display && !display->lease->context &&
        !display->lease->library_loaded && !display->native.gl.library_loaded &&
        display->native.gl.context == ticket->context;
}
bool qa_display_endpoint_read(const qa_display *display, qa_display_endpoint *out)
{
    if (!out || !display_endpoint_owned(display)) return false;
    *out = (qa_display_endpoint){
        .owner = display, .lease = display->lease, .window = display->window,
        .context = display->backend == QA_DISPLAY_OPENGL ? display->native.gl.context : NULL,
        .renderer = display->backend == QA_DISPLAY_CPU ? display->native.cpu.renderer : NULL,
        .texture = display->backend == QA_DISPLAY_CPU ? display->native.cpu.texture : NULL,
        .dispatch = &native_revision, .revision = display->revision,
        .native_revision = native_revision, .backend = display->backend};
    return true;
}
bool qa_display_endpoint_is(const qa_display *display, const qa_display_endpoint *saved)
{
    return saved && display_endpoint_owned(display) && saved->owner == display &&
        saved->lease == display->lease && saved->window == display->window &&
        saved->revision == display->revision && saved->dispatch == &native_revision &&
        saved->native_revision == native_revision && saved->backend == display->backend &&
        (display->backend == QA_DISPLAY_OPENGL ? saved->context == display->native.gl.context &&
            !saved->renderer && !saved->texture : !saved->context &&
            saved->renderer == display->native.cpu.renderer && saved->texture == display->native.cpu.texture);
}
bool qa_display_surface_ready_is(const qa_display_surface_ticket *ticket,
    const qa_display *active, const qa_display *candidate)
{
    return surface_owned(ticket) && active && candidate && active != candidate &&
        ticket->active == active && ticket->candidate == candidate && ticket->entered &&
        ticket->staged_valid && !ticket->native_restored && active->ready_surface == ticket &&
        candidate->ready_surface == ticket && active->backend == candidate->backend &&
        qa_display_endpoint_is(active, &active->ready_endpoint) &&
        qa_display_endpoint_is(candidate, &candidate->ready_endpoint) &&
        (candidate->backend != QA_DISPLAY_CPU ||
            (candidate->native.cpu.width == ticket->staged.drawable_width &&
             candidate->native.cpu.height == ticket->staged.drawable_height));
}

static bool surface_info_equal(const qa_display_info *a, const qa_display_info *b)
{
    return a->window_id == b->window_id && a->backend == b->backend &&
        a->logical_width == b->logical_width && a->logical_height == b->logical_height &&
        a->drawable_width == b->drawable_width && a->drawable_height == b->drawable_height &&
        a->display_index == b->display_index && a->refresh_rate == b->refresh_rate &&
        a->fullscreen == b->fullscreen && a->visible == b->visible &&
        a->focused == b->focused && a->minimized == b->minimized && a->maximized == b->maximized;
}

static bool surface_restore_context(const qa_display_surface_ticket *ticket, qa_error *error)
{
    if (ticket->active->backend != QA_DISPLAY_OPENGL) return true;
    native_changed();
    if (SDL_GL_MakeCurrent(ticket->previous_window, ticket->previous_context) < 0)
        return display_error(error, QA_ERROR_IO, "Restoring the previous surface context");
    if (SDL_GL_GetCurrentWindow() != ticket->previous_window ||
        SDL_GL_GetCurrentContext() != ticket->previous_context)
        return display_save_error(error, QA_ERROR_IO,
            "SDL did not restore the previous surface context");
    return true;
}

static const SDL_GLattr surface_visual_attributes[11] = {
    SDL_GL_RED_SIZE, SDL_GL_GREEN_SIZE, SDL_GL_BLUE_SIZE, SDL_GL_ALPHA_SIZE,
    SDL_GL_BUFFER_SIZE, SDL_GL_DOUBLEBUFFER, SDL_GL_DEPTH_SIZE, SDL_GL_STENCIL_SIZE,
    SDL_GL_STEREO, SDL_GL_MULTISAMPLEBUFFERS, SDL_GL_MULTISAMPLESAMPLES
};

static bool surface_gl_query(int values[11], qa_error *error)
{
    typedef void (APIENTRY *get_integer_proc)(GLenum, GLint *);
    typedef void (APIENTRY *bind_framebuffer_proc)(GLenum, GLuint);
    typedef GLenum (APIENTRY *get_error_proc)(void);
    get_integer_proc get_integer = NULL;
    bind_framebuffer_proc bind_framebuffer = NULL;
    get_error_proc get_error = NULL;
    _Static_assert(sizeof(get_integer) == sizeof(void *) &&
        sizeof(bind_framebuffer) == sizeof(void *) && sizeof(get_error) == sizeof(void *),
        "SDL GL procedure pointers must fit in void pointers");
    void *address = SDL_GL_GetProcAddress("glGetIntegerv");
    memcpy(&get_integer, &address, sizeof(get_integer));
    address = SDL_GL_GetProcAddress("glBindFramebuffer");
    if (!address) address = SDL_GL_GetProcAddress("glBindFramebufferEXT");
    memcpy(&bind_framebuffer, &address, sizeof(bind_framebuffer));
    address = SDL_GL_GetProcAddress("glGetError");
    memcpy(&get_error, &address, sizeof(get_error));
    if (!get_integer || !bind_framebuffer || !get_error)
        return display_error(error, QA_ERROR_UNSUPPORTED, "Reading native GL framebuffer precision");
    if (get_error() != GL_NO_ERROR)
        return display_save_error(error, QA_ERROR_IO, "Active GL owner has an outstanding error");
    GLint draw = 0, read = 0;
    get_integer(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    get_integer(GL_READ_FRAMEBUFFER_BINDING, &read);
    if (get_error() != GL_NO_ERROR)
        return display_save_error(error, QA_ERROR_IO, "Reading original GL framebuffer bindings");
    bind_framebuffer(GL_FRAMEBUFFER, 0);
    bool ok = get_error() == GL_NO_ERROR;
    if (!ok) display_save_error(error, QA_ERROR_IO, "Selecting native GL framebuffer for visual query");
    for (size_t i = 0; ok && i < 11; ++i)
        if (SDL_GL_GetAttribute(surface_visual_attributes[i], values + i) < 0) {
            display_error(error, QA_ERROR_UNSUPPORTED, "Reading the active compatible-window GL visual");
            ok = false;
        }
    bind_framebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
    bind_framebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    if (get_error() != GL_NO_ERROR)
        return display_save_error(error, QA_ERROR_IO, "Restoring original GL framebuffer bindings");
    return ok;
}

static bool surface_gl_visual(qa_display_surface_ticket *ticket, qa_error *error)
{
    if (!surface_gl_query(ticket->visual, error)) return false;
    for (size_t i = 0; i < 11; ++i)
        if (!set_gl_attribute(surface_visual_attributes[i], ticket->visual[i], error)) return false;
    return true;
}

static bool surface_gl_visual_equal(const qa_display_surface_ticket *ticket, qa_error *error)
{
    int actual[11];
    if (!surface_gl_query(actual, error)) return false;
    for (size_t i = 0; i < 11; ++i) {
        if (actual[i] != ticket->visual[i])
            return display_save_error(error, QA_ERROR_UNSUPPORTED,
                "The candidate window does not retain the active GL visual");
    }
    return true;
}

bool qa_display_surface_prepare(qa_display *active, const qa_display_settings *settings,
                                qa_display_surface_ticket **out, qa_error *error)
{
    if (!out || *out || !active || active->surface_ticket || active->destroy_pending || !settings ||
        (unsigned)settings->fullscreen > QA_DISPLAY_DESKTOP ||
        settings->swap_interval < -1 || settings->swap_interval > 1 ||
        !valid_dimensions(settings->width, settings->height, error))
        return display_save_error(error, QA_ERROR_ARGUMENT, "Invalid shared display settings ticket");
    qa_display_surface_ticket *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return display_save_error(error, QA_ERROR_MEMORY, "Allocating surface settings ticket");
    ticket->active = active;
    display_changed(active);
    active->surface_ticket = ticket;
    ticket->active_lease = active->lease;
    ticket->settings = *settings;
    if (active->backend == QA_DISPLAY_OPENGL) {
        ticket->context = active->native.gl.context;
        ticket->previous_window = SDL_GL_GetCurrentWindow();
        ticket->previous_context = SDL_GL_GetCurrentContext();
    }
    if (!surface_owner(ticket, error) || !qa_display_info_get(active, &ticket->original, error)) {
        active->surface_ticket = NULL; free(ticket); return false;
    }
    if (active->backend == QA_DISPLAY_CPU && active->native.cpu.has_frame &&
        (active->native.cpu.width != ticket->original.drawable_width ||
         active->native.cpu.height != ticket->original.drawable_height)) {
        active->surface_ticket = NULL; free(ticket); return display_save_error(error, QA_ERROR_ARGUMENT,
            "CPU native presentation does not match the completed drawable");
    }
    SDL_GetWindowPosition(active->window, &ticket->x, &ticket->y);
    if (SDL_GetWindowDisplayMode(active->window, &ticket->original_mode) < 0) {
        active->surface_ticket = NULL; free(ticket); return display_error(error, QA_ERROR_IO, "Reading prior native window display mode");
    }
    SDL_Window *focus = SDL_GetKeyboardFocus();
    ticket->previous_focus = focus ? SDL_GetWindowID(focus) : 0;
    *out = ticket;
    if (active->backend == QA_DISPLAY_OPENGL) {
        if (!qa_display_make_current(active, error)) return false;
        ticket->original_interval = SDL_GL_GetSwapInterval();
        ticket->interval_known = true;
        if (!surface_gl_visual(ticket, error)) return false;
    }
    qa_display *candidate = calloc(1, sizeof(*candidate));
    display_native_lease *lease = calloc(1, sizeof(*lease));
    if (!candidate || !lease) {
        free(candidate); free(lease);
        return display_save_error(error, QA_ERROR_MEMORY, "Allocating candidate surface ownership");
    }
    candidate->backend = active->backend;
    candidate->revision = 1; candidate->surface_ticket = ticket;
    candidate->lease = lease;
    lease->references = 1; lease->backend = active->backend;
    ticket->candidate = candidate;
    Uint32 retained_flags = SDL_GetWindowFlags(active->window) &
        (SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_BORDERLESS |
         SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SKIP_TASKBAR | SDL_WINDOW_UTILITY |
         SDL_WINDOW_TOOLTIP | SDL_WINDOW_POPUP_MENU);
    if (active->backend == QA_DISPLAY_OPENGL) retained_flags |= SDL_WINDOW_OPENGL;
    candidate->window = SDL_CreateWindow(SDL_GetWindowTitle(active->window), ticket->x, ticket->y,
        (int)settings->width, (int)settings->height, retained_flags | SDL_WINDOW_HIDDEN);
    lease->window = candidate->window;
    if (!candidate->window) return display_error(error, QA_ERROR_IO, "Preparing candidate surface window");
    if (active->backend == QA_DISPLAY_OPENGL) {
        candidate->native.gl.context = ticket->context;
        candidate->native_borrowed = true;
        if (!qa_display_make_current(candidate, error) || !surface_gl_visual_equal(ticket, error) ||
            !surface_restore_context(ticket, error)) return false;
    } else {
        if (!cpu_surface_create(candidate, error)) return false;
        lease->renderer = candidate->native.cpu.renderer;
    }
    qa_display_info current;
    if (!qa_display_info_get(active, &current, error) ||
        !surface_info_equal(&current, &ticket->original))
        return display_save_error(error, QA_ERROR_IO,
            "Preparing a candidate changed the active native surface");
    return true;
}

qa_display *qa_display_surface_candidate(const qa_display_surface_ticket *ticket)
{ return ticket ? ticket->candidate : NULL; }
const qa_display *qa_display_surface_active(const qa_display_surface_ticket *ticket)
{ return ticket ? ticket->active : NULL; }

bool qa_display_surface_stage(qa_display_surface_ticket *ticket, qa_error *error)
{
    if (!surface_owner(ticket, error) || !ticket->candidate || ticket->entered || ticket->native_restored ||
        ticket->active->destroy_pending || ticket->candidate->destroy_pending)
        return display_save_error(error, QA_ERROR_ARGUMENT, "Surface settings have no unstaged candidate");
    qa_display_info active_info;
    int x, y;
    SDL_GetWindowPosition(ticket->active->window, &x, &y);
    if (!qa_display_info_get(ticket->active, &active_info, error) ||
        !surface_info_equal(&active_info, &ticket->original) || x != ticket->x || y != ticket->y)
        return display_save_error(error, QA_ERROR_ARGUMENT,
            "The native display changed before candidate settings were entered");
    display_changed(ticket->active); display_changed(ticket->candidate);
    ticket->entered = true;
    qa_display *candidate = ticket->candidate;
    if (!window_settings_stage(candidate,&ticket->settings,ticket->original.visible,
            ticket->original.focused,error)) return false;
    if (candidate->backend == QA_DISPLAY_OPENGL) {
        if (!surface_gl_visual_equal(ticket, error)) return false;
    }
    if (!qa_display_info_get(candidate,&ticket->staged,error)) return false;
    if (ticket->staged.fullscreen != ticket->settings.fullscreen ||
        ticket->staged.visible != ticket->original.visible ||
        (ticket->original.focused && !ticket->staged.focused) ||
        (ticket->settings.fullscreen == QA_DISPLAY_WINDOWED &&
         (ticket->staged.logical_width != ticket->settings.width ||
          ticket->staged.logical_height != ticket->settings.height)))
        return display_save_error(error, QA_ERROR_IO,
            "SDL candidate native settings differ from the requested settings");
    if (candidate->backend == QA_DISPLAY_CPU &&
        !resize_cpu_texture(candidate, ticket->staged.drawable_width,
                            ticket->staged.drawable_height, error)) return false;
    ticket->staged_valid = true;
    return qa_display_surface_ready(ticket, error);
}

bool qa_display_surface_ready(const qa_display_surface_ticket *ticket, qa_error *error)
{
    if (ticket && ticket->active) ticket->active->ready_surface = NULL;
    if (ticket && ticket->candidate) ticket->candidate->ready_surface = NULL;
    if (!surface_owner(ticket, error) || !ticket->candidate || !ticket->entered ||
        !ticket->staged_valid || ticket->native_restored || ticket->active->destroy_pending ||
        ticket->candidate->destroy_pending)
        return display_save_error(error, QA_ERROR_ARGUMENT, "Surface settings are not natively staged");
    const qa_display *candidate = ticket->candidate;
    qa_display_info current;
    if (!qa_display_info_get(candidate, &current, error) ||
        !surface_info_equal(&current, &ticket->staged))
        return display_save_error(error, QA_ERROR_IO, "Candidate native surface changed before publication");
    if (candidate->backend == QA_DISPLAY_OPENGL) {
        if (candidate->native.gl.context != ticket->context ||
            candidate->lease->context != NULL || SDL_GL_GetCurrentWindow() != candidate->window ||
            SDL_GL_GetCurrentContext() != ticket->context ||
            SDL_GL_GetSwapInterval() != ticket->settings.swap_interval)
            return display_save_error(error, QA_ERROR_ARGUMENT,
                "Candidate surface is not bound to the prepared active GL context");
    } else if (candidate->native.cpu.width != current.drawable_width ||
               candidate->native.cpu.height != current.drawable_height ||
               !candidate->native.cpu.texture)
        return display_save_error(error, QA_ERROR_ARGUMENT, "Candidate CPU texture is not prepared");
    qa_display_endpoint active, next;
    if (!qa_display_endpoint_read(ticket->active, &active) ||
        !qa_display_endpoint_read(candidate, &next))
        return display_save_error(error, QA_ERROR_ARGUMENT,"Surface readiness lost its retained native endpoints");
    ticket->active->ready_endpoint = active; ticket->candidate->ready_endpoint = next;
    ticket->active->ready_surface = ticket->candidate->ready_surface = ticket;
    return true;
}

bool qa_display_surface_rollback(qa_display_surface_ticket *ticket, qa_error *error)
{
    if (!surface_owner(ticket, error)) return false;
    display_changed(ticket->active);
    if (ticket->candidate) display_changed(ticket->candidate);
    if (!ticket->native_restored && ticket->candidate && ticket->candidate->window) {
        if (!qa_display_set_fullscreen(ticket->candidate, QA_DISPLAY_WINDOWED, error)) return false;
        SDL_HideWindow(ticket->candidate->window);
    }
    if (!ticket->native_restored && ticket->entered) {
        qa_display_info actual;
        if (!qa_display_info_get(ticket->active, &actual, error)) return false;
        if (actual.fullscreen != ticket->original.fullscreen) {
            if (SDL_SetWindowDisplayMode(ticket->active->window, &ticket->original_mode) < 0 ||
                !qa_display_set_fullscreen(ticket->active, ticket->original.fullscreen, error))
                return display_error(error, QA_ERROR_IO, "Restoring prior native fullscreen mode");
        }
        if (ticket->original.visible) SDL_ShowWindow(ticket->active->window);
        else SDL_HideWindow(ticket->active->window);
        if (ticket->original.minimized) SDL_MinimizeWindow(ticket->active->window);
        else if (ticket->original.maximized) SDL_MaximizeWindow(ticket->active->window);
        else SDL_RestoreWindow(ticket->active->window);
        SDL_Window *focus = ticket->previous_focus ? SDL_GetWindowFromID(ticket->previous_focus) : NULL;
        if (focus && SDL_SetWindowInputFocus(focus) < 0)
            return display_error(error, QA_ERROR_IO, "Restoring prior native surface focus");
    }
    if (!ticket->native_restored && ticket->active->backend == QA_DISPLAY_OPENGL) {
        if (ticket->interval_known &&
            (!qa_display_set_swap_interval(ticket->active, ticket->original_interval, error) ||
             SDL_GL_GetSwapInterval() != ticket->original_interval)) return false;
        if (!surface_restore_context(ticket, error)) return false;
    }
    qa_display_info current;
    int x, y;
    SDL_GetWindowPosition(ticket->active->window, &x, &y);
    if (!qa_display_info_get(ticket->active, &current, error) || x != ticket->x || y != ticket->y ||
        !surface_info_equal(&current, &ticket->original))
        return display_save_error(error, QA_ERROR_IO,
            "The prior native surface has not recovered; retain the surface ticket");
    if (ticket->active->backend == QA_DISPLAY_OPENGL &&
        (SDL_GL_GetCurrentWindow() != ticket->previous_window ||
         SDL_GL_GetCurrentContext() != ticket->previous_context) &&
        !surface_restore_context(ticket, error)) return false;
    if (!ticket->native_restored && ticket->active->backend == QA_DISPLAY_CPU &&
        ticket->active->native.cpu.has_frame) {
        if (SDL_RenderCopy(ticket->active->native.cpu.renderer,
                            ticket->active->native.cpu.texture, NULL, NULL) < 0)
            return display_error(error, QA_ERROR_IO, "Restoring original native CPU presentation");
        SDL_RenderPresent(ticket->active->native.cpu.renderer);
    }
    ticket->native_restored = true;
    ticket->staged_valid = false;
    return true;
}

bool qa_display_surface_abort(qa_display_surface_ticket **out, qa_error *error)
{
    if (!out) return display_save_error(error, QA_ERROR_ARGUMENT, "Invalid surface settings abort");
    qa_display_surface_ticket *ticket = *out;
    if (!ticket) return true;
    if (!qa_display_surface_rollback(ticket, error)) return false;
    display_changed(ticket->active);
    ticket->active->surface_ticket = NULL;
    if (ticket->candidate) ticket->candidate->surface_ticket = NULL;
    qa_display_destroy(ticket->candidate);
    free(ticket); *out = NULL;
    return true;
}

void qa_display_surface_publish(qa_display_surface_ticket **out,
                                qa_display **active, qa_display **retired)
{
    if (!out || !*out || !active || !retired || *retired || *active != (*out)->active ||
        !qa_display_surface_ready_is(*out, *active, (*out)->candidate)) return;
    qa_display_surface_ticket *ticket = *out;
    qa_display *candidate = ticket->candidate;
    display_changed(ticket->active); display_changed(candidate);
    if (candidate->backend == QA_DISPLAY_OPENGL) {
        candidate->lease->context = ticket->context;
        candidate->lease->library_loaded = ticket->active_lease->library_loaded;
        candidate->native.gl.library_loaded = candidate->lease->library_loaded;
        candidate->native_borrowed = false;
        ticket->active_lease->context = NULL;
        ticket->active_lease->library_loaded = false;
        ticket->active->native.gl.context = NULL;
        ticket->active->native.gl.library_loaded = false;
    }
    ticket->active->surface_ticket = candidate->surface_ticket = NULL;
    if (ticket->active->gamma) {
        qa_display_gamma *gamma = ticket->active->gamma;
        gamma->display = candidate; gamma->lease = candidate->lease; gamma->window = candidate->window;
        if (!gamma->ticket || gamma->ticket->target != candidate) gamma->applied = false;
        candidate->gamma = gamma; ticket->active->gamma = NULL;
    }
    *retired = ticket->active; *active = candidate;
    free(ticket); *out = NULL;
}
