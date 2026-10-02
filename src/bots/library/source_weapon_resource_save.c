#include "source_weapon_resource_save.h"
#include "source_weapon_standalone.h"
#include "../save_fields.h"
#include "../memory/internal.h"
#include "qa/bots_allocator_save.h"

static bool diagnostics(qa_source_save_io *io,bot_weapon_resource *resource) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!qa_source_save_count(io,&resource->diagnostic_count,SIZE_MAX)) return false;
    if(reading && resource->diagnostic_count) {
        size_t remaining=io->offset<=io->input.size?io->input.size-io->offset:0;
        if(resource->diagnostic_count>remaining/34 ||
           resource->diagnostic_count>SIZE_MAX/sizeof(*resource->diagnostics))
            return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated retained weapon diagnostics");
        resource->diagnostics=calloc(resource->diagnostic_count,sizeof(*resource->diagnostics));
        if(!resource->diagnostics)
            return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring retained weapon diagnostics");
        resource->diagnostic_capacity=resource->diagnostic_count;
    }
    for(size_t index=0;index<resource->diagnostic_count;++index) {
        bot_weapon_diagnostic *row=&resource->diagnostics[index];
        uint32_t origin=(uint32_t)row->origin,severity=(uint32_t)row->value.severity;
        const char *path=reading?NULL:row->path,*message=reading?NULL:row->message;
        bool ok=qa_source_save_u32(io,&origin) && origin<=BOT_WEAPON_DIAGNOSTIC_PRINT &&
            qa_source_save_u32(io,&severity) && severity<=QA_SCRIPT_FATAL &&
            bot_save_text(io,&path) && path && bot_save_text(io,&message) && message;
        if(reading) {
            row->origin=(bot_weapon_diagnostic_origin)origin;row->path=(char *)path;
            row->message=(char *)message;row->value.severity=(qa_script_severity)severity;
            row->value.location.path=path;row->value.message=message;
        }
        if(ok) ok=qa_source_save_u32(io,&row->value.location.line) &&
            qa_source_save_u32(io,&row->value.location.column) &&
            qa_source_save_count(io,&row->value.location.offset,SIZE_MAX);
        if(!ok) return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid retained weapon diagnostic");
    }
    return true;
}
static bool source_fields(qa_source_save_io *io,bot_weapon_resource *resource) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=!reading && resource->reader!=NULL;
    if(!qa_source_save_bool(io,&present)) return false;
    if(!present) return true;
    if(!qa_script_services_valid(&resource->services))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Retained weapon PC requires its actual source services");
    qa_script_checkpoint state={0};qa_buffer bytes={0};size_t extent=0;
    bool ok=reading || (qa_script_capture(resource->reader,&state,io->error) &&
        qa_script_checkpoint_encode(&state,&bytes,io->error));
    if(!reading) extent=bytes.size;
    if(ok) ok=qa_source_save_count(io,&extent,SIZE_MAX);
    if(ok && reading) {
        if(io->offset>io->input.size || extent>io->input.size-io->offset)
            ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated retained weapon PC source");
        else {
            qa_script_services services=bot_weapon_resource_services(resource);
            ok=qa_script_checkpoint_decode((qa_bytes){io->input.data+io->offset,extent},&state,io->error) &&
                qa_script_restore(&services,&state,&resource->reader,io->error);
            if(ok) io->offset+=extent;
        }
    } else if(ok) ok=qa_source_save_bytes(io,bytes.data,extent);
    qa_script_checkpoint_free(&state);qa_buffer_free(&bytes);
    if(!ok) io->failed=true;
    return ok;
}
static bool pending_fields(qa_source_save_io *io,bot_weapon_resource *resource) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;size_t count=0;
    if(!reading) for(bot_weapon_acquired_source *row=resource->pending;row;row=row->next) ++count;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/9))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated pending weapon source acquisitions");
    bot_weapon_acquired_source **link=&resource->pending;
    for(size_t index=0;index<count;++index) {
        if(reading) {
            *link=calloc(1,sizeof(**link));
            if(!*link) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring pending weapon source ownership");
            (*link)->owned=true;
        }
        bot_weapon_acquired_source *row=*link;
        const char *path=reading?NULL:row->value.path;
        size_t extent=reading?0:row->value.bytes.size;
        bool ok=bot_save_text(io,&path) && qa_source_save_count(io,&extent,SIZE_MAX);
        if(reading) row->value.path=path;
        if(ok && reading) {
            if(io->offset>io->input.size || extent>io->input.size-io->offset)
                ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated pending weapon source bytes");
            else if(extent) {
                uint8_t *bytes=malloc(extent);
                if(!bytes) ok=bot_save_fail(io,QA_ERROR_MEMORY,"Restoring pending weapon source bytes");
                else row->value.bytes=(qa_bytes){bytes,extent};
            }
        }
        if(ok && extent && !row->value.bytes.data)
            ok=bot_save_fail(io,QA_ERROR_FORMAT,"Pending weapon source has no acquired bytes");
        if(ok) ok=qa_source_save_bytes(io,(void *)row->value.bytes.data,extent);
        if(!ok) return false;
        link=&row->next;
    }
    return true;
}
static bool qualified(qa_source_save_io *io,bot_weapon_resource *resource,bool allocation,bool bound) {
    if(resource->active || (bound && (!allocation || !resource->path || !*resource->path || resource->reader || resource->report_failed)) ||
       (!qa_script_services_valid(&resource->services) && (!bound || resource->pending || resource->reader)) ||
       (resource->reader && (!resource->attempted || bound || resource->missing_root)) ||
       (resource->missing_root && (!resource->attempted || allocation || bound || resource->reader)) ||
       (resource->pending && (!resource->attempted || bound || resource->missing_root)) ||
       (!resource->attempted && !bound && (allocation || resource->reader || resource->path ||
         resource->diagnostic_count || resource->missing_root || resource->invalid_root_path || resource->own_failure || resource->report_failed || resource->pending)) ||
       (resource->own_failure && (!resource->attempted || bound)) ||
       (resource->invalid_root_path && (!resource->attempted || !resource->own_failure || bound || allocation || resource->reader)))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid reached weapon resource stage");
    if(allocation) {
        bot_memory_record *record=bot_memory_record_get(resource->memory,resource->record.allocation);
        if(!record || record->kind!=QA_BOT_MEMORY_HUNK)
            return bot_save_fail(io,QA_ERROR_FORMAT,"Weapon resource does not alias its actual hunk allocation");
    }
    return true;
}
bool bot_weapon_resource_fields(qa_source_save_io *io,const bot_weapon_resource *source,
    const bot_weapon_resource_factory *factory,bot_weapon_resource **out) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if((reading && (!factory || !out || *out)) || (!reading && !source))
        return bot_save_fail(io,QA_ERROR_ARGUMENT,"Missing genuine weapon resource codec owners");
    bot_weapon_resource *resource=(bot_weapon_resource *)source;
    if(reading) {
        bool created=!factory->services && !factory->options?
            bot_weapon_resource_pure(factory->memory,&resource,io->error):
            bot_weapon_resource_create(factory->memory,factory->services,factory->options,
                factory->host,&resource,io->error);
        if(!created) {io->failed=true;return false;}
    }
    bool allocation=!reading && resource->record.allocation.owner!=0,bound=resource->bound;
    const char *path=reading?NULL:resource->path;
    size_t reference=0;
    bool ok=qa_source_save_bool(io,&resource->attempted) && qa_source_save_bool(io,&bound) &&
        qa_source_save_bool(io,&resource->missing_root) && qa_source_save_bool(io,&resource->report_failed) &&
        qa_source_save_bool(io,&resource->invalid_root_path) && qa_source_save_bool(io,&resource->own_failure) &&
        bot_save_text(io,&path);
    if(reading) resource->path=(char *)path;
    uint32_t code=(uint32_t)resource->report_error.code;
    if(ok) ok=qa_source_save_u32(io,&code) && code<=QA_ERROR_NOT_FOUND &&
        qa_source_save_count(io,&resource->report_error.offset,SIZE_MAX) &&
        qa_source_save_bytes(io,resource->report_error.message,sizeof(resource->report_error.message)) &&
        memchr(resource->report_error.message,0,sizeof(resource->report_error.message)) &&
        qa_source_save_bool(io,&allocation);
    if(reading) resource->report_error.code=(qa_status)code;
    if(ok && allocation) {
        ok=(reading || qa_bot_memory_reference(resource->memory,resource->record.allocation,&reference,io->error)) &&
            qa_source_save_count(io,&reference,SIZE_MAX);
        if(ok && reading) {
            resource->record.memory=resource->memory;
            ok=qa_bot_memory_resolve(resource->memory,reference,&resource->record.allocation,io->error);
        }
    }
    if(ok) ok=diagnostics(io,resource) && source_fields(io,resource) && pending_fields(io,resource) &&
        qualified(io,resource,allocation,bound);
    if(ok && reading && bound)
        ok=bot_weapon_resource_bind(resource,resource->record,resource->path,io->error);
    if(reading) {
        if(ok) *out=resource;
        else bot_weapon_resource_destroy(resource);
    }
    if(!ok && !io->failed) return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid retained weapon resource fields");
    return ok;
}
