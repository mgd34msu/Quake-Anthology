#include "source_command.h"
#include "source_alias.h"

static bool bytes(qa_bots *bots, bot_ai_state *state, uint8_t **out, qa_error *error) {
    if (!state || (!state->source_span.data && !bot_ai_source_alias_bind(bots, state, error)))
        return false;
    if (state->source_span.length != QA_BOT_STATE_SOURCE_BYTES)
        return bot_ai_fail(error, "User command requires its complete actual BotState allocation");
    *out = state->source_span.data + QA_BOT_SOURCE_COMMAND;
    return true;
}

bool bot_ai_source_command_read(qa_bots *bots, bot_ai_state *state,
                                 qa_movement_command *out, qa_error *error) {
    uint8_t *command;
    if (!out || !bytes(bots, state, &command, error)) return false;
    *out = (qa_movement_command){.kind = QA_MOVEMENT_Q3,
        .server_time_ms = bot_source_i32_read(command),
        .buttons = bot_source_word_read(command + 16), .weapon = command[20]};
    for (uint32_t axis = 0; axis < 3; ++axis)
        out->angle_words[axis] = bot_source_i32_read(command + 4 + axis * 4);
    int8_t forward, right, up;
    memcpy(&forward, command + 21, 1); memcpy(&right, command + 22, 1); memcpy(&up, command + 23, 1);
    out->forward_move = forward; out->side_move = right; out->up_move = up;
    return true;
}

bool bot_ai_source_command_write(qa_bots *bots, bot_ai_state *state,
                                  const qa_movement_command *value, qa_error *error) {
    uint8_t *command;
    if (!value || !bytes(bots, state, &command, error)) return false;
    bot_source_i32_write(command, value->server_time_ms);
    bot_source_word_write(command + 16, value->buttons);
    command[20] = value->weapon;
    for (uint32_t axis = 0; axis < 3; ++axis)
        bot_source_i32_write(command + 4 + axis * 4, value->angle_words[axis]);
    command[21] = (uint8_t)(int32_t)value->forward_move;
    command[22] = (uint8_t)(int32_t)value->side_move;
    command[23] = (uint8_t)(int32_t)value->up_move;
    return true;
}

bool bot_ai_source_command_pause(qa_bots *bots, bot_ai_state *state, int32_t time, qa_error *error) {
    uint8_t *command;
    if (!bytes(bots, state, &command, error)) return false;
    command[21] = command[22] = command[23] = 0;
    bot_source_word_write(command + 16, 0);
    bot_source_i32_write(command, time);
    return true;
}
