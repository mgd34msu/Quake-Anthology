#include "internal.h"
#include "source_initial_save.h"
#include "../save_fields.h"
#include "../memory/internal.h"
#include "qa/bots_allocator_save.h"

static bool memory_fields(qa_source_save_io *io,qa_bot_memory *memory) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;qa_buffer bytes={0};size_t extent=0;
    bool ok=reading || qa_bot_memory_capture(memory,&bytes,io->error);
    if(!reading) extent=bytes.size;
    if(ok) ok=qa_source_save_count(io,&extent,SIZE_MAX);
    if(ok && reading) {
        if(io->offset>io->input.size || extent>io->input.size-io->offset)
            ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated standalone chat MEMORY");
        else {
            ok=qa_bot_memory_restore(memory,(qa_bytes){io->input.data+io->offset,extent},io->error);
            if(ok) io->offset+=extent;
        }
    } else if(ok) ok=qa_source_save_bytes(io,bytes.data,extent);
    qa_buffer_free(&bytes);
    if(!ok) io->failed=true;
    return ok;
}
static bool pointers(qa_source_save_io *io,uint32_t **values,size_t *count,size_t *capacity,
    const bot_chat_initial *chat,bool types) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!qa_source_save_count(io,count,UINT32_MAX)) return false;
    if(reading && *count) {
        if(io->offset>io->input.size || *count>(io->input.size-io->offset)/4 ||
           *count>SIZE_MAX/sizeof(**values)) return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated initial chat member map");
        *values=calloc(*count,sizeof(**values));
        if(!*values) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring initial chat typed members");
        *capacity=*count;
    }
    for(size_t index=0;index<*count;++index) {
        if(!qa_source_save_u32(io,&(*values)[index]) || !(*values)[index])
            return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid initial chat member pointer");
        for(size_t prior=0;prior<index;++prior)
            if((*values)[prior]==(*values)[index])
                return bot_save_fail(io,QA_ERROR_FORMAT,"Duplicate initial chat member pointer");
        qa_bot_memory_span bytes;
        bool ok=types?bot_chat_initial_type(chat,(*values)[index],&bytes,io->error):
            bot_chat_initial_message(chat,(*values)[index],&bytes,io->error);
        if(!ok) {io->failed=true;return false;}
    }
    return true;
}
static bool topology(qa_source_save_io *io,bot_chat_initial *chat) {
    uint32_t type;
    if(!bot_chat_initial_first(chat,&type,io->error)) {io->failed=true;return false;}
    for(size_t visited=0;type;++visited) {
        if(visited>=chat->type_count) return bot_save_fail(io,QA_ERROR_FORMAT,"Saved initial chat type list cycles");
        uint32_t message;int32_t count;
        if(!bot_chat_initial_type_first(chat,type,&message,io->error) ||
           !bot_chat_initial_type_count(chat,type,&count,io->error)) {io->failed=true;return false;}
        size_t seen=0;
        for(;message;++seen) {
            if(seen>=chat->message_count) return bot_save_fail(io,QA_ERROR_FORMAT,"Saved initial chat message list cycles");
            qa_bytes text;
            if(!bot_chat_initial_message_text(chat,message,&text,io->error) ||
               !bot_chat_initial_message_next(chat,message,&message,io->error)) {io->failed=true;return false;}
        }
        if(count<0 || (size_t)count!=seen)
            return bot_save_fail(io,QA_ERROR_FORMAT,"Saved initial chat message count differs from raw links");
        if(!bot_chat_initial_type_next(chat,type,&type,io->error)) {io->failed=true;return false;}
    }
    return true;
}
static bool reader_fields(qa_source_save_io *io,bot_chat_initial_resource *resource) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=!reading && resource->reader;
    if(!qa_source_save_bool(io,&present)) return false;
    if(!present) return true;
    if(!qa_script_services_valid(&resource->services))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Retained chat PC requires its actual source services");
    qa_script_checkpoint checkpoint={0};qa_buffer bytes={0};size_t extent=0;
    bool ok=reading || (qa_script_capture(resource->reader,&checkpoint,io->error) &&
        qa_script_checkpoint_encode(&checkpoint,&bytes,io->error));
    if(!reading) extent=bytes.size;
    if(ok) ok=qa_source_save_count(io,&extent,SIZE_MAX);
    if(ok && reading) {
        if(io->offset>io->input.size || extent>io->input.size-io->offset)
            ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated retained initial chat PC");
        else {
            qa_script_services services=bot_chat_initial_resource_services(resource);
            ok=qa_script_checkpoint_decode((qa_bytes){io->input.data+io->offset,extent},&checkpoint,io->error) &&
                qa_script_restore(&services,&checkpoint,&resource->reader,io->error);
            if(ok) io->offset+=extent;
        }
    } else if(ok) ok=qa_source_save_bytes(io,bytes.data,extent);
    qa_script_checkpoint_free(&checkpoint);qa_buffer_free(&bytes);
    if(!ok) io->failed=true;
    return ok;
}
static bool pending_fields(qa_source_save_io *io,bot_chat_initial_resource *resource) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;size_t count=0;
    if(!reading) for(bot_chat_initial_acquired *row=resource->pending;row;row=row->next) ++count;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/9))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated retained initial chat acquisitions");
    bot_chat_initial_acquired **link=&resource->pending;
    for(size_t index=0;index<count;++index) {
        if(reading) {
            *link=calloc(1,sizeof(**link));
            if(!*link) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring acquired chat source");
            (*link)->owned=true;
        }
        bot_chat_initial_acquired *row=*link;
        const char *path=reading?NULL:row->resource.path;
        size_t extent=reading?0:row->resource.bytes.size;
        bool ok=bot_save_text(io,&path) && qa_source_save_count(io,&extent,SIZE_MAX);
        if(reading) row->resource.path=path;
        if(ok && reading && extent) {
            if(io->offset>io->input.size || extent>io->input.size-io->offset)
                ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated acquired initial chat bytes");
            else {
                uint8_t *bytes=malloc(extent);
                if(!bytes) ok=bot_save_fail(io,QA_ERROR_MEMORY,"Restoring acquired initial chat bytes");
                else row->resource.bytes=(qa_bytes){bytes,extent};
            }
        }
        if(ok && extent && !row->resource.bytes.data)
            ok=bot_save_fail(io,QA_ERROR_FORMAT,"Acquired chat has no retained source bytes");
        if(ok) ok=qa_source_save_bytes(io,(void *)row->resource.bytes.data,extent);
        if(!ok) return false;
        link=&row->next;
    }
    return true;
}
static bool resource_fields(qa_source_save_io *io,qa_bot_chat_asset *asset) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    bot_chat_initial_resource *resource=asset->initial_source;
    bool allocated=!reading && resource->initial.allocation.owner;
    uint32_t failure=(uint32_t)resource->failure,code=(uint32_t)resource->report_error.code;
    const char *path=reading?NULL:resource->path;
    bool ok=qa_source_save_bool(io,&resource->attempted) && qa_source_save_bool(io,&resource->published) &&
        qa_source_save_bool(io,&resource->loaded) && qa_source_save_bool(io,&resource->missing_root) &&
        qa_source_save_bool(io,&resource->own_failure) && qa_source_save_bool(io,&resource->report_failed) &&
        qa_source_save_bool(io,&resource->retired_abort) && qa_source_save_bool(io,&resource->service_failed) &&
        qa_source_save_u32(io,&resource->size) && qa_source_save_u32(io,&resource->pass) && resource->pass<=2 &&
        qa_source_save_u32(io,&failure) && failure<=BOT_CHAT_INITIAL_NAME_MISSING && bot_save_text(io,&path) &&
        qa_source_save_u32(io,&code) && code<=QA_ERROR_NOT_FOUND &&
        qa_source_save_count(io,&resource->report_error.offset,SIZE_MAX) &&
        qa_source_save_bytes(io,resource->report_error.message,sizeof(resource->report_error.message)) &&
        memchr(resource->report_error.message,0,sizeof(resource->report_error.message)) &&
        qa_source_save_bool(io,&allocated);
    if(reading) {resource->failure=(bot_chat_initial_failure)failure;resource->path=(char *)path;
        resource->report_error.code=(qa_status)code;}
    if(ok && allocated) {
        size_t reference=0;
        ok=(reading || qa_bot_memory_reference(resource->memory,resource->initial.allocation,&reference,io->error)) &&
            qa_source_save_count(io,&reference,SIZE_MAX) && qa_source_save_u32(io,&resource->initial.pointer);
        if(ok && reading) {
            resource->initial.memory=resource->memory;
            ok=qa_bot_memory_resolve(resource->memory,reference,&resource->initial.allocation,io->error);
        }
        if(ok) {
            bot_memory_record *record=bot_memory_record_get(resource->memory,resource->initial.allocation);
            ok=record && record->kind==QA_BOT_MEMORY_HEAP && record->size>=4;
        }
        if(ok) ok=pointers(io,&resource->initial.types,&resource->initial.type_count,
            &resource->initial.type_capacity,&resource->initial,true) &&
            pointers(io,&resource->initial.messages,&resource->initial.message_count,
                &resource->initial.message_capacity,&resource->initial,false);
        if(ok && resource->published) ok=resource->initial.pointer!=0 && topology(io,&resource->initial);
    }
    if(ok) ok=reader_fields(io,resource) && pending_fields(io,resource);
    if(ok && (resource->active || (resource->published && !allocated) ||
        (resource->loaded && (!resource->published || resource->reader || resource->report_failed ||
         resource->retired_abort || resource->service_failed || resource->failure!=BOT_CHAT_INITIAL_NO_FAILURE)) ||
        (resource->reader && (!resource->attempted || resource->loaded)) ||
        (!resource->attempted && (allocated || resource->path || resource->reader || resource->pending ||
         resource->published || resource->own_failure || resource->report_failed ||
         resource->retired_abort || resource->service_failed ||
         resource->failure!=BOT_CHAT_INITIAL_NO_FAILURE))))
        ok=bot_save_fail(io,QA_ERROR_FORMAT,"Invalid reached initial chat source stage");
    if(ok) {
        asset->source_loaded=resource->loaded;
        if(resource->loaded) ok=chat_initial_asset_refresh(asset,io->error);
    }
    if(!ok && !io->failed) return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid retained initial chat resource");
    return ok;
}
bool bot_chat_initial_asset_fields(qa_source_save_io *io,const qa_bot_chat_asset *source,
    qa_bot_library *library,qa_bot_memory *memory,qa_bot_chat_asset **out) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ,standalone=memory==NULL;
    if((reading && (!out || *out)) || (!reading && (!source || !source->initial_source)) ||
       (library && memory!=library->memory))
        return bot_save_fail(io,QA_ERROR_ARGUMENT,"Initial chat codec requires its actual owners/output");
    qa_bot_chat_asset *asset=(qa_bot_chat_asset *)source;
    const char *path=reading?NULL:source->view.path,*name=reading?NULL:source->view.name;
    bool ok=bot_save_text(io,&path) && path && bot_save_text(io,&name) && name;
    if(ok && reading) ok=chat_asset_allocate(QA_BOT_CHAT_INITIAL,path,name,&asset,io->error);
    if(reading) {free((void *)path);free((void *)name);}
    if(ok && standalone) {
        ok=!reading || qa_bot_memory_create(NULL,&memory,io->error);
        if(!reading) memory=source->initial_source->memory;
        if(ok) ok=memory_fields(io,memory);
    }
    if(ok && reading) ok=chat_initial_asset_owner(asset,library,memory,io->error);
    if(ok && !reading && asset->initial_source->memory!=memory)
        ok=bot_save_fail(io,QA_ERROR_FORMAT,"Initial chat aliases another MEMORY owner");
    if(ok) ok=resource_fields(io,asset);
    if(reading && standalone && memory) (void)qa_bot_memory_release(memory,NULL);
    if(reading) {
        if(ok) *out=asset;
        else qa_bot_chat_asset_release(asset);
    }
    if(!ok) io->failed=true;
    return ok;
}
