#include "internal.h"
#include "qa/json_writer.h"
#include "qa/text.h"
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

struct qa_frontend_tools {
    qa_frontend *frontend;
    qa_http *http;
    qa_llm *llm;
    qa_tools *owner;
    qa_resource_pool *private_resources;
    qa_vfs *settings, *files;
    qa_mount_id private_mount, output_mount;
    char *output_root;
    uint64_t configuration, map_revision;
    float debug_width;
};
static double milliseconds(void *context) { qa_frontend *f = context; return (double)f->time_ns / 1000000.0; }
static double profiler_milliseconds(void *context) {
    (void)context; uint64_t frequency = SDL_GetPerformanceFrequency();
    return frequency ? (double)SDL_GetPerformanceCounter() * 1000 / (double)frequency : NAN;
}
static double wall_milliseconds(void *context) {
    (void)context; struct timespec now;
    if (timespec_get(&now, TIME_UTC) != TIME_UTC) return NAN;
    return (double)now.tv_sec * 1000 + (double)now.tv_nsec / 1000000;
}
static bool open_browser(void *context, const char *url, qa_error *error) {
    (void)context;
    if (SDL_OpenURL(url) == 0) return true;
    return frontend_fail(error, QA_ERROR_IO, "could not launch subscription sign-in browser");
}
static bool context_active(void *context, const qa_command_context *source) {
    qa_frontend *f = context;
    if (!f->application || !source) return false;
    qa_command_context current; qa_error error = {0};
    if (!source->registry && !source->generation) {
        if (!qa_application_capture_command_context(f->application, source, &current, &error)) return false;
        source = &current;
    }
    if (!qa_application_command_context_active(f->application, source)) return false;
    if (source->origin == QA_COMMAND_SEAT) return !f->options.dedicated && source->seat < f->options.seats && f->seats[source->seat].console;
    return source->origin == QA_COMMAND_LOCAL || source->origin == QA_COMMAND_SERVER || source->origin == QA_COMMAND_REMOTE;
}
static bool capture_context(void *context, const qa_command_context *source, qa_command_context *out, qa_error *error) {
    qa_frontend *f = context; return qa_application_capture_command_context(f->application, source, out, error);
}
static bool render_context_active(void *context, const qa_command_context *source) {
    qa_frontend *f = context; return f->display && context_active(context, source);
}
static void source_print(void *context, const qa_command_context *source, const char *text) {
    qa_frontend *f = context;
    if (!context_active(context, source)) return;
    if (source->origin == QA_COMMAND_SEAT) {
        qa_error error = {0};
        if (!qa_seat_console_print(f->seats[source->seat].console, text, &error)) fprintf(stderr, "console output: %s\n", error.message);
    } else fputs(text, stdout);
}
static const char *map_name(void *context) {
    qa_frontend *f = context; qa_application_map_view map;
    return qa_application_map_read(f->application, &map) ? map.name : NULL;
}
static bool read_frame(void *context, qa_image *out, qa_error *error) {
    qa_frontend *f = context; qa_image image = {.srgb_intent = -1}; bool ok;
    if (f->gl) ok = qa_gl_capture_presented(f->gl, &image.rgba, &image.width, &image.height, error);
    else if (f->cpu && f->display) ok = qa_display_capture_cpu(f->display, &image.rgba, &image.width, &image.height, error);
    else return frontend_fail(error, QA_ERROR_UNSUPPORTED, "capture requires an active drawable");
    if (ok) *out = image; else qa_image_free(&image); return ok;
}
static bool forward(void *context, const qa_command_invocation *call, qa_error *error) {
    qa_frontend *f = context; return qa_application_source_command(f->application, call, error);
}
static bool files_for_context(void *context, const qa_command_context *source, qa_vfs **out, qa_mount_id *mount, qa_error *error) {
    qa_frontend *f = context;
    if (!context_active(context, source)) return frontend_fail(error, QA_ERROR_ARGUMENT, "source resource context has retired");
    if (source->owner) {
        qa_command_context current;
        if (!qa_application_capture_command_context(f->application, source, &current, error)) return false;
        *out = qa_application_context_files(f->application, &current, mount);
        return *out != NULL || frontend_fail(error, QA_ERROR_NOT_FOUND, "source resource owner is unavailable");
    }
    *out = f->tools->files; *mount = f->tools->output_mount; return true;
}
typedef struct diagnostic_text { qa_buffer bytes; size_t capacity; } diagnostic_text;
static bool append(diagnostic_text *text, qa_error *error, const char *format, ...) {
    va_list args, count_args; va_start(args, format); va_copy(count_args, args);
    int length = vsnprintf(NULL, 0, format, count_args); va_end(count_args);
    if (length < 0 || (size_t)length > SIZE_MAX - text->bytes.size - 1) { va_end(args); return frontend_fail(error, QA_ERROR_MEMORY, "diagnostic text exceeds native range"); }
    size_t needed = text->bytes.size + (size_t)length + 1;
    if (needed > text->capacity) {
        size_t capacity = text->capacity ? text->capacity : 256;
        while (capacity < needed) { if (capacity > SIZE_MAX / 2) { capacity = needed; break; } capacity *= 2; }
        void *copy = realloc(text->bytes.data, capacity);
        if (!copy) { va_end(args); return frontend_fail(error, QA_ERROR_MEMORY, "allocating diagnostic text"); }
        text->bytes.data = copy; text->capacity = capacity;
    }
    (void)vsnprintf((char *)text->bytes.data + text->bytes.size, text->capacity - text->bytes.size, format, args); va_end(args);
    text->bytes.size += (size_t)length; return true;
}
static bool path_report(qa_vfs *files, diagnostic_text *text, qa_error *error) {
    if (!append(text, error, "mount kind writable referenced path\n")) return false;
    for (size_t i = 0; i < qa_vfs_mount_count(files); ++i) {
        qa_vfs_mount_info mount; if (!qa_vfs_mount_at(files, i, &mount)) return frontend_fail(error, QA_ERROR_ARGUMENT, "active mount order changed during observation");
        if (!append(text, error, "%" PRIu64 " %s %u %u %s\n", mount.id, mount.is_archive ? "archive" : "directory", (unsigned)mount.writable, (unsigned)mount.referenced, qa_vfs_mount_path(files, mount.id))) return false;
    }
    for (size_t i = 0; i < qa_vfs_prefix_count(files); ++i) {
        const char *prefix; const qa_mount_id *order; size_t count;
        if (!qa_vfs_prefix_at(files, i, &prefix, &order, &count)) return frontend_fail(error, QA_ERROR_ARGUMENT, "active prefix order changed during observation");
        if (!append(text, error, "%s:\n", prefix)) return false;
        for (size_t j = 0; j < count; ++j) if (!append(text, error, "  %" PRIu64 " %s\n", order[j], qa_vfs_mount_path(files, order[j]))) return false;
    }
    return true;
}
static bool resource_report(qa_vfs *files, diagnostic_text *text, qa_error *error) {
    if (!append(text, error, "identity bytes readers sha256 path\n")) return false;
    size_t count = qa_vfs_resource_count(files);
    for (size_t i = 0; i < count; ++i) {
        size_t readers = 0; const qa_resource *resource = qa_vfs_resource_at(files, i, &readers);
        if (!resource) return frontend_fail(error, QA_ERROR_ARGUMENT, "resource inventory changed during observation");
        char digest[65]; qa_sha256_hex(qa_resource_digest(resource), digest);
        if (!append(text, error, "%" PRIu64 " %zu %zu %s %s\n", qa_resource_id(resource), qa_resource_bytes(resource).size, readers, digest, qa_resource_path(resource))) return false;
    }
    return append(text, error, "%zu cached opened resources\n", count);
}
static bool images_report(qa_frontend *f, diagnostic_text *text, qa_arena *scratch, qa_error *error) {
    const qa_scene_image *const *images = NULL; size_t count = 0;
    if (!append(text, error, "ordinal width height encoding mipLevels identity revision name\n")) return false;
    if (f->gl) {
        if (!qa_gl_resident_images(f->gl, scratch, &images, &count, error)) return false;
    } else if (f->cpu) {
        /* The CPU backend samples the retained scene images directly. It has
         * no private texture copy or separate GPU residency list. */
        size_t ordinal = 0;
        for (size_t family = 0; family < 4; ++family) {
            for (size_t index = 0;; ++index) {
                const qa_scene_resources *owner = NULL;
                if (family == 0 && index < 2) owner = index == 0 ? f->images : f->ui_images;
                else if (family == 1) owner = frontend_source_images_at(f, index);
                else if (family == 2) owner = frontend_native_q2_images_at(f, index);
                else if (family == 3) owner = frontend_visual_images_at(f, index);
                if (!owner) { if (family == 0 && index < 2) continue; break; }
                if (!qa_scene_resources_images(owner, scratch, &images, &count, error)) return false;
                for (size_t j = 0; j < count; ++j) {
                    const qa_scene_image *image = images[j];
                    if (!append(text, error, "%zu %u %u %s %zu %" PRIu64 " %" PRIu64 " %s\n", ordinal++, image->levels[0].width, image->levels[0].height,
                        image->kind == QA_SCENE_DEPTH32F ? "depth32f" : image->kind == QA_SCENE_RGB8 ? "rgb8" : "rgba8", image->level_count, image->identity, image->revision, image->name)) return false;
                }
            }
        }
        return append(text, error, "%zu resident CPU images\n", ordinal);
    } else return frontend_fail(error, QA_ERROR_UNSUPPORTED, "image inventory requires a renderer");
    for (size_t i = 0; i < count; ++i) {
        const qa_scene_image *image = images[i];
        if (!append(text, error, "%zu %u %u %s %zu %" PRIu64 " %" PRIu64 " %s\n", i, image->levels[0].width, image->levels[0].height,
            image->kind == QA_SCENE_DEPTH32F ? "depth32f" : image->kind == QA_SCENE_RGB8 ? "rgb8" : "rgba8", image->level_count, image->identity, image->revision, image->name)) return false;
    }
    return append(text, error, "%zu resident GPU images\n", count);
}
static bool shaders_report(qa_frontend *f, bool sorted, diagnostic_text *text, qa_arena *scratch, qa_error *error) {
    if (!f->order) return frontend_fail(error, QA_ERROR_UNSUPPORTED, "shader inventory requires a scene owner");
    const qa_material *const *materials; size_t count;
    if (!qa_material_order_snapshot(f->order, sorted, scratch, &materials, &count, error) || !append(text, error, "passes lightmap iterator sort registration name\n")) return false;
    for (size_t i = 0; i < count; ++i) {
        const qa_material *material = materials[i]; char sort[32]; size_t passes; qa_material_iterator iterator;
        static const char *const names[] = {"generic", "sky", "vertex-lit", "lightmapped"};
        if (!qa_material_diagnostic_plan(material, false, &passes, &iterator, error) || !qa_format_number(material->sort, sort, error) ||
            !append(text, error, "%zu %" PRId32 " %s %s %u %s\n", passes, material->lightmap_index, names[iterator], sort, material->registration, material->name)) return false;
    }
    return append(text, error, "%zu registered shaders\n", count);
}
static bool model_report(qa_frontend *f, const qa_command_context *source, bool skin, diagnostic_text *text, qa_arena *scratch, qa_error *error) {
    qa_q3_presentation_assets *assets = frontend_source_assets(f, source);
    if (!assets) return frontend_fail(error, QA_ERROR_ARGUMENT, "no active source client resource registry");
    size_t count;
    if (skin) {
        const qa_q3_registered_skin *rows;
        if (!qa_q3_registered_skins(assets, scratch, &rows, &count, error)) return false;
        for (size_t i = 0; i < count; ++i) {
            if (!append(text, error, "%" PRId32 " %s\n", rows[i].handle, rows[i].name)) return false;
            for (size_t j = 0; j < rows[i].surfaces->count; ++j) if (!append(text, error, "  %s = %s\n", rows[i].surfaces->mappings[j].surface, rows[i].surfaces->mappings[j].shader)) return false;
        }
    } else {
        const qa_q3_registered_model *rows; static const char *const formats[] = {"mdl", "md2", "md3", "md4", "md5", "spr", "sp2"};
        if (!qa_q3_registered_models(assets, scratch, &rows, &count, error) || !append(text, error, "handle kind name\n")) return false;
        for (size_t i = 0; i < count; ++i) {
            const char *kind = rows[i].world ? rows[i].inline_model ? "inline" : "world" : (unsigned)rows[i].format < sizeof formats / sizeof formats[0] ? formats[rows[i].format] : "unknown";
            if (!append(text, error, "%" PRId32 " %s %s\n", rows[i].handle, kind, rows[i].name)) return false;
        }
    }
    return append(text, error, "%zu registered %s\n", count, skin ? "skins" : "models");
}
static bool diagnostic(void *context, const qa_command_invocation *call, qa_buffer *out, qa_error *error) {
    qa_frontend *f = context; diagnostic_text text = {0}; qa_arena scratch = {0}; bool ok = false;
    const qa_console_entry *entry = qa_console_find(call->console, &call->context, call->argv[0]);
    if (!entry) return frontend_fail(error, QA_ERROR_ARGUMENT, "diagnostic command registration retired");
    const char *name = entry->name;
    qa_mount_id unused; qa_vfs *files;
    if (!files_for_context(context, &call->context, &files, &unused, error)) return false;
    if (!strcmp(name, "path")) ok = path_report(files, &text, error);
    else if (!strcmp(name, "resourceinfo")) ok = resource_report(files, &text, error);
    else if (!strcmp(name, "frameinfo")) {
        qa_application_map_view map; bool mapped = qa_application_map_read(f->application, &map);
        ok = append(&text, error, "frame=%" PRIu64 " milliseconds=%.3f map=%s renderer=%s clients=%zu configuration=%" PRIu64 "\n",
            f->frame_number, milliseconds(f), mapped ? map.name : "", f->gl ? "opengl" : f->cpu ? "cpu" : "dedicated", qa_application_player_count(f->application), qa_application_configuration_generation(f->application));
    } else if (!strcmp(name, "imagelist")) ok = images_report(f, &text, &scratch, error);
    else if (!strcmp(name, "shaderlist")) ok = shaders_report(f, call->argc > 1, &text, &scratch, error);
    else if (!strcmp(name, "modellist") || !strcmp(name, "skinlist")) ok = model_report(f, &call->context, !strcmp(name, "skinlist"), &text, &scratch, error);
    else if (!strcmp(name, "modelist")) {
        qa_display_info display;
        if (!f->display) frontend_fail(error, QA_ERROR_UNSUPPORTED, "display modes require an active video device");
        else if (qa_display_info_get(f->display, &display, error)) {
            int count = SDL_GetNumDisplayModes(display.display_index); ok = count >= 0;
            if (!ok) frontend_fail(error, QA_ERROR_IO, "could not enumerate active video device modes");
            for (int i = 0; ok && i < count; ++i) {
                SDL_DisplayMode mode;
                if (SDL_GetDisplayMode(display.display_index, i, &mode) != 0) ok = frontend_fail(error, QA_ERROR_IO, "could not read active video device mode");
                else ok = append(&text, error, "%d: %dx%d %u bit %d Hz\n", i, mode.w, mode.h, (unsigned)SDL_BITSPERPIXEL(mode.format), mode.refresh_rate);
            }
            if (ok) ok = append(&text, error, "%d display modes\n", count);
        }
    } else if (!strcmp(name, "gfxinfo")) {
        qa_display_info display;
        if (!f->display) frontend_fail(error, QA_ERROR_UNSUPPORTED, "graphics information requires an active drawable");
        else if (qa_display_info_get(f->display, &display, error)) {
            ok = append(&text, error, "backend: %s\ndrawable: %ux%u\n", f->gl ? "opengl" : "cpu", display.drawable_width, display.drawable_height);
            const qa_gl_capabilities *gl = qa_gl_capabilities_get(f->gl);
            if (ok && gl) ok = append(&text, error, "vendor: %s\nrenderer: %s\nversion: %s\nshadingLanguage: %s\n", gl->vendor, gl->renderer, gl->version, gl->shading_language);
            else if (ok) ok = append(&text, error, "driver: software renderer\n");
        }
    } else frontend_fail(error, QA_ERROR_ARGUMENT, "unknown application diagnostic");
    qa_arena_destroy(&scratch);
    if (ok) { *out = text.bytes; text.bytes = (qa_buffer){0}; }
    qa_buffer_free(&text.bytes); return ok;
}
static bool files_sync(qa_frontend *f, qa_error *error) {
    qa_frontend_tools *services = f->tools;
    qa_vfs *source = qa_launch_snapshot_mounts(qa_application_launch(f->application));
    qa_vfs *next = source ? qa_vfs_clone(source, error) : qa_vfs_create(qa_application_resources(f->application), error);
    if (!next) return false;
    qa_mount_id output;
    if (!qa_vfs_mount_directory(next, services->output_root, QA_ARCHIVE_EXACT, true, &output, error)) { qa_vfs_destroy(next); return false; }
    if (services->owner && !qa_tools_set_files(services->owner, next, output, error)) { qa_vfs_destroy(next); return false; }
    qa_vfs_destroy(services->files); services->files = next; services->output_mount = output;
    services->configuration = qa_application_configuration_generation(f->application);
    qa_application_map_view map; services->map_revision = qa_application_map_read(f->application, &map) ? map.revision : 0; return true;
}
static bool consoles_sync(qa_frontend *f, qa_error *error) {
    qa_frontend_tools *services = f->tools;
    size_t count = qa_application_console_count(f->application);
    for (size_t i = 0; i < count; ++i) {
        qa_console *console = qa_application_console_at(f->application, i, NULL);
        if (!console) return frontend_fail(error, QA_ERROR_ARGUMENT, "source console inventory changed during binding");
        if (!qa_tools_attach_console(services->owner, console, error) || !qa_llm_attach_console(services->llm, console, error)) return false;
    }
    return true;
}
bool frontend_tools_create(qa_frontend *f, qa_error *error) {
    if (!f || f->tools) return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid frontend tools admission");
    qa_frontend_tools *services = calloc(1, sizeof *services);
    if (!services) return frontend_fail(error, QA_ERROR_MEMORY, "allocating frontend tools services");
    services->frontend = f; f->tools = services;
    services->debug_width = 2;
    if (!qa_cvars_register(qa_application_cvars(f->application), "gl_debug_linewidth", "2", 0,
                           FRONTEND_OWNER, "Width in pixels for shared debug shapes.", error)) return false;
    char *base = SDL_GetBasePath();
    if (!base) return frontend_fail(error, QA_ERROR_IO, "could not locate executable private settings directory");
    services->private_resources = qa_resource_pool_create(error);
    services->settings = services->private_resources ? qa_vfs_create(services->private_resources, error) : NULL;
    bool mounted = services->settings && qa_vfs_mount_directory(services->settings, base, QA_ARCHIVE_EXACT, true, &services->private_mount, error); SDL_free(base);
    if (!mounted || !qa_http_create(&services->http, error)) return false;
    if (f->options.application.user_root) {
        size_t n = strlen(f->options.application.user_root); services->output_root = malloc(n + 1);
        if (services->output_root) memcpy(services->output_root, f->options.application.user_root, n + 1);
    } else {
        char *root = SDL_GetPrefPath("quake-anthology", "content");
        if (root) { size_t n = strlen(root); services->output_root = malloc(n + 1); if (services->output_root) memcpy(services->output_root, root, n + 1); SDL_free(root); }
    }
    if (!services->output_root) return frontend_fail(error, QA_ERROR_MEMORY, "retaining tool output directory");
    if (!files_sync(f, error)) return false;
    qa_tools_options options = {.files = services->files, .output_mount = services->output_mount,
        .owner = FRONTEND_OWNER, .context = f, .milliseconds = milliseconds, .profiler_milliseconds = profiler_milliseconds, .read_frame = read_frame,
        .context_active = render_context_active, .map_name = map_name, .print = source_print, .forward = forward, .diagnostic = diagnostic, .files_for_context = files_for_context, .capture_context = capture_context};
    if (!qa_tools_create(&options, &services->owner, error)) return false;
    qa_llm_options llm = {.http = services->http, .settings = services->settings, .private_mount = services->private_mount,
        .owner = FRONTEND_OWNER, .context = f, .wall_milliseconds = wall_milliseconds,
        .open_browser = open_browser, .context_active = context_active, .print = source_print, .capture_context = capture_context};
    if (!qa_llm_create(&llm, &services->llm, error)) return false;
    return consoles_sync(f, error);
}
bool frontend_tools_before_world_change(qa_frontend *f, qa_error *error) {
    if (!f || !f->tools) return true;
    qa_frontend_tools *services = f->tools;
    if (!qa_llm_before_world_change(services->llm, error)) return false;
    if (services->owner && !qa_tools_before_world_change(services->owner, error)) return false;
    return true;
}
bool frontend_tools_world_change_ready(qa_frontend *f, qa_error *error) {
    if (!f || !f->tools) return true;
    const qa_frontend_tools *services = f->tools;
    return (qa_tools_callbacks_idle(services->owner) && qa_llm_callbacks_idle(services->llm) &&
            qa_http_callbacks_idle(services->http)) || frontend_fail(error, QA_ERROR_ARGUMENT, "tool callbacks must return before world retirement");
}
bool frontend_tools_sync(qa_frontend *f, qa_error *error) {
    if (!f || !f->tools) return true;
    qa_frontend_tools *services = f->tools;
    qa_application_map_view map; uint64_t revision = qa_application_map_read(f->application, &map) ? map.revision : 0;
    if (services->configuration != qa_application_configuration_generation(f->application) || services->map_revision != revision) {
        if (!frontend_tools_before_world_change(f, error) || !files_sync(f, error)) return false;
    }
    return consoles_sync(f, error);
}
bool frontend_tools_pump(qa_frontend *f, qa_error *error) {
    if (!f || !f->tools) return true;
    if (!frontend_tools_sync(f, error)) return false;
    return qa_http_pump(f->tools->http, error) && qa_llm_tick(f->tools->llm, error);
}
bool frontend_tools_camera(qa_frontend *f, uint32_t seat, bool portal, qa_scene_view *view, qa_error *error) {
    if (!f || !f->tools) return true;
    if (seat >= f->options.seats) return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid camera presentation seat");
    qa_arena scratch = {0}; qa_scene_view result;
    bool ok = qa_tools_apply_camera(f->tools->owner, view, portal, &scratch, &result, error);
    if (ok) *view = result;
    qa_arena_destroy(&scratch); return ok;
}
bool frontend_tools_debug(qa_frontend *f, const qa_scene_view *view, qa_error *error) {
    if (!f || !f->tools) return true;
    qa_cvars *cvars = qa_application_cvars(f->application);
    const qa_cvar_view *width = qa_cvars_find(cvars, "gl_debug_linewidth");
    if (width && isfinite(width->number) && width->number > 0) f->tools->debug_width = width->number;
    else if (width && !qa_cvars_set_number(cvars, "gl_debug_linewidth", f->tools->debug_width, error)) return false;
    qa_arena scratch = {0}; const qa_debug_line *lines; size_t count;
    bool ok = qa_debug_store_snapshot(qa_tools_debug(f->tools->owner), milliseconds(f), f->frame_number, &scratch, &lines, &count, error);
    if (ok && count) ok = qa_debug_draw(&f->frame, view, qa_scene_white(f->ui_images), lines, count, f->tools->debug_width, error);
    qa_arena_destroy(&scratch); return ok;
}
bool frontend_tools_after_present(qa_frontend *f, qa_error *error) {
    if (!f || !f->tools) return true;
    if (f->display) {
        qa_display_info display;
        if (!qa_display_info_get(f->display, &display, error)) return false;
        if (display.minimized || !display.drawable_width || !display.drawable_height) return true;
    }
    return qa_tools_after_present(f->tools->owner, error);
}
bool frontend_tools_destroy(qa_frontend *f, qa_error *error) {
    if (!f || !f->tools) return true;
    qa_frontend_tools *services = f->tools;
    if (!qa_llm_destroy(services->llm, error)) return false;
    services->llm = NULL;
    if (!qa_tools_destroy(services->owner, error)) return false;
    services->owner = NULL;
    if (!qa_http_destroy(services->http, error)) return false;
    services->http = NULL;
    qa_vfs_destroy(services->files); qa_vfs_destroy(services->settings); qa_resource_pool_destroy(services->private_resources);
    free(services->output_root); free(services); f->tools = NULL; return true;
}
qa_http *frontend_tools_http(qa_frontend *f) { return f && f->tools ? f->tools->http : NULL; }
qa_llm *frontend_tools_llm(qa_frontend *f) { return f && f->tools ? f->tools->llm : NULL; }
qa_tools *frontend_tools_owner(qa_frontend *f) { return f && f->tools ? f->tools->owner : NULL; }

