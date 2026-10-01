/* ai-main.ts setupClient and ai-combat.ts BotDeathmatchAI setup continuation. */
#include "internal.h"
#include "source_inventory.h"
#include "source_setup.h"
#include "source_storage.h"
#include "source_player.h"

void bot_ai_source_setup_init(bot_source_setup_state *state) {
    *state=(bot_source_setup_state){0};
}
void bot_ai_source_setup_team(bot_source_setup_state *state,const char *team) {
    size_t length=strlen(team);
    if(length>=sizeof(state->team)) length=sizeof(state->team)-1;
    memset(state->team,0,sizeof(state->team));
    memcpy(state->team,team,length);
}
void bot_ai_source_setup_stage(bot_source_setup_state *state,bot_source_setup_stage stage) {
    state->progress=(bot_source_setup_progress){
        .kind=BOT_SOURCE_SETUP_SETTING_UP,.value.stage=stage};
}
void bot_ai_source_setup_failed(bot_source_setup_state *state,bot_source_setup_failure stage,
                                 int32_t error) {
    state->progress=(bot_source_setup_progress){
        .kind=BOT_SOURCE_SETUP_FAILED,.value.failure={.stage=stage,.error=error}};
}
static bool gender(qa_bots *b,bot_ai_state *s,char text[144],qa_error *e) {
    const char *value;
    bool written;
    if(!qa_bot_runtime_character_string(b->runtime,s->character,BOT_C_GENDER,&value,&written,e))
        return false;
    size_t length=written?strlen(value):0;
    if(length>143) length=143;
    if(length) memcpy(text,value,length);
    text[length]=0;
    return true;
}
bool bot_ai_source_setup_gender(qa_bots *b,bot_ai_state *s,qa_error *e) {
    char text[144];
    if(!gender(b,s,text,e)) return false;
    uint32_t value=text[0]=='f' || text[0]=='F'?1:text[0]=='m' || text[0]=='M'?2:0;
    qa_bot_chat_set_gender(qa_bot_runtime_chat(b->runtime,s->chat),value);
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_CHAT_GENDER);
    return true;
}
bool bot_ai_source_setup_published(qa_bots *b,bot_ai_state *s,bool restart,bool interbreed,
                                    qa_error *e) {
    if(!qa_bot_moves_allocate(qa_bot_runtime_moves(b->runtime),&s->movement,e)) return false;
    if(!bot_ai_storage_u32(b,s,QA_BOT_SOURCE_MOVEMENT,&s->movement,true,e)) return false;
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_MOVE_STATE);
    if(!bot_ai_character_float(b,s,BOT_C_WALKER,0,1,&s->walker,e)) return false;
    if(!bot_ai_storage_f32(b,s,QA_BOT_SOURCE_WALKER,&s->walker,true,e)) return false;
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_WALKER);
    ++b->count;
    s->counted=true;
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_COUNTED);
    qa_cvars *configuration=b->services.configuration?
        b->services.configuration(b->services.context):NULL;
    if(!configuration) return bot_ai_fail(e,"Bot setup has no actual source cvar owner");
    const qa_cvar_view *test=qa_cvars_find(configuration,"bot_testichat");
    if(test && test->integer!=0) {
        if(!qa_bot_library_variable_set(qa_bot_runtime_library(b->runtime),"bot_testichat","1",e) ||
           !bot_ai_source_chat_test(b,s,e)) return false;
    }
    if(!bot_ai_schedule(b,e)) return false;
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_SCHEDULED);
    if(interbreed && !qa_bot_goals_mutate(qa_bot_runtime_goals(b->runtime),s->goals,e)) return false;
    bot_ai_source_setup_stage(&s->source_setup,BOT_SOURCE_SETUP_INTERBRED);
    if(restart && !bot_ai_session_read(b,s,e)) return false;
    s->source_setup.progress=(bot_source_setup_progress){.kind=BOT_SOURCE_SETUP_COMPLETE};
    return true;
}
/* Source q3 Info_SetValueForKey removes only the first matching key, prepends
 * the pair, and prints while retaining source text for rejected values. */
