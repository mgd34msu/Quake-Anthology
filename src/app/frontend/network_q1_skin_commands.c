#include "network_q1_skin_commands.h"
#include "remote_q1_private.h"
#include "remote_q1_skins.h"
#include "internal.h"

static bool named(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char left = (unsigned char)*a++, right = (unsigned char)*b++;
        if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
        if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
        if (left != right) return false;
    }
    return *a == *b;
}
static bool invocation_current(frontend_remote_q1 *row, const qa_command_invocation *call,
    qa_error *error)
{
    qa_command_context expected;
    if (!remote_q1_mutable(row) || row->busy || !remote_q1_live(row, error) ||
        call->console != row->options.domain.console ||
        !qa_application_capture_command_context(row->options.domain.application,
            &row->options.domain.command_context, &expected, error)) return false;
    const qa_command_context *actual = &call->context;
    return actual->session == expected.session && actual->owner == expected.owner &&
        actual->client == expected.client && actual->seat == expected.seat &&
        actual->dialect == expected.dialect && actual->registry == expected.registry &&
        actual->generation == expected.generation && qa_actor_id_equal(actual->actor, expected.actor) &&
        qa_application_command_context_active(row->options.domain.application, actual);
}
qa_command_result frontend_network_q1_skin_command(frontend_remote_q1 *row,
    const qa_command_invocation *call, qa_error *error)
{
    if (!call || !call->argc || !call->argv || !call->argv[0]) return QA_COMMAND_UNHANDLED;
    bool skins = named(call->argv[0], "skins"), all = named(call->argv[0], "allskins");
    bool cancel = named(call->argv[0], "stopdownload"), retry = named(call->argv[0], "retrydownload");
    if ((!skins && !all && !cancel && !retry) || !row ||
        !qa_q1_is_qw(row->options.domain.protocol)) return QA_COMMAND_UNHANDLED;
    if (!row->skins || !invocation_current(row, call, error) ||
        !frontend_remote_q1_skins_resume(row->skins, error)) return QA_COMMAND_FAILED;
    bool ready = false, ok;
    if (all) {
        if (call->argc > 1 && !call->argv[1]) return QA_COMMAND_FAILED;
        ok = frontend_remote_q1_skins_all(row->skins, call->argc > 1 ? call->argv[1] : "", &ready, error);
    } else if (skins) ok = frontend_remote_q1_skins_refresh(row->skins, &ready, error);
    else if (retry) ok = frontend_remote_q1_skins_retry(row->skins, &ready, error);
    else {
        ok = frontend_remote_q1_skins_cancel(row->skins, error);
        if (ok && invocation_current(row, call, error))
            frontend_console_print(row->frontend, &call->context, "Downloads paused. Use retrydownload to resume.\n");
        else ok = false;
    }
    if (ok) ok = invocation_current(row, call, error);
    if (ok && ready) {
        qa_network_q1_client_state source;
        ok = qa_network_q1_client_state_read(row->options.domain.runtime,
            row->options.domain.client, &source, error) &&
            (!source.waiting_skins || qa_network_q1_client_skins_ready(row->options.domain.runtime,
                row->options.domain.client, error));
        if (ok) ok = invocation_current(row, call, error);
    }
    return ok ? QA_COMMAND_HANDLED : QA_COMMAND_FAILED;
}
