#include "source_command.h"
#include "qa/binary.h"
#include "source_alias.h"
#include "source_player.h"
#include "source_view.h"
#include "source_selectors.h"
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
    qa_q3_usercmd raw = {.serverTime = qa_load_i32le(command),
        .buttons = qa_load_i32le(command + 16), .weapon = command[20]};
    for (uint32_t axis = 0; axis < 3; ++axis)
        raw.angles[axis] = qa_load_i32le(command + 4 + axis * 4);
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
        uint32_t byte = (uint32_t)qa_source_to_i32(*moves[axis]) & 255u;
        *moves[axis] = (float)(byte >= 128u ? (int32_t)byte - 256 : (int32_t)byte);
    }
    qa_q3_usercmd raw;
    qa_usercmd_to_q3(value, &raw);
    qa_store_u32le(command, (uint32_t)raw.serverTime);
    qa_store_u32le(command + 16, (uint32_t)raw.buttons);
    command[20] = raw.weapon;
    for (uint32_t axis = 0; axis < 3; ++axis)
        qa_store_u32le(command + 4 + axis * 4, (uint32_t)raw.angles[axis]);
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
    qa_store_u32le(command + 16, 0);
    qa_store_u32le(command, (uint32_t)time);
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
    int32_t client=qa_load_i32le(source+QA_BOT_SOURCE_CLIENT);
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
bool bot_ai_source_action_drift(qa_bots *bots,bot_ai_state *state,qa_vec3 velocity,
    qa_error *error)
{
    uint32_t client;bool ready;
    if(!action_client(bots,state,&client,&ready,error)) return false;
    if(!ready) return true;
    qa_bot_actions *actions=qa_bot_runtime_actions(bots->runtime);
    qa_bot_input input;
    if(!qa_bot_actions_read(actions,client,&input,error)) return false;
    if(input.speed<=0 || input.direction.z!=0 ||
       (input.action_flags&(QA_BOT_JUMP|QA_BOT_DELAYED_JUMP|QA_BOT_CROUCH|
        QA_BOT_MOVE_UP|QA_BOT_MOVE_DOWN|QA_BOT_MOVE_FORWARD|QA_BOT_MOVE_BACK|
        QA_BOT_MOVE_LEFT|QA_BOT_MOVE_RIGHT))) return true;
    if(!bots->services.shared.player_control) return true;
    qa_builtin_player_control control;
    if(!bots->services.shared.player_control(bots->services.shared.context,0,
        state->view.actor,&control,error)) return false;
    if(state->retired || !bot_ai_live(bots,state->view.actor)) return true;
    if(control.player->water_level>=2 || control.player->cutscene ||
       control.ground->hit==QA_TRACE_HIT_NONE) return true;
    qa_vec3 direction=qa_vec_normalize(input.direction);
    if(qa_vec_dot(direction,direction)==0) return true;
    velocity.z=0;
    float along=qa_vec_dot(velocity,direction);
    qa_vec3 lateral=qa_vec_sub(velocity,qa_vec_scale(direction,along));
    qa_vec3 wish=qa_vec_sub(qa_vec_scale(input.direction,input.speed),lateral);
    float speed=qa_vec_length(wish);
    if(speed==0) return true;
    return qa_bot_actions_move(actions,client,qa_vec_scale(wish,1/speed),
        fminf(speed,input.speed),error);
}
bool bot_ai_source_action_trigger(qa_bots *bots,bot_ai_state *state,
    const qa_bot_source_weapon *weapon,qa_error *error)
{
    uint32_t client;bool ready;
    if(!action_client(bots,state,&client,&ready,error)) return false;
    if(!ready) return true;
    qa_bot_actions *actions=qa_bot_runtime_actions(bots->runtime);
    qa_bot_input input;
    if(!qa_bot_actions_read(actions,client,&input,error)) return false;
    if(state->source_settings && state->hazard.escaping)
        input.action_flags&=~(uint32_t)QA_BOT_ATTACK;
    if(!bot_ai_live(bots,bot_ai_enemy_actor(bots,state))) {
        bot_ai_flag_set(state,BOT_AI_ATTACKED,false);bot_ai_fire_shoot_time_set(state,0);
    } else {
        qa_bot_trigger_state trigger={.held=bot_ai_flag(state,BOT_AI_ATTACKED),
            .held_since=bot_ai_fire_shoot_time(state),.ready_at=bot_ai_fire_wait_time(state)};
        bool fire=qa_bot_weapon_trigger_step(&trigger,weapon,
            (input.action_flags&QA_BOT_ATTACK)!=0,bots->time);
        bot_ai_flag_set(state,BOT_AI_ATTACKED,trigger.held);
        bot_ai_fire_shoot_time_set(state,trigger.held_since);bot_ai_fire_wait_time_set(state,trigger.ready_at);
        if(!fire) input.action_flags&=~(uint32_t)QA_BOT_ATTACK;
    }
    return qa_bot_actions_restore(actions,client,&input,error);
}
