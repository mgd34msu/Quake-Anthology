#include "internal.h"
#include "qa/bot_runtime.h"

#include <stdio.h>

static bool sources_shutdown(void *context,qa_error *error)
{
    return q3_script_namespace_shutdown(context,error);
}

static bool update(q3_call *call, qa_bot_runtime *runtime, qa_error *error)
{
    int32_t number;
    if(!q3_bot_entity_number(call,q3_integer(call,0),&number,error)) return false;
    if (!call->arguments[1])
        return qa_bot_runtime_update_entity(runtime, number, NULL, error);
    uint8_t bytes[112];
    if (!q3_read(call, call->arguments[1], bytes, sizeof(bytes), error)) return false;
    qa_bot_entity_update entity = {0};
    int32_t source=q3_integer(call,0);
    if(call->host->game && source>=0 && source<1022)
        entity.actor=call->host->game->slots[source].actor;
    entity.type = qa_load_i32le(bytes); entity.flags = qa_load_i32le(bytes + 4);
    qa_vec3 *vectors[] = {&entity.origin, &entity.angles, &entity.old_origin, &entity.mins, &entity.maxs};
    for (size_t i = 0; i < 5; ++i)
        *vectors[i] = qa_v3(qa_load_f32le(bytes + 8 + i * 12),
                            qa_load_f32le(bytes + 12 + i * 12), qa_load_f32le(bytes + 16 + i * 12));
    int32_t *words[] = {&entity.ground_entity, &entity.solid, &entity.model_index, &entity.model_index2,
        &entity.frame, &entity.event, &entity.event_parameter, &entity.powerups, &entity.weapon,
        &entity.legs_animation, &entity.torso_animation};
    for (size_t i = 0; i < 11; ++i) *words[i] = qa_load_i32le(bytes + 68 + i * 4);
    return qa_bot_runtime_update_entity(runtime, number, &entity, error);
}

static q3_service_result clients(q3_call *call, int32_t *result, qa_error *error)
{
    qa_q3_host_server_services *server = &call->host->options.server;
    int32_t client = q3_integer(call, 0);
    bool ok;
    switch (call->service) {
    case 34:
        if (!server->allocate_bot) break;
        return server->allocate_bot(server->context, result, error) ? Q3_COMPLETED : Q3_FAILED;
    case 35:
        if (!server->free_bot) break;
        return server->free_bot(server->context, client, error) ? Q3_COMPLETED : Q3_FAILED;
    case 209:
        if (!server->bot_snapshot_entity) break;
        return server->bot_snapshot_entity(server->context, client, q3_integer(call, 1), result, error) ? Q3_COMPLETED : Q3_FAILED;
    case 210: {
        if (!server->bot_console_message) break;
        const char *message;
        if (!server->bot_console_message(server->context, client, &message, error)) return Q3_FAILED;
        if (!message) return Q3_COMPLETED;
        if (!q3_write_string(call, call->arguments[1], message, q3_integer(call, 2), error)) return Q3_FAILED;
        *result = 1; return Q3_COMPLETED;
    }
    case 211: {
        if (!server->bot_user_command) break;
        if (call->native && call->native_profile == QA_NATIVE_QUAKE_LIVE_GAME_API10) {
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Quake Live user-command layout is not established");
            return Q3_FAILED;
        }
        qa_q3_usercmd command; q3_record record;
        ok = q3_record_open(call, call->arguments[1], 24, &record, error) &&
             qa_q3_abi_read_usercmd(&record.abi, 0, &command, error) &&
             server->bot_user_command(server->context, client, &command, error);
        return ok ? Q3_COMPLETED : Q3_FAILED;
    }
    default: return Q3_UNHANDLED;
    }
    q3_fail(error, QA_ERROR_UNSUPPORTED, (size_t)call->service, "Q3 bot client service is unbound");
    return Q3_FAILED;
}

static bool character(q3_call *call, qa_bot_runtime *runtime, int32_t *result, qa_error *error)
{
    uint32_t handle = (uint32_t)q3_integer(call, 0), index = (uint32_t)q3_integer(call, 1);
    float value;
    switch (call->service) {
    case 500: {
        qa_buffer path = {0};
        float skill = call->host->options.abi == QA_QVM_Q3_116N ? (float)q3_integer(call, 1) : q3_float(call, 1);
        bool ok = q3_string(call, call->arguments[0], &path, error) &&
            qa_bot_runtime_character_load(runtime, (const char *)path.data, skill, &handle, error);
        qa_buffer_free(&path);
        if (ok) *result = (int32_t)handle;
        return ok;
    }
    case 501: return qa_bot_runtime_character_free(runtime, handle, error);
    case 502:
        if (!qa_bot_runtime_character_float(runtime, handle, index, &value, error)) return false;
        *result = q3_float_bits(value); return true;
    case 503:
        if (!qa_bot_runtime_character_bounded_float(runtime, handle, index,
                                                    q3_float(call, 2), q3_float(call, 3), &value, error)) return false;
        *result = q3_float_bits(value); return true;
    case 504: return qa_bot_runtime_character_integer(runtime, handle, index, result, error);
    case 505: return qa_bot_runtime_character_bounded_integer(runtime, handle, index,
                                                               q3_integer(call, 2), q3_integer(call, 3), result, error);
    case 506: {
        const char *text; bool written;
        if (!qa_bot_runtime_character_string(runtime, handle, index, &text, &written, error)) return false;
        if (!written) text = "";
        return q3_write_string(call, call->arguments[2], text, q3_integer(call, 3), error);
    }
    default: return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 character service");
    }
}

