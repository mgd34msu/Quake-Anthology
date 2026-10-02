#include "internal.h"
#include "qa/bots_log_consumers.h"
#include "qa/text.h"
#include <stdarg.h>
#include <stdio.h>

typedef struct chat_log_output {
    qa_bot_log *log;
    qa_bot_log_file *file;
    bool raw;
} chat_log_output;

bool qa_bot_chat_system_log_bind(qa_bot_chat_system *system, qa_bot_log *log, qa_error *error) {
    if (!system || system->retired || qa_bot_chat_system_active(system)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Chat log binding requires its idle actual system");
        return false;
    }
    system->log = log;
    return true;
}

static bool write_text(chat_log_output *output, const char *text, qa_error *error) {
    if (!output->raw) return qa_bot_log_write(output->log, text, error);
    int64_t written;
    return qa_bot_log_file_write(output->file, text, &written, error);
}

static bool write_format(chat_log_output *output, qa_error *error, const char *format, ...) {
    va_list arguments, copy;
    va_start(arguments, format);
    va_copy(copy, arguments);
    int count = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (count < 0) {
        va_end(arguments);
        qa_error_set(error, QA_ERROR_IO, 0, "Formatting bot chat log text");
        return false;
    }
    char *text = malloc((size_t)count + 1);
    if (!text) {
        va_end(arguments);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating bot chat log text");
        return false;
    }
    (void)vsnprintf(text, (size_t)count + 1, format, arguments);
    va_end(arguments);
    bool okay = write_text(output, text, error);
    free(text);
    return okay;
}

static bool pieces(chat_log_output *output, const qa_bot_chat_asset_view *view,
                     qa_bot_chat_range range, bool first_only, qa_error *error) {
    for (uint32_t index = 0; index < range.count; ++index) {
        const qa_bot_chat_piece *piece = view->pieces + range.first + index;
        if (piece->kind == QA_BOT_CHAT_VARIABLE) {
            if (!write_format(output, error, "%u", piece->data.variable)) return false;
        } else {
            uint32_t count = first_only ? 1 : piece->data.alternatives.count;
            for (uint32_t alternative = 0; alternative < count; ++alternative) {
                const char *text = view->alternatives[piece->data.alternatives.first + alternative];
                if (!write_format(output, error, "\"%s\"", text)) return false;
                if (alternative + 1 < count && !write_text(output, "|", error)) return false;
            }
        }
        if (index + 1 < range.count && !write_text(output, ", ", error)) return false;
    }
    return true;
}

