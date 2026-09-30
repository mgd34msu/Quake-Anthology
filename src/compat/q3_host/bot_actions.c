#include "internal.h"
#include "qa/bot_runtime.h"

static uint32_t legacy_flags(uint32_t flags)
{
    static const struct { uint32_t modern, source; } fields[] = {
        {QA_BOT_RESPAWN,4}, {QA_BOT_JUMP | QA_BOT_MOVE_UP,8},
        {QA_BOT_CROUCH | QA_BOT_MOVE_DOWN,16}, {QA_BOT_MOVE_FORWARD,32},
        {QA_BOT_MOVE_BACK,64}, {QA_BOT_MOVE_LEFT,128}, {QA_BOT_MOVE_RIGHT,256},
        {QA_BOT_DELAYED_JUMP,512}, {QA_BOT_TALK,1024}, {QA_BOT_GESTURE,2048}, {QA_BOT_WALK,4096}
    };
    uint32_t source = flags & 3u;
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        if (flags & fields[i].modern) source |= fields[i].source;
    return source;
}

static void store_float(uint8_t *out, float number)
{
    uint32_t bits; memcpy(&bits, &number, sizeof(bits)); qa_store_u32le(out, bits);
}

q3_service_result q3_bot_actions(q3_call *call, int32_t *result, qa_error *error)
{
    (void)result;
    if (call->host->options.role != QA_QVM_GAME) return Q3_UNHANDLED;
    bool legacy = call->host->options.abi == QA_QVM_Q3_116N;
    bool extension = legacy && call->source_service >= 402 && call->source_service <= 405;
    int32_t operation = call->service;
    if (!extension && (operation < 400 || operation > 423)) return Q3_UNHANDLED;
    qa_bot_actions *actions = qa_bot_runtime_actions(q3_bot_runtime(call));
    if (!actions) {
        q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 elementary action owner is unbound");
        return Q3_FAILED;
    }
    int32_t client;
    if(!q3_bot_client_number(call,q3_integer(call,0),&client,error)) return Q3_FAILED;
    bool ok;
    if (extension || operation <= 402) {
        qa_buffer text = {0};
        qa_bot_text_action kind = extension ? (qa_bot_text_action)(QA_BOT_USE_ITEM + call->source_service - 402) :
                                     operation == 400 ? QA_BOT_SAY : operation == 401 ? QA_BOT_SAY_TEAM : QA_BOT_COMMAND;
        ok = q3_string(call, call->arguments[1], &text, error) &&
             qa_bot_actions_text(actions, client, kind, 0, (const char *)text.data, error);
        qa_buffer_free(&text);
    } else if (operation >= 403 && operation <= 415) {
        static const uint32_t flags[] = {0,QA_BOT_GESTURE,QA_BOT_TALK,QA_BOT_ATTACK,
            QA_BOT_USE,QA_BOT_RESPAWN,QA_BOT_CROUCH,QA_BOT_MOVE_UP,QA_BOT_MOVE_DOWN,
            QA_BOT_MOVE_FORWARD,QA_BOT_MOVE_BACK,QA_BOT_MOVE_LEFT,QA_BOT_MOVE_RIGHT};
        uint32_t add = operation == 403 ? (uint32_t)call->arguments[1] : flags[operation - 403];
        ok = qa_bot_actions_add(actions, (uint32_t)client, add, error);
    } else if (operation == 416) {
        ok = qa_bot_actions_weapon(actions, (uint32_t)client, q3_integer(call, 1), error);
    } else if (operation == 417 || operation == 418) {
        ok = qa_bot_actions_jump(actions, (uint32_t)client, operation == 418, error);
    } else if (operation == 419 || operation == 420) {
        qa_vec3 vector;
        ok = q3_vector(call, call->arguments[1], &vector, error);
        if (ok) ok = operation == 419 ? qa_bot_actions_move(actions, (uint32_t)client, vector, q3_float(call, 2), error) :
                                         qa_bot_actions_view(actions, (uint32_t)client, vector, error);
    } else if (operation == 421) {
        qa_bot_actions_end_regular(actions, client, q3_float(call, 1)); ok = true;
    } else if (operation == 422) {
        qa_bot_input input;
        q3_record admitted;
        ok = (legacy || q3_record_open(call, call->arguments[2], 40, &admitted, error)) &&
             qa_bot_actions_input(actions, (uint32_t)client, q3_float(call, 1), &input, error);
        if (ok) {
            uint8_t bytes[40];
            store_float(bytes, input.think_time);
            store_float(bytes + 4, input.direction.x); store_float(bytes + 8, input.direction.y);
            store_float(bytes + 12, input.direction.z); store_float(bytes + 16, input.speed);
            store_float(bytes + 20, input.view_angles.x); store_float(bytes + 24, input.view_angles.y);
            store_float(bytes + 28, input.view_angles.z);
            qa_store_u32le(bytes + 32, input.action_flags); qa_store_u32le(bytes + 36, (uint32_t)input.weapon);
            ok = q3_write(call, call->arguments[2], (qa_bytes){bytes, sizeof(bytes)}, error);
            if (ok && legacy) {
                uint8_t current[4];
                ok = q3_read(call, call->arguments[2] + 32, current, sizeof(current), error) &&
                     q3_write_word(call, call->arguments[2] + 32, legacy_flags(qa_load_u32le(current)), error);
            }
        }
    } else ok = qa_bot_actions_reset(actions, (uint32_t)client, error);
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
