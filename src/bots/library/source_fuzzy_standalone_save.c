#include "internal.h"
#include "source_fuzzy_store.h"
#include "source_fuzzy_view.h"
#include "source_fuzzy_standalone_save.h"
#include "source_fuzzy_save.h"
#include "../save_fields.h"
#include "qa/bots_allocator_save.h"

static bool memory_fields(qa_source_save_io *io,qa_bot_memory *memory)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;qa_buffer bytes={0};size_t extent=0;
    bool ok=reading || qa_bot_memory_capture(memory,&bytes,io->error);
    if(!reading) extent=bytes.size;
    if(ok) ok=qa_source_save_count(io,&extent,SIZE_MAX);
    if(ok && reading) {
        if(io->offset>io->input.size || extent>io->input.size-io->offset)
            ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated standalone fuzzy MEMORY");
        else {
            ok=qa_bot_memory_restore(memory,(qa_bytes){io->input.data+io->offset,extent},io->error);
            if(ok) io->offset+=extent;
        }
    } else if(ok) ok=qa_source_save_bytes(io,bytes.data,extent);
    qa_buffer_free(&bytes);if(!ok) io->failed=true;return ok;
}
static bool allocation_fields(qa_source_save_io *io,qa_bot_memory *memory,qa_bot_memory_allocation *allocation)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;size_t reference=0;
    bool ok=reading || qa_bot_memory_reference(memory,*allocation,&reference,io->error);
    if(ok) ok=qa_source_save_count(io,&reference,SIZE_MAX);
    if(ok && reading) ok=qa_bot_memory_resolve(memory,reference,allocation,io->error);
    if(!ok) io->failed=true;
    return ok;
}
static bool pointer_fields(qa_source_save_io *io,bot_fuzzy_heap *heap)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;size_t count=0;
    if(!reading) for(bot_fuzzy_pointer *row=heap->first;row;row=row->next) ++count;
    if(!qa_source_save_u64(io,&heap->next_pointer) || !heap->next_pointer ||
       heap->next_pointer>UINT64_C(0x100000000) || !qa_source_save_count(io,&count,SIZE_MAX))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid standalone fuzzy pointer count");
    if(reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/16))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated standalone fuzzy pointer aliases");
    bot_fuzzy_pointer **tail=&heap->first,*last=NULL;
    for(size_t i=0;i<count;++i) {
        if(reading) {
            *tail=calloc(1,sizeof(**tail));
            if(!*tail) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring standalone fuzzy pointer aliases");
        }
        bot_fuzzy_pointer *row=*tail;uint32_t kind=(uint32_t)row->kind;
        bool ok=qa_source_save_u32(io,&row->pointer) && row->pointer && row->pointer<heap->next_pointer &&
            qa_source_save_u32(io,&kind) && kind<=BOT_FUZZY_SEPARATOR &&
            allocation_fields(io,heap->memory,&row->allocation);
        if(reading) row->kind=(bot_fuzzy_pointer_kind)kind;
        qa_bot_memory_span bytes;
        if(ok) ok=qa_bot_memory_bytes(heap->memory,row->allocation,&bytes,io->error) &&
            (row->kind==BOT_FUZZY_NAME?memchr(bytes.data,0,bytes.size)!=NULL:bytes.size==BOT_FUZZY_SEPARATOR_BYTES);
        for(bot_fuzzy_pointer *prior=heap->first;ok && prior!=row;prior=prior->next)
            if(prior->pointer==row->pointer) ok=false;
        if(!ok) return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid standalone fuzzy pointer alias");
        last=row;if(reading) heap->last=row;tail=&row->next;
    }
    return !*tail && last==heap->last ? true : bot_save_fail(io,QA_ERROR_FORMAT,"Invalid standalone fuzzy pointer tail");
}
static bool diagnostic_fields(qa_source_save_io *io,bot_fuzzy_owned *config)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!qa_source_save_count(io,&config->reported_count,SIZE_MAX)) return false;
    if(reading && config->reported_count) {
        if(io->offset>io->input.size || config->reported_count>(io->input.size-io->offset)/30 ||
           config->reported_count>SIZE_MAX/sizeof(*config->reported))
            return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated standalone fuzzy diagnostics");
        config->reported=calloc(config->reported_count,sizeof(*config->reported));
        if(!config->reported) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring standalone fuzzy diagnostics");
    }
    for(size_t i=0;i<config->reported_count;++i) {
        bot_fuzzy_diagnostic *row=&config->reported[i];uint32_t severity=(uint32_t)row->value.severity;
        const char *path=reading?NULL:row->path,*message=reading?NULL:row->message;
        bool ok=qa_source_save_u32(io,&severity) && severity<=QA_SCRIPT_FATAL &&
            bot_save_text(io,&path) && path && bot_save_text(io,&message) && message;
        if(reading) {
            row->path=(char *)path;row->message=(char *)message;
            row->value.severity=(qa_script_severity)severity;
            row->value.location.path=path;row->value.message=message;
        }
        if(ok) ok=qa_source_save_u32(io,&row->value.location.line) &&
            qa_source_save_u32(io,&row->value.location.column) &&
            qa_source_save_count(io,&row->value.location.offset,SIZE_MAX);
        if(!ok) return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid standalone fuzzy diagnostic");
    }
    return true;
}
bool bot_weights_source_fields(qa_source_save_io *io,const qa_bot_weights *source,qa_bot_weights **out)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;qa_bot_weights *weights=(qa_bot_weights *)source;
    qa_bot_memory *memory=NULL;
    if(reading) {
        weights=calloc(1,sizeof(*weights));
        if(!weights) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring standalone fuzzy resource");
        atomic_init(&weights->references,1);
        weights->source=calloc(1,sizeof(*weights->source));
        weights->standalone_heap=calloc(1,sizeof(*weights->standalone_heap));
        if(weights->source) weights->source->references=1;
        if(!weights->source || !weights->standalone_heap || !qa_bot_memory_create(NULL,&memory,io->error)) goto failed;
        if(!bot_fuzzy_heap_bind(weights->standalone_heap,memory,io->error)) goto failed;
        weights->standalone_heap->standalone_users=1;
        (void)qa_bot_memory_release(memory,NULL);memory=NULL;
        weights->source->source.heap=weights->standalone_heap;
    } else if(!weights || !weights->source || !bot_fuzzy_owned_open(weights->source,io->error)) {
        io->failed=true;return false;
    }
    bot_fuzzy_owned *config=weights->source;bot_fuzzy_heap *heap=config->source.heap;
    const char *path=reading?NULL:config->path;
    bool ok=memory_fields(io,heap->memory) && pointer_fields(io,heap) && bot_fuzzy_heap_topology(heap,io->error) &&
        allocation_fields(io,heap->memory,&config->source.allocation) && bot_save_text(io,&path);
    if(reading) config->path=(char *)path;
    if(ok) ok=path && *path && diagnostic_fields(io,config);
    if(ok) {
        int32_t count;ok=bot_fuzzy_config_count(&config->source,&count,io->error);
        if(ok && reading) ok=bot_weights_source_view(weights,io->error);
    }
    if(!ok) goto failed;
    if(reading) *out=weights;
    return true;
