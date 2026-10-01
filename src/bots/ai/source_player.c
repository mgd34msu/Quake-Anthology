#include "source_player.h"
#include "source_storage.h"
#include "source_alias.h"

bool qa_bot_player_state_generic(const qa_bot_player_state_view *view,int32_t *out,qa_error *error)
{
    if(!view || !view->bytes || !out)
        return bot_ai_fail(error,"Source player generic field requires its actual fixed view");
    *out=bot_source_i32_read(view->bytes+440);return true;
}

bool qa_bot_player_state_slot(const qa_bot_player_state_view *view,
    qa_bot_player_state_array field,int32_t index,int32_t *out,qa_error *error)
{
    int32_t count;
    switch(field) {
    case QA_BOT_PS_DELTA_ANGLES:count=3;break;
    case QA_BOT_PS_EVENTS:case QA_BOT_PS_EVENT_PARAMETERS:count=2;break;
    case QA_BOT_PS_STATS:case QA_BOT_PS_PERSISTENT:case QA_BOT_PS_POWERUPS:case QA_BOT_PS_AMMO:
        count=16;break;
    default:return bot_ai_fail(error,"Invalid source player slot array");
    }
    if(!view || !view->bytes || !out || index<0 || index>=count)
        return bot_ai_fail(error,"Source player slot requires its actual fixed source view");
    *out=bot_source_i32_read(view->bytes+(uint32_t)field+(uint32_t)index*4);
    return true;
}

bool bot_ai_source_player_view(qa_bots *bots,bot_ai_state *state,
    qa_bot_player_state_view *out,qa_error *error)
{
    if(!bots || !state || !out) return bot_ai_fail(error,"Source player view requires its BotState owner");
    if(!state->source_span.data && !bot_ai_source_alias_bind(bots,state,error)) return false;
    if(state->source_span.length!=QA_BOT_STATE_SOURCE_BYTES)
        return bot_ai_fail(error,"Source player view lacks its complete retained allocation");
    *out=(qa_bot_player_state_view){state->source_span.data+QA_BOT_SOURCE_PLAYER,
        bots->services.team_arena?QA_Q3_TEAM_ARENA:QA_Q3_ARENA};
    return true;
}

static bool word_field(bot_source_player_word field)
{
    switch(field) {
    case BOT_PS_COMMAND_TIME:case BOT_PS_MOVE_TYPE:case BOT_PS_BOB_CYCLE:
    case BOT_PS_MOVE_FLAGS:case BOT_PS_MOVE_TIME:case BOT_PS_WEAPON_TIME:
    case BOT_PS_GRAVITY:case BOT_PS_SPEED:case BOT_PS_GROUND_ENTITY:
    case BOT_PS_LEGS_TIME:case BOT_PS_LEGS_ANIMATION:case BOT_PS_TORSO_TIME:
    case BOT_PS_TORSO_ANIMATION:case BOT_PS_MOVE_DIRECTION:case BOT_PS_ENTITY_FLAGS:
    case BOT_PS_EVENT_SEQUENCE:case BOT_PS_EXTERNAL_EVENT:
    case BOT_PS_EXTERNAL_EVENT_PARAMETER:case BOT_PS_EXTERNAL_EVENT_TIME:
    case BOT_PS_CLIENT:case BOT_PS_WEAPON:case BOT_PS_WEAPON_STATE:
    case BOT_PS_VIEW_HEIGHT:case BOT_PS_DAMAGE_EVENT:case BOT_PS_DAMAGE_YAW:
    case BOT_PS_DAMAGE_PITCH:case BOT_PS_DAMAGE_COUNT:case BOT_PS_GENERIC:
    case BOT_PS_LOOP_SOUND:case BOT_PS_JUMP_PAD:case BOT_PS_PING:
    case BOT_PS_MOVE_FRAME:case BOT_PS_JUMP_PAD_FRAME:case BOT_PS_ENTITY_EVENT_SEQUENCE:
        return true;
    }
    return false;
}

bool bot_ai_source_player_word(qa_bots *bots,bot_ai_state *state,
    bot_source_player_word field,int32_t *out,qa_error *error)
{
    if(!out || !word_field(field)) return bot_ai_fail(error,"Invalid source player scalar field");
    int32_t value;
    if(!bot_ai_storage_i32(bots,state,QA_BOT_SOURCE_PLAYER+(uint32_t)field,&value,false,error)) return false;
    if((field==BOT_PS_MOVE_TYPE && (value<0 || value>6)) ||
       (field==BOT_PS_WEAPON && (value<0 || value>13)) ||
       (field==BOT_PS_WEAPON_STATE && (value<0 || value>3))) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Unsupported enum in the actual bot player source field");
        return false;
    }
    *out=value;return true;
}

bool bot_ai_source_player_vector(qa_bots *bots,bot_ai_state *state,
    bot_source_player_vector field,qa_vec3 *out,qa_error *error)
{
    if(field!=BOT_PS_ORIGIN && field!=BOT_PS_VELOCITY &&
       field!=BOT_PS_GRAPPLE_POINT && field!=BOT_PS_VIEW_ANGLES)
        return bot_ai_fail(error,"Invalid source player vector field");
    return bot_ai_storage_vec3(bots,state,QA_BOT_SOURCE_PLAYER+(uint32_t)field,out,false,error);
}

bool bot_ai_source_player_slot(qa_bots *bots,bot_ai_state *state,
    bot_source_player_array field,int32_t index,int32_t *out,qa_error *error)
{
    int32_t count;
    switch(field) {
    case BOT_PS_DELTA_ANGLES:count=3;break;
    case BOT_PS_EVENTS:case BOT_PS_EVENT_PARAMETERS:count=2;break;
    case BOT_PS_STATS:case BOT_PS_PERSISTENT:case BOT_PS_POWERUPS:case BOT_PS_AMMO:
        count=16;break;
    default:return bot_ai_fail(error,"Invalid source player slot array");
    }
    if(index<0 || index>=count) return bot_ai_fail(error,"Source player slot is outside its fixed array");
    return bot_ai_storage_i32(bots,state,QA_BOT_SOURCE_PLAYER+(uint32_t)field+(uint32_t)index*4,out,false,error);
}

bool bot_ai_source_player_copy(qa_bots *bots,bot_ai_state *state,
    const qa_q3_player *source,qa_error *error)
{
    if(!bots || !state || !source) return bot_ai_fail(error,"Source player copy requires its actual BotState owner");
    qa_q3_product product=bots->services.team_arena?QA_Q3_TEAM_ARENA:QA_Q3_ARENA;
    if(source->product!=product)
        return bot_ai_fail(error,"Bot player-state product differs from its actual GAME source");
    qa_bot_source_record player;
    if(!qa_bot_source_record_alias(state->source_record,QA_BOT_SOURCE_PLAYER,468,&player,error)) return false;
    qa_q3_player observed=*source;
    return qa_bot_source_record_player(&bots->services.memory,player,&observed,true,error);
}
