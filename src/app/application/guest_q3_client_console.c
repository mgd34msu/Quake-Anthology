#include "guest_q3_client_console.h"
#include "startup_flow.h"
#include "q3_product.h"
#include "qa/application_q3_factory.h"
#include "engine_shutdown.h"
#include "qa/script_defines_save.h"
#include "qa/bots_allocator_save.h"
#include "qa/console_cvar_observer.h"

typedef enum client_configuration_state {
    CLIENT_CONFIGURATION_LIVE, CLIENT_CONFIGURATION_SHUTDOWN,
    CLIENT_CONFIGURATION_RETIRING, CLIENT_CONFIGURATION_RETIRED
} client_configuration_state;

struct application_guest_q3_client_console {
    struct application_guest_q3_client_console *next;
    struct application_q3_guest *engine;
    qa_qvm_role kind;
    uint32_t seat;
    qa_console *console;
    qa_cvars *cvars;
    qa_script_defines *script_globals;
    qa_bot_memory *script_memory;
    qa_string_id script_globals_owner;
    size_t calls;
    application_provider *retiring_game;
    qa_application_startup_source retiring_source;
    qa_cvars *binding_cvars;
    client_configuration_state configuration;
    bool retiring;
};

static bool available(const struct application_guest_q3_client_console *row)
{ return !row->retiring && row->configuration < CLIENT_CONFIGURATION_RETIRING; }
static bool globals_close(struct application_guest_q3_client_console *row,qa_error *error)
{
    qa_script_defines_release(row->script_globals); row->script_globals=NULL;
    if(!qa_bot_memory_release(row->script_memory,error)) return false;
    row->script_memory=NULL; return true;
}

