#include "internal.h"
#include "qa/binary.h"
#include "qa/q3_key.h"
#include "qa/audio_save.h"
#include <limits.h>
#include <stdio.h>

typedef struct frontend_source_lease frontend_source_lease;
struct frontend_source {
    frontend_source *next;
    qa_frontend *frontend;
    qa_application *application;
    qa_actor_owner owner;
    uint32_t seat;
    uint64_t identity;
    unsigned leases;
    frontend_source_lease *lease_list;
    qa_vfs *mounts;
    const qa_vfs *source_files;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_audio_music *music;
    qa_media_library *movies;
    qa_q3_key *keys;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    qa_audio_listener listener;
    bool has_listener, music_attached;
};
struct frontend_source_lease {
    frontend_source_lease *next;
    frontend_source *source;
    qa_qvm_role role;
    qa_q3_host_common_services common;
    qa_console *console;
    qa_command_context command;
};

static double milliseconds(void *context)
{
    frontend_source *source = context;
    return (double)source->frontend->time_ns / 1000000.0;
}
static int32_t source_milliseconds(void *context) { return (int32_t)((uint64_t)milliseconds(context) & INT32_MAX); }
static int32_t frame_number(void *context)
{
    return (int32_t)(((frontend_source *)context)->frontend->frame_number & INT32_MAX);
}
static uint64_t audio_bus(void *context) { return ((frontend_source *)context)->identity; }
static void print_source(void *context, const char *text)
{
    frontend_source *source = context;
    qa_command_context command = {.origin = QA_COMMAND_SEAT, .seat = source->seat, .owner = source->owner};
    frontend_console_print(source->frontend, &command, text);
}
static bool source_remap(void *context, const char *original, const char *replacement, float offset, qa_error *error)
{
    return frontend_shader_remap(((frontend_source *)context)->frontend, original, replacement, offset, error);
}
static bool source_actor(void *context, int32_t number, uint64_t *out, qa_error *error)
{
    frontend_source *source = context;
    if (number < 0) { *out = QA_AUDIO_NO_ACTOR; return true; }
    qa_actor_id actor = {0};
    qa_error observed = {0};
    if (!qa_application_guest_source_actor(source->application, source->owner,
            source->seat, number, &actor, &observed)) {
        if (observed.code == QA_ERROR_NOT_FOUND) { *out = QA_AUDIO_NO_ACTOR; return true; }
        if (error) *error = observed;
        return false;
    }
    *out = frontend_audio_actor(source->frontend, actor, error);
    return *out != QA_AUDIO_NO_ACTOR;
}
static bool listener(void *context, const qa_audio_listener *value, qa_error *error)
{
    frontend_source *source = context; (void)error;
    source->listener = *value;
    source->listener.gain = 1.0f / (float)source->frontend->options.seats;
    source->has_listener = true;
    return true;
}
static bool music(void *context, const char *intro_name, const char *loop_name, qa_error *error)
{
    frontend_source *source = context;
    if (!source->frontend->audio) return frontend_fail(error, QA_ERROR_UNSUPPORTED, "source music output is disabled");
    if (source->music_attached) {
        source->music = qa_audio_engine_bus_music(source->frontend->audio, source->identity);
        source->music_attached = source->music != NULL;
    }
    if (!source->music && !qa_audio_music_create(48000, QA_AUDIO_Q3, false, &source->music, error)) return false;
    if (!intro_name || !*intro_name) { qa_audio_music_stop(source->music); return true; }
    qa_audio_stream *intro = NULL, *loop = NULL;
    if (!qa_audio_bank_music(source->sounds, intro_name, NULL, NULL, &intro, error)) return false;
    if (loop_name && *loop_name) {
        if (!strcmp(intro_name, loop_name)) loop = intro;
        else if (!qa_audio_bank_music(source->sounds, loop_name, NULL, NULL, &loop, error)) {
            qa_audio_stream_close(intro); return false;
        }
    }
    qa_audio_music_start(source->music, intro, loop);
    bool ok = qa_audio_engine_music(source->frontend->audio, source->identity, source->seat, 1, source->music, error);
    if (ok) source->music_attached = true;
    return ok;
}
static bool prepare_view(void *context, const qa_q3_refdef *definition, qa_q3_scene_options *options, qa_error *error)
{
    frontend_source *source = context;
    qa_application_map_view map;
    if (qa_application_map_read(source->application, &map)) {
        const qa_product *product = qa_catalog_product(qa_application_catalog(source->application), map.geometry);
        options->world_family = product && product->family == QA_GAME_Q2 ? QA_SCENE_Q2 :
            product && product->family == QA_GAME_Q3 ? QA_SCENE_Q3 : QA_SCENE_Q1;
    }
    options->split_screen = source->frontend->options.seats > 1;
    (void)definition;
    return frontend_tools_camera(source->frontend, source->seat, options->world.view.clip_enabled, &options->world.view, error) &&
        (options->world.no_world || frontend_event_world(source->frontend, source->seat, &options->world, error));
}
static bool submit_view(void *context, const qa_q3_scene_options *options, qa_scene_frame *frame, qa_error *error)
{
    frontend_source *source = context;
    if (options->world.no_world) return true;
    return frontend_visuals_submit(source->frontend, source->seat, source->owner, &options->world, frame, error) &&
        frontend_particle_draw(source->frontend, &options->world.view, error) &&
        frontend_event_debug(source->frontend, &options->world.view, error) &&
        frontend_tools_debug(source->frontend, &options->world.view, error);
}
static void float_word(uint8_t *out, float value)
{
    uint32_t bits; memcpy(&bits, &value, sizeof(bits)); qa_store_u32le(out, bits);
}
static bool configuration(void *context, uint8_t out[11332], qa_error *error)
{
    frontend_source *source = context;
    qa_frontend *frontend = source->frontend;
    qa_display_info display;
    if (!qa_display_info_get(frontend->display, &display, error)) return false;
    memset(out, 0, 11332);
    const qa_gl_capabilities *caps = qa_gl_capabilities_get(frontend->gl);
    snprintf((char *)out, 1024, "%s", caps ? caps->renderer : "Quake Anthology CPU renderer");
    snprintf((char *)out + 1024, 1024, "%s", caps ? caps->vendor : "Quake Anthology");
    snprintf((char *)out + 2048, 1024, "%s", caps ? caps->version : "retained scene renderer");
    /* CPU textures use unsigned dimensions; the source ABI advertises the
     * signed representable limit. Its color output is RGBA8 and depth float. */
    qa_store_u32le(out + 11264, caps ? caps->maximum_texture_size : INT32_MAX);
    qa_store_u32le(out + 11268, caps ? caps->texture_units : 1);
    qa_store_u32le(out + 11272, caps ? caps->color_bits : 32);
    qa_store_u32le(out + 11276, caps ? caps->depth_bits : 64);
    qa_store_u32le(out + 11280, caps ? caps->stencil_bits : 8);
    qa_store_u32le(out + 11304, display.drawable_width);
    qa_store_u32le(out + 11308, display.drawable_height);
    float_word(out + 11312, display.drawable_height ? (float)display.drawable_width / (float)display.drawable_height : 1);
    qa_store_u32le(out + 11316, display.refresh_rate > 0 ? (uint32_t)display.refresh_rate : 0);
    qa_store_u32le(out + 11320, display.fullscreen != QA_DISPLAY_WINDOWED);
    qa_store_u32le(out + 11324, caps && caps->stereo);
    return true;
}
static bool update_screen(void *context, qa_error *error)
{
    qa_frontend *frontend = ((frontend_source *)context)->frontend;
    if (frontend->cpu) return qa_cpu_execute(frontend->cpu, &frontend->frame, error) && qa_cpu_present_frame(frontend->cpu, error);
    return qa_gl_execute(frontend->gl, &frontend->frame, error) && qa_gl_finish(frontend->gl, error) && qa_display_swap(frontend->display, error);
}
static void common_print(void *context, const char *text)
{
    frontend_source_lease *lease = context;
    qa_console *console = lease->console;
    qa_console *shared = qa_application_console(lease->source->application);
    if (console && qa_console_output_redirected(console)) qa_console_emit(console, &lease->command, text);
    else if (shared && qa_console_output_redirected(shared)) qa_console_emit(shared, &lease->command, text);
    else print_source(lease->source, text);
}
static uint32_t common_milliseconds(void *context)
{
    frontend_source_lease *lease = context;
    if (frontend_network_remote(lease->source->frontend))
        return (uint32_t)((lease->source->frontend->time_ns / UINT64_C(1000000)) & UINT32_MAX);
    return lease->common.milliseconds ? lease->common.milliseconds(lease->common.context) :
        (uint32_t)((uint64_t)milliseconds(lease->source) & UINT32_MAX);
}
static int32_t common_calendar(void *context, qa_q3_host_calendar *out)
{
    frontend_source_lease *lease = context;
    return lease->common.calendar ? lease->common.calendar(lease->common.context, out) : 0;
}
static bool common_arguments(void *context, qa_native_host_command_view *out, qa_error *error)
{
    frontend_source_lease *lease = context;
    return lease->common.arguments ? lease->common.arguments(lease->common.context, out, error) :
        frontend_fail(error, QA_ERROR_UNSUPPORTED, "source argument owner is unavailable");
}
static bool common_command(void *context, const char *text, qa_error *error)
{
    frontend_source_lease *lease = context;
    if (frontend_network_remote(lease->source->frontend))
        return frontend_network_client_command(lease->source->frontend, text, error);
    return lease->common.client_command ? lease->common.client_command(lease->common.context, text, error) :
        frontend_fail(error, QA_ERROR_UNSUPPORTED, "source client command route is unavailable");
}
static bool common_mods(void *context, qa_vfs_listing *out, qa_error *error)
{
    frontend_source_lease *lease = context;
    return lease->common.installed_mods ? lease->common.installed_mods(lease->common.context, out, error) :
        frontend_fail(error, QA_ERROR_UNSUPPORTED, "source installed mod listing owner is unavailable");
}
static bool common_clipboard(void *context, qa_buffer *out, qa_error *error)
{
    (void)context;
    char *text = SDL_GetClipboardText();
    if (!text) { qa_error_set(error, QA_ERROR_IO, 0, "reading clipboard: %s", SDL_GetError()); return false; }
    size_t length = strlen(text); uint8_t *copy = malloc(length + 1);
    if (!copy) { SDL_free(text); return frontend_fail(error, QA_ERROR_MEMORY, "retaining clipboard text"); }
    memcpy(copy, text, length + 1); SDL_free(text); *out = (qa_buffer){copy, length}; return true;
}
static void source_free(frontend_source *source)
{
    if (!source) return;
    qa_frontend *frontend = source->frontend;
    qa_error error = {0};
    if (source->presentation && !qa_q3_presentation_destroy(source->presentation, &error))
        fprintf(stderr, "source presentation retirement: %s\n", error.message);
    if (frontend->audio) {
        qa_audio_engine_remove_music(frontend->audio, source->identity);
        if (!qa_audio_engine_stop_owner(frontend->audio, source->identity, source->seat, &error))
            fprintf(stderr, "source audio retirement: %s\n", error.message);
    }
    qa_q3_presentation_assets_destroy(source->assets);
    qa_q3_key_destroy(source->keys);
    if (!source->music_attached) qa_audio_music_destroy(source->music);
    qa_media_library_destroy(source->movies);
    qa_font_library_destroy(source->fonts);
    qa_audio_bank_destroy(source->sounds);
    qa_material_library_destroy(source->materials);
    qa_scene_resources_destroy(source->images);
    qa_vfs_destroy(source->mounts); free(source);
}
static void release_source(void *context)
{
    frontend_source_lease *lease = context; frontend_source *source = lease->source;
    frontend_source_lease **held=&source->lease_list;
    while (*held && *held!=lease) held=&(*held)->next;
    if (*held) *held=lease->next;
    free(lease);
    if (--source->leases) return;
    frontend_source **link = &source->frontend->sources;
    while (*link && *link != source) link = &(*link)->next;
    if (*link) *link = source->next;
    source_free(source);
}
static bool create_source(qa_frontend *frontend, qa_application *application, qa_actor_owner owner,
    uint32_t seat, const qa_q3_host_options *host, frontend_source **out, qa_error *error)
{
    if (frontend->next_source_id == UINT64_MAX - QA_FRONTEND_COMMAND_OWNER - 1)
        return frontend_fail(error, QA_ERROR_MEMORY, "source frontend identity exhausted");
    frontend_source *source = calloc(1, sizeof(*source));
    if (!source) return frontend_fail(error, QA_ERROR_MEMORY, "allocating source presentation owner");
    source->frontend = frontend; source->application = application; source->owner = owner; source->seat = seat;
    source->identity = QA_FRONTEND_COMMAND_OWNER + ++frontend->next_source_id;
    source->source_files = host->mounts;
    source->mounts = qa_vfs_clone(host->mounts, error);
    source->images = source->mounts ? qa_scene_resources_create(source->mounts, error) : NULL;
    source->materials = source->images ? qa_material_library_create(source->images, frontend->order, error) : NULL;
    source->fonts = source->images ? qa_font_library_create(source->mounts, source->images, error) : NULL;
    source->movies = source->images ? qa_media_library_create(source->images, error) : NULL;
    qa_scene_image_options images = {.family = QA_SCENE_Q3, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true, .transparent_index = -1};
    bool ok = source->mounts && source->images && source->materials && source->fonts && source->movies &&
        qa_material_library_load_scripts(source->materials, source->mounts, &images, error) &&
        qa_audio_bank_create(source->mounts, &source->sounds, error) &&
        qa_q3_key_create(host->cvars, false, &source->keys, error);
    if (ok && frontend->audio) ok = qa_audio_music_create(48000, QA_AUDIO_Q3, false, &source->music, error);
    if (ok) ok = frontend_material_remaps(frontend, source->materials, error);
    qa_q3_presentation_asset_options assets = {.provider = {source->mounts, source->images, source->materials, QA_SCENE_Q3},
        .sounds = source->sounds, .movies = source->movies, .context = source, .print = print_source};
    if (ok) ok = qa_q3_presentation_assets_create(&assets, &source->assets, error);
    qa_q3_presentation_options presentation = {.assets = source->assets, .audio = frontend->audio,
        .clock = {source, milliseconds}, .seat = seat, .owner = source->identity,
        .viewport = {0, 0, frontend->width, frontend->height}, .near_clip = 4, .far_clip = 16384,
        .identity_light = 1, .lod_scale = 5, .rail_core_width = 6, .rail_ring_width = 16, .rail_segment_length = 32,
        .context = source, .audio_actor = source_actor, .listener = listener, .music = music,
        .frame_number = frame_number, .milliseconds = source_milliseconds, .audio_bus = audio_bus,
        .prepare_view = prepare_view, .submit_view = submit_view, .remap = source_remap, .print = print_source};
    if (ok) ok = qa_q3_presentation_create(&presentation, &source->presentation, error);
    if (!ok) { source_free(source); return false; }
    source->next = frontend->sources; frontend->sources = source; *out = source;
    return true;
}
bool frontend_source_services(void *context, qa_application *application, qa_actor_owner owner,
    qa_qvm_role role, uint32_t seat, qa_q3_host_options *host, qa_error *error)
{
    qa_frontend *frontend = context;
    if (role == QA_QVM_GAME) return frontend_network_source_services(frontend, host, error);
    if (frontend->options.dedicated || seat >= frontend->options.seats)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "source client presentation requires an active local seat");
    if (!frontend_scene_sync(frontend, error) ||
        !frontend_network_client_services(frontend, owner, role, seat, host, error)) return false;
    frontend_source *source = frontend->sources;
    while (source && (source->owner != owner || source->seat != seat || source->source_files != host->mounts)) source = source->next;
    bool created = source == NULL;
    if (created && !create_source(frontend, application, owner, seat, host, &source, error)) return false;
    frontend_source_lease *lease = malloc(sizeof(*lease));
    if (!lease || source->leases == UINT_MAX) {
        free(lease);
        if (created) { frontend->sources = source->next; source_free(source); }
        return frontend_fail(error, QA_ERROR_MEMORY, "retaining source frontend lease");
    }
    *lease = (frontend_source_lease){.next=source->lease_list,.source = source,.role=role,.common = host->common,
        .console = host->console, .command = host->command_context}; source->lease_list=lease; ++source->leases;
    host->frontend_lifetime = lease; host->release_frontend = release_source;
    host->seat = frontend->seats[seat].input;
    host->console_field = qa_seat_console_field(frontend->seats[seat].console, false);
    host->keys = source->keys;
    host->scene_resources = source->images; host->scene_frame = &frontend->frame;
    host->scene_world = frontend->scene_world; host->sound_bank = source->sounds;
    host->presentation = (qa_q3_host_presentation_services){source, source->presentation,
        source->fonts, configuration, update_screen};
    host->common = (qa_q3_host_common_services){lease, common_print, common_milliseconds,
        common_calendar, host->common.arguments ? common_arguments : NULL,
        (host->common.client_command || frontend_network_remote(frontend)) ? common_command : NULL, host->common.installed_mods ? common_mods : NULL,
        common_clipboard};
    return frontend_source_publish_world(frontend, error) &&
        qa_q3_presentation_frame(source->presentation, &frontend->frame,
            (qa_scene_rect){0, 0, frontend->width, frontend->height}, error);
}
bool frontend_source_remap(qa_frontend *frontend, const char *original, const char *replacement, float offset, qa_error *error)
{
    for (frontend_source *source = frontend->sources; source; source = source->next)
        if (!qa_material_remap(source->materials, original, replacement, offset, error)) return false;
    return true;
}
qa_q3_presentation_assets *frontend_source_assets(qa_frontend *frontend, const qa_command_context *context)
{
    if (!frontend || !context) return NULL;
    qa_actor_owner owner = (qa_actor_owner)context->owner;
    if (!context->owner) {
        qa_application_presentation_view active;
        if (!qa_application_presentation_read(frontend->application, context->seat, &active)) return NULL;
        owner = active.source_hud ? active.hud : active.source_menu ? active.menu : 0;
    } else if (context->owner > UINT32_MAX) return NULL;
    if (!owner) return NULL;
    qa_command_context captured = *context;
    captured.owner = owner;
    qa_vfs *files = qa_application_context_files(frontend->application, &captured, NULL);
    if (!files) return NULL;
    for (frontend_source *source = frontend->sources; source; source = source->next)
        if (source->owner == owner && source->seat == context->seat && source->source_files == files) return source->assets;
    return NULL;
}
const qa_scene_resources *frontend_source_images_at(qa_frontend *frontend, size_t index)
{
    if (!frontend) return NULL;
    frontend_source *source = frontend->sources;
    while (source && index) { source = source->next; --index; }
    return source ? source->images : NULL;
}
bool frontend_source_rebind_ready(const qa_frontend *owned, const qa_frontend *destination, qa_error *error)
{
    if (!owned || !destination || owned->stepping || destination->stepping || !owned->application)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "source lease publication requires idle frontend owners");
    if (!qa_application_guest_context_rebind_ready(owned->application, &owned->frame, error)) return false;
    for (const frontend_source *source = owned->sources; source; source = source->next) {
        if (source->frontend != owned || source->application != owned->application || !source->leases ||
            !source->identity || source->seat >= owned->options.seats || !source->source_files ||
            !qa_application_provider_instance(owned->application, source->owner))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "source lease belongs to another frontend publication");
        size_t held=0;
        for (const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
            if (lease->source!=source || (lease->role!=QA_QVM_CGAME && lease->role!=QA_QVM_UI))
                return frontend_fail(error,QA_ERROR_ARGUMENT,"source role lease does not match its installed owner");
            ++held;
        }
        if (held!=source->leases) return frontend_fail(error,QA_ERROR_ARGUMENT,"source lease list differs from active ownership");
        if (!qa_q3_presentation_frontend_rebind_ready(source->presentation, &owned->frame,
                owned->audio, source->identity, error)) return false;
    }
    return true;
}
void frontend_source_rebind(qa_frontend *owned, qa_frontend *destination)
{
    for (frontend_source *source = owned->sources; source; source = source->next) {
        qa_q3_presentation_frontend_rebind(source->presentation, &owned->frame, &destination->frame,
            owned->audio, source->identity);
        if (source->music_attached)
            source->music = qa_audio_engine_bus_music(owned->audio, source->identity);
        source->frontend = destination;
    }
}
bool frontend_source_retire_world(qa_frontend *frontend, qa_error *error)
{
    for (frontend_source *source = frontend->sources; source; source = source->next) {
        if (!qa_q3_presentation_retire_world(source->presentation, error)) return false;
        if (source->music_attached) {
            qa_audio_engine_remove_music(frontend->audio, source->identity);
            source->music = NULL; source->music_attached = false;
        } else if (source->music) qa_audio_music_stop(source->music);
        source->has_listener = false;
    }
    return true;
}
bool frontend_source_publish_world(qa_frontend *frontend, qa_error *error)
{
    qa_bsp_view bsp;
    if (!frontend->scene_world || !frontend->map_resource) return true;
    if (!qa_bsp_open(qa_resource_bytes(frontend->map_resource), &bsp, error)) return false;
    for (frontend_source *source = frontend->sources; source; source = source->next)
        if (!qa_q3_presentation_world(source->presentation, frontend->scene_world,
            qa_world_geometry(qa_application_world(source->application)), bsp.lumps[QA_BSP_ENTITIES].bytes, error)) return false;
    return true;
}
bool frontend_source_frame(qa_frontend *frontend, uint32_t seat, qa_scene_rect rect, qa_error *error)
{
    for (frontend_source *source = frontend->sources; source; source = source->next)
        if (source->seat == seat) {
            source->has_listener = false;
            if (!qa_q3_presentation_frame(source->presentation, &frontend->frame, rect, error)) return false;
        }
    return true;
}
bool frontend_source_listener(qa_frontend *frontend, uint32_t seat, qa_audio_listener *out)
{
    for (frontend_source *source = frontend->sources; source; source = source->next)
        if (source->seat == seat && source->has_listener) { *out = source->listener; return true; }
    return false;
}
bool frontend_before_world_change(void *context, qa_application *application, qa_error *error)
{
    qa_frontend *frontend = context; (void)application;
    return frontend_tools_before_world_change(frontend, error);
}
bool frontend_world_change_ready(void *context, qa_application *application, qa_error *error)
{
    qa_frontend *frontend = context; (void)application;
    return frontend_tools_world_change_ready(frontend, error) && frontend_network_world_change_ready(frontend, error);
}
bool frontend_world_retired(void *context, qa_application *application, qa_error *error)
{
    qa_frontend *frontend = context; (void)application;
    qa_scene_frame_reset(&frontend->frame, frontend->frame_number);
    frontend_native_q2_retire_world(frontend);
    for (unsigned i = 0; i < frontend->options.seats; ++i) frontend_player_retire(&frontend->seats[i]);
    if (!frontend_shader_retire(frontend, error)) return false;
    frontend_visuals_destroy(frontend);
    if (!frontend_source_retire_world(frontend, error)) return false;
    frontend_particle_retire(frontend);
    frontend_event_retire(frontend);
    frontend->audio_id_count = 0;
    frontend->silent_audio_remainder = 0;
    return !frontend->audio || qa_audio_engine_reset_round(frontend->audio, error);
}

