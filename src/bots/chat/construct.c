#include "internal.h"
#include "qa/text.h"
#include <stdio.h>

bool chat_random_string(qa_bot_chat_system *system,const char *name,const char **out,qa_error *error) {
    const qa_bot_chat_asset *a = system->options.randoms;
    *out=NULL;
    if (a == NULL)
        return true;
    for (size_t i = 0; i < a->view.list_count; ++i) {
        qa_bot_chat_list list=a->lists[i];
        const char *actual;int32_t count;
        if(!bot_chat_packed_list(a,(uint32_t)i,&actual,&count,error)) return false;
        if (strcmp(actual, name) != 0)
            continue;
        float random=bot_random(&system->services.random);
        if(!bot_chat_packed_count(a,(uint32_t)i,&count,error)) return false;
        float selected=(float)(random*(float)count);
        if(!isfinite(selected)) {
            qa_error_set(error,QA_ERROR_ARGUMENT,0,"Random chat selection exceeds finite source range");return false;
        }
        double chosen=trunc((double)selected);
        if(chosen>=0 && chosen<list.messages.count) {
            uint32_t index=(uint32_t)chosen;
            if(a->packed_source) {
                const bot_chat_packed_member *member=&a->packed_source->groups[i];
                if(!bot_chat_packed_message(a,member->first+member->count-1-index,out,error)) return false;
            } else *out=a->messages[list.messages.first+index];
            return true;
        }
    }
    return true;
}
static bool expand(qa_bot_chat *state, const char *source, uint32_t context,
                   const qa_bot_chat_match *match, uint32_t variable_context, bool reply,
                   bool *expanded,bool *stopped, qa_error *e) {
    qa_bot_memory_span span;
    if(!chat_state_span(state,&span,e)) return false;
    char *output=(char *)span.data+CHAT_MESSAGE;
    size_t length = 0, pointer = 0;
    *expanded = *stopped = false;
    while (source[pointer] != 0) {
        if (source[pointer] != 1) {
            if (length == 255)
                goto overflow;
            if(!chat_state_span(state,&span,e)) return false;
            output=(char *)span.data+CHAT_MESSAGE;
            output[length++] = source[pointer++];
            continue;
        }
        char kind = source[++pointer];
        if (kind != 'v' && kind != 'r') {
            size_t size=strlen(source);
            if(size>SIZE_MAX-80) {qa_error_set(e,QA_ERROR_MEMORY,0,"Chat expansion diagnostic exceeds native extent");return false;}
            char *message=malloc(size+80);
            if(!message) {qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining chat expansion diagnostic");return false;}
            (void)snprintf(message,size+80,"BotConstructChat: message \"%s\" invalid escape char",source);
            bool ok=chat_print(state->system,QA_SCRIPT_FATAL,message,e);free(message);
            if(!ok) return false;
            continue;
        }
        ++pointer;
        size_t start=pointer;
        while(source[pointer] && source[pointer]!=1) ++pointer;
        size_t size=pointer-start;
        char *key=malloc(size+1);
        if(!key) {qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining chat escape argument");return false;}
        memcpy(key,source+start,size);key[size]=0;
        if (source[pointer] == 1)
            ++pointer;
        const char *value;
        char *variable_value=NULL;
        char variable[256];
        if (kind == 'v') {
            if(size>SIZE_MAX/2) {free(key);qa_error_set(e,QA_ERROR_MEMORY,0,"Chat numeric argument exceeds native extent");return false;}
            uint8_t *utf8=malloc(size?size*2:1);size_t count=0;
            if(!utf8) {free(key);qa_error_set(e,QA_ERROR_MEMORY,0,"Reading byte-valued chat numeric argument");return false;}
            for(size_t i=0;i<size;++i) {
                uint8_t byte=(uint8_t)key[i];
                if(byte>=128) {utf8[count++]=(uint8_t)(0xc0u|(byte>>6));utf8[count++]=(uint8_t)(0x80u|(byte&63u));}
                else utf8[count++]=byte;
            }
            double number;qa_error numeric={0};
            bool numeric_ok=qa_parse_ecmascript_number((qa_bytes){utf8,count},&number,&numeric);
            free(utf8);
            if(!numeric_ok && numeric.code!=QA_ERROR_FORMAT) {
                free(key);if(e) *e=numeric;return false;
            }
            if(!numeric_ok || !isfinite(number) || trunc(number)!=number || number<0 || number>=8) {
                size_t capacity=size+40;char *diagnostic=malloc(capacity);
                if(!diagnostic) {free(key);qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining invalid chat variable diagnostic");return false;}
                (void)snprintf(diagnostic,capacity,"message variable %s outside 0..7",key);
                *stopped=true;bool ok=chat_print(state->system,QA_SCRIPT_ERROR,diagnostic,e);
                free(diagnostic);free(key);return ok;
            }
            uint32_t index=(uint32_t)number;
            if(match->variables[index].offset<0) {free(key);continue;}
            if (!qa_bot_chat_match_variable(match, index, variable, sizeof(variable), e) ||
                !chat_replace_source(state->system,variable,variable_context,false,reply,&variable_value,e)) {
                free(key);return false;
            }
            value = variable_value;
        } else {
            if(!chat_random_string(state->system,key,&value,e)) {free(key);return false;}
            if (value == NULL) {
                char *message=malloc(size+48);
                if(!message) {free(key);qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining unknown random chat diagnostic");return false;}
                (void)snprintf(message,size+48,"BotConstructChat: unknown random string %s",key);
                *stopped=true;bool ok=chat_print(state->system,QA_SCRIPT_ERROR,message,e);
                free(message);free(key);return ok;
            }
            *expanded = true;
        }
        free(key);size = strlen(value);
        if (size >= QA_BOT_CHAT_MESSAGE_SIZE - length) {
            free(variable_value);
            size_t capacity=strlen(source)+48;char *message=malloc(capacity);
            if(!message) {qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining chat segment overflow diagnostic");return false;}
            (void)snprintf(message,capacity,kind=='v'?"BotConstructChat: message %s too long":"BotConstructChat: message \"%s\" too long",source);
            *stopped=true;bool ok=chat_print(state->system,QA_SCRIPT_ERROR,message,e);free(message);return ok;
        }
        if(!chat_state_span(state,&span,e)) {free(variable_value);return false;}
        output=(char *)span.data+CHAT_MESSAGE;
        memcpy(output + length, value, size);
        free(variable_value);
        length += size;
        output[length] = 0;
    }
    if(!chat_state_span(state,&span,e)) return false;
    output=(char *)span.data+CHAT_MESSAGE;output[length]=0;
    char *weighted=NULL;
    if(!chat_replace_source(state->system,output,context,true,false,&weighted,e)) return false;
    if(strlen(weighted)>=256) {
        free(weighted);*stopped=true;
        return chat_print(state->system,QA_SCRIPT_ERROR,"weighted message exceeds the source buffer",e);
    }
    if(!chat_state_span(state,&span,e)) {free(weighted);return false;}
    memcpy(span.data+CHAT_MESSAGE, weighted, strlen(weighted) + 1);
    free(weighted);
    return true;
overflow:
    *stopped=true;
    return chat_print(state->system,QA_SCRIPT_ERROR,"expanded message exceeds the source buffer",e);
}
bool chat_construct(qa_bot_chat *state, const char *text, uint32_t context,
                    qa_bot_chat_match *match, uint32_t variable_context, bool reply, qa_error *e) {
    size_t initial_size=strlen(text)+1;
    char *source=malloc(initial_size);
    if(!source) {qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining the actual untruncated source chat expression");return false;}
    memcpy(source,text,initial_size);bool ok=true;
    for (unsigned i = 0; i < 10; ++i) {
        bool expanded,stopped;
        ok=expand(state, source, context, match, variable_context, reply, &expanded,&stopped, e);
        if(!ok || stopped || !expanded) {free(source);return ok;}
        char *message;
        if(!chat_state_text(state,CHAT_MESSAGE,256,&message,e)) {free(source);return false;}
        size_t size=strlen(message)+1;
        if(size>initial_size) {
            char *next=realloc(source,size);
            if(!next) {free(source);qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining expanded source chat expression");return false;}
            source=next;initial_size=size;
        }
        memcpy(source,message,size);
    }
    free(source);char *message;
    return chat_print(state->system,QA_SCRIPT_WARNING,"too many expansions in chat message",e) &&
        chat_state_text(state,CHAT_MESSAGE,256,&message,e) && chat_print(state->system,QA_SCRIPT_WARNING,message,e);
}
bool chat_append_variables(qa_bot_chat_match *match, const char *const variables[8],qa_error *error) {
    qa_bot_chat_text_source sources[8]={0};
    if(variables) for(size_t i=0;i<8;++i) if(variables[i]) sources[i]=chat_text_source(variables[i]);
    return chat_append_variables_from(match,sources,error);
}
bool chat_append_variables_from(qa_bot_chat_match *match,const qa_bot_chat_text_source variables[8],qa_error *error) {
    if (variables == NULL)
        return true;
    for (size_t i = 0; i < 8; ++i) {
        if (!variables[i].read)
            continue;
        qa_bytes value;if(!chat_text_read(&variables[i],SIZE_MAX,&value,error)) return false;
        size_t length = strlen(match->text), size = value.size;
        uint8_t offset=(uint8_t)length;int8_t signed_offset;memcpy(&signed_offset,&offset,1);
        match->variables[i] = (qa_bot_chat_capture){signed_offset, (uint16_t)size};
        if(size>=256-length) {
            qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat text exceeds the source 256-byte buffer");return false;
        }
        if(size) memcpy(match->text + length, value.data, size);
        match->text[length + size] = 0;
    }
    return true;
}
size_t qa_bot_chat_initial_count(const qa_bot_chat *state, const char *name) {
    int32_t count=0;
    return qa_bot_chat_initial_count_source(state,name,&count,NULL)?(size_t)count:0;
}
bool qa_bot_chat_initial(qa_bot_chat *state, const char *name, uint32_t context,
                         const char *const variables[8], float time, bool *found, qa_error *e) {
    if (state == NULL || state->retired || state->system->restoring || found == NULL || !isfinite(time)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid initial bot chat request");
        return false;
    }
    *found = false;
    return chat_initial_source_construct(state,name,context,variables,time,found,e);
}
bool qa_bot_chat_initial_from(qa_bot_chat *state,const qa_bot_chat_text_source *name,uint32_t context,
    const qa_bot_chat_text_source variables[8],float time,bool *found,qa_error *error) {
    if(!state || state->retired || state->system->restoring || !found || !isfinite(time)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid source initial chat request");return false;
    }
    *found=false;return chat_initial_source_construct_from(state,name,context,variables,time,found,error);
}
static bool reply_key(const qa_bot_chat_asset *a, const qa_bot_chat_key *key,
                      const qa_bot_chat *state,const qa_bot_chat_text_source *input,
                      qa_bot_chat_match *match,bool *out,qa_error *error) {
    *out=false;
    switch (key->kind) {
    case QA_BOT_CHAT_NAME: {
        char *name,*text=NULL;
        if(!chat_state_text(state,CHAT_NAME,32,&name,error)) return false;
        char expected[33];memcpy(expected,name,strlen(name)+1);
        bool ok=chat_text_copy(input,SIZE_MAX,&text,error);
        if(ok) *out=qa_bot_chat_contains(text,expected,false)>=0;
        free(text);return ok;
    }
    case QA_BOT_CHAT_GENDER: {
        uint32_t gender;if(!chat_state_get(state,CHAT_GENDER,&gender,error)) return false;
        *out=gender==key->data.gender;return true;
    }
    case QA_BOT_CHAT_BOT_NAMES: {
        char *name;if(!chat_state_text(state,CHAT_NAME,32,&name,error)) return false;
        *out=qa_bot_chat_contains(key->data.text,name,false)>=0;return true;
    }
    case QA_BOT_CHAT_WORD: {
        char *text=NULL;bool ok=chat_text_copy(input,SIZE_MAX,&text,error);
        if(ok) *out=chat_word(text,key->data.text,0)>=0;
        free(text);return ok;
    }
    case QA_BOT_CHAT_PATTERN:
        *out=chat_match_pieces(a,key->data.pieces,match);return true;
    }
    return false;
}
static bool graph_text(const bot_chat_graph *graph,uint32_t key,const char **out,qa_error *error) {
    uint32_t string;
    return bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,4,BOT_CHAT_GRAPH_STRING,&string,error) &&
        bot_chat_graph_text(graph,string,out,error);
}
static bool graph_reply_key(qa_bot_chat_asset *asset,uint32_t key,qa_bot_chat *state,
    const qa_bot_chat_text_source *input,qa_bot_chat_match *match,bool *out,qa_error *error) {
    bot_chat_graph *graph=&asset->packed_source->graph;uint32_t flags;
    *out=false;
    if(!bot_chat_graph_word(graph,key,BOT_CHAT_GRAPH_KEY,0,&flags,error)) return false;
    if(flags&4) {
        char *text=NULL,*name;
        if(!chat_text_copy(input,SIZE_MAX,&text,error)) return false;
        bool ok=chat_state_text(state,CHAT_NAME,32,&name,error);
        if(ok) *out=qa_bot_chat_contains(text,name,false)>=0;
        free(text);return ok;
    }
    if(flags&32) {
        const char *text;char *name;
        if(!graph_text(graph,key,&text,error) || !chat_state_text(state,CHAT_NAME,32,&name,error)) return false;
        *out=qa_bot_chat_contains(text,name,false)>=0;return true;
    }
    if(flags&(64|128|256)) {
        uint32_t gender;
        if(!chat_state_get(state,CHAT_GENDER,&gender,error)) return false;
        *out=gender==(flags&64?1u:flags&128?2u:0u);return true;
    }
    if(flags&16) {
        uint32_t first;
        return bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,8,BOT_CHAT_GRAPH_PIECE,&first,error) &&
            bot_chat_graph_match(asset,first,match,out,error);
    }
    if(flags&8) {
        char *text=NULL;const char *word;
        if(!chat_text_copy(input,SIZE_MAX,&text,error)) return false;
        bool ok=graph_text(graph,key,&word,error);
        if(ok) *out=chat_word(text,word,0)>=0;
        free(text);return ok;
    }
    return true;
}
static bool graph_priority(const bot_chat_graph *graph,uint32_t reply,float *out,qa_error *error) {
    uint32_t bits;
    if(!bot_chat_graph_word(graph,reply,BOT_CHAT_GRAPH_REPLY,4,&bits,error)) return false;
    memcpy(out,&bits,4);return true;
}
static bool graph_reply_message(qa_bot_chat *state,qa_bot_chat_asset *asset,const char *input,
    const qa_bot_chat_text_source *source,uint32_t context,uint32_t variable_context,
    const qa_bot_chat_text_source variables[8],float time,bool *found,qa_error *error) {
    bot_chat_graph *graph=&asset->packed_source->graph;
    qa_bot_chat_match match,best_match;chat_match_clear(&match,input);
    float priority=-1;uint32_t selected=0,best_reply=0;
    uint32_t reply=graph->root;
    for(size_t visited=0;reply;++visited) {
        if(visited>=graph->count) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Reply source list cycles");return false;}
        uint32_t key;bool matches=false;
        if(!bot_chat_graph_link(graph,reply,BOT_CHAT_GRAPH_REPLY,0,BOT_CHAT_GRAPH_KEY,&key,error)) return false;
        for(size_t seen=0;key;++seen) {
            if(seen>=graph->count) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Reply source key list cycles");return false;}
            bool result;uint32_t flags;
            if(!graph_reply_key(asset,key,state,source,&match,&result,error) ||
               !bot_chat_graph_word(graph,key,BOT_CHAT_GRAPH_KEY,0,&flags,error)) return false;
            if(flags&1) {if(!result) {matches=false;break;}}
            else if(flags&2) {if(result) {matches=false;break;}}
            else if(result) matches=true;
            if(!bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,12,BOT_CHAT_GRAPH_KEY,&key,error)) return false;
        }
        float value;
        if(matches && !graph_priority(graph,reply,&value,error)) return false;
        if(matches && !(value<=priority)) {
            uint32_t message,eligible=0;
            if(!bot_chat_graph_link(graph,reply,BOT_CHAT_GRAPH_REPLY,12,BOT_CHAT_GRAPH_MESSAGE,&message,error)) return false;
            for(size_t seen=0;message;++seen) {
                if(seen>=graph->count) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Reply source message list cycles");return false;}
                float cooldown,current;
                if(!bot_chat_graph_message_time(asset,message,&cooldown,false,error) ||
                   !chat_source_time(state->system,time,&current,error)) return false;
                if(cooldown<=current) ++eligible;
                if(!bot_chat_graph_link(graph,message,BOT_CHAT_GRAPH_MESSAGE,8,BOT_CHAT_GRAPH_MESSAGE,&message,error)) return false;
            }
            double draw=trunc((double)(float)(bot_random(&state->system->services.random)*(float)eligible));
            if(!bot_chat_graph_link(graph,reply,BOT_CHAT_GRAPH_REPLY,12,BOT_CHAT_GRAPH_MESSAGE,&message,error)) return false;
            for(size_t seen=0;message;++seen) {
                if(seen>=graph->count) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Reply selection source list cycles");return false;}
                if(--draw<0) {
                    selected=message;best_reply=reply;best_match=match;
                    if(!graph_priority(graph,reply,&value,error)) return false;
                    priority=truncf(value);break;
                }
                float cooldown,current;
                if(!bot_chat_graph_message_time(asset,message,&cooldown,false,error) ||
                   !chat_source_time(state->system,time,&current,error)) return false;
                if(!bot_chat_graph_link(graph,message,BOT_CHAT_GRAPH_MESSAGE,8,BOT_CHAT_GRAPH_MESSAGE,&message,error)) return false;
            }
        }
        if(!bot_chat_graph_link(graph,reply,BOT_CHAT_GRAPH_REPLY,16,BOT_CHAT_GRAPH_REPLY,&reply,error)) return false;
    }
    if(!selected) return true;
    if(!chat_append_variables_from(&best_match,variables,error)) return false;
    qa_bot_chat_services services=state->system->services;
    if(services.test_reply && services.test_reply(services.context)) {
        uint32_t message;
        if(!bot_chat_graph_link(graph,best_reply,BOT_CHAT_GRAPH_REPLY,12,BOT_CHAT_GRAPH_MESSAGE,&message,error)) return false;
        for(size_t seen=0;message;++seen) {
            if(seen>=graph->count) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Reply test source list cycles");return false;}
            const char *text;char *output;
            if(!bot_chat_graph_message_text(asset,message,&text,error) ||
               !chat_construct(state,text,context,&best_match,variable_context,true,error) ||
               !chat_state_message_strip(state,error) || !chat_state_text(state,CHAT_MESSAGE,256,&output,error) ||
               !chat_print(state->system,QA_SCRIPT_INFO,output,error)) return false;
            if(!bot_chat_graph_link(graph,message,BOT_CHAT_GRAPH_MESSAGE,8,BOT_CHAT_GRAPH_MESSAGE,&message,error)) return false;
        }
    } else {
        float current;const char *text;
        if(!chat_source_time(state->system,time,&current,error)) return false;
        current=(float)(current+20);
        if(!bot_chat_graph_message_time(asset,selected,&current,true,error) ||
           !bot_chat_graph_message_text(asset,selected,&text,error) ||
           !chat_construct(state,text,context,&best_match,variable_context,true,error)) return false;
    }
    *found=true;return true;
}
static bool reply_message(qa_bot_chat *state,qa_bot_chat_asset *asset,const char *input,
                               const qa_bot_chat_text_source *source,uint32_t context,
                               uint32_t variable_context,const qa_bot_chat_text_source variables[8],
                               float time, bool *found, qa_error *e) {
    *found = false;
    if (asset == NULL)
        return true;
    if(asset->packed_source) return graph_reply_message(state,asset,input,source,context,
        variable_context,variables,time,found,e);
    qa_bot_chat_match match, best_match;
    chat_match_clear(&match, input);
    float priority = -1;
    uint32_t selected = QA_BOT_NO_INDEX;
    qa_bot_chat_range selected_messages = {0};
    for (size_t i = 0; i < asset->view.reply_count; ++i) {
        const qa_bot_chat_reply *reply = asset->replies + i;
        bool matches = false;
        for (uint32_t j = 0; j < reply->keys.count; ++j) {
            const qa_bot_chat_key *key = asset->keys + reply->keys.first + j;
            bool result;
            if(!reply_key(asset,key,state,source,&match,&result,e)) return false;
            if (key->mode == QA_BOT_CHAT_AND) {
                if (!result) {
                    matches = false;
                    break;
                }
            } else if (key->mode == QA_BOT_CHAT_NOT) {
                if (result) {
                    matches = false;
                    break;
                }
            } else if (result)
                matches = true;
        }
        if (!matches || reply->priority <= priority)
            continue;
        uint32_t eligible = 0;
        for (uint32_t j = 0; j < reply->messages.count; ++j) {
            float cooldown=asset->cooldowns[reply->messages.first+j],current;
            if(!chat_source_time(state->system,time,&current,e)) return false;
            if (cooldown <= current)
                ++eligible;
        }
        uint64_t draw =
            (uint64_t)(float)(bot_random(&state->system->services.random) * (float)eligible);
        for (uint32_t j = 0; j < reply->messages.count; ++j) {
            /* The source decrements before checking cooldown, including when
             * eligible is zero; preserve its observable message selection. */
            if (draw == 0) {
                selected = reply->messages.first + j;
                selected_messages = reply->messages;
                best_match = match;
                priority = truncf(reply->priority);
                break;
            }
            --draw;
            float cooldown=asset->cooldowns[reply->messages.first+j],current;
            if(!chat_source_time(state->system,time,&current,e)) return false;
            if(cooldown>current) continue;
        }
    }
    if (selected == QA_BOT_NO_INDEX)
        return true;
    if(!chat_append_variables_from(&best_match, variables,e)) return false;
    qa_bot_chat_services services = state->system->services;
    if (services.test_reply != NULL && services.test_reply(services.context)) {
        chat_retain(state);
        qa_bot_chat_asset_retain(asset);
        uint64_t revision = state->system->revision;
        bool ok = true;
        for (uint32_t i = 0; ok && !state->retired && state->system->revision == revision &&
                             i < selected_messages.count;
             ++i) {
            ok = chat_construct(state, asset->messages[selected_messages.first + i], context,
                                &best_match, variable_context, true, e);
            if (ok && !state->retired) {
                char *message;ok=chat_state_message_strip(state,e) &&
                    chat_state_text(state,CHAT_MESSAGE,256,&message,e);
                if(ok) ok=chat_print(state->system,QA_SCRIPT_INFO,message,e);
            }
        }
        qa_bot_chat_asset_release(asset);
        chat_release(state);
        if (!ok)
            return false;
    } else {
        float current;if(!chat_source_time(state->system,time,&current,e)) return false;
        asset->cooldowns[selected] = (float)(current + 20);
        if (!chat_construct(state, asset->messages[selected], context, &best_match,
                            variable_context, true, e))
            return false;
    }
    *found = true;
    return true;
}
bool qa_bot_chat_reply_message_from(qa_bot_chat *state,const qa_bot_chat_text_source *input,
    uint32_t context,uint32_t variable_context,const qa_bot_chat_text_source variables[8],
    float time,bool *found,qa_error *error) {
    if(!state || state->retired || state->system->restoring || !input || !input->read ||
       !found || !isfinite(time)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid source bot reply request");return false;
    }
    *found=false;chat_retain(state);char *text=NULL;
    bool ok=chat_text_copy(input,SIZE_MAX,&text,error);
    if(ok && strlen(text)>=256) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat text exceeds the source 256-byte buffer");ok=false;
    }
    qa_bot_chat_asset *asset=state->system->options.replies;qa_bot_chat_asset_retain(asset);
    if(ok) ok=reply_message(state,asset,text,input,context,variable_context,variables,time,found,error);
    qa_bot_chat_asset_release(asset);free(text);chat_release(state);return ok;
}
bool qa_bot_chat_reply_message(qa_bot_chat *state,const char *input,uint32_t context,
    uint32_t variable_context,const char *const variables[8],float time,bool *found,qa_error *error) {
    qa_bot_chat_text_source source=chat_text_source(input),values[8]={0};
    if(variables) for(size_t index=0;index<8;++index)
        if(variables[index]) values[index]=chat_text_source(variables[index]);
    return qa_bot_chat_reply_message_from(state,input?&source:NULL,context,variable_context,values,time,found,error);
}
