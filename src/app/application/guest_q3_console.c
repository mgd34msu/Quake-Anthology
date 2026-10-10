#include "guest_q3_console.h"
#include "qa/console_cvar_observer.h"
#include "guest_q3_private.h"
#include "q3_product.h"
#include "startup_flow.h"
#include "guest_q3_client_console.h"
#include "engine_shutdown.h"
#include "native_q3_settings.h"

#include <ctype.h>

struct application_guest_q3_console {
    struct application_q3_guest *engine;
    qa_cvars *cvars;
    qa_cvar_handle controls[APPLICATION_Q3_CVAR_CONTROL_COUNT];
    qa_console *console;
    size_t calls;
};

static bool named(const char *left, const char *right)
{
    if (!left) return false;
    while (*left && *right)
        if (tolower((unsigned char)*left++) != (unsigned char)*right++) return false;
    return *left == *right;
}

qa_cvars *application_guest_q3_console_registry(const application_provider *provider)
{
    struct application_q3_guest *engine = q3g_engine((application_provider *)provider);
    return engine && engine->console ? engine->console->cvars : NULL;
}

qa_cvar_handle application_guest_q3_console_control(const application_provider *provider,
    application_q3_cvar_control control)
{
    struct application_q3_guest *engine = q3g_engine((application_provider *)provider);
    return engine && engine->console ? engine->console->controls[control] : (qa_cvar_handle){0};
}

qa_console *application_guest_q3_console_owner(const application_provider *provider)
{
    struct application_q3_guest *engine = q3g_engine((application_provider *)provider);
    return engine && engine->console ? engine->console->console : NULL;
}

qa_cvars *application_guest_q3_cvar_owner(const application_provider *provider, const char *name)
{
    return provider && named(name, "sv_cheats") ? application_engine_shutdown_cvars(provider) :
        application_guest_q3_console_registry(provider);
}

