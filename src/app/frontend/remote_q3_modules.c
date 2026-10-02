#include "remote_q3_modules_private.h"
#include "remote_q3_video_media.h"
#include "equipment_source.h"
#include "network_browser.h"
#include "system_cinematic.h"
#include "network_restore_attempt.h"
#include "shared_render_controls.h"
#include "q3_render_policy.h"
#include "q3_color_policy.h"
#include "music_sources.h"
#include "material_movies.h"
#include "qa/audio_music_prepare.h"
#include "qa/catalog.h"
#include "qa/catalog_write.h"
#include "qa/binary.h"
#include "qa/bsp.h"
#include "qa/q3_presentation_save.h"
#include "qa/material_source_scratch.h"
#include <SDL.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>

const qa_application_q3_remote_source *frontend_remote_modules_source(const frontend_remote_q3_modules *owner)
{ return owner->kind == REMOTE_MODULE_INITIAL ? &owner->basis.initial.view.attempt.source : &owner->basis.decoded.view.domain.source; }
static const qa_application_q3_remote_source *source(const frontend_remote_q3_modules *owner)
{ return frontend_remote_modules_source(owner); }
static uint32_t physical_seat(const frontend_remote_q3_modules *owner)
{ return owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.physical_seat : owner->basis.decoded.view.physical_seat; }
static frontend_client_registry *registry(const frontend_remote_q3_modules *owner)
{ return owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.registry : owner->basis.decoded.view.registry; }
static qa_q3_presentation_assets *assets(const frontend_remote_q3_modules *owner)
{ return owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.assets : owner->basis.decoded.view.assets; }
static qa_audio_bank *sounds(const frontend_remote_q3_modules *owner)
{ return owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.sounds : owner->basis.decoded.view.sounds; }
static qa_material_library *materials(const frontend_remote_q3_modules *owner)
{ return owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.materials : owner->basis.decoded.view.materials; }
static qa_scene_resources *images(const frontend_remote_q3_modules *owner)
{ return owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.images : owner->basis.decoded.view.images; }
static qa_font_library *fonts(const frontend_remote_q3_modules *owner)
{ return owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.fonts : owner->basis.decoded.view.fonts; }
static qa_vfs *mounts(const frontend_remote_q3_modules *owner)
{ return owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.mounts : owner->basis.decoded.view.mounts; }
static qa_media_library *movies(const frontend_remote_q3_modules *owner)
{ return owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.movies : owner->basis.decoded.view.movies; }
static bool cinematic_parent(const remote_module_lease *lease,
    qa_q3_cinematic_source **out, qa_error *error)
{
    frontend_material_movies *provider = NULL;
    if (!frontend_material_movies_library_owner(materials(lease->owner), &provider, error) ||
        !frontend_material_movies_cinematic_read(provider, out, error)) return false;
    return *out || frontend_fail(error, QA_ERROR_ARGUMENT,
        "Acquired cinematic role requires its actual numeric movie provider");
}
static bool cinematic_current(const remote_module_lease *lease, qa_error *error)
{
    if (!lease->cinematics) return lease->owner->restoring || lease->legacy_cinematics;
    qa_q3_cinematic_source *actual = NULL;
    const qa_q3_cinematic_source *parent = NULL;
    uint32_t seat = 0; uint64_t bus = 0;
    return cinematic_parent(lease, &actual, error) &&
        qa_q3_cinematic_source_role_read(lease->cinematics, &parent, &seat, &bus) &&
        parent == actual && seat == physical_seat(lease->owner) && bus == lease->service_owner &&
        qa_q3_cinematic_source_handles(lease->cinematics) == lease->owner->frontend->source_cinematics;
}
static uint64_t identity(const frontend_remote_q3_modules *owner)
{ return owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.identity : owner->basis.decoded.view.identity; }
static qa_input_seat *input(const frontend_remote_q3_modules *owner)
{ return owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.view.input : owner->basis.decoded.view.input; }
static bool attached(const frontend_remote_q3_modules *owner)
{
    return owner && owner->attached && (owner->kind == REMOTE_MODULE_INITIAL ?
        owner->basis.initial.owner != NULL : frontend_remote_q3_modules_read(owner->basis.decoded.row) == owner);
}

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
static bool basis_current(const frontend_remote_q3_modules *owner, const qa_application_q3_remote_source *source,
    const qa_q3_gamestate *gamestate, qa_error *error)
{
    if (!owner || owner->retiring || !attached(owner)) return false;
    if (owner->kind == REMOTE_MODULE_INITIAL) {
        frontend_remote_q3_initial_view view;
        uint64_t generation; bool complete;
        return !gamestate && (owner->video ?
            frontend_remote_q3_initial_video_read(owner->basis.initial.owner, owner, &view, &generation, &complete, error) :
            frontend_remote_q3_initial_read(owner->basis.initial.owner, &view, error)) &&
            same_source(source, &view.attempt.source);
    }
    frontend_remote_q3_resources resources;
    uint64_t generation; bool complete;
    return (owner->video ?
        frontend_remote_q3_resources_video_read(owner->basis.decoded.row, owner, &resources, &generation, &complete, error) :
        frontend_remote_q3_resources_read(owner->basis.decoded.row, &resources, error)) &&
        same_source(source, &resources.domain.source) && gamestate == resources.domain.gamestate;
}
static bool current(void *context, const qa_application_q3_remote_source *source,
    const qa_q3_gamestate *gamestate, qa_error *error)
{ return basis_current(context, source, gamestate, error); }
static bool entered(const remote_module_lease *lease, qa_q3_host **host,
    qa_q3_host_client_context *out, qa_error *error)
{
    const frontend_remote_q3_modules *owner = lease ? lease->owner : NULL;
    qa_q3_host_client_context actual;
    qa_q3_host *actual_host = NULL;
    if (!attached(owner) || lease->released || !owner->modules ||
        !qa_application_native_q3_client_modules_entered_host_read(owner->modules, lease->role,
            lease->service_owner, &actual_host, &actual, error) || actual.frontend_lifetime != lease ||
        actual.console != source(owner)->receiver.console ||
        actual.cvars != frontend_client_registry_cvars(lease->registry) ||
        actual.owner != source(owner)->receiver.receiver ||
        actual.command_context.seat != source(owner)->receiver.seat)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module callback lost its actual entered host lease");
    if (host) *host = actual_host;
    if (out) *out = actual;
    return true;
}
static bool initial_entered(void *context, const frontend_network_client_attempt *attempt, qa_error *error)
{
    remote_module_lease *lease = context;
    const frontend_remote_q3_modules *owner = lease ? lease->owner : NULL;
    const frontend_network_client_attempt *held = owner && owner->kind == REMOTE_MODULE_INITIAL ?
        &owner->basis.initial.view.attempt : NULL;
    return (held && attempt && held->epoch == attempt->epoch &&
        held->restart_generation == attempt->restart_generation &&
        qa_net_address_equal(&held->endpoint, &attempt->endpoint, true) &&
        same_source(&held->source, &attempt->source) &&
        held->configuration.owner == attempt->configuration.owner &&
        held->configuration.console == attempt->configuration.console &&
        held->configuration.cvars == attempt->configuration.cvars &&
        entered(lease, NULL, NULL, error)) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Initial UI DATA lost its retained entered attempt lease");
}
static bool same_host_context(const qa_q3_host_client_context *a, const qa_q3_host_client_context *b)
{
    const qa_command_context *x = &a->command_context, *y = &b->command_context;
    return a->session == b->session && a->role == b->role && a->owner == b->owner &&
        a->service_owner == b->service_owner && a->console == b->console && a->cvars == b->cvars &&
        a->client_time_cvars == b->client_time_cvars && a->client_time_owner == b->client_time_owner &&
        a->frontend_lifetime == b->frontend_lifetime && x->session == y->session && x->owner == y->owner &&
        x->seat == y->seat && x->client == y->client && x->dialect == y->dialect && x->origin == y->origin &&
        x->direct == y->direct && x->console_text == y->console_text && x->script == y->script &&
        x->registry == y->registry && x->generation == y->generation && qa_actor_id_equal(x->actor, y->actor);
}
static bool restored_entered(void *context, const frontend_network_client_domain *domain, qa_error *error)
{
    remote_module_lease *lease = context;
    const frontend_remote_q3_modules *owner = lease ? lease->owner : NULL;
    const frontend_network_client_domain *held = owner && owner->kind == REMOTE_MODULE_DECODED ?
        &owner->basis.decoded.view.domain : NULL;
    return (held && domain && held->epoch == domain->epoch &&
        held->restart_generation == domain->restart_generation &&
        held->connection.owner == domain->connection.owner && held->connection.slot == domain->connection.slot &&
        held->connection.generation == domain->connection.generation &&
        held->content_owner == domain->content_owner && held->content == domain->content &&
        held->map == domain->map && held->gamestate == domain->gamestate &&
        same_source(&held->source, &domain->source) && entered(lease, NULL, NULL, error)) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Restored DATA lost its retained entered module domain");
}
static bool browser_current(void *context, const qa_q3_host_client_context *ui, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!lease || !ui || lease->released || lease->role != QA_QVM_UI ||
        !same_host_context(ui, &lease->constructor)) return false;
    if (lease->preparing) {
        const frontend_remote_q3_modules *owner = lease->owner;
        const qa_q3_gamestate *gs = owner->kind == REMOTE_MODULE_INITIAL ? NULL : owner->basis.decoded.view.domain.gamestate;
        return owner->constructing && basis_current(owner, source(owner), gs, error);
    }
    qa_q3_host_client_context actual;
    return entered(lease, NULL, &actual, error) && same_host_context(ui, &actual);
}
static bool ui_host_current(void *context, const qa_q3_host *host,
    const qa_q3_host_client_context *ui, qa_error *error)
{
    qa_q3_host *actual_host = NULL; qa_q3_host_client_context actual;
    return host && entered(context, &actual_host, &actual, error) && host == actual_host &&
        same_host_context(ui, &actual);
}
static bool ui_state(void *context, const qa_q3_host *host, qa_q3_ui_client_state *out, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!lease || lease->callbacks == SIZE_MAX) return false;
    ++lease->callbacks;
    bool ok = frontend_network_ui_client_state(&lease->browser, host, lease, ui_host_current, out, error);
    --lease->callbacks; return ok;
}
static bool installed_mods(void *context, qa_vfs_listing *out, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!out || lease->callbacks == SIZE_MAX || !entered(lease, NULL, NULL, error)) return false;
    ++lease->callbacks;
    bool ok = qa_catalog_q3_mod_list(qa_launch_instance_catalog(source(lease->owner)->descriptor), out, error);
    if (ok) ok = entered(lease, NULL, NULL, error);
    --lease->callbacks; return ok;
}
static bool movie_current(void *context, const frontend_system_cinematic_source *view)
{
    const remote_module_lease *lease = context;
    const frontend_remote_q3_modules *owner = lease ? lease->owner : NULL;
    if (!view || !owner || lease->released || !lease->movie_references || owner->retiring) return false;
    const qa_application_q3_remote_source *held = source(owner);
    const frontend_system_cinematic_identity *id = &view->identity;
    const qa_q3_gamestate *gs = owner->kind == REMOTE_MODULE_INITIAL ? NULL : owner->basis.decoded.view.domain.gamestate;
    return view->context == lease && view->files == mounts(owner) && view->movies == movies(owner) &&
        view->cvars == frontend_client_registry_cvars(lease->registry) && view->cinematics==lease->cinematics &&
        id->source_group == identity(owner) && id->source_owner == held->receiver.receiver &&
        id->service_owner == lease->service_owner && id->audio_bus == lease->service_owner &&
        id->role == lease->role && id->physical_seat == physical_seat(owner) &&
        id->launch_seat == held->receiver.seat && basis_current(owner, held, gs, NULL);
}
static void movie_release(void *context)
{ remote_module_lease *lease = context; if (lease->movie_references) --lease->movie_references; }
static bool movie_append(void *context, const char *text, qa_error *error)
{
    remote_module_lease *lease = context;
    frontend_remote_q3_modules *owner = lease->owner;
    const qa_application_q3_remote_source *held = source(owner);
    const qa_q3_gamestate *gs = owner->kind == REMOTE_MODULE_INITIAL ? NULL : owner->basis.decoded.view.domain.gamestate;
    qa_command_context command;
    if (!text || !lease->movie_references || lease->callbacks == SIZE_MAX || lease->released ||
        !basis_current(owner, held, gs, error) ||
        !qa_application_capture_command_context(owner->application, &held->receiver.command_context, &command, error)) return false;
    command.script = "q3-system-movie"; command.direct = false; command.console_text = false;
    ++lease->callbacks;
    bool ok = qa_console_append(held->receiver.console, &command, text, error) &&
        basis_current(owner, held, gs, error);
    --lease->callbacks; return ok;
}
static bool system_movie(void *context, const qa_q3_host *host, const qa_qvm_call *call,
    const qa_q3_movie_request *request, qa_q3_system_movie *out, qa_error *error)
{
    remote_module_lease *lease = context; qa_q3_host *actual_host = NULL;
    if (!lease || lease->callbacks == SIZE_MAX || lease->movie_references == SIZE_MAX ||
        !entered(lease, &actual_host, NULL, error) || actual_host != host ||
        !qa_q3_host_system_movie_scope_current(host, call, lease, lease->service_owner,
            lease->role, lease->presentation))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "System cinematic lost its actual entered remote role");
    frontend_remote_q3_modules *owner = lease->owner;
    frontend_system_cinematic_source view = {
        .identity = {identity(owner), lease->service_owner, lease->service_owner,
            source(owner)->receiver.receiver, lease->role, physical_seat(owner), source(owner)->receiver.seat},
        .files = mounts(owner), .movies = movies(owner), .cvars = frontend_client_registry_cvars(lease->registry),
        .cinematics=lease->cinematics,.context = lease, .current = movie_current, .append = movie_append, .release = movie_release};
    ++lease->callbacks; ++lease->movie_references;
    bool ok = frontend_system_cinematic_open(owner->frontend, &view, request, out, error);
    if (!ok) --lease->movie_references;
    --lease->callbacks; return ok;
}
bool frontend_remote_q3_modules_cinematic_source_decode(frontend_remote_q3_modules *owner,
    const frontend_system_cinematic_identity *id, frontend_system_cinematic_source *out, qa_error *error)
{
    if (!owner || !id || !out || owner->retiring || !attached(owner) || !owner->modules ||
        id->source_group != identity(owner) || id->source_owner != source(owner)->receiver.receiver ||
        id->physical_seat != physical_seat(owner) || id->launch_seat != source(owner)->receiver.seat)
        return frontend_fail(error, QA_ERROR_FORMAT, "Saved system movie leaves its real module parent");
    for (remote_module_lease *lease = owner->leases; lease; lease = lease->next) {
        if (lease->role != id->role || lease->service_owner != id->service_owner) continue;
        qa_q3_host *host; qa_q3_host_client_context context;
        const qa_q3_gamestate *gs = owner->kind == REMOTE_MODULE_INITIAL ? NULL : owner->basis.decoded.view.domain.gamestate;
        if (lease->released || lease->movie_references == SIZE_MAX || id->audio_bus != lease->service_owner ||
            !basis_current(owner, source(owner), gs, error) ||
            !qa_application_native_q3_client_modules_host_read(owner->modules, lease->role, &host, &context, error) ||
            !same_host_context(&context, &lease->constructor)) return false;
        *out = (frontend_system_cinematic_source){.identity = *id, .files = mounts(owner),
            .movies = movies(owner), .cvars = frontend_client_registry_cvars(lease->registry),
            .cinematics=lease->cinematics,.context = lease, .current = movie_current, .append = movie_append, .release = movie_release};
        ++lease->movie_references; return true;
    }
    return frontend_fail(error, QA_ERROR_FORMAT, "Saved system movie has no actual role namespace");
}
static bool namespace_entered(void *context, qa_application *app,
    const qa_application_startup_source *tuple, const qa_q3_host **out, qa_error *error)
{
    remote_module_lease *lease = context;
    if (out) *out = NULL;
    bool matches = lease && out && app == lease->owner->application && tuple &&
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
{ remote_module_lease *lease = context; return (double)lease->owner->frontend->wall_time_ns / 1000000.0; }
static uint32_t common_milliseconds(void *context)
{ remote_module_lease *lease = context; return (uint32_t)(lease->owner->frontend->wall_time_ns / UINT64_C(1000000)); }
static int32_t source_milliseconds(void *context)
{ remote_module_lease *lease = context; return (int32_t)(uint32_t)(lease->owner->frontend->wall_time_ns / UINT64_C(1000000)); }
static int32_t frame_number(void *context)
{ remote_module_lease *lease = context; return (int32_t)(lease->owner->frontend->frame_number & INT32_MAX); }
static uint64_t audio_bus(void *context)
{ return ((remote_module_lease *)context)->service_owner; }
static bool cinematic_diagnostic_current(void *context, const qa_q3_cinematic_source *source_value)
{
    const remote_module_lease *lease = context;
    const frontend_remote_q3_modules *owner = lease ? lease->owner : NULL;
    if (!attached(owner) || !lease->registry || lease->callbacks == SIZE_MAX ||
        owner->application != owner->frontend->application || lease->cinematics != source_value ||
        lease->constructor.frontend_lifetime != lease || lease->constructor.role != lease->role ||
        lease->constructor.service_owner != lease->service_owner ||
        lease->constructor.session != source(owner)->receiver.session ||
        lease->constructor.owner != source(owner)->receiver.receiver ||
        lease->constructor.console != source(owner)->receiver.console ||
        lease->constructor.cvars != frontend_client_registry_cvars(lease->registry)) return false;
    return cinematic_current(lease, NULL);
}
static void cinematic_print(void *context, const char *text)
{
    remote_module_lease *lease = context;
    if (!lease || !cinematic_diagnostic_current(lease, lease->cinematics)) return;
    ++lease->callbacks;
    qa_console *console = lease->constructor.console;
    qa_console *shared = qa_application_console(lease->owner->application);
    if (qa_console_output_redirected(console)) qa_console_emit(console, &lease->command, text);
    else if (shared && qa_console_output_redirected(shared)) qa_console_emit(shared, &lease->command, text);
    else frontend_console_print(lease->owner->frontend, &lease->command, text);
    --lease->callbacks;
}
static void print(void *context, const char *text)
{
    remote_module_lease *lease = context;
    if (!entered(lease, NULL, NULL, NULL)) return;
    ++lease->callbacks;
    qa_console *console = source(lease->owner)->receiver.console;
    qa_console *shared = qa_application_console(lease->owner->application);
    if (qa_console_output_redirected(console)) qa_console_emit(console, &lease->command, text);
    else if (shared && qa_console_output_redirected(shared)) qa_console_emit(shared, &lease->command, text);
    else frontend_console_print(lease->owner->frontend, &lease->command, text);
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
    bool ok = frontend_network_client_reliable(lease->owner->frontend, &lease->command, text, error);
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
    *out = present ? frontend_audio_actor(lease->owner->frontend, actual, error) : QA_AUDIO_NO_ACTOR;
    return !present || *out != QA_AUDIO_NO_ACTOR;
}
static bool listener(void *context, const qa_audio_listener *value, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!value || !entered(lease, NULL, NULL, error)) return false;
    lease->listener = *value;
    lease->listener.gain = 1.0f / (float)lease->owner->frontend->options.seats;
    lease->has_listener = true; return true;
}
static bool music_origin_current(void *context, const frontend_music_origin *origin)
{
    remote_module_lease *lease = context;
    frontend_remote_q3_modules *owner = lease ? lease->owner : NULL;
    const qa_application_q3_remote_source *held = owner ? source(owner) : NULL;
    qa_application_q3_remote_source retained;
    return held && origin && origin->kind == FRONTEND_MUSIC_MODULE &&
        origin->context == lease && origin->bus == lease->service_owner &&
        origin->physical_seat == physical_seat(owner) && origin->receiver == held->receiver.receiver &&
        origin->descriptor && origin->descriptor->storage == held->descriptor->storage &&
        origin->catalog == qa_launch_instance_catalog(held->descriptor) &&
        origin->product == held->descriptor->selection.product && origin->files == mounts(owner) &&
        origin->music == lease->music && attached(owner) &&
        qa_application_native_q3_client_modules_retained_source_read(owner->modules, &retained, NULL) &&
        same_source(held, &retained) && lease->constructor.frontend_lifetime == lease &&
        lease->constructor.service_owner == lease->service_owner && lease->constructor.role == lease->role &&
        lease->constructor.console == retained.receiver.console && lease->constructor.cvars == retained.receiver.cvars &&
        lease->constructor.owner == retained.receiver.receiver &&
        lease->constructor.command_context.seat == retained.receiver.seat;
}
static bool music_stop(void *context, qa_error *error)
{
    remote_module_lease *lease = context; qa_audio_engine *engine = lease->owner->frontend->audio;
    qa_audio_music *attached_player = qa_audio_engine_bus_music(engine, lease->service_owner);
    if (attached_player && attached_player != lease->music)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Music retirement found another actual bus player");
    lease->music_attached = attached_player != NULL;
    if (lease->music_attached) {
        qa_audio_engine_remove_music(engine, lease->service_owner);
        if (qa_audio_engine_bus_music(engine, lease->service_owner) == lease->music)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Music retirement retains its actual engine bus");
        lease->music_attached = false;
    }
    qa_audio_music_stop(lease->music);
    free(lease->music_intro); free(lease->music_loop); lease->music_intro = lease->music_loop = NULL;
    lease->music_looping = false; return true;
}
static frontend_music_origin music_origin(remote_module_lease *lease)
{
    const qa_launch_instance *descriptor = source(lease->owner)->descriptor;
    return (frontend_music_origin){.kind = FRONTEND_MUSIC_MODULE, .bus = lease->service_owner,
        .physical_seat = physical_seat(lease->owner), .receiver = source(lease->owner)->receiver.receiver,
        .descriptor = descriptor, .catalog = qa_launch_instance_catalog(descriptor),
        .product = descriptor->selection.product, .files = mounts(lease->owner), .music = lease->music,
        .context = lease, .current = music_origin_current, .stop = music_stop};
}
bool frontend_remote_modules_music_restore_origin(remote_module_lease *lease, qa_error *error)
{
    frontend_music_sources *sources = lease->owner->frontend->music_sources;
    if (!lease->music) return true;
    qa_audio_music_controls *controls = frontend_music_sources_controls(sources);
    if (!controls || !qa_audio_music_controls_bind(lease->music, controls, error)) return false;
    frontend_music_origin origin = music_origin(lease);
    return !frontend_music_sources_restore_origin_matches(sources, &origin) ||
        frontend_music_sources_restore_origin(sources, &origin, error);
}
static bool music(void *context, const char *intro_name, const char *loop_name, qa_error *error)
{
    remote_module_lease *lease = context; qa_frontend *f = lease->owner->frontend;
    if (!entered(lease, NULL, NULL, error)) return false;
    if (!f->audio) return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Remote module music has no audio output owner");
    qa_audio_music *attached_player = qa_audio_engine_bus_music(f->audio, lease->service_owner);
    if (attached_player && attached_player != lease->music)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Module music lost its actual retained engine player");
    lease->music_attached = attached_player != NULL;
    bool retained_player = lease->music != NULL;
    if (!lease->music && !qa_audio_music_create(qa_audio_engine_rate(f->audio), QA_AUDIO_Q3, true, &lease->music, error)) return false;
    frontend_music_origin origin = music_origin(lease);
    if (retained_player && !frontend_music_sources_explicit_selected(f->music_sources, &origin)) {
        if (attached_player || !qa_audio_music_idle(lease->music))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Unselected module soundtrack retains an actual engine bus");
        qa_audio_music *fresh = NULL;
        if (!qa_audio_music_create(qa_audio_engine_rate(f->audio), QA_AUDIO_Q3, true, &fresh, error)) return false;
        qa_audio_music_destroy(lease->music); lease->music = fresh;
        free(lease->music_intro); free(lease->music_loop); lease->music_intro = lease->music_loop = NULL;
        lease->music_looping = false; origin = music_origin(lease);
    }
    if (!frontend_music_sources_explicit_begin(f->music_sources, &origin, error)) return false;
    const char *requested_loop = loop_name ? loop_name : "";
    bool enabled;
    if (!qa_audio_music_controls_enabled(frontend_music_sources_controls(f->music_sources), &enabled))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Module music lost its actual shared CD controls");
    if (intro_name && *intro_name && !enabled)
        return frontend_music_sources_explicit(f->music_sources, &origin, NULL, NULL, false, error);
    if (intro_name && lease->music_intro && lease->music_loop && lease->music_looping &&
        !strcmp(intro_name, lease->music_intro) && !strcmp(requested_loop, lease->music_loop) &&
        qa_audio_music_playing(lease->music)) return true;
    qa_audio_music_stop(lease->music);
    free(lease->music_intro); free(lease->music_loop);
    lease->music_intro = lease->music_loop = NULL; lease->music_looping = false;
    if (!intro_name || !*intro_name)
        return frontend_music_sources_explicit(f->music_sources, &origin, NULL, NULL, false, error);
    size_t a = strlen(intro_name), b = strlen(requested_loop);
    lease->music_intro = malloc(a + 1); lease->music_loop = malloc(b + 1);
    if (!lease->music_intro || !lease->music_loop) {
        free(lease->music_intro); free(lease->music_loop); lease->music_intro = lease->music_loop = NULL;
        return frontend_fail(error, QA_ERROR_MEMORY, "Retaining remote module music selection");
    }
    memcpy(lease->music_intro, intro_name, a + 1); memcpy(lease->music_loop, requested_loop, b + 1);
    lease->music_looping = true;
    qa_audio_stream *intro = NULL, *loop = NULL;
    if (!qa_audio_bank_music_cue(sounds(lease->owner), intro_name, QA_AUDIO_Q3, NULL, NULL, &intro, error)) return false;
    if (!intro) return frontend_music_sources_explicit(f->music_sources, &origin,
        intro_name, requested_loop, lease->music_looping, error);
    loop = intro;
    if (loop_name && *loop_name && strcmp(loop_name, intro_name) &&
        !qa_audio_bank_music_cue(sounds(lease->owner), loop_name, QA_AUDIO_Q3, NULL, NULL, &loop, error)) {
        qa_audio_stream_close(intro); return false;
    }
    lease->music_looping = loop != NULL;
    qa_audio_music_start(lease->music, intro, loop);
    bool held = lease->music_attached || qa_audio_music_retain(lease->music, error);
    bool ok = held && qa_audio_engine_music(f->audio, lease->service_owner, physical_seat(lease->owner), 1, lease->music, error);
    if (!ok && held && !lease->music_attached) qa_audio_music_release(lease->music);
    if (ok) lease->music_attached = true;
    return ok && frontend_music_sources_explicit(f->music_sources, &origin,
        intro_name, requested_loop, lease->music_looping, error);
}
static qa_collision_geometry *geometry(void *context)
{ remote_module_lease *lease = context; return lease->owner->kind == REMOTE_MODULE_DECODED && entered(lease, NULL, NULL, NULL) ? lease->owner->basis.decoded.view.geometry : NULL; }
static bool load_map(void *context, const char *path, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!path || !geometry(lease)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module collision lost its entered map owner");
    const char *map = qa_resource_path(lease->owner->basis.decoded.view.map);
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
        qa_material_remap(materials(lease->owner), from, to, offset, error);
}
static bool render_current(const remote_module_lease *lease)
{
    return lease && lease->render_definition && lease->callbacks &&
        qa_q3_host_render_scope_current(lease->render_host, lease->render_call, lease,
            lease->service_owner, lease->role, lease->presentation) && entered(lease, NULL, NULL, NULL);
}
static bool prepare_picture(void *context, qa_material_context *material, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!material || !entered(lease, NULL, NULL, error)) return false;
    qa_frontend *f = lease->owner->frontend;
    qa_q3_color_lighting lighting;
    if (!f->source_color)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote picture lacks its physical Source color owner");
    if (!frontend_q3_source_color_lighting_read(f, NULL, &lighting, error)) return false;
    material->identity_light = lighting.identity_light;
    qa_render_controls *controls = f->cpu ? qa_cpu_render_controls(f->cpu) : f->gl ? qa_gl_render_controls(f->gl) : NULL;
    if (!controls) return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote picture lost its actual physical renderer");
    material->source_scratch = qa_render_controls_source_scratch(controls, error);
    return material->source_scratch && frontend_q3_material_diagnostics_read(f, &material->source_diagnostics, error) &&
        entered(lease, NULL, NULL, error);
}
static bool prepare_view(void *context, const qa_q3_refdef *definition, qa_q3_scene_options *options, qa_error *error)
{
    remote_module_lease *lease = context;
    if (!render_current(lease) || definition != lease->render_definition || !options)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module view lost its actual entered RenderScene");
    options->world_family = QA_SCENE_Q3;
    options->split_screen = lease->owner->frontend->options.seats > 1;
    if (lease->equipment && !frontend_equipment_source_prepare_view(lease->equipment, definition, options, error)) return false;
    if (!render_current(lease) || !frontend_q3_scene_policy_read(lease->owner->frontend, options, error)) return false;
    options->shadow_mode = 0;
    if (lease->role == QA_QVM_CGAME) {
        bool initialized, succeeded;
        if (!qa_application_native_q3_client_modules_initialization_read(lease->owner->modules,
            lease->role, &initialized, &succeeded, error)) return false;
        const qa_cvars *cvars = frontend_client_registry_cvars(lease->registry);
        if (succeeded || qa_cvars_find(cvars, "cg_shadows"))
            return frontend_q3_shadow_mode_read(cvars, &options->shadow_mode, error) && render_current(lease);
    }
    return true;
}
static bool submit_view(void *context, const qa_q3_scene_options *options, qa_scene_frame *frame, qa_error *error)
{
    remote_module_lease *lease = context;
    return render_current(lease) && frame == &lease->owner->frontend->frame &&
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
    qa_q3_host *host; qa_q3_host_client_context retained;
    if (!client || lease->owner->kind != REMOTE_MODULE_DECODED || lease->role != QA_QVM_CGAME ||
        lease->released || !lease->owner->modules ||
        !qa_application_native_q3_client_modules_host_read(lease->owner->modules, lease->role, &host, &retained, NULL) ||
        !same_host_context(&retained, &lease->constructor) ||
        !frontend_remote_q3_resources_read(lease->owner->basis.decoded.row, &resources, NULL)) return false;
    bool initialized, succeeded;
    if (!qa_application_native_q3_client_modules_initialization_read(lease->owner->modules,
        QA_QVM_CGAME, &initialized, &succeeded, NULL) || !initialized || !succeeded) return false;
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
    if (!out || lease->owner->kind != REMOTE_MODULE_DECODED || lease->callbacks == SIZE_MAX ||
        !entered(lease, NULL, NULL, error) ||
        !frontend_remote_q3_resources_read(lease->owner->basis.decoded.row, &resources, error)) return false;
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
    remote_module_lease *lease = context; qa_frontend *f = lease->owner->frontend;
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
    remote_module_lease *lease = context; qa_frontend *f = lease->owner->frontend; bool drawn;
    if (!entered(lease, NULL, NULL, error) ||
        !qa_application_native_q3_client_modules_loading_screen(lease->owner->modules, &drawn, error)) return false;
    f->frame.source_backend = true;
    if (!frontend_render_controls_live(f, error) || !entered(lease, NULL, NULL, error)) return false;
    if (f->frame.source_skip_backend) {
        if (f->frame.source_pending && !qa_material_source_frame_end(f->frame.source_pending, &f->frame, false, error))
            return false;
        return true;
    }
    return f->cpu ? qa_cpu_execute(f->cpu, &f->frame, error) && qa_cpu_present_frame(f->cpu, error) :
        qa_gl_execute(f->gl, &f->frame, error) && qa_gl_finish(f->gl, error) && qa_gl_swap(f->gl, error);
}

