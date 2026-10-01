#include "chat.h"
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
    qa_console_dialect dialect = QA_CONSOLE_Q3;
    qa_command_context context = {.seat = seat->id, .origin = QA_COMMAND_SEAT, .direct = true};
    if (remote) {
        if (seat->id != 0 || frontend->options.seats != 1 || !frontend_network_client_ready(frontend))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Chat requires the admitted remote Q3 client seat");
    } else {
        qa_application_visual_view visual;
        if (!frontend_seat_launch_id_read(frontend,seat->id,&context.seat) ||
            !qa_application_player_actor(frontend->application, context.seat, &context.actor))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Chat requires the current admitted player actor");
        if (!qa_application_visual_read(frontend->application, context.actor, &visual, error)) return false;
        const qa_product *character = qa_catalog_product(qa_application_catalog(frontend->application), visual.character_content);
        if (character && character->family == QA_GAME_Q1)
            dialect = character->edition == QA_EDITION_QUAKEWORLD ? QA_CONSOLE_QW : QA_CONSOLE_Q1;
        else if (character && character->family == QA_GAME_Q2)
            dialect = character->edition == QA_EDITION_RERELEASE ? QA_CONSOLE_Q2_RERELEASE : QA_CONSOLE_Q2;
        context.dialect = dialect;
        if (!qa_application_capture_command_context(frontend->application, &context, &context, error)) return false;
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
    const char *argv[] = {name, targeted ? target_text : text, text};
    qa_command_invocation command = {.console = qa_application_console(frontend->application),
        .context = context, .argc = targeted ? 3 : 2, .argv = argv,
        .args_text = raw + strlen(name) + 1, .raw = raw};
    /* This is one source invocation. Engine separators in chat text are never
     * replayed through the local engine command buffer. Source GAME owns the
     * audience, team restrictions, flood control and delivered notification. */
    bool ok = remote ? frontend_network_client_command_seat(frontend, seat->id, raw, error) :
        qa_application_source_command(frontend->application, &command, error);
    free(raw); return ok;
}
