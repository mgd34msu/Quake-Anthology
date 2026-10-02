#include "native_q1_console.h"
#include "startup_flow.h"
#include "qa/cvars_save.h"
#include "qa/source_number.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct application_native_q1_console {
    application_provider *provider;
    qa_console *console;
    qa_cvars *cvars;
    size_t calls;
};

qa_cvars *application_native_q1_console_registry(const application_provider *provider)
{
    return provider && provider->kind == APPLICATION_PROVIDER_Q1 && provider->native_q1_console
        ? provider->native_q1_console->cvars : NULL;
}

bool application_native_q1_console_idle(const application_provider *provider)
{
    const struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    return !owner || (!owner->calls && qa_console_idle(owner->console));
}

static qa_console_dialect dialect(const application_provider *provider)
{
    return provider->launch->selection.clock.kind == QA_CLOCK_QUAKEWORLD ? QA_CONSOLE_QW : QA_CONSOLE_Q1;
}

static bool capture(void *opaque, const qa_command_context *source,
                    qa_command_context *out, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    qa_command_context command = *source;
    if (command.owner && command.owner != owner->provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console received another source owner");
    command.owner = owner->provider->owner;
    command.dialect = dialect(owner->provider);
    return application_command_capture(owner->provider->application, &command, out, error);
}

static bool active(void *opaque, const qa_command_context *command)
{
    struct application_native_q1_console *owner = opaque;
    return command && command->owner == owner->provider->owner &&
        command->dialect == dialect(owner->provider) &&
        application_command_active(owner->provider->application, command);
}

static void print(void *opaque, const qa_command_context *command, const char *text)
{
    struct application_native_q1_console *owner = opaque;
    ++owner->calls;
    application_console_print(owner->provider->application, command, text);
    --owner->calls;
}

static void cvar_print(void *opaque, const char *text)
{
    struct application_native_q1_console *owner = opaque;
    qa_console_emit(owner->console, NULL, text);
}
void application_native_q1_source_console_print(void *opaque, const char *text)
{
    application_provider *provider = opaque;
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    if (owner && provider->kind == APPLICATION_PROVIDER_Q1 && text)
        qa_console_emit(owner->console, NULL, text);
}

static qa_cvars *cvar_owner(void *opaque, const qa_command_context *command, const char *name)
{
    struct application_native_q1_console *owner = opaque;
    qa_cvars *selected = application_startup_cvar_owner(owner->provider, owner->console, command, name);
    return selected ? selected : owner->cvars;
}

static qa_cvars *visible_cvars(void *opaque, const qa_command_context *command, size_t index)
{
    struct application_native_q1_console *owner = opaque;
    qa_cvars *selected = NULL;
    if (application_startup_visible_cvars(owner->provider, owner->console, command, index, &selected))
        return selected;
    return index == 0 ? owner->cvars : NULL;
}

static bool cvar_edit(void *opaque, const qa_command_context *command,
    qa_cvars *registry, qa_cvars_edit **out, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    return application_startup_cvar_edit(owner->provider, owner->console, command, registry, out, error);
}

static qa_command_result command(void *opaque, const qa_command_invocation *invocation, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    ++owner->calls;
    qa_command_result result = application_command_fallback(owner->provider->application, invocation, error);
    --owner->calls;
    return result;
}

static bool read_script(void *opaque, const qa_command_context *command,
                        const char *path, qa_bytes *out, void **lease, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    if (!active(owner, command))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 script publication has retired");
    if (application_startup_source_active(owner->provider))
        return application_startup_script_read(owner->provider, command, path, out, lease, error);
    if (application_startup_source_scripts(owner->provider))
        return application_startup_source_script_read(owner->provider, owner->console,
            command, path, out, lease, error);
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire(owner->provider->launch->content, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource);
    *lease = resource;
    return true;
}

static void release_script(void *opaque, void *lease)
{
    struct application_native_q1_console *owner = opaque;
    if (application_startup_source_active(owner->provider))
        application_startup_script_release(owner->provider, lease);
    else if (application_startup_source_scripts(owner->provider))
        application_startup_source_script_release(owner->provider, owner->console, lease);
    else qa_resource_release(lease);
}

static void script_complete(void *opaque, const qa_command_context *command,
    const char *path, bool success)
{
    struct application_native_q1_console *owner = opaque;
    application_startup_script_complete(owner->provider, command, path, success);
}

static bool allow_command(void *opaque, const qa_command_invocation *command)
{
    struct application_native_q1_console *owner = opaque;
    return application_startup_command_allowed(owner->provider, command);
}

bool application_native_q1_console_create_restored(application_provider *provider, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1 || !provider->owner ||
        !provider->application || !provider->application->session || !provider->launch ||
        !provider->product || provider->product->family != QA_GAME_Q1 ||
        provider->close_pending || provider->native_q1_console)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console requires its actual source owner");
    struct application_native_q1_console *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "allocating native Q1 source console");
    owner->provider = provider;
    qa_cvar_options cvars = {.dialect = dialect(provider), .user = owner, .print = cvar_print};
    owner->cvars = qa_cvars_create(&cvars, error);
    qa_console_options options = {.context = {.owner = provider->owner, .dialect = dialect(provider),
        .origin = QA_COMMAND_SERVER}, .cvars = owner->cvars, .user = owner, .print = print,
        .cvar_owner = cvar_owner, .visible_cvars = visible_cvars, .cvar_edit = cvar_edit,
        .capture_context = capture, .context_active = active, .read_script = read_script,
        .release_script = release_script, .script_complete = script_complete,
        .allow_command = allow_command, .source_command = command};
    if (owner->cvars) owner->console = qa_console_create(&options, error);
    if (!owner->console) {
        qa_console_destroy(owner->console);
        qa_cvars_destroy(owner->cvars);
        free(owner);
        return false;
    }
    provider->native_q1_console = owner;
    return true;
}