static struct application_guest_q3_client_console *find(struct application_q3_guest *engine,
    qa_qvm_role kind, uint32_t seat)
{
    for (struct application_guest_q3_client_console *row = engine ? engine->client_preparation : NULL;
        row; row = row->next) if (row->kind == kind && row->seat == seat) return row;
    return NULL;
}
static bool capture(void *context, const qa_command_context *source, qa_command_context *out, qa_error *error)
{
    struct application_guest_q3_client_console *row = context;
    application_provider *provider = row->engine->provider;
    if (!available(row) || !source || (source->owner && source->owner != provider->owner) || source->seat != row->seat)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT console lost its actual receiver and authored seat");
    qa_command_context command = *source;
    command.owner = provider->owner; command.dialect = QA_RULESET_Q3;
    return application_command_capture(provider->application, &command, out, error);
}
static bool active(void *context, const qa_command_context *command)
{
    struct application_guest_q3_client_console *row = context;
    return available(row) && command && command->owner == row->engine->provider->owner && command->seat == row->seat &&
        command->dialect == QA_RULESET_Q3 && application_command_active(row->engine->provider->application, command);
}
static void print(void *context, const qa_command_context *command, const char *text)
{
    struct application_guest_q3_client_console *row = context;
    ++row->calls; application_console_print(row->engine->provider->application, command, text); --row->calls;
}
static void cvar_print(void *context, const char *text)
{ application_console_print(context, NULL, text); }
static bool cheats(void *context)
{
    qa_application *app = context;
    qa_cvars *engine = app->cvars ? app->cvars :
        application_engine_shutdown_cvars(app->engine_shutdown_provider);
    const qa_cvar_view *value = qa_cvars_find(engine, "sv_cheats");
    return value && value->integer != 0;
}
static bool read_script(void *context, const qa_command_context *command, const char *path,
    qa_bytes *out, void **lease, qa_error *error)
{
    struct application_guest_q3_client_console *row = context;
    application_provider *provider = row->engine->provider;
    if (!active(row, command)) return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT script source has retired");
    if (application_startup_console_active(provider, row->cvars))
        return application_startup_console_script_read(provider, row->console, command, path, out, lease, error);
    if (application_startup_source_scripts(provider))
        return application_startup_source_script_read(provider, row->console, command, path, out, lease, error);
    qa_resource *resource = NULL;
    const qa_launch_instance *descriptor = row->engine->client_descriptor ?
        qa_launch_instance_lease_view(row->engine->client_descriptor) : provider->launch;
    if (!descriptor || !qa_vfs_acquire(descriptor->content, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource); *lease = resource; return true;
}
static void release_script(void *context, void *lease)
{
    struct application_guest_q3_client_console *row = context;
    application_provider *provider = row->engine->provider;
    if (application_startup_console_active(provider, row->cvars))
        application_startup_console_script_release(provider, row->cvars, lease);
    else if (application_startup_source_scripts(provider))
        application_startup_source_script_release(provider, row->cvars, lease);
    else qa_resource_release(lease);
}
static void complete(void *context, const qa_command_context *command, const char *path, bool success)
{
    struct application_guest_q3_client_console *row = context;
    application_startup_console_script_complete(row->engine->provider, row->console, command, path, success);
}
static bool allowed(void *context, const qa_command_invocation *command)
{
    struct application_guest_q3_client_console *row = context;
    return application_startup_console_command_allowed(row->engine->provider, row->console, command);
}
static qa_command_result dispatch(void *context, const qa_command_invocation *command, qa_error *error)
{
    struct application_guest_q3_client_console *row = context;
    ++row->calls;
    qa_command_result result = application_command_fallback(row->engine->provider->application, command, error);
    --row->calls; return result;
}
static qa_command_result forward(void *context,const qa_command_invocation *command,qa_error *error)
{
    struct application_guest_q3_client_console *row=context;
    application_provider *provider=row->engine->provider;
    qa_application *app=provider->application;
    if (!available(row) || row->kind!=QA_QVM_CGAME || !app->console_forward)
        return QA_COMMAND_UNHANDLED;
    const q3g_role *role=NULL;
    for (const q3g_role *current=row->engine->roles; current; current=current->next)
        if (current->kind==row->kind && current->seat==row->seat && !current->retired &&
            current->ready && current->host && !current->local_client && !current->native_client) {
            if (role) {
                application_fail(error,QA_ERROR_ARGUMENT,"CLIENT forwarding has ambiguous actual role lifetimes");
                return QA_COMMAND_FAILED;
            }
            role=current;
        }
    if (!role) return QA_COMMAND_UNHANDLED;
    qa_application_q3_client_context source;
    if (!command || command->console!=row->console || row->calls==SIZE_MAX ||
        !qa_console_invocation_current(row->console,command) ||
        !qa_application_q3_remote_context_read(app,provider->owner,row->seat,&source,error) ||
        source.cvars!=row->cvars || source.service_owner!=role->service_owner ||
        source.command_context.cvar_view!=qa_cvars_view_identity(row->cvars)) {
        if (error && error->code==QA_OK)
            application_fail(error,QA_ERROR_ARGUMENT,"CLIENT forwarding lost its actual role and view");
        return QA_COMMAND_FAILED;
    }
    bool engine=!command->context.owner &&
        command->context.cvar_view==qa_cvars_view_identity(app->cvars) &&
        qa_console_invocation_delivered_view(command,source.command_context.cvar_view,
            source.receiver,source.service_owner) &&
        qa_application_command_context_active(app,&command->context);
    if (!active(row,&command->context) && !engine) {
        application_fail(error,QA_ERROR_ARGUMENT,"CLIENT forwarding lacks its entered Source capability");
        return QA_COMMAND_FAILED;
    }
    ++row->calls;
    qa_command_result result=app->console_forward(app->guest_context,command,error);
    --row->calls; return result;
}
bool application_guest_q3_client_console_prepare(struct application_q3_guest *engine,
    qa_qvm_role kind, uint32_t seat, qa_error *error)
{
    if (!engine || kind == QA_QVM_GAME || kind > QA_QVM_UI || seat == UINT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT preparation requires its actual receiver role and seat");
    if (find(engine, kind, seat)) return true;
    struct application_guest_q3_client_console *row = calloc(1, sizeof(*row));
    if (!row) return application_fail(error, QA_ERROR_MEMORY, "Retaining CLIENT console view");
    row->engine = engine; row->kind = kind; row->seat = seat;
    char name[160];
    snprintf(name, sizeof(name), "q3-client-globals:%u:%s:%u:%u", engine->provider->owner, engine->provider->launch->selection.instance, (unsigned)kind, seat);
    if (!qa_strings_intern_cstr(qa_session_strings(engine->provider->application->session), name,
        &row->script_globals_owner, error) || !qa_bot_memory_create(NULL,&row->script_memory,error) ||
        !qa_script_defines_create(&row->script_globals, error) ||
        !qa_script_defines_bind_memory(row->script_globals,qa_bot_memory_script_services(row->script_memory),error)) {
        (void)globals_close(row,NULL);
        free(row); return false;
    }
    qa_cvar_options cvars = {.dialect = QA_RULESET_Q3, .side = QA_CVAR_SIDE_CLIENT,
        .role = kind == QA_QVM_CGAME ? QA_CVAR_ROLE_CGAME : QA_CVAR_ROLE_UI, .seat = seat,
        .user = engine->provider->application,
        .print = cvar_print, .cheats_allowed = cheats};
    row->cvars = qa_cvars_create_view(engine->provider->application->cvars, &cvars, error);
    qa_console_options options = {.context = {.owner = engine->provider->owner, .seat = seat,
        .dialect = QA_RULESET_Q3, .origin = QA_COMMAND_SEAT}, .cvars = row->cvars,
        .user = row, .print = print, .capture_context = capture, .context_active = active, .read_script = read_script,
        .release_script = release_script, .script_complete = complete, .allow_command = allowed,
        .source_command = dispatch, .forward = forward};
    options.context.cvar_view = qa_cvars_view_identity(row->cvars);
    if (row->cvars && qa_console_bind_source(engine->provider->application->console, &options, error))
        row->console = engine->provider->application->console;
    if (!row->console) {
        qa_console_unbind_source(row->console, qa_cvars_view_identity(row->cvars), error); qa_cvars_destroy(row->cvars);
        (void)globals_close(row,NULL); free(row); return false;
    }
    struct application_guest_q3_client_console **tail = &engine->client_preparation;
    while (*tail) tail = &(*tail)->next;
    *tail = row; return true;
}
bool application_guest_q3_client_console_source(struct application_q3_guest *engine, size_t index,
    qa_application_startup_source *out)
{
    struct application_guest_q3_client_console *row = engine ? engine->client_preparation : NULL;
    while (row && index) { row = row->next; --index; }
    if (!row || !out) return false;
    const q3g_role *declaration = engine->constructing_role;
    if (!declaration || declaration->kind != row->kind || declaration->seat != row->seat)
        declaration = NULL;
    for (const q3g_role *role = engine->roles; !declaration && role; role = role->next)
        if (role->kind == row->kind && role->seat == row->seat && !role->retired && role->service_owner)
            declaration = role;
    *out = (qa_application_startup_source){.descriptor = engine->provider->launch,
        .scope = {.provider = engine->provider->owner, .kind = row->kind == QA_QVM_CGAME ?
            QA_APPLICATION_CONSOLE_Q3_CGAME : QA_APPLICATION_CONSOLE_Q3_UI, .seat = row->seat},
        .console = row->console, .cvars = row->cvars,
        .declaration_owner = declaration ? declaration->service_owner : engine->provider->owner,
        .command = {.owner = engine->provider->owner, .seat = row->seat,
            .cvar_view = qa_cvars_view_identity(row->cvars), .dialect = QA_RULESET_Q3,
            .origin = QA_COMMAND_SEAT}};
    return true;
}
bool application_guest_q3_client_consoles_prepare(struct application_q3_guest *engine,
    const qa_launch_choices *choices, qa_error *error)
{
    if (!engine || !choices || !engine->provider->launch || !engine->provider->launch->selection.artifact)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT consoles require their actual selected source recipe");
    for (size_t index = 0; index < choices->seat_count; ++index)
        for (qa_qvm_role kind = QA_QVM_CGAME; kind <= QA_QVM_UI; ++kind)
            if (q3g_selected_client_seat(engine->provider, choices, kind, index) &&
                !application_guest_q3_client_console_prepare(engine, kind, choices->seats[index].id, error)) return false;
    return true;
}
bool application_guest_q3_client_console_at(struct application_q3_guest *engine, qa_qvm_role kind, uint32_t seat,
    qa_console **console, qa_cvars **cvars)
{
    struct application_guest_q3_client_console *row = find(engine, kind, seat);
    if (!row || !available(row)) return false;
    if (console) *console = row->console;
    if (cvars) *cvars = row->cvars;
    return true;
}
bool application_guest_q3_client_console_globals(struct application_q3_guest *engine, qa_qvm_role kind, uint32_t seat,
    qa_script_defines **out, qa_string_id *owner, qa_error *error)
{
    struct application_guest_q3_client_console *row = find(engine, kind, seat);
    if (!row || !available(row) || !out || !owner || !row->script_globals || !row->script_globals_owner ||
        !row->script_memory || !qa_script_defines_memory(row->script_globals) ||
        qa_script_defines_memory(row->script_globals)->context!=row->script_memory)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT parser globals lost their physical console owner");
    *out = row->script_globals; *owner = row->script_globals_owner; return true;
}
bool application_guest_q3_client_console_idle(const struct application_q3_guest *engine)
{
    for (const struct application_guest_q3_client_console *row = engine ? engine->client_preparation : NULL;
        row; row = row->next)
        if (row->calls || !qa_console_idle(row->console) || !qa_bot_memory_idle(row->script_memory) ||
            (available(row) && !qa_cvars_observer_idle(row->cvars))) return false;
    return true;
}
static bool child_released(const struct application_guest_q3_client_console *row)
{
    if (!row || row->calls || row->engine->calls || !qa_console_idle(row->console)) return false;
    for (const q3g_role *role = row->engine->roles; role; role = role->next)
        if (role->kind == row->kind && role->seat == row->seat &&
            (!role->retired || role->initialized || role->host || role->vm || role->native ||
             role->native_client || role->client_source || role->client_engine)) return false;
    return true;
}

bool application_guest_q3_client_console_retire_begin(struct application_q3_guest *engine,
    qa_qvm_role kind, uint32_t seat, application_provider *old_game, qa_error *error)
{
    struct application_guest_q3_client_console *row = find(engine, kind, seat);
    application_provider *provider = engine ? engine->provider : NULL;
    qa_application *app = provider ? provider->application : NULL;
    const qa_launch_snapshot *candidate = app ? qa_application_startup_candidate(app) : NULL;
    if (!candidate && app) candidate = qa_configuration_current(app->configuration);
    const qa_launch_instance *selected = provider && candidate ?
        qa_launch_snapshot_find(candidate, provider->launch->selection.instance) : NULL;
    if (!row || row->retiring || !old_game || old_game->application != app || !app->publication_started ||
        app->operation != APPLICATION_CONFIGURING || !provider->constructed || !provider->attached ||
        provider->close_pending || engine->restore_pending || engine->calls || row->calls ||
        !selected || selected->state != provider || selected->storage != provider->launch->storage ||
        !qa_console_idle(row->console))
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT retirement lost its retained publication child");
    if (row->configuration != CLIENT_CONFIGURATION_LIVE)
        return row->retiring_game == old_game ||
            application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT retirement names another old GAME");
    bool bound = false;
    for (q3g_role *role = engine->roles; role; role = role->next) {
        if (role->kind != row->kind || role->seat != seat) continue;
        if (!role->local_client || role->client_source != old_game || !role->host || role->retired)
            return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT retirement lost its actual old role leases");
        bound = true;
    }
    if (!bound) return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT retirement has no genuine old source binding");
    bool found = false;
    for (size_t i = 0; application_guest_q3_client_console_source(engine, i, &row->retiring_source); ++i)
        if (row->retiring_source.console == row->console && row->retiring_source.cvars == row->cvars) { found = true; break; }
    if (!found) return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT retirement lost its physical tuple");
    row->retiring_game = old_game; row->configuration = CLIENT_CONFIGURATION_SHUTDOWN;
    return true;
}

bool application_guest_q3_client_console_entered(struct application_q3_guest *engine,
    const qa_application_startup_source *source)
{
    struct application_guest_q3_client_console *row = source ? find(engine, source->scope.kind == QA_APPLICATION_CONSOLE_Q3_UI ? QA_QVM_UI : QA_QVM_CGAME, source->scope.seat) : NULL;
    qa_application_startup_source actual;
    if (!row || !available(row) || !source->descriptor || !engine->provider->launch) return false;
    bool found = false;
    for (size_t i = 0; application_guest_q3_client_console_source(engine, i, &actual); ++i)
        if (actual.console == row->console && actual.cvars == row->cvars) { found = true; break; }
    return found && source->descriptor->storage == actual.descriptor->storage &&
        source->descriptor->content == actual.descriptor->content &&
        source->descriptor->roles == actual.descriptor->roles &&
        (source->descriptor->identity == actual.descriptor->identity) &&
        source->scope.provider == actual.scope.provider && source->scope.kind == actual.scope.kind &&
        source->scope.seat == actual.scope.seat && source->console == actual.console && source->cvars == actual.cvars &&
        source->declaration_owner == actual.declaration_owner && qa_cvars_same_store(qa_console_cvars(row->console), row->cvars) &&
        qa_command_context_equal(&source->command, &actual.command, 0);
}

bool application_guest_q3_client_console_retirement(application_provider *provider,
    const qa_application_startup_source *source)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    struct application_guest_q3_client_console *row = source ? find(engine, source->scope.kind == QA_APPLICATION_CONSOLE_Q3_UI ? QA_QVM_UI : QA_QVM_CGAME, source->scope.seat) : NULL;
    if (!row || !source->descriptor || !row->retiring_source.descriptor || row->retiring ||
        row->configuration < CLIENT_CONFIGURATION_RETIRING || !row->retiring_game ||
        !provider->application->publication_started || provider->application->operation != APPLICATION_CONFIGURING ||
        !provider->constructed || !provider->attached || provider->close_pending || engine->restore_pending ||
        !child_released(row)) return false;
    const qa_application_startup_source *held = &row->retiring_source;
    bool receiver_live = false, game_live = false;
    for (size_t i = 0; i < provider->application->provider_count; ++i) {
        receiver_live |= provider->application->providers[i] == provider;
        game_live |= provider->application->providers[i] == row->retiring_game;
    }
    if (!receiver_live || !game_live || !row->retiring_game->constructed ||
        row->retiring_game->application != provider->application) return false;
    return source->descriptor->storage == held->descriptor->storage &&
        source->descriptor->content == held->descriptor->content &&
        source->descriptor->roles == held->descriptor->roles &&
        (source->descriptor->identity == held->descriptor->identity) && source->console == held->console &&
        source->cvars == held->cvars && source->scope.provider == held->scope.provider &&
        source->scope.kind == held->scope.kind && source->scope.seat == held->scope.seat &&
        source->declaration_owner == held->declaration_owner && qa_command_context_equal(&source->command, &held->command, 0);
}

bool application_guest_q3_client_console_retire_finish(struct application_q3_guest *engine,
    qa_qvm_role kind, uint32_t seat, qa_error *error)
{
    struct application_guest_q3_client_console *row = find(engine, kind, seat);
    if (!row || row->configuration == CLIENT_CONFIGURATION_LIVE || !child_released(row))
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT configuration still owns its old role leases");
    if (row->configuration == CLIENT_CONFIGURATION_RETIRED)
        return application_guest_q3_client_console_retirement(engine->provider, &row->retiring_source) ||
            application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT retirement lost its retained old GAME parent");
    if (row->configuration == CLIENT_CONFIGURATION_SHUTDOWN) {
        if (!qa_cvars_observer_idle(row->cvars))
            return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT old registry still has entered observers");
        row->configuration = CLIENT_CONFIGURATION_RETIRING;
    }
    if (!application_startup_tuple_retire_client(engine->provider, &row->retiring_source, error)) return false;
    row->configuration = CLIENT_CONFIGURATION_RETIRED; return true;
}

bool application_guest_q3_client_console_bound(application_provider *provider,
    const qa_application_startup_source *source)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    struct application_guest_q3_client_console *row = source ? find(engine, source->scope.kind == QA_APPLICATION_CONSOLE_Q3_UI ? QA_QVM_UI : QA_QVM_CGAME, source->scope.seat) : NULL;
    return row && source->descriptor && row->configuration == CLIENT_CONFIGURATION_RETIRED &&
        row->binding_cvars && row->cvars == row->binding_cvars && source->cvars == row->binding_cvars &&
        source->console == row->console && source->descriptor->storage == provider->launch->storage &&
        source->scope.provider == provider->owner && source->scope.kind == row->retiring_source.scope.kind &&
        source->scope.seat == row->seat && source->declaration_owner == row->retiring_source.declaration_owner &&
        application_guest_q3_client_console_retirement(provider, &row->retiring_source);
}