bool frontend_tools_capture_clock(qa_frontend *f, uint64_t elapsed_ns, uint64_t *out, qa_error *error)
{
    if (!f || !out) return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid frontend capture clock");
    if (!f->tools || f->options.dedicated || !f->options.seats) { *out = elapsed_ns; return true; }
    qa_command_context source = qa_input_seat_context(f->seats[0].input);
    qa_console *console = qa_application_console(f->application);
    qa_cvars *registry = qa_console_cvar_owner(console, &source, "cl_avidemo");
    const qa_cvar_view *fps = qa_cvars_find(registry, "cl_avidemo");
    if (!fps || fps->number <= 0 || elapsed_ns == 0) { *out = elapsed_ns; return true; }
    const qa_cvar_view *force = qa_cvars_find(qa_console_cvar_owner(console, &source, "cl_forceavidemo"), "cl_forceavidemo");
    const qa_cvar_view *scale = qa_cvars_find(qa_console_cvar_owner(console, &source, "timescale"), "timescale");
    qa_capture_clock clock;
    if (!qa_capture_frame_time((double)elapsed_ns / 1000000.0, fps->number, scale ? scale->number : 1,
        qa_application_get_state(f->application) == QA_APPLICATION_RUNNING, force && force->number != 0, &clock, error)) return false;
    double duration = clock.milliseconds * 1000000;
    if (!isfinite(duration) || duration < 0 || duration >= 18446744073709551616.0) return frontend_fail(error, QA_ERROR_ARGUMENT, "capture frame duration exceeds native range");
    if (clock.capture) {
        source.direct = false; source.script = NULL;
        if (!qa_tools_capture_frame(f->tools->owner, &source, error)) return false;
    }
    *out = (uint64_t)duration; return true;
}
