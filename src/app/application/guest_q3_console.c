#include "guest_q3_console.h"
#include "guest_q3_private.h"
#include "q3_product.h"
#include "startup_flow.h"
#include "guest_q3_client_console.h"
#include "engine_shutdown.h"

#include <ctype.h>

struct application_guest_q3_console {
    struct application_q3_guest *engine;
    qa_cvars *cvars;
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
    qa_command_context command = {.owner = provider->owner, .dialect = QA_CONSOLE_Q3,
        .origin = QA_COMMAND_SERVER};
    if (!provider->attached && !application_startup_source_preinit(provider,
        application_guest_q3_console_owner(provider), cvars, &command, error)) return false;
    if (!cvars || !qa_cvars_apply_latched(cvars, NULL, error)) return false;
    const qa_cvar_view *capacity = qa_cvars_find(cvars, "sv_maxclients");
    const qa_cvar_view *dedicated = qa_cvars_find(cvars, "dedicated");
    if (!engine || engine->restore_pending || engine->game || !capacity || !dedicated)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original GAME startup lacks its fresh physical registry");
    size_t seats = 0;
    for (size_t i = 0; i < 64; ++i) seats += engine->seats[i] != UINT32_MAX;
    float minimum = dedicated->number != 0 ? 1.0f : (float)(seats ? seats : 1);
    float requested = truncf(capacity->number);
    if (requested < minimum) requested = minimum;
    if (requested > 64) requested = 64;
    if (!isfinite(requested) || requested < 1 || requested > 64)
        return application_fail(error, QA_ERROR_FORMAT, "Original GAME capacity must be between 1 and 64");
    char value[16];
    snprintf(value, sizeof(value), "%u", (uint32_t)requested);
    return qa_cvars_set(cvars, "sv_maxclients", value, true, error);
}

static qa_cvars *cvar_owner(void *context, const qa_command_context *command, const char *name)
{
    struct application_guest_q3_console *owner = context;
    qa_cvars *routed = application_startup_cvar_owner(owner->engine->provider,
        owner->console, command, name);
    if (routed) return routed;
    return application_guest_q3_cvar_owner(owner->engine->provider, name);
}

static qa_cvars *visible_cvars(void *context, const qa_command_context *command, size_t index)
{
    struct application_guest_q3_console *owner = context;
    qa_cvars *routed = NULL;
    if (application_startup_visible_cvars(owner->engine->provider, owner->console,
        command, index, &routed)) return routed;
    return index == 0 ? owner->cvars : index == 1 ?
        application_engine_shutdown_cvars(owner->engine->provider) : NULL;
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
    command.dialect = QA_CONSOLE_Q3;
    return application_command_capture(provider->application, &command, out, error);
}

static bool active(void *context, const qa_command_context *command)
{
    struct application_guest_q3_console *owner = context;
    return command && command->owner == owner->engine->provider->owner &&
        command->dialect == QA_CONSOLE_Q3 &&
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
        application_startup_source_script_release(owner->engine->provider, owner->console, lease);
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
    qa_command_result result = application_command_fallback(owner->engine->provider->application,
        invocation, error);
    --owner->calls;
    return result;
}

