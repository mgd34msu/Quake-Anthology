#include "internal.h"

static bool record_bytes(qa_script *s,uint8_t **out,qa_error *error)
{
    script_source_record *record=&s->source_record;
    if(!record->bytes) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source header has no actual allocation");return false;
    }
    if(s->memory.context && !record->detached) {
        qa_script_memory_span span;
        if(!s->memory.bytes(s->memory.context,record->allocation,&span,error)) return false;
        if(span.size!=SCRIPT_SOURCE_BYTES) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"Source allocation differs from source_t extent");return false;
        }
        record->bytes=span.data;
    }
    *out=record->bytes;return true;
}
static void copy_path(uint8_t *bytes,const char *path)
{
    size_t length=strlen(path);
    if(length>64) length=64;
    memcpy(bytes,path,length);
}
bool script_source_create(qa_script *s,qa_error *error)
{
    script_source_record *record=&s->source_record;
    record->memory_reference=SIZE_MAX;
    if(s->memory.context) {
        if(!s->memory.allocate(s->memory.context,SCRIPT_SOURCE_BYTES,false,&record->allocation,error)) return false;
        qa_script_memory_span span={0};
        bool borrowed=s->memory.bytes(s->memory.context,record->allocation,&span,error);
        if(!borrowed || span.size!=SCRIPT_SOURCE_BYTES) {
            (void)s->memory.free(s->memory.context,record->allocation,NULL);
            if(borrowed) qa_error_set(error,QA_ERROR_FORMAT,0,"Allocated source has the wrong extent");
            return false;
        }
        record->bytes=span.data;
    } else {
        record->bytes=malloc(SCRIPT_SOURCE_BYTES);
        if(!record->bytes) {qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating raw source header");return false;}
    }
    memset(record->bytes,0,SCRIPT_SOURCE_BYTES);
    copy_path(record->bytes,s->frames[0].resource.path);
    copy_path(record->bytes+SCRIPT_SOURCE_INCLUDE,s->options.include_path);
    size_t length=strlen((char *)record->bytes+SCRIPT_SOURCE_INCLUDE);
    if(length && record->bytes[SCRIPT_SOURCE_INCLUDE+length-1]!='/' &&
       record->bytes[SCRIPT_SOURCE_INCLUDE+length-1]!='\\') record->bytes[SCRIPT_SOURCE_INCLUDE+length]='/';
    s->options.include_path=(char *)record->bytes+SCRIPT_SOURCE_INCLUDE;
    script_source_stack(s);return true;
}
void script_source_stack(qa_script *s)
{
    if(s->source_record.bytes)
        qa_store_u32le(s->source_record.bytes+SCRIPT_SOURCE_STACK,
            s->stack_count?(uint32_t)s->stack[s->stack_count-1]+1:0);
}
bool script_source_capture(const qa_script *source,qa_script_checkpoint *out,qa_arena *arena,qa_error *error)
{
    qa_script *s=(qa_script *)source;uint8_t *bytes;
    if(!record_bytes(s,&bytes,error)) return false;
    uint8_t *copy=qa_arena_alloc(arena,SCRIPT_SOURCE_BYTES,1,error);
    if(!copy) return false;
    memcpy(copy,bytes,SCRIPT_SOURCE_BYTES);
    out->source_record=(qa_bytes){copy,SCRIPT_SOURCE_BYTES};
    out->source_reference=s->source_record.memory_reference;
    return !s->memory.context || s->source_record.detached ||
        s->memory.reference(s->memory.context,s->source_record.allocation,&out->source_reference,error);
}
bool script_source_restore(qa_script *s,const qa_script_checkpoint *saved,qa_error *error)
{
    script_source_record *record=&s->source_record;
    record->memory_reference=saved->source_reference;
    if(s->memory.context && !s->memory_deferred) {
        qa_script_memory_span span;
        if(saved->source_reference==SIZE_MAX) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"Saved source has no MEMORY allocation");return false;
        }
        if(!s->memory.resolve(s->memory.context,saved->source_reference,&record->allocation,error) ||
           !s->memory.bytes(s->memory.context,record->allocation,&span,error)) return false;
        if(span.size!=SCRIPT_SOURCE_BYTES || memcmp(span.data,saved->source_record.data,SCRIPT_SOURCE_BYTES)) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"Saved source differs from restored MEMORY");return false;
        }
        record->bytes=span.data;
    } else {
        record->bytes=malloc(SCRIPT_SOURCE_BYTES);
        if(!record->bytes) {qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring detached source header");return false;}
        memcpy(record->bytes,saved->source_record.data,SCRIPT_SOURCE_BYTES);record->detached=true;
    }
    s->options.include_path=(char *)record->bytes+SCRIPT_SOURCE_INCLUDE;return true;
}
bool script_source_adopt(qa_script *s,qa_error *error)
{
    script_source_record *record=&s->source_record;
    if(!s->memory.context || !record->detached) return true;
    qa_script_memory_span span;
    if(!s->memory.resolve_history(s->memory.context,record->memory_reference,&record->allocation,error) ||
       !s->memory.bytes(s->memory.context,record->allocation,&span,error)) return false;
    if(span.size!=SCRIPT_SOURCE_BYTES || memcmp(span.data,record->bytes,SCRIPT_SOURCE_BYTES)) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"History source differs from committed MEMORY");return false;
    }
    free(record->bytes);record->bytes=span.data;record->detached=false;
    s->options.include_path=(char *)record->bytes+SCRIPT_SOURCE_INCLUDE;return true;
}
void script_source_close(qa_script *s,bool source)
{
    script_source_record *record=&s->source_record;
    if(!record->bytes) return;
    if(record->detached || !s->memory.context) free(record->bytes);
    else if(source) (void)s->memory.free(s->memory.context,record->allocation,NULL);
    record->bytes=NULL;
}
