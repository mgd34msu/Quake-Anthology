#include "native_q3_console.h"
#include "q3_product.h"
#include "startup_flow.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

typedef struct q3_engine_cvar {
    const char *name, *value;
    uint32_t flags;
} q3_engine_cvar;

/* q3ServerCvarDefinitions and collisionMapCvarDefinitions registration order.
 * A NULL default is the actual incoming map identity, not a retained old map. */
static const q3_engine_cvar engine_cvars[] = {
    {"vm_game", "2", QA_CVAR_ARCHIVE},
    {"protocol", "68", QA_CVAR_SERVERINFO | QA_CVAR_READONLY},
    {"sv_pure", "1", QA_CVAR_SYSTEMINFO},
    {"sv_allowDownload", "0", QA_CVAR_SERVERINFO},
    {"sv_maxRate", "0", QA_CVAR_SERVERINFO},
    {"sv_fps", "20", 0},
    {"sv_serverid", "0", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
    {"sv_paks", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
    {"sv_pakNames", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
    {"sv_referencedPaks", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
    {"sv_referencedPakNames", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
    {"sv_maxclients", "8", QA_CVAR_SERVERINFO | QA_CVAR_LATCH},
    {"mapname", NULL, QA_CVAR_SERVERINFO | QA_CVAR_READONLY},
    {"sv_mapname", "", QA_CVAR_SERVERINFO | QA_CVAR_READONLY},
    {"sv_privateClients", "0", QA_CVAR_SERVERINFO},
    {"sv_privatePassword", "", QA_CVAR_TEMPORARY},
    {"sv_reconnectlimit", "3", 0},
    {"sv_minPing", "0", QA_CVAR_ARCHIVE | QA_CVAR_SERVERINFO},
    {"sv_maxPing", "0", QA_CVAR_ARCHIVE | QA_CVAR_SERVERINFO},
    {"sv_floodProtect", "1", QA_CVAR_ARCHIVE | QA_CVAR_SERVERINFO},
    {"sv_strictAuth", "1", QA_CVAR_ARCHIVE},
    {"bot_enable", "1", 0},
    {"cm_noAreas", "0", QA_CVAR_CHEAT},
    {"cm_noCurves", "0", QA_CVAR_CHEAT},
    {"cm_playerCurveClip", "1", QA_CVAR_ARCHIVE | QA_CVAR_CHEAT}
};

struct application_native_q3_console {
    application_provider *provider;
    qa_console *console;
    qa_cvars *cvars;
    size_t calls;
    bool settings_bound;
};

bool application_native_q3_console_settings_bound(const application_provider *provider)
{
    return provider && provider->native_q3_console && provider->native_q3_console->settings_bound;
}

void application_native_q3_console_settings_commit(application_provider *provider)
{
    provider->native_q3_console->settings_bound = true;
}

qa_cvars *application_native_q3_console_registry(const application_provider *provider)
{
    return provider && provider->kind == APPLICATION_PROVIDER_Q3 && provider->native_q3_console
        ? provider->native_q3_console->cvars : NULL;
}

qa_cvars *application_native_q3_cvar_owner(const application_provider *provider, const char *name)
{
    const char *engine = "sv_cheats";
    const char *value = name;
    while (value && *value && *engine && tolower((unsigned char)*value) == *engine) {
        ++value;
        ++engine;
    }
    if (value && !*value && !*engine)
        return provider && provider->application ? provider->application->cvars : NULL;
    return application_native_q3_console_registry(provider);
}

static qa_cvars *cvar_owner(void *context, const qa_command_context *command, const char *name)
{
    struct application_native_q3_console *owner = context;
    qa_cvars *selected = application_startup_cvar_owner(owner->provider, owner->console, command, name);
    if (selected) return selected;
    return application_native_q3_cvar_owner(owner->provider, name);
}

static qa_cvars *visible_cvars(void *context, const qa_command_context *command, size_t index)
{
    struct application_native_q3_console *owner = context;
    qa_cvars *selected = NULL;
    if (application_startup_visible_cvars(owner->provider, owner->console, command, index, &selected))
        return selected;
    return index == 0 ? owner->cvars : index == 1 ? owner->provider->application->cvars : NULL;
}

static bool cheats_allowed(void *context)
{
    struct application_native_q3_console *owner = context;
    return application_native_cheats_enabled(owner->provider);
}

bool application_native_q3_console_idle(const application_provider *provider)
{
    const struct application_native_q3_console *owner = provider ? provider->native_q3_console : NULL;
    return !owner || (!owner->calls && qa_console_idle(owner->console));
}

bool application_native_q3_console_borrow(application_provider *provider, qa_error *error)
{
    struct application_native_q3_console *owner = provider ? provider->native_q3_console : NULL;
    if (!owner || !provider->constructed || !provider->attached || provider->close_pending ||
        provider->application->destroy_requested || owner->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 console source is not admitted");
    ++owner->calls;
    return true;
}

void application_native_q3_console_release(application_provider *provider)
{
    if (provider && provider->native_q3_console && provider->native_q3_console->calls)
        --provider->native_q3_console->calls;
}

static bool capture(void *context, const qa_command_context *source,
                    qa_command_context *out, qa_error *error)
{
    struct application_native_q3_console *owner = context;
    qa_command_context command = *source;
    if (command.owner && command.owner != owner->provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 console received another source owner");
    command.owner = owner->provider->owner;
    command.dialect = QA_CONSOLE_Q3;
    return application_command_capture(owner->provider->application, &command, out, error);
}

static bool active(void *context, const qa_command_context *command)
{
    struct application_native_q3_console *owner = context;
    return command && command->owner == owner->provider->owner &&
        command->dialect == QA_CONSOLE_Q3 &&
        application_command_active(owner->provider->application, command);
}

static void print(void *context, const qa_command_context *command, const char *text)
{
    struct application_native_q3_console *owner = context;
    ++owner->calls;
    application_console_print(owner->provider->application, command, text);
    --owner->calls;
}

static void cvar_print(void *context, const char *text)
{
    struct application_native_q3_console *owner = context;
    qa_console_emit(owner->console, NULL, text);
}

static bool read_script(void *context, const qa_command_context *command,
                        const char *path, qa_bytes *out, void **lease, qa_error *error)
{
    struct application_native_q3_console *owner = context;
    if (!active(owner, command))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 script publication has retired");
    if (application_startup_source_active(owner->provider))
        return application_startup_script_read(owner->provider, command, path, out, lease, error);
    if (application_startup_source_scripts(owner->provider))
        return application_startup_source_script_read(owner->provider, owner->console,
            command, path, out, lease, error);
    qa_vfs *files = qa_application_context_files(owner->provider->application, command, NULL);
    qa_resource *resource = NULL;
    if (!files || !qa_vfs_acquire(files, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource);
    *lease = resource;
    return true;
}

static void release_script(void *context, void *lease)
{
    struct application_native_q3_console *owner = context;
    if (application_startup_source_active(owner->provider))
        application_startup_script_release(owner->provider, lease);
    else if (application_startup_source_scripts(owner->provider))
        application_startup_source_script_release(owner->provider, owner->console, lease);
    else qa_resource_release(lease);
}

static void script_complete(void *context, const qa_command_context *command,
    const char *path, bool success)
{
    struct application_native_q3_console *owner = context;
    application_startup_script_complete(owner->provider, command, path, success);
}

static bool allow_command(void *context, const qa_command_invocation *command)
{
    struct application_native_q3_console *owner = context;
    return application_startup_command_allowed(owner->provider, command);
}

static qa_command_result command(void *context, const qa_command_invocation *invocation,
                                  qa_error *error)
{
    struct application_native_q3_console *owner = context;
    ++owner->calls;
    qa_command_result result = application_command_fallback(owner->provider->application, invocation, error);
    --owner->calls;
    return result;
}

bool application_native_q3_console_at(application_provider *provider, qa_console **console,
                                       qa_cvars **cvars, qa_command_context *context)
{
    struct application_native_q3_console *owner = provider ? provider->native_q3_console : NULL;
    if (!owner || !console) return false;
    *console = owner->console;
    if (cvars) *cvars = owner->cvars;
    if (context) *context = (qa_command_context){.owner = provider->owner,
        .dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_SERVER};
    return true;
}

static bool startup_cvar(struct application_native_q3_console *owner,
    const char *name, qa_error *error)
{
    const qa_cvar_view *startup = qa_cvars_find(owner->provider->application->cvars, name);
    if (!startup) return true;
    if (startup->owner && startup->owner != owner->provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "native Q3 startup cvar belongs to another source");
    return qa_cvars_set(owner->cvars, name,
        startup->latched_value ? startup->latched_value : startup->value, true, error);
}

static bool register_engine_cvars(struct application_native_q3_console *owner,
    const char *map_path, qa_error *error)
{
    const char *name = !strncmp(map_path, "maps/", 5) ? map_path + 5 : map_path;
    size_t length = strlen(name);
    if (length > 4 && !strcmp(name + length - 4, ".bsp")) length -= 4;
    if (!length)
        return application_fail(error, QA_ERROR_FORMAT,
                                "native Q3 startup map has no source identity");
    char *map = malloc(length + 1);
    if (!map)
        return application_fail(error, QA_ERROR_MEMORY,
                                "retaining native Q3 startup map identity");
    memcpy(map, name, length);
    map[length] = 0;

    bool okay = true;
    for (size_t i = 0; okay && i < sizeof(engine_cvars) / sizeof(engine_cvars[0]); ++i) {
        const q3_engine_cvar *definition = &engine_cvars[i];
        okay = qa_cvars_register(owner->cvars, definition->name,
            definition->value ? definition->value : map, definition->flags,
            owner->provider->owner, NULL, error);
    }
    if (okay) okay = qa_cvars_set(owner->cvars, "sv_mapname", map, true, error);
    for (size_t i = 0; okay && i < sizeof(engine_cvars) / sizeof(engine_cvars[0]); ++i) {
        const char *variable = engine_cvars[i].name;
        if (!strcmp(variable, "mapname") || !strcmp(variable, "sv_mapname")) continue;
        okay = startup_cvar(owner, variable, error);
    }
    if (okay)
        okay = qa_cvars_set(owner->cvars, "mapname", map, true, error) &&
               qa_cvars_set(owner->cvars, "sv_mapname", map, true, error) &&
               qa_cvars_register(owner->cvars, "dedicated", "0", 0,
                   owner->provider->owner, NULL, error) &&
               startup_cvar(owner, "dedicated", error);
    free(map);
    return okay;
}

bool application_native_q3_console_create(application_provider *provider,
    const char *map_path, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->owner ||
        !provider->application || !provider->application->session || !provider->launch ||
        !provider->product || provider->product->family != QA_GAME_Q3 ||
        !map_path || provider->close_pending || provider->native_q3_console)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 console requires its actual source owner");
    struct application_native_q3_console *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "allocating native Q3 source console");
    owner->provider = provider;
    qa_cvar_options cvars = {.dialect = QA_CONSOLE_Q3, .user = owner, .print = cvar_print,
        .cheats_allowed = cheats_allowed};
    owner->cvars = qa_cvars_create(&cvars, error);
    qa_console_options options = {.context = {.owner = provider->owner, .dialect = QA_CONSOLE_Q3,
        .origin = QA_COMMAND_SERVER}, .cvars = owner->cvars, .user = owner, .print = print,
        .cvar_owner = cvar_owner, .visible_cvars = visible_cvars,
        .capture_context = capture, .context_active = active, .read_script = read_script,
        .release_script = release_script, .script_complete = script_complete,
        .allow_command = allow_command, .source_command = command};
    if (owner->cvars) owner->console = qa_console_create(&options, error);
    if (!owner->console || !application_startup_seed_source(provider, owner->cvars, error) ||
        !application_q3_product_register_source(application_q3_product_source_policy(provider->application),
            owner->cvars, provider->owner, error) || !register_engine_cvars(owner, map_path, error)) {
        qa_console_destroy(owner->console);
        qa_cvars_destroy(owner->cvars);
        free(owner);
        return false;
    }
    provider->native_q3_console = owner;
    return true;
}

bool application_native_q3_console_destroy(application_provider *provider, qa_error *error)
{
    if (!provider || !application_native_q3_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 console is borrowed");
    struct application_native_q3_console *owner = provider->native_q3_console;
    if (owner) {
        if (!application_startup_source_retire(provider, owner->console, owner->cvars, error)) return false;
        qa_console_destroy(owner->console);
        qa_cvars_destroy(owner->cvars);
        free(owner);
        provider->native_q3_console = NULL;
    }
    return true;
}
