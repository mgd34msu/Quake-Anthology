#include "internal.h"
#include "guest_native_q2_private.h"
#include "guest_qc_profile.h"
#include "match_intents.h"
#include "native_q3_clients.h"
#include "native_q3_ipfilters.h"
#include "native_q3_postgame.h"
#include "native_q1_console.h"
#include "startup_flow.h"
#include "bots_catalog.h"
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

static bool q3_command_named(const char *text, const char *name)
{
    while (*text && *name) {
        unsigned char left = (unsigned char)*text++;
        unsigned char right = (unsigned char)*name++;
        if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
        if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
        if (left != right) return false;
    }
    return *text == *name;
}

static qa_command_result q3_round_command(qa_application *application,
    const qa_command_invocation *invocation, qa_error *error)
{
    if (invocation->context.dialect != QA_CONSOLE_Q3 ||
        !q3_command_named(invocation->argv[0], "map_restart"))
        return QA_COMMAND_UNHANDLED;
    qa_mode_id mode = application->primary_mode;
    application_provider *provider = application->primary_mode_ready
        ? application_native_q3_mode_source_provider(application, mode) : NULL;
    if (invocation->context.owner) {
        provider = NULL;
        for (size_t i = 0; i < application->mode_count; ++i) {
            application_provider *candidate = application_native_q3_mode_source_provider(application,
                application->mode_ids[i]);
            if (candidate && candidate->owner == invocation->context.owner) {
                provider = candidate;
                mode = application->mode_ids[i];
                break;
            }
        }
    }
    qa_mode_view view;
    if (!provider || !qa_modes_read(application->modes, mode, &view, NULL) ||
        view.rules.source < QA_MODE_Q3)
        return QA_COMMAND_UNHANDLED;
    qa_command_invocation command = *invocation;
    command.context.owner = provider->owner;
    bool game_view = false;
    for (size_t i = 0;; ++i) {
        qa_application_startup_source source;
        bool present;
        if (!application_provider_startup_source_at(provider, i, &source, &present, error))
            return QA_COMMAND_FAILED;
        if (!present) break;
        if (source.scope.kind != QA_APPLICATION_CONSOLE_Q3_GAME) continue;
        command.context.session = source.command.session;
        command.context.cvar_view = source.command.cvar_view;
        game_view = true;
        break;
    }
    if (!game_view) return QA_COMMAND_UNHANDLED;
    if (!qa_application_capture_command_context(application, &command.context,
                                                  &command.context, error))
        return QA_COMMAND_FAILED;
    if (!application->match_intents)
        application->match_intents = application_match_intents_create(error);
    return application->match_intents &&
        application_match_intents_request_restart(application->match_intents,
            application, mode, &command, error)
        ? QA_COMMAND_HANDLED : QA_COMMAND_FAILED;
}