static bool clone_source(application_provider *provider, qa_cvars *destination,
                         bool *cloned, qa_error *error)
{
    *cloned = false;
    if (provider->application->startup_hooks) {
        qa_application_startup_source source = {.descriptor = provider->launch,
            .scope = {.provider = provider->owner, .kind = QA_APPLICATION_CONSOLE_Q1_GAME},
            .cvars = destination, .declaration_owner = provider->owner};
        if (!application_native_q1_console_at(provider, &source.console, NULL, &source.command))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q1 carry lost its fresh physical console");
        return application_startup_source_carry(provider, &source, cloned, error);
    }
    for (application_provider *previous = provider->application->live_providers; previous;
         previous = previous->next_live) {
        if (previous == provider || previous->kind != APPLICATION_PROVIDER_Q1 ||
            !previous->constructed || !previous->attached || previous->close_pending ||
            !previous->launch || strcmp(previous->launch->selection.instance, provider->launch->selection.instance) ||
            previous->owner != provider->owner) continue;
        qa_cvars *source = application_native_q1_console_registry(previous);
        if (!source || qa_cvars_dialect(source) != qa_cvars_dialect(destination)) continue;
        qa_buffer bytes = {0};
        qa_cvars_restore *ticket = NULL;
        bool okay = application_native_q1_console_idle(previous) &&
            qa_cvars_save_capture(source, &bytes, error) &&
            qa_cvars_save_prepare(destination, (qa_bytes){bytes.data, bytes.size}, &ticket, error) &&
            qa_cvars_save_commit(ticket, error);
        if (!okay) qa_cvars_save_abort(ticket);
        qa_buffer_free(&bytes);
        if (okay) *cloned = true;
        return okay;
    }
    return true;
}

