#include "internal.h"

static bool allocation_bytes(script_macro_table *table,script_lexer_allocation *record,qa_error *error)
{
    if(!record->bytes) {qa_error_set(error,QA_ERROR_FORMAT,0,"Missing actual precompiler allocation");return false;}
    if(table->memory.context && !record->detached) {
        qa_script_memory_span span;
        if(!table->memory.bytes(table->memory.context,record->allocation,&span,error)) return false;
        if(span.size!=record->size) {qa_error_set(error,QA_ERROR_FORMAT,0,"Precompiler allocation differs from its actual extent");return false;}
        record->bytes=span.data;
    }
    return true;
}
bool script_table_hash_bind(const script_macro_table *owner,qa_error *error)
{
    script_macro_table *table=(script_macro_table *)owner;
    if(table->source && (!table->source->source_record.bytes ||
       qa_load_u32le(table->source->source_record.bytes+SCRIPT_SOURCE_HASH)!=1)) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Source define hash pointer does not identify its actual allocation");return false;
    }
    return allocation_bytes(table,&table->hash,error);
}
static bool allocate(script_macro_table *table,script_lexer_allocation *record,uint32_t size,bool clear,qa_error *error)
{
    *record=(script_lexer_allocation){.reference=SIZE_MAX,.size=size};
    if(table->memory.context) {
        if(!table->memory.allocate(table->memory.context,size,clear,&record->allocation,error)) return false;
        qa_script_memory_span span={0};bool borrowed=table->memory.bytes(table->memory.context,record->allocation,&span,error);
        if(!borrowed || span.size!=size) {
            (void)table->memory.free(table->memory.context,record->allocation,NULL);
            if(borrowed) qa_error_set(error,QA_ERROR_FORMAT,0,"Allocated precompiler record has an invalid extent");
            return false;
        }
        record->bytes=span.data;
    } else {
        record->bytes=malloc(size);
        if(!record->bytes) {qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating raw precompiler record");return false;}
    }
    if(clear) memset(record->bytes,0,size);
    return true;
}
static bool release(script_macro_table *table,script_lexer_allocation *record,qa_error *error)
{
    if(!allocation_bytes(table,record,error)) return false;
    if(table->memory.context && !record->detached) {
        if(!table->memory.free(table->memory.context,record->allocation,error)) return false;
    } else free(record->bytes);
    record->bytes=NULL;return true;
}
bool script_macro_bind(script_macro *macro,qa_error *error)
{
    return macro && allocation_bytes(macro->owner,&macro->record,error);
}
uint32_t script_macro_word(const script_macro *macro,size_t offset)
{
    return qa_load_u32le(macro->record.bytes+offset);
}
void script_macro_word_set(script_macro *macro,size_t offset,uint32_t value)
{
    qa_store_u32le(macro->record.bytes+offset,value);
}
qa_bytes script_macro_name(const script_macro *macro)
{
    if(!macro || !macro->record.bytes) return (qa_bytes){0};
    uint32_t offset=script_macro_word(macro,0);
    if(offset>=macro->record.size) return (qa_bytes){0};
    const uint8_t *text=macro->record.bytes+offset;
    const uint8_t *zero=memchr(text,0,macro->record.size-offset);
    return zero?(qa_bytes){text,(size_t)(zero-text)}:(qa_bytes){0};
}
script_macro *script_macro_resolve(const script_macro_table *table,uint32_t pointer)
{
    for(script_macro *macro=table->records;macro;macro=macro->registry_next)
        if(macro->record.bytes && macro->pointer==pointer) return macro;
    return NULL;
}
bool script_macro_allocate(script_macro_table *table,qa_bytes name,bool clear,script_macro **out,qa_error *error)
{
    if(!table->next_define_pointer || table->next_define_pointer==UINT32_MAX || name.size>UINT32_MAX-33) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Define pointer identity or extent exhausted");return false;
    }
    script_macro *macro=qa_arena_alloc(&table->arena,sizeof(*macro),_Alignof(script_macro),error);
    if(!macro) return false;
    *macro=(script_macro){.owner=table};
    if(!allocate(table,&macro->record,(uint32_t)name.size+33,false,error)) return false;
    if(clear) memset(macro->record.bytes,0,32);
    qa_store_u32le(macro->record.bytes,32);memcpy(macro->record.bytes+32,name.data,name.size);macro->record.bytes[32+name.size]=0;
    macro->pointer=table->next_define_pointer++;macro->registry_next=table->records;table->records=macro;
    *out=macro;return true;
}
bool script_macro_publish(script_macro_table *table,script_macro *macro,qa_error *error)
{
    if(table->global) {
        script_macro_word_set(macro,24,table->first);table->first=macro->pointer;
    } else {
        if(!script_table_hash_bind(table,error)) return false;
        qa_bytes name=script_macro_name(macro);uint32_t bucket=script_macro_hash(name);
        script_macro_word_set(macro,28,qa_load_u32le(table->hash.bytes+bucket*4));
        qa_store_u32le(table->hash.bytes+bucket*4,macro->pointer);
    }
    macro->published=true;++table->count;return true;
}
bool script_macro_add_token(script_macro *macro,size_t offset,script_queued_token token,uint32_t *last,qa_error *error)
{
    script_macro_table *table=macro->owner;script_token_record *node;token.expansion=NULL;
    if(!script_heap_copy_token(table,token,&node,error)) return false;
    qa_store_u32le(node->record.bytes+1048,0);qa_store_u32le(node->record.bytes+1052,0);qa_store_u32le(node->record.bytes+1060,0);
    node->whitespace=(qa_bytes){0};
    if(*last) {
        script_token_record *previous=script_heap_token(table,*last);
        if(!script_heap_token_bytes(table,previous,error)) return false;
        qa_store_u32le(previous->record.bytes+1064,node->pointer);
    } else script_macro_word_set(macro,offset,node->pointer);
    *last=node->pointer;return true;
}
bool script_heap_free_chain(script_macro_table *table,uint32_t head,qa_error *error)
{
    size_t remaining=table->queue_count;
    while(head) {
        if(!remaining--) {qa_error_set(error,QA_ERROR_FORMAT,0,"Define token chain contains a cycle");return false;}
        script_token_record *node=script_heap_token(table,head);
        if(!script_heap_token_bytes(table,node,error)) return false;
        head=qa_load_u32le(node->record.bytes+1064);
        if(!script_heap_free_token(table,node,error)) return false;
    }
    return true;
}
bool script_macro_free(script_macro *macro,qa_error *error)
{
    return script_macro_bind(macro,error) && script_heap_free_chain(macro->owner,script_macro_word(macro,16),error) &&
        script_heap_free_chain(macro->owner,script_macro_word(macro,20),error) && release(macro->owner,&macro->record,error);
}
bool script_table_open(script_macro_table *table,const qa_script_memory *memory,bool global,qa_error *error)
{
    table->global=global;table->next_token_pointer=1;table->next_define_pointer=1;
    if(memory) table->memory=*memory;
    return global || allocate(table,&table->hash,4096,true,error);
}
bool script_table_clear(script_macro_table *table,qa_error *error)
{
    if(table->global) {
        size_t remaining=table->count;
        while(table->first) {
            script_macro *macro=script_macro_resolve(table,table->first);
            if(!macro || !remaining-- || !script_macro_bind(macro,error)) {qa_error_set(error,QA_ERROR_FORMAT,0,"Global define chain has no live record");return false;}
            table->first=script_macro_word(macro,24);
            if(!script_macro_free(macro,error)) return false;
            --table->count;
        }
    } else if(table->hash.bytes) {
        if(!script_table_hash_bind(table,error)) return false;
        for(size_t bucket=0;bucket<1024;++bucket) {
            uint32_t pointer=qa_load_u32le(table->hash.bytes+bucket*4);size_t remaining=table->count;
            while(pointer) {
                script_macro *macro=script_macro_resolve(table,pointer);
                if(!macro || !remaining-- || !script_macro_bind(macro,error)) {qa_error_set(error,QA_ERROR_FORMAT,0,"Define hash chain has no live record");return false;}
                pointer=script_macro_word(macro,28);qa_store_u32le(table->hash.bytes+bucket*4,pointer);
                if(!script_macro_free(macro,error)) return false;
                --table->count;
            }
        }
    }
    return true;
}
void script_table_dispose(script_macro_table *table,bool source)
{
    if(source && !table->deferred) (void)script_table_clear(table,NULL);
    if(table->hash.bytes) {
        if(source && !table->deferred && (!table->memory.context || !table->source ||
           (table->source->source_record.bytes && qa_load_u32le(table->source->source_record.bytes+SCRIPT_SOURCE_HASH)==1)))
            (void)release(table,&table->hash,NULL);
        else if(table->hash.detached || !table->memory.context) free(table->hash.bytes);
    }
    for(script_macro *macro=table->records;macro;macro=macro->registry_next)
        if(macro->record.bytes && (macro->record.detached || !table->memory.context)) free(macro->record.bytes);
    for(size_t i=0;i<table->queue_records;++i)
        if(table->queue[i].record.bytes && (table->queue[i].record.detached || !table->memory.context)) free(table->queue[i].record.bytes);
    free(table->queue);qa_arena_destroy(&table->arena);
    if(table->retained) table->memory.release(table->memory.context);
    *table=(script_macro_table){0};
}
static bool token_chain(script_macro_table *table,uint32_t head,qa_script_token **out,size_t *count,qa_arena *arena,qa_error *error)
{
    size_t length=0,capacity=0;qa_script_token *tokens=NULL;uint32_t pointer=head;
    while(pointer) {
        script_token_record *node=script_heap_token(table,pointer);
        if(length>=table->queue_count || !script_heap_token_bytes(table,node,error) ||
           !script_grow((void **)&tokens,&capacity,length+1,sizeof(*tokens),error)) {free(tokens);return false;}
        if(!script_token_load(node->record.bytes,node->extent,node->location,node->whitespace,arena,tokens+length,error)) {free(tokens);return false;}
        ++length;pointer=qa_load_u32le(node->record.bytes+1064);
    }
    qa_script_token *copy=length?qa_arena_alloc(arena,length*sizeof(*copy),_Alignof(qa_script_token),error):NULL;
    if(length && !copy) {free(tokens);return false;}
    if(length) memcpy(copy,tokens,length*sizeof(*copy));
    free(tokens);*out=copy;*count=length;return true;
}
bool script_macro_project(const script_macro *macro,qa_script_macro_state *out,qa_arena *arena,qa_error *error)
{
    script_macro_table *table=macro->owner;
    if(!allocation_bytes(table,(script_lexer_allocation *)&macro->record,error)) return false;
    qa_bytes name=script_macro_name(macro);if(!name.size) return false;
    *out=(qa_script_macro_state){.pointer=macro->pointer,.memory_reference=macro->record.reference,
        .parameter_count=script_macro_word(macro,12),.builtin=script_macro_word(macro,8),
        .fixed=(script_macro_word(macro,4)&1)!=0,.function=script_macro_word(macro,12)!=0,.active=macro->published};
    char *name_copy=script_string(arena,name.data,name.size,error);if(!name_copy) return false;
    out->name=(qa_bytes){(uint8_t *)name_copy,name.size};
    qa_script_token *parameters=NULL,*tokens=NULL;size_t count=0;
    if(!token_chain(table,script_macro_word(macro,16),&parameters,&count,arena,error) || count!=out->parameter_count ||
       !token_chain(table,script_macro_word(macro,20),&tokens,&out->token_count,arena,error)) return false;
    qa_bytes *names=count?qa_arena_alloc(arena,count*sizeof(*names),_Alignof(qa_bytes),error):NULL;
    if(count && !names) return false;
    for(size_t i=0;i<count;++i) names[i]=parameters[i].text;
    out->parameters=names;out->tokens=tokens;
    uint8_t *bytes=qa_arena_alloc(arena,macro->record.size,1,error);if(!bytes) return false;
    memcpy(bytes,macro->record.bytes,macro->record.size);out->record=(qa_bytes){bytes,macro->record.size};
    if(table->memory.context && !macro->record.detached && !table->memory.reference(table->memory.context,
        macro->record.allocation,&out->memory_reference,error)) return false;
    return true;
}

