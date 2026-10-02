#include "internal.h"

static bool text_read(void *context,size_t maximum,qa_bytes *out,qa_error *error) {
    (void)error;const char *text=context;size_t size=0;
    while(size<maximum && text[size]) ++size;
    *out=(qa_bytes){(const uint8_t *)text,size};return true;
}
qa_bot_chat_text_source chat_text_source(const char *text) {
    return (qa_bot_chat_text_source){(void *)text,text_read};
}
bool chat_text_read(const qa_bot_chat_text_source *source,size_t maximum,qa_bytes *out,
    qa_error *error) {
    if(!source || !source->read || !out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat argument requires its actual source callback/output");return false;
    }
    if(!source->read(source->context,maximum,out,error)) return false;
    if(out->size && !out->data) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat source callback returned no bytes");return false;
    }
    const uint8_t *end=out->size?memchr(out->data,0,out->size):NULL;
    if(end) out->size=(size_t)(end-out->data);
    if(out->size>maximum) out->size=maximum;
    return true;
}
bool chat_text_copy(const qa_bot_chat_text_source *source,size_t maximum,char **out,
    qa_error *error) {
    qa_bytes bytes;if(!chat_text_read(source,maximum,&bytes,error)) return false;
    if(bytes.size==SIZE_MAX) {qa_error_set(error,QA_ERROR_MEMORY,0,"Chat argument exceeds native text extent");return false;}
    char *copy=malloc(bytes.size+1);
    if(!copy) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining reached chat source argument");return false;}
    if(bytes.size) memcpy(copy,bytes.data,bytes.size);
    copy[bytes.size]=0;*out=copy;return true;
}

static bool current(void *context,qa_error *error) {
    qa_bot_chat_asset *asset=context;
    if(!asset->source_library || qa_bot_memory_disposed(asset->source_library->memory)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat library MEMORY is no longer live");return false;
    }
    qa_bot_chat *state=asset->loading_state;
    if(state && (state->retired || state->system->retired ||
        state->initial_revision!=asset->loading_revision)) {
        if(asset->initial_source) asset->initial_source->retired_abort=true;
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat load was retired by its source callback");return false;
    }
    return true;
}
static bool report(void *context,bot_chat_initial_diagnostic_origin origin,
    const qa_script_diagnostic *issue,qa_error *error) {
    qa_bot_chat_asset *asset=context;
    if(origin==BOT_CHAT_INITIAL_SOURCE_DIAGNOSTIC) return true;
    qa_bot_log *log=asset->source_library->log;
    if(log) return qa_bot_log_print(log,issue->severity,issue->message,error);
    const qa_script_services *services=&asset->source_library->options.scripts;
    if(services->diagnostic) services->diagnostic(services->context,issue);
    return true;
}
static bool name_equal(void *context,qa_bytes name,bool *out,qa_error *error) {
    qa_bot_chat_asset *asset=context;
    if(!current(asset,error)) return false;
    *out=true;
    for(size_t index=0;*out && index<=name.size;++index) {
        qa_bytes value;
        if(!chat_text_read(asset->loading_name,index+1,&value,error)) {
            asset->initial_source->service_failed=true;return false;
        }
        if(!current(asset,error)) return false;
        unsigned char first=index<name.size?name.data[index]:0,second=index<value.size?value.data[index]:0;
        if(first>='a' && first<='z') first=(unsigned char)(first-32);
        if(second>='a' && second<='z') second=(unsigned char)(second-32);
        *out=first==second;
    }
    return true;
}
static bool path(void *context,const char **out,qa_error *error) {
    qa_bot_chat_asset *asset=context;
    if(!current(asset,error)) return false;
    qa_bytes bytes;
    if(!chat_text_read(asset->loading_path,SIZE_MAX,&bytes,error)) {
        asset->initial_source->service_failed=true;return false;
    }
    if(!current(asset,error)) return false;
    const char *text=bot_string(&asset->arena,bytes,error);
    if(!text) {asset->initial_source->own_failure=true;return false;}
    asset->view.path=text;*out=text;return true;
}
static bool identity(void *context,uint32_t *out,qa_error *error) {
    qa_bot_chat_asset *asset=context;
    if(!current(asset,error)) return false;
    uint32_t highest=0;
    for(qa_bot_chat_asset *row=asset->source_library->chat_assets;row;row=row->next)
        if(row->initial_source && row->initial_source->published &&
           row->initial_source->initial.pointer>highest) highest=row->initial_source->initial.pointer;
    if(highest==UINT32_MAX) {
        asset->initial_source->own_failure=true;
        qa_error_set(error,QA_ERROR_MEMORY,0,"Initial chat pointer map exceeds source word extent");return false;
    }
    *out=highest+1;return true;
}
static bool publish(void *context,bot_chat_initial *initial,qa_error *error) {
    qa_bot_chat_asset *asset=context;
    if(!current(asset,error)) return false;
    return initial==&asset->initial_source->initial;
}
bool chat_initial_asset_owner(qa_bot_chat_asset *asset,qa_bot_library *library,
    qa_bot_memory *memory,qa_error *error) {
    if(!asset || asset->initial_source || !memory || (library && library->memory!=memory)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat resource factory has another actual owner");return false;
    }
    asset->source_library=library;
    if(!library) return bot_chat_initial_resource_pure(memory,&asset->initial_source,error);
    bot_chat_initial_resource_host host={asset,current,report,name_equal,path,identity,publish};
    return bot_chat_initial_resource_create(memory,&library->options.scripts,
        &library->options.preprocessor,&host,&asset->initial_source,error);
}

