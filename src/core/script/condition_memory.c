#include "internal.h"
#include "qa/binary.h"

bool script_memory_bind(qa_script *s,qa_error *error)
{
    s->next_condition_pointer=1;
    const qa_script_memory *memory=s->services.memory;
    if(!memory) return true;
    if(!memory->context || !memory->retain || !memory->release || !memory->allocate || !memory->bytes ||
       !memory->free || !memory->reference || !memory->resolve || !memory->resolve_history) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Script MEMORY requires its complete retained allocation owner");
        return false;
    }
    s->memory=*memory;
    if(!s->memory.retain(s->memory.context,error)) {s->memory=(qa_script_memory){0};return false;}
    s->services.memory=&s->memory;
    return true;
}
static bool extent(const qa_script *s,script_condition_record *record,uint8_t **out,qa_error *error)
{
    if(!record || !record->bytes) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Conditional pointer does not name its actual live indent");return false;
    }
    if(s->memory.context && !record->detached) {
        qa_script_memory_span span;
        if(!s->memory.bytes(s->memory.context,record->allocation,&span,error)) return false;
        if(span.size!=16) {qa_error_set(error,QA_ERROR_FORMAT,0,"Conditional allocation differs from indent_t extent");return false;}
        record->bytes=span.data;
    }
    *out=record->bytes;return true;
}
static script_condition_record *resolve(const qa_script *s,uint32_t pointer)
{
    for(size_t i=0;i<s->condition_records;++i)
        if(s->conditions[i].bytes && s->conditions[i].pointer==pointer) return s->conditions+i;
    return NULL;
}
bool script_condition_top(qa_script *s,script_condition *out,qa_error *error)
{
    uint8_t *bytes;
    if(!script_indent_head(s) || !extent(s,resolve(s,script_indent_head(s)),&bytes,error)) return false;
    uint32_t frame=qa_load_u32le(bytes+8),type=qa_load_u32le(bytes);
    if(!frame || frame>s->frame_count || (type!=1 && type!=2 && type!=4 && type!=8 && type!=16)) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Conditional script/type pointer is outside its actual source");return false;
    }
    *out=(script_condition){qa_load_u32le(bytes+4)!=0,type==2,(size_t)frame-1};return true;
}
static bool record(qa_script *s,script_condition_record **out,qa_error *error)
{
    size_t slot=0;
    while(slot<s->condition_records && s->conditions[slot].bytes) ++slot;
    if(slot==s->condition_records) {
        if(!script_grow((void **)&s->conditions,&s->condition_capacity,slot+1,sizeof(*s->conditions),error)) return false;
        ++s->condition_records;
    }
    s->conditions[slot]=(script_condition_record){.memory_reference=SIZE_MAX};
    *out=s->conditions+slot;return true;
}
bool script_condition_push(qa_script *s,uint32_t type,bool skip,size_t frame,qa_error *error)
{
    if(frame>=UINT32_MAX || !s->next_condition_pointer || s->next_condition_pointer==UINT32_MAX) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Conditional source pointer identities exhausted");return false;
    }
    script_condition_record *node;
    if(!record(s,&node,error)) return false;
    if(s->memory.context) {
        if(!s->memory.allocate(s->memory.context,16,false,&node->allocation,error)) return false;
        qa_script_memory_span span={0};
        bool borrowed=s->memory.bytes(s->memory.context,node->allocation,&span,error);
        if(!borrowed || span.size!=16) {
            (void)s->memory.free(s->memory.context,node->allocation,NULL);
            if(borrowed) qa_error_set(error,QA_ERROR_FORMAT,0,"Allocated indent differs from its source extent");
            return false;
        }
        node->bytes=span.data;
    } else {
        node->bytes=calloc(16,1);
        if(!node->bytes) {qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating raw conditional indent");return false;}
    }
    node->pointer=s->next_condition_pointer++;
    qa_store_u32le(node->bytes,type);qa_store_u32le(node->bytes+4,skip);
    qa_store_u32le(node->bytes+8,(uint32_t)frame+1);qa_store_u32le(node->bytes+12,script_indent_head(s));
    script_indent_head_set(s,node->pointer);++s->condition_count;
    if(skip) script_skipping_set(s,script_skipping(s)+1);
    return true;
}
static bool release_condition(qa_script *s,bool branch,qa_error *error)
{
    script_condition_record *node=resolve(s,script_indent_head(s));uint8_t *bytes;
    if(!extent(s,node,&bytes,error)) return false;
    uint32_t next=qa_load_u32le(bytes+12);bool skip=qa_load_u32le(bytes+4)!=0;
    if(next && !resolve(s,next)) {qa_error_set(error,QA_ERROR_FORMAT,0,"Conditional next pointer is outside its source");return false;}
    if(s->memory.context) {if(!s->memory.free(s->memory.context,node->allocation,error)) return false;}
    else free(bytes);
    node->bytes=NULL;script_indent_head_set(s,next);--s->condition_count;
    if(branch && skip) script_skipping_set(s,script_skipping(s)-1);
    return true;
}
bool script_condition_pop(qa_script *s,qa_error *error) {return release_condition(s,true,error);}
bool script_conditions_capture(const qa_script *s,qa_script_condition_state *out,qa_error *error)
{
    uint32_t pointer=script_indent_head(s);
    for(size_t i=s->condition_count;i;--i) {
        script_condition_record *node=resolve(s,pointer);uint8_t *bytes;
        if(!extent(s,node,&bytes,error)) return false;
        uint32_t frame=qa_load_u32le(bytes+8);
        if(!frame) {qa_error_set(error,QA_ERROR_FORMAT,0,"Conditional has a null source script pointer");return false;}
        qa_script_condition_state *saved=out+i-1;
        *saved=(qa_script_condition_state){.frame=(size_t)frame-1,.skip=qa_load_u32le(bytes+4)!=0,
            .was_else=qa_load_u32le(bytes)==2,.pointer=pointer,.memory_reference=node->memory_reference};
        if(s->memory.context && !node->detached && !s->memory.reference(s->memory.context,node->allocation,&saved->memory_reference,error)) return false;
        memcpy(saved->bytes,bytes,16);pointer=qa_load_u32le(bytes+12);
    }
    if(pointer) {qa_error_set(error,QA_ERROR_FORMAT,0,"Conditional chain exceeds its actual source count");return false;}
    return true;
}
bool script_conditions_restore(qa_script *s,const qa_script_checkpoint *checkpoint,qa_error *error)
{
    s->next_condition_pointer=checkpoint->next_condition_pointer;
    for(size_t i=0;i<checkpoint->condition_count;++i) {
        const qa_script_condition_state *saved=checkpoint->conditions+i;script_condition_record *node;
        if(!record(s,&node,error)) return false;
        node->pointer=saved->pointer;node->memory_reference=saved->memory_reference;
        if(s->memory.context && !s->memory_deferred) {
            if(saved->memory_reference==SIZE_MAX) {
                qa_error_set(error,QA_ERROR_FORMAT,i,"Saved indent has no actual script MEMORY allocation");return false;
            }
            if(!s->memory.resolve(s->memory.context,saved->memory_reference,&node->allocation,error)) return false;
            qa_script_memory_span span;
            if(!s->memory.bytes(s->memory.context,node->allocation,&span,error)) return false;
            if(span.size!=16 || memcmp(span.data,saved->bytes,16)) {
                qa_error_set(error,QA_ERROR_FORMAT,i,"Saved indent bytes differ from restored script MEMORY");return false;
            }
            node->bytes=span.data;
        } else {
            node->bytes=malloc(16);
            if(!node->bytes) {qa_error_set(error,QA_ERROR_MEMORY,i,"Restoring detached raw indent");return false;}
            memcpy(node->bytes,saved->bytes,16);node->detached=true;
        }
        ++s->condition_count;
    }
    return true;
}
void script_conditions_close(qa_script *s,bool source)
{
    if(source && !s->memory_deferred)
        while(s->condition_count && release_condition(s,false,NULL)) {}
    for(size_t i=0;i<s->condition_records;++i) {
        script_condition_record *node=s->conditions+i;
        if(!node->bytes) continue;
        if(node->detached || !s->memory.context) free(node->bytes);
        else if(source) (void)s->memory.free(s->memory.context,node->allocation,NULL);
    }
    free(s->conditions);

}

