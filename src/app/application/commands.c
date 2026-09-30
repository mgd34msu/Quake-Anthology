#include "internal.h"
#include "guest_native_q2_private.h"
#include "qa/application_players.h"

#include <string.h>

static application_provider *command_owner(const qa_application *application, uint64_t owner)
{
    if (owner == 0 || owner > UINT32_MAX)
        return NULL;
    for (application_provider *provider = application->live_providers;
         provider != NULL; provider = provider->next_live)
        if (provider->owner == owner && provider->constructed && provider->attached)
            return provider;
    return NULL;
}

bool qa_application_command_context_active(const qa_application *application,
                                           const qa_command_context *context)
{
    if (application == NULL || context == NULL || application->session == NULL ||
        application->destroy_requested || application->state == QA_APPLICATION_FAULTED ||
        context->registry != qa_actors_identity(qa_session_actors(application->session)) ||
        context->generation != application->command_generation)
        return false;
    if (context->owner != 0 && command_owner(application, context->owner) == NULL)
        return false;
    if (context->origin == QA_COMMAND_SEAT ||
        (context->origin == QA_COMMAND_LOCAL && context->actor.registry != 0)) {
        qa_actor_id controlled = {0};
        bool present = qa_application_player_actor(application, context->seat, &controlled);
        if (present != (context->actor.registry != 0) ||
            (present && !qa_actor_id_equal(controlled, context->actor)))
            return false;
    }
    return context->actor.registry == 0 ||
        qa_actors_get(qa_session_actors(application->session), context->actor) != NULL;
}

bool qa_application_capture_command_context(qa_application *application,
                                             const qa_command_context *source,
                                             qa_command_context *out, qa_error *error)
{
    if (application == NULL || source == NULL || out == NULL || application->session == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "command capture requires an application session");
    qa_command_context next = *source;
    if ((next.registry != 0 || next.generation != 0) &&
        !qa_application_command_context_active(application, &next))
        return application_fail(error, QA_ERROR_ARGUMENT, "command belongs to a retired publication");
    next.registry = qa_actors_identity(qa_session_actors(application->session));
    next.generation = application->command_generation;
    if (next.actor.registry == 0 &&
        (next.origin == QA_COMMAND_LOCAL || next.origin == QA_COMMAND_SEAT))
        (void)qa_application_player_actor(application, next.seat, &next.actor);
    if (!qa_application_command_context_active(application, &next))
        return application_fail(error, QA_ERROR_ARGUMENT, "command owner or actor is no longer admitted");
    *out = next;
    return true;
}

bool application_command_capture(void *opaque, const qa_command_context *source,
                                  qa_command_context *out, qa_error *error)
{
    qa_application *application = opaque;
    if (application != NULL && application->session == NULL) {
        *out = *source;
        return true;
    }
    return qa_application_capture_command_context(application, source, out, error);
}

bool application_command_active(void *opaque, const qa_command_context *context)
{
    if (context == NULL)
        return false;
    if (context->registry == 0 && context->generation == 0)
        return true;
    return qa_application_command_context_active(opaque, context);
}

qa_vfs *qa_application_context_files(qa_application *application,
                                      const qa_command_context *context, qa_mount_id *write_mount)
{
    if (write_mount != NULL)
        *write_mount = 0;
    if (!qa_application_command_context_active(application, context))
        return NULL;
    qa_vfs *files;
    if (context->owner != 0) {
        application_provider *provider = command_owner(application, context->owner);
        files = provider == NULL || provider->launch == NULL ? NULL : provider->launch->content;
    } else {
        const qa_launch_snapshot *snapshot = application->routing_snapshot;
        if (snapshot == NULL)
            snapshot = qa_application_launch(application);
        files = qa_launch_snapshot_mounts(snapshot);
    }
    if (files != NULL && write_mount != NULL)
        for (size_t index = 0; index < qa_vfs_mount_count(files); ++index) {
            qa_vfs_mount_info mount;
            if (qa_vfs_mount_at(files, index, &mount) && mount.writable) {
                *write_mount = mount.id;
                break;
            }
        }
    return files;
}

static bool console_seen_before(const qa_application *application,
                                 const application_provider *stop, size_t ordinal,
                                 qa_console *console)
{
    if (console == application->console)
        return true;
    for (application_provider *provider = application->live_providers;
         provider != NULL; provider = provider->next_live) {
        if (!provider->attached || !provider->constructed)
            continue;
        for (size_t index = 0;; ++index) {
            if (provider == stop && index == ordinal)
                return false;
            qa_console *prior;
            if (!application_guest_console_at(provider, index, &prior, NULL, NULL))
                break;
            if (prior == console)
                return true;
        }
    }
    return false;
}

size_t qa_application_console_count(const qa_application *application)
{
    if (application == NULL)
        return 0;
    size_t count = application->console != NULL;
    for (application_provider *provider = application->live_providers;
         provider != NULL; provider = provider->next_live)
        if (provider->attached && provider->constructed)
            for (size_t index = 0;; ++index) {
                qa_console *console;
                if (!application_guest_console_at(provider, index, &console, NULL, NULL))
                    break;
                if (!console_seen_before(application, provider, index, console))
                    ++count;
            }
    return count;
}

qa_console *qa_application_console_at(qa_application *application, size_t index,
                                       qa_actor_owner *owner)
{
    if (application == NULL)
        return NULL;
    if (application->console != NULL) {
        if (index == 0) {
            if (owner != NULL) *owner = 0;
            return application->console;
        }
        --index;
    }
    for (application_provider *provider = application->live_providers;
         provider != NULL; provider = provider->next_live) {
        if (!provider->attached || !provider->constructed)
            continue;
        for (size_t ordinal = 0;; ++ordinal) {
            qa_console *console;
            if (!application_guest_console_at(provider, ordinal, &console, NULL, NULL))
                break;
            if (console_seen_before(application, provider, ordinal, console))
                continue;
            if (index == 0) {
                if (owner != NULL) *owner = provider->owner;
                return console;
            }
            --index;
        }
    }
    return NULL;
}

