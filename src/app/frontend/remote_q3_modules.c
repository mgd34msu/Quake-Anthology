#include "remote_q3_modules.h"
#include "remote_q3_private.h"
#include "remote_config.h"
#include "equipment_source.h"
#include "qa/binary.h"
#include "qa/bsp.h"
#include <SDL.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>

typedef struct remote_module_lease {
    struct remote_module_lease *next;
    frontend_remote_q3_modules *owner;
    qa_qvm_role role;
    uint64_t service_owner;
    frontend_client_registry *registry;
    frontend_key_profile *keys;
    frontend_config_host_cvars namespaces;
    qa_q3_host_client_services network;
    qa_q3_presentation *presentation;
    frontend_equipment_source *equipment;
    qa_audio_music *music;
    char *music_intro, *music_loop;
    qa_audio_listener listener;
    const qa_q3_host *render_host;
    const qa_qvm_call *render_call;
    const qa_q3_refdef *render_definition;
    qa_command_context command;
    size_t callbacks;
    bool released, music_attached, music_looping, has_listener;
} remote_module_lease;

struct frontend_remote_q3_modules {
    frontend_remote_q3 *row;
    frontend_remote_q3_resources resources;
    application_native_q3_client_modules *modules;
    remote_module_lease *leases;
    bool attached, constructing, retiring;
};