static bool reply_keys(chat_log_output *output, const qa_bot_chat_asset_view *view,
                         const qa_bot_chat_reply *reply, qa_error *error) {
    if (!write_text(output, "[", error)) return false;
    for (uint32_t index = 0; index < reply->keys.count; ++index) {
        const qa_bot_chat_key *key = view->keys + reply->keys.first + index;
        if (key->mode == QA_BOT_CHAT_AND && !write_text(output, "&", error)) return false;
        if (key->mode == QA_BOT_CHAT_NOT && !write_text(output, "!", error)) return false;
        if (key->kind == QA_BOT_CHAT_NAME) {
            if (!write_text(output, "name", error)) return false;
        } else if (key->kind == QA_BOT_CHAT_GENDER) {
            const char *gender = key->data.gender == 1 ? "female" : key->data.gender == 2 ? "male" : "it";
            if (!write_text(output, gender, error)) return false;
        } else if (key->kind == QA_BOT_CHAT_PATTERN) {
            if (!write_text(output, "(", error) ||
                !pieces(output, view, key->data.pieces, true, error) ||
                !write_text(output, ")", error)) return false;
        } else if (key->kind == QA_BOT_CHAT_WORD) {
            if (!write_format(output, error, "\"%s\"", key->data.text)) return false;
        }
        /* Source prints no text for its bot-name key bit. */
        if (index + 1 < reply->keys.count) {
            if (!write_text(output, ", ", error)) return false;
        } else {
            char priority[64];
            if (!qa_format_fixed(reply->priority, 0, priority, sizeof(priority), error) ||
                !write_format(output, error, "] = %s\n", priority)) return false;
        }
    }
    return write_text(output, "{\n", error);
}
static bool graph_pieces(chat_log_output *output,const bot_chat_graph *graph,uint32_t piece,
    bool first_only,qa_error *error) {
    for(size_t seen=0;piece;++seen) {
        if(seen>=graph->count) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat log piece list cycles");return false;}
        uint32_t type,next;
        if(!bot_chat_graph_word(graph,piece,BOT_CHAT_GRAPH_PIECE,0,&type,error)) return false;
        if(type==1 || (first_only && type!=2)) {
            uint32_t variable;
            if(!bot_chat_graph_word(graph,piece,BOT_CHAT_GRAPH_PIECE,8,&variable,error) ||
               !write_format(output,error,"%u",variable)) return false;
        } else if(type==2) {
            uint32_t string;
            if(!bot_chat_graph_link(graph,piece,BOT_CHAT_GRAPH_PIECE,4,BOT_CHAT_GRAPH_MATCH_STRING,&string,error)) return false;
            if(first_only && !string) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Reply match string has no first alternative");return false;}
            for(size_t count=0;string;++count) {
                if(count>=graph->count) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat log alternative list cycles");return false;}
                uint32_t text;const char *value;
                if(!bot_chat_graph_link(graph,string,BOT_CHAT_GRAPH_MATCH_STRING,0,BOT_CHAT_GRAPH_STRING,&text,error) ||
                   !bot_chat_graph_text(graph,text,&value,error) || !write_format(output,error,"\"%s\"",value)) return false;
                if(first_only) break;
                if(!bot_chat_graph_link(graph,string,BOT_CHAT_GRAPH_MATCH_STRING,4,BOT_CHAT_GRAPH_MATCH_STRING,&next,error) ||
                   (next && !write_text(output,"|",error)) ||
                   !bot_chat_graph_link(graph,string,BOT_CHAT_GRAPH_MATCH_STRING,4,BOT_CHAT_GRAPH_MATCH_STRING,&string,error)) return false;
            }
        }
        if(!bot_chat_graph_link(graph,piece,BOT_CHAT_GRAPH_PIECE,12,BOT_CHAT_GRAPH_PIECE,&next,error) ||
           (next && !write_text(output,", ",error)) ||
           !bot_chat_graph_link(graph,piece,BOT_CHAT_GRAPH_PIECE,12,BOT_CHAT_GRAPH_PIECE,&piece,error)) return false;
    }
    return true;
}
static bool graph_reply_keys(chat_log_output *output,const bot_chat_graph *graph,uint32_t reply,qa_error *error) {
    if(!write_text(output,"[",error)) return false;
    uint32_t key;
    if(!bot_chat_graph_link(graph,reply,BOT_CHAT_GRAPH_REPLY,0,BOT_CHAT_GRAPH_KEY,&key,error)) return false;
    for(size_t seen=0;key;++seen) {
        if(seen>=graph->count) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat log key list cycles");return false;}
        uint32_t flags;
        if(!bot_chat_graph_word(graph,key,BOT_CHAT_GRAPH_KEY,0,&flags,error) ||
           ((flags&1)?!write_text(output,"&",error):(flags&2)?!write_text(output,"!",error):false)) return false;
        if(!bot_chat_graph_word(graph,key,BOT_CHAT_GRAPH_KEY,0,&flags,error)) return false;
        const char *name=flags&4?"name":flags&64?"female":flags&128?"male":flags&256?"it":NULL;
        if(name) {if(!write_text(output,name,error)) return false;}
        else if(flags&16) {
            uint32_t piece;
            if(!write_text(output,"(",error) ||
               !bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,8,BOT_CHAT_GRAPH_PIECE,&piece,error) ||
               !graph_pieces(output,graph,piece,true,error) || !write_text(output,")",error)) return false;
        } else if(flags&8) {
            uint32_t string;const char *text;
            if(!bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,4,BOT_CHAT_GRAPH_STRING,&string,error) ||
               !bot_chat_graph_text(graph,string,&text,error) || !write_format(output,error,"\"%s\"",text)) return false;
        }
        uint32_t next;
        if(!bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,12,BOT_CHAT_GRAPH_KEY,&next,error)) return false;
        if(next) {if(!write_text(output,", ",error)) return false;}
        else {
            uint32_t bits;float value;char priority[64];
            if(!bot_chat_graph_word(graph,reply,BOT_CHAT_GRAPH_REPLY,4,&bits,error)) return false;
            memcpy(&value,&bits,4);
            if(!qa_format_fixed(value,0,priority,sizeof(priority),error) || !write_format(output,error,"] = %s\n",priority)) return false;
        }
        if(!bot_chat_graph_link(graph,key,BOT_CHAT_GRAPH_KEY,12,BOT_CHAT_GRAPH_KEY,&key,error)) return false;
    }
    return write_text(output,"{\n",error);
}
static bool graph_dump(chat_log_output *output,qa_bot_chat_asset *asset,qa_error *error) {
    const bot_chat_graph *graph=&asset->packed_source->graph;
    bool replies=asset->view.kind==QA_BOT_CHAT_REPLIES;
    bot_chat_graph_kind kind=replies?BOT_CHAT_GRAPH_REPLY:BOT_CHAT_GRAPH_TEMPLATE;
    if(replies && !write_text(output,"BotDumpReplyChat:\n",error)) return false;
    for(uint32_t root=graph->root,seen=0;root;++seen) {
        if(seen>=graph->count) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat log root list cycles");return false;}
        if(replies) {
            if(!graph_reply_keys(output,graph,root,error)) return false;
            uint32_t message;
            if(!bot_chat_graph_link(graph,root,kind,12,BOT_CHAT_GRAPH_MESSAGE,&message,error)) return false;
            for(size_t count=0;message;++count) {
                if(count>=graph->count) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat log message list cycles");return false;}
                const char *text;
                if(!bot_chat_graph_message_text(asset,message,&text,error) ||
                   !write_format(output,error,"\t\"%s\";\n",text) ||
                   !bot_chat_graph_link(graph,message,BOT_CHAT_GRAPH_MESSAGE,8,BOT_CHAT_GRAPH_MESSAGE,&message,error)) return false;
            }
            if(!write_text(output,"}\n",error)) return false;
        } else {
            uint32_t piece,type_bits,subtype_bits;int32_t type,subtype;
            if(!write_text(output,"{ ",error) ||
               !bot_chat_graph_link(graph,root,kind,12,BOT_CHAT_GRAPH_PIECE,&piece,error) ||
               !graph_pieces(output,graph,piece,false,error) ||
               !bot_chat_graph_word(graph,root,kind,4,&type_bits,error) ||
               !bot_chat_graph_word(graph,root,kind,8,&subtype_bits,error)) return false;
            memcpy(&type,&type_bits,4);memcpy(&subtype,&subtype_bits,4);
            if(!write_format(output,error," = (%d, %d);}\n",type,subtype)) return false;
        }
        if(!bot_chat_graph_link(graph,root,kind,16,kind,&root,error)) return false;
    }
    return true;
}

