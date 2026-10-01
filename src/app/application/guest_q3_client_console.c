#include "guest_q3_client_console.h"
#include "startup_flow.h"
#include "q3_product.h"
#include "qa/application_q3_factory.h"
#include "engine_shutdown.h"

struct application_guest_q3_client_console {
    struct application_guest_q3_client_console *next;
    struct application_q3_guest *engine;
    qa_qvm_role kind;
    uint32_t seat;
    qa_console *console;
    qa_cvars *cvars;
    size_t calls;
    bool owns_cvars, retiring;
};

static struct application_guest_q3_client_console *find(struct application_q3_guest *engine,
    uint32_t seat)
{
    for (struct application_guest_q3_client_console *row = engine ? engine->client_preparation : NULL;
        row; row = row->next) if (row->seat == seat) return row;
    return NULL;
}
static qa_cvars *cvar_owner(void *context, const qa_command_context *command, const char *name)
{
    struct application_guest_q3_client_console *row = context;
    if (row->retiring) return NULL;
    qa_cvars *routed = application_startup_cvar_owner(row->engine->provider, row->console, command, name);
    return routed ? routed : row->cvars;
}
static qa_cvars *visible_cvars(void *context, const qa_command_context *command, size_t index)
{
    struct application_guest_q3_client_console *row = context;
    if (row->retiring) return NULL;
    qa_cvars *routed = NULL;
    if (application_startup_visible_cvars(row->engine->provider, row->console, command, index, &routed))
        return routed;
    return index == 0 ? row->cvars : index == 1 ?
        application_engine_shutdown_cvars(row->engine->provider) : NULL;
}
static bool capture(void *context, const qa_command_context *source, qa_command_context *out, qa_error *error)
{
    struct application_guest_q3_client_console *row = context;
    application_provider *provider = row->engine->provider;
    if (row->retiring || !source || (source->owner && source->owner != provider->owner) || source->seat != row->seat)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT console lost its actual receiver and authored seat");
    qa_command_context command = *source;
    command.owner = provider->owner; command.dialect = QA_CONSOLE_Q3;
    return application_command_capture(provider->application, &command, out, error);
}
static bool active(void *context, const qa_command_context *command)
{
    struct application_guest_q3_client_console *row = context;
    return !row->retiring && command && command->owner == row->engine->provider->owner && command->seat == row->seat &&
        command->dialect == QA_CONSOLE_Q3 && application_command_active(row->engine->provider->application, command);
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
    if (application_startup_console_active(provider, row->console))
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
    if (application_startup_console_active(provider, row->console))
        application_startup_console_script_release(provider, row->console, lease);
    else if (application_startup_source_scripts(provider))
        application_startup_source_script_release(provider, row->console, lease);
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
bool application_guest_q3_client_console_prepare(struct application_q3_guest *engine,
    qa_qvm_role kind, uint32_t seat, qa_error *error)
{
    if (!engine || kind == QA_QVM_GAME || kind > QA_QVM_UI || seat == UINT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT preparation requires its actual receiver role and seat");
    if (find(engine, seat)) return true;
    struct application_guest_q3_client_console *row = calloc(1, sizeof(*row));
    if (!row) return application_fail(error, QA_ERROR_MEMORY, "Retaining private CLIENT console");
    row->engine = engine; row->kind = kind; row->seat = seat; row->owns_cvars = true;
    qa_cvar_options cvars = {.dialect = QA_CONSOLE_Q3, .user = engine->provider->application,
        .print = cvar_print, .cheats_allowed = cheats};
    row->cvars = qa_cvars_create(&cvars, error);
    qa_console_options options = {.context = {.owner = engine->provider->owner, .seat = seat,
        .dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_SEAT}, .cvars = row->cvars,
        .user = row, .print = print, .cvar_owner = cvar_owner, .visible_cvars = visible_cvars,
        .capture_context = capture, .context_active = active, .read_script = read_script,
        .release_script = release_script, .script_complete = complete, .allow_command = allowed,
        .source_command = dispatch};
    if (row->cvars) row->console = qa_console_create(&options, error);
    if (!row->console || (!engine->restore_pending && !application_startup_seed_source(engine->provider, row->cvars, error))) {
        qa_console_destroy(row->console); qa_cvars_destroy(row->cvars); free(row); return false;
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
    *out = (qa_application_startup_source){.descriptor = engine->provider->launch,
        .scope = {.provider = engine->provider->owner, .kind = row->kind == QA_QVM_CGAME ?
            QA_APPLICATION_CONSOLE_Q3_CGAME : QA_APPLICATION_CONSOLE_Q3_UI, .seat = row->seat},
        .console = row->console, .cvars = row->cvars, .declaration_owner = engine->provider->owner,
        .command = {.owner = engine->provider->owner, .seat = row->seat, .dialect = QA_CONSOLE_Q3,
            .origin = QA_COMMAND_SEAT}};
    return true;
}
bool application_guest_q3_client_consoles_prepare(struct application_q3_guest *engine,
    const qa_launch_choices *choices, qa_error *error)
{
    if (!engine || !choices || !engine->provider->launch || !engine->provider->launch->selection.artifact)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT consoles require their actual selected source recipe");
    qa_qvm_role primary = q3g_primary_role(engine->provider->launch->selection.artifact);
    for (size_t index = 0; index < choices->seat_count; ++index) {
        qa_qvm_role kind = primary;
        if (kind == QA_QVM_GAME) kind = q3g_selected_client_seat(engine->provider, choices,
            QA_QVM_CGAME, index) ? QA_QVM_CGAME : QA_QVM_UI;
        if (q3g_selected_client_seat(engine->provider, choices, kind, index) &&
            !application_guest_q3_client_console_prepare(engine, kind, choices->seats[index].id, error)) return false;
    }
    return true;
}
bool application_guest_q3_client_console_at(struct application_q3_guest *engine, uint32_t seat,
    qa_console **console, qa_cvars **cvars)
{
    struct application_guest_q3_client_console *row = find(engine, seat);
    if (!row || row->retiring) return false;
    if (console) *console = row->console;
    if (cvars) *cvars = row->cvars;
    return true;
}
bool application_guest_q3_client_console_bind(struct application_q3_guest *engine, uint32_t seat,
    qa_cvars *cvars, qa_error *error)
{
    struct application_guest_q3_client_console *row = find(engine, seat);
    if (!row || row->retiring || !cvars)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT registry binding lost its physical console");
    if (row->cvars == cvars) return true;
    if (row->calls || !qa_console_idle(row->console) || !qa_cvars_observer_idle(row->cvars))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT registry is borrowed");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->seat == seat && role->kind != QA_QVM_GAME && role->host)
            return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT binding would replace a retained live role registry");
    if (!qa_console_set_profile(row->console, QA_CONSOLE_Q3, cvars, error)) return false;
    if (row->owns_cvars) qa_cvars_destroy(row->cvars);
    row->cvars = cvars; row->owns_cvars = false; return true;
}
bool application_guest_q3_client_console_idle(const struct application_q3_guest *engine)
{
    for (const struct application_guest_q3_client_console *row = engine ? engine->client_preparation : NULL;
        row; row = row->next)
        if (row->calls || !qa_console_idle(row->console) ||
            (!row->retiring && !qa_cvars_observer_idle(row->cvars))) return false;
    return true;
}
bool application_guest_q3_client_console_take(struct application_q3_guest *engine, uint32_t seat,
    qa_cvars **out, qa_error *error)
{
    struct application_guest_q3_client_console *row = find(engine, seat);
    if (!row || row->retiring || !out || *out || !row->owns_cvars || row->calls || !qa_console_idle(row->console) ||
        !qa_cvars_observer_idle(row->cvars))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT heap transfer requires its unborrowed physical owner");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->seat == seat && role->kind != QA_QVM_GAME && role->host)
            return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT heap transfer precedes its actual host construction");
    *out = row->cvars; row->owns_cvars = false; return true;
}
bool application_guest_q3_client_console_destroy(struct application_q3_guest *engine, qa_error *error)
{
    if (!application_guest_q3_client_console_idle(engine))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT console is borrowed");
    while (engine && engine->client_preparation) {
        struct application_guest_q3_client_console *row = engine->client_preparation;
        qa_application_startup_source source;
        if (!application_guest_q3_client_console_source(engine, 0, &source))
            return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT retirement lost its held physical tuple");
        /* The configuration owner can consume its borrowed heap before a
         * later lifetime callback fails. Retire the console authority first
         * so cleanup retries never inspect that consumed registry. */
        row->retiring = true;
        if (!application_startup_tuple_retire(engine->provider, &source, error)) return false;
        engine->client_preparation = row->next;
        qa_console_destroy(row->console);
        if (row->owns_cvars) qa_cvars_destroy(row->cvars);
        free(row);
    }
    return true;
}