bool chat_initial_asset_load(qa_bot_library *library,const char *path_text,const char *name,
    qa_bot_chat *state,uint64_t revision,qa_bot_chat_asset **out,bool *source_failure,
    qa_error *error) {
    if(!path_text || !name) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat load requires its actual library/text/output");return false;
    }
    qa_bot_chat_text_source path_source=chat_text_source(path_text),name_source=chat_text_source(name);
    bool ok=chat_initial_asset_load_from(library,&path_source,&name_source,state,revision,out,source_failure,error);
    if(ok) {
        (*out)->view.name=bot_string(&(*out)->arena,(qa_bytes){(const uint8_t *)name,strlen(name)},error);
        ok=(*out)->view.name!=NULL;
    }
    return ok;
}
bool chat_initial_asset_load_from(qa_bot_library *library,const qa_bot_chat_text_source *path_source,
    const qa_bot_chat_text_source *name_source,qa_bot_chat *state,uint64_t revision,
    qa_bot_chat_asset **out,bool *source_failure,qa_error *error) {
    if(!library || !path_source || !path_source->read || !name_source || !name_source->read || !out || !source_failure) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat load requires actual source argument callbacks/output");return false;
    }
    *out=NULL;*source_failure=false;
    qa_bot_chat_asset *asset=NULL;
    if(!chat_asset_allocate(QA_BOT_CHAT_INITIAL,"","",&asset,error)) return false;
    asset->loading_path=path_source;asset->loading_name=name_source;
    asset->source_library=library;asset->loading_state=state;asset->loading_revision=revision;
    if(!chat_initial_asset_owner(asset,library,library->memory,error)) {
        qa_bot_chat_asset_release(asset);return false;
    }
    /* The library owns the reader before the first resolver callback. Failed
     * loads retain exactly the reached PC acquisition and heap stages. */
    asset->next=library->chat_assets;library->chat_assets=asset;
    qa_bot_chat_asset_retain(asset);*out=asset;
    bool ok=bot_chat_initial_resource_load(asset->initial_source,source_failure,error);
    if(ok) {asset->source_loaded=true;ok=chat_initial_asset_refresh(asset,error);}
    asset->loading_state=NULL;asset->loading_path=asset->loading_name=NULL;return ok;
}