q3_service_result q3_bot_library(q3_call *call, int32_t *result, qa_error *error)
{
    if (call->host->options.role != QA_QVM_GAME) return Q3_UNHANDLED;
    q3_service_result client_result = clients(call, result, error);
    if (client_result != Q3_UNHANDLED) return client_result;
    if (call->service >= 500 && call->service <= 506) {
        qa_bot_runtime *runtime = q3_bot_runtime(call);
        if (!runtime) { q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 bot runtime is unbound"); return Q3_FAILED; }
        return character(call, runtime, result, error) ? Q3_COMPLETED : Q3_FAILED;
    }
    /* The source's developer test import has an empty body. */
    if (call->service == 208) { *result = 0; return Q3_COMPLETED; }
    if (call->service < 200 || call->service > 207 || call->service == 204) return Q3_UNHANDLED;
    qa_bot_runtime *runtime = call->host->options.bots;
    qa_bot_library *library = qa_bot_runtime_library(runtime);
    if (!runtime) {
        if (call->service == 200) return Q3_COMPLETED;
        const char *message = call->service == 201 ? "BotLibShutdown: bot library used before being setup\n" :
            call->service == 205 ? "BotStartFrame: bot library used before being setup\n" :
            call->service == 206 ? "BotLoadMap: bot library used before being setup\n" :
            call->service == 207 ? "BotUpdateEntity: bot library used before being setup\n" : NULL;
        if (message) {
            if (call->host->options.common.print)
                call->host->options.common.print(call->host->options.common.context, message);
            *result = 1; return Q3_COMPLETED;
        }
        q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 bot resource library is unbound"); return Q3_FAILED;
    }
    if (call->service == 200) {
        if(call->host->options.shared_bot_lifetime && qa_bot_runtime_initialized(runtime)) {
            call->host->bots_shutdown=false;*result=0;return Q3_COMPLETED;
        }
        if(!qa_bot_runtime_setup(runtime,result,error)) return Q3_FAILED;
        if(!*result) call->host->bots_shutdown=false;
        return Q3_COMPLETED;
    }
    if (call->service == 201) {
        if (call->host->bots_shutdown || qa_bot_runtime_closed(runtime)) return Q3_COMPLETED;
        if (call->host->script_namespace->generation == UINT64_MAX) {
            q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 script lifetime counter exhausted"); return Q3_FAILED;
        }
        bool reported=call->host->options.shared_bot_lifetime?sources_shutdown(call->host,error):
            qa_bot_runtime_shutdown(runtime,error);
        if(!reported) return Q3_FAILED;
        call->host->bots_shutdown=true;
        return reported ? Q3_COMPLETED : Q3_FAILED;
    }
    if(call->host->bots_shutdown && call->service>=205) {
        *result=1;return Q3_COMPLETED;
    }
    if (call->service == 205)
        return qa_bot_runtime_start_frame(runtime, q3_float(call, 0), error) ? Q3_COMPLETED : Q3_FAILED;
    if (call->service == 207) return update(call, runtime, error) ? Q3_COMPLETED : Q3_FAILED;
    if ((call->service == 202 || call->service == 203) && !library) {
        q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 bot resource library is closed"); return Q3_FAILED;
    }
    qa_buffer name = {0}, value = {0};
    if (call->service == 206) {
        if (!qa_bot_runtime_lease_begin(runtime, error)) return Q3_FAILED;
        bool ok = q3_string(call, call->arguments[0], &name, error);
        qa_bot_runtime_lease_end(runtime);
        if (ok) ok = qa_bot_runtime_load_map(runtime, (const char *)name.data, error);
        qa_buffer_free(&name);
        return ok ? Q3_COMPLETED : Q3_FAILED;
    }
    bool ok = q3_string(call, call->arguments[0], &name, error);
    if (ok && call->service == 202)
        ok = q3_string(call, call->arguments[1], &value, error) &&
             qa_bot_library_variable_set(library, (const char *)name.data, (const char *)value.data, error);
    else if (ok) {
        const qa_bot_variable *variable = qa_bot_library_variable(library, (const char *)name.data);
        const char *text = variable ? variable->string : "";
        ok = q3_write_string(call, call->arguments[1], text, q3_integer(call, 2), error);
    }
    qa_buffer_free(&value); qa_buffer_free(&name);
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