static bool dispose_lease(remote_module_lease *lease, qa_error *error)
{
    frontend_remote_q3_modules *owner = lease->owner; qa_frontend *f = owner->frontend;
    if (!lease->released || lease->callbacks || lease->render_definition ||
        (f->music_sources && !frontend_music_sources_explicit_retire(f->music_sources, lease, error)) ||
        !frontend_equipment_source_destroy(lease->equipment, error)) return false;
    lease->equipment = NULL;
    if (lease->presentation && !qa_q3_presentation_destroy(lease->presentation, error)) return false;
    lease->presentation = NULL;
    if (!qa_q3_cinematic_source_destroy(&lease->cinematics, error)) return false;
    if (lease->movie_references)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module retirement retains a system cinematic source lease");
    if (f->audio) {
        if (!qa_audio_engine_stop_owner(f->audio, lease->service_owner, physical_seat(owner), error)) return false;
        qa_audio_music *attached_player = qa_audio_engine_bus_music(f->audio, lease->service_owner);
        if (attached_player && attached_player != lease->music) return false;
        if (attached_player) {
            qa_audio_engine_remove_music(f->audio, lease->service_owner);
            if (qa_audio_engine_bus_music(f->audio, lease->service_owner) == lease->music) return false;
            lease->music_attached = false;
        }
    }
    qa_audio_music_destroy(lease->music); lease->music = NULL;
    free(lease->music_intro); free(lease->music_loop); lease->music_intro = lease->music_loop = NULL;
    if (lease->keys && !frontend_key_profile_release(lease->keys, error)) return false;
    lease->keys = NULL;
    if (!frontend_client_registry_release(&lease->registry, error)) return false;
    qa_catalog_write_resolver_destroy(lease->write_resolver); lease->write_resolver = NULL;
    return true;
}
bool frontend_remote_modules_released_drain(frontend_remote_q3_modules *owner, qa_error *error)
{
    for (remote_module_lease **link = &owner->leases; *link;) {
        remote_module_lease *previous = *link;
        if (!previous->released) { link = &previous->next; continue; }
        if (!dispose_lease(previous, error)) return false;
        *link = previous->next; free(previous);
    }
    return true;
}

