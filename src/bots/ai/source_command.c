#include "source_command.h"
#include "source_alias.h"
#include "source_player.h"
#include "source_view.h"
#include "qa/text.h"

static bool bytes(qa_bots *bots, bot_ai_state *state, uint8_t **out, qa_error *error) {
    if (!state || (!state->source_span.data && !bot_ai_source_alias_bind(bots, state, error)))
        return false;
    if (state->source_span.length != QA_BOT_STATE_SOURCE_BYTES) {
        bot_ai_fail(error, "Source command and action require their complete actual BotState allocation");
        return false;
    }
    *out = state->source_span.data;
    return true;
}

bool bot_ai_source_command_read(qa_bots *bots, bot_ai_state *state,
                                 qa_usercmd *out, qa_error *error) {
    uint8_t *command;
    if (!out || !bytes(bots, state, &command, error)) return false;
    command+=QA_BOT_SOURCE_COMMAND;
    qa_q3_usercmd raw = {.serverTime = bot_source_i32_read(command),
        .buttons = bot_source_i32_read(command + 16), .weapon = command[20]};
    for (uint32_t axis = 0; axis < 3; ++axis)
        raw.angles[axis] = bot_source_i32_read(command + 4 + axis * 4);
    memcpy(&raw.forwardmove, command + 21, 1);
    memcpy(&raw.rightmove, command + 22, 1);
    memcpy(&raw.upmove, command + 23, 1);
    qa_usercmd_from_q3(&raw, 0, out);
    return true;
}

bool bot_ai_source_command_write(qa_bots *bots, bot_ai_state *state,
                                  qa_usercmd *value, qa_error *error) {
    uint8_t *command;
    if (!value || !bytes(bots, state, &command, error)) return false;
    command+=QA_BOT_SOURCE_COMMAND;
    for (uint32_t axis = 0; axis < 3; ++axis)
        value->angle_words[axis] = qa_input_signed_word((uint32_t)value->angle_words[axis]);
    float *moves[] = {&value->forward_move, &value->side_move, &value->up_move};
    for (unsigned axis = 0; axis < 3; ++axis) {
        uint32_t byte = (uint32_t)qa_source_float_to_i32(*moves[axis]) & 255u;
        *moves[axis] = (float)(byte >= 128u ? (int32_t)byte - 256 : (int32_t)byte);
    }
    qa_q3_usercmd raw;
    qa_usercmd_to_q3(value, &raw);
    bot_source_i32_write(command, raw.serverTime);
    bot_source_i32_write(command + 16, raw.buttons);
    command[20] = raw.weapon;
    for (uint32_t axis = 0; axis < 3; ++axis)
        bot_source_i32_write(command + 4 + axis * 4, raw.angles[axis]);
    command[21] = (uint8_t)raw.forwardmove;
    command[22] = (uint8_t)raw.rightmove;
    command[23] = (uint8_t)raw.upmove;
    return true;
}

bool bot_ai_source_command_pause(qa_bots *bots, bot_ai_state *state, int32_t time, qa_error *error) {
    uint8_t *command;
    if (!bytes(bots, state, &command, error)) return false;
    command+=QA_BOT_SOURCE_COMMAND;
    command[21] = command[22] = command[23] = 0;
    bot_source_word_write(command + 16, 0);
    bot_source_i32_write(command, time);
    return true;
}

static bool action_client(qa_bots *bots,bot_ai_state *state,uint32_t *out,bool *ready,qa_error *error) {
    *ready=false;
    if(state->retired || !bot_ai_live(bots,state->view.actor)) return true;
    uint8_t *source;
    if(!bytes(bots,state,&source,error)) return false;
    if(state->retired || !bot_ai_live(bots,state->view.actor)) return true;
    if(!bots->services.source_action_client)
        return bot_ai_fail(error,"Source action requires its actual installed input namespace");
    int32_t client=bot_source_i32_read(source+QA_BOT_SOURCE_CLIENT);
    if(!bots->services.source_action_client(bots->services.context,client,out,error)) return false;
    *ready=!state->retired && bot_ai_live(bots,state->view.actor);
    return true;
}
bool bot_ai_source_action(qa_bots *bots,bot_ai_state *state,uint32_t flags,qa_error *error) {
    uint32_t client;bool ready;
    if(!action_client(bots,state,&client,&ready,error)) return false;
    return !ready || qa_bot_actions_add(qa_bot_runtime_actions(bots->runtime),client,flags,error);
}
bool bot_ai_source_action_text(qa_bots *bots,bot_ai_state *state,qa_bot_text_action kind,
                                int32_t recipient,const char *text,qa_error *error) {
    uint32_t client;bool ready;
    if(!action_client(bots,state,&client,&ready,error)) return false;
    return !ready || qa_bot_actions_text(qa_bot_runtime_actions(bots->runtime),(int32_t)client,
        kind,recipient,text,error);
}
bool bot_ai_source_action_view(qa_bots *bots,bot_ai_state *state,qa_error *error) {
    uint32_t client;bool ready;
    if(!action_client(bots,state,&client,&ready,error)) return false;
    return !ready || qa_bot_actions_view(qa_bot_runtime_actions(bots->runtime),client,
        bot_ai_view_angles(state),error);
}
bool bot_ai_source_action_weapon(qa_bots *bots,bot_ai_state *state,qa_error *error) {
    uint32_t client;bool ready;
    if(!action_client(bots,state,&client,&ready,error)) return false;
    return !ready || qa_bot_actions_weapon(qa_bot_runtime_actions(bots->runtime),client,
        bot_ai_weapon_number(state),error);
}
bool bot_ai_source_action_input(qa_bots *bots,bot_ai_state *state,float time,
                                 qa_bot_input *out,qa_error *error) {
    uint32_t client;bool ready;
    if(!action_client(bots,state,&client,&ready,error)) return false;
    return !ready || qa_bot_actions_input(qa_bot_runtime_actions(bots->runtime),client,time,out,error);
}
