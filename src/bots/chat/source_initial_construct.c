#include "internal.h"
#include "qa/text.h"
#include <stdio.h>

bool qa_bot_chat_initial_count_source(const qa_bot_chat *state,const char *name,int32_t *out,
    qa_error *error) {
    qa_bot_chat_text_source source=chat_text_source(name);
    return qa_bot_chat_initial_count_from(state,name?&source:NULL,out,error);
}
bool qa_bot_chat_initial_count_from(const qa_bot_chat *state,const qa_bot_chat_text_source *name,int32_t *out,
    qa_error *error) {
    if(!state || !out || state->retired) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat count requires its live source state/output");return false;
    }
    *out=0;
    qa_bot_chat_asset *asset;
    if(!chat_state_initial(state,&asset,error)) return false;
    if(!asset) return true;
    if(!asset->initial_source) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat state has no genuine source resource");return false;
    }
    qa_bot_chat *retained=(qa_bot_chat *)state;chat_retain(retained);qa_bot_chat_asset_retain(asset);
    uint32_t type;bool ok=chat_initial_type_find_from(asset,name,&type,error);
    if(ok && type && state->system->services.test_initial &&
       state->system->services.test_initial(state->system->services.context)) {
        char *argument=NULL,*text=NULL;int32_t count;
        ok=chat_text_copy(name,SIZE_MAX,&argument,error) &&
            bot_chat_initial_type_count(&asset->initial_source->initial,type,&count,error);
        int length=ok?snprintf(NULL,0,"%s has %d chat lines",argument,count):-1;
        if(ok && length<0) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Formatting source initial chat count");ok=false;}
        if(ok && !(text=malloc((size_t)length+1))) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source initial chat count report");ok=false;
        }
        if(ok) {
            (void)snprintf(text,(size_t)length+1,"%s has %d chat lines",argument,count);
            ok=chat_print(state->system,QA_SCRIPT_INFO,text,error) &&
                chat_print(state->system,QA_SCRIPT_INFO,"-------------------",error);
        }
        free(argument);free(text);
    }
    if(ok && type) ok=bot_chat_initial_type_count(&asset->initial_source->initial,type,out,error);
    qa_bot_chat_asset_release(asset);chat_release(retained);return ok;
}
static bool missing(qa_bot_chat *state,const char *name,qa_error *error) {
    if(!state->system->options.debug) return true;
    if(!name) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"BotInitialChat: DEBUG print reads a null source type string");return false;}
    size_t length=strlen(name);
    static const char prefix[]="no chat messages of type ";
    if(length>SIZE_MAX-sizeof(prefix)) {qa_error_set(error,QA_ERROR_MEMORY,0,"Initial chat report exceeds native extent");return false;}
    char *text=malloc(length+sizeof(prefix));
    if(!text) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining missing initial chat source report");return false;}
    memcpy(text,prefix,sizeof(prefix)-1);memcpy(text+sizeof(prefix)-1,name,length+1);
    bool ok=chat_print(state->system,QA_SCRIPT_INFO,text,error);free(text);return ok;
}
static bool message_next(const bot_chat_initial *chat,uint32_t *message,size_t *visited,
    qa_error *error) {
    if(++*visited>chat->message_count) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Initial chat message links contain a cycle");return false;
    }
    return bot_chat_initial_message_next(chat,*message,message,error);
}
bool chat_initial_source_construct(qa_bot_chat *state,const char *name,uint32_t context,
    const char *const variables[8],float fallback,bool *found,qa_error *error) {
    qa_bot_chat_text_source source=chat_text_source(name),values[8]={0};
    if(variables) for(size_t index=0;index<8;++index)
        if(variables[index]) values[index]=chat_text_source(variables[index]);
    return chat_initial_source_construct_from(state,name?&source:NULL,context,values,fallback,found,error);
}
bool chat_initial_source_construct_from(qa_bot_chat *state,const qa_bot_chat_text_source *name,uint32_t context,
    const qa_bot_chat_text_source variables[8],float fallback,bool *found,qa_error *error) {
    qa_bot_chat_asset *asset;
    if(!chat_state_initial(state,&asset,error)) return false;
    if(!asset) return true;
    qa_bot_chat_asset_retain(asset);chat_retain(state);
    bot_chat_initial *chat=&asset->initial_source->initial;
    uint32_t type,first=0,message=0,selected=0;bool ok=chat_initial_type_find_from(asset,name,&type,error);
    if(ok && type) ok=bot_chat_initial_type_first(chat,type,&first,error);
    int32_t eligible=0;size_t visited=0;
    for(message=first;ok && message;) {
        float cooldown,time;
        ok=bot_chat_initial_message_time(chat,message,&cooldown,false,error) &&
            chat_source_time(state->system,fallback,&time,error);
        if(ok && cooldown<=time) ++eligible;
        if(ok) ok=message_next(chat,&message,&visited,error);
    }
    if(ok && eligible==0) {
        float best=0;visited=0;
        for(message=first;ok && message;) {
            float cooldown;
            ok=bot_chat_initial_message_time(chat,message,&cooldown,false,error);
            if(ok && (best==0 || cooldown<best)) {selected=message;best=cooldown;}
            if(ok) ok=message_next(chat,&message,&visited,error);
        }
    } else if(ok) {
        int32_t index=(int32_t)(float)(bot_random(&state->system->services.random)*(float)eligible);
        visited=0;
        for(message=first;ok && message;) {
            float cooldown,time;
            ok=bot_chat_initial_message_time(chat,message,&cooldown,false,error) &&
                chat_source_time(state->system,fallback,&time,error);
            if(ok && cooldown<=time && --index<0) {
                selected=message;
                ok=chat_source_time(state->system,fallback,&time,error);
                if(ok) {time=(float)(time+20);ok=bot_chat_initial_message_time(chat,message,&time,true,error);}
                break;
            }
            if(ok) ok=message_next(chat,&message,&visited,error);
        }
    }
    if(ok && selected) {
        qa_bot_chat_match match;chat_match_clear(&match,"");
        ok=chat_append_variables_from(&match,variables,error);
        qa_bytes text;
        if(ok) ok=bot_chat_initial_message_text(chat,selected,&text,error) &&
            chat_construct(state,(const char *)text.data,context,&match,0,false,error);
        if(ok) *found=true;
    } else if(ok && state->system->options.debug) {
        char *text=NULL;
        ok=!name?missing(state,NULL,error):chat_text_copy(name,SIZE_MAX,&text,error) && missing(state,text,error);
        free(text);
    }
    qa_bot_chat_asset_release(asset);chat_release(state);return ok;
}