static bool prepare(void *context, const qa_application_native_q3_module_preparation *request, qa_error *error)
{
    frontend_remote_q3_modules *owner = context;
    qa_frontend *f = owner->frontend;
    const qa_q3_gamestate *gamestate = owner->kind == REMOTE_MODULE_INITIAL ? NULL : owner->basis.decoded.view.domain.gamestate;
    remote_module_saved *saved = request && owner->restoring ?
        frontend_remote_modules_saved(owner, request->role, request->service_owner) : NULL;
    if (!request || !request->services || request->restoring != owner->restoring || !owner->constructing ||
        (owner->restoring && (!saved || saved->prepared)) ||
        (owner->kind == REMOTE_MODULE_INITIAL && request->role != QA_QVM_UI) ||
        !current(owner, request->source, gamestate, error)) return false;
    if (!frontend_remote_modules_released_drain(owner, error)) return false;
    remote_module_lease *lease = calloc(1, sizeof(*lease));
    if (!lease) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining acquired remote host lease");
    lease->owner = owner; lease->role = request->role; lease->service_owner = request->service_owner;
    lease->command = request->services->command_context;
    lease->next = owner->leases; owner->leases = lease;
    qa_q3_host_options *host = request->services;
    host->frontend_lifetime = lease; host->release_frontend = released;
    qa_application_startup_source tuple;
    frontend_remote_config *config = frontend_config_store_client(f->config_store, host->console);
    frontend_remote_config_view configuration_view;
    bool ok = config && frontend_remote_config_read(config, &configuration_view) &&
        frontend_remote_config_current(config, &configuration_view) && configuration_view.ready &&
        configuration_view.cvars == host->cvars && configuration_view.physical_seat == physical_seat(owner) &&
        frontend_client_registry_retain(registry(owner), &lease->registry, error) &&
        qa_application_q3_client_configuration_read(owner->application, host->owner,
            request->source->receiver.seat, &tuple, error) &&
        frontend_config_host_cvars_prepare(&lease->namespaces, f->config_store,
            owner->application, &tuple, NULL, &host->cvar_namespaces, error);
    if (ok) ok = frontend_config_host_cvars_set_entry(&lease->namespaces, lease, namespace_entered, error);
    if (ok) {
        lease->keys = configuration_view.keys;
        ok = lease->keys && (!saved || frontend_key_profile_id(lease->keys) == saved->keys) &&
            frontend_key_profile_state(lease->keys) && frontend_key_profile_retain(lease->keys, error);
        if (!ok) lease->keys = NULL;
    }
    if (ok && owner->kind == REMOTE_MODULE_INITIAL)
        ok = owner->restoring ? frontend_network_client_restore_attempt_services(f, &owner->basis.initial.view.attempt,
            &lease->initial_network, lease, initial_entered, &lease->network, error) :
            frontend_network_client_attempt_services(f, &owner->basis.initial.view.attempt,
                &lease->initial_network, lease, initial_entered, &lease->network, error);
    else if (ok)
        ok = owner->restoring ? frontend_network_client_restore_services(f, &owner->basis.decoded.view.domain,
            &lease->restored_network, lease, restored_entered, &lease->network, error) :
            frontend_network_presentation_services(f, &request->source->receiver, &lease->network, error);
    lease->constructor = (qa_q3_host_client_context){host->session, host->role, host->owner,
        host->service_owner, host->console, host->cvars, host->client_time_cvars,
        host->client_time_owner, host->command_context, lease};
    lease->media_views[0] = mounts(owner);
    if (ok && request->role == QA_QVM_UI) {
        lease->preparing = true;
        ok = frontend_network_browser_services(f, &lease->constructor, request->source->receiver.seat,
            request->source->connection_epoch, lease, browser_current, &lease->browser, &host->browser, error);
        lease->preparing = false;
    }
    qa_q3_cinematic_source *parent = NULL;
    if (ok && !owner->restoring) ok = cinematic_parent(lease, &parent, error) &&
        qa_q3_cinematic_source_create_role(parent, physical_seat(owner), lease->service_owner,
            &lease->cinematics, error) &&
        qa_q3_cinematic_source_role_diagnostic_bind(lease->cinematics, lease, cinematic_print,
            cinematic_diagnostic_current, error);
    qa_q3_presentation_options backend = {.assets = assets(owner), .audio = f->audio,
        .cinematics = lease->cinematics,
        .clock = {lease, milliseconds}, .seat = physical_seat(owner), .owner = lease->service_owner,
        .viewport = {0, 0, f->width, f->height},
        .near_clip = 4, .far_clip = 16384, .identity_light = 1, .lod_scale = 5,
        .rail_core_width = 6, .rail_ring_width = 16, .rail_segment_length = 32,
        .context = lease, .audio_actor = actor, .listener = listener, .music = music,
        .frame_number = frame_number, .milliseconds = source_milliseconds, .audio_bus = audio_bus,
        .prepare_view = prepare_view, .submit_view = submit_view, .prepare_picture = prepare_picture,
        .scene_cleared = scene_cleared, .remap = remap, .print = print};
    if (ok && !owner->restoring) ok = frontend_q3_renderer_options_read(f, &backend, error);
    if (ok) ok = qa_q3_presentation_create(&backend, &lease->presentation, error);
    if (ok && owner->kind == REMOTE_MODULE_DECODED) {
        const frontend_remote_q3_resources *resources = &owner->basis.decoded.view;
        qa_bsp_view bsp;
        ok = resources->world && qa_bsp_open(qa_resource_bytes(resources->map), &bsp, error);
        if (ok) ok = owner->restoring ? qa_q3_presentation_prepare_restored(lease->presentation,
            &f->frame, resources->world, resources->geometry, bsp.lumps[QA_BSP_ENTITIES].bytes, error) :
            qa_q3_presentation_world(lease->presentation, resources->world, resources->geometry,
                bsp.lumps[QA_BSP_ENTITIES].bytes, error);
    }
    if (ok && owner->kind == REMOTE_MODULE_INITIAL && owner->restoring)
        ok = qa_q3_presentation_prepare_restored(lease->presentation, &f->frame, NULL, NULL, (qa_bytes){0}, error);
    if (ok && !owner->restoring) ok = qa_q3_presentation_frame(lease->presentation, &f->frame, backend.viewport, error);
    if (ok && request->role == QA_QVM_CGAME) {
        frontend_equipment_source_options equipment = {.frontend = f,
            .receiver = host->owner, .seat = request->source->receiver.seat, .physical_seat = physical_seat(owner),
            .assets = assets(owner), .presentation = lease->presentation, .lease = lease,
            .borrow = equipment_borrow, .current = equipment_current, .release = equipment_return,
            .requests_context = lease, .requests = equipment_requests};
        ok = request->equipment_services && frontend_equipment_source_create(&equipment, &lease->equipment, error);
        if (ok) frontend_equipment_source_services(lease->equipment, request->equipment_services);
    }
    qa_catalog *catalog = qa_launch_instance_catalog(request->source->descriptor);
    qa_product_id product = request->source->descriptor->selection.product;
    if (ok && qa_catalog_product_write_root(catalog, product))
        ok = qa_catalog_write_resolver_create(catalog, product, &lease->write_resolver, error);
    if (!ok) return false;
    host->client = lease->network; host->keys = frontend_key_profile_state(lease->keys); host->seat = input(owner);
    if (request->role == QA_QVM_UI) {
        host->client.ui_state_context = lease; host->client.ui_state = ui_state;
    }
    host->game_directory = frontend_key_profile_game_directory(lease->keys);
    host->write_view = (qa_q3_host_write_view){.root = qa_catalog_write_resolver_root(lease->write_resolver),
        .resolver = qa_catalog_write_resolver_services(lease->write_resolver)};
    host->input = (qa_q3_host_input_services){&lease->namespaces, frontend_config_host_bindings};
    host->cvar_entry = (qa_q3_host_cvar_entry_services){&lease->namespaces, frontend_config_host_cvar_entered};
    host->console_field = qa_seat_console_field(f->seats[physical_seat(owner)].console, false);
    host->scene_resources = images(owner); host->scene_frame = &f->frame; host->sound_bank = sounds(owner);
    if (owner->kind == REMOTE_MODULE_DECODED) {
        host->scene_world = owner->basis.decoded.view.world;
        host->collision = (qa_q3_host_collision_services){lease, geometry, load_map};
    }
    host->presentation = (qa_q3_host_presentation_services){.context = lease,
        .seat = lease->presentation, .fonts = fonts(owner), .configuration = configuration,
        .update_screen = update_screen, .system_movie_context = lease, .system_movie = system_movie};
    host->render = (qa_q3_host_render_services){lease, render_enter, render_leave};
    host->common = (qa_q3_host_common_services){.context = lease, .print = print, .milliseconds = common_milliseconds,
        .calendar = calendar, .arguments = arguments, .client_command = client_command,
        .installed_mods = installed_mods, .clipboard = clipboard};
    if (saved) saved->prepared = true;
    return true;
}