static bool dump(chat_log_output *output,qa_bot_chat_asset *asset,qa_error *error) {
    const qa_bot_chat_asset_view *view=&asset->view;
    if(asset->packed_source && view->kind>=QA_BOT_CHAT_MATCHES) return graph_dump(output,asset,error);
    if (view->kind == QA_BOT_CHAT_SYNONYMS) {
        for (size_t index = 0; index < view->group_count; ++index) {
            qa_bot_chat_synonyms group;
            if(!bot_chat_packed_group(asset,(uint32_t)index,&group,error)) return false;
            int32_t context;
            memcpy(&context, &group.context, sizeof(context));
            if (!write_format(output, error, "%d : [", context)) return false;
            for (uint32_t entry = 0; entry < group.entries.count; ++entry) {
                qa_bot_chat_synonym synonym;
                if(!bot_chat_packed_entry(asset,group.entries.first+entry,&synonym,error)) return false;
                char weight[64];
                if (!qa_format_fixed(synonym.weight, 2, weight, sizeof(weight), error) ||
                    !write_format(output, error, "(\"%s\", %s)", synonym.text, weight)) return false;
                if (entry + 1 < group.entries.count && !write_text(output, ", ", error)) return false;
            }
            if (!write_text(output, "]\n", error)) return false;
        }
    } else if (view->kind == QA_BOT_CHAT_RANDOMS) {
        for (size_t index = 0; index < view->list_count; ++index) {
            qa_bot_chat_list list=view->lists[index];const char *name;int32_t count;
            if(!bot_chat_packed_list(asset,(uint32_t)index,&name,&count,error) ||
               !write_format(output, error, "%s = {",name)) return false;
            for (uint32_t message = 0; message < list.messages.count; ++message) {
                const char *text;
                if(asset->packed_source) {
                    const bot_chat_packed_member *group=&asset->packed_source->groups[index];
                    if(!bot_chat_packed_message(asset,group->first+group->count-1-message,&text,error)) return false;
                } else text=view->messages[list.messages.first+message];
                if (!write_format(output, error, "\"%s\"",text) ||
                    !write_text(output, message + 1 < list.messages.count ? ", " : "}\n", error)) return false;
            }
        }
    } else if (view->kind == QA_BOT_CHAT_MATCHES) {
        for (size_t index = 0; index < view->template_count; ++index) {
            const qa_bot_chat_template *match = view->templates + index;
            if (!write_text(output, "{ ", error) || !pieces(output, view, match->pieces, false, error) ||
                !write_format(output, error, " = (%d, %d);}\n", match->type, match->subtype)) return false;
        }
    } else {
        if (!write_text(output, "BotDumpReplyChat:\n", error)) return false;
        for (size_t index = 0; index < view->reply_count; ++index) {
            const qa_bot_chat_reply *reply = view->replies + index;
            if (!reply_keys(output, view, reply, error)) return false;
            for (uint32_t message = 0; message < reply->messages.count; ++message)
                if (!write_format(output, error, "\t\"%s\";\n", view->messages[reply->messages.first + message])) return false;
            if (!write_text(output, "}\n", error)) return false;
        }
    }
    return true;
}