bool qa_application_command_context_active(const qa_application *application,
                                           const qa_command_context *context)
{
    if (application == NULL || context == NULL || application->session == NULL ||
        application->destroy_requested || application->state == QA_APPLICATION_FAULTED ||
        (!context->owner && application->engine_shutdown) ||
        context->registry != qa_actors_identity(qa_session_actors(application->session)) ||
        context->generation != application->command_generation)
        return false;
    if (context->owner != 0 && command_owner(application, context->owner) == NULL &&
        application_startup_flow_provider(application, context->owner) == NULL)
        return false;
    application_provider *physical = context->owner ? command_owner(application, context->owner) : NULL;
    if (!physical && context->owner)
        physical = application_startup_flow_provider(application, context->owner);
    if (!qa_console_context_bound(application->console, context)) return false;
    if (physical && physical->client_only_owned) {
        /* A standalone decoded CLIENT has its own observer namespace. Its
         * pending seat origin must never acquire the old local GAME actor. */
        if (context->actor.registry) return false;
    } else if (context->origin == QA_COMMAND_SEAT ||
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
    if (!next.owner && !next.cvar_view)
        next.cvar_view = qa_cvars_view_identity(application->cvars);
    if ((next.registry != 0 || next.generation != 0) &&
        !qa_application_command_context_active(application, &next))
        return application_fail(error, QA_ERROR_ARGUMENT, "command belongs to a retired publication");
    next.registry = qa_actors_identity(qa_session_actors(application->session));
    next.generation = application->command_generation;
    application_provider *physical = next.owner ? command_owner(application, next.owner) : NULL;
    if (!physical && next.owner)
        physical = application_startup_flow_provider(application, next.owner);
    if (!(physical && physical->client_only_owned) && next.actor.registry == 0 &&
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
    if (application != NULL && application->engine_shutdown && source && !source->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE command owner is physically detached");
    if (application != NULL && application->session == NULL) {
        *out = *source;
        return true;
    }
    return qa_application_capture_command_context(application, source, out, error);
}

bool application_command_active(void *opaque, const qa_command_context *context)
{
    if (context == NULL || (opaque && !context->owner && ((qa_application *)opaque)->engine_shutdown))
        return false;
    if (context->registry == 0 && context->generation == 0)
        return true;
    return qa_application_command_context_active(opaque, context);
}

qa_vfs *qa_application_provider_files(qa_application *application,qa_actor_owner owner)
{
    if(!application||!application->session||!owner||application->destroy_requested||
        application->state==QA_APPLICATION_FAULTED)return NULL;
    application_provider *provider=command_owner(application,owner);
    if(!provider)provider=application_startup_flow_provider(application,owner);
    return provider&&provider->launch?provider->launch->content:NULL;
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
        files = qa_application_provider_files(application,(qa_actor_owner)context->owner);
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

size_t qa_application_console_count(const qa_application *application)
{
    return application && application->console ? 1 : 0;
}

qa_console *qa_application_console_at(qa_application *application, size_t index,
                                       qa_actor_owner *owner)
{
    if (!application || !application->console || index) return NULL;
    if (owner) *owner = 0;
    return application->console;
}

bool qa_application_console_source_at(qa_application *application, size_t index,
    qa_application_startup_source *out, bool *present, qa_error *error)
{
    if (!application || !out || !present)
        return application_fail(error, QA_ERROR_ARGUMENT, "Console Source enumeration requires its application and outputs");
    *present = false;
    if (application->console) {
        if (!index) {
            qa_application_startup_source root = {
                .scope = {.kind = QA_APPLICATION_CONSOLE_ENGINE},
                .console = application->console, .cvars = application->cvars};
            if (!qa_console_context_read(root.console, &root.command, error)) return false;
            *out = root;
            *present = true;
            return true;
        }
        --index;
    }
    for (application_provider *provider = application->live_providers;
         provider; provider = provider->next_live) {
        if (!provider->attached || !provider->constructed || provider->close_pending) continue;
        for (size_t ordinal = 0;; ++ordinal) {
            qa_application_startup_source source;
            bool found;
            if (!application_provider_startup_source_at(provider, ordinal, &source, &found, error)) return false;
            if (!found) break;
            if (!index) {
                *out = source;
                *present = true;
                return true;
            }
            --index;
        }
    }
    return true;
}

bool qa_application_console_scope_read(const qa_application *application,
    const qa_console *console, qa_application_console_scope *out)
{
    if (!application || !console || !out || console != application->console)
        return false;
    *out = (qa_application_console_scope){.kind = QA_APPLICATION_CONSOLE_ENGINE};
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
            return application_native_q2_game_command(provider, command, handled, error);
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

static qa_command_result command_dispatch(qa_application *application,
    const qa_command_invocation *invocation, qa_error *error)
{
    qa_command_invocation command = *invocation;
    if (!qa_application_capture_command_context(application, &invocation->context,
                                                  &command.context, error))
        return QA_COMMAND_FAILED;
    application_snapshot_mutated(application);
    qa_command_result flight = application_native_engine_fly(application, invocation,
        &command.context, error);
    if (flight != QA_COMMAND_UNHANDLED) return flight;
    qa_command_result round = q3_round_command(application, &command, error);
    if (round != QA_COMMAND_UNHANDLED)
        return round;
    bool handled = false;
    if (!application_bots_catalog_console(application, &command, &handled, error))
        return QA_COMMAND_FAILED;
    if (handled) return QA_COMMAND_HANDLED;
    if (application->q3_campaign_command) {
        if (!application->q3_campaign_command(
                application->guest_context, application, &command,
                &handled, error))
            return QA_COMMAND_FAILED;
        if (handled) return QA_COMMAND_HANDLED;
    }
    qa_actor_id actor = command.context.actor;
    application_provider *game = application_world_provider(application, QA_ROLE_ENTITIES, "");
    qa_q1_chat_mode chat = qa_q1_chat_command_read(command.context.dialect, command.argv[0], true);
    if (game && game->kind == APPLICATION_PROVIDER_Q1 &&
        (command.context.dialect == QA_CONSOLE_Q1 || command.context.dialect == QA_CONSOLE_QW) &&
        (!command.context.owner || command.context.owner == game->owner) &&
        (actor.registry || (command.context.owner == game->owner &&
         command.context.origin == QA_COMMAND_SERVER)) &&
        chat != QA_Q1_CHAT_UNKNOWN &&
        (chat != QA_Q1_CHAT_TEAM || command.context.dialect == QA_CONSOLE_Q1 || actor.registry)) {
        return application_native_q1_chat(game, invocation, chat, error)
            ? QA_COMMAND_HANDLED : QA_COMMAND_FAILED;
    }
    uint32_t source_slot;
    if (!actor.registry && game && game->kind == APPLICATION_PROVIDER_Q3 &&
        command.context.dialect == QA_CONSOLE_Q3 &&
        (!command.context.owner || command.context.owner == game->owner)) {
        if (!application_native_q3_postgame_console(game, &command, &handled, error))
            return QA_COMMAND_FAILED;
        if (handled) return QA_COMMAND_HANDLED;
        if (!application_native_q3_ipfilters_console(game, &command, &handled, error))
            return QA_COMMAND_FAILED;
        if (handled) return QA_COMMAND_HANDLED;
    }
    if (actor.registry && game && game->kind == APPLICATION_PROVIDER_Q3 &&
        command.context.dialect == QA_CONSOLE_Q3 &&
        (!command.context.owner || command.context.owner == game->owner) &&
        qa_q3_native_client_slot(game->state.q3, actor, &source_slot, NULL)) {
        if (!application_native_q3_client_command(game, actor, &command, &handled, error))
            return QA_COMMAND_FAILED;
        if (handled) return QA_COMMAND_HANDLED;
    }
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
    if (!handled && provider && provider->kind == APPLICATION_PROVIDER_QC &&
        !application_qc_host_command(provider, invocation, &handled, error))
        return QA_COMMAND_FAILED;
    return handled ? QA_COMMAND_HANDLED : QA_COMMAND_UNHANDLED;
}

qa_command_result application_command_fallback(void *opaque,
    const qa_command_invocation *invocation, qa_error *error)
{
    if (!invocation || !invocation->argc || !invocation->argv)
        return QA_COMMAND_UNHANDLED;
    qa_application *application = opaque;
    struct application_native_q1_console *source = NULL;
    if (!application_native_q1_console_engine_borrow(application, invocation, &source, error))
        return QA_COMMAND_FAILED;
    qa_command_result result = command_dispatch(application, invocation, error);
    application_native_q1_console_engine_release(source);
    return result;
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
