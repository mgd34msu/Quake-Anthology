#include "internal.h"

static bool client_index(const qa_q3_host_server_services *services, int32_t index,
                           qa_error *error)
{
    return (index >= 0 && (uint32_t)index < services->maximum_clients) ||
           q3_fail(error, QA_ERROR_ARGUMENT, (size_t)(uint32_t)index, "invalid Q3 client index");
}

static bool config_index(q3_call *call, int32_t input, int32_t *index, qa_error *error)
{
    if (input < 0 || input >= 1024)
        return q3_fail(error, QA_ERROR_ARGUMENT, (size_t)(uint32_t)input,
                        "invalid Q3 configstring index");
    return qa_qvm_configstring_tag(call->host->options.abi, input, index, error);
}

q3_service_result q3_information(q3_call *call, int32_t *result, qa_error *error)
{
    (void)result;
    if (call->host->options.role != QA_QVM_GAME ||
        !((call->service >= 16 && call->service <= 22) || call->service == 36))
        return Q3_UNHANDLED;
    const qa_q3_host_server_services *services = &call->host->options.server;
    int32_t index = q3_integer(call, 0), trap = call->service;
    const char *value = NULL;
    qa_buffer text = {0};
    bool ok = false;
    if ((trap == 16 || trap == 17) &&
        !(index == -1 && trap == 17) &&
        (index < 0 || (uint32_t)index >= services->maximum_clients)) return Q3_COMPLETED;
    if (trap == 16 || trap == 17) {
        ok = q3_string(call, call->arguments[1], &text, error);
        if (ok && trap == 16 && services->drop_client)
            ok = services->drop_client(services->context, (uint32_t)index,
                                         (const char *)text.data, error);
        else if (ok && trap == 17 && services->send_command)
            ok = services->send_command(services->context, index, (const char *)text.data, error);
        else if (ok) ok = q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 server transport is unbound");
    } else if (trap == 18) {
        ok = config_index(call, index, &index, error) &&
             (!call->arguments[1] || q3_string(call, call->arguments[1], &text, error));
        if (ok) ok = services->set_configstring ?
            services->set_configstring(services->context, (uint32_t)index,
                                         text.data ? (const char *)text.data : "", error) :
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 configstring owner is unbound");
    } else if (trap == 19 || trap == 20) {
        int32_t capacity = q3_integer(call, 2);
        ok = capacity > 0 || q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 information buffer is empty");
        if (ok) ok = trap == 19 ? config_index(call, index, &index, error) :
                                  client_index(services, index, error);
        if (ok && trap == 19 && services->configstring)
            ok = services->configstring(services->context, (uint32_t)index, &value, error);
        else if (ok && trap == 20 && services->userinfo)
            ok = services->userinfo(services->context, (uint32_t)index, &value, error);
        else if (ok) ok = q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 information owner is unbound");
        if (ok) ok = q3_write_string(call, call->arguments[1], value ? value : "", capacity, error);
    } else if (trap == 21) {
        ok = client_index(services, index, error) &&
             (!call->arguments[1] || q3_string(call, call->arguments[1], &text, error));
        if (ok) ok = services->set_userinfo ?
            services->set_userinfo(services->context, (uint32_t)index,
                                     text.data ? (const char *)text.data : "", error) :
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 userinfo owner is unbound");
    } else if (trap == 22) {
        int32_t capacity = q3_integer(call, 1);
        ok = capacity > 0 || q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 serverinfo buffer is empty");
        if (ok) ok = call->host->options.cvars ?
            qa_cvars_info(call->host->options.cvars, QA_CVAR_SERVERINFO, 8192, &text, error) :
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 cvar owner is unbound");
        if (ok) ok = q3_write_string(call, call->arguments[0], (const char *)text.data, capacity, error);
    } else {
        if (call->native && call->native_profile == QA_NATIVE_QUAKE_LIVE_GAME_API10) {
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Quake Live user-command layout is not established");
            return Q3_FAILED;
        }
        qa_q3_usercmd command;
        q3_record record;
        ok = client_index(services, index, error);
        if (ok) ok = services->user_command ?
            services->user_command(services->context, (uint32_t)index, &command, error) :
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 user-command owner is unbound");
        if (ok) ok = q3_record_open(call, call->arguments[1], 24, &record, error) &&
                     qa_q3_abi_write_usercmd(&record.abi, 0, false, &command, error);
    }
    qa_buffer_free(&text);
    return ok ? Q3_COMPLETED : Q3_FAILED;
}

