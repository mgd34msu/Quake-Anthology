#include "internal.h"

bool script_lexer_memory_bind(qa_script_lexer *l,qa_error *error)
{
    const qa_script_memory *memory=l->options.memory;
    if(!memory) return true;
    if(!memory->context || !memory->retain || !memory->release || !memory->allocate ||
       !memory->bytes || !memory->free || !memory->reference || !memory->resolve || !memory->resolve_history) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Lexer MEMORY requires its complete retained owner");return false;
    }
    l->memory=*memory;
    if(!l->memory.retain(l->memory.context,error)) {l->memory=(qa_script_memory){0};return false;}
    l->options.memory=&l->memory;return true;
}
static bool allocate(qa_script_lexer *l,script_lexer_allocation *record,uint32_t size,bool clear,qa_error *error)
{
    record->size=size;record->reference=SIZE_MAX;
    if(l->memory.context) {
        if(!l->memory.allocate(l->memory.context,size,clear,&record->allocation,error)) return false;
        qa_script_memory_span span={0};bool borrowed=l->memory.bytes(l->memory.context,record->allocation,&span,error);
        if(!borrowed || span.size!=size) {
            (void)l->memory.free(l->memory.context,record->allocation,NULL);
            if(borrowed) qa_error_set(error,QA_ERROR_FORMAT,0,"Script allocation has the wrong source extent");
            return false;
        }
        record->bytes=span.data;
    } else {
        record->bytes=clear?calloc(size,1):malloc(size);record->detached=true;
        if(!record->bytes) {qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating native script storage");return false;}
    }
    return true;
}
bool script_lexer_memory_open(qa_script_lexer *l,const char *path,qa_bytes input,qa_error *error)
{
    size_t path_size=strlen(path);
    if(input.size>INT32_MAX-SCRIPT_LEXER_BYTES-1 || path_size>=1024) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Script filename/text exceeds its release32 allocation profile");return false;
    }
    if(!allocate(l,&l->record,(uint32_t)input.size+SCRIPT_LEXER_BYTES+1,true,error)) return false;
    uint8_t *bytes=l->record.bytes;
    memset(bytes,0,SCRIPT_LEXER_BYTES);memcpy(bytes,path,path_size);
    bytes[SCRIPT_LEXER_BYTES+input.size]=0;
    qa_store_u32le(bytes+SCRIPT_LEXER_BUFFER,SCRIPT_LEXER_BYTES);
    qa_store_u32le(bytes+SCRIPT_LEXER_POINTER,SCRIPT_LEXER_BYTES);
    qa_store_u32le(bytes+SCRIPT_LEXER_LAST_POINTER,SCRIPT_LEXER_BYTES);
    qa_store_u32le(bytes+SCRIPT_LEXER_END,(uint32_t)input.size+SCRIPT_LEXER_BYTES);
    qa_store_u32le(bytes+SCRIPT_LEXER_LENGTH,(uint32_t)input.size);
    qa_store_u32le(bytes+SCRIPT_LEXER_LINE,1);qa_store_u32le(bytes+SCRIPT_LEXER_LAST_LINE,1);
    l->input=(qa_bytes){bytes+SCRIPT_LEXER_BYTES,input.size};return true;
}
bool script_lexer_punctuation_open(qa_script_lexer *l,qa_error *error)
{
    if(!allocate(l,&l->table,SCRIPT_PUNCTUATION_BYTES,false,error)) return false;
    memset(l->table.bytes,0,SCRIPT_PUNCTUATION_BYTES);
    for(size_t i=0;i<256;++i) qa_store_u32le(l->table.bytes+i*4,l->heads[i]<0?0:(uint32_t)l->heads[i]+1);
    qa_store_u32le(l->record.bytes+SCRIPT_LEXER_PUNCTUATIONS,1);
    qa_store_u32le(l->record.bytes+SCRIPT_LEXER_TABLE,1);return true;
}
void script_lexer_copy_text(qa_script_lexer *l,qa_bytes input)
{
    if(input.size) memcpy(l->record.bytes+SCRIPT_LEXER_BYTES,input.data,input.size);
    qa_store_u32le(l->record.bytes+SCRIPT_LEXER_FLAGS,l->options.flags);
}
void script_lexer_compress(qa_script_lexer *l)
{
    uint8_t *bytes=l->record.bytes+SCRIPT_LEXER_BYTES;size_t input=0,output=0;
    bool newline=false,whitespace=false;uint8_t byte;
    while((byte=bytes[input])!=0) {
        if(byte=='/' && bytes[input+1]=='/') {
            while(bytes[input] && bytes[input]!='\n') ++input;
        } else if(byte=='/' && bytes[input+1]=='*') {
            while(bytes[input] && (bytes[input]!='*' || bytes[input+1]!='/')) ++input;
            if(bytes[input]) input+=2;
        } else if(byte=='\n' || byte=='\r') {newline=true;++input;}
        else if(byte==' ' || byte=='\t') {whitespace=true;++input;}
        else {
            if(newline) {bytes[output++]='\n';newline=false;whitespace=false;}
            if(whitespace) {bytes[output++]=' ';whitespace=false;}
            bytes[output++]=bytes[input++];
            if(byte=='"') {
                while((byte=bytes[input])!=0 && byte!='"') bytes[output++]=bytes[input++];
                if(byte=='"') bytes[output++]=bytes[input++];
            }
        }
    }
    bytes[output]=0;qa_store_u32le(l->record.bytes+SCRIPT_LEXER_LENGTH,(uint32_t)output);
}
static bool extent(qa_script_lexer *l,script_lexer_allocation *record,qa_error *error)
{
    if(!record->bytes) {qa_error_set(error,QA_ERROR_FORMAT,0,"Script pointer does not name live retained storage");return false;}
    if(l->memory.context && !record->detached) {
        qa_script_memory_span span;
        if(!l->memory.bytes(l->memory.context,record->allocation,&span,error)) return false;
        if(span.size!=record->size) {qa_error_set(error,QA_ERROR_FORMAT,0,"Script extent differs from its actual allocation");return false;}
        record->bytes=span.data;
    }
    return true;
}
bool script_lexer_memory_validate(qa_script_lexer *l,qa_error *error)
{
    if(!extent(l,&l->record,error) || !extent(l,&l->table,error)) return false;
    if(l->record.size<SCRIPT_LEXER_BYTES+1 || l->table.size!=SCRIPT_PUNCTUATION_BYTES) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Script allocations have invalid release32 extents");return false;
    }
    const uint8_t *bytes=l->record.bytes;uint32_t pointer=qa_load_u32le(bytes+SCRIPT_LEXER_POINTER);
    if(
       !memchr(bytes,0,1024) || qa_load_u32le(bytes+SCRIPT_LEXER_BUFFER)!=SCRIPT_LEXER_BYTES ||
       qa_load_u32le(bytes+SCRIPT_LEXER_END)!=l->record.size-1 || pointer<SCRIPT_LEXER_BYTES ||
       pointer>l->record.size-1 || qa_load_u32le(bytes+SCRIPT_LEXER_LENGTH)>l->record.size-SCRIPT_LEXER_BYTES-1 ||
       !qa_load_u32le(bytes+SCRIPT_LEXER_LINE) || qa_load_u32le(bytes+SCRIPT_LEXER_TABLE)!=1 ||
       qa_load_u32le(bytes+SCRIPT_LEXER_PUNCTUATIONS)!=1 || bytes[l->record.size-1]) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Script header pointers are outside their actual allocation");return false;
    }
    l->input=(qa_bytes){l->record.bytes+SCRIPT_LEXER_BYTES,l->record.size-SCRIPT_LEXER_BYTES-1};return true;
}
static void release(qa_script_lexer *l,script_lexer_allocation *record,bool source)
{
    if(!record->bytes) return;
    if(record->detached || !l->memory.context) free(record->bytes);
    else if(source) (void)l->memory.free(l->memory.context,record->allocation,NULL);
    record->bytes=NULL;
}
void script_lexer_memory_close(qa_script_lexer *l,bool source)
{
    release(l,&l->table,source);release(l,&l->record,source);
    if(l->memory.context) l->memory.release(l->memory.context);
}
static bool retire(qa_script_lexer *l,script_lexer_allocation *record,qa_error *error)
{
    if(record->retired) return true;
    if(record->detached || !l->memory.context) {record->retired=true;return true;}
    if(!extent(l,record,error)) return false;
    uint8_t *copy=malloc(record->size);
    if(!copy) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining retired script metadata");return false;}
    memcpy(copy,record->bytes,record->size);
    if(!l->memory.free(l->memory.context,record->allocation,error)) {free(copy);return false;}
    record->bytes=copy;record->detached=true;record->retired=true;record->reference=SIZE_MAX;return true;
}
bool script_lexer_retire(qa_script_lexer *l,qa_error *error)
{
    if(l->released) return true;
    if(!retire(l,&l->table,error) || !retire(l,&l->record,error)) return false;
    l->input=(qa_bytes){l->record.bytes+SCRIPT_LEXER_BYTES,l->record.size-SCRIPT_LEXER_BYTES-1};
    l->released=true;return true;
}
static bool capture(qa_script_lexer *l,script_lexer_allocation *record,qa_arena *arena,qa_bytes *bytes,size_t *reference,qa_error *error)
{
    if(!extent(l,record,error)) return false;
    uint8_t *copy=qa_arena_alloc(arena,record->size,1,error);if(!copy) return false;
    memcpy(copy,record->bytes,record->size);*bytes=(qa_bytes){copy,record->size};*reference=record->reference;
    return !l->memory.context || record->detached || l->memory.reference(l->memory.context,record->allocation,reference,error);
}
bool script_lexer_frame_capture(const qa_script_lexer *lexer,qa_script_frame_state *out,qa_arena *arena,qa_error *error)
{
    qa_script_lexer *l=(qa_script_lexer *)lexer;
    out->script_released=l->record.retired;out->punctuation_released=l->table.retired;
    return capture(l,&l->record,arena,&out->script_record,&out->script_reference,error) &&
        capture(l,&l->table,arena,&out->punctuation_record,&out->punctuation_reference,error);
}
bool script_lexer_frame_valid(const qa_script_frame_state *frame,qa_error *error)
{
    const qa_bytes record=frame->script_record,table=frame->punctuation_record;
    if(!record.data || !table.data || frame->source.size>INT32_MAX-SCRIPT_LEXER_BYTES-1 ||
       record.size!=SCRIPT_LEXER_BYTES+frame->source.size+1 || table.size!=SCRIPT_PUNCTUATION_BYTES)
        goto bad;
    const uint8_t *bytes=record.data;
    if(!memchr(bytes,0,1024) || bytes[record.size-1] ||
       qa_load_u32le(bytes+SCRIPT_LEXER_BUFFER)!=SCRIPT_LEXER_BYTES ||
       qa_load_u32le(bytes+SCRIPT_LEXER_END)!=record.size-1 ||
       qa_load_u32le(bytes+SCRIPT_LEXER_POINTER)!=SCRIPT_LEXER_BYTES+frame->lexer.offset ||
       qa_load_u32le(bytes+SCRIPT_LEXER_LINE)!=frame->lexer.line ||
       (qa_load_u32le(bytes+SCRIPT_LEXER_AVAILABLE)!=0)!=frame->lexer.unread ||
       qa_load_u32le(bytes+SCRIPT_LEXER_LENGTH)>frame->source.size ||
       qa_load_u32le(bytes+SCRIPT_LEXER_PUNCTUATIONS)!=1 ||
       qa_load_u32le(bytes+SCRIPT_LEXER_TABLE)!=1 ||
       (frame->active && (frame->script_released || frame->punctuation_released)) ||
       (frame->script_released && (!frame->punctuation_released || frame->script_reference!=SIZE_MAX)) ||
       (frame->punctuation_released && frame->punctuation_reference!=SIZE_MAX))
        goto bad;
    const unsigned pointers[]={SCRIPT_LEXER_LAST_POINTER,SCRIPT_LEXER_WHITESPACE,SCRIPT_LEXER_END_WHITESPACE};
    for(size_t i=0;i<sizeof(pointers)/sizeof(*pointers);++i) {
        uint32_t pointer=qa_load_u32le(bytes+pointers[i]);
        if(pointer && (pointer<SCRIPT_LEXER_BYTES || pointer>=record.size)) goto bad;
    }
    for(size_t i=0;i<256;++i) if(qa_load_u32le(table.data+i*4)>QA_SCRIPT_DOLLAR) goto bad;
    const qa_script_token *token=&frame->lexer.token;const uint8_t *raw=bytes+SCRIPT_LEXER_TOKEN;
    uint8_t expected[SCRIPT_TOKEN_BYTES];
    if(!script_token_store(expected,token,qa_load_u32le(raw+1048),qa_load_u32le(raw+1052),error)) return false;
    if(memcmp(raw,expected,token->text.size) ||
       ((!token->text.size || !memchr(token->text.data,0,token->text.size)) && raw[token->text.size]) ||
       memcmp(raw+1024,expected+1024,22) ||
       memcmp(raw+1056,expected+1056,8)) goto bad;
    return true;
bad:
    qa_error_set(error,QA_ERROR_FORMAT,0,"Invalid script_t allocation or retained lexer projection");return false;
}
static bool restore(qa_script_lexer *l,script_lexer_allocation *record,qa_bytes bytes,size_t reference,bool detached,qa_error *error)
{
    if(bytes.size>UINT32_MAX || !bytes.data) {qa_error_set(error,QA_ERROR_FORMAT,0,"Saved script allocation has an invalid extent");return false;}
    record->size=(uint32_t)bytes.size;record->reference=reference;
    if(l->memory.context && !detached) {
        qa_script_memory_span span;
        if(reference==SIZE_MAX) {qa_error_set(error,QA_ERROR_FORMAT,0,"Saved live script has no MEMORY allocation");return false;}
        if(!l->memory.resolve(l->memory.context,reference,&record->allocation,error) ||
           !l->memory.bytes(l->memory.context,record->allocation,&span,error)) return false;
        if(span.size!=bytes.size || memcmp(span.data,bytes.data,bytes.size)) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"Saved script differs from restored MEMORY");return false;
        }
        record->bytes=span.data;
    } else {
        record->bytes=malloc(bytes.size);record->detached=true;
        if(!record->bytes) {qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring native script metadata");return false;}
        memcpy(record->bytes,bytes.data,bytes.size);
    }
    return true;
}
bool script_lexer_memory_restore(qa_script_lexer *l,const qa_script_frame_state *saved,bool history,qa_error *error)
{
    l->record.retired=saved->script_released;l->table.retired=saved->punctuation_released;
    l->released=saved->script_released && saved->punctuation_released;
    if(!l->memory.context &&
       ((!saved->script_released && saved->script_reference!=SIZE_MAX) ||
        (!saved->punctuation_released && saved->punctuation_reference!=SIZE_MAX))) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Restored lexer requires its actual script MEMORY owner");return false;
    }
    return restore(l,&l->record,saved->script_record,saved->script_reference,history || saved->script_released,error) &&
        restore(l,&l->table,saved->punctuation_record,saved->punctuation_reference,history || saved->punctuation_released,error) &&
        script_lexer_memory_validate(l,error);
}
static bool adopt(qa_script_lexer *l,script_lexer_allocation *record,qa_error *error)
{
    if(!record->detached || !l->memory.context || record->retired) return true;
    qa_script_memory_span span;
    if(!l->memory.resolve_history(l->memory.context,record->reference,&record->allocation,error) ||
       !l->memory.bytes(l->memory.context,record->allocation,&span,error)) return false;
    if(span.size!=record->size || memcmp(span.data,record->bytes,span.size)) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"History script differs from committed MEMORY");return false;
    }
    free(record->bytes);record->bytes=span.data;record->detached=false;return true;
}
bool script_lexer_adopt(qa_script_lexer *l,qa_error *error)
{
    return adopt(l,&l->record,error) && adopt(l,&l->table,error) && script_lexer_memory_validate(l,error);
}
