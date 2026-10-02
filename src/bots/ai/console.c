#include "internal.h"

static bool word(const char *text, const char *expected) {
    while (*text && *expected) {
        unsigned char byte = (unsigned char)*text++;
        if (byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
        unsigned char other=(unsigned char)*expected++;
        if(other>='A' && other<='Z') other+='a'-'A';
        if (byte != other) return false;
    }
    return !*text && !*expected;
}
static void uncolor(char *text) {
    char *out = text;
    for (const unsigned char *in = (const unsigned char *)text; *in; ++in) {
        if (*in == '^' && in[1] && in[1] != '^') { ++in; continue; }
        if (*in == 127) continue;
        *out++ = (char)*in;
    }
    *out = 0;
}
static bool remove_console(qa_bot_chat *chat,uint32_t handle,qa_error *error) {
    bool removed;return qa_bot_chat_console_remove_source(chat,handle,&removed,error);
}
bool bot_ai_console(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if (!b->services.console) return true;
    for (;;) {
        char text[1024] = {0}; bool found;
        if (!b->services.console(b->services.context, s->view.actor, text, sizeof(text), &found, e)) return false;
        if (!found || s->retired || !bot_ai_live(b, s->view.actor)) return true;
        text[sizeof(text) - 1] = 0;
        char *arguments = strchr(text, ' ');
        if (!arguments) continue;
        *arguments++ = 0;
        uncolor(arguments);
        bool print = word(text, "print");
        if (print || word(text, "chat") || word(text, "tchat")) {
            size_t length = strlen(arguments);
            if (length < 2) return bot_ai_fail(e, "bot console quoted command is truncated");
            arguments[length - 1] = 0;
            uint32_t handle;
            if (!qa_bot_chat_console_queue(qa_bot_runtime_chat(b->runtime, s->chat), print ? 0 : 1,
                                              arguments + 1, b->time, &handle, e)) return false;
        } else if (s->team_arena) {
            int32_t channel = word(text, "vchat") ? 0 : word(text, "vtchat") ? 1 : word(text, "vtell") ? 2 : -1;
            if (channel >= 0 && !bot_ai_voice(b, s, channel, arguments, e)) return false;
        }
    }
}
bool bot_ai_messages(qa_bots *b,bot_ai_state *s,qa_error *e) {
    qa_bot_chat *chat=qa_bot_runtime_chat(b->runtime,s->chat);
    qa_bot_chat_system *system=qa_bot_runtime_chat_system(b->runtime);
    qa_bot_console_message message;int32_t self;char bot_name[36];
    if(!bot_ai_source_client(b,s,&self,e) ||
       !bot_ai_client_name(b,self,bot_name,sizeof(bot_name),true,e)) return false;
    for(;;) {
        bool present;int32_t count;
        if(!qa_bot_chat_console_first_source(chat,&message,&present,e)) return false;
        if(!present) break;
        if(!qa_bot_chat_console_count_source(chat,&count,e)) return false;
        if(count<10 && message.type==1) {
            float random;if(!bot_ai_random(b,&random,e)) return false;
            volatile float delay=1+random,threshold=b->time-delay;
            if(message.time>threshold) break;
        }
        qa_bot_chat_match match;bool found,matched;size_t offset=0;uint32_t synonym_context;
        if(message.type==1) {
            if(!qa_bot_chat_find_match(system,message.text,128,&match,&found,e)) return false;
            if(found && match.variables[2].offset>=0) offset=(size_t)match.variables[2].offset;
        }
        if(offset>strlen(message.text)) return bot_ai_fail(e,"source reply message offset is outside its actual text");
        if(!bot_ai_source_synonym_context(b,s,&synonym_context,e)) return false;
        qa_bot_chat_unify_whitespace(message.text+offset);
        if(!qa_bot_chat_replace_synonyms(system,message.text+offset,sizeof(message.text)-offset,
                synonym_context,false,false,e) ||
           !bot_ai_source_order_message(b,s,message.text,&matched,e)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        if(!matched && message.type==1 && !b->controls.no_chat) {
            if(!qa_bot_chat_find_match(system,message.text,128,&match,&found,e)) return false;
            if(!found || (match.subtype&32768)) {if(!remove_console(chat,message.handle,e)) return false;continue;}
            char name[36],body[256];
            if(!qa_bot_chat_match_variable(&match,0,name,sizeof(name),e) ||
               !qa_bot_chat_match_variable(&match,2,body,sizeof(body),e)) return false;
            qa_bot_chat_unify_whitespace(body);
            int32_t sender;if(!bot_ai_source_client_from_name(b,name,&sender,e)) return false;
            if(sender!=self) {
                int32_t test;
                if(!bot_ai_source_test_random_chat(b,&test,e)) return false;
                const char *variables[8]={NULL,NULL,NULL,NULL,NULL,NULL,bot_name,name};
                if(test) {
                    bool reply;
                    if(!qa_bot_library_variable_set(qa_bot_runtime_library(b->runtime),"bot_testrchat","1",e) ||
                       !qa_bot_chat_reply_message(chat,body,synonym_context,16,variables,b->time,&reply,e) ||
                       !bot_ai_source_print(b,reply?"------------------------\n":"**** no valid reply ****\n",e)) return false;
                    if(!remove_console(chat,message.handle,e)) return false;continue;
                }
                bool allowed=false;
                if(s->view.decision!=QA_BOT_STANDING &&
                   !bot_ai_source_valid_chat_position(b,s,&allowed,e)) return false;
                allowed=allowed && b->source_goals.game_type<3;
                if(!allowed) {if(!remove_console(chat,message.handle,e)) return false;continue;}
                float chance;
                if(!bot_ai_character_float(b,s,BOT_C_CHAT_REPLY,0,1,&chance,e)) return false;
                float first,second;
                if(!bot_ai_random(b,&first,e)) return false;
                bool willing=first<1.5f/((float)b->count+1);
                if(willing && !bot_ai_random(b,&second,e)) return false;
                if(willing && second<chance) {
                    bool reply;
                    if(!qa_bot_chat_reply_message(chat,body,synonym_context,16,variables,b->time,&reply,e)) return false;
                    if(reply) {
                        float duration;
                        if(!bot_ai_source_chat_time(b,s,&duration,e)) return false;
                        if(!remove_console(chat,message.handle,e)) return false;
                        s->stand_until=b->time+duration;s->stand_enemy_time=b->time+1;
                        s->view.decision=QA_BOT_STANDING;
                        return true;
                    }
                }
            }
        }
        if(!remove_console(chat,message.handle,e)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    }
    return true;
}