bool qa_application_console_scope_read(const qa_application *application,
    const qa_console *console, qa_application_console_scope *out)
{
    if (!application || !console || !out)
        return false;
    if (console == application->console) {
        *out = (qa_application_console_scope){.kind = QA_APPLICATION_CONSOLE_ENGINE};
        return true;
    }
    const char *instance = NULL;
    qa_application_console_scope result = {0};
    for (application_provider *provider = application->live_providers;
         provider; provider = provider->next_live) {
        qa_application_console_scope scope;
        if (!provider->attached || !provider->constructed || provider->close_pending ||
            !provider->launch ||
            !application_guest_console_scope(provider, console, &scope))
            continue;
        const char *name = provider->launch->selection.instance;
        if (!instance || strcmp(name, instance) < 0) {
            instance = name;
            result = scope;
        }
    }
    if (!instance)
        return false;
    *out = result;
    return true;
}

static qa_launch_role command_role(const char *name)
{
    static const char *const arsenal[] = {
        "use", "drop", "give", "giveall", "impulse", "weapon", "weapnext", "weapprev",
        "weaplast", "invnext", "invprev", "invuse", "invdrop", "useholdable"
    };
    for (size_t index = 0; index < sizeof(arsenal) / sizeof(arsenal[0]); ++index)
        if (!strcmp(name, arsenal[index]))
            return QA_ROLE_ARSENAL;
    return QA_ROLE_CHARACTER;
}

static bool provider_command(application_provider *provider, const qa_command_invocation *command,
                              bool *handled, qa_error *error)
{
    *handled = false;
    if (provider == NULL)
        return true;
    qa_actor_id actor = command->context.actor;
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1:
        return qa_q1_game_console_command(provider->state.q1, actor, command, handled, error);
    case APPLICATION_PROVIDER_Q2:
        return qa_q2_game_console_command(provider->state.q2, actor, command, handled, error);
    case APPLICATION_PROVIDER_Q3:
        return qa_q3_game_console_command(provider->state.q3, actor, command, handled, error);
    case APPLICATION_PROVIDER_QC:
        return application_qc_console_command(provider, actor, command->raw,
                                                 actor.registry != 0, handled, error);
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE: {
        if (provider->kind == APPLICATION_PROVIDER_NATIVE &&
            provider->state.native.q2_engine != NULL)
            return application_native_q2_console_command(provider, actor,
                                                           command->raw, handled, error);
        uint32_t slot;
        if (actor.registry != 0 && application_q3_guest_actor_client(provider, actor, &slot)) {
            bool ok = application_q3_guest_client_command(provider, slot, command->raw, error);
            *handled = ok;
            return ok;
        }
        return application_q3_guest_console_command(provider, command->raw, handled, error);
    }
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "unknown source command provider");
}

qa_command_result application_command_fallback(void *opaque,
                                                 const qa_command_invocation *invocation,
                                                 qa_error *error)
{
    qa_application *application = opaque;
    if (invocation == NULL || invocation->argc == 0 || invocation->argv == NULL)
        return QA_COMMAND_UNHANDLED;
    qa_command_invocation command = *invocation;
    if (!qa_application_capture_command_context(application, &invocation->context,
                                                  &command.context, error))
        return QA_COMMAND_FAILED;
    bool handled = false;
    qa_actor_id actor = command.context.actor;
    if (actor.registry != 0 && application->primary_mode_ready &&
        !qa_modes_console_command(application->modes, application->primary_mode,
                                    actor, &command, &handled, error))
        return QA_COMMAND_FAILED;
    if (handled)
        return QA_COMMAND_HANDLED;
    application_provider *provider = command.context.owner != 0
        ? command_owner(application, command.context.owner)
        : application_provider_for(application, actor, command_role(command.argv[0]), "");
    if (!provider_command(provider, &command, &handled, error))
        return QA_COMMAND_FAILED;
    return handled ? QA_COMMAND_HANDLED : QA_COMMAND_UNHANDLED;
}

bool qa_application_source_command(qa_application *application,
                                     const qa_command_invocation *command, qa_error *error)
{
    qa_command_result result = application_command_fallback(application, command, error);
    if (result == QA_COMMAND_UNHANDLED)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "selected source did not handle the command");
    return result == QA_COMMAND_HANDLED;
}

bool qa_application_actor_command(qa_application *application, qa_actor_id actor,
                                    const char *text, qa_error *error)
{
    if (application == NULL || text == NULL || application->session == NULL ||
        qa_actors_get(qa_session_actors(application->session), actor) == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "actor command requires an admitted actor");
    application_provider *provider = application_provider_for(application, actor, QA_ROLE_CHARACTER, "");
    qa_console_dialect dialect = QA_CONSOLE_Q3;
    if (provider != NULL && provider->product != NULL) {
        if (provider->product->family == QA_GAME_Q1)
            dialect = provider->product->edition == QA_EDITION_QUAKEWORLD ? QA_CONSOLE_QW : QA_CONSOLE_Q1;
        else if (provider->product->family == QA_GAME_Q2)
            dialect = provider->product->edition == QA_EDITION_RERELEASE ? QA_CONSOLE_Q2_RERELEASE : QA_CONSOLE_Q2;
    }
    qa_command_context context = {.dialect = dialect, .origin = QA_COMMAND_REMOTE,
        .actor = actor};
    (void)qa_application_player_seat(application, actor, &context.seat);
    return qa_console_execute_now(application->console, &context, text, error);
}