bool application_guest_q3_client_console_rebind(struct application_q3_guest *engine,
    qa_qvm_role kind, uint32_t seat, application_provider *next_game, qa_error *error)
{
    struct application_guest_q3_client_console *row = find(engine, kind, seat);
    if (!row || row->configuration != CLIENT_CONFIGURATION_RETIRED || !next_game ||
        next_game->application != engine->provider->application || next_game == row->retiring_game ||
        !child_released(row))
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT rebinding requires its consumed old source");
    qa_application *app = engine->provider->application;
    const qa_launch_snapshot *candidate = qa_configuration_current(app->configuration);
    const qa_launch_instance *receiver = candidate ? qa_launch_snapshot_find(candidate,
        engine->provider->launch->selection.instance) : NULL;
    const qa_launch_instance *game = candidate ? qa_launch_snapshot_find(candidate,
        next_game->launch->selection.instance) : NULL;
    const qa_launch_binding *entities = candidate ? qa_launch_binding_for(qa_launch_snapshot_choices(candidate),
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "") : NULL;
    if (!app->publication_started || app->operation != APPLICATION_CONFIGURING ||
        !receiver || receiver->state != engine->provider || receiver->storage != engine->provider->launch->storage ||
        !game || game->state != next_game || game->storage != next_game->launch->storage ||
        !next_game->constructed || next_game->close_pending || !entities ||
        strcmp(entities->instance, game->selection.instance))
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT rebinding lost its genuine new GAME publication");
    qa_application_startup_source backing = {0}; bool found = false;
    for (size_t i = 0;; ++i) {
        if (!application_provider_startup_source_at(next_game, i, &backing, &found, error)) return false;
        if (!found) return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT new GAME has no physical configuration source");
        if (backing.scope.kind == QA_APPLICATION_CONSOLE_Q3_GAME) break;
    }
    qa_application_startup_source target = row->retiring_source;
    target.descriptor = receiver;
    qa_cvars *actual = NULL;
    if (!application_startup_tuple_bind_client(engine->provider, candidate, &target, &backing, &actual, error)) return false;
    if (actual != row->cvars)
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT rebinding changed its retained Source view");
    row->binding_cvars = actual;
    application_startup_tuple_bound_client(engine->provider, &target);
    row->binding_cvars = NULL; row->retiring_game = NULL;
    row->retiring_source = (qa_application_startup_source){0}; row->configuration = CLIENT_CONFIGURATION_LIVE;
    return true;
}

