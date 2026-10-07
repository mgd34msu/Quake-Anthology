#include "native_q3_remote_role_private.h"
#include "startup_flow.h"
#include "engine_shutdown.h"
#include "qa/application_native_q3_remote_client.h"
#include "qa/application_character_selection.h"
#include "native_q3_client_modules.h"
#include "qa/console_cvar_observer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct application_native_q3_remote_role *find(application_provider *provider, uint32_t seat)
{
    for (struct application_native_q3_remote_role *row = provider ? provider->native_q3_remote_roles : NULL;
        row; row = row->next) if (row->seat == seat) return row;
    return NULL;
}
static bool native_receiver(const application_provider *provider)
{
    return provider && provider->kind == APPLICATION_PROVIDER_Q3 && provider->launch &&
        provider->launch->selection.runtime == QA_PROGRAM_BUILTIN && provider->product &&
        provider->product->family == QA_GAME_Q3 && !provider->launch->artifact;
}
bool application_native_q3_remote_client_only(const application_provider *provider)
{
    uint64_t client_roles = QA_ROLE_BIT(QA_ROLE_HUD) | QA_ROLE_BIT(QA_ROLE_MENU);
    return native_receiver(provider) && (provider->launch->roles & QA_ROLE_BIT(QA_ROLE_HUD)) &&
        !(provider->launch->roles & ~client_roles);
}
static bool capture(void *context, const qa_command_context *source, qa_command_context *out, qa_error *error)
{
    struct application_native_q3_remote_role *row = context;
    if (row->retiring || !source || (source->owner && source->owner != row->provider->owner) || source->seat != row->seat)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT console lost its selected receiver and authored seat");
    qa_command_context actual = *source;
    actual.owner = row->provider->owner; actual.dialect = QA_CONSOLE_Q3;
    return application_command_capture(row->provider->application, &actual, out, error);
}
static bool active(void *context, const qa_command_context *command)
{
    struct application_native_q3_remote_role *row = context;
    return !row->retiring && command && command->owner == row->provider->owner && command->seat == row->seat &&
        command->dialect == QA_CONSOLE_Q3 && application_command_active(row->provider->application, command);
}
static void print(void *context, const qa_command_context *command, const char *text)
{
    struct application_native_q3_remote_role *row = context;
    ++row->calls; application_console_print(row->provider->application, command, text); --row->calls;
}
static void cvar_print(void *context, const char *text)
{
    struct application_native_q3_remote_role *row = context;
    ++row->calls; application_console_print(row->provider->application, NULL, text); --row->calls;
}
static bool cheats(void *context)
{
    struct application_native_q3_remote_role *row = context;
    const qa_cvar_view *value = row->retiring ? NULL : qa_cvars_find(row->cvars, "sv_cheats");
    return value && value->integer != 0;
}
static bool read_script(void *context, const qa_command_context *command, const char *path,
    qa_bytes *out, void **lease, qa_error *error)
{
    struct application_native_q3_remote_role *row = context;
    if (!active(row, command)) return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT script source has retired");
    if (application_startup_console_active(row->provider, row->cvars))
        return application_startup_console_script_read(row->provider, row->console, command, path, out, lease, error);
    if (application_startup_source_scripts(row->provider))
        return application_startup_source_script_read(row->provider, row->console, command, path, out, lease, error);
    const qa_launch_instance *source = row->descriptor ? qa_launch_instance_lease_view(row->descriptor) : row->provider->launch;
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire(source->content, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource); *lease = resource; return true;
}
static void release_script(void *context, void *lease)
{
    struct application_native_q3_remote_role *row = context;
    if (application_startup_console_active(row->provider, row->cvars))
        application_startup_console_script_release(row->provider, row->cvars, lease);
    else if (application_startup_source_scripts(row->provider))
        application_startup_source_script_release(row->provider, row->cvars, lease);
    else qa_resource_release(lease);
}
static void script_complete(void *context, const qa_command_context *command, const char *path, bool success)
{
    struct application_native_q3_remote_role *row = context;
    application_startup_console_script_complete(row->provider, row->console, command, path, success);
}
static bool allowed(void *context, const qa_command_invocation *command)
{
    struct application_native_q3_remote_role *row = context;
    return !row->retiring && application_startup_console_command_allowed(row->provider, row->console, command);
}
static qa_command_result dispatch(void *context, const qa_command_invocation *command, qa_error *error)
{
    struct application_native_q3_remote_role *row = context;
    if (!command || command->console != row->console || !active(row, &command->context) || row->calls == SIZE_MAX) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT command lost its physical console and origin");
        return QA_COMMAND_FAILED;
    }
    ++row->calls;
    qa_command_result result = QA_COMMAND_UNHANDLED;
    qa_application_q3_role_receipt ui;
    bool ui_present = false;
    if (row->modules) {
        if (!qa_application_native_q3_client_modules_optional_receipt_read(row->modules, QA_QVM_UI,
            &ui, &ui_present, error)) result = QA_COMMAND_FAILED;
    }
    qa_command_invocation continued=*command;
    continued.receiver=continued.registration_owner=0;
    if (result != QA_COMMAND_FAILED && row->modules && row->acquired_initialized) {
        qa_application_q3_role_receipt cgame;
        bool present=false;
        bool okay=qa_application_native_q3_client_modules_optional_receipt_read(row->modules,QA_QVM_CGAME,
            &cgame,&present,error);
        if (okay && present && qa_console_invocation_delivered(command,cgame.receiver,cgame.service_owner)) {
            result=QA_COMMAND_UNHANDLED;
        } else {
            int32_t handled=0;
            if (okay) okay=qa_application_native_q3_client_modules_console_command(row->modules,QA_QVM_CGAME,
                &continued,0,&handled,error);
            result=okay ? handled ? QA_COMMAND_HANDLED : QA_COMMAND_UNHANDLED : QA_COMMAND_FAILED;
        }
    } else if (result != QA_COMMAND_FAILED && row->service)
        result = qa_native_q3_remote_client_command(row->service, command, error);
    if (result == QA_COMMAND_UNHANDLED && ui_present) {
        int32_t milliseconds = 0, handled = 0;
        bool okay = qa_application_native_q3_client_modules_receipt_current(row->modules, &ui);
        if (!okay) application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT UI command lost its successful Init receipt");
        else if (row->transport)
            okay = qa_native_q3_remote_client_transport_milliseconds(row->transport, command, &milliseconds, error);
        else if (row->service)
            okay = qa_native_q3_remote_client_milliseconds(row->service, command, &milliseconds, error);
        else okay = application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT UI command has no retained frontend clock");
        if (okay) okay = qa_application_native_q3_client_modules_console_command(row->modules, QA_QVM_UI,
            &continued, milliseconds, &handled, error);
        result = okay ? handled ? QA_COMMAND_HANDLED : QA_COMMAND_UNHANDLED : QA_COMMAND_FAILED;
    }
    if (!active(row, &command->context)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT command changed its captured source");
        result = QA_COMMAND_FAILED;
    }
    --row->calls; return result;
}
static qa_command_result forward(void *context, const qa_command_invocation *command, qa_error *error)
{
    struct application_native_q3_remote_role *row = context;
    bool delivered=command && !command->context.owner && !row->retiring &&
        command->context.cvar_view==qa_cvars_view_identity(row->provider->application->cvars) &&
        qa_console_invocation_delivered_view(command,qa_cvars_view_identity(row->cvars),
            row->provider->owner,row->service_owner) &&
        qa_application_command_context_active(row->provider->application,&command->context);
    if (!command || command->console != row->console ||
        (!active(row, &command->context) && !delivered) || row->calls == SIZE_MAX) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT forwarding lost its physical console and origin");
        return QA_COMMAND_FAILED;
    }
    if (!row->transport && !row->service) return QA_COMMAND_UNHANDLED;
    ++row->calls;
    bool okay = row->transport ? qa_native_q3_remote_client_transport_forward(row->transport, command, error) :
        qa_native_q3_remote_client_forward(row->service, command, error);
    --row->calls; return okay ? QA_COMMAND_HANDLED : QA_COMMAND_FAILED;
}
static const qa_launch_binding *hud_binding(const qa_launch_choices *choices, const qa_launch_seat *seat)
{
    if (seat->actor.generation)
        for (size_t i = 0; i < choices->binding_count; ++i) {
            const qa_launch_binding *binding = &choices->bindings[i];
            if (binding->role == QA_ROLE_HUD && binding->scope.kind == QA_SCOPE_ACTOR &&
                qa_actor_id_equal(binding->scope.actor, seat->actor) && !*binding->selector) return binding;
        }
    const qa_launch_binding *binding = qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = seat->id}, QA_ROLE_HUD, "");
    return binding ? binding : qa_launch_binding_for(choices, (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_HUD, "");
}
static bool selected_seat(const qa_launch_instance *launch, const qa_launch_choices *choices, const qa_launch_seat *seat)
{
    const qa_launch_binding *binding = hud_binding(choices, seat);
    return seat->local && !seat->bot && binding && !strcmp(binding->instance, launch->selection.instance);
}
bool application_native_q3_remote_role_selected(const qa_launch_instance *launch,
    const qa_launch_choices *choices, uint32_t seat)
{
    for (size_t i=0; choices && i<choices->seat_count; ++i)
        if (choices->seats[i].id==seat)
            return selected_seat(launch, choices, choices->seats+i);
    return false;
}

