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
    qa_bot_console_message message;
    while(qa_bot_chat_console_first(chat,&message)) {
        if(qa_bot_chat_console_count(chat)<10 && message.type==1 &&
           message.time>b->time-(1+bot_ai_random(b))) break;
        qa_bot_chat_match match;bool found;
        if(!qa_bot_chat_find_match(system,message.text,128,&match,&found,e)) return false;
        bool allowed=!b->controls.no_chat && !s->player.dead && !s->player.observer &&
            !s->player.intermission && s->view.decision!=QA_BOT_STANDING;
        if(b->services.modes && s->view.mode.generation) {
            qa_mode_view mode;
            if(!qa_modes_read(b->services.modes,s->view.mode,&mode,e)) return false;
            allowed=allowed && (mode.rules.kind==QA_MODE_FFA || mode.rules.kind==QA_MODE_DUEL ||
                               mode.rules.kind==QA_MODE_SINGLE_PLAYER);
        }
        if(message.type==1 && found && !(match.subtype&32768) && allowed) {
            char name[36],body[256];
            if(!qa_bot_chat_match_variable(&match,0,name,sizeof(name),e) ||
               !qa_bot_chat_match_variable(&match,2,body,sizeof(body),e)) return false;
            qa_bot_chat_unify_whitespace(body);
            if(!word(name,s->name)) {
                float chance;
                if(!bot_ai_character_float(b,s,BOT_C_CHAT_REPLY,0,1,&chance,e)) return false;
                if(bot_ai_random(b)<1.5f/((float)b->count+1) && bot_ai_random(b)<chance) {
                    const char *variables[8]={NULL,NULL,NULL,NULL,NULL,NULL,s->name,name};
                    bool reply;
                    if(!qa_bot_chat_reply_message(chat,body,1,16,variables,b->time,&reply,e)) return false;
                    if(reply) {
                        qa_bot_chat_console_remove(chat,message.handle);
                        s->chat_pending=true;s->stand_until=b->time+2;s->stand_enemy_time=0;
                        s->view.decision=QA_BOT_STANDING;
                        return true;
                    }
                }
            }
        }
        qa_bot_chat_console_remove(chat,message.handle);
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    }
    return true;
}