static bool set_sex(qa_bots *b,char text[1024],const char *value,qa_error *e) {
    if(strchr(value,'\\'))
        return bot_ai_source_print(b,"Can't use keys or values with a \\\n",e);
    if(strchr(value,';'))
        return bot_ai_source_print(b,"Can't use keys or values with a semicolon\n",e);
    if(strchr(value,'"'))
        return bot_ai_source_print(b,"Can't use keys or values with a \"\n",e);
    size_t length=strlen(text),cursor=0;
    while(cursor<length) {
        size_t start=cursor;
        if(text[cursor]=='\\') ++cursor;
        const char *separator=strchr(text+cursor,'\\');
        if(!separator) break;
        size_t offset=(size_t)(separator-text);
        const char *next=strchr(separator+1,'\\');
        size_t end=next?(size_t)(next-text):length;
        if(offset-cursor==3 && memcmp(text+cursor,"sex",3)==0) {
            memmove(text+start,text+end,length-end+1);
            length-=end-start;
            break;
        }
        cursor=end;
    }
    size_t value_length=strlen(value);
    if(!value_length) return true;
    size_t pair_length=5+value_length;
    if(pair_length+length>1024)
        return bot_ai_source_print(b,"Info string length exceeded\n",e);
    if(pair_length+length==1024)
        return bot_ai_fail(e,"Info string overflows source terminator");
    memmove(text+pair_length,text,length+1);
    memcpy(text,"\\sex\\",5);
    memcpy(text+5,value,value_length);
    return true;
}
bool bot_ai_source_setup_frame(qa_bots *b,bot_ai_state *s,bool *ready,qa_error *e) {
    *ready=true;
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_SETUP_COUNT,&s->setup_count,false,e)) return false;
    if(s->setup_count<=0) return true;
    --s->setup_count;
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_SETUP_COUNT,&s->setup_count,true,e)) return false;
    if(s->setup_count) {*ready=false;return true;}
    char text[144],userinfo[1024];
    if(!gender(b,s,text,e)) return false;
    if(!b->services.get_userinfo || !b->services.set_userinfo)
        return bot_ai_fail(e,"Bot setup requires its actual raw source userinfo owner");
    if(!b->services.get_userinfo(b->services.context,s->view.actor,userinfo,sizeof(userinfo),e) ||
       !set_sex(b,userinfo,text,e) ||
       !b->services.set_userinfo(b->services.context,s->view.actor,userinfo,e)) return false;
    if(!bot_ai_storage_bool(b,s,QA_BOT_SOURCE_MAP_RESTART,&s->source_setup.map_restart,false,e) ||
       !qa_bot_source_record_text_read(&b->services.memory,s->source_record,
            QA_BOT_SOURCE_TEAM,s->source_setup.team,sizeof(s->source_setup.team),e)) return false;
    if(!s->source_setup.map_restart) {
        int32_t game_type;
        if(!b->services.source_game_type)
            return bot_ai_fail(e,"Bot setup requires its actual source GAME game type");
        if(!b->services.source_game_type(b->services.context,&game_type,e)) return false;
        if(game_type!=1) {
            char command[144];
            size_t length=strlen(s->source_setup.team);
            if(length>138) length=138;
            memcpy(command,"team ",5);
            memcpy(command+5,s->source_setup.team,length);
            command[5+length]=0;
            if(!qa_bot_actions_text(qa_bot_runtime_actions(b->runtime),(int32_t)s->view.client,
                                    QA_BOT_COMMAND,0,command,e)) return false;
        }
    }
    qa_bot_chat *chat=qa_bot_runtime_chat(b->runtime,s->chat);
    qa_bot_chat_set_gender(chat,text[0]=='m'?2:text[0]=='f'?1:0);
    char name[144];
    int32_t source_client;
    if(!bot_ai_source_client(b,s,&source_client,e) ||
       !bot_ai_client_name(b,source_client,name,sizeof(name),true,e)) return false;
    qa_bot_chat_set_name(chat,name,source_client);
    s->source_chat.last_frame_health=bot_ai_inventory_value(s,QA_BOT_INV_HEALTH);
    if(!bot_ai_source_player_slot(b,s,BOT_PS_PERSISTENT,1,&s->source_chat.last_hit_count,e)) return false;
    s->setup_count=0;
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_FRAME_HEALTH,&s->source_chat.last_frame_health,true,e) ||
       !bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_HIT_COUNT,&s->source_chat.last_hit_count,true,e) ||
       !bot_ai_storage_i32(b,s,QA_BOT_SOURCE_SETUP_COUNT,&s->setup_count,true,e)) return false;
    return bot_ai_source_routes_setup(b,b->services.team_arena,e);
}