size_t frontend_source_group_count(const qa_frontend *frontend)
{
    size_t count=0; if (frontend) for (const frontend_source *source=frontend->sources;source;source=source->next) ++count;
    return count;
}
bool frontend_source_group_read(const qa_frontend *frontend, size_t index, frontend_source_group_view *out)
{
    if (!frontend || !out || frontend->stepping) return false;
    const frontend_source *source=frontend->sources;
    while (source && index--) source=source->next;
    if (!source || source->frontend!=frontend || source->application!=frontend->application) return false;
    frontend_source_group_view view={.owner=source->owner,.seat=source->seat,.identity=source->identity,
        .source_files=source->source_files,.mounts=source->mounts,.images=source->images,.materials=source->materials,
        .fonts=source->fonts,.sounds=source->sounds,.movies=source->movies,.keys=source->keys,
        .assets=source->assets,.presentation=source->presentation,.listener=source->listener,
        .has_listener=source->has_listener,.music_attached=source->music_attached};
    unsigned held=0;
    for (const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
        if (lease->source!=source || (lease->role!=QA_QVM_CGAME && lease->role!=QA_QVM_UI) || held==UINT_MAX) return false;
        ++view.roles[lease->role]; ++held;
    }
    if (held!=source->leases) return false;
    view.music=source->music_attached?qa_audio_engine_bus_music(frontend->audio,source->identity):source->music;
    *out=view; return true;
}
