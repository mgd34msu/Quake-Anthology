#include "internal.h"
#include "source_fuzzy_save.h"
#include "../save_fields.h"
#include "qa/bots_allocator_save.h"

static const uint8_t magic[8]={'Q','A','F','U','Z','Z','Y',0};
static bool fail(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_FORMAT,0,"%s",message);return false;
}
bool bot_fuzzy_store_reference(const bot_fuzzy_store *store,const bot_fuzzy_owned *config,
    size_t *out,qa_error *error) {
    if(!store || !out || !config) return fail(error,"Missing actual fuzzy configuration reference");
    size_t index=0;
    for(const bot_fuzzy_owned *row=store->first;row;row=row->next,++index)
        if(row==config) {*out=index;return true;}
    return fail(error,"Fuzzy configuration is outside its actual ordered owner");
}
bool bot_fuzzy_store_resolve(const bot_fuzzy_store *store,size_t reference,bot_fuzzy_owned **out,qa_error *error) {
    if(!store || !out) return fail(error,"Missing fuzzy configuration owner/output");
    bot_fuzzy_owned *row=store->first;
    while(row && reference) {row=row->next;--reference;}
    if(!row) return fail(error,"Saved fuzzy configuration reference is missing");
    *out=row;return true;
}
static bool allocation_fields(qa_source_save_io *io,bot_fuzzy_heap *heap,qa_bot_memory_allocation *allocation) {
    size_t reference=0;bool reading=io->direction==QA_SOURCE_SAVE_READ;
    bool ok=reading || qa_bot_memory_reference(heap->memory,*allocation,&reference,io->error);
    if(ok) ok=qa_source_save_count(io,&reference,SIZE_MAX);
    if(ok && reading) ok=qa_bot_memory_resolve(heap->memory,reference,allocation,io->error);
    if(!ok) io->failed=true;return ok;
}
static bool diagnostics_fields(qa_source_save_io *io,bot_fuzzy_diagnostic **rows,size_t *count) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!qa_source_save_count(io,count,SIZE_MAX)) return false;
    if(reading && *count) {
        if(io->offset>io->input.size || *count>(io->input.size-io->offset)/30 || *count>SIZE_MAX/sizeof(**rows))
            return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated source fuzzy diagnostics");
        *rows=calloc(*count,sizeof(**rows));
        if(!*rows) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring source fuzzy diagnostics");
    }
    for(size_t index=0;index<*count;++index) {
        bot_fuzzy_diagnostic *row=&(*rows)[index];uint32_t severity=(uint32_t)row->value.severity;
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
        if(!ok) return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid source fuzzy diagnostic");
    }
    return true;
}
typedef struct fuzzy_visit {bot_fuzzy_pointer *row;unsigned stage;} fuzzy_visit;
static size_t pointer_index(bot_fuzzy_pointer **rows,size_t count,uint32_t pointer) {
    for(size_t index=0;index<count;++index) if(rows[index]->pointer==pointer) return index;
    return SIZE_MAX;
}
bool bot_fuzzy_heap_topology(bot_fuzzy_heap *heap,qa_error *error) {
    if(!heap || !heap->memory || !heap->next_pointer || heap->next_pointer>UINT64_C(0x100000000))
        return fail(error,"Invalid source fuzzy heap owner or next pointer");
    size_t count=0;bot_fuzzy_pointer *last=NULL;
    for(bot_fuzzy_pointer *row=heap->first;row;row=row->next) {++count;last=row;}
    if(last!=heap->last || count>SIZE_MAX/sizeof(bot_fuzzy_pointer *) || count>SIZE_MAX/sizeof(fuzzy_visit))
        return fail(error,"Invalid fuzzy pointer chain extent");
    bot_fuzzy_pointer **rows=count?malloc(count*sizeof(*rows)):NULL;
    uint8_t *marks=count?calloc(count,1):NULL;
    fuzzy_visit *stack=count?malloc(count*sizeof(*stack)):NULL;
    if(count && (!rows || !marks || !stack)) {
        free(rows);free(marks);free(stack);qa_error_set(error,QA_ERROR_MEMORY,0,"Validating source fuzzy pointer graph");return false;
    }
    size_t index=0;bool ok=true;
    for(bot_fuzzy_pointer *row=heap->first;row;row=row->next) rows[index++]=row;
    for(index=0;ok && index<count;++index) {
        bot_fuzzy_pointer *row=rows[index];qa_bot_memory_span bytes;
        ok=row->pointer && row->pointer<heap->next_pointer &&
            (row->kind==BOT_FUZZY_NAME || row->kind==BOT_FUZZY_SEPARATOR) &&
            qa_bot_memory_bytes(heap->memory,row->allocation,&bytes,error);
        for(size_t prior=0;ok && prior<index;++prior) if(rows[prior]->pointer==row->pointer) ok=false;
        if(ok) ok=row->kind==BOT_FUZZY_NAME?memchr(bytes.data,0,bytes.size)!=NULL:bytes.size==BOT_FUZZY_SEPARATOR_BYTES;
    }
    for(index=0;ok && index<count;++index) {
        if(rows[index]->kind!=BOT_FUZZY_SEPARATOR || marks[index]==2) continue;
        size_t pending=1;stack[0]=(fuzzy_visit){rows[index],0};marks[index]=1;
        while(ok && pending) {
            fuzzy_visit *frame=&stack[pending-1];
            if(frame->stage==2) {marks[pointer_index(rows,count,frame->row->pointer)]=2;--pending;continue;}
            bot_fuzzy_separator separator={heap,frame->row->pointer};uint32_t child;
            ok=bot_fuzzy_separator_word_read(&separator,frame->stage++?BOT_FUZZY_NEXT:BOT_FUZZY_CHILD,&child,error);
            if(!ok || !child) continue;
            size_t at=pointer_index(rows,count,child);
            if(at==SIZE_MAX || rows[at]->kind!=BOT_FUZZY_SEPARATOR || marks[at]==1) {ok=false;break;}
            if(marks[at]==2) continue;
            marks[at]=1;stack[pending++]=(fuzzy_visit){rows[at],0};
        }
    }
    free(rows);free(marks);free(stack);
    return ok || fail(error,"Invalid source fuzzy pointer graph");
}
static bool store_topology(bot_fuzzy_store *store,qa_error *error) {
    if(!store || store->closed || store->active || !store->library || store->cached_count>128)
        return fail(error,"Fuzzy continuation requires its actual idle live owner");
    if(!bot_fuzzy_heap_topology(&store->heap,error)) return false;
    bot_fuzzy_owned *last=NULL;
    for(bot_fuzzy_owned *config=store->first;config;config=config->next) {
        int32_t count;
        if(config->store!=store || !config->owned || config->disposed || !config->path || !config->path[0] ||
            config->source.heap!=&store->heap || !bot_fuzzy_config_count(&config->source,&count,error))
            return fail(error,"Invalid retained source fuzzy configuration");
        for(int32_t index=0;index<count;++index) {
            uint32_t pointer;qa_bytes name;bot_fuzzy_separator separator;
            if(!bot_fuzzy_config_pointer_read(&config->source,index,false,&pointer,error) ||
                !bot_fuzzy_name_read(&store->heap,pointer,&name,error) ||
                !bot_fuzzy_config_pointer_read(&config->source,index,true,&pointer,error) ||
                !bot_fuzzy_separator_bind(&store->heap,pointer,&separator,error)) return false;
        }
        last=config;
    }
    return last==store->last || fail(error,"Invalid source fuzzy configuration tail");
}
static bool pointer_fields(qa_source_save_io *io,bot_fuzzy_heap *heap) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;size_t count=0;
    if(!reading) for(bot_fuzzy_pointer *row=heap->first;row;row=row->next) ++count;
    if(!qa_source_save_u64(io,&heap->next_pointer) || !qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/16))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated source fuzzy pointer chain");
    bot_fuzzy_pointer **tail=&heap->first;
    for(size_t index=0;index<count;++index) {
        if(reading) {
            *tail=calloc(1,sizeof(**tail));
            if(!*tail) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring source fuzzy pointer metadata");
        }
        bot_fuzzy_pointer *row=*tail;uint32_t kind=(uint32_t)row->kind;
        bool ok=qa_source_save_u32(io,&row->pointer) && qa_source_save_u32(io,&kind) &&
            allocation_fields(io,heap,&row->allocation);
        if(reading) {row->kind=(bot_fuzzy_pointer_kind)kind;heap->last=row;}
        if(!ok) return false;tail=&row->next;
    }
    return true;
}
static bool config_fields(qa_source_save_io *io,bot_fuzzy_store *store) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;size_t count=0;
    if(!reading) for(bot_fuzzy_owned *row=store->first;row;row=row->next) ++count;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/18))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated source fuzzy configuration chain");
    bot_fuzzy_owned **tail=&store->first;
    for(size_t index=0;index<count;++index) {
        if(reading) {
            *tail=calloc(1,sizeof(**tail));
            if(!*tail) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring source fuzzy configuration identity");
            (*tail)->references=1;(*tail)->store=store;(*tail)->owned=true;
            (*tail)->source.heap=&store->heap;++store->references;store->last=*tail;
        }
        bot_fuzzy_owned *row=*tail;const char *path=reading?NULL:row->path;
        bool ok=allocation_fields(io,&store->heap,&row->source.allocation) && bot_save_text(io,&path);
        if(reading) row->path=(char *)path;
        if(ok) ok=diagnostics_fields(io,&row->reported,&row->reported_count);
        if(!ok) return false;tail=&row->next;
    }
    if(!qa_source_save_count(io,&store->cached_count,128)) return false;
    for(size_t index=0;index<store->cached_count;++index) {
        int64_t reference=-1;
        if(!reading && store->cached[index]) {
            size_t at;qa_error ignored={0};
            reference=bot_fuzzy_store_reference(store,store->cached[index],&at,&ignored)?(int64_t)at:-2;
        }
        if(!qa_source_save_i64(io,&reference)) return false;
        if(reading && reference!=-1) {
            bot_fuzzy_owned *config=NULL;
            if(reference<0 || (uint64_t)reference>SIZE_MAX ||
                !bot_fuzzy_store_resolve(store,(size_t)reference,&config,io->error))
                return bot_save_fail(io,QA_ERROR_FORMAT,"Missing saved fuzzy cache configuration");
            bot_fuzzy_owned_retain(config);store->cached[index]=config;
        }
    }
    return true;
}
static bool reader_fields(qa_source_save_io *io,bot_fuzzy_store *store) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;size_t count=0;
    if(!reading) for(bot_fuzzy_reader *row=store->readers;row;row=row->next) ++count;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/282))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated retained fuzzy reader chain");
    bot_fuzzy_reader *row=reading?NULL:store->readers;
    for(size_t index=0;index<count;++index) {
        if(reading) {
            row=NULL;
            if(!bot_fuzzy_reader_create(store,0,&row,io->error)) {io->failed=true;return false;}
        }
        uint32_t code=(uint32_t)row->report_error.code;
        bool ok=qa_source_save_u64(io,&row->generation) && qa_source_save_bool(io,&row->report_failed) &&
            qa_source_save_u32(io,&code) && code<=QA_ERROR_NOT_FOUND &&
            qa_source_save_count(io,&row->report_error.offset,SIZE_MAX) &&
            qa_source_save_bytes(io,row->report_error.message,sizeof(row->report_error.message)) &&
            memchr(row->report_error.message,0,sizeof(row->report_error.message));
        if(reading) row->report_error.code=(qa_status)code;
        if(ok) ok=diagnostics_fields(io,&row->reported,&row->reported_count);
        if(reading) row->reported_capacity=row->reported_count;
        qa_script_checkpoint state={0};qa_buffer encoded={0};size_t extent=0;
        if(ok && !reading) ok=qa_script_capture(row->source,&state,io->error) &&
            qa_script_checkpoint_encode(&state,&encoded,io->error);
        if(!reading) extent=encoded.size;
        if(ok) ok=qa_source_save_count(io,&extent,SIZE_MAX);
        if(ok && reading) {
            if(io->offset>io->input.size || extent>io->input.size-io->offset)
                ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated retained fuzzy source");
            else {
                qa_script_services services=bot_fuzzy_reader_services(row);
                ok=qa_script_checkpoint_decode((qa_bytes){io->input.data+io->offset,extent},&state,io->error) &&
                    qa_script_restore(&services,&state,&row->source,io->error);
                if(ok) io->offset+=extent;
            }
        } else if(ok) ok=qa_source_save_bytes(io,encoded.data,extent);
        qa_script_checkpoint_free(&state);qa_buffer_free(&encoded);
        if(!ok) {io->failed=true;return false;}
        if(!reading) row=row->next;
    }
    if(reading) {
        bot_fuzzy_reader *previous=NULL,*next;
        for(row=store->readers;row;row=next) {next=row->next;row->next=previous;previous=row;}
        store->readers=previous;
    }
    return true;
}
static bool fields(qa_source_save_io *io,bot_fuzzy_store *store) {
    return bot_save_signature(io,magic) && qa_source_save_u64(io,&store->generation) &&
        pointer_fields(io,&store->heap) && config_fields(io,store) && reader_fields(io,store);
}
bool bot_fuzzy_store_capture(const bot_fuzzy_store *source,qa_buffer *out,qa_error *error) {
    bot_fuzzy_store *store=(bot_fuzzy_store *)source;
    if(!out || !store_topology(store,error)) return false;
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && fields(&io,store) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);return ok;
}
bool bot_fuzzy_store_restore(qa_bot_library *library,qa_bytes bytes,bot_fuzzy_store **out,qa_error *error) {
    if(!library || !out || *out) return fail(error,"Fuzzy restore requires actual imported MEMORY and empty output");
    bot_fuzzy_store *store=NULL;
    if(!bot_fuzzy_store_create(library,&store,error)) return false;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && fields(&io,store) &&
        qa_source_save_finish(&io,NULL) && store_topology(store,error);
    qa_source_save_dispose(&io);
    if(!ok) {bot_fuzzy_store_dispose(store);return false;}
    *out=store;return true;
}