bool application_guest_q3_client_consoles_retarget(struct application_q3_guest *engine,
    application_provider *old_game, application_provider *next_game,
    const qa_launch_choices *choices, qa_error *error)
{
    if (!engine || engine->game || !old_game || !next_game || old_game == next_game) return true;
    qa_application *app = engine->provider->application;
    const qa_launch_snapshot *candidate = qa_application_startup_candidate(app);
    if (!candidate) candidate = qa_configuration_current(app->configuration);
    const qa_launch_instance *selected = candidate ? qa_launch_snapshot_find(candidate,
        engine->provider->launch->selection.instance) : NULL;
    /* Removed receivers retire through their ordinary detached provider path. */
    if (!selected || selected->state != engine->provider) return true;
    if (!choices || choices != qa_launch_snapshot_choices(candidate) || !app->publication_started ||
        app->operation != APPLICATION_CONFIGURING || next_game->application != app)
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT retargeting lost its real publication choices");
    struct application_guest_q3_client_console **position = &engine->client_preparation;
    while (*position) {
        struct application_guest_q3_client_console *row = *position;
        bool bound = row->configuration != CLIENT_CONFIGURATION_LIVE && row->retiring_game == old_game;
        for (q3g_role *role = engine->roles; !bound && role; role = role->next)
            bound = role->kind == row->kind && role->seat == row->seat &&
                role->local_client && role->client_source == old_game;
        if (!bound) { position = &row->next; continue; }
        bool admitted = false;
        for (size_t i = 0; i < choices->seat_count; ++i)
            if (choices->seats[i].id == row->seat &&
                q3g_selected_client_seat(engine->provider, choices, row->kind, i)) admitted = true;
        if (!admitted && !qa_console_idle(row->console))
            return application_fail(error, QA_ERROR_ARGUMENT, "Unselected hosted CLIENT console still has a retained program");
        if (!application_guest_q3_client_console_retire_begin(engine, row->kind, row->seat, old_game, error)) return false;
        for (q3g_role *role = engine->roles; role; role = role->next) {
            if (role->kind != row->kind || role->seat != row->seat) continue;
            if (!q3g_role_shutdown(role, false, error) || !q3g_role_consume(role, error)) return false;
        }
        if (!application_guest_q3_client_console_retire_finish(engine, row->kind, row->seat, error)) return false;
        if (admitted) {
            if (!application_guest_q3_client_console_rebind(engine, row->kind, row->seat, next_game, error)) return false;
            position = &row->next; continue;
        }
        q3g_role **role_position = &engine->roles;
        while (*role_position) {
            q3g_role *role = *role_position;
            if (role->kind != row->kind || role->seat != row->seat) { role_position = &role->next; continue; }
            q3g_role *next = role->next;
            if (!q3g_role_destroy(role, error)) return false;
            *role_position = next;
        }
        if(!globals_close(row,error)) return false;
        if (!qa_console_unbind_source(row->console, qa_cvars_view_identity(row->cvars), error)) return false;
        *position = row->next;
        qa_cvars_detach_callbacks(row->cvars); qa_cvars_destroy(row->cvars);
        free(row);
    }
    return true;
}

bool application_guest_q3_client_console_destroy(struct application_q3_guest *engine, qa_error *error)
{
    if (!application_guest_q3_client_console_idle(engine))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT console is borrowed");
    while (engine && engine->client_preparation) {
        struct application_guest_q3_client_console *row = engine->client_preparation;
        for (const q3g_role *role = engine->roles; role; role = role->next)
            if (role->kind == row->kind && role->seat == row->seat &&
                (role->host || role->vm || role->native))
                return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT globals still have a retained module host");
        if (!qa_console_idle(row->console))
            return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT console still has retained command owners");
        qa_application_startup_source source;
        if (!application_guest_q3_client_console_source(engine, 0, &source))
            return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT retirement lost its held physical tuple");
        /* Retire the exact Source tuple before releasing its retained view. */
        row->retiring = true;
        if (!application_startup_tuple_retire(engine->provider, &source, error)) return false;
        if(!globals_close(row,error)) return false;
        if (!qa_console_unbind_source(row->console, qa_cvars_view_identity(row->cvars), error)) return false;
        engine->client_preparation = row->next;
        qa_cvars_detach_callbacks(row->cvars); qa_cvars_destroy(row->cvars);
        free(row);
    }
    return true;
}