bool application_native_q1_console_create(application_provider *provider,
                                          const qa_q1_options *rules, qa_error *error)
{
    if (!rules || !application_native_q1_console_create_restored(provider, error)) return false;
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    bool cloned;
    bool okay = clone_source(provider, cvars, &cloned, error);
    for (size_t i = 0; okay && !cloned && i < qa_cvars_count(provider->application->cvars); ++i) {
        const qa_cvar_view *startup = qa_cvars_at(provider->application->cvars, i);
        if (!startup || (startup->owner && startup->owner != provider->owner) ||
            (!startup->console_created && !(startup->flags & QA_CVAR_ARCHIVE) && startup->owner != provider->owner)) continue;
        okay = qa_cvars_register(cvars, startup->name,
            startup->latched_value ? startup->latched_value : startup->value,
            startup->flags & (QA_CVAR_ARCHIVE | QA_CVAR_USERINFO | QA_CVAR_SERVERINFO),
            provider->owner, startup->description, error);
    }
    static const char *const names[] = {"skill", "deathmatch", "coop", "teamplay", "sv_gravity",
        "sv_maxspeed", "samelevel", "timelimit", "fraglimit", "gamecfg", "sv_cheats", "footsteps",
        "maxclients", "registered", "developer", "sv_aim"};
    char skill[16], deathmatch[16], coop[2], teamplay[16], gravity[32], gamecfg[16], maximum[16], aim[32];
    snprintf(skill, sizeof(skill), "%u", rules->skill);
    snprintf(deathmatch, sizeof(deathmatch), "%d", rules->deathmatch);
    snprintf(coop, sizeof(coop), "%u", rules->coop ? 1u : 0u);
    snprintf(teamplay, sizeof(teamplay), "%d", rules->teamplay);
    snprintf(gravity, sizeof(gravity), "%.9g", (double)rules->gravity);
    snprintf(gamecfg, sizeof(gamecfg), "%u", rules->gamecfg);
    snprintf(maximum, sizeof(maximum), "%u", rules->quakeworld ? 8u : rules->max_clients);
    snprintf(aim, sizeof(aim), "%.9g", (double)rules->aim_threshold);
    const char *values[] = {skill, deathmatch, coop, teamplay, gravity, "320", "0", "0", "0", gamecfg,
        "0", "1", maximum, "1", "0", aim};
    for (size_t i = 0; okay && i < sizeof(names) / sizeof(*names); ++i) {
        if (!qa_cvars_find(cvars, names[i]))
            okay = qa_cvars_register(cvars, names[i], values[i], 0, provider->owner, NULL, error);
        const qa_cvar_view *startup = !cloned ? qa_cvars_find(provider->application->cvars, names[i]) : NULL;
        if (okay && startup && (!startup->owner || startup->owner == provider->owner))
            okay = qa_cvars_set(cvars, names[i], startup->latched_value ? startup->latched_value : startup->value,
                true, error);
    }
    if (rules->quakeworld) {
        static const char *const qw_names[] = {"sv_maxvelocity", "sv_stopspeed",
            "sv_spectatormaxspeed", "sv_accelerate", "sv_airaccelerate",
            "sv_wateraccelerate", "sv_friction", "sv_waterfriction",
            "maxspectators", "pausable", "sv_spectalk", "sv_mapcheck",
            "hostname", "spawn", "watervis", "sv_phs"};
        static const char *const qw_values[] = {"2000", "100", "500", "10", "0.7",
            "10", "4", "4", "8", "1", "1", "1", "unnamed", "0", "0", "1"};
        for (size_t i = 0; okay && i < sizeof(qw_names) / sizeof(*qw_names); ++i) {
            if (!qa_cvars_find(cvars, qw_names[i]))
                okay = qa_cvars_register(cvars, qw_names[i], qw_values[i], 0,
                    provider->owner, NULL, error);
            const qa_cvar_view *startup = !cloned
                ? qa_cvars_find(provider->application->cvars, qw_names[i]) : NULL;
            if (okay && startup && (!startup->owner || startup->owner == provider->owner))
                okay = qa_cvars_set(cvars, qw_names[i],
                    startup->latched_value ? startup->latched_value : startup->value, true, error);
        }
        static const char *const info_names[] = {"fraglimit", "timelimit", "teamplay",
            "samelevel", "maxclients", "maxspectators", "deathmatch", "spawn",
            "watervis", "hostname"};
        for (size_t i = 0; okay && i < sizeof(info_names) / sizeof(*info_names); ++i)
            okay = qa_cvars_add_flags(cvars, info_names[i], QA_CVAR_SERVERINFO, error);
    }
    if (okay) okay = qa_cvars_set(cvars, "skill", skill, true, error) &&
        qa_cvars_set(cvars, "deathmatch", deathmatch, true, error) &&
        qa_cvars_set(cvars, "coop", coop, true, error) &&
        (rules->quakeworld || qa_cvars_set(cvars, "maxclients", maximum, true, error));
    if (!okay) application_native_q1_console_destroy(provider, NULL);
    return okay;
}