static bool same_source(const qa_application_q3_remote_source *a,
    const qa_application_q3_remote_source *b)
{
    return a && b && a->descriptor && b->descriptor &&
        a->descriptor->storage == b->descriptor->storage && a->descriptor->content == b->descriptor->content &&
        a->configuration_generation == b->configuration_generation && a->connection_epoch == b->connection_epoch &&
        a->receiver.session == b->receiver.session && a->receiver.receiver == b->receiver.receiver &&
        a->receiver.seat == b->receiver.seat && a->receiver.service_owner == b->receiver.service_owner &&
        a->receiver.frontend_lifetime == b->receiver.frontend_lifetime &&
        a->receiver.console == b->receiver.console && a->receiver.cvars == b->receiver.cvars;
}
static bool current(void *context, const qa_application_q3_remote_source *source,
    const qa_q3_gamestate *gamestate, qa_error *error)
{
    frontend_remote_q3_modules *owner = context;
    frontend_remote_q3_resources resources;
    return owner && !owner->retiring && owner->attached &&
        frontend_remote_q3_modules_read(owner->row) == owner &&
        frontend_remote_q3_resources_read(owner->row, &resources, error) &&
        same_source(source, &resources.domain.source) && gamestate == resources.domain.gamestate;
}
static bool entered(const remote_module_lease *lease, qa_q3_host **host,
    qa_q3_host_client_context *out, qa_error *error)
{
    const frontend_remote_q3_modules *owner = lease ? lease->owner : NULL;
    qa_q3_host_client_context actual;
    qa_q3_host *actual_host = NULL;
    if (!owner || !owner->attached || lease->released || !owner->modules ||
        frontend_remote_q3_modules_read(owner->row) != owner ||
        !qa_application_native_q3_client_modules_entered_host_read(owner->modules, lease->role,
            lease->service_owner, &actual_host, &actual, error) || actual.frontend_lifetime != lease ||
        actual.console != owner->resources.domain.source.receiver.console ||
        actual.cvars != frontend_client_registry_cvars(lease->registry) ||
        actual.owner != owner->resources.domain.source.receiver.receiver ||
        actual.command_context.seat != owner->resources.domain.source.receiver.seat)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module callback lost its actual entered host lease");
    if (host) *host = actual_host;
    if (out) *out = actual;
    return true;
}
static bool namespace_entered(void *context, qa_application *app,
    const qa_application_startup_source *tuple, const qa_q3_host **out, qa_error *error)
{
    remote_module_lease *lease = context;
    if (out) *out = NULL;
    bool matches = lease && out && app == lease->owner->row->application && tuple &&
        tuple->descriptor == lease->namespaces.source.descriptor &&
        tuple->console == lease->namespaces.source.console && tuple->cvars == lease->namespaces.source.cvars &&
        tuple->scope.provider == lease->namespaces.source.scope.provider &&
        tuple->scope.kind == lease->namespaces.source.scope.kind && tuple->scope.seat == lease->namespaces.source.scope.seat &&
        tuple->declaration_owner == lease->namespaces.source.declaration_owner &&
        tuple->command.session == lease->namespaces.source.command.session &&
        tuple->command.owner == lease->namespaces.source.command.owner &&
        tuple->command.seat == lease->namespaces.source.command.seat &&
        tuple->command.client == lease->namespaces.source.command.client &&
        tuple->command.origin == lease->namespaces.source.command.origin &&
        tuple->command.dialect == lease->namespaces.source.command.dialect &&
        tuple->command.direct == lease->namespaces.source.command.direct &&
        tuple->command.console_text == lease->namespaces.source.command.console_text &&
        tuple->command.script == lease->namespaces.source.command.script &&
        tuple->command.registry == lease->namespaces.source.command.registry &&
        tuple->command.generation == lease->namespaces.source.command.generation &&
        qa_actor_id_equal(tuple->command.actor, lease->namespaces.source.command.actor);
    qa_q3_host *host = NULL;
    if (!matches || !entered(lease, &host, NULL, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module namespaces lost their entered constructor tuple");
    *out = host; return true;
}
static void released(void *context)
{ remote_module_lease *lease = context; lease->released = true; }
static double milliseconds(void *context)
{ remote_module_lease *lease = context; return (double)lease->owner->row->frontend->wall_time_ns / 1000000.0; }
static uint32_t common_milliseconds(void *context)
{ remote_module_lease *lease = context; return (uint32_t)(lease->owner->row->frontend->wall_time_ns / UINT64_C(1000000)); }
static int32_t source_milliseconds(void *context)
{ remote_module_lease *lease = context; return (int32_t)((lease->owner->row->frontend->wall_time_ns / UINT64_C(1000000)) & INT32_MAX); }
static int32_t frame_number(void *context)
{ remote_module_lease *lease = context; return (int32_t)(lease->owner->row->frontend->frame_number & INT32_MAX); }
static uint64_t audio_bus(void *context)
{ return ((remote_module_lease *)context)->service_owner; }
static void print(void *context, const char *text)
{
    remote_module_lease *lease = context;
    if (!entered(lease, NULL, NULL, NULL)) return;
    ++lease->callbacks;
    qa_console *console = lease->owner->resources.domain.source.receiver.console;
    qa_console *shared = qa_application_console(lease->owner->row->application);
    if (qa_console_output_redirected(console)) qa_console_emit(console, &lease->command, text);
    else if (shared && qa_console_output_redirected(shared)) qa_console_emit(shared, &lease->command, text);
    else frontend_console_print(lease->owner->row->frontend, &lease->command, text);
    --lease->callbacks;
}
static int32_t calendar(void *context, qa_q3_host_calendar *out)
{
    if (out) *out = (qa_q3_host_calendar){0};
    if (!entered(context, NULL, NULL, NULL)) return 0;
    time_t now = time(NULL);
    struct tm *calendar = out ? localtime(&now) : NULL;
    if (calendar) *out = (qa_q3_host_calendar){calendar->tm_sec, calendar->tm_min, calendar->tm_hour,
        calendar->tm_mday, calendar->tm_mon, calendar->tm_year, calendar->tm_wday,
        calendar->tm_yday, calendar->tm_isdst};
    return (int32_t)now;
}
static bool arguments(void *context, qa_native_host_command_view *out, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!entered(lease, NULL, NULL, error)) return false;
    uint64_t revision;
    return qa_application_native_q3_client_modules_entered_arguments_read(lease->owner->modules,
        lease->role, lease->service_owner, out, &revision, error);
}
static bool client_command(void *context, const char *text, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!entered(lease, NULL, NULL, error)) return false;
    ++lease->callbacks;
    bool ok = frontend_network_client_reliable(lease->owner->row->frontend, &lease->command, text, error);
    --lease->callbacks; return ok;
}
static bool clipboard(void *context, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || !entered(context, NULL, NULL, error)) return false;
    char *text = SDL_GetClipboardText();
    if (!text) { qa_error_set(error, QA_ERROR_IO, 0, "Reading clipboard: %s", SDL_GetError()); return false; }
    size_t size = strlen(text); uint8_t *copy = malloc(size + 1);
    if (!copy) { SDL_free(text); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining remote module clipboard text"); }
    memcpy(copy, text, size + 1); SDL_free(text); *out = (qa_buffer){copy, size}; return true;
}
static bool actor(void *context, int32_t number, uint64_t *out, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!out || !entered(lease, NULL, NULL, error)) return false;
    if (number < 0) { *out = QA_AUDIO_NO_ACTOR; return true; }
    qa_actor_id actual; bool present;
    if (!lease->network.source_actor(lease->network.context, (uint32_t)number, &actual, &present, error)) return false;
    *out = present ? frontend_audio_actor(lease->owner->row->frontend, actual, error) : QA_AUDIO_NO_ACTOR;
    return !present || *out != QA_AUDIO_NO_ACTOR;
}
static bool listener(void *context, const qa_audio_listener *value, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!value || !entered(lease, NULL, NULL, error)) return false;
    lease->listener = *value;
    lease->listener.gain = 1.0f / (float)lease->owner->row->frontend->options.seats;
    lease->has_listener = true; return true;
}
static bool music(void *context, const char *intro_name, const char *loop_name, qa_error *error)
{
    remote_module_lease *lease = context; qa_frontend *f = lease->owner->row->frontend;
    if (!entered(lease, NULL, NULL, error)) return false;
    if (!f->audio) return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Remote module music has no audio output owner");
    if (lease->music_attached) {
        lease->music = qa_audio_engine_bus_music(f->audio, lease->service_owner);
        lease->music_attached = lease->music != NULL;
    }
    if (!lease->music && !qa_audio_music_create(qa_audio_engine_rate(f->audio), QA_AUDIO_Q3, true, &lease->music, error)) return false;
    const char *requested_loop = loop_name ? loop_name : "";
    if (intro_name && lease->music_intro && lease->music_loop && lease->music_looping &&
        !strcmp(intro_name, lease->music_intro) && !strcmp(requested_loop, lease->music_loop) &&
        qa_audio_music_playing(lease->music)) return true;
    qa_audio_music_stop(lease->music);
    free(lease->music_intro); free(lease->music_loop);
    lease->music_intro = lease->music_loop = NULL; lease->music_looping = false;
    if (!intro_name || !*intro_name) return true;
    size_t a = strlen(intro_name), b = strlen(requested_loop);
    lease->music_intro = malloc(a + 1); lease->music_loop = malloc(b + 1);
    if (!lease->music_intro || !lease->music_loop) {
        free(lease->music_intro); free(lease->music_loop); lease->music_intro = lease->music_loop = NULL;
        return frontend_fail(error, QA_ERROR_MEMORY, "Retaining remote module music selection");
    }
    memcpy(lease->music_intro, intro_name, a + 1); memcpy(lease->music_loop, requested_loop, b + 1);
    lease->music_looping = true;
    qa_audio_stream *intro = NULL, *loop = NULL;
    if (!qa_audio_bank_music_cue(lease->owner->resources.sounds, intro_name, QA_AUDIO_Q3, NULL, NULL, &intro, error)) return false;
    if (!intro) return true;
    loop = intro;
    if (loop_name && *loop_name && strcmp(loop_name, intro_name) &&
        !qa_audio_bank_music_cue(lease->owner->resources.sounds, loop_name, QA_AUDIO_Q3, NULL, NULL, &loop, error)) {
        qa_audio_stream_close(intro); return false;
    }
    lease->music_looping = loop != NULL;
    qa_audio_music_start(lease->music, intro, loop);
    bool ok = qa_audio_engine_music(f->audio, lease->service_owner, lease->owner->resources.physical_seat, 1, lease->music, error);
    if (ok) lease->music_attached = true;
    return ok;
}
static qa_collision_geometry *geometry(void *context)
{ remote_module_lease *lease = context; return entered(lease, NULL, NULL, NULL) ? lease->owner->resources.geometry : NULL; }
static bool load_map(void *context, const char *path, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!path || !geometry(lease)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module collision lost its entered map owner");
    const char *map = qa_resource_path(lease->owner->resources.map);
    if (!map) return frontend_fail(error, QA_ERROR_FORMAT, "Remote collision map has no actual resource path");
    if (!strncmp(path, "maps/", 5)) path += 5;
    if (!strncmp(map, "maps/", 5)) map += 5;
    size_t a = strlen(path), b = strlen(map);
    if (a > 4 && !strcmp(path + a - 4, ".bsp")) a -= 4;
    if (b > 4 && !strcmp(map + b - 4, ".bsp")) b -= 4;
    return (a == b && !memcmp(path, map, a)) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module collision differs from its retained decoded map");
}
static bool remap(void *context, const char *from, const char *to, float offset, qa_error *error)
{
    remote_module_lease *lease = context;
    return entered(lease, NULL, NULL, error) &&
        qa_material_remap(lease->owner->resources.materials, from, to, offset, error);
}
static bool render_current(const remote_module_lease *lease)
{
    return lease && lease->render_definition && lease->callbacks &&
        qa_q3_host_render_scope_current(lease->render_host, lease->render_call, lease,
            lease->service_owner, lease->role, lease->presentation) && entered(lease, NULL, NULL, NULL);
}
static bool prepare_view(void *context, const qa_q3_refdef *definition, qa_q3_scene_options *options, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!render_current(lease) || definition != lease->render_definition || !options)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module view lost its actual entered RenderScene");
    options->world_family = QA_SCENE_Q3;
    options->split_screen = lease->owner->row->frontend->options.seats > 1;
    return !lease->equipment || frontend_equipment_source_prepare_view(lease->equipment, definition, options, error);
}
static bool submit_view(void *context, const qa_q3_scene_options *options, qa_scene_frame *frame, qa_error *error)
{
    remote_module_lease *lease = context;
    return render_current(lease) && frame == &lease->owner->row->frontend->frame &&
        (!lease->equipment || frontend_equipment_source_submit(lease->equipment, options, frame, error));
}
static void scene_cleared(void *context)
{ remote_module_lease *lease = context; frontend_equipment_source_clear(lease->equipment); }
static bool render_enter(void *context, const qa_q3_host *host, const qa_qvm_call *call,
    const qa_q3_refdef *definition, void **out, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!out || *out || !definition || lease->render_definition || lease->callbacks == SIZE_MAX ||
        !qa_q3_host_render_scope_current(host, call, lease, lease->service_owner, lease->role, lease->presentation) ||
        !entered(lease, NULL, NULL, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module renderer lost its actual host and lease");
    ++lease->callbacks; lease->render_host = host; lease->render_call = call; lease->render_definition = definition;
    *out = lease; return true;
}
static void render_leave(void *context, void *token, bool rendered)
{
    remote_module_lease *lease = context; (void)rendered;
    if (token != lease) return;
    lease->render_host = NULL; lease->render_call = NULL; lease->render_definition = NULL; --lease->callbacks;
}
static bool equipment_current(void *context, const qa_application_q3_client_context *client)
{
    remote_module_lease *lease = context; frontend_remote_q3_resources resources;
    if (!client || lease->role != QA_QVM_CGAME || !entered(lease, NULL, NULL, NULL) ||
        !frontend_remote_q3_resources_read(lease->owner->row, &resources, NULL)) return false;
    qa_application_q3_role_receipt receipt;
    if (!qa_application_native_q3_client_modules_receipt_read(lease->owner->modules,
        QA_QVM_CGAME, &receipt, NULL) || receipt.service_owner != lease->service_owner ||
        !qa_application_native_q3_client_modules_receipt_current(lease->owner->modules, &receipt)) return false;
    qa_actor_id actor; bool present;
    if (!lease->network.source_actor(lease->network.context,
        (uint32_t)resources.domain.initial.client_number, &actor, &present, NULL)) return false;
    if (!present) actor = (qa_actor_id){0};
    const qa_application_q3_client_context *actual = &resources.domain.source.receiver;
    return client->session == actual->session && client->native_source == actual->native_source &&
        !client->source_owner && !client->source_cvars && qa_actor_id_equal(client->source_actor, actor) &&
        client->source_client == actual->source_client && client->source_milliseconds == actual->source_milliseconds &&
        client->client_time_cvars == actual->client_time_cvars && client->client_time_owner == actual->client_time_owner &&
        client->frontend_lifetime == lease && client->service_owner == lease->service_owner &&
        client->receiver == resources.domain.source.receiver.receiver && client->seat == resources.domain.source.receiver.seat &&
        client->console == resources.domain.source.receiver.console && client->cvars == resources.domain.source.receiver.cvars &&
        client->initialized;
}
static bool equipment_borrow(void *context, qa_application_q3_client_context *out, qa_error *error)
{
    remote_module_lease *lease = context; frontend_remote_q3_resources resources;
    if (!out || lease->callbacks == SIZE_MAX || !entered(lease, NULL, NULL, error) ||
        !frontend_remote_q3_resources_read(lease->owner->row, &resources, error)) return false;
    qa_application_q3_client_context client = resources.domain.source.receiver;
    client.frontend_lifetime = lease; client.service_owner = lease->service_owner;
    qa_application_q3_role_receipt receipt;
    if (!qa_application_native_q3_client_modules_receipt_read(lease->owner->modules,
        QA_QVM_CGAME, &receipt, error) || receipt.service_owner != lease->service_owner ||
        !qa_application_native_q3_client_modules_receipt_current(lease->owner->modules, &receipt)) return false;
    client.initialized = true;
    bool present;
    if (!lease->network.source_actor(lease->network.context, (uint32_t)resources.domain.initial.client_number,
        &client.source_actor, &present, error)) return false;
    if (!present) client.source_actor = (qa_actor_id){0};
    ++lease->callbacks; *out = client; return true;
}
static void equipment_return(void *context) { --((remote_module_lease *)context)->callbacks; }
static bool equipment_requests(void *context, bool *hud, bool *view, qa_error *error)
{ remote_module_lease *lease = context; return qa_application_native_q3_client_modules_equipment_requests(lease->owner->modules, hud, view, error); }
static bool configuration(void *context, uint8_t out[11332], qa_error *error)
{
    remote_module_lease *lease = context; qa_frontend *f = lease->owner->row->frontend;
    qa_display_info display;
    if (!entered(lease, NULL, NULL, error) || !qa_display_info_get(f->display, &display, error)) return false;
    memset(out, 0, 11332);
    const qa_gl_capabilities *caps = qa_gl_capabilities_get(f->gl);
    snprintf((char *)out, 1024, "%s", caps ? caps->renderer : "Quake Anthology CPU renderer");
    snprintf((char *)out + 1024, 1024, "%s", caps ? caps->vendor : "Quake Anthology");
    snprintf((char *)out + 2048, 1024, "%s", caps ? caps->version : "retained scene renderer");
    qa_store_u32le(out + 11264, caps ? caps->maximum_texture_size : INT32_MAX);
    qa_store_u32le(out + 11268, caps ? caps->texture_units : 1);
    qa_store_u32le(out + 11272, caps ? caps->color_bits : 32);
    qa_store_u32le(out + 11276, caps ? caps->depth_bits : 64);
    qa_store_u32le(out + 11280, caps ? caps->stencil_bits : 8);
    qa_store_u32le(out + 11304, display.drawable_width); qa_store_u32le(out + 11308, display.drawable_height);
    float aspect = display.drawable_height ? (float)display.drawable_width / (float)display.drawable_height : 1;
    uint32_t bits; memcpy(&bits, &aspect, sizeof(bits)); qa_store_u32le(out + 11312, bits);
    qa_store_u32le(out + 11316, display.refresh_rate > 0 ? (uint32_t)display.refresh_rate : 0);
    qa_store_u32le(out + 11320, display.fullscreen != QA_DISPLAY_WINDOWED); qa_store_u32le(out + 11324, caps && caps->stereo);
    return true;
}
static bool update_screen(void *context, qa_error *error)
{
    remote_module_lease *lease = context; qa_frontend *f = lease->owner->row->frontend; bool drawn;
    if (!entered(lease, NULL, NULL, error) ||
        !qa_application_native_q3_client_modules_loading_screen(lease->owner->modules, &drawn, error)) return false;
    return f->cpu ? qa_cpu_execute(f->cpu, &f->frame, error) && qa_cpu_present_frame(f->cpu, error) :
        qa_gl_execute(f->gl, &f->frame, error) && qa_gl_finish(f->gl, error) && qa_display_swap(f->display, error);
}

static bool prepare(void *context, const qa_application_native_q3_module_preparation *request, qa_error *error)
{
    frontend_remote_q3_modules *owner = context;
    frontend_remote_q3_resources resources;
    if (!request || !request->services || request->restoring || !owner->constructing ||
        !current(owner, request->source, owner->resources.domain.gamestate, error) ||
        !frontend_remote_q3_resources_read(owner->row, &resources, error)) return false;
    remote_module_lease *lease = calloc(1, sizeof(*lease));
    if (!lease) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining acquired remote host lease");
    lease->owner = owner; lease->role = request->role; lease->service_owner = request->service_owner;
    lease->command = request->services->command_context;
    lease->next = owner->leases; owner->leases = lease;
    qa_q3_host_options *host = request->services;
    host->frontend_lifetime = lease; host->release_frontend = released;
    qa_application_startup_source tuple;
    frontend_remote_config *config = frontend_config_store_client(owner->row->frontend->config_store, host->console);
    frontend_remote_config_view configuration_view;
    bool ok = config && frontend_remote_config_read(config, &configuration_view) &&
        frontend_remote_config_current(config, &configuration_view) && configuration_view.ready &&
        configuration_view.cvars == host->cvars && configuration_view.physical_seat == resources.physical_seat &&
        frontend_client_registry_retain(resources.registry, &lease->registry, error) &&
        qa_application_q3_client_configuration_read(owner->row->application, host->owner,
            request->source->receiver.seat, &tuple, error) &&
        frontend_config_host_cvars_prepare(&lease->namespaces, owner->row->frontend->config_store,
            owner->row->application, &tuple, NULL, &host->cvar_namespaces, error);
    if (ok) ok = frontend_config_host_cvars_set_entry(&lease->namespaces, lease, namespace_entered, error);
    if (ok) {
        lease->keys = configuration_view.keys;
        ok = lease->keys && frontend_key_profile_state(lease->keys) && frontend_key_profile_retain(lease->keys, error);
        if (!ok) lease->keys = NULL;
    }
    if (ok) ok = frontend_network_presentation_services(owner->row->frontend,
        &resources.domain.source.receiver, &lease->network, error);
    qa_q3_presentation_options backend = {.assets = resources.assets, .audio = owner->row->frontend->audio,
        .clock = {lease, milliseconds}, .seat = resources.physical_seat, .owner = lease->service_owner,
        .viewport = {0, 0, owner->row->frontend->width, owner->row->frontend->height},
        .near_clip = 4, .far_clip = 16384, .identity_light = 1, .lod_scale = 5,
        .rail_core_width = 6, .rail_ring_width = 16, .rail_segment_length = 32,
        .context = lease, .audio_actor = actor, .listener = listener, .music = music,
        .frame_number = frame_number, .milliseconds = source_milliseconds, .audio_bus = audio_bus,
        .prepare_view = prepare_view, .submit_view = submit_view, .scene_cleared = scene_cleared, .remap = remap, .print = print};
    if (ok) ok = qa_q3_presentation_create(&backend, &lease->presentation, error);
    qa_bsp_view bsp;
    if (ok) ok = qa_bsp_open(qa_resource_bytes(resources.map), &bsp, error) &&
        qa_q3_presentation_world(lease->presentation, resources.world, resources.geometry,
            bsp.lumps[QA_BSP_ENTITIES].bytes, error) &&
        qa_q3_presentation_frame(lease->presentation, &owner->row->frontend->frame, backend.viewport, error);
    if (ok && request->role == QA_QVM_CGAME) {
        frontend_equipment_source_options equipment = {.frontend = owner->row->frontend,
            .receiver = host->owner, .seat = request->source->receiver.seat, .physical_seat = resources.physical_seat,
            .assets = resources.assets, .presentation = lease->presentation, .lease = lease,
            .borrow = equipment_borrow, .current = equipment_current, .release = equipment_return,
            .requests_context = lease, .requests = equipment_requests};
        ok = request->equipment_services && frontend_equipment_source_create(&equipment, &lease->equipment, error);
        if (ok) frontend_equipment_source_services(lease->equipment, request->equipment_services);
    }
    if (!ok) return false;
    host->client = lease->network; host->keys = frontend_key_profile_state(lease->keys); host->seat = resources.input;
    host->game_directory = frontend_key_profile_game_directory(lease->keys);
    host->input = (qa_q3_host_input_services){&lease->namespaces, frontend_config_host_bindings};
    host->cvar_entry = (qa_q3_host_cvar_entry_services){&lease->namespaces, frontend_config_host_cvar_entered};
    host->console_field = qa_seat_console_field(owner->row->frontend->seats[resources.physical_seat].console, false);
    host->scene_resources = resources.images; host->scene_world = resources.world; host->scene_frame = &owner->row->frontend->frame;
    host->sound_bank = resources.sounds; host->collision = (qa_q3_host_collision_services){lease, geometry, load_map};
    host->presentation = (qa_q3_host_presentation_services){lease, lease->presentation, resources.fonts, configuration, update_screen};
    host->render = (qa_q3_host_render_services){lease, render_enter, render_leave};
    host->common = (qa_q3_host_common_services){.context = lease, .print = print, .milliseconds = common_milliseconds,
        .calendar = calendar, .arguments = arguments, .client_command = client_command, .clipboard = clipboard};
    return true;
}

bool frontend_remote_q3_modules_create(frontend_remote_q3 *row, frontend_remote_q3_modules **out, qa_error *error)
{
    frontend_remote_q3_resources resources;
    if (!row || !out || *out || !frontend_remote_q3_resources_read(row, &resources, error)) return false;
    frontend_remote_q3_modules *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining acquired remote CLIENT child");
    owner->row = row; owner->resources = resources; *out = owner;
    if (!frontend_remote_q3_modules_attach(row, owner, error)) return false;
    owner->attached = true; owner->constructing = true;
    qa_application_native_q3_client_modules_options options = {.source = resources.domain.source,
        .gamestate = resources.domain.gamestate, .context = owner, .current = current, .prepare = prepare};
    bool ok = qa_application_native_q3_client_modules_create(row->application, &options, &owner->modules, error);
    owner->constructing = false; return ok;
}
frontend_remote_q3 *frontend_remote_q3_modules_parent(const frontend_remote_q3_modules *owner)
{ return owner ? owner->row : NULL; }
application_native_q3_client_modules *frontend_remote_q3_modules_owner(const frontend_remote_q3_modules *owner)
{ return owner ? owner->modules : NULL; }
bool frontend_remote_q3_modules_idle(const frontend_remote_q3_modules *owner)
{
    if (!owner) return true;
    if (owner->constructing || (owner->modules && !qa_application_native_q3_client_modules_idle(owner->modules))) return false;
    for (const remote_module_lease *lease = owner->leases; lease; lease = lease->next)
        if (lease->callbacks || lease->render_definition || !frontend_equipment_source_idle(lease->equipment) ||
            (lease->presentation && !qa_q3_presentation_idle(lease->presentation))) return false;
    return true;
}
bool frontend_remote_q3_modules_retired(const frontend_remote_q3_modules *owner)
{ return owner && !owner->constructing && !owner->modules && !owner->leases; }
bool frontend_remote_q3_modules_destroy(frontend_remote_q3_modules **owned, qa_error *error)
{
    if (!owned || !*owned) return true;
    frontend_remote_q3_modules *owner = *owned; qa_frontend *f = owner->row->frontend;
    if (f->capture || !frontend_remote_q3_modules_idle(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module retirement retains an actual entered child");
    owner->retiring = true;
    if (!qa_application_native_q3_client_modules_destroy(&owner->modules, error)) return false;
    while (owner->leases) {
        remote_module_lease *lease = owner->leases;
        if (!lease->released || lease->callbacks || !frontend_equipment_source_destroy(lease->equipment, error)) return false;
        lease->equipment = NULL;
        if (lease->presentation && !qa_q3_presentation_destroy(lease->presentation, error)) return false;
        lease->presentation = NULL;
        if (f->audio) {
            if (!qa_audio_engine_stop_owner(f->audio, lease->service_owner, owner->resources.physical_seat, error)) return false;
            if (lease->music_attached) {
                qa_audio_engine_remove_music(f->audio, lease->service_owner); lease->music = NULL; lease->music_attached = false;
            }
        }
        qa_audio_music_destroy(lease->music); lease->music = NULL;
        free(lease->music_intro); free(lease->music_loop); lease->music_intro = lease->music_loop = NULL;
        if (lease->keys && !frontend_key_profile_release(lease->keys, error)) return false;
        lease->keys = NULL;
        if (!frontend_client_registry_release(&lease->registry, error)) return false;
        owner->leases = lease->next; free(lease);
    }
    if (owner->attached && !frontend_remote_q3_modules_detach(owner->row, owner, error)) return false;
    free(owner); *owned = NULL; return true;
}
bool frontend_remote_q3_modules_media_read(const frontend_remote_q3_modules *owner, qa_qvm_role role,
    frontend_remote_q3_module_media *out, qa_error *error)
{
    if (!owner || !out || owner->retiring || !owner->modules) return false;
    qa_application_q3_role_receipt receipt;
    if (!qa_application_native_q3_client_modules_receipt_read(owner->modules, role, &receipt, error)) return false;
    for (const remote_module_lease *lease = owner->leases; lease; lease = lease->next)
        if (lease->role == role && lease->service_owner == receipt.service_owner && !lease->released && lease->presentation) {
            frontend_remote_q3_resources resources;
            if (!frontend_remote_q3_resources_read(owner->row, &resources, error)) return false;
            *out = (frontend_remote_q3_module_media){owner, receipt, lease->presentation, resources.assets,
                resources.mounts, resources.physical_seat}; return true;
        }
    return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module media lost its initialized host lease");
}
bool frontend_remote_q3_modules_media_current(const frontend_remote_q3_module_media *view)
{
    frontend_remote_q3_module_media actual;
    return view && frontend_remote_q3_modules_media_read(view->owner, view->receipt.role, &actual, NULL) &&
        qa_application_native_q3_client_modules_receipt_current(view->owner->modules, &view->receipt) &&
        actual.presentation == view->presentation && actual.assets == view->assets &&
        actual.mounts == view->mounts && actual.physical_seat == view->physical_seat;
}
bool frontend_remote_q3_modules_listener_begin(frontend_remote_q3_modules *owner,
    qa_qvm_role role, qa_error *error)
{
    frontend_remote_q3_module_media media;
    if (!frontend_remote_q3_modules_idle(owner) ||
        !frontend_remote_q3_modules_media_read(owner, role, &media, error)) return false;
    for (remote_module_lease *lease = owner->leases; lease; lease = lease->next)
        if (lease->role == role && lease->service_owner == media.receipt.service_owner && !lease->released) {
            lease->has_listener = false; return true;
        }
    return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote Draw lost its actual listener lease");
}
bool frontend_remote_q3_modules_listener_read(const frontend_remote_q3_modules *owner,
    qa_qvm_role role, qa_audio_listener *out, bool *present, qa_error *error)
{
    frontend_remote_q3_module_media media;
    if (!out || !present || !frontend_remote_q3_modules_idle(owner) ||
        !frontend_remote_q3_modules_media_read(owner, role, &media, error)) return false;
    *present = false;
    for (const remote_module_lease *lease = owner->leases; lease; lease = lease->next)
        if (lease->role == role && lease->service_owner == media.receipt.service_owner && !lease->released) {
            *present = lease->has_listener;
            if (*present) *out = lease->listener;
            return true;
        }
    return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote listener lost its actual initialized host lease");
}