static bool register_engine(struct application_guest_q3_console *owner,
    const char *map_path, qa_error *error)
{
    static const struct { const char *name, *value; uint32_t flags; } definitions[] = {
        {"vm_game", "2", QA_CVAR_ARCHIVE},
        {"protocol", "68", QA_CVAR_SERVERINFO | QA_CVAR_READONLY},
        {"sv_pure", "1", QA_CVAR_SYSTEMINFO},
        {"sv_allowDownload", "0", QA_CVAR_SERVERINFO},
        {"sv_maxRate", "0", QA_CVAR_SERVERINFO}, {"sv_fps", "20", 0},
        {"sv_serverid", "0", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
        {"sv_paks", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
        {"sv_pakNames", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
        {"sv_referencedPaks", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
        {"sv_referencedPakNames", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
        {"sv_maxclients", "8", QA_CVAR_SERVERINFO | QA_CVAR_LATCH},
        {"sv_privateClients", "0", QA_CVAR_SERVERINFO},
        {"sv_privatePassword", "", QA_CVAR_TEMPORARY}, {"sv_reconnectlimit", "3", 0},
        {"sv_minPing", "0", QA_CVAR_ARCHIVE | QA_CVAR_SERVERINFO},
        {"sv_maxPing", "0", QA_CVAR_ARCHIVE | QA_CVAR_SERVERINFO},
        {"sv_floodProtect", "1", QA_CVAR_ARCHIVE | QA_CVAR_SERVERINFO},
        {"sv_strictAuth", "1", QA_CVAR_ARCHIVE}, {"bot_enable", "1", 0},
        {"cm_noAreas", "0", QA_CVAR_CHEAT}, {"cm_noCurves", "0", QA_CVAR_CHEAT},
        {"cm_playerCurveClip", "1", QA_CVAR_ARCHIVE | QA_CVAR_CHEAT}, {"dedicated", "0", 0}
    };
    application_provider *provider = owner->engine->provider;
    qa_cvars *startup = provider->application->cvars;
    for (size_t i = 0; i < qa_cvars_count(startup); ++i) {
        const qa_cvar_view *value = qa_cvars_at(startup, i);
        if (value->owner || named(value->name, "sv_cheats") || named(value->name, "mapname") ||
            named(value->name, "sv_mapname") || named(value->name, "com_prereleaseDemo") ||
            named(value->name, "com_prereleaseTeamArenaDemo") || named(value->name, "fs_restrict")) continue;
        uint32_t flags = value->flags | QA_CVAR_USER_CREATED;
        for (size_t j = 0; j < sizeof(definitions) / sizeof(*definitions); ++j)
            if (named(value->name, definitions[j].name)) { flags = QA_CVAR_USER_CREATED; break; }
        if (!qa_cvars_find(owner->cvars, value->name) &&
            !qa_cvars_register(owner->cvars, value->name, value->reset_value,
                flags, 0, value->description, error)) return false;
        if (!qa_cvars_set(owner->cvars, value->name,
            value->latched_value ? value->latched_value : value->value, true, error)) return false;
    }
    if (!application_startup_seed_source(provider, owner->cvars, error) ||
        !application_q3_product_register_source(application_q3_product_source_policy(provider->application),
            owner->cvars, provider->owner, error)) return false;
    for (size_t i = 0; i < sizeof(definitions) / sizeof(*definitions); ++i)
        if (!qa_cvars_register(owner->cvars, definitions[i].name, definitions[i].value,
            definitions[i].flags, provider->owner, NULL, error)) return false;
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
            QA_CVAR_SERVERINFO | QA_CVAR_READONLY, provider->owner, NULL, error) &&
        qa_cvars_set(owner->cvars, "sv_mapname", map, true, error);
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
    qa_cvar_options cvars = {.dialect = QA_CONSOLE_Q3, .user = owner,
        .print = cvar_print, .cheats_allowed = cheats_allowed};
    owner->cvars = qa_cvars_create(&cvars, error);
    qa_console_options options = {.context = {.owner = engine->provider->owner,
        .dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_SERVER}, .cvars = owner->cvars,
        .user = owner, .print = print, .cvar_owner = cvar_owner, .visible_cvars = visible_cvars,
        .capture_context = capture, .context_active = active, .read_script = read_script,
        .release_script = release_script, .script_complete = script_complete,
        .allow_command = allow_command, .source_command = command};
    if (owner->cvars) owner->console = qa_console_create(&options, error);
    if (!owner->console || (!restoring && !register_engine(owner, map_path, error))) {
        qa_console_destroy(owner->console); qa_cvars_destroy(owner->cvars); free(owner);
        return false;
    }
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
        qa_console_destroy(engine->console->console);
        qa_cvars_destroy(engine->console->cvars);
        free(engine->console); engine->console = NULL;
    }
    return true;
}
