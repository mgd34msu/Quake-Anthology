#include "internal.h"
#include "source_fuzzy_checkpoint.h"

typedef struct fuzzy_config_image {
    bot_fuzzy_owned *object;
    qa_bot_memory_allocation allocation;
    bool owned,disposed;
} fuzzy_config_image;
typedef struct fuzzy_reader_image {
    qa_script_checkpoint source;
    bot_fuzzy_diagnostic *reported;
    size_t reported_count;
    uint64_t generation;
    bool report_failed;
    qa_error report_error;
    struct fuzzy_reader_image *next;
} fuzzy_reader_image;
typedef struct fuzzy_external_image {
    bot_fuzzy_heap *owner;
    qa_bot_memory_checkpoint *memory;
    bot_fuzzy_history *store;
    bot_fuzzy_heap heap;
    struct fuzzy_external_config *configs;
    struct fuzzy_external_image *next;
} fuzzy_external_image;
typedef struct fuzzy_external_config {
    qa_bot_weights *weights;
    qa_bot_memory_allocation allocation;
    bool disposed;
    struct fuzzy_external_config *next;
} fuzzy_external_config;
typedef struct fuzzy_external_alias {
    const fuzzy_external_config *image;
    qa_bot_memory_allocation allocation;
    struct fuzzy_external_alias *next;
} fuzzy_external_alias;
typedef struct fuzzy_external_restore {
    const fuzzy_external_image *image;
    qa_bot_memory_prepared *memory;
    bool owns_memory;
    bot_fuzzy_history_restore *store;
    bot_fuzzy_heap heap;
    fuzzy_external_alias *aliases;
    struct fuzzy_external_restore *next;
} fuzzy_external_restore;
struct bot_fuzzy_history {
    bot_fuzzy_store *owner;
    bot_fuzzy_heap heap;
    fuzzy_config_image *configs;
    size_t config_count,config_capacity,owned_count,cached_count,leases;
    bot_fuzzy_owned *cached[128];
    fuzzy_reader_image *readers;
    fuzzy_external_image *external;
    uint64_t generation;
    bool destroy_pending;
};
struct bot_fuzzy_history_restore {
    bot_fuzzy_history *image;
    bot_fuzzy_store *owner;
    bot_fuzzy_heap heap;
    qa_bot_memory_allocation *allocations;
    bot_fuzzy_owned **old_owned;
    size_t old_count;
    bot_fuzzy_reader *readers;
    fuzzy_external_restore *external;
};
static bool fail(qa_error *error,const char *message)
{
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static void store_release(bot_fuzzy_store *store)
{
    if(store && --store->references==0) {
        (void)bot_fuzzy_heap_clear(&store->heap,NULL);free(store);
    }
}
static void diagnostics_clear(bot_fuzzy_diagnostic *rows,size_t count)
{
    for(size_t i=0;i<count;++i) {free(rows[i].path);free(rows[i].message);}
    free(rows);
}
static char *text_copy(const char *text,qa_error *error)
{
    if(!text) {fail(error,"Captured fuzzy diagnostic has no text");return NULL;}
    size_t size=strlen(text)+1;char *copy=malloc(size);
    if(!copy) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining fuzzy checkpoint diagnostic");return NULL;}
    memcpy(copy,text,size);return copy;
}
static bool diagnostics_copy(const bot_fuzzy_diagnostic *rows,size_t count,
    bot_fuzzy_diagnostic **out,qa_error *error)
{
    if(!count) return true;
    if(!rows || count>SIZE_MAX/sizeof(**out)) return fail(error,"Invalid fuzzy diagnostic extent");
    bot_fuzzy_diagnostic *copy=calloc(count,sizeof(*copy));
    if(!copy) {qa_error_set(error,QA_ERROR_MEMORY,0,"Copying fuzzy checkpoint diagnostics");return false;}
    for(size_t i=0;i<count;++i) {
        copy[i].value=rows[i].value;
        copy[i].path=text_copy(rows[i].path,error);
        if(copy[i].path) copy[i].message=text_copy(rows[i].message,error);
        if(!copy[i].path || !copy[i].message) {diagnostics_clear(copy,count);return false;}
        copy[i].value.location.path=copy[i].path;copy[i].value.message=copy[i].message;
    }
    *out=copy;return true;
}
static void readers_clear(bot_fuzzy_reader *reader)
{
    while(reader) {
        bot_fuzzy_reader *next=reader->next;
        qa_script_close(reader->source);
        diagnostics_clear(reader->reported,reader->reported_count);free(reader);reader=next;
    }
}
static bool heap_copy(const bot_fuzzy_heap *source,bot_fuzzy_heap *out,
    const qa_bot_memory_prepared *memory,qa_error *error)
{
    if(!source->memory || !source->next_pointer || !bot_fuzzy_heap_bind(out,source->memory,error)) return false;
    out->next_pointer=source->next_pointer;
    bot_fuzzy_pointer **tail=&out->first;
    for(const bot_fuzzy_pointer *row=source->first;row;row=row->next) {
        *tail=malloc(sizeof(**tail));
        if(!*tail) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining fuzzy checkpoint pointer order");return false;}
        **tail=*row;(*tail)->next=NULL;out->last=*tail;
        if(memory) {
            if(!qa_bot_memory_checkpoint_resolve(memory,row->allocation,&(*tail)->allocation,error)) return false;
        } else {
            qa_bot_memory_span bytes;
            if(!qa_bot_memory_bytes(source->memory,row->allocation,&bytes,error)) return false;
        }
        tail=&(*tail)->next;
    }
    return true;
}
static bool config_add(bot_fuzzy_history *image,bot_fuzzy_owned *object,bool owned,qa_error *error)
{
    if(!object) return true;
    for(size_t i=0;i<image->config_count;++i) if(image->configs[i].object==object) return true;
    if(object->store!=image->owner || object->source.heap!=&image->owner->heap || object->owned!=owned)
        return fail(error,"Fuzzy checkpoint configuration belongs to another source owner");
    if(!object->disposed) {
        qa_bot_memory_span bytes;
        if(!qa_bot_memory_bytes(image->heap.memory,object->source.allocation,&bytes,error)) return false;
    }
    if(!bot_grow((void **)&image->configs,&image->config_capacity,image->config_count+1,
        sizeof(*image->configs),error)) return false;
    bot_fuzzy_owned_retain(object);
    image->configs[image->config_count++]=(fuzzy_config_image){object,object->source.allocation,owned,object->disposed};
    return true;
}
void bot_fuzzy_history_destroy(bot_fuzzy_history *image)
{
    if(!image) return;
    if(image->leases) {image->destroy_pending=true;return;}
    for(size_t i=0;i<image->config_count;++i) bot_fuzzy_owned_release(image->configs[i].object);
    while(image->readers) {
        fuzzy_reader_image *row=image->readers;image->readers=row->next;
        qa_script_checkpoint_free(&row->source);diagnostics_clear(row->reported,row->reported_count);free(row);
    }
    while(image->external) {
        fuzzy_external_image *row=image->external;image->external=row->next;
        bot_fuzzy_history_destroy(row->store);
        qa_bot_memory_checkpoint_destroy(row->memory);
        (void)bot_fuzzy_heap_clear(&row->heap,NULL);
        while(row->configs) {
            fuzzy_external_config *config=row->configs;row->configs=config->next;
            qa_bot_weights_release(config->weights);free(config);
        }
        free(row);
    }
    (void)bot_fuzzy_heap_clear(&image->heap,NULL);
    store_release(image->owner);free(image->configs);free(image);
}
bool bot_fuzzy_history_capture(qa_bot_library *library,bot_fuzzy_history **out,qa_error *error)
{
    bot_fuzzy_store *store=library?library->fuzzy_store:NULL;
    if(!store || store->closed || store->active || store->library!=library ||
       store->heap.memory!=library->memory || store->references==SIZE_MAX ||
       store->cached_count>128 || !out || *out) return fail(error,"Fuzzy checkpoint requires its actual idle owner");
    bot_fuzzy_history *image=calloc(1,sizeof(*image));
    if(!image) {qa_error_set(error,QA_ERROR_MEMORY,0,"Capturing fuzzy source history");return false;}
    image->owner=store;++store->references;
    image->cached_count=store->cached_count;image->generation=store->generation;
    memcpy(image->cached,store->cached,sizeof(image->cached));
    if(!heap_copy(&store->heap,&image->heap,NULL,error)) goto failed;
    bot_fuzzy_owned *last=NULL;
    for(bot_fuzzy_owned *row=store->first;row;row=row->next) {
        if(!config_add(image,row,true,error)) goto failed;
        last=row;
    }
    if(last!=store->last) {fail(error,"Fuzzy checkpoint owner tail differs");goto failed;}
    image->owned_count=image->config_count;
    for(size_t i=0;i<128;++i)
        if(store->cached[i] && !config_add(image,store->cached[i],store->cached[i]->owned,error)) goto failed;
    for(qa_bot_weights *row=library->weights;row;row=row->next)
        if(row->source && row->source->store==store && !config_add(image,row->source,row->source->owned,error)) goto failed;
    fuzzy_reader_image **tail=&image->readers;
    for(bot_fuzzy_reader *reader=store->readers;reader;reader=reader->next) {
        *tail=calloc(1,sizeof(**tail));
        if(!*tail) {qa_error_set(error,QA_ERROR_MEMORY,0,"Capturing reached fuzzy reader");goto failed;}
        fuzzy_reader_image *row=*tail;
        row->generation=reader->generation;row->report_failed=reader->report_failed;row->report_error=reader->report_error;
        if(!qa_script_capture(reader->source,&row->source,error) ||
           !diagnostics_copy(reader->reported,reader->reported_count,&row->reported,error)) goto failed;
        row->reported_count=reader->reported_count;tail=&row->next;
    }
    *out=image;return true;
failed:
    bot_fuzzy_history_destroy(image);return false;
}
static bool external_config_add(fuzzy_external_image *row,qa_bot_weights *weights,qa_error *error)
{
    for(fuzzy_external_config *config=row->configs;config;config=config->next)
        if(config->weights==weights) return true;
    if(weights->standalone_heap!=row->owner || weights->source->source.heap!=row->owner)
        return fail(error,"Independent fuzzy history has another actual standalone heap");
    fuzzy_external_config *config=calloc(1,sizeof(*config));
    if(!config) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining independent fuzzy parent identity");return false;}
    config->weights=weights;qa_bot_weights_retain(weights);
    config->allocation=weights->source->source.allocation;config->disposed=weights->source->disposed;
    config->next=row->configs;row->configs=config;return true;
}
bool bot_fuzzy_history_include(bot_fuzzy_history *image,qa_bot_weights *weights,qa_error *error)
{
    if(!image || image->destroy_pending || image->leases || !weights || !weights->source)
        return fail(error,"Fuzzy history inclusion requires its captured image and genuine configuration");
    bot_fuzzy_owned *config=weights->source;bot_fuzzy_heap *heap=config->source.heap;
    if(heap==&image->owner->heap) return config_add(image,config,config->owned,error);
    for(fuzzy_external_image *row=image->external;row;row=row->next) if(row->owner==heap) {
        if(row->store) return true;
        return external_config_add(row,weights,error);
    }
    if(!heap || !heap->memory || (config->store && (config->store->closed || !config->store->library)) ||
       (!config->store && weights->standalone_heap!=heap))
        return fail(error,"Independent fuzzy history requires its actual retained heap owner");
    fuzzy_external_image *row=calloc(1,sizeof(*row));
    if(!row) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining independent fuzzy source owner");return false;}
    row->owner=heap;row->next=image->external;image->external=row;
    if(heap->memory!=image->heap.memory && !qa_bot_memory_checkpoint_capture(heap->memory,&row->memory,error)) return false;
    if(config->store) return bot_fuzzy_history_capture(config->store->library,&row->store,error);
    return external_config_add(row,weights,error) && heap_copy(heap,&row->heap,NULL,error);
}
bool bot_fuzzy_history_prepare(qa_bot_library *library,const bot_fuzzy_history *captured,
    const qa_bot_memory_prepared *memory,bot_fuzzy_history_restore **out,qa_error *error)
{
    bot_fuzzy_store *store=library?library->fuzzy_store:NULL;
    if(!captured || captured->destroy_pending || captured->leases==SIZE_MAX || !store ||
       captured->owner!=store || store->closed || store->active || store->library!=library ||
       library->memory!=captured->heap.memory || !memory || !out || *out)
        return fail(error,"Fuzzy restore differs from its captured source owner");
    bot_fuzzy_history_restore *plan=calloc(1,sizeof(*plan));
    if(!plan) {qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing fuzzy source history");return false;}
    plan->owner=store;plan->image=(bot_fuzzy_history *)captured;
    ++plan->image->leases;store->active=true;
    if(!heap_copy(&captured->heap,&plan->heap,memory,error)) goto failed;
    if(captured->config_count) {
        plan->allocations=calloc(captured->config_count,sizeof(*plan->allocations));
        if(!plan->allocations) goto memory_failure;
    }
    for(size_t i=0;i<captured->config_count;++i) {
        const fuzzy_config_image *row=&captured->configs[i];
        if(row->object->store!=store || row->object->source.heap!=&store->heap) {
            fail(error,"Captured fuzzy configuration identity changed");goto failed;
        }
        plan->allocations[i]=row->allocation;
        if(!row->disposed && !qa_bot_memory_checkpoint_resolve(memory,row->allocation,&plan->allocations[i],error)) goto failed;
    }
    for(bot_fuzzy_owned *row=store->first;row;row=row->next) ++plan->old_count;
    if(plan->old_count) {
        if(plan->old_count>SIZE_MAX/sizeof(*plan->old_owned)) goto memory_failure;
        plan->old_owned=malloc(plan->old_count*sizeof(*plan->old_owned));
        if(!plan->old_owned) goto memory_failure;
    }
    size_t at=0;
    for(bot_fuzzy_owned *row=store->first;row;row=row->next) plan->old_owned[at++]=row;
    bot_fuzzy_reader **tail=&plan->readers;
    for(const fuzzy_reader_image *row=captured->readers;row;row=row->next) {
        *tail=calloc(1,sizeof(**tail));
        if(!*tail) goto memory_failure;
        bot_fuzzy_reader *reader=*tail;
        reader->store=store;reader->services=library->options.scripts;reader->generation=row->generation;
        reader->report_failed=row->report_failed;reader->report_error=row->report_error;
        if(!diagnostics_copy(row->reported,row->reported_count,&reader->reported,error)) goto failed;
        reader->reported_count=reader->reported_capacity=row->reported_count;
        qa_script_services services=bot_fuzzy_reader_services(reader);
        if(!qa_script_restore(&services,&row->source,&reader->source,error)) goto failed;
        tail=&reader->next;
    }
    fuzzy_external_restore **external=&plan->external;
    for(const fuzzy_external_image *row=captured->external;row;row=row->next) {
        *external=calloc(1,sizeof(**external));
        if(!*external) goto memory_failure;
        fuzzy_external_restore *prepared=*external;prepared->image=row;
        const qa_bot_memory_prepared *aliases=memory;
        if(row->memory) {
            for(fuzzy_external_restore *prior=plan->external;prior!=prepared;prior=prior->next)
                if(prior->image->owner->memory==row->owner->memory) {prepared->memory=prior->memory;break;}
            if(!prepared->memory) {
                if(!qa_bot_memory_checkpoint_prepare(row->owner->memory,row->memory,&prepared->memory,error)) goto failed;
                prepared->owns_memory=true;
            }
            aliases=prepared->memory;
        }
        if(row->store) {
            bot_fuzzy_store *owner=row->store->owner;
            if(owner->closed || !owner->library) {
                fail(error,"Captured independent fuzzy store has been closed");goto failed;
            }
            if(!bot_fuzzy_history_prepare(owner->library,row->store,aliases,&prepared->store,error)) goto failed;
        } else {
            if(!heap_copy(&row->heap,&prepared->heap,aliases,error)) goto failed;
            fuzzy_external_alias **parents=&prepared->aliases;
            for(const fuzzy_external_config *config=row->configs;config;config=config->next) {
                if(config->weights->source->source.heap!=row->owner || config->weights->standalone_heap!=row->owner) {
                    fail(error,"Captured independent fuzzy parent changed owner");goto failed;
                }
                *parents=calloc(1,sizeof(**parents));
                if(!*parents) goto memory_failure;
                (*parents)->image=config;(*parents)->allocation=config->allocation;
                if(!config->disposed && !qa_bot_memory_checkpoint_resolve(aliases,config->allocation,&(*parents)->allocation,error)) goto failed;
                parents=&(*parents)->next;
            }
        }
        external=&prepared->next;
    }
    *out=plan;return true;
memory_failure:
    qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing fuzzy source aliases");
failed:
    bot_fuzzy_history_finish(plan,false);return false;
}
void bot_fuzzy_history_finish(bot_fuzzy_history_restore *plan,bool commit)
{
    if(!plan) return;
    bot_fuzzy_history *image=plan->image;bot_fuzzy_store *store=plan->owner;
    /* Every independently held allocator commits before any of its aliases. */
    for(fuzzy_external_restore *row=plan->external;row;row=row->next)
        if(row->owns_memory) qa_bot_memory_checkpoint_finish(row->memory,commit);
    while(plan->external) {
        fuzzy_external_restore *row=plan->external;plan->external=row->next;
        bot_fuzzy_history_finish(row->store,commit);
        if(commit && !row->store) {
            bot_fuzzy_heap old=*row->image->owner;
            row->heap.standalone_users=old.standalone_users;
            *row->image->owner=row->heap;row->heap=(bot_fuzzy_heap){0};
            for(fuzzy_external_alias *parent=row->aliases;parent;parent=parent->next) {
                parent->image->weights->source->source.allocation=parent->allocation;
                parent->image->weights->source->disposed=parent->image->disposed;
            }
            (void)bot_fuzzy_heap_clear(&old,NULL);
        }
        while(row->aliases) {fuzzy_external_alias *parent=row->aliases;row->aliases=parent->next;free(parent);}
        (void)bot_fuzzy_heap_clear(&row->heap,NULL);free(row);
    }
    if(commit) {
        bot_fuzzy_heap old_heap=store->heap;bot_fuzzy_reader *old_readers=store->readers;
        bot_fuzzy_owned *old_cache[128];memcpy(old_cache,store->cached,sizeof(old_cache));
        for(size_t i=0;i<image->owned_count;++i) bot_fuzzy_owned_retain(image->configs[i].object);
        for(size_t i=0;i<128;++i) bot_fuzzy_owned_retain(image->cached[i]);
        for(size_t i=0;i<plan->old_count;++i) {
            bot_fuzzy_owned *row=plan->old_owned[i];row->owned=false;row->disposed=true;row->next=NULL;
        }
        store->heap=plan->heap;plan->heap=(bot_fuzzy_heap){0};
        store->first=store->last=NULL;
        for(size_t i=0;i<image->config_count;++i) {
            fuzzy_config_image *row=&image->configs[i];bot_fuzzy_owned *object=row->object;
            object->source.allocation=plan->allocations[i];object->source.heap=&store->heap;
            object->owned=row->owned;object->disposed=row->disposed;object->next=NULL;
            if(i<image->owned_count) {
                if(store->last) store->last->next=object;else store->first=object;
                store->last=object;
            }
        }
        memcpy(store->cached,image->cached,sizeof(store->cached));
        store->cached_count=image->cached_count;store->generation=image->generation;
        store->readers=plan->readers;plan->readers=NULL;
        for(size_t i=0;i<plan->old_count;++i) bot_fuzzy_owned_release(plan->old_owned[i]);
        for(size_t i=0;i<128;++i) bot_fuzzy_owned_release(old_cache[i]);
        readers_clear(old_readers);(void)bot_fuzzy_heap_clear(&old_heap,NULL);
    }
    readers_clear(plan->readers);(void)bot_fuzzy_heap_clear(&plan->heap,NULL);
    free(plan->allocations);free(plan->old_owned);store->active=false;
    --image->leases;
    if(!image->leases && image->destroy_pending) bot_fuzzy_history_destroy(image);
    free(plan);
}
