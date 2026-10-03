#include "tools_internal.h"
#include "save_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool tools_fail(qa_error *error, const char *text) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text); return false; }
bool qa_tools_callbacks_idle(const qa_tools *tools) { return !tools || !tools->busy; }
char *tools_copy(const char *text, qa_error *error) {
    size_t n = strlen(text); if (n == SIZE_MAX) { tools_fail(error, "tool text exceeds native range"); return NULL; }
    char *out = malloc(n + 1); if (out) memcpy(out, text, n + 1); else qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating tool text");
    return out;
}
void tools_print(qa_tools *tools, const qa_command_context *context, const char *text) {
    if (tools->output_console && qa_console_output_redirected(tools->output_console))
        qa_console_emit(tools->output_console, context, text);
    else if (tools->options.print) tools->options.print(tools->options.context, context, text);
}
bool tools_files(qa_tools *tools, const qa_command_context *source, qa_vfs **files, qa_mount_id *mount, qa_error *error) {
    if (tools->options.files_for_context) return tools->options.files_for_context(tools->options.context, source, files, mount, error);
    *files = tools->options.files; *mount = tools->options.output_mount; return true;
}
static void capture_free(tools_capture *request) { free(request->script); free(request->name); free(request); }
static bool equal_name(const char *a, const char *b) {
    while (*a && *b) {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return *a == *b;
}
static void stop(qa_tools *tools) { qa_camera_playback_destroy(tools->playback); tools->playback = NULL; }
static void reset_camera(qa_tools *tools) { stop(tools); qa_camera_release(tools->camera); tools->camera = NULL; }
static bool start(qa_tools *tools, qa_error *error) {
    if (!tools->camera) return tools_fail(error, "no camera loaded");
    qa_camera_playback *playback = NULL;
    if (!qa_camera_playback_create(tools->camera, tools->options.milliseconds(tools->options.context), &playback, error)) return false;
    stop(tools); tools->playback = playback; return true;
}
static bool capture_queue(qa_tools *tools, const qa_command_context *source, qa_capture_format format,
                           const char *name, bool levelshot, bool silent, qa_error *error) {
    qa_command_context captured;
    if (source && tools->options.capture_context) {
        if (!tools->options.capture_context(tools->options.context, source, &captured, error)) return false;
        source = &captured;
    }
    if (!source || !tools->options.context_active(tools->options.context, source))
        return tools_fail(error, "screenshot requires an active seat renderer");
    tools_capture *request = calloc(1, sizeof *request);
    if (!request) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating pending capture"); return false; }
    request->source = *source; request->format = format; request->levelshot = levelshot; request->silent = silent;
    if (source->script) {
        request->script = tools_copy(source->script, error); if (!request->script) goto failed;
        request->source.script = request->script;
    }
    if (name) { request->name = tools_copy(name, error); if (!request->name) goto failed; }
    *tools->capture_tail = request; tools->capture_tail = &request->next; return true;
failed:
    capture_free(request); return false;
}
static bool load(qa_tools *tools, const char *path, const qa_command_context *source, qa_error *error) {
    qa_resource *resource = NULL; qa_error local = {0};
    qa_vfs *files; qa_mount_id mount;
    if (!tools_files(tools, source, &files, &mount, error)) return false;
    bool opened = mount && qa_vfs_acquire_from(files, mount, path, &resource, &local);
    if (!opened && (!mount || local.code == QA_ERROR_NOT_FOUND)) opened = qa_vfs_acquire(files, path, &resource, NULL, &local);
    if (!opened) { if (error) *error = local; return false; }
    qa_camera_document *document = NULL;
    bool decoded = qa_camera_decode(qa_resource_bytes(resource), &document, error); qa_resource_release(resource);
    if (!decoded) return false;
    reset_camera(tools); tools->camera = document;
    char message[256]; const qa_camera_definition *d = qa_camera_describe(document);
    (void)snprintf(message, sizeof message, "Loaded camera: %.3gs, %zu events\n", d->seconds, d->event_count);
    tools_print(tools, source, message); return true;
}
static bool command(void *context, const qa_command_invocation *call, qa_error *error) {
    qa_tools *tools = context;
    if (tools->busy || tools->pending_restore) return tools_fail(error, "tools command requires returned callbacks and restored continuation");
    ++tools->busy; tools->output_console = call->console; bool success = false;
    const char *name = call->argv[0];
    if (equal_name(name, "loadcamera")) {
        if (call->argc != 2) tools_fail(error, "usage: loadcamera <file.camera>");
        else success = load(tools, call->argv[1], &call->context, error);
    } else if (equal_name(name, "savecamera")) {
        if (call->argc != 2) tools_fail(error, "usage: savecamera <file.camera>");
        else if (!tools->camera) tools_fail(error, "no camera loaded");
        else {
            qa_buffer bytes = {0};
            qa_vfs *files; qa_mount_id mount;
            success = tools_files(tools, &call->context, &files, &mount, error) && qa_camera_encode(tools->camera, &bytes, error) &&
                qa_vfs_write(files, mount, call->argv[1], (qa_bytes){bytes.data, bytes.size}, error);
            qa_buffer_free(&bytes);
        }
    } else if (equal_name(name, "startcamera")) {
        if (call->argc != 1) tools_fail(error, "usage: startcamera"); else success = start(tools, error);
    } else if (equal_name(name, "stopcamera")) { stop(tools); success = true; }
    else if (equal_name(name, "source-camera")) success = tools_camera_author(tools, call, error);
    else if (equal_name(name, "timers") || equal_name(name, "timerstamp")) success = tools_timer_command(tools, call, error);
    else if (!equal_name(name, "screenshot") && !equal_name(name, "screenshotPNG") && !equal_name(name, "screenshotJPEG") && !equal_name(name, "levelshot")) success = tools_diagnostic_command(tools, call, error);
    else {
        const char *argument = call->argc > 1 ? call->argv[1] : NULL;
        bool levelshot = equal_name(name, "levelshot") || (argument && !strcmp(argument, "levelshot"));
        bool silent = argument && !strcmp(argument, "silent");
        if (equal_name(name, "levelshot") && call->context.dialect == QA_CONSOLE_Q3) {
            if (tools->options.forward) success = tools->options.forward(tools->options.context, call, error);
            else tools_fail(error, "Q3 levelshot needs the active source command route");
        } else {
            const char *capture_name = levelshot ? tools->options.map_name(tools->options.context) : silent ? NULL : argument;
            if (levelshot && !capture_name) tools_fail(error, "no active map for levelshot");
            else success = capture_queue(tools, &call->context,
                equal_name(name, "screenshotPNG") ? QA_CAPTURE_PNG : equal_name(name, "screenshotJPEG") ? QA_CAPTURE_JPEG : QA_CAPTURE_TGA,
                capture_name, levelshot, silent, error);
        }
    }
    tools->output_console = NULL; --tools->busy; return success;
}
static const struct { const char *name, *usage, *summary; } commands[] = {
    {"loadcamera", "loadcamera <file.camera>", "Load an authored camera through the active resource owner."},
    {"savecamera", "savecamera <file.camera>", "Save the current authored camera."},
    {"startcamera", "startcamera", "Start the current authored camera."},
    {"stopcamera", "stopcamera", "Stop the current authored camera."},
    {"screenshot", "screenshot [name|silent|levelshot]", "Capture the next presented frame as TGA."},
    {"screenshotPNG", "screenshotPNG [name|silent|levelshot]", "Capture the next presented frame as PNG."},
    {"screenshotJPEG", "screenshotJPEG [name|silent|levelshot]", "Capture the next presented frame as JPEG."},
    {"levelshot", "levelshot", "Capture a source levelshot, retaining source command routing."},
    {"timers", "timers [on|off|reset|report|stamps]", "Inspect actual application profiler timings."},
    {"timerstamp", "timerstamp <label>", "Record a label against the actual profiler clock."}
    ,{"source-camera", "source-camera normalize <input> <output> | sample <input> <output> [step-ms]", "Normalize or sample authored cameras through the active resource owner."}
    ,{"meminfo", "meminfo", "Report actual native process memory bytes."}
    ,{"path", "path", "Show active filesystem search order and overrides."}
    ,{"dir", "dir [path] [extension]", "List files through the active resource owner."}
    ,{"touchFile", "touchFile <file>", "Acquire a file through the active resource owner."}
    ,{"resourceinfo", "resourceinfo", "Report resources actually opened by the active owner."}
    ,{"frameinfo", "frameinfo", "Report current application frame and source state."}
    ,{"imagelist", "imagelist", "List actual resident renderer images."}
    ,{"shaderlist", "shaderlist [sorted]", "List registered shaders in registration or source sorted order."}
    ,{"modellist", "modellist", "List model handles registered by the invoking source client."}
    ,{"skinlist", "skinlist", "List source skin handles and their registered surface shaders."}
    ,{"modelist", "modelist", "List actual active video device display modes."}
    ,{"gfxinfo", "gfxinfo", "Report actual backend, drawable size and driver."}
};
static bool create_owner(const qa_tools_options *options, qa_tools **out, qa_error *error) {
    qa_tools *tools = calloc(1, sizeof *tools);
    if (!tools) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating application tools"); return false; }
    tools->options = *options; tools->capture_tail = &tools->captures;
    if (!qa_profiler_create(options->profiler_milliseconds ? options->profiler_milliseconds : options->milliseconds, options->context, 4096, &tools->profiler, error)) { free(tools); return false; }
    if (!qa_debug_store_create(9216, &tools->debug, error)) { (void)qa_profiler_destroy(tools->profiler, NULL); free(tools); return false; }
    *out = tools; return true;
}
static bool output_ready(const qa_tools_options *options) {
    return options->output_mount && options->read_frame && options->context_active && options->map_name && options->print;
}
bool qa_tools_create(const qa_tools_options *options, qa_tools **out, qa_error *error) {
    if (!options || !out || !options->files || !options->owner || !options->milliseconds || !output_ready(options))
        return tools_fail(error, "invalid application tools services");
    return create_owner(options,out,error);
}
bool qa_tools_create_diagnostics(qa_vfs *files, uint64_t owner, double (*milliseconds)(void *),
                                  void *context, qa_tools **out, qa_error *error) {
    if (!files || !owner || !milliseconds || !out || *out)
        return tools_fail(error,"detached diagnostics require qualified files, clock and empty output");
    qa_tools_options options={.files=files,.owner=owner,.milliseconds=milliseconds,.context=context};
    return create_owner(&options,out,error);
}
bool qa_tools_create_empty(const qa_tools_options *options, qa_tools **out, qa_error *error) {
    if (!options || !out || *out || !options->files || !options->owner || !options->milliseconds)
        return tools_fail(error, "empty tools require qualified files, clock and an empty output");
    qa_tools *tools = calloc(1, sizeof *tools);
    if (!tools) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating empty tools"); return false; }
    tools->options = *options; tools->capture_tail = &tools->captures; tools->pending_restore = true;
    if (!tools_profiler_empty(options, &tools->profiler, error) || !qa_debug_store_create(9216, &tools->debug, error)) {
        (void)qa_profiler_destroy(tools->profiler, NULL); free(tools); return false;
    }
    tools_debug_pending(tools->debug);
    *out = tools; return true;
}
static bool saved_options_equal(const qa_tools_options *a, const qa_tools_options *b) {
    return a->files == b->files && a->output_mount == b->output_mount && a->owner == b->owner && a->context == b->context &&
        a->milliseconds == b->milliseconds && a->profiler_milliseconds == b->profiler_milliseconds && a->read_frame == b->read_frame &&
        a->context_active == b->context_active && a->map_name == b->map_name && a->print == b->print && a->forward == b->forward &&
        a->diagnostic == b->diagnostic && a->files_for_context == b->files_for_context && a->capture_context == b->capture_context;
}
static void saved_private_free(qa_tools *tools) {
    while (tools->captures) { tools_capture *v = tools->captures; tools->captures = v->next; capture_free(v); }
    while (tools->consoles) { tools_console *v = tools->consoles; tools->consoles = v->next; free(v); }
    reset_camera(tools); (void)qa_profiler_destroy(tools->profiler, NULL); qa_debug_store_destroy(tools->debug);
}
static bool saved_tools_fields(qa_source_save_io *io, qa_tools *tools, const qa_tools *installed, const qa_tools_checkpoint_refs *refs) {
    uint64_t services = 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE && !refs->services_encode(refs->context, &tools->options, &services, io->error)) return false;
    if (!qa_source_save_u64(io, &services)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && (!refs->services_decode(refs->context, services, &tools->options, io->error) ||
        !saved_options_equal(&tools->options, &installed->options))) return tool_save_fail(io, "tools continuation services differ from installed candidate owners");
    size_t count = 0; for (tools_console *v = tools->consoles; v; v = v->next) ++count;
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(tools_console))) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        size_t actual = 0; for (tools_console *v = installed->consoles; v; v = v->next) ++actual;
        if (actual != count) return tool_save_fail(io, "tools continuation console roster differs from installed candidate");
    }
    tools_console *cursor = tools->consoles, **tail = &tools->consoles;
    for (size_t i = 0; i < count; ++i) {
        uint64_t id = 0; qa_console *console = NULL;
        if (io->direction == QA_SOURCE_SAVE_WRITE && !refs->console_encode(refs->context, cursor->console, &id, io->error)) return false;
        if (!qa_source_save_u64(io, &id)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            if (!refs->console_decode(refs->context, id, &console, io->error) || !console) return tool_save_fail(io, "unresolved tools continuation console");
            bool found = false; for (tools_console *v = installed->consoles; v; v = v->next) if (v->console == console) found = true;
            if (!found) return tool_save_fail(io, "tools continuation console has no installed handlers");
            for (tools_console *v = tools->consoles; v; v = v->next) if (v->console == console) return tool_save_fail(io, "duplicate tools continuation console");
            tools_console *v = calloc(1, sizeof *v); if (!v) return tool_save_fail(io, "allocating tools continuation console");
            v->console = console; *tail = v; tail = &v->next;
        } else cursor = cursor->next;
    }
    if (!tools_profiler_fields(io, &tools->profiler, &tools->options) || !tools_debug_fields(io, &tools->debug) ||
        !tools_camera_fields(io, &tools->camera, &tools->playback)) return false;
    count = 0; for (tools_capture *v = tools->captures; v; v = v->next) ++count;
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(tools_capture))) return false;
    if (count && !output_ready(&tools->options)) return tool_save_fail(io, "tools capture continuation lacks installed output services");
    tools_capture *request = tools->captures; tools_capture **capture_tail = &tools->captures;
    for (size_t i = 0; i < count; ++i) {
        if (io->direction == QA_SOURCE_SAVE_READ) {
            request = calloc(1, sizeof *request); if (!request) return tool_save_fail(io, "allocating tools capture continuation");
            *capture_tail = request;
        }
        capture_tail = &request->next;
        uint32_t format = (uint32_t)request->format; char *script = request->script;
        if (!tool_save_context(io, &request->source, &script)) { if (io->direction == QA_SOURCE_SAVE_READ) request->script = script; return false; }
        if (io->direction == QA_SOURCE_SAVE_READ) request->script = script;
        qa_command_context qualified = {0};
        if (!refs->command_context(refs->context, &request->source, &qualified, io->error)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            if (qualified.dialect != request->source.dialect || qualified.origin != request->source.origin || qualified.seat != request->source.seat ||
                qualified.direct != request->source.direct || qualified.console_text != request->source.console_text)
                return tool_save_fail(io, "tools continuation changed deferred command semantics");
            qualified.script = request->script; request->source = qualified;
        }
        if (!tool_save_text(io, &request->name) || !qa_source_save_u32(io, &format) || format > QA_CAPTURE_JPEG ||
            !qa_source_save_bool(io, &request->levelshot) || !qa_source_save_bool(io, &request->silent) ||
            !qa_source_save_u32(io, &request->presented_delays) || request->presented_delays > 5 ||
            (request->presented_delays && !request->levelshot)) return tool_save_fail(io, "invalid capture continuation");
        request->format = (qa_capture_format)format;
        if (request->levelshot && !request->name) return tool_save_fail(io, "saved levelshot lacks its admitted map name");
        if (io->direction == QA_SOURCE_SAVE_WRITE) request = request->next;
    }
    if (io->direction == QA_SOURCE_SAVE_READ) tools->capture_tail = capture_tail;
    return true;
}
bool qa_tools_checkpoint_ready(const qa_tools *tools, qa_error *error) {
    return (tools && !tools->pending_restore && !tools->busy && !tools->output_console && qa_profiler_idle(tools->profiler)) ||
        tools_fail(error, "tools continuation requires returned callbacks and profiler scopes");
}
bool qa_tools_checkpoint(const qa_tools *tools, qa_session *session, const qa_tools_checkpoint_refs *refs, qa_buffer *out, qa_error *error) {
    if (!qa_tools_checkpoint_ready(tools, error) || !out || !refs || !refs->services_encode ||
        !refs->console_encode || !refs->command_context) return tools_fail(error, "tools continuation requires idle qualified owners");
    qa_source_save_io io = {0}; if (!qa_source_save_writer(&io, session, error)) return false;
    bool ok = saved_tools_fields(&io, (qa_tools *)tools, tools, refs) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_tools_restore(qa_tools *tools, qa_session *session, const qa_tools_checkpoint_refs *refs, qa_bytes bytes, qa_error *error) {
    if (!tools || tools->busy || tools->output_console || !qa_profiler_idle(tools->profiler) || !refs || !refs->services_decode ||
        !refs->console_decode || !refs->command_context) return tools_fail(error, "tools restoration requires idle installed owners");
    qa_source_save_io io = {0}; qa_tools next = {0};
    if (!qa_source_save_reader(&io, session, bytes, error)) return false;
    bool ok = saved_tools_fields(&io, &next, tools, refs) && qa_source_save_finish(&io, NULL);
    if (ok) {
        qa_tools old = *tools;
        tools_profiler_exchange(tools->profiler, next.profiler); tools_debug_exchange(tools->debug, next.debug);
        old.profiler = next.profiler; old.debug = next.debug; next.profiler = tools->profiler; next.debug = tools->debug;
        *tools = next; if (!tools->captures) tools->capture_tail = &tools->captures;
        saved_private_free(&old);
    } else saved_private_free(&next);
    qa_source_save_dispose(&io); return ok;
}
bool qa_tools_rebind_ready(const qa_tools *tools, const void *old_context, const void *new_context, qa_error *error) {
    return (tools && !tools->pending_restore && !tools->busy && !tools->output_console && new_context && tools->options.context == old_context &&
        tools_profiler_rebind_ready(tools->profiler, old_context, error)) || tools_fail(error, "tools publication requires its actual idle callback owner");
}
void qa_tools_rebind_context(qa_tools *tools, void *context) { tools->options.context = context; tools_profiler_rebind(tools->profiler, context); }
bool qa_tools_attach_console(qa_tools *tools, qa_console *console, qa_error *error) {
    if (!tools || !console || tools->busy || !output_ready(&tools->options)) return tools_fail(error, "invalid tools console admission");
    for (tools_console *it = tools->consoles; it; it = it->next) if (it->console == console) return true;
    tools_console *binding = malloc(sizeof *binding);
    if (!binding) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating tools console binding"); return false; }
    size_t count = 0;
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
        if (!qa_console_register_owned(console, commands[i].name, commands[i].summary, 0, tools->options.owner, true, command, tools, error)) goto failed;
        ++count;
        const qa_console_documentation doc = {.usage = commands[i].usage};
        if (!qa_console_document(console, commands[i].name, 0, &doc, error)) goto failed;
    }
    *binding = (tools_console){tools->consoles, console}; tools->consoles = binding; return true;
failed:
    while (count) (void)qa_console_unregister(console, commands[--count].name, 0);
    free(binding); return false;
}
bool qa_tools_detach_console(qa_tools *tools, qa_console *console, qa_error *error) {
    if (!tools || tools->busy) return tools_fail(error, "tools detach requires callbacks to return");
    tools_console **link = &tools->consoles;
    while (*link && (*link)->console != console) link = &(*link)->next;
    if (!*link) return true;
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) (void)qa_console_unregister(console, commands[i].name, 0);
    tools_console *binding = *link; *link = binding->next; free(binding); return true;
}
bool qa_tools_before_world_change(qa_tools *tools, qa_error *error) {
    if (!tools || tools->busy) return tools_fail(error, "tools travel requires callbacks to return");
    ++tools->busy;
    while (tools->captures) {
        tools_capture *request = tools->captures; tools->captures = request->next;
        if (tools->options.context_active(tools->options.context, &request->source))
            tools_print(tools, &request->source, "Screenshot canceled before the requested frame was presented\n");
        capture_free(request);
    }
    tools->capture_tail = &tools->captures; reset_camera(tools); qa_debug_store_clear(tools->debug); --tools->busy;
    while (tools->consoles) if (!qa_tools_detach_console(tools, tools->consoles->console, error)) return false;
    return true;
}
bool qa_tools_destroy(qa_tools *tools, qa_error *error) {
    if (!tools) return true;
    if (!qa_profiler_idle(tools->profiler)) return tools_fail(error, "tools teardown requires profiler scopes to return");
    if (!qa_tools_before_world_change(tools, error)) return false;
    if (!qa_profiler_destroy(tools->profiler, error)) return false;
    qa_debug_store_destroy(tools->debug);
    free(tools); return true;
}
bool qa_tools_set_files(qa_tools *tools, qa_vfs *files, qa_mount_id mount, qa_error *error) {
    if (!tools || !files || !mount || tools->busy || tools->pending_restore || tools->captures) return tools_fail(error, "tools filesystem change requires restored idle capture");
    tools->options.files = files; tools->options.output_mount = mount; return true;
}
bool qa_tools_set_camera(qa_tools *tools, qa_camera_document *document, qa_error *error) {
    if (!tools || tools->busy || tools->pending_restore) return tools_fail(error, "camera editing requires restored continuation and returned callbacks");
    qa_camera_retain(document); reset_camera(tools); tools->camera = document; return true;
}
bool qa_tools_start_camera(qa_tools *tools, qa_error *error) {
    if (!tools || tools->busy || tools->pending_restore) return tools_fail(error, "camera start requires restored continuation and returned callbacks");
    ++tools->busy; bool success = start(tools, error); --tools->busy; return success;
}
bool qa_tools_stop_camera(qa_tools *tools, qa_error *error) {
    if (!tools || tools->busy || tools->pending_restore) return tools_fail(error, "camera stop requires restored continuation and returned callbacks");
    stop(tools); return true;
}
bool qa_tools_apply_camera(qa_tools *tools, const qa_scene_view *base, bool portal,
                            qa_arena *scratch, qa_scene_view *out, qa_error *error) {
    if (!tools || !base || !out || !scratch || tools->busy || tools->pending_restore) return tools_fail(error, "invalid or pending camera view application");
    if (portal || !tools->playback) { *out = *base; return true; }
    ++tools->busy; qa_camera_sample sample; bool active = false;
    bool success = qa_camera_playback_sample(tools->playback, tools->options.milliseconds(tools->options.context), scratch, &sample, &active, error);
    if (success && active) success = qa_camera_sample_view(&sample, base, out, error);
    else if (success) { stop(tools); *out = *base; }
    --tools->busy; return success;
}
bool qa_tools_capture_frame(qa_tools *tools, const qa_command_context *source, qa_error *error) {
    if (!tools || tools->busy || tools->pending_restore || !output_ready(&tools->options)) return tools_fail(error, "capture admission requires restored idle output services");
    ++tools->busy; bool success = capture_queue(tools, source, QA_CAPTURE_TGA, NULL, false, false, error); --tools->busy; return success;
}
bool qa_tools_capture_levelshot(qa_tools *tools, const qa_command_context *source, qa_error *error) {
    if (!tools || tools->busy || tools->pending_restore || !output_ready(&tools->options))
        return tools_fail(error, "levelshot admission requires restored idle output services");
    ++tools->busy;
    const char *map = tools->options.map_name(tools->options.context);
    tools_capture **queued = tools->capture_tail;
    bool success = map ? capture_queue(tools, source, QA_CAPTURE_TGA, map, true, false, error) :
        tools_fail(error, "no active map for levelshot");
    /* The source callback runs after this frame's console drain. Skip this
     * initial presentation, then the donor's four command-wait boundaries. */
    if (success) (*queued)->presented_delays = 5;
    --tools->busy; return success;
}
bool qa_tools_pending_capture(const qa_tools *tools) { return tools && tools->captures; }
qa_profiler *qa_tools_profiler(qa_tools *tools) { return tools ? tools->profiler : NULL; }
qa_debug_store *qa_tools_debug(qa_tools *tools) { return tools ? tools->debug : NULL; }
bool qa_tools_after_present(qa_tools *tools, qa_error *error) {
    if (!tools || tools->busy || tools->pending_restore) return tools_fail(error, "capture presentation requires restored continuation and returned callbacks");
    if (!tools->captures) return true;
    ++tools->busy; qa_image image = {0}; qa_error read_error = {0};
    bool ready = false;
    for (tools_capture *request = tools->captures; request; request = request->next)
        if (!request->presented_delays && tools->options.context_active(tools->options.context, &request->source)) ready = true;
    bool success = !ready || tools->options.read_frame(tools->options.context, &image, &read_error);
    if (!success && read_error.code == QA_OK) qa_error_set(&read_error, QA_ERROR_IO, 0, "presented frame readback failed");
    qa_error first_error = read_error;
    tools_capture **position = &tools->captures;
    while (*position) {
        tools_capture *request = *position;
        if (!tools->options.context_active(tools->options.context, &request->source)) {
            *position = request->next; capture_free(request); continue;
        }
        if (request->presented_delays) { --request->presented_delays; position = &request->next; continue; }
        *position = request->next;
        qa_capture_result result = {0}; qa_error local = read_error;
        qa_vfs *files; qa_mount_id mount;
        bool written = success && tools_files(tools, &request->source, &files, &mount, &local) && (request->levelshot ?
            qa_capture_save_levelshot(files, mount, &image, request->name, NULL, &result, &local) :
            qa_capture_save(files, mount, &image, request->format, request->name, 90, &result, &local));
        char message[512];
        if (written) {
            if (!request->silent) { (void)snprintf(message, sizeof message, "Wrote %s\n", result.path); tools_print(tools, &request->source, message); }
        } else {
            (void)snprintf(message, sizeof message, "Console output failed: %s\n", local.message); tools_print(tools, &request->source, message);
            if (first_error.code == QA_OK) first_error = local;
        }
        qa_capture_result_free(&result); capture_free(request);
    }
    tools->capture_tail = position; qa_image_free(&image); --tools->busy;
    if (first_error.code != QA_OK) { if (error) *error = first_error; return false; }
    return success;
}