failed:
    if(memory) (void)qa_bot_memory_release(memory,NULL);
    if(reading) qa_bot_weights_release(weights);
    io->failed=true;return false;
}
bool bot_weights_source_alias_fields(qa_source_save_io *io,qa_bot_weights *first,
    const qa_bot_weights *source,qa_bot_weights **out)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;qa_bot_weights *weights=(qa_bot_weights *)source;
    if(!first || !first->source || !first->source->source.heap ||
       (reading && (!first->standalone_heap || first->standalone_heap->standalone_users==SIZE_MAX)) ||
       (!reading && (!source || !source->source || source->source->source.heap!=first->source->source.heap ||
        !bot_fuzzy_owned_open(source->source,io->error))))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Fuzzy alias differs from its actual standalone owner");
    if(reading) {
        weights=calloc(1,sizeof(*weights));
        if(!weights) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring shared standalone fuzzy parent");
        atomic_init(&weights->references,1);weights->source=calloc(1,sizeof(*weights->source));
        if(!weights->source) {qa_bot_weights_release(weights);return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring shared fuzzy configuration identity");}
        weights->source->references=1;weights->standalone_heap=first->standalone_heap;
        ++weights->standalone_heap->standalone_users;weights->source->source.heap=weights->standalone_heap;
    }
    bot_fuzzy_owned *config=weights->source;
    const char *path=reading?NULL:config->path;
    bool ok=allocation_fields(io,config->source.heap->memory,&config->source.allocation) && bot_save_text(io,&path);
    if(reading) config->path=(char *)path;
    if(ok) ok=path && *path && diagnostic_fields(io,config);
    if(ok && reading) ok=bot_weights_source_view(weights,io->error);
    if(!ok) {
        if(reading) qa_bot_weights_release(weights);
        io->failed=true;return false;
    }
    if(reading) *out=weights;
    return true;
}