bool frontend_remote_q3_modules_create(frontend_remote_q3 *row, frontend_remote_q3_modules **out, qa_error *error)
{
    frontend_remote_q3_resources resources;
    if (!row || row->frontend->resource_inventory || !out || *out ||
        !frontend_remote_q3_resources_read(row, &resources, error)) return false;
    frontend_remote_q3_modules *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining acquired remote CLIENT child");
    owner->frontend = row->frontend; owner->application = row->application; owner->kind = REMOTE_MODULE_DECODED;
    owner->basis.decoded.row = row; owner->basis.decoded.view = resources; *out = owner;
    if (!frontend_remote_q3_modules_attach(row, owner, error)) return false;
    owner->attached = true; owner->constructing = true;
    qa_application_native_q3_client_modules_options options = {.source = resources.domain.source,
        .gamestate = resources.domain.gamestate, .context = owner, .current = current, .prepare = prepare};
    bool ok = qa_application_native_q3_client_modules_create(row->application, &options, &owner->modules, error);
    owner->constructing = false; return ok;
}
remote_module_saved *frontend_remote_modules_saved(frontend_remote_q3_modules *owner,
    qa_qvm_role role, uint64_t service_owner)
{
    for (size_t i = 0; owner && i < owner->saved_count; ++i)
        if (owner->saved[i].role == (uint32_t)role && owner->saved[i].service_owner == service_owner)
            return &owner->saved[i];
    return NULL;
}
bool frontend_remote_modules_construct_restored(qa_frontend *f, frontend_remote_q3 *row,
    frontend_remote_q3_initial *initial, remote_module_saved **saved, size_t count,
    qa_bytes wrapper, qa_bytes modules, frontend_remote_q3_modules **out, qa_error *error)
{
    if (!f || !out || *out || !saved || !*saved || !count || count > 2 ||
        (!!row == !!initial) || f->capture || f->resource_inventory || f->shutdown)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Module import requires its genuine detached parent and complete role records");
    frontend_remote_q3_resources decoded = {0}; frontend_remote_q3_initial_view connecting = {0};
    if (row ? row->frontend != f || !frontend_remote_q3_resources_import_read(row, &decoded, error) || !decoded.world :
        frontend_remote_q3_initial_frontend(initial) != f ||
        !frontend_remote_q3_initial_import_read(initial, &connecting, error)) return false;
    frontend_remote_q3_modules *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining restored module wrapper");
    owner->frontend = f; owner->application = f->application; owner->restoring = true;
    owner->kind = row ? REMOTE_MODULE_DECODED : REMOTE_MODULE_INITIAL;
    if (row) {
        owner->basis.decoded.row = row; owner->basis.decoded.view = decoded;
        if (!frontend_remote_q3_modules_attach(row, owner, error)) { free(owner); return false; }
    } else {
        owner->basis.initial.view = connecting;
        if (!frontend_remote_q3_initial_child_retain(initial, &owner->basis.initial.owner, error)) { free(owner); return false; }
    }
    owner->attached = true; *out = owner;
    owner->saved = *saved; *saved = NULL; owner->saved_count = count;
    if (wrapper.size) {
        owner->saved_bytes.data = malloc(wrapper.size);
        if (!owner->saved_bytes.data) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining wrapper recapture witness");
        memcpy(owner->saved_bytes.data, wrapper.data, wrapper.size); owner->saved_bytes.size = wrapper.size;
    }
    owner->constructing = true;
    qa_application_native_q3_client_modules_options options = {.source = *source(owner),
        .gamestate = row ? decoded.domain.gamestate : NULL, .context = owner, .current = current, .prepare = prepare};
    bool ok = qa_application_native_q3_client_modules_restore(f->application, &options, modules, &owner->modules, error);
    owner->constructing = false;
    if (ok) {
        if (frontend_remote_q3_modules_role_count(owner) != count)
            return frontend_fail(error, QA_ERROR_FORMAT, "Restored wrapper and module role inventories differ");
        for (size_t i = 0; i < count; ++i) if (!owner->saved[i].prepared)
            return frontend_fail(error, QA_ERROR_FORMAT, "Saved wrapper role has no actual restored host");
    }
    return ok;
}
bool frontend_remote_q3_modules_create_initial(qa_frontend *f, frontend_remote_q3_initial *initial,
    frontend_remote_q3_modules **out, qa_error *error)
{
    frontend_remote_q3_initial_view view;
    if (!f || !initial || frontend_remote_q3_initial_frontend(initial) != f || !out || *out ||
        f->capture || f->resource_inventory || f->source_restoring || f->round || f->shutdown ||
        !frontend_remote_q3_initial_read(initial, &view, error) ||
        !frontend_network_client_attempt_current(f, &view.attempt) || !f->seats ||
        view.physical_seat >= f->options.seats || view.input != f->seats[view.physical_seat].input ||
        frontend_config_store_client(f->config_store, view.attempt.source.receiver.console) != view.attempt.configuration.owner ||
        frontend_client_registry_cvars(view.registry) != view.attempt.source.receiver.cvars)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Initial source UI requires its actual CLIENT attempt and structural media parent");
    frontend_remote_q3_modules *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining initial acquired UI child");
    owner->frontend = f; owner->application = f->application; owner->kind = REMOTE_MODULE_INITIAL;
    owner->basis.initial.view = view;
    if (!frontend_remote_q3_initial_child_retain(initial, &owner->basis.initial.owner, error)) { free(owner); return false; }
    owner->attached = true; *out = owner; owner->constructing = true;
    qa_application_native_q3_client_modules_options options = {.source = view.attempt.source,
        .context = owner, .current = current, .prepare = prepare};
    bool ok = qa_application_native_q3_client_modules_create(f->application, &options, &owner->modules, error);
    owner->constructing = false; return ok;
}
frontend_remote_q3 *frontend_remote_q3_modules_parent(const frontend_remote_q3_modules *owner)
{ return owner && owner->kind == REMOTE_MODULE_DECODED ? owner->basis.decoded.row : NULL; }
frontend_remote_q3_initial *frontend_remote_q3_modules_initial_parent(const frontend_remote_q3_modules *owner)
{ return owner && owner->kind == REMOTE_MODULE_INITIAL ? owner->basis.initial.owner : NULL; }
application_native_q3_client_modules *frontend_remote_q3_modules_owner(const frontend_remote_q3_modules *owner)
{ return owner ? owner->modules : NULL; }
size_t frontend_remote_q3_modules_role_count(const frontend_remote_q3_modules *owner)
{
    size_t count = 0;
    for (const remote_module_lease *lease = owner ? owner->leases : NULL; lease; lease = lease->next) ++count;
    return count;
}
bool frontend_remote_modules_restore_renderer_parameters(frontend_remote_q3_modules *owner, qa_error *error)
{
    if (!owner || !owner->restoring || !owner->frontend->source_color)
        return frontend_fail(error, QA_ERROR_FORMAT, "Restored renderer parameters require the imported physical Source color owner");
    qa_q3_presentation_options parameters = {0};
    if (!frontend_q3_renderer_options_read(owner->frontend, &parameters, error)) return false;
    for (remote_module_lease *lease = owner->leases; lease; lease = lease->next)
        if (lease->released || lease->callbacks || lease->render_definition ||
            !qa_q3_presentation_renderer_parameters_set(lease->presentation,
                parameters.near_clip, parameters.identity_light, error)) return false;
    return true;
}
bool frontend_remote_q3_modules_cinematics_bind(frontend_remote_q3_modules *owner, qa_error *error)
{
    if (!owner || !owner->restoring || owner->retiring || owner->constructing ||
        !attached(owner) || !owner->modules || !qa_application_native_q3_client_modules_idle(owner->modules))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Restored cinematic roles require their returned module prefix");
    if (!frontend_remote_modules_restore_renderer_parameters(owner, error)) return false;
    for (remote_module_lease *lease = owner->leases; lease; lease = lease->next) {
        qa_q3_presentation_binding binding;
        qa_q3_host *host = NULL; qa_q3_host_client_context context;
        if (lease->released || lease->callbacks || lease->render_definition ||
            !frontend_equipment_source_idle(lease->equipment) || !lease->presentation ||
            !qa_q3_presentation_binding_read(lease->presentation, &binding, error) ||
            binding.options.assets != assets(owner) ||
            !qa_application_native_q3_client_modules_host_read(owner->modules, lease->role, &host, &context, error) ||
            !same_host_context(&context, &lease->constructor)) return false;
        remote_module_saved *saved = frontend_remote_modules_saved(owner, lease->role, lease->service_owner);
        bool shared = false;
        if (!saved || !saved->prepared || !qa_q3_presentation_media_binding_read(
            (qa_bytes){saved->media.data, saved->media.size}, &shared, error)) return false;
        if (!shared) {
            if (lease->cinematics || binding.options.cinematics)
                return frontend_fail(error, QA_ERROR_FORMAT, "Saved local movie role cannot acquire a numeric source");
            continue;
        }
        qa_q3_cinematic_source *parent = NULL;
        if (!cinematic_parent(lease, &parent, error)) return false;
        if (!lease->cinematics && !qa_q3_cinematic_source_create_role(parent,
            physical_seat(owner), lease->service_owner, &lease->cinematics, error)) return false;
        if (!cinematic_current(lease, error)) return false;
        if (!qa_q3_cinematic_source_role_diagnostic_bind(lease->cinematics, lease, cinematic_print,
            cinematic_diagnostic_current, error)) return false;
        if (binding.options.cinematics != lease->cinematics &&
            !qa_q3_presentation_cinematics_bind(lease->presentation, lease->cinematics, error)) return false;
    }
    return frontend_remote_q3_modules_capture_returned(owner, error);
}
bool frontend_remote_q3_modules_capture_returned(const frontend_remote_q3_modules *owner, qa_error *error)
{
    if (!owner || owner->retiring || owner->constructing || !attached(owner) || !owner->modules ||
        !qa_application_native_q3_client_modules_idle(owner->modules)) return false;
    for (const remote_module_lease *lease = owner->leases; lease; lease = lease->next) {
        qa_q3_presentation_binding binding; qa_q3_host *host; qa_q3_host_client_context context;
        if (lease->released || lease->callbacks || lease->render_definition ||
            !frontend_equipment_source_idle(lease->equipment) || !lease->presentation ||
            !qa_q3_presentation_binding_read(lease->presentation, &binding, error) ||
            !qa_application_native_q3_client_modules_host_read(owner->modules, lease->role, &host, &context, error) ||
            !same_host_context(&context, &lease->constructor) || binding.options.assets != assets(owner) ||
            binding.options.cinematics != lease->cinematics || !cinematic_current(lease, error)) return false;
    }
    return true;
}
static bool role_read(const frontend_remote_q3_modules *owner, size_t index,
    bool cinematic_only, frontend_remote_q3_module_topology *out, qa_error *error)
{
    if (!owner || !out || owner->retiring || !attached(owner) || !owner->modules ||
        owner->constructing || !qa_application_native_q3_client_modules_idle(owner->modules))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module inventory requires its idle attached host children");
    for (const remote_module_lease *lease = owner->leases; lease; lease = lease->next) {
        qa_q3_presentation_binding binding;
        if (lease->callbacks || lease->render_definition || !frontend_equipment_source_idle(lease->equipment) ||
            !lease->presentation || !qa_q3_presentation_binding_read(lease->presentation, &binding, error) ||
            binding.options.cinematics != lease->cinematics)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module inventory retains an active role callback");
    }
    qa_application_q3_remote_source physical;
    if (owner->kind == REMOTE_MODULE_INITIAL) {
        frontend_remote_q3_initial_view view;
        bool ok = owner->frontend->resource_inventory ?
            frontend_remote_q3_initial_metadata_read(owner->basis.initial.owner, &view, error) :
            frontend_remote_q3_initial_read(owner->basis.initial.owner, &view, error);
        if (!ok) return false;
        physical = view.attempt.source;
    } else {
        frontend_remote_q3_resources view;
        bool ok = owner->frontend->resource_inventory ?
            frontend_remote_q3_resources_metadata_read(owner->basis.decoded.row, &view, error) :
            frontend_remote_q3_resources_read(owner->basis.decoded.row, &view, error);
        if (!ok) return false;
        physical = view.domain.source;
    }
    const remote_module_lease *lease = owner->leases;
    for (size_t i = 0; lease && i < index; ++i) lease = lease->next;
    qa_q3_host *host = NULL; qa_q3_host_client_context constructor;
    if (!lease || lease->released || !lease->presentation || !lease->keys ||
        !same_source(source(owner), &physical) ||
        frontend_key_profile_registry(lease->keys) != physical.receiver.cvars ||
        !qa_application_native_q3_client_modules_host_read(owner->modules, lease->role, &host, &constructor, error) ||
        !same_host_context(&constructor, &lease->constructor) ||
        constructor.service_owner != lease->service_owner || constructor.frontend_lifetime != lease ||
        (cinematic_only && !lease->cinematics) || !cinematic_current(lease, error) ||
        (!cinematic_only && qa_audio_engine_bus_music(owner->frontend->audio, lease->service_owner) &&
            qa_audio_engine_bus_music(owner->frontend->audio, lease->service_owner) != lease->music))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module inventory lost its retained role namespace");
    *out = (frontend_remote_q3_module_topology){.owner = owner, .index = index, .source = physical,
        .source_group = identity(owner), .service_owner = lease->service_owner,
        .physical_seat = physical_seat(owner), .role = lease->role, .host = host,
        .constructor = constructor, .modules = owner->modules, .presentation = lease->presentation,
        .assets = assets(owner), .movies = movies(owner), .cinematics = lease->cinematics,
        .mounts = mounts(owner), .keys = lease->keys,
        .equipment = lease->equipment, .music = lease->music,
        .music_attached = lease->music && qa_audio_engine_bus_music(owner->frontend->audio, lease->service_owner) == lease->music};
    return true;
}
bool frontend_remote_q3_modules_role_read(const frontend_remote_q3_modules *owner, size_t index,
    frontend_remote_q3_module_topology *out, qa_error *error)
{ return role_read(owner, index, false, out, error); }
bool frontend_remote_q3_modules_cinematics_role_read(const frontend_remote_q3_modules *owner, size_t index,
    frontend_remote_q3_module_topology *out, qa_error *error)
{ return role_read(owner, index, true, out, error); }
static bool role_current(const frontend_remote_q3_module_topology *view, bool cinematic_only)
{
    frontend_remote_q3_module_topology actual;
    return view && role_read(view->owner, view->index, cinematic_only, &actual, NULL) &&
        same_source(&view->source, &actual.source) && same_host_context(&view->constructor, &actual.constructor) &&
        view->source_group == actual.source_group && view->service_owner == actual.service_owner &&
        view->physical_seat == actual.physical_seat && view->role == actual.role && view->host == actual.host &&
        view->modules == actual.modules && view->presentation == actual.presentation && view->assets == actual.assets &&
        view->movies == actual.movies && view->cinematics == actual.cinematics &&
        view->mounts == actual.mounts && view->keys == actual.keys &&
        view->equipment == actual.equipment && view->music == actual.music && view->music_attached == actual.music_attached;
}
bool frontend_remote_q3_modules_role_current(const frontend_remote_q3_module_topology *view)
{ return role_current(view, false); }
bool frontend_remote_q3_modules_cinematics_role_current(const frontend_remote_q3_module_topology *view)
{ return role_current(view, true); }
bool frontend_remote_q3_modules_idle(const frontend_remote_q3_modules *owner)
{
    if (!owner) return true;
    if (owner->video || owner->constructing ||
        (owner->modules && !qa_application_native_q3_client_modules_idle(owner->modules))) return false;
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
    frontend_remote_q3_modules *owner = *owned; qa_frontend *f = owner->frontend;
    if (f->capture || f->resource_inventory || !frontend_remote_q3_modules_idle(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module retirement retains an actual entered child");
    for (remote_module_lease *lease = owner->leases; lease; lease = lease->next)
        if (f->music_sources && !frontend_music_sources_explicit_retire(f->music_sources, lease, error)) return false;
    owner->retiring = true;
    if (!qa_application_native_q3_client_modules_destroy(&owner->modules, error)) return false;
    while (owner->leases) {
        remote_module_lease *lease = owner->leases;
        if (!dispose_lease(lease, error)) return false;
        owner->leases = lease->next; free(lease);
    }
    if (owner->attached) {
        bool ok = owner->kind == REMOTE_MODULE_INITIAL ?
            frontend_remote_q3_initial_child_release(&owner->basis.initial.owner, error) :
            frontend_remote_q3_modules_detach(owner->basis.decoded.row, owner, error);
        if (!ok) return false;
        owner->attached = false;
    }
    frontend_remote_modules_saved_dispose(owner->saved, owner->saved_count);
    qa_buffer_free(&owner->saved_bytes);
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
            const qa_q3_gamestate *gamestate = owner->kind == REMOTE_MODULE_INITIAL ? NULL : owner->basis.decoded.view.domain.gamestate;
            if (!basis_current(owner, source(owner), gamestate, error)) return false;
            *out = (frontend_remote_q3_module_media){owner, receipt, lease->presentation, assets(owner),
                mounts(owner), physical_seat(owner), lease->media_views, 1}; return true;
        }
    return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote module media lost its initialized host lease");
}
bool frontend_remote_q3_modules_media_current(const frontend_remote_q3_module_media *view)
{
    frontend_remote_q3_module_media actual;
    return view && frontend_remote_q3_modules_media_read(view->owner, view->receipt.role, &actual, NULL) &&
        qa_application_native_q3_client_modules_receipt_current(view->owner->modules, &view->receipt) &&
        actual.presentation == view->presentation && actual.assets == view->assets &&
        actual.mounts == view->mounts && actual.physical_seat == view->physical_seat &&
        actual.media_views == view->media_views && actual.media_view_count == view->media_view_count &&
        view->media_view_count == 1 && view->media_views[0] == view->mounts;
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
