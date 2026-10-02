#include "internal.h"
#include "qa/text.h"

void chat_report(qa_bot_chat_system *s, qa_script_severity severity, const char *message) {
    if (s->services.diagnostic != NULL)
        s->services.diagnostic(s->services.context, severity, message);
}
bool chat_print(qa_bot_chat_system *system,qa_script_severity severity,const char *message,
    qa_error *error) {
    if(system->services.report)
        return system->services.report(system->services.context,severity,message,error);
    chat_report(system,severity,message);return true;
}
bool chat_source_time(qa_bot_chat_system *system,float fallback,float *out,qa_error *error) {
    if(system->services.time) {
        if(!system->services.time(system->services.context,out,error)) return false;
    } else *out=fallback;
    if(!isfinite(*out)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Bot chat source time is not a finite float");return false;
    }
    return true;
}
static bool kind(const qa_bot_chat_asset *a, qa_bot_chat_asset_kind expected) {
    return a == NULL || a->view.kind == expected;
}
bool qa_bot_chat_system_create(const qa_bot_chat_services *services,
                               const qa_bot_chat_options *options, qa_bot_chat_system **out,
                               qa_error *e) {
    if (services == NULL || services->random.next == NULL || options == NULL || out == NULL ||
        options->console_capacity >= UINT32_MAX || !kind(options->synonyms, QA_BOT_CHAT_SYNONYMS) ||
        !kind(options->randoms, QA_BOT_CHAT_RANDOMS) ||
        !kind(options->matches, QA_BOT_CHAT_MATCHES) ||
        !kind(options->replies, QA_BOT_CHAT_REPLIES)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid native bot chat services/assets");
        return false;
    }
    qa_bot_chat_system *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating shared bot chat system");
        return false;
    }
    if(!qa_bot_memory_create(NULL,&s->memory,e)) {free(s);return false;}
    s->services = *services;
    s->options = *options;
    s->free_console = 0;
    s->references = 1;
    s->revision = 1;
    qa_bot_chat_asset_retain(options->synonyms);
    qa_bot_chat_asset_retain(options->randoms);
    qa_bot_chat_asset_retain(options->matches);
    qa_bot_chat_asset_retain(options->replies);
    if(!options->console_unavailable && options->console_capacity &&
       !chat_console_heap(s,(uint32_t)options->console_capacity,true,e)) {
        chat_system_release(s);return false;
    }
    *out = s;
    return true;
}
void chat_system_release(qa_bot_chat_system *s) {
    if (--s->references != 0)
        return;
    qa_bot_chat_asset_release(s->options.synonyms);
    qa_bot_chat_asset_release(s->options.randoms);
    qa_bot_chat_asset_release(s->options.matches);
    qa_bot_chat_asset_release(s->options.replies);
    free(s->console);
    (void)qa_bot_memory_release(s->memory,NULL);
    free(s);
}
void qa_bot_chat_system_destroy(qa_bot_chat_system *s) {
    if (s == NULL || s->retired || s->restoring)
        return;
    s->retired = true;
    while (s->states != NULL)
        qa_bot_chat_destroy(s->states);
    chat_system_release(s);
}
bool qa_bot_chat_system_active(const qa_bot_chat_system *s) {
    if (!s) return false;
    if (s->restoring) return true;
    size_t references = 1;
    for (const qa_bot_chat *state = s->states; state; state = state->next) {
        if (state->references != 1) return true;
        ++references;
    }
    return s->references != references;
}
bool qa_bot_chat_system_configure(qa_bot_chat_system *s, const qa_bot_chat_options *o,
                                  qa_error *e) {
    if (!s || s->retired || s->restoring || s->revision == UINT64_MAX || !o || o->console_capacity >= UINT32_MAX ||
        !kind(o->synonyms, QA_BOT_CHAT_SYNONYMS) || !kind(o->randoms, QA_BOT_CHAT_RANDOMS) ||
        !kind(o->matches, QA_BOT_CHAT_MATCHES) || !kind(o->replies, QA_BOT_CHAT_REPLIES)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat configuration");
        return false;
    }
    if(!o->console_unavailable && o->console_capacity &&
       (!s->console_heap.owner || s->options.console_capacity!=o->console_capacity)) {
        if(s->console_heap.owner && !qa_bot_memory_free(s->memory,s->console_heap,e)) return false;
        if(!chat_console_heap(s,(uint32_t)o->console_capacity,true,e)) return false;
    }
    qa_bot_chat_asset_retain(o->synonyms);
    qa_bot_chat_asset_retain(o->randoms);
    qa_bot_chat_asset_retain(o->matches);
    qa_bot_chat_asset_retain(o->replies);
    qa_bot_chat_options previous = s->options;
    s->options = *o;
    ++s->revision;
    qa_bot_chat_asset_release(previous.synonyms);
    qa_bot_chat_asset_release(previous.randoms);
    qa_bot_chat_asset_release(previous.matches);
    qa_bot_chat_asset_release(previous.replies);
    return true;
}
bool qa_bot_chat_create(qa_bot_chat_system *system, int32_t client, qa_bot_chat **out,
                        qa_error *e) {
    if (system == NULL || system->retired || system->restoring || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing bot chat system/output");
        return false;
    }
    qa_bot_chat *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating bot chat state");
        return false;
    }
    s->system = system;
    if(!qa_bot_memory_allocate(system->memory,CHAT_STATE_BYTES,QA_BOT_MEMORY_HEAP,true,NULL,&s->allocation,e)) {
        free(s);return false;
    }
    if(!chat_state_set(s,CHAT_CLIENT,(uint32_t)client,e)) {free(s);return false;}
    s->references = 1;
    s->initial_revision = 1;
    ++system->references;
    s->next = system->states;
    if (s->next != NULL)
        s->next->previous = s;
    system->states = s;
    *out = s;
    return true;
}
void qa_bot_chat_destroy(qa_bot_chat *s) {
    if (s == NULL || s->retired || s->system->restoring)
        return;
    if (s->next != NULL)
        s->next->previous = s->previous;
    if (s->previous != NULL)
        s->previous->next = s->next;
    else
        s->system->states = s->next;
    s->retired = true;
    chat_release(s);
}
bool qa_bot_chat_free(qa_bot_chat *state,qa_error *error) {
    if(!state || state->retired || state->system->restoring) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat Free requires its actual live state");return false;
    }
    chat_retain(state);qa_bot_chat_system *system=state->system;uint64_t revision=system->revision;
    bool reload=system->services.reload_characters?
        system->services.reload_characters(system->services.context):
        system->library?bot_reload_characters(system->library):false;
    bool ok=true;
    if(!state->retired && !system->retired && system->revision==revision) {
        if(reload) ok=chat_initial_free(state,error);
        qa_bot_console_message first;bool found=false,removed;
        while(ok) {
            ok=qa_bot_chat_console_first_source(state,&first,&found,error);
            if(!ok || !found || !first.handle) break;
            ok=qa_bot_chat_console_remove_source(state,first.handle,&removed,error);
            if(ok && !removed) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source console Free could not unlink the reached handle");ok=false;}
        }
        if(ok) ok=qa_bot_memory_free(system->memory,state->allocation,error);
        if(ok) qa_bot_chat_destroy(state);
    }
    chat_release(state);return ok;
}
void chat_retain(qa_bot_chat *s) { ++s->references; }
void chat_release(qa_bot_chat *s) {
    if (--s->references != 0)
        return;
    qa_bot_chat_system *system = s->system;
    qa_bot_chat_asset_release(s->initial);
    free(s);
    chat_system_release(system);
}
bool qa_bot_chat_set_initial(qa_bot_chat *s, qa_bot_chat_asset *asset, qa_error *e) {
    if (s == NULL || s->retired || s->system->restoring || s->initial_revision == UINT64_MAX ||
        !kind(asset, QA_BOT_CHAT_INITIAL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid initial bot chat asset/state");
        return false;
    }
    qa_bot_chat_asset_retain(asset);
    uint32_t pointer=asset && asset->initial_source?asset->initial_source->initial.pointer:0;
    if(asset && !pointer) {
        qa_bot_chat_asset_release(asset);qa_error_set(e,QA_ERROR_ARGUMENT,0,"Initial chat publication requires its true source pointer");return false;
    }
    if(!chat_state_set(s,CHAT_INITIAL,pointer,e)) {qa_bot_chat_asset_release(asset);return false;}
    qa_bot_chat_asset_release(s->initial);
    s->initial = asset;
    ++s->initial_revision;
    return true;
}
void qa_bot_chat_set_name(qa_bot_chat *s, const char *name, int32_t client) {
    qa_bot_chat_set_identity(s, name, &client);
}
void qa_bot_chat_set_identity(qa_bot_chat *s, const char *name, const int32_t *client) {
    qa_bot_chat_text_source source=chat_text_source(name?name:"");
    (void)qa_bot_chat_set_identity_from(s,&source,client,NULL);
}
bool qa_bot_chat_set_identity_from(qa_bot_chat *state,const qa_bot_chat_text_source *source,
    const int32_t *client,qa_error *error) {
    if(!state || state->retired || state->system->restoring || !source || !source->read) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat identity requires its actual state/text source");return false;
    }
    qa_bot_memory_span span;if(!chat_state_span(state,&span,error)) return false;
    if(client) chat_raw_store(span.data+CHAT_CLIENT,(uint32_t)*client);
    memset(span.data+CHAT_NAME,0,32);
    chat_retain(state);qa_bytes text;
    bool ok=chat_text_read(source,32,&text,error) && chat_state_span(state,&span,error);
    if(ok) {
        size_t size=text.size>31?31:text.size;
        if(size) memmove(span.data+CHAT_NAME,text.data,size);
        memset(span.data+CHAT_NAME+size,0,32-size);
    }
    chat_release(state);return ok;
}
void qa_bot_chat_set_gender(qa_bot_chat *s, uint32_t gender) {
    if (s != NULL && !s->retired && !s->system->restoring)
        (void)chat_state_set(s,CHAT_GENDER,gender == 1 || gender == 2 ? gender : 0,NULL);
}
const char *qa_bot_chat_message(const qa_bot_chat *s) {
    const char *message=NULL;return s && qa_bot_chat_message_source(s,&message,NULL)?message:"";
}
bool qa_bot_chat_message_source(const qa_bot_chat *state,const char **out,qa_error *error) {
    char *message;if(!out || !chat_state_text(state,CHAT_MESSAGE,256,&message,error)) return false;
    *out=message;return true;
}
bool qa_bot_chat_write_message(qa_bot_chat *s, void *context,
                               bool (*write)(void *, const char *, qa_error *), qa_error *e) {
    if (s == NULL || s->retired || s->system->restoring || write == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat output");
        return false;
    }
    char *message;
    if(!chat_state_message_strip(s,e) || !chat_state_text(s,CHAT_MESSAGE,256,&message,e)) return false;
    chat_retain(s);
    bool ok = write(context, message, e);
    if(ok && !s->retired) ok=chat_state_message_clear(s,e);
    chat_release(s);
    return ok;
}
typedef struct chat_output {
    char *text;
    size_t capacity;
} chat_output;
static bool write_text(void *context, const char *message, qa_error *e) {
    chat_output *output = context;
    if (output->text == NULL || output->capacity == 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat output");
        return false;
    }
    chat_copy(output->text, output->capacity, message);
    return true;
}
bool qa_bot_chat_take_message(qa_bot_chat *s, char *out, size_t capacity, qa_error *e) {
    chat_output output = {out, capacity};
    return qa_bot_chat_write_message(s, &output, write_text, e);
}
bool qa_bot_chat_enter(qa_bot_chat *s, int32_t recipient, qa_bot_chat_destination destination,
                       qa_error *e) {
    return qa_bot_chat_enter_from(s, NULL, recipient, destination, e);
}
bool qa_bot_chat_enter_from(qa_bot_chat *s, const int32_t *source_client, int32_t recipient,
                            qa_bot_chat_destination destination, qa_error *e) {
    if (s == NULL || s->retired || s->system->restoring) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Missing bot chat command service or invalid destination");
        return false;
    }
    char *message;
    if(!chat_state_text(s,CHAT_MESSAGE,256,&message,e)) return false;
    if (message[0] == 0)
        return true;
    if(!chat_state_message_strip(s,e)) return false;
    qa_bot_chat_services services = s->system->services;
    chat_retain(s);
    bool test = services.test_initial != NULL && services.test_initial(services.context);
    if(s->retired || !chat_state_text(s,CHAT_MESSAGE,256,&message,e)) {
        chat_release(s);return false;
    }
    if (!test && services.command == NULL) {
        chat_release(s);
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing bot chat command service");
        return false;
    }
    bool ok = true;
    if (test) {
        ok=chat_state_text(s,CHAT_MESSAGE,256,&message,e) && chat_print(s->system,QA_SCRIPT_INFO,message,e);
    } else {
        char command[304];
        const char *prefix=destination==QA_BOT_CHAT_TEAM?"say_team ":
            destination==QA_BOT_CHAT_TELL?"tell ":"say ";
        size_t length=strlen(prefix);memcpy(command,prefix,length);
        if(destination==QA_BOT_CHAT_TELL) {
            char number[32];
            ok=qa_format_number(recipient,number,e);
            if(ok) {
                size_t size=strlen(number);memcpy(command+length,number,size);
                length+=size;command[length++]=' ';
            }
        }
        if(ok) memcpy(command+length,message,strlen(message)+1);
        uint32_t client;
        if(ok) ok=chat_state_get(s,CHAT_CLIENT,&client,e);
        if(ok) {
            int32_t signed_client;memcpy(&signed_client,&client,4);
            ok=services.command(services.context,source_client?*source_client:signed_client,command,e);
        }
    }
    if(ok && !s->retired) ok=chat_state_message_clear(s,e);
    chat_release(s);
    return ok;
}
bool chat_reserve_console(qa_bot_chat_system *s, size_t count, qa_error *e) {
    size_t available=0;uint32_t pointer=s->free_console;
    while(pointer) {
        if(++available>s->console_capacity || !chat_console_get(s,pointer,272,&pointer,e)) {
            if(e && e->code==QA_OK) qa_error_set(e,QA_ERROR_ARGUMENT,0,"Source console free list cycles");
            return false;
        }
    }
    if(count<=s->console_count+available) return true;
    if(s->options.console_unavailable || s->options.console_capacity) {
        qa_error_set(e,QA_ERROR_MEMORY,count,"Source console heap has insufficient free cells");return false;
    }
    size_t additional=count-s->console_count-available;
    if(additional>UINT32_MAX) {qa_error_set(e,QA_ERROR_MEMORY,0,"Source console heap size exceeds its word extent");return false;}
    return chat_console_heap(s,(uint32_t)additional,false,e);
}
bool qa_bot_chat_console_queue(qa_bot_chat *s, int32_t type, const char *text, float time,
                               uint32_t *handle, qa_error *e) {
    if(!text) {qa_error_set(e,QA_ERROR_ARGUMENT,0,"Source console text is NULL");return false;}
    qa_bot_chat_text_source source=chat_text_source(text);
    return qa_bot_chat_console_queue_from(s,type,&source,time,handle,e);
}
bool qa_bot_chat_console_queue_from(qa_bot_chat *s,int32_t type,const qa_bot_chat_text_source *source,float time,
    uint32_t *handle,qa_error *e) {
    if(handle) *handle=0;
    if (s == NULL || s->retired || s->system->restoring || !source || !source->read || !isfinite(time)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot console message");
        return false;
    }
    qa_bot_chat_system *system = s->system;
    if(!system->free_console && !system->options.console_unavailable && !system->options.console_capacity)
        if(!chat_reserve_console(system,system->console_count+16,e)) return false;
    if (system->options.console_unavailable || !system->free_console) {
        if (handle != NULL)
            *handle = 0;
        return chat_print(system, QA_SCRIPT_ERROR, "empty console message heap",e);
    }
    uint32_t index = system->free_console;
    uint32_t next,last,count,source_handle;
    if(!chat_console_get(system,index,272,&next,e)) return false;
    system->free_console=next;
    if(next && !chat_console_set(system,next,268,0,e)) return false;
    if(!chat_state_get(s,CHAT_HANDLE,&source_handle,e)) return false;
    ++source_handle;
    int32_t signed_handle;memcpy(&signed_handle,&source_handle,4);
    if(signed_handle<=0 || signed_handle>8192) source_handle=1;
    if(!chat_state_set(s,CHAT_HANDLE,source_handle,e) || !chat_console_set(system,index,0,source_handle,e)) return false;
    chat_retain(s);
    uint64_t revision=system->revision;
    bool ok=chat_source_time(system,time,&time,e);
    if(ok && (system->revision!=revision || s->retired)) {chat_release(s);return true;}
    qa_bot_memory_span span;
    if(ok) ok=chat_console_span(system,index,&span,e);
    if(ok) {
        uint32_t bits;memcpy(&bits,&time,4);
        chat_raw_store(span.data+4,bits);
        chat_raw_store(span.data+8,(uint32_t)type);
        qa_bytes text;ok=chat_text_read(source,256,&text,e);
        if(ok) ok=chat_console_span(system,index,&span,e);
        if(ok) {
            if(text.size) memmove(span.data+12,text.data,text.size);
            if(text.size<256) memset(span.data+12+text.size,0,256-text.size);
            chat_raw_store(span.data+272,0);ok=chat_state_get(s,CHAT_LAST,&last,e);
        }
    }
    if(ok) ok=chat_console_set(system,index,268,last,e);
    if(ok) ok=last?chat_console_set(system,last,272,index,e):chat_state_set(s,CHAT_FIRST,index,e);
    if(ok) ok=chat_state_set(s,CHAT_LAST,index,e) && chat_state_get(s,CHAT_COUNT,&count,e) &&
        chat_state_set(s,CHAT_COUNT,count+1,e);
    if(!ok) {chat_release(s);return false;}
    ++system->console_count;
    if (handle != NULL)
        *handle = source_handle;
    chat_release(s);
    return true;
}
bool qa_bot_chat_console_first(const qa_bot_chat *s, qa_bot_console_message *out) {
    bool found=false;return qa_bot_chat_console_first_source(s,out,&found,NULL) && found;
}
bool qa_bot_chat_console_first_source(const qa_bot_chat *s,qa_bot_console_message *out,bool *found,qa_error *error) {
    uint32_t pointer;qa_bot_memory_span span;
    if(!found || !s || !out || !chat_state_get(s,CHAT_FIRST,&pointer,error)) return false;
    *found=false;if(!pointer) return true;
    if(!chat_console_span(s->system,pointer,&span,error)) return false;
    uint32_t bits=chat_raw_word(span.data+4);float time;memcpy(&time,&bits,4);
    int32_t type;bits=chat_raw_word(span.data+8);memcpy(&type,&bits,4);
    *out=(qa_bot_console_message){.handle=chat_raw_word(span.data),.time=time,.type=type};
    memcpy(out->text,span.data+12,256);
    *found=true;return true;
}
bool qa_bot_chat_console_remove(qa_bot_chat *s, uint32_t handle) {
    bool removed=false;return qa_bot_chat_console_remove_source(s,handle,&removed,NULL) && removed;
}
bool qa_bot_chat_console_remove_source(qa_bot_chat *s,uint32_t handle,bool *removed,qa_error *error) {
    if (s == NULL || s->system->restoring || !removed)
        return false;
    *removed=false;
    qa_bot_chat_system *system = s->system;
    uint32_t pointer;
    if(!chat_state_get(s,CHAT_FIRST,&pointer,error)) return false;
    for(size_t visited=0;pointer;++visited) {
        uint32_t current,next,previous,count;
        if(visited>=system->console_capacity) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source console list cycles");return false;}
        if(!chat_console_get(system,pointer,0,&current,error) || !chat_console_get(system,pointer,272,&next,error)) return false;
        if(current!=handle) {pointer=next;continue;}
        if(!chat_console_get(system,pointer,268,&previous,error)) return false;
        if(!(next?chat_console_set(system,next,268,previous,error):chat_state_set(s,CHAT_LAST,previous,error)) ||
           !(previous?chat_console_set(system,previous,272,next,error):chat_state_set(s,CHAT_FIRST,next,error))) return false;
        if(system->free_console && !chat_console_set(system,system->free_console,268,pointer,error)) return false;
        if(!chat_console_set(system,pointer,268,0,error) || !chat_console_set(system,pointer,272,system->free_console,error)) return false;
        system->free_console=pointer;
        if(!chat_state_get(s,CHAT_COUNT,&count,error) || !chat_state_set(s,CHAT_COUNT,count-1,error)) return false;
        if(system->console_count) --system->console_count;
        *removed=true;return true;
    }
    return true;
}
size_t qa_bot_chat_console_count(const qa_bot_chat *s) {
    uint32_t count=0;return s && chat_state_get(s,CHAT_COUNT,&count,NULL)?count:0;
}
bool qa_bot_chat_console_count_source(const qa_bot_chat *state,int32_t *out,qa_error *error) {
    uint32_t count;if(!out || !chat_state_get(state,CHAT_COUNT,&count,error)) return false;
    memcpy(out,&count,4);return true;
}