q3_service_result q3_client_state(q3_call *call, int32_t *result, qa_error *error)
{
    qa_qvm_role role = call->host->options.role;
    int32_t trap = call->service;
    if (role == QA_QVM_GAME ||
        (role == QA_QVM_UI ? trap != 45 : !(trap >= 50 && trap <= 56))) return Q3_UNHANDLED;
    const qa_q3_host_client_services *services = &call->host->options.client;
    bool ok = false;
    q3_record record;
    if (role == QA_QVM_UI || trap == 50) {
        const qa_q3_gamestate *state = services->gamestate ? services->gamestate(services->context) : NULL;
        if (!state) {
            if (role != QA_QVM_UI || !services->gamestate || !services->configstring_absent) {
                q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 client gamestate is unbound");
                return Q3_FAILED;
            }
            if (!services->configstring_absent(services->context, error)) return Q3_FAILED;
        }
        if (role == QA_QVM_UI) {
            int32_t index = q3_integer(call, 0);
            if (index < 0 || index >= 1024) return Q3_COMPLETED;
            if (!config_index(call, index, &index, error)) return Q3_FAILED;
            const char *text = state ? qa_q3_configstring(state, (uint32_t)index) : NULL;
            if (!text || !*text) {
                *result = 0;
                const uint8_t zero = 0;
                ok = !q3_integer(call, 2) || q3_write(call, call->arguments[1], (qa_bytes){&zero, 1}, error);
            } else {
                ok = q3_write_string(call, call->arguments[1], text, q3_integer(call, 2), error);
                *result = ok;
            }
        } else {
            qa_q3_gamestate *copy = qa_arena_alloc(&call->host->scratch, sizeof(*copy), _Alignof(qa_q3_gamestate), error);
            if (!copy) return Q3_FAILED;
            memcpy(copy->config_offsets, state->config_offsets, sizeof(copy->config_offsets));
            memcpy(copy->strings, state->strings, sizeof(copy->strings));
            copy->string_bytes = state->string_bytes;
            ok = q3_record_open(call, call->arguments[0], 20100, &record, error) &&
                 qa_q3_abi_write_gamestate(&record.abi, 0, false, copy, error);
        }
    } else if (trap == 51 && services->current_snapshot) {
        int32_t number, time;
        uint8_t admitted[4];
        ok = q3_read(call, call->arguments[0], admitted, sizeof(admitted), error) &&
             q3_read(call, call->arguments[1], admitted, sizeof(admitted), error) &&
             services->current_snapshot(services->context, &number, &time, error) &&
             q3_write_word(call, call->arguments[0], (uint32_t)number, error) &&
             q3_write_word(call, call->arguments[1], (uint32_t)time, error);
    } else if (trap == 52 && services->snapshot) {
        const qa_q3_snapshot *snapshot = NULL;
        int32_t ping;
        ok = services->snapshot(services->context, q3_integer(call, 0), &snapshot, &ping, error);
        if (ok && snapshot) {
            qa_q3_snapshot copy = *snapshot;
            if (copy.entity_count > 256 || (copy.entity_count && !copy.entities)) {
                q3_fail(error, QA_ERROR_FORMAT, 0, "invalid Q3 retained snapshot");
                return Q3_FAILED;
            }
            if (copy.entity_count) {
                size_t size = copy.entity_count * sizeof(*copy.entities);
                qa_q3_entity *entities = qa_arena_alloc(&call->host->scratch, size, _Alignof(qa_q3_entity), error);
                if (!entities) return Q3_FAILED;
                memcpy(entities, copy.entities, size); copy.entities = entities;
            }
            ok = q3_record_open(call, call->arguments[1], qa_qvm_snapshot_bytes(call->host->options.abi), &record, error) &&
                 qa_q3_abi_write_snapshot(&record.abi, 0, false, &copy, ping, error);
            *result = ok;
        }
    } else if (trap == 53 && services->server_command) {
        bool present;
        ok = services->server_command(services->context, q3_integer(call, 0), &present, error);
        if (ok) *result = present;
    } else if (trap == 54 && services->current_command) {
        *result = services->current_command(services->context); ok = true;
    } else if (trap == 55 && services->user_command) {
        qa_q3_usercmd command; bool present;
        ok = services->user_command(services->context, q3_integer(call, 0), &command, &present, error);
        if (ok && present) {
            ok = q3_record_open(call, call->arguments[1], 24, &record, error) &&
                 qa_q3_abi_write_usercmd(&record.abi, 0, false, &command, error);
            *result = ok;
        }
    } else if (trap == 56 && services->command_values) {
        ok = services->command_values(services->context, q3_integer(call, 0), q3_float(call, 1), error);
    } else ok = q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 connected-client service is unbound");
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