static bool engine_defaults(struct application_native_q3_remote_role *row, const qa_launch_choices *choices,
    const qa_launch_seat *seat, qa_error *error)
{
    static const struct { const char *name, *value; uint32_t flags; } defaults[] = {
        {"cl_allowDownload","0",QA_CVAR_ARCHIVE}, {"cl_timeNudge","0",QA_CVAR_TEMPORARY},
        {"rate","25000",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO}, {"cl_maxpackets","30",QA_CVAR_ARCHIVE},
        {"cl_packetdup","1",QA_CVAR_ARCHIVE}, {"snaps","20",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"name","Player",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO}, {"color1","4",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"color2","5",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO}, {"sex","male",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"cl_anonymous","0",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO}, {"cg_predictItems","1",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"teamtask","0",QA_CVAR_USERINFO}, {"password","",QA_CVAR_USERINFO}, {"handicap","100",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"cl_maxPing","800",QA_CVAR_ARCHIVE}, {"cl_serverStatusResendTime","750",0}, {"sv_master1","master.quake3arena.com",0}
    };
    for (size_t i = 0; i < sizeof(defaults) / sizeof(*defaults); ++i) {
        const char *value = !strcmp(defaults[i].name, "name") && seat->name ? seat->name : defaults[i].value;
        if (!qa_cvars_register(row->cvars, defaults[i].name, value, defaults[i].flags, row->service_owner,
            "Native Q3 CLIENT engine policy", error)) return false;
    }
    qa_application_character_declaration declaration; bool found;
    if (!qa_application_character_declaration_read(row->provider->product_catalog, choices, seat, &declaration, &found, error)) return false;
    if (!found) return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT requires its actual selected CHARACTER declaration");
    const qa_native_q3_character_declaration *appearance = &declaration.appearance;
    const char *models[] = {appearance->model, *appearance->head_model ? appearance->head_model : appearance->model};
    const char *skins[] = {appearance->skin, appearance->head_skin};
    const char *names[2][2] = {{"model", "team_model"}, {"headmodel", "team_headmodel"}};
    for (size_t i = 0; i < 2; ++i) {
        size_t a = strlen(models[i]), b = strlen(skins[i]);
        if (b > SIZE_MAX - 2 || a > SIZE_MAX - b - 2)
            return application_fail(error, QA_ERROR_MEMORY, "Native CLIENT CHARACTER userinfo exceeds capacity");
        char *value = malloc(a + b + 2);
        if (!value) return application_fail(error, QA_ERROR_MEMORY, "Retaining native CLIENT CHARACTER userinfo");
        memcpy(value, models[i], a); value[a] = '/'; memcpy(value + a + 1, skins[i], b + 1);
        bool ok = qa_cvars_register(row->cvars, names[i][0], value, QA_CVAR_ARCHIVE | QA_CVAR_USERINFO,
            row->service_owner, "Selected native CLIENT CHARACTER", error) &&
            qa_cvars_register(row->cvars, names[i][1], value, QA_CVAR_ARCHIVE | QA_CVAR_USERINFO,
                row->service_owner, "Selected native CLIENT CHARACTER", error);
        free(value); if (!ok) return false;
    }
    return true;
}
static bool prepare_seat(application_provider *provider, const qa_launch_choices *choices, const qa_launch_seat *seat, qa_error *error)
{
    if (find(provider, seat->id)) return true;
    struct application_native_q3_remote_role *row = calloc(1, sizeof(*row));
    if (!row) return application_fail(error, QA_ERROR_MEMORY, "Retaining native CLIENT console");
    row->provider = provider; row->seat = seat->id;
    char name[160];
    snprintf(name, sizeof(name), "q3-native-client:%u:%llu:%u", provider->owner,
        (unsigned long long)provider->launch->identity, seat->id);
    qa_cvar_options cvars = {.dialect = QA_CONSOLE_Q3,
        .side = QA_CVAR_SIDE_CLIENT, .role = QA_CVAR_ROLE_CGAME, .seat = row->seat, .user = row, .print = cvar_print, .cheats_allowed = cheats};
    row->cvars = qa_cvars_create_view(provider->application->cvars, &cvars, error);
    qa_console_options options = {.context = {.owner = provider->owner, .seat = seat->id,
        .dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_SEAT}, .cvars = row->cvars, .user = row,
        .print = print, .capture_context = capture, .context_active = active, .read_script = read_script,
        .release_script = release_script, .script_complete = script_complete, .allow_command = allowed,
        .source_command = dispatch, .forward = forward};
    options.context.cvar_view = qa_cvars_view_identity(row->cvars);
    if (row->cvars && qa_console_bind_source(provider->application->console, &options, error))
        row->console = provider->application->console;
    if (!row->console || !qa_strings_intern_cstr(qa_session_strings(provider->application->session), name,
        &row->service_owner, error) ||
        !engine_defaults(row, choices, seat, error)) {
        qa_console_unbind_source(row->console, qa_cvars_view_identity(row->cvars), error); qa_cvars_destroy(row->cvars); free(row); return false;
    }
    struct application_native_q3_remote_role **tail = &provider->native_q3_remote_roles;
    while (*tail) tail = &(*tail)->next;
    *tail = row; return true;
}
bool application_native_q3_remote_roles_prepare(application_provider *provider, const qa_launch_choices *choices, qa_error *error)
{
    if (!native_receiver(provider) || !choices)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT preparation requires its selected builtin Q3 descriptor");
    for (size_t i = 0; i < choices->seat_count; ++i) {
        const qa_launch_seat *seat = &choices->seats[i];
        if (selected_seat(provider->launch, choices, seat) &&
            !prepare_seat(provider, choices, seat, error)) return false;
    }
    return true;
}
bool application_native_q3_remote_role_source_at(application_provider *provider, size_t index,
    qa_application_startup_source *out, bool *found, qa_error *error)
{
    if (!native_receiver(provider) || !out || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT inventory requires its actual source owner");
    struct application_native_q3_remote_role *row = provider->native_q3_remote_roles;
    while (row && index) { row = row->next; --index; }
    *found = row != NULL;
    if (row) *out = (qa_application_startup_source){.descriptor = provider->launch,
        .scope = {.provider = provider->owner, .kind = QA_APPLICATION_CONSOLE_Q3_CGAME, .seat = row->seat},
        .console = row->console, .cvars = row->cvars, .declaration_owner = provider->owner,
        .command = {.owner = provider->owner, .seat = row->seat,
            .cvar_view = qa_cvars_view_identity(row->cvars), .dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_SEAT}};
    return true;
}
bool application_native_q3_remote_role_configuration(application_provider *provider, uint32_t seat,
    qa_application_startup_source *out, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || row->retiring || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT configuration lost its physical authored seat");
    for (size_t i = 0; ; ++i) {
        bool found;
        if (!application_native_q3_remote_role_source_at(provider, i, out, &found, error)) return false;
        if (!found) return application_fail(error, QA_ERROR_NOT_FOUND, "Native CLIENT console left its actual inventory");
        if (out->console == row->console && out->cvars == row->cvars && out->scope.seat == row->seat) return true;
    }
}
bool application_native_q3_remote_roles_preinit(application_provider *provider, qa_error *error)
{
    for (size_t i = 0; ; ++i) {
        qa_application_startup_source source; bool found;
        if (!application_native_q3_remote_role_source_at(provider, i, &source, &found, error)) return false;
        if (!found) return true;
        if (!application_startup_tuple_preinit(provider, &source, error) ||
            !application_startup_apply_latched(provider, source.cvars, error)) return false;
    }
}
static bool replace_ready(const struct application_native_q3_remote_role *row)
{
    return row && !row->retiring && !row->service && !row->transport && !row->modules && !row->calls && qa_console_idle(row->console) &&
        qa_cvars_observer_idle(row->cvars);
}
bool application_native_q3_remote_role_unborrowed(application_provider *provider,uint32_t seat)
{ return replace_ready(find(provider,seat)); }
bool application_native_q3_remote_role_context(application_provider *provider, uint32_t seat,
    qa_application_q3_client_context *out, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!native_receiver(provider) || !row || row->retiring || !out || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native remote CLIENT context lost its physical receiver");
    *out = (qa_application_q3_client_context){.session = provider->application->session, .receiver = provider->owner,
        .seat = seat, .source_client = UINT32_MAX, .service_owner = row->service_owner, .frontend_lifetime = row,
        .console = row->console, .cvars = row->cvars, .client_time_cvars = row->cvars,
        .client_time_owner = provider->owner, .command_context = {.owner = provider->owner, .seat = seat,
            .cvar_view = qa_cvars_view_identity(row->cvars),
            .dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_SEAT}, .native_source = true, .initialized = row->initialized};
    return true;
}
bool application_native_q3_remote_role_current(application_provider *provider, const qa_application_q3_client_context *source)
{
    qa_application_q3_client_context actual;
    return source && application_native_q3_remote_role_context(provider, source->seat, &actual, NULL) &&
        source->session == actual.session && source->receiver == actual.receiver && !source->source_owner &&
        qa_actor_id_equal(source->source_actor, (qa_actor_id){0}) && !source->source_cvars &&
        source->service_owner == actual.service_owner && source->frontend_lifetime == actual.frontend_lifetime &&
        source->console == actual.console && source->cvars == actual.cvars && source->client_time_cvars == actual.cvars &&
        source->client_time_owner == actual.receiver && source->command_context.owner == actual.receiver &&
        source->command_context.seat == actual.seat && source->command_context.cvar_view == actual.command_context.cvar_view &&
        source->command_context.dialect == QA_CONSOLE_Q3 &&
        source->command_context.origin == QA_COMMAND_SEAT && !source->command_context.client &&
        qa_actor_id_equal(source->command_context.actor, (qa_actor_id){0}) &&
        source->native_source && source->initialized == actual.initialized;
}
bool application_native_q3_remote_role_attach(application_provider *provider, uint32_t seat,
    qa_native_q3_remote_client_service *service, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || row->retiring || row->service || row->transport || row->calls || !service ||
        !qa_console_idle(row->console) || !qa_cvars_observer_idle(row->cvars))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native remote service attachment requires its prepared physical CLIENT");
    row->service = service; row->lifecycle = NATIVE_Q3_REMOTE_ATTACHED; return true;
}
bool application_native_q3_remote_role_service_read(application_provider *provider, uint32_t seat,
    qa_native_q3_remote_client_service **out, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || row->retiring || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT attachment lost its physical receiver");
    *out = row->service; return true;
}
bool application_native_q3_remote_role_transport_attach(application_provider *provider,
    const qa_application_q3_remote_source *source, qa_native_q3_remote_client_transport *transport, qa_error *error)
{
    struct application_native_q3_remote_role *row = source ? find(provider, source->receiver.seat) : NULL;
    if (!row || !transport || row->transport || row->service || row->calls || row->retiring ||
        !qa_console_idle(row->console) || !qa_cvars_observer_idle(row->cvars) ||
        !application_native_q3_remote_role_source_current(provider, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote transport attachment lost its actual pre-CGAME CLIENT");
    row->connection_epoch = source->connection_epoch;
    row->transport = transport; row->lifecycle = NATIVE_Q3_REMOTE_ATTACHED; return true;
}
bool application_native_q3_remote_role_transport_current(application_provider *provider, uint32_t seat,
    const qa_native_q3_remote_client_transport *transport)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    return row && !row->retiring && transport && row->transport == transport;
}
bool application_native_q3_remote_role_transport_detach_ready(application_provider *provider, uint32_t seat,
    const qa_native_q3_remote_client_transport *transport, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || !transport || row->transport != transport || row->calls ||
        !qa_console_idle(row->console) || !qa_cvars_observer_idle(row->cvars))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote transport detach retains a physical CLIENT invocation");
    return true;
}
bool application_native_q3_remote_role_transport_detach(application_provider *provider, uint32_t seat,
    const qa_native_q3_remote_client_transport *transport, qa_error *error)
{
    if (!application_native_q3_remote_role_transport_detach_ready(provider, seat, transport, error)) return false;
    find(provider, seat)->transport = NULL; return true;
}
bool application_native_q3_remote_role_initialized(application_provider *provider, uint32_t seat,
    qa_native_q3_remote_client_service *service, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || row->retiring || !service || row->service != service || row->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native remote Init completion lost its actual service attachment");
    row->initialized = true; return true;
}
bool application_native_q3_remote_role_video_reset(application_provider *provider,uint32_t seat,
    qa_native_q3_remote_client_service *service,qa_error *error)
{
    struct application_native_q3_remote_role *row=find(provider,seat);
    if(!row || row->retiring || row->calls || row->module_calls || row->modules ||
        !service || row->service!=service || !qa_native_q3_remote_client_idle(service))
        return application_fail(error,QA_ERROR_ARGUMENT,"Remote CG video reset requires its actual returned compiled service");
    row->initialized=false; return true;
}
bool application_native_q3_remote_role_detach_ready(application_provider *provider, uint32_t seat,
    const qa_native_q3_remote_client_service *service, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || !service || row->service != service || row->modules || row->calls)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native remote service is borrowed or no longer attached");
    return true;
}
bool application_native_q3_remote_role_detach(application_provider *provider, uint32_t seat,
    qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!application_native_q3_remote_role_detach_ready(provider, seat, service, error)) return false;
    struct application_native_q3_remote_role *row = find(provider, seat);
    row->initialized = false; row->service = NULL; return true;
}
bool application_native_q3_remote_role_command(application_provider *provider, uint32_t seat,
    const qa_q3_tokens *tokens, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || row->retiring || !tokens || tokens->truncated || row->argument_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT argv lost its actual command owner");
    size_t bytes = 0;
    for (size_t i = 0; i < tokens->count; ++i) bytes += strlen(qa_q3_token(tokens, i)) + 1;
    qa_command_tokens copy = {.count = tokens->count};
    copy.values = calloc(tokens->count ? tokens->count : 1, sizeof(*copy.values));
    copy.storage = malloc(bytes ? bytes : 1); copy.args_text = malloc(bytes + 1);
    if (!copy.values || !copy.storage || !copy.args_text) {
        qa_command_tokens_free(&copy); return application_fail(error, QA_ERROR_MEMORY, "Retaining reached native CLIENT arguments");
    }
    size_t cursor = 0, args = 0;
    for (size_t i = 0; i < tokens->count; ++i) {
        const char *value = qa_q3_token(tokens, i); size_t length = strlen(value);
        copy.values[i] = copy.storage + cursor; memcpy(copy.storage + cursor, value, length + 1); cursor += length + 1;
        if (i) { if (i > 1) copy.args_text[args++] = ' '; memcpy(copy.args_text + args, value, length); args += length; }
    }
    copy.args_text[args] = 0;
    qa_command_tokens_free(&row->arguments); row->arguments = copy; ++row->argument_revision; return true;
}
bool application_native_q3_remote_role_arguments(application_provider *provider, uint32_t seat,
    const qa_command_tokens **out, uint64_t *revision, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || row->retiring || !out || !revision)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT argument view lost its physical owner");
    *out = &row->arguments; *revision = row->argument_revision; return true;
}
static bool same_name(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return !*a && !*b;
}
bool application_native_q3_remote_role_system_info(application_provider *provider, uint32_t seat, const char *info, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || row->retiring || !info || row->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native SystemInfo requires its actual configured CLIENT registry");
    size_t length = strlen(info);
    char *retained = malloc(length + 1), *working = malloc(length + 1);
    if (!retained || !working) {
        free(retained); free(working); return application_fail(error, QA_ERROR_MEMORY, "Retaining native CLIENT SystemInfo");
    }
    memcpy(retained, info, length + 1); memcpy(working, info, length + 1);
    ++row->calls; bool ok = true;
    char *cursor = working; if (*cursor == '\\') ++cursor;
    while (ok && *cursor) {
        char *name = cursor, *separator = strchr(cursor, '\\'); if (!separator) break;
        *separator = 0; char *value = separator + 1; separator = strchr(value, '\\');
        if (separator) { *separator = 0; cursor = separator + 1; } else cursor = value + strlen(value);
        /* These controls already belong to the same physical CLIENT time
         * registry; received source time does not overwrite that owner. */
        if (*name && !same_name(name, "cl_allowdownload") && !same_name(name, "timescale") &&
            !same_name(name, "fixedtime") && !same_name(name, "com_cameraMode"))
            ok = qa_cvars_set(row->cvars, name, value, true, error) && !row->retiring;
    }
    --row->calls; free(working);
    if (ok) { free(row->system_info); row->system_info = retained; } else free(retained);
    return ok;
}
bool application_native_q3_remote_role_product(application_provider *provider, uint32_t seat, qa_q3_product *out, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!native_receiver(provider) || !row || row->retiring || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT product lost its real compiled receiver");
    *out = !strcmp(provider->product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA; return true;
}
bool application_native_q3_remote_role_source_read(application_provider *provider, uint32_t seat, uint64_t epoch,
    qa_application_q3_remote_source *out, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || row->retiring || !out || !epoch || (row->connection_epoch && row->connection_epoch != epoch))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT source differs from its actual connection epoch");
    qa_application_q3_remote_source source = {.descriptor = row->descriptor ? qa_launch_instance_lease_view(row->descriptor) : provider->launch,
        .configuration_generation = row->configuration_generation ? row->configuration_generation :
            qa_application_configuration_generation(provider->application), .connection_epoch = epoch};
    if (!source.configuration_generation || !application_native_q3_remote_role_context(provider, seat, &source.receiver, error)) return false;
    *out = source; return true;
}
bool application_native_q3_remote_role_source_current(application_provider *provider, const qa_application_q3_remote_source *source)
{
    qa_application_q3_remote_source actual;
    return source && application_native_q3_remote_role_source_read(provider, source->receiver.seat, source->connection_epoch, &actual, NULL) &&
        source->descriptor && source->descriptor->storage == actual.descriptor->storage && source->descriptor->content == actual.descriptor->content &&
        source->descriptor->identity == actual.descriptor->identity &&
        source->configuration_generation == actual.configuration_generation &&
        application_native_q3_remote_role_current(provider, &source->receiver);
}
bool application_native_q3_remote_role_descriptor_bind(application_provider *provider, uint32_t seat,
    const qa_launch_instance *descriptor, uint64_t epoch, uint64_t generation, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!replace_ready(row) || row->lifecycle == NATIVE_Q3_REMOTE_ATTACHED ||
        !native_receiver(provider) || !descriptor || !descriptor->storage || !descriptor->content ||
        descriptor->artifact || descriptor->selection.runtime != QA_PROGRAM_BUILTIN || !epoch || !generation ||
        strcmp(descriptor->selection.instance, provider->launch->selection.instance) ||
        strcmp(descriptor->selection.implementation, provider->launch->selection.implementation))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT content binding lost its actual compiled descriptor");
    qa_launch_instance_lease *lease = NULL;
    if (!qa_launch_instance_retain_metadata(descriptor, &lease, error)) return false;
    qa_launch_instance_lease_release(row->descriptor); row->descriptor = lease;
    row->connection_epoch = epoch; row->configuration_generation = generation;
    row->lifecycle = NATIVE_Q3_REMOTE_COLD; return true;
}
bool application_native_q3_remote_role_modules_attach(application_provider *provider,
    const qa_application_q3_remote_source *source, application_native_q3_client_modules *modules, qa_error *error)
{
    struct application_native_q3_remote_role *row = source ? find(provider, source->receiver.seat) : NULL;
    if (!row || !modules || row->modules || row->retiring || row->calls || !qa_console_idle(row->console) ||
        !qa_cvars_observer_idle(row->cvars) || !application_native_q3_remote_role_source_current(provider, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native client modules require their current physical CLIENT slot");
    row->connection_epoch = source->connection_epoch;
    row->module_generation = source->configuration_generation;
    row->modules = modules; row->lifecycle = NATIVE_Q3_REMOTE_ATTACHED; return true;
}
bool application_native_q3_remote_role_modules_pointer_read(application_provider *provider, uint32_t seat,
    application_native_q3_client_modules **out, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!native_receiver(provider) || !row || row->retiring || !out || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native client module inventory lost its physical CLIENT row");
    *out = row->modules; return true;
}
bool application_native_q3_remote_role_modules_read(application_provider *provider,
    const qa_application_q3_remote_source *source, application_native_q3_client_modules **out, qa_error *error)
{
    struct application_native_q3_remote_role *row = source ? find(provider, source->receiver.seat) : NULL;
    if (!row || !out || !application_native_q3_remote_role_source_current(provider, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native client module inventory lost its current physical CLIENT");
    *out = row->modules; return true;
}
bool application_native_q3_remote_role_modules_source_read(application_provider *provider, uint32_t seat,
    qa_application_q3_remote_source *source, application_native_q3_client_modules **modules, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || !source || !modules || !row->connection_epoch)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native client module lookup lost its retained connection epoch");
    qa_application_q3_remote_source actual;
    application_native_q3_client_modules *attached = NULL;
    if (!application_native_q3_remote_role_source_read(provider, seat, row->connection_epoch, &actual, error) ||
        !application_native_q3_remote_role_modules_read(provider, &actual, &attached, error)) return false;
    *source = actual; *modules = attached; return true;
}
bool application_native_q3_remote_role_modules_current(application_provider *provider,
    const qa_application_q3_remote_source *source, const application_native_q3_client_modules *modules)
{
    struct application_native_q3_remote_role *row = source ? find(provider, source->receiver.seat) : NULL;
    return row && modules && row->modules == modules &&
        application_native_q3_remote_role_source_current(provider, source);
}
bool application_native_q3_remote_role_modules_initialized(application_provider *provider,
    const qa_application_q3_remote_source *source, const application_native_q3_client_modules *modules,
    qa_error *error)
{
    struct application_native_q3_remote_role *row = source ? find(provider, source->receiver.seat) : NULL;
    qa_application_q3_role_receipt receipt;
    if (!row || row->calls || row->module_calls ||
        !application_native_q3_remote_role_modules_current(provider, source, modules) ||
        !qa_application_native_q3_client_modules_idle(modules) ||
        !qa_application_native_q3_client_modules_receipt_read(modules, QA_QVM_CGAME, &receipt, error) ||
        !qa_application_native_q3_client_modules_receipt_current(modules, &receipt))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CGAME completion lost its successful current Init receipt");
    row->acquired_initialized = true; return true;
}
bool application_native_q3_remote_role_modules_initialized_read(application_provider *provider,
    const qa_application_q3_remote_source *source, const application_native_q3_client_modules *modules,
    bool *out, qa_error *error)
{
    struct application_native_q3_remote_role *row = source ? find(provider, source->receiver.seat) : NULL;
    qa_application_q3_role_receipt receipt;
    if (!out || !row || !application_native_q3_remote_role_modules_current(provider, source, modules) ||
        (row->acquired_initialized &&
            (!qa_application_native_q3_client_modules_receipt_read(modules, QA_QVM_CGAME, &receipt, error) ||
             !qa_application_native_q3_client_modules_receipt_current(modules, &receipt))))
        return application_fail(error, QA_ERROR_ARGUMENT, "Acquired CGAME completion lost its physical receipt owner");
    *out = row->acquired_initialized; return true;
}
bool application_native_q3_remote_role_modules_retained(application_provider *provider,
    const qa_application_q3_remote_source *source, const application_native_q3_client_modules *modules)
{
    struct application_native_q3_remote_role *row = source ? find(provider, source->receiver.seat) : NULL;
    if (!native_receiver(provider) || !provider->application || !row || row->provider != provider ||
        !modules || row->modules != modules || !source->descriptor || !row->connection_epoch ||
        source->connection_epoch != row->connection_epoch || !row->module_generation ||
        source->configuration_generation != row->module_generation ||
        (row->configuration_generation && source->configuration_generation != row->configuration_generation)) return false;
    const qa_launch_instance *descriptor = row->descriptor ? qa_launch_instance_lease_view(row->descriptor) : provider->launch;
    const qa_application_q3_client_context *receiver = &source->receiver;
    const qa_command_context *command = &receiver->command_context;
    return source->descriptor->storage == descriptor->storage && source->descriptor->content == descriptor->content &&
        source->descriptor->identity == descriptor->identity &&
        receiver->session == provider->application->session && receiver->receiver == provider->owner &&
        receiver->seat == row->seat && receiver->service_owner == row->service_owner &&
        receiver->frontend_lifetime == row && receiver->console == row->console && receiver->cvars == row->cvars &&
        receiver->client_time_cvars == row->cvars && receiver->client_time_owner == provider->owner &&
        !receiver->source_owner && !receiver->source_cvars && qa_actor_id_equal(receiver->source_actor, (qa_actor_id){0}) &&
        receiver->native_source && command->owner == provider->owner && command->seat == row->seat &&
        command->cvar_view == qa_cvars_view_identity(row->cvars) &&
        command->dialect == QA_CONSOLE_Q3 && command->origin == QA_COMMAND_SEAT && !command->client &&
        qa_actor_id_equal(command->actor, (qa_actor_id){0});
}
bool application_native_q3_remote_role_modules_borrow(application_provider *provider,
    const qa_application_q3_remote_source *source, const application_native_q3_client_modules *modules, qa_error *error)
{
    if (!application_native_q3_remote_role_modules_current(provider, source, modules))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native client module borrow lost its retained physical CLIENT");
    struct application_native_q3_remote_role *row = find(provider, source->receiver.seat);
    if (row->calls == SIZE_MAX || row->module_calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "Native client module borrow exceeds capacity");
    ++row->calls; ++row->module_calls; return true;
}
bool application_native_q3_remote_role_modules_return(application_provider *provider, uint32_t seat,
    const application_native_q3_client_modules *modules, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || !modules || row->modules != modules || !row->calls || !row->module_calls)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native client module return lost its actual borrow");
    --row->calls; --row->module_calls; return true;
}
bool application_native_q3_remote_role_modules_detach(application_provider *provider, uint32_t seat,
    const application_native_q3_client_modules *modules, qa_error *error)
{
    struct application_native_q3_remote_role *row = find(provider, seat);
    if (!row || !modules || row->modules != modules || row->calls || !qa_console_idle(row->console) ||
        !qa_cvars_observer_idle(row->cvars))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native client module detach retains a physical CLIENT borrow");
    row->modules = NULL; row->module_generation = 0; row->acquired_initialized = false; return true;
}
bool application_native_q3_remote_role_module_sequence_read(application_provider *provider,
    const qa_application_q3_remote_source *source, uint64_t *out, qa_error *error)
{
    struct application_native_q3_remote_role *row = source ? find(provider, source->receiver.seat) : NULL;
    if (!row || !out || !application_native_q3_remote_role_source_current(provider, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native module sequence lost its current physical CLIENT");
    *out = row->module_sequence; return true;
}
bool application_native_q3_remote_role_module_sequence_reserve(application_provider *provider,
    const qa_application_q3_remote_source *source, const application_native_q3_client_modules *modules,
    uint64_t *out, qa_error *error)
{
    if (!out || !application_native_q3_remote_role_modules_current(provider, source, modules) ||
        provider->application->operation == APPLICATION_PERSISTING)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native module lifetime requires its genuine fresh CLIENT owner");
    struct application_native_q3_remote_role *row = find(provider, source->receiver.seat);
    if (row->calls || row->module_sequence == UINT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native module lifetime retains a borrow or exhausted sequence");
    *out = ++row->module_sequence; return true;
}
bool application_native_q3_remote_roles_idle(const application_provider *provider)
{
    for (const struct application_native_q3_remote_role *row = provider ? provider->native_q3_remote_roles : NULL;
        row; row = row->next)
        if (row->calls || (row->service && !qa_native_q3_remote_client_idle(row->service)) || !qa_console_idle(row->console) ||
            (row->transport && !qa_native_q3_remote_client_transport_idle(row->transport)) ||
            (!row->retiring && !qa_cvars_observer_idle(row->cvars))) return false;
    return true;
}
bool application_native_q3_remote_role_retirement(application_provider *provider,
    const qa_application_startup_source *source)
{
    struct application_native_q3_remote_role *row=source?find(provider,source->scope.seat):NULL;
    return native_receiver(provider) && row && row->retiring && !row->modules && !row->service &&
        !row->transport && !row->calls && qa_console_idle(row->console) && source->descriptor &&
        source->descriptor->storage==provider->launch->storage && source->scope.provider==provider->owner &&
        source->scope.kind==QA_APPLICATION_CONSOLE_Q3_CGAME && source->console==row->console &&
        source->cvars==row->cvars && source->declaration_owner==provider->owner;
}

static bool role_destroy(application_provider *provider,
    struct application_native_q3_remote_role **link, qa_error *error)
{
    struct application_native_q3_remote_role *row=*link;
    if (row->modules || row->service || row->transport || row->calls ||
        !qa_console_idle(row->console) || (!row->retiring && !qa_cvars_observer_idle(row->cvars)))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Native CLIENT teardown retains its actual modules, services or callbacks");
    qa_application_startup_source source;
    bool found=false;
    for (size_t i=0;;++i) {
        if (!application_native_q3_remote_role_source_at(provider,i,&source,&found,error)) return false;
        if (!found)
            return application_fail(error,QA_ERROR_ARGUMENT,"Native CLIENT teardown lost its physical console");
        if (source.console==row->console && source.cvars==row->cvars) break;
    }
    row->retiring=true;
    bool retired=provider->attached
        ? application_startup_tuple_retire_client(provider,&source,error)
        : application_startup_tuple_retire(provider,&source,error);
    if (!retired || !application_startup_flow_release_view(provider,row->cvars,error)) return false;
    if (!qa_console_unbind_source(row->console, qa_cvars_view_identity(row->cvars), error)) return false;
    *link=row->next;
    qa_cvars_remove_owner(row->cvars, row->service_owner);
    qa_cvars_detach_callbacks(row->cvars); qa_cvars_destroy(row->cvars);
    qa_command_tokens_free(&row->arguments); qa_buffer_free(&row->modules_restore);
    qa_launch_instance_lease_release(row->descriptor); free(row->system_info); free(row);
    return true;
}

bool application_native_q3_remote_roles_retain(application_provider *provider,
    const qa_launch_choices *choices, qa_error *error)
{
    struct application_native_q3_remote_role **link=&provider->native_q3_remote_roles;
    while (*link) {
        if (application_native_q3_remote_role_selected(provider->launch,choices,(*link)->seat))
            link=&(*link)->next;
        else if (!role_destroy(provider,link,error)) return false;
    }
    return true;
}

bool application_native_q3_remote_roles_destroy(application_provider *provider, qa_error *error)
{
    if (!application_native_q3_remote_roles_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT consoles retain actual service leases");
    return !provider || application_native_q3_remote_roles_retain(provider,NULL,error);
}
