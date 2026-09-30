#include "guest_q3_private.h"
#include "guest_native_q2_private.h"
#include "guest_projection_private.h"
#include "guest_qc_internal.h"

bool application_guest_console_at(application_provider *provider, size_t index,
                                    qa_console **console, qa_cvars **cvars,
                                    qa_command_context *context)
{
    if (!provider || !provider->constructed || !console) return false;
    if (provider->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = provider->state.qc.engine;
        if (index || !engine || !engine->console) return false;
        *console = engine->console;
        if (cvars) *cvars = engine->cvars;
        if (context) *context = engine->command_context;
        return true;
    }
    if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine) {
        struct application_native_q2 *engine = provider->state.native.q2_engine;
        if (index || !engine->console) return false;
        *console = engine->console;
        if (cvars) *cvars = engine->cvars;
        if (context) *context = engine->command_context;
        return true;
    }
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine) return false;
    for (q3g_role *role = engine->roles; role; role = role->next) {
        qa_console *current = qa_q3_host_console(role->host, NULL, NULL);
        if (!current) continue;
        bool duplicate = false;
        for (q3g_role *prior = engine->roles; prior != role; prior = prior->next)
            if (qa_q3_host_console(prior->host, NULL, NULL) == current) { duplicate = true; break; }
        if (duplicate) continue;
        if (index) { --index; continue; }
        *console = qa_q3_host_console(role->host, cvars, context);
        return true;
    }
    return false;
}

bool application_guest_console_scope(application_provider *provider,
    const qa_console *console, qa_application_console_scope *out)
{
    if (!provider || !provider->constructed || !console || !out)
        return false;
    qa_application_console_scope scope = {.provider = provider->owner};
    if (provider->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = provider->state.qc.engine;
        if (!engine || engine->console != console)
            return false;
        scope.kind = QA_APPLICATION_CONSOLE_QC;
    } else if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine) {
        if (provider->state.native.q2_engine->console != console)
            return false;
        scope.kind = QA_APPLICATION_CONSOLE_NATIVE_Q2;
    } else {
        struct application_q3_guest *engine = q3g_engine(provider);
        q3g_role *selected = NULL;
        if (engine)
            for (q3g_role *role = engine->roles; role; role = role->next)
                if (qa_q3_host_console(role->host, NULL, NULL) == console &&
                    (!selected || role->kind < selected->kind ||
                     (role->kind == selected->kind && role->seat < selected->seat)))
                    selected = role;
        if (!selected)
            return false;
        scope.kind = selected->kind == QA_QVM_GAME ? QA_APPLICATION_CONSOLE_Q3_GAME
            : selected->kind == QA_QVM_CGAME ? QA_APPLICATION_CONSOLE_Q3_CGAME
            : QA_APPLICATION_CONSOLE_Q3_UI;
        scope.seat = selected->kind == QA_QVM_GAME ? 0 : selected->seat;
    }
    *out = scope;
    return true;
}

static q3g_role *find_role(application_provider *provider, qa_qvm_role kind,
                            uint32_t seat, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (engine) for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->kind == kind && (kind == QA_QVM_GAME || role->seat == seat)) return role;
    application_fail(error, QA_ERROR_NOT_FOUND, "Q3 guest role is not installed for this seat");
    return NULL;
}

bool application_q3_guest_role_add(application_provider *provider, qa_qvm_role kind,
                                     uint32_t seat, const char *path, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || engine->calls || kind == QA_QVM_GAME || kind > QA_QVM_UI || !path)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 companion creation requires an idle owner and client role");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->kind == kind && role->seat == seat)
            return (role->ready && !role->retired && !strcmp(role->path, path)) || application_fail(error, QA_ERROR_ARGUMENT, "Q3 role seat already has another artifact");
    q3g_role *role = NULL;
    if (!q3g_role_create(engine, kind, seat, path, false, &role, error)) return false;
    role->next = engine->roles; engine->roles = role; return true;
}

