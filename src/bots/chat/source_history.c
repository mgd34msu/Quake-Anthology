#include "internal.h"
#include "source_history.h"
#include "../runtime/internal.h"

typedef struct chat_resource_image {
    qa_bot_chat_asset *asset;
    bot_chat_initial_resource *resource;
    bot_chat_packed *packed;
    struct chat_resource_image *next;
} chat_resource_image;
typedef struct chat_memory_image {
    qa_bot_memory *owner;
    qa_bot_memory_checkpoint *checkpoint;
    qa_bot_memory_prepared *prepared;
    struct chat_memory_image *next;
} chat_memory_image;
struct bot_chat_history {
    qa_bot_runtime *owner;
    qa_bot_chat_system system;
    qa_bot_chat *handles[64];
    qa_bot_chat_asset **assets;
    size_t asset_count;
    chat_resource_image *resources;
    chat_memory_image *external;
};
struct bot_chat_history_restore {
    qa_bot_runtime *owner;
    bot_chat_history *image;
};
static bool fail(qa_error *error,const char *text) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",text);return false;}
static char *copy(const char *text,qa_error *error) {
    if(!text) return NULL;
    size_t size=strlen(text)+1;char *result=malloc(size);
    if(!result) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source chat checkpoint text");return NULL;}
    memcpy(result,text,size);return result;
}
static bool resource_copy(const bot_chat_initial_resource *source,const qa_bot_memory_prepared *memory,
    bot_chat_initial_resource **out,qa_error *error) {
    if(source->active) return fail(error,"Cannot checkpoint an entered chat parser");
    bot_chat_initial_resource *target=NULL;
    if(!bot_chat_initial_resource_pure(source->memory,&target,error)) return false;
    *target=*source;
    target->path=target->include_path=target->date=target->time=NULL;
    target->reader=NULL;target->pending=NULL;target->initial.types=target->initial.messages=NULL;
    if(target->options.globals) qa_script_defines_retain((qa_script_defines *)target->options.globals);
    bool ok=true;
#define TEXT(field) do {if(source->field && !(target->field=copy(source->field,error))) ok=false;} while(0)
    TEXT(path);TEXT(include_path);TEXT(date);TEXT(time);
#undef TEXT
    target->options.include_path=target->include_path;target->services.date=target->date;target->services.time=target->time;
    if(ok && source->initial.allocation.owner) {
        if(memory) ok=qa_bot_memory_checkpoint_resolve(memory,source->initial.allocation,&target->initial.allocation,error);
        else {qa_bot_memory_span bytes;ok=qa_bot_memory_bytes(source->memory,source->initial.allocation,&bytes,error);}
    }
    if(ok && source->initial.type_count) {
        target->initial.types=malloc(source->initial.type_count*sizeof(*target->initial.types));
        ok=target->initial.types!=NULL;
        if(ok) memcpy(target->initial.types,source->initial.types,source->initial.type_count*sizeof(*target->initial.types));
    }
    if(ok && source->initial.message_count) {
        target->initial.messages=malloc(source->initial.message_count*sizeof(*target->initial.messages));
        ok=target->initial.messages!=NULL;
        if(ok) memcpy(target->initial.messages,source->initial.messages,source->initial.message_count*sizeof(*target->initial.messages));
    }
    target->initial.type_capacity=target->initial.type_count;target->initial.message_capacity=target->initial.message_count;
    if(ok && source->reader) {
        qa_script_checkpoint checkpoint={0};qa_script_services services=bot_chat_initial_resource_services(target);
        ok=qa_script_capture(source->reader,&checkpoint,error) && qa_script_restore_detached(&services,&checkpoint,&target->reader,error);
        qa_script_checkpoint_free(&checkpoint);
    }
    bot_chat_initial_acquired **tail=&target->pending;
    for(const bot_chat_initial_acquired *row=source->pending;ok && row;row=row->next) {
        *tail=calloc(1,sizeof(**tail));
        if(!*tail) {ok=false;break;}
        bot_chat_initial_acquired *item=*tail;item->owned=true;
        item->resource.path=copy(row->resource.path,error);item->resource.bytes.size=row->resource.bytes.size;
        if(row->resource.bytes.size && !row->resource.bytes.data) {
            ok=fail(error,"Pending chat source acquisition has no readable saved bytes");
        } else if(row->resource.bytes.size) {
            uint8_t *bytes=malloc(row->resource.bytes.size);item->resource.bytes.data=bytes;
            if(bytes) memcpy(bytes,row->resource.bytes.data,row->resource.bytes.size);
            else ok=false;
        }
        if(row->resource.path && !item->resource.path) ok=false;
        tail=&item->next;
    }
    if(!ok) {
        if(!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source chat checkpoint aliases");
        bot_chat_initial_resource_destroy(target);return false;
    }
    *out=target;return true;
}
static bool alias(qa_bot_memory *owner,const qa_bot_memory_prepared *memory,qa_bot_memory_allocation source,
    qa_bot_memory_allocation *out,qa_error *error) {
    if(!source.owner) {*out=source;return true;}
    if(memory) return qa_bot_memory_checkpoint_resolve(memory,source,out,error);
    qa_bot_memory_span bytes;if(!qa_bot_memory_bytes(owner,source,&bytes,error)) return false;
    *out=source;return true;
}
static bool external_include(bot_chat_history *image,const bot_chat_history *saved,
    qa_bot_memory *owner,const qa_bot_memory_prepared **out,qa_error *error) {
    for(chat_memory_image *row=image->external;row;row=row->next)
        if(row->owner==owner) {*out=row->prepared;return true;}
    chat_memory_image *row=calloc(1,sizeof(*row));
    if(!row) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining independent chat MEMORY history");return false;}
    if(!qa_bot_memory_retain(owner,error)) {free(row);return false;}
    row->owner=owner;row->next=image->external;image->external=row;
    bool ok=false;
    if(!saved) ok=qa_bot_memory_checkpoint_capture(owner,&row->checkpoint,error);
    else for(chat_memory_image *source=saved->external;source;source=source->next)
        if(source->owner==owner) {ok=qa_bot_memory_checkpoint_prepare(owner,source->checkpoint,&row->prepared,error);break;}
    if(!ok && (!error || error->code==QA_OK)) return fail(error,"Independent chat MEMORY was not captured by this history");
    *out=row->prepared;return ok;
}
static bool resource_add(bot_chat_history *image,const bot_chat_history *saved,qa_bot_chat_asset *asset,
    const qa_bot_memory_prepared *memory,qa_error *error) {
    for(chat_resource_image *row=image->resources;row;row=row->next) if(row->asset==asset) return true;
    const bot_chat_initial_resource *resource=asset->initial_source;
    const bot_chat_packed *packed=asset->packed_source;
    if(saved) {
        resource=NULL;packed=NULL;
        for(chat_resource_image *row=saved->resources;row;row=row->next)
            if(row->asset==asset) {resource=row->resource;packed=row->packed;break;}
    }
    if(!resource && !packed) return true;
    qa_bot_memory *owner=resource?resource->memory:packed->memory;
    const qa_bot_memory_prepared *aliases=memory;
    if(owner!=image->system.memory && !external_include(image,saved,owner,&aliases,error)) return false;
    chat_resource_image *row=calloc(1,sizeof(*row));
    if(!row) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining initial chat resource history");return false;}
    row->asset=asset;qa_bot_chat_asset_retain(asset);
    row->next=image->resources;image->resources=row;
    return resource?resource_copy(resource,aliases,&row->resource,error):
        bot_chat_packed_copy(packed,aliases,&row->packed,error);
}
static bool image_copy(qa_bot_runtime *runtime,const bot_chat_history *saved,
    const qa_bot_memory_prepared *memory,bot_chat_history **out,qa_error *error) {
    const qa_bot_chat_system *source=saved?&saved->system:runtime->chat_system;
    if(!source || source->memory!=runtime->memory || source->retired || source->restoring)
        return fail(error,"Chat checkpoint requires the actual live library MEMORY owner");
    bot_chat_history *image=calloc(1,sizeof(*image));
    if(!image) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source chat history");return false;}
    image->owner=runtime;image->system=*source;image->system.console=NULL;image->system.states=NULL;
    image->system.references=1;image->system.restoring=false;
    if(!qa_bot_memory_retain(source->memory,error)) {free(image);return false;}
    qa_bot_chat_asset_retain(source->options.synonyms);qa_bot_chat_asset_retain(source->options.randoms);
    qa_bot_chat_asset_retain(source->options.matches);qa_bot_chat_asset_retain(source->options.replies);
    bool ok=alias(source->memory,memory,source->console_heap,&image->system.console_heap,error);
    for(size_t index=0;ok && index<64;++index) ok=alias(source->memory,memory,source->initial_cache[index],&image->system.initial_cache[index],error);
    if(ok && source->console_capacity) {
        image->system.console=calloc(source->console_capacity,sizeof(*image->system.console));ok=image->system.console!=NULL;
        for(size_t index=0;ok && index<source->console_capacity;++index) {
            image->system.console[index]=source->console[index];
            ok=alias(source->memory,memory,source->console[index].allocation,&image->system.console[index].allocation,error);
        }
    }
    qa_bot_chat **tail=&image->system.states,*previous=NULL;
    for(const qa_bot_chat *state=source->states;ok && state;state=state->next) {
        qa_bot_chat *target=calloc(1,sizeof(*target));if(!target) {ok=false;break;}
        *target=*state;target->system=&image->system;target->next=NULL;target->previous=previous;target->references=1;
        qa_bot_chat_asset_retain(target->initial);*tail=target;tail=&target->next;previous=target;++image->system.references;
        ok=alias(source->memory,memory,state->allocation,&target->allocation,error);
        for(size_t handle=0;handle<64;++handle)
            if((saved?saved->handles[handle]:runtime->chats[handle])==state) image->handles[handle]=target;
    }
    if(saved) image->asset_count=saved->asset_count;
    else for(qa_bot_chat_asset *asset=runtime->library->chat_assets;asset;asset=asset->next) ++image->asset_count;
    if(ok && image->asset_count) {image->assets=calloc(image->asset_count,sizeof(*image->assets));ok=image->assets!=NULL;}
    qa_bot_chat_asset *current=runtime->library->chat_assets;
    for(size_t index=0;ok && index<image->asset_count;++index) {
        qa_bot_chat_asset *asset=saved?saved->assets[index]:current;
        image->assets[index]=asset;qa_bot_chat_asset_retain(asset);if(!saved) current=current->next;
        ok=resource_add(image,saved,asset,memory,error);
    }
    for(qa_bot_chat *state=image->system.states;ok && state;state=state->next)
        if(state->initial) ok=resource_add(image,saved,state->initial,memory,error);
    qa_bot_chat_asset *selected[]={image->system.options.synonyms,image->system.options.randoms,
        image->system.options.matches,image->system.options.replies};
    for(size_t index=0;ok && index<4;++index)
        if(selected[index]) ok=resource_add(image,saved,selected[index],memory,error);
    if(!ok) {
        if(!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source chat history maps");
        bot_chat_history_destroy(image);return false;
    }
    *out=image;return true;
}
bool bot_chat_history_capture(qa_bot_runtime *runtime,bot_chat_history **out,qa_error *error) {
    if(!runtime || !out || *out || !runtime->library || !runtime->chat_system || qa_bot_chat_system_active(runtime->chat_system))
        return fail(error,"Chat history capture requires its actual idle runtime");
    return image_copy(runtime,NULL,NULL,out,error);
}
void bot_chat_history_destroy(bot_chat_history *image) {
    if(!image) return;
    while(image->external) {
        chat_memory_image *row=image->external;image->external=row->next;
        qa_bot_memory_checkpoint_finish(row->prepared,false);qa_bot_memory_checkpoint_destroy(row->checkpoint);
        (void)qa_bot_memory_release(row->owner,NULL);free(row);
    }
    while(image->system.states) {
        qa_bot_chat *state=image->system.states;image->system.states=state->next;
        qa_bot_chat_asset_release(state->initial);free(state);
    }
    qa_bot_chat_asset_release(image->system.options.synonyms);qa_bot_chat_asset_release(image->system.options.randoms);
    qa_bot_chat_asset_release(image->system.options.matches);qa_bot_chat_asset_release(image->system.options.replies);
    while(image->resources) {chat_resource_image *row=image->resources;image->resources=row->next;
        bot_chat_initial_resource_destroy(row->resource);bot_chat_packed_destroy(row->packed);
        qa_bot_chat_asset_release(row->asset);free(row);}
    for(size_t index=0;image->assets && index<image->asset_count;++index) qa_bot_chat_asset_release(image->assets[index]);
    free(image->assets);free(image->system.console);(void)qa_bot_memory_release(image->system.memory,NULL);free(image);
}
bool bot_chat_history_prepare(qa_bot_runtime *runtime,const bot_chat_history *image,
    const qa_bot_memory_prepared *memory,bot_chat_history_restore **out,qa_error *error) {
    if(!runtime || !image || image->owner!=runtime || !memory || !out || *out ||
       !runtime->chat_system || runtime->chat_system->restoring) return fail(error,"Chat history restore owner changed");
    bot_chat_history_restore *prepared=calloc(1,sizeof(*prepared));
    if(!prepared) {qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing source chat history");return false;}
    prepared->owner=runtime;
    bool ok=image_copy(runtime,image,memory,&prepared->image,error);
    if(!ok) {free(prepared);return false;}
    runtime->chat_system->restoring=true;*out=prepared;return true;
}
void bot_chat_history_finish(bot_chat_history_restore *prepared,bool commit) {
    if(!prepared) return;
    qa_bot_runtime *runtime=prepared->owner;qa_bot_chat_system *system=runtime->chat_system;
    bot_chat_history *image=prepared->image;
    if(commit) {
        for(chat_memory_image *row=image->external;row;row=row->next) {
            qa_bot_memory_checkpoint_finish(row->prepared,true);row->prepared=NULL;
        }
        system->restoring=false;
        while(system->states) qa_bot_chat_destroy(system->states);
        qa_bot_chat_asset_release(system->options.synonyms);qa_bot_chat_asset_release(system->options.randoms);
        qa_bot_chat_asset_release(system->options.matches);qa_bot_chat_asset_release(system->options.replies);
        free(system->console);(void)qa_bot_memory_release(system->memory,NULL);
        qa_bot_chat_services services=system->services;qa_bot_log *log=system->log;
        *system=image->system;system->services=services;system->log=log;
        for(qa_bot_chat *state=system->states;state;state=state->next) state->system=system;
        memcpy(runtime->chats,image->handles,sizeof(image->handles));
        image->system.options=(qa_bot_chat_options){0};image->system.states=NULL;image->system.console=NULL;image->system.memory=NULL;
        while(runtime->library->chat_assets) {
            qa_bot_chat_asset *asset=runtime->library->chat_assets;runtime->library->chat_assets=asset->next;
            asset->next=NULL;qa_bot_chat_asset_release(asset);
        }
        qa_bot_chat_asset **tail=&runtime->library->chat_assets;
        for(size_t index=0;index<image->asset_count;++index) {
            qa_bot_chat_asset *asset=image->assets[index];qa_bot_chat_asset_retain(asset);
            *tail=asset;asset->next=NULL;tail=&asset->next;
        }
        for(chat_resource_image *row=image->resources;row;row=row->next) {
            if(row->resource) {
                (void)qa_script_adopt_memory(row->resource->reader,NULL);
                bot_chat_initial_resource_destroy(row->asset->initial_source);
                row->asset->initial_source=row->resource;row->asset->source_loaded=row->resource->loaded;row->resource=NULL;
            }
            if(row->packed) {
                (void)qa_script_adopt_memory(row->packed->reader,NULL);
                bot_chat_packed_destroy(row->asset->packed_source);row->asset->packed_source=row->packed;
                row->asset->source_loaded=row->packed->loaded;row->packed=NULL;
            }
        }
    }
    system->restoring=false;bot_chat_history_destroy(image);free(prepared);
}