bool script_memory_enter(qa_script *s,qa_error *error)
{
    if(!s->memory_deferred) return true;
    if(!script_source_adopt(s,error)) return false;
    for(size_t i=0;i<s->frame_count;++i) if(!script_lexer_adopt(s->frames[i].lexer,error)) return false;
    if(s->memory.context) {
        for(size_t i=0;i<s->condition_records;++i) {
            script_condition_record *node=s->conditions+i;
            if(!node->bytes) continue;
            qa_script_memory_span span;
            if(node->memory_reference==SIZE_MAX ||
               !s->memory.resolve_history(s->memory.context,node->memory_reference,&node->allocation,error) ||
               !s->memory.bytes(s->memory.context,node->allocation,&span,error)) return false;
            if(span.size!=16 || memcmp(span.data,node->bytes,16)) {
                qa_error_set(error,QA_ERROR_FORMAT,i,"History indent differs from committed script MEMORY");return false;
            }
        }
        for(size_t i=0;i<s->condition_records;++i) {
            script_condition_record *node=s->conditions+i;
            if(!node->bytes) continue;
            qa_script_memory_span span;
            if(!s->memory.bytes(s->memory.context,node->allocation,&span,error)) return false;
            free(node->bytes);node->bytes=span.data;node->detached=false;
        }
    }
    s->memory_deferred=false;return true;
}

bool qa_script_adopt_memory(qa_script *s,qa_error *error)
{
    return !s || script_memory_enter(s,error);
}