bool application_guest_q3_console_startup(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    qa_cvars *cvars = application_guest_q3_console_registry(provider);
    if (!provider || !engine || !cvars)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original GAME startup lost its physical registry");
    qa_command_context command = {.owner = provider->owner, .cvar_view = qa_cvars_view_identity(cvars), .dialect = QA_RULESET_Q3,
        .origin = QA_COMMAND_SERVER};
    if (!provider->attached && !application_startup_source_preinit(provider,
        application_guest_q3_console_owner(provider), cvars, &command, error)) return false;
    if (!cvars || !application_startup_apply_latched(provider, cvars, error)) return false;
    qa_cvar_view capacity, dedicated;
    if (!engine || engine->restore_pending || engine->game ||
        !qa_cvars_effective_view(cvars, "sv_maxclients", &capacity, error) ||
        !qa_cvars_effective_view(cvars, "dedicated", &dedicated, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original GAME startup lacks its fresh physical registry");
    size_t seats = 0;
    for (size_t i = 0; i < 64; ++i) seats += engine->seats[i] != UINT32_MAX;
    float minimum = dedicated.number != 0 ? 1.0f : (float)(seats ? seats : 1);
    float requested = truncf(capacity.number);
    if (requested < minimum) requested = minimum;
    if (requested > 64) requested = 64;
    if (!isfinite(requested) || requested < 1 || requested > 64)
        return application_fail(error, QA_ERROR_FORMAT, "Original GAME capacity must be between 1 and 64");
    return application_publication_source_capacity(provider, (uint32_t)requested, error);
}

static bool cheats_allowed(void *context)
{
    struct application_guest_q3_console *owner = context;
    return application_native_cheats_enabled(owner->engine->provider);
}

static bool capture(void *context, const qa_command_context *source,
    qa_command_context *out, qa_error *error)
{
    struct application_guest_q3_console *owner = context;
    application_provider *provider = owner->engine->provider;
    qa_command_context command = *source;
    if (command.owner && command.owner != provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q3 console received another GAME owner");
    command.owner = provider->owner;
    command.dialect = QA_RULESET_Q3;
    return application_command_capture(provider->application, &command, out, error);
}

static bool active(void *context, const qa_command_context *command)
{
    struct application_guest_q3_console *owner = context;
    return command && command->owner == owner->engine->provider->owner &&
        command->dialect == QA_RULESET_Q3 &&
        application_command_active(owner->engine->provider->application, command);
}

static void print(void *context, const qa_command_context *command, const char *text)
{
    struct application_guest_q3_console *owner = context;
    ++owner->calls;
    application_console_print(owner->engine->provider->application, command, text);
    --owner->calls;
}

static void cvar_print(void *context, const char *text)
{
    struct application_guest_q3_console *owner = context;
    qa_console_emit(owner->console, NULL, text);
}

static bool read_script(void *context, const qa_command_context *command,
    const char *path, qa_bytes *out, void **lease, qa_error *error)
{
    struct application_guest_q3_console *owner = context;
    if (!active(owner, command))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q3 script publication has retired");
    if (application_startup_source_active(owner->engine->provider))
        return application_startup_script_read(owner->engine->provider, command,
            path, out, lease, error);
    if (application_startup_source_scripts(owner->engine->provider))
        return application_startup_source_script_read(owner->engine->provider, owner->console,
            command, path, out, lease, error);
    qa_vfs *files = qa_application_context_files(owner->engine->provider->application, command, NULL);
    qa_resource *resource = NULL;
    if (!files || !qa_vfs_acquire(files, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource);
    *lease = resource;
    return true;
}

static void release_script(void *context, void *lease)
{
    struct application_guest_q3_console *owner = context;
    if (application_startup_source_active(owner->engine->provider)) {
        application_startup_script_release(owner->engine->provider, lease);
        return;
    }
    if (application_startup_source_scripts(owner->engine->provider)) {
        application_startup_source_script_release(owner->engine->provider, owner->cvars, lease);
        return;
    }
    qa_resource_release(lease);
}

static void script_complete(void *context, const qa_command_context *command,
    const char *path, bool success)
{
    struct application_guest_q3_console *owner = context;
    application_startup_script_complete(owner->engine->provider, command, path, success);
}

static bool allow_command(void *context, const qa_command_invocation *command)
{
    struct application_guest_q3_console *owner = context;
    return application_startup_command_allowed(owner->engine->provider, command);
}

static qa_command_result command(void *context, const qa_command_invocation *invocation,
    qa_error *error)
{
    struct application_guest_q3_console *owner = context;
    ++owner->calls;
    application_provider *provider = owner->engine->provider;
    qa_command_result result = application_startup_common_command(provider, owner->console,
        owner->cvars, invocation, error);
    if (result == QA_COMMAND_UNHANDLED)
        result = application_command_fallback(provider->application, invocation, error);
    --owner->calls;
    return result;
}

static bool register_engine(struct application_guest_q3_console *owner,
    const char *map_path, qa_error *error)
{
    static const struct { const char *name, *value; uint32_t flags; qa_cvar_save_policy save_policy; } definitions[] = {
        {"vm_game", "2", QA_CVAR_ARCHIVE, QA_CVAR_SAVE_SETTING},
        {"protocol", "68", QA_CVAR_SERVERINFO | QA_CVAR_READONLY, QA_CVAR_SAVE_SETTING},
        {"sv_pure", "1", QA_CVAR_SYSTEMINFO, QA_CVAR_SAVE_SETTING},
        {"sv_allowDownload", "0", QA_CVAR_SERVERINFO, QA_CVAR_SAVE_SETTING},
        {"sv_maxRate", "0", QA_CVAR_SERVERINFO, QA_CVAR_SAVE_SETTING}, {"sv_fps", "20", 0, QA_CVAR_SAVE_GAMEPLAY},
        {"sv_serverid", "0", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY, QA_CVAR_SAVE_SETTING},
        {"sv_paks", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY, QA_CVAR_SAVE_SETTING},
        {"sv_pakNames", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY, QA_CVAR_SAVE_SETTING},
        {"sv_referencedPaks", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY, QA_CVAR_SAVE_SETTING},
        {"sv_referencedPakNames", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY, QA_CVAR_SAVE_SETTING},
        {"sv_maxclients", "8", QA_CVAR_SERVERINFO | QA_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
        {"sv_privateClients", "0", QA_CVAR_SERVERINFO, QA_CVAR_SAVE_SETTING},
        {"sv_privatePassword", "", QA_CVAR_TEMPORARY, QA_CVAR_SAVE_SETTING}, {"sv_reconnectlimit", "3", 0, QA_CVAR_SAVE_SETTING},
        {"sv_minPing", "0", QA_CVAR_ARCHIVE | QA_CVAR_SERVERINFO, QA_CVAR_SAVE_SETTING},
        {"sv_maxPing", "0", QA_CVAR_ARCHIVE | QA_CVAR_SERVERINFO, QA_CVAR_SAVE_SETTING},
        {"sv_floodProtect", "1", QA_CVAR_ARCHIVE | QA_CVAR_SERVERINFO, QA_CVAR_SAVE_SETTING},
        {"sv_strictAuth", "1", QA_CVAR_ARCHIVE, QA_CVAR_SAVE_SETTING}, {"bot_enable", "1", 0, QA_CVAR_SAVE_SETTING},
        {"cm_noAreas", "0", QA_CVAR_CHEAT, QA_CVAR_SAVE_GAMEPLAY}, {"cm_noCurves", "0", QA_CVAR_CHEAT, QA_CVAR_SAVE_GAMEPLAY},
        {"cm_playerCurveClip", "1", QA_CVAR_ARCHIVE | QA_CVAR_CHEAT, QA_CVAR_SAVE_GAMEPLAY}, {"dedicated", "0", 0, QA_CVAR_SAVE_SETTING}
    };
    application_provider *provider = owner->engine->provider;
    if (!application_startup_seed_source(provider, owner->cvars, error) ||
        !application_q3_product_register_source(application_q3_product_source_policy(provider->application),
            owner->cvars, provider->owner, error)) return false;
    for (size_t i = 0; i < sizeof(definitions) / sizeof(*definitions); ++i)
        if (!qa_cvars_register(owner->cvars, definitions[i].name, definitions[i].value,
            definitions[i].flags, provider->owner, NULL, error) ||
            !qa_cvars_declare_save_policy(owner->cvars, definitions[i].name,
                definitions[i].save_policy, error)) return false;
    const char *name = !strncmp(map_path, "maps/", 5) ? map_path + 5 : map_path;
    size_t length = strlen(name);
    if (length > 4 && !strcmp(name + length - 4, ".bsp")) length -= 4;
    if (!length) return application_fail(error, QA_ERROR_FORMAT, "Original GAME map has no source identity");
    char *map = malloc(length + 1);
    if (!map) return application_fail(error, QA_ERROR_MEMORY, "Retaining original GAME map identity");
    memcpy(map, name, length); map[length] = 0;
    bool okay = qa_cvars_register(owner->cvars, "mapname", map,
        QA_CVAR_SERVERINFO | QA_CVAR_READONLY, provider->owner, NULL, error) &&
        qa_cvars_register(owner->cvars, "sv_mapname", "",
            QA_CVAR_SERVERINFO | QA_CVAR_READONLY, provider->owner, NULL, error);
    if (okay) okay = qa_cvars_declare_save_policy(owner->cvars, "mapname", QA_CVAR_SAVE_SETTING, error) &&
        qa_cvars_declare_save_policy(owner->cvars, "sv_mapname", QA_CVAR_SAVE_SETTING, error);
    free(map);
    return okay;
}

bool application_guest_q3_console_create(struct application_q3_guest *engine,
    const char *map_path, bool restoring, qa_error *error)
{
    if (!engine || engine->console || !engine->provider || !engine->provider->application ||
        !engine->provider->owner || !map_path || restoring != engine->restore_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original GAME console requires its real engine owner");
    struct application_guest_q3_console *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Allocating original GAME console");
    owner->engine = engine;
    qa_cvar_options cvars = {.dialect = QA_RULESET_Q3,
        .side = QA_CVAR_SIDE_SERVER, .role = QA_CVAR_ROLE_GAME, .user = owner,
        .print = cvar_print, .cheats_allowed = cheats_allowed,
        .declaration_save_policy = application_native_q3_cvar_save_policy};
    owner->cvars = qa_cvars_create_view(engine->provider->application->cvars, &cvars, error);
    qa_console_options options = {.context = {.owner = engine->provider->owner,
        .dialect = QA_RULESET_Q3, .origin = QA_COMMAND_SERVER}, .cvars = owner->cvars,
        .user = owner, .print = print, .capture_context = capture, .context_active = active, .read_script = read_script,
        .release_script = release_script, .script_complete = script_complete,
        .allow_command = allow_command, .source_command = command};
    options.context.cvar_view = qa_cvars_view_identity(owner->cvars);
    if (owner->cvars && qa_console_bind_source(engine->provider->application->console, &options, error))
        owner->console = engine->provider->application->console;
    if (!owner->console || !register_engine(owner, map_path, error)) {
        qa_console_unbind_source(owner->console, qa_cvars_view_identity(owner->cvars), error); qa_cvars_detach_callbacks(owner->cvars); qa_cvars_destroy(owner->cvars); free(owner);
        return false;
    }
    application_q3_cvar_controls_bind(owner->cvars, owner->controls);
    engine->console = owner;
    return true;
}

bool application_guest_q3_console_idle(const struct application_q3_guest *engine)
{
    return application_guest_q3_client_console_idle(engine) && (!engine || !engine->console ||
        (!engine->console->calls && qa_console_idle(engine->console->console) &&
            qa_cvars_observer_idle(engine->console->cvars)));
}

bool application_guest_q3_console_destroy(struct application_q3_guest *engine, qa_error *error)
{
    if (!application_guest_q3_console_idle(engine))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original GAME console is borrowed");
    if (engine && engine->console) {
        if (!application_startup_source_retire(engine->provider, engine->console->console,
            engine->console->cvars, error)) return false;
        if (!qa_console_unbind_source(engine->console->console,
            qa_cvars_view_identity(engine->console->cvars), error)) return false;
        qa_cvars_remove_owner(engine->console->cvars, engine->provider->owner);
        qa_cvars_detach_callbacks(engine->console->cvars); qa_cvars_destroy(engine->console->cvars);
        free(engine->console); engine->console = NULL;
    }
    return true;
}