static bool restore_record(script_macro_table *table,script_lexer_allocation *record,qa_bytes bytes,size_t reference,qa_error *error)
{
    *record=(script_lexer_allocation){.reference=reference,.size=(uint32_t)bytes.size};
    if(table->memory.context && !table->deferred) {
        qa_script_memory_span span;
        if(reference==SIZE_MAX || !table->memory.resolve(table->memory.context,reference,&record->allocation,error) ||
           !table->memory.bytes(table->memory.context,record->allocation,&span,error)) return false;
        if(span.size!=bytes.size || memcmp(span.data,bytes.data,bytes.size)) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"Saved precompiler record differs from restored MEMORY");return false;
        }
        record->bytes=span.data;
    } else {
        record->bytes=malloc(bytes.size);
        if(!record->bytes) {qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring detached precompiler record");return false;}
        memcpy(record->bytes,bytes.data,bytes.size);record->detached=true;
    }
    return true;
}
bool script_table_restore(script_macro_table *table,const qa_script_checkpoint *saved,qa_error *error)
{
    table->next_define_pointer=saved->next_define_pointer;table->first=saved->define_first;
    if(saved->define_hash.size && !restore_record(table,&table->hash,saved->define_hash,saved->hash_reference,error)) return false;
    for(size_t i=0;i<saved->macro_count;++i) {
        const qa_script_macro_state *row=saved->macros+i;
        script_macro *macro=qa_arena_alloc(&table->arena,sizeof(*macro),_Alignof(script_macro),error);
        if(!macro) return false;
        *macro=(script_macro){.owner=table,.pointer=row->pointer,.published=row->active};
        if(!restore_record(table,&macro->record,row->record,row->memory_reference,error)) return false;
        macro->registry_next=table->records;table->records=macro;
        if(row->active) ++table->count;
    }
    return true;
}
bool script_table_capture(script_macro_table *table,qa_script_checkpoint *saved,qa_arena *arena,qa_error *error)
{
    saved->next_token_pointer=table->next_token_pointer;saved->next_define_pointer=table->next_define_pointer;
    saved->define_first=table->first;saved->queue_count=table->queue_count;
    saved->hash_reference=table->hash.reference;
    if(table->hash.bytes) {
        if(!script_table_hash_bind(table,error)) return false;
        uint8_t *copy=qa_arena_alloc(arena,4096,1,error);if(!copy) return false;
        memcpy(copy,table->hash.bytes,4096);saved->define_hash=(qa_bytes){copy,4096};
        if(table->memory.context && !table->hash.detached && !table->memory.reference(table->memory.context,
            table->hash.allocation,&saved->hash_reference,error)) return false;
    }
    for(script_macro *macro=table->records;macro;macro=macro->registry_next) if(macro->record.bytes) ++saved->macro_count;
    qa_script_macro_state *macros=saved->macro_count?qa_arena_alloc(arena,saved->macro_count*sizeof(*macros),_Alignof(qa_script_macro_state),error):NULL;
    qa_script_queued_state *tokens=saved->queue_count?qa_arena_alloc(arena,saved->queue_count*sizeof(*tokens),_Alignof(qa_script_queued_state),error):NULL;
    if((saved->macro_count && !macros) || (saved->queue_count && !tokens)) return false;
    if(saved->queue_count) memset(tokens,0,saved->queue_count*sizeof(*tokens));
    saved->macros=macros;saved->queue=tokens;
    if(!script_queue_snapshot(table,tokens,arena,error)) return false;
    for(size_t i=0;i<saved->queue_count;++i) tokens[i].expansion=SIZE_MAX;
    size_t index=0;
    for(script_macro *macro=table->records;macro;macro=macro->registry_next) if(macro->record.bytes) {
        if(!script_macro_project(macro,macros+index,arena,error)) return false;
        ++index;
    }
    return true;
}
static bool adopt_record(script_macro_table *table,script_lexer_allocation *record,bool history,qa_error *error)
{
    if(!record->bytes || !record->detached || !table->memory.context) return true;
    qa_script_memory_span span;
    bool resolved=record->reference!=SIZE_MAX && (history?
        table->memory.resolve_history(table->memory.context,record->reference,&record->allocation,error):
        table->memory.resolve(table->memory.context,record->reference,&record->allocation,error));
    if(!resolved || !table->memory.bytes(table->memory.context,record->allocation,&span,error)) return false;
    if(span.size!=record->size || memcmp(span.data,record->bytes,record->size)) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Detached precompiler record differs from committed MEMORY");return false;
    }
    free(record->bytes);record->bytes=span.data;record->detached=false;return true;
}
bool script_table_adopt(script_macro_table *table,bool history,qa_error *error)
{
    if(!table->deferred) return true;
    if(!adopt_record(table,&table->hash,history,error)) return false;
    for(script_macro *macro=table->records;macro;macro=macro->registry_next)
        if(!adopt_record(table,&macro->record,history,error)) return false;
    if(!script_queue_adopt(table,history,error)) return false;
    table->deferred=false;return true;
}
static bool upload_record(script_macro_table *table,script_lexer_allocation *record,qa_error *error)
{
    if(!record->bytes) return true;
    script_lexer_allocation uploaded;
    if(!allocate(table,&uploaded,record->size,false,error)) return false;
    memcpy(uploaded.bytes,record->bytes,record->size);free(record->bytes);*record=uploaded;return true;
}
bool qa_script_defines_bind_memory(qa_script_defines *owner,const qa_script_memory *memory,qa_error *error)
{
    if(!owner || !memory || !memory->context || !memory->retain || !memory->release || !memory->allocate ||
       !memory->bytes || !memory->free || !memory->reference || !memory->resolve || !memory->resolve_history) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Global define MEMORY requires its complete retained owner");return false;
    }
    script_macro_table *table=&owner->table;
    if(table->memory.context && table->memory.context!=memory->context) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Global defines already have a different actual MEMORY owner");return false;
    }
    if(!table->memory.context) {
        if(!memory->retain(memory->context,error)) return false;
        table->memory=*memory;table->retained=true;
        for(script_macro *macro=table->records;macro;macro=macro->registry_next)
            if(macro->record.bytes) macro->record.detached=true;
        for(size_t i=0;i<table->queue_records;++i)
            if(table->queue[i].record.bytes) table->queue[i].record.detached=true;
    }
    table->deferred=true;
    /* Managed owned cells copy into this explicit owner; shared saved references
     * remain detached until the real MEMORY restore has committed. */
    for(script_macro *macro=table->records;macro;macro=macro->registry_next)
        if(macro->record.bytes && macro->record.detached && macro->record.reference==SIZE_MAX &&
           !upload_record(table,&macro->record,error)) return false;
    for(size_t i=0;i<table->queue_records;++i) {
        script_lexer_allocation *record=&table->queue[i].record;
        if(record->bytes && record->detached && record->reference==SIZE_MAX && !upload_record(table,record,error)) return false;
    }
    table->deferred=false;
    for(script_macro *macro=table->records;macro;macro=macro->registry_next)
        if(macro->record.bytes && macro->record.detached) table->deferred=true;
    for(size_t i=0;i<table->queue_records;++i)
        if(table->queue[i].record.bytes && table->queue[i].record.detached) table->deferred=true;
    return true;

}
static bool token_equal(const qa_script_token *left,const qa_script_token *right)
{
    return left->kind==right->kind && left->subtype==right->subtype && left->integer==right->integer &&
        left->lines_crossed==right->lines_crossed && left->location.line==right->location.line &&
        script_bytes_equal(left->text,right->text) && !memcmp(&left->number,&right->number,sizeof(left->number));
}
bool script_table_saved_valid(const qa_script_checkpoint *saved,bool global,qa_error *error)
{
    if(!saved->next_define_pointer || !saved->next_token_pointer ||
       (saved->macro_count && !saved->macros) || (saved->queue_count && !saved->queue)) goto bad;
    script_macro_table table={.next_define_pointer=saved->next_define_pointer,.next_token_pointer=saved->next_token_pointer,
        .first=saved->define_first,.global=global,.queue_count=saved->queue_count,.queue_records=saved->queue_count,
        .hash={.bytes=(uint8_t *)saved->define_hash.data,.size=(uint32_t)saved->define_hash.size}};
    script_macro *records=saved->macro_count?calloc(saved->macro_count,sizeof(*records)):NULL;
    script_token_record *tokens=saved->queue_count?calloc(saved->queue_count,sizeof(*tokens)):NULL;
    if((saved->macro_count && !records) || (saved->queue_count && !tokens)) {
        free(records);free(tokens);qa_error_set(error,QA_ERROR_MEMORY,0,"Validating actual precompiler ownership");return false;
    }
    bool ok=true;table.queue=tokens;
    for(size_t i=0;ok && i<saved->queue_count;++i) {
        const qa_script_queued_state *row=saved->queue+i;
        if(!row->pointer || row->pointer>=saved->next_token_pointer || !script_token_saved_valid(row,error)) {ok=false;break;}
        tokens[i]=(script_token_record){.pointer=row->pointer,.record={.bytes=(uint8_t *)row->bytes,.size=SCRIPT_TOKEN_BYTES},
            .location=row->token.location,.whitespace=row->token.leading_whitespace,.extent=row->text_extent};
        for(size_t j=0;j<i;++j) if(row->pointer==saved->queue[j].pointer ||
            (row->memory_reference!=SIZE_MAX && row->memory_reference==saved->queue[j].memory_reference)) ok=false;
    }
    for(size_t i=0;ok && i<saved->macro_count;++i) {
        const qa_script_macro_state *row=saved->macros+i;
        if(!row->pointer || row->pointer>=saved->next_define_pointer || row->record.size<34 || row->record.size>1056 ||
           !row->record.data || qa_load_u32le(row->record.data)!=32 ||
           !memchr(row->record.data+32,0,row->record.size-32) || qa_load_u32le(row->record.data+8)>4 ||
           qa_load_u32le(row->record.data+12)>128) {ok=false;break;}
        records[i]=(script_macro){.owner=&table,.pointer=row->pointer,.published=row->active,
            .record={.bytes=(uint8_t *)row->record.data,.size=(uint32_t)row->record.size},.registry_next=table.records};
        table.records=records+i;
        for(size_t j=0;j<i;++j) if(row->pointer==saved->macros[j].pointer ||
            (row->memory_reference!=SIZE_MAX && row->memory_reference==saved->macros[j].memory_reference)) ok=false;
    }
    for(size_t i=0;ok && i<saved->macro_count;++i) {
        qa_arena arena={0};qa_script_macro_state projected={0};const qa_script_macro_state *row=saved->macros+i;
        ok=script_macro_project(records+i,&projected,&arena,error);
        if(ok && row->name.data) {
            ok=(!row->parameter_count || row->parameters) && (!row->token_count || row->tokens) &&
                script_bytes_equal(projected.name,row->name) && projected.parameter_count==row->parameter_count &&
                projected.token_count==row->token_count && projected.builtin==row->builtin && projected.fixed==row->fixed;
            for(size_t j=0;ok && j<projected.parameter_count;++j) ok=script_bytes_equal(projected.parameters[j],row->parameters[j]);
            for(size_t j=0;ok && j<projected.token_count;++j) ok=token_equal(projected.tokens+j,row->tokens+j);
        }
        qa_arena_destroy(&arena);
    }
    if(ok && !global) {
        if(saved->define_hash.size!=4096 || !saved->define_hash.data || saved->define_first) ok=false;
        for(size_t bucket=0;ok && bucket<1024;++bucket) {
            uint32_t pointer=qa_load_u32le(saved->define_hash.data+bucket*4);size_t remaining=saved->macro_count;
            while(pointer) {
                script_macro *macro=script_macro_resolve(&table,pointer);
                if(!remaining-- || !macro) {ok=false;break;}
                pointer=script_macro_word(macro,28);
            }
        }
    }
    free(records);free(tokens);if(ok) return true;
bad:
    qa_error_set(error,QA_ERROR_FORMAT,0,"Invalid actual precompiler define/hash/token ownership");return false;
}