bool qa_bot_chat_dump_asset(qa_bot_log *log, qa_bot_chat_asset *asset, qa_error *error) {
    if (!asset || asset->view.kind == QA_BOT_CHAT_INITIAL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Chat dump requires an actual configuration asset");
        return false;
    }
    qa_bot_log_file *file = qa_bot_log_file_pointer(log);
    if (!file) return true;
    qa_bot_chat_asset_retain(asset);
    chat_log_output output = {.log = log, .file = file, .raw = true};
    bool okay = dump(&output,asset,error);
    qa_bot_chat_asset_release(asset);
    return okay;
}

bool qa_bot_chat_log_initial(qa_bot_log *log, qa_bot_chat_asset *asset, qa_error *error) {
    if (!asset || asset->view.kind != QA_BOT_CHAT_INITIAL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Initial chat log requires its actual source asset");
        return false;
    }
    if (!log) return true;
    if (!asset->initial_source || !asset->initial_source->published) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Initial chat log requires its actual published source asset");
        return false;
    }
    qa_bot_chat_asset_retain(asset);
    const bot_chat_initial *chat = &asset->initial_source->initial;
    chat_log_output output = {.log = log};
    uint32_t type = 0;
    bool okay = write_text(&output, "{", error) && bot_chat_initial_first(chat, &type, error);
    for (size_t seen = 0; okay && type; ++seen) {
        if (seen >= chat->type_count) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Initial chat log type list cycles");
            okay = false;
            break;
        }
        qa_bot_memory_span record;
        okay = bot_chat_initial_type(chat, type, &record, error);
        if (!okay) break;
        char name[33];
        memcpy(name, record.data, 32);
        name[32] = 0;
        int32_t count;
        uint32_t message = 0;
        okay = write_format(&output, error, " type \"%s\"", name) &&
            write_text(&output, " {", error) &&
            bot_chat_initial_type_count(chat, type, &count, error) &&
            write_format(&output, error, "  numchatmessages = %d", count) &&
            bot_chat_initial_type_first(chat, type, &message, error);
        for (size_t visited = 0; okay && message; ++visited) {
            if (visited >= chat->message_count) {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Initial chat log message list cycles");
                okay = false;
                break;
            }
            qa_bytes text;
            okay = bot_chat_initial_message_text(chat, message, &text, error) &&
                write_format(&output, error, "  \"%s\"", (const char *)text.data) &&
                bot_chat_initial_message_next(chat, message, &message, error);
        }
        if (okay) okay = write_text(&output, " }", error) &&
            bot_chat_initial_type_next(chat, type, &type, error);
    }
    if (okay) okay = write_text(&output, "}", error);
    qa_bot_chat_asset_release(asset);
    return okay;
}