bool application_q3_guest_role_initialize(application_provider *provider, qa_qvm_role kind,
                                            uint32_t seat, int32_t server_message,
                                            int32_t server_command, int32_t client_number,
                                            bool connecting, qa_error *error)
{
    q3g_role *role = find_role(provider, kind, seat, error);
    if (!role) return false;
    if (kind == QA_QVM_GAME || role->engine->calls || role->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client initialization requires an idle fresh role");
    if (!q3g_role_activate(role, error)) return false;
    int32_t result;
    if (kind == QA_QVM_UI) {
        if (role->vm) {
            ++role->engine->calls;
            bool ok = qa_qvm_validate_ui(role->vm, &result, error);
            --role->engine->calls;
            if (!ok) return false;
        } else if (!q3g_call(role, 0, NULL, 0, &result, error)) return false;
        if (result != 4 && !(role->abi == QA_QVM_Q3_MODERN && result == 6))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 UI returned an unsupported API version");
        int32_t argument = connecting ? 1 : 0;
        if (!q3g_call(role, 1, &argument, 1, &result, error)) return false;
    } else {
        if (server_message < 0 || server_command < 0 || client_number < 0 || client_number >= 64 ||
            !role->client_services.gamestate || !role->client_services.current_snapshot)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 cgame requires admitted client and gamestate services");
        int32_t message, time;
        ++role->engine->calls;
        bool ready = role->client_services.current_snapshot(role->client_services.context, &message, &time, error);
        const qa_q3_gamestate *state = ready ? role->client_services.gamestate(role->client_services.context) : NULL;
        bool matches = state && message == server_message && server_command == state->command_sequence &&
            client_number == state->client_number;
        --role->engine->calls;
        if (!ready) return false;
        if (!matches)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 cgame initialization differs from the active client gamestate");
        if (role->client < 64 && !role->engine->clients[role->client].connected)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 local cgame has no connected source client");
        if (role->client < 64 && role->client != (uint32_t)client_number)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 cgame source client ordinal differs from its seat");
        if (role->local_client && !q3g_client_effect(role, QA_APPLICATION_Q3_SYSTEM_INFO,
                qa_q3_configstring(state, 1), error)) return false;
        int32_t arguments[] = {server_message, server_command, client_number};
        if (!q3g_call(role, 0, arguments, 3, &result, error)) return false;
    }
    role->initialized = true; return true;
}

bool application_q3_guest_role_call(application_provider *provider, qa_qvm_role kind,
                                      uint32_t seat, int32_t command, const int32_t *arguments,
                                      size_t count, int32_t *result, qa_error *error)
{
    q3g_role *role = find_role(provider, kind, seat, error);
    if (!role) return false;
    int32_t first = kind == QA_QVM_UI ? 3 : kind == QA_QVM_CGAME ? 2 : 8;
    int32_t last = kind == QA_QVM_UI ? 10 : kind == QA_QVM_CGAME ? 8 : 10;
    if (!role->initialized || command < first || command > last)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 role export is unavailable in its current lifecycle/profile");
    return q3g_call(role, command, arguments, count, result, error);
}

bool application_q3_guest_console_command(application_provider *provider, const char *text,
                                            bool *handled, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !engine->game || !engine->game->initialized || !text || !handled)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 console command requires initialized game");
    qa_command_tokens next = {0};
    if (!qa_command_tokenize(text, QA_CONSOLE_Q3, false, &next, error)) return false;
    qa_command_tokens prior = engine->arguments; engine->arguments = next;
    int32_t result;
    bool ok = q3g_call(engine->game, 9, NULL, 0, &result, error);
    qa_command_tokens_free(&engine->arguments); engine->arguments = prior;
    if (ok) *handled = result != 0;
    if (ok) ok = application_guest_bots_admit(provider, error);
    if (ok) ok = application_guest_clients_drain(provider, error);
    return ok;
}

bool application_q3_guest_role_command(application_provider *provider, qa_qvm_role kind,
                                         uint32_t seat, int32_t time, const char *text,
                                         bool *handled, qa_error *error)
{
    if (kind == QA_QVM_GAME) return application_q3_guest_console_command(provider, text, handled, error);
    q3g_role *role = find_role(provider, kind, seat, error);
    if (!role) return false;
    if (!text || !handled || !role->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 console export requires initialized client role");
    qa_command_tokens next = {0};
    if (!qa_command_tokenize(text, QA_CONSOLE_Q3, false, &next, error)) return false;
    qa_command_tokens prior = role->arguments; role->arguments = next;
    bool prior_scope = role->arguments_scoped; role->arguments_scoped = true;
    int32_t result;
    bool ok = q3g_call(role, kind == QA_QVM_UI ? 8 : 2,
                       kind == QA_QVM_UI ? &time : NULL, kind == QA_QVM_UI ? 1 : 0,
                       &result, error);
    qa_command_tokens_free(&role->arguments); role->arguments = prior;
    role->arguments_scoped = prior_scope;
    if (ok) *handled = result != 0;
    return ok;
}
