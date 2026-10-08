#include "chat.h"
#include "network_recipient.h"
#include "qa/application_startup_prepare.h"
#include <stdio.h>

bool frontend_chat_send(frontend_seat *seat, const char *text, bool team,
    bool targeted, int32_t target, qa_error *error)
{
    if (!seat || !seat->frontend || !text || seat->frontend->options.dedicated ||
        seat->id >= seat->frontend->options.seats || seat != &seat->frontend->seats[seat->id] ||
        (targeted && target < 0))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Chat requires its admitted physical seat");
    qa_frontend *frontend = seat->frontend;
    bool remote = frontend_network_remote(frontend);
    frontend_network_client_recipient recipient;
    bool client_source = false;
    if (!remote && !frontend_network_client_recipient_read(frontend,seat->id,
        &recipient,&client_source,error)) return false;
    qa_console_dialect dialect = QA_CONSOLE_Q3;
    qa_console *source_console = qa_application_console(frontend->application);
    qa_command_context context = {.seat = seat->id, .origin = QA_COMMAND_SEAT, .direct = true};
    if (client_source) {
        const qa_net_client *client=qa_net_connections_get(qa_network_connections(recipient.source.runtime),
            recipient.source.client);
        if (!recipient.ready || !client || client->phase!=QA_NET_ACTIVE ||
            !frontend_network_client_recipient_current(frontend,seat->id,&recipient))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Chat requires its current active CLIENT recipient");
        context=recipient.source.context.command;
        dialect=context.dialect;
    } else if (remote) {
        if (seat->id != 0 || frontend->options.seats != 1 || !frontend_network_client_ready(frontend))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Chat requires the admitted remote Q3 client seat");
    } else {
        if (!frontend_seat_launch_id_read(frontend,seat->id,&context.seat) ||
            !qa_application_player_actor(frontend->application, context.seat, &context.actor))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Chat requires the current admitted player actor");
        context.dialect=qa_seat_console_context_read(seat->console).dialect;
        if (targeted) {
            qa_actor_owner owner;
            const qa_cvars *source_variables;
            if (!qa_application_control_source_read(frontend->application,context.actor,
                &owner,&source_variables,error)) return false;
            const qa_launch_snapshot *publication=qa_application_launch(frontend->application);
            const char *instance=qa_application_provider_instance(frontend->application,owner);
            const qa_launch_instance *source=instance?qa_launch_snapshot_find(publication,instance):NULL;
            if (!source)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Chat lost its current GAME command recipient");
            qa_application_startup_source game;
            if (!qa_application_startup_source_read(frontend->application,publication,
                source,&game,error)) return false;
            dialect=game.command.dialect;
        }
    }
    if (targeted && dialect != QA_CONSOLE_Q3)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Selected source has no numeric client tell command");
    const char *name = targeted ? "tell" : team ? "say_team" : "say";
    char target_text[16];
    snprintf(target_text, sizeof(target_text), "%d", target);
    size_t prefix = strlen(name) + 1 + (targeted ? strlen(target_text) + 1 : 0), size = strlen(text);
    if (size > SIZE_MAX - prefix - 1)
        return frontend_fail(error, QA_ERROR_MEMORY, "Chat command size overflow");
    char *raw = malloc(prefix + size + 1);
    if (!raw) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining source chat command");
    if (targeted) snprintf(raw, prefix + size + 1, "%s %s %s", name, target_text, text);
    else snprintf(raw, prefix + size + 1, "%s %s", name, text);
    /* This is one source invocation. Engine separators in chat text are never
     * replayed through the local engine command buffer. Source GAME owns the
     * audience, team restrictions, flood control and delivered notification. */
    bool ok = remote ? frontend_network_client_command_seat(frontend, seat->id, raw, error) :
        qa_console_execute_now(client_source ? recipient.source.context.console : source_console,
            &context, raw, error);
    if (ok && client_source && !frontend_network_client_recipient_current(frontend,seat->id,&recipient))
        ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Chat changed its actual CLIENT recipient");
    free(raw); return ok;
}