bool application_native_q1_console_destroy(application_provider *provider, qa_error *error)
{
    if (!provider || !application_native_q1_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console is borrowed");
    struct application_native_q1_console *owner = provider->native_q1_console;
    if (owner) {
        if (!application_startup_source_retire(provider, owner->console, owner->cvars, error)) return false;
        qa_console_destroy(owner->console);
        qa_cvars_destroy(owner->cvars);
        free(owner);
        provider->native_q1_console = NULL;
    }
    return true;
}

bool application_native_q1_console_at(application_provider *provider, qa_console **console,
                                      qa_cvars **cvars, qa_command_context *context)
{
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    if (!owner || !console) return false;
    *console = owner->console;
    if (cvars) *cvars = owner->cvars;
    if (context) *context = (qa_command_context){.owner = provider->owner,
        .dialect = dialect(provider), .origin = QA_COMMAND_SERVER};
    return true;
}

bool application_native_q1_console_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    if (!cvars || !application_native_q1_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 registry capture requires its idle source owner");
    return qa_cvars_save_capture(cvars, out, error);
}

bool application_native_q1_console_restore(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    if (!cvars || !application_native_q1_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 registry restore requires its idle source owner");
    qa_cvars_restore *ticket = NULL;
    bool okay = qa_cvars_save_prepare(cvars, bytes, &ticket, error) && qa_cvars_save_commit(ticket, error);
    if (!okay) qa_cvars_save_abort(ticket);
    return okay;
}

bool application_native_q1_cvar(void *opaque, qa_string_id name, float *out, qa_error *error)
{
    application_provider *provider = opaque;
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    const char *text = provider && provider->application && provider->application->session
        ? qa_strings_cstr(qa_session_strings(provider->application->session), name) : NULL;
    if (!cvars || !text || !out || !provider->constructed || provider->close_pending ||
        provider->application->destroy_requested)
        return application_fail(error, QA_ERROR_NOT_FOUND, "native Q1 cvar source has retired");
    const qa_cvar_view *value = qa_cvars_find(cvars, text);
    if (value && value->owner && value->owner != provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 cvar belongs to another source");
    *out = value ? (float)qa_source_fround(value->number) : 0;
    return true;
}

bool application_native_q1_client_attack(void *opaque, qa_actor_id actor, bool *out)
{
    application_provider *provider = opaque;
    qa_application_control_view control;
    uint32_t slot;
    if (!out || !provider || provider->kind != APPLICATION_PROVIDER_Q1 || !provider->constructed ||
        !provider->attached || provider->close_pending || provider->application->destroy_requested ||
        !qa_q1_native_client_slot_prepared(provider->state.q1, actor, &slot, NULL)) return false;
    *out = qa_application_control_read(provider->application, actor, &control) && (control.buttons & 1u) != 0;
    return true;
}
