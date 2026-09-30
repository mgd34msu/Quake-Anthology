#include "native_q3_console.h"

#include <stdlib.h>
#include <ctype.h>

struct application_native_q3_console {
    application_provider *provider;
    qa_console *console;
    qa_cvars *cvars;
    size_t calls;
};

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
    (void)command;
    struct application_native_q3_console *owner = context;
    return application_native_q3_cvar_owner(owner->provider, name);
}

static qa_cvars *visible_cvars(void *context, const qa_command_context *command, size_t index)
{
    (void)command;
    struct application_native_q3_console *owner = context;
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
    qa_vfs *files = qa_application_context_files(owner->provider->application, command, NULL);
    qa_resource *resource = NULL;
    if (!files || !qa_vfs_acquire(files, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource);
    *lease = resource;
    return true;
}

static void release_script(void *context, void *lease)
{
    (void)context;
    qa_resource_release(lease);
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

bool application_native_q3_console_create(application_provider *provider, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->application || provider->native_q3_console)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 console requires its actual game owner");
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
        .release_script = release_script, .source_command = command};
    if (owner->cvars) owner->console = qa_console_create(&options, error);
    if (!owner->console) { qa_cvars_destroy(owner->cvars); free(owner); return false; }
    provider->native_q3_console = owner;
    return true;
}

bool application_native_q3_console_destroy(application_provider *provider, qa_error *error)
{
    if (!provider || !application_native_q3_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 console is borrowed");
    struct application_native_q3_console *owner = provider->native_q3_console;
    if (owner) {
        qa_console_destroy(owner->console);
        qa_cvars_destroy(owner->cvars);
        free(owner);
        provider->native_q3_console = NULL;
    }
    return true;
}