bool chat_initial_asset_refresh(qa_bot_chat_asset *asset,qa_error *error) {
    if(!asset || !asset->initial_source || !asset->initial_source->published) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat projection requires its actual published source view");return false;
    }
    bot_chat_initial *chat=&asset->initial_source->initial;
    qa_arena_destroy(&asset->projection_arena);
    asset->view.list_count=asset->view.message_count=0;
    uint32_t type;
    if(!bot_chat_initial_first(chat,&type,error)) return false;
    for(size_t visited=0;type;++visited) {
        if(visited>=chat->type_count) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"Initial chat type links contain a cycle");return false;
        }
        qa_bot_memory_span cell;
        if(!bot_chat_initial_type(chat,type,&cell,error)) return false;
        size_t name_length=0;while(name_length<32 && cell.data[name_length]) ++name_length;
        const char *name=bot_string(&asset->projection_arena,(qa_bytes){cell.data,name_length},error);
        if(!name) return false;
        qa_bot_chat_list list={.name=name,
            .messages={.first=(uint32_t)asset->view.message_count}};
        uint32_t message;
        if(!bot_chat_initial_type_first(chat,type,&message,error)) return false;
        for(size_t seen=0;message;++seen) {
            if(seen>=chat->message_count) {
                qa_error_set(error,QA_ERROR_FORMAT,0,"Initial chat message links contain a cycle");return false;
            }
            qa_bytes text;
            if(!bot_chat_initial_message_text(chat,message,&text,error)) return false;
            size_t index=asset->view.message_count;
            if(!bot_grow((void **)&asset->messages,&asset->message_capacity,index+1,
                sizeof(*asset->messages),error)) return false;
            uint32_t *ids=realloc(asset->initial_messages,(index+1)*sizeof(*ids));
            if(!ids) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining initial chat projection pointers");return false;}
            asset->initial_messages=ids;ids[index]=message;
            asset->messages[index]=(const char *)text.data;++asset->view.message_count;
            if(!bot_chat_initial_message_next(chat,message,&message,error)) return false;
        }
        list.messages.count=(uint32_t)(asset->view.message_count-list.messages.first);
        size_t index=asset->view.list_count;
        if(!bot_grow((void **)&asset->lists,&asset->list_capacity,index+1,sizeof(*asset->lists),error)) return false;
        uint32_t *ids=realloc(asset->initial_types,(index+1)*sizeof(*ids));
        if(!ids) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining initial chat type projection pointers");return false;}
        asset->initial_types=ids;ids[index]=type;asset->lists[index]=list;++asset->view.list_count;
        if(!bot_chat_initial_type_next(chat,type,&type,error)) return false;
    }
    chat_asset_view(asset);return true;
}
bool chat_initial_type_find(const qa_bot_chat_asset *asset,const char *name,uint32_t *out,
    qa_error *error) {
    qa_bot_chat_text_source source=chat_text_source(name);
    return chat_initial_type_find_from(asset,name?&source:NULL,out,error);
}
bool chat_initial_type_find_from(const qa_bot_chat_asset *asset,const qa_bot_chat_text_source *name,
    uint32_t *out,qa_error *error) {
    *out=0;
    if(!asset || !name || !asset->initial_source) return true;
    const bot_chat_initial *chat=&asset->initial_source->initial;
    uint32_t type;if(!bot_chat_initial_first(chat,&type,error)) return false;
    for(size_t visited=0;type;++visited) {
        if(visited>=chat->type_count) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"Initial chat type links contain a cycle");return false;
        }
        qa_bot_memory_span cell;if(!bot_chat_initial_type(chat,type,&cell,error)) return false;
        uint8_t expected[32];memcpy(expected,cell.data,32);bool same=true;
        size_t length=0;while(length<32 && expected[length]) ++length;
        for(size_t index=0;index<=length;++index) {
            qa_bytes value;if(!chat_text_read(name,index+1,&value,error)) return false;
            uint8_t first=index<length?expected[index]:0,second=index<value.size?value.data[index]:0;
            if(first>='a' && first<='z') first=(uint8_t)(first-32);
            if(second>='a' && second<='z') second=(uint8_t)(second-32);
            if(first!=second) {same=false;break;}
            if(!first) break;
        }
        if(same) {*out=type;return true;}
        if(!bot_chat_initial_type_next(chat,type,&type,error)) return false;
    }
    return true;
}
bool chat_asset_message_time(qa_bot_chat_asset *asset,uint32_t index,float *time,bool write,
    qa_error *error) {
    if(!asset || index>=asset->view.message_count) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat message index has no actual source view");return false;
    }
    if(asset->initial_source)
        return bot_chat_initial_message_time(&asset->initial_source->initial,
            asset->initial_messages[index],time,write,error);
    if(asset->packed_source && asset->view.kind==QA_BOT_CHAT_REPLIES)
        return bot_chat_graph_message_time(asset,asset->graph_messages[index],time,write,error);
    if(write) asset->cooldowns[index]=*time;
    else *time=asset->cooldowns[index];
    return true;
}
bool chat_asset_message_text(const qa_bot_chat_asset *asset,uint32_t index,const char **out,
    qa_error *error) {
    if(!asset || index>=asset->view.message_count || !out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat message text requires its actual member/output");return false;
    }
    if(asset->initial_source) {
        qa_bytes text;
        if(!bot_chat_initial_message_text(&asset->initial_source->initial,
            asset->initial_messages[index],&text,error)) return false;
        *out=(const char *)text.data;
    } else if(asset->packed_source && asset->view.kind==QA_BOT_CHAT_REPLIES)
        return bot_chat_graph_message_text(asset,asset->graph_messages[index],out,error);
    else *out=asset->messages[index];
    return true;
}
bool chat_initial_asset_from_view(const qa_bot_chat_asset_view *view,qa_bot_chat_asset **out,
    qa_error *error) {
    qa_bot_chat_asset *asset=NULL;qa_bot_memory *memory=NULL;
    size_t size=4;
    for(size_t index=0;index<view->list_count;++index) {
        const qa_bot_chat_list *type=&view->lists[index];
        if(size>UINT32_MAX-44) goto invalid;
        size+=44;
        for(uint32_t row=0;row<type->messages.count;++row) {
            size_t length=strlen(view->messages[type->messages.first+row]);
            if(size>UINT32_MAX-13 || length>UINT32_MAX-size-13) goto invalid;
            size+=13+length;
        }
    }
    bool ok=chat_asset_allocate(QA_BOT_CHAT_INITIAL,view->path,view->name,&asset,error) &&
        qa_bot_memory_create(NULL,&memory,error) && chat_initial_asset_owner(asset,NULL,memory,error);
    if(memory) (void)qa_bot_memory_release(memory,NULL);
    if(ok) ok=bot_chat_initial_allocate(asset->initial_source->memory,(uint32_t)size,1,
        &asset->initial_source->initial,error);
    uint32_t offset=4;
    for(size_t index=view->list_count;ok && index;) {
        const qa_bot_chat_list *list=&view->lists[--index];uint32_t type;
        ok=bot_chat_initial_type_add(&asset->initial_source->initial,offset,
            (qa_bytes){(const uint8_t *)list->name,strlen(list->name)},&type,error);
        offset+=44;
        for(uint32_t row=list->messages.count;ok && row;) {
            const char *text=view->messages[list->messages.first+--row];uint32_t message;
            ok=bot_chat_initial_message_add(&asset->initial_source->initial,type,offset,&message,error);
            offset+=12;
            if(ok) ok=bot_chat_initial_message_write(&asset->initial_source->initial,type,offset,
                (qa_bytes){(const uint8_t *)text,strlen(text)},error);
            offset+=(uint32_t)strlen(text)+1;
        }
    }
    if(ok) {
        bot_chat_initial_resource *resource=asset->initial_source;
        resource->attempted=resource->published=resource->loaded=true;
        resource->size=(uint32_t)size;resource->pass=2;asset->source_loaded=true;
        ok=chat_initial_asset_refresh(asset,error);
    }
    if(ok) {*out=asset;return true;}
    qa_bot_chat_asset_release(asset);return false;
invalid:
    qa_error_set(error,QA_ERROR_FORMAT,0,"Initial chat value layout exceeds source allocation extent");return false;
}
