#include "internal.h"
#include "qa/script_defines_save.h"
#include "qa/source_save.h"

static bool fail(qa_source_save_io *io,const char *message)
{
    qa_error_set(io->error,QA_ERROR_FORMAT,io->offset,"%s",message);io->failed=true;return false;
}
static bool span(qa_source_save_io *io,qa_arena *arena,qa_bytes *value)
{
    size_t size=value->size;
    if(!qa_source_save_count(io,&size,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        qa_bytes input;
        if(!qa_source_save_span(io,size,&input)) return false;
        char *bytes=script_string(arena,input.data,size,io->error);
        if(!bytes) {io->failed=true;return false;}
        *value=(qa_bytes){(uint8_t *)bytes,size};return true;
    }
    return qa_source_save_bytes(io,(void *)value->data,size);
}
static bool location(qa_source_save_io *io,qa_arena *arena,qa_script_location *value)
{
    const char *path=value->path?value->path:"";
    qa_bytes bytes=io->direction==QA_SOURCE_SAVE_READ?(qa_bytes){0}:script_bytes(path);
    if(!span(io,arena,&bytes) || (bytes.size && memchr(bytes.data,0,bytes.size))) return fail(io,"Global token path has no actual C string");
    if(io->direction==QA_SOURCE_SAVE_READ) value->path=(char *)bytes.data;
    return qa_source_save_u32(io,&value->line) && qa_source_save_u32(io,&value->column) && qa_source_save_count(io,&value->offset,SIZE_MAX);
}
static bool profile(qa_source_save_io *io,qa_arena *arena,const char **value)
{
    bool present=*value!=NULL;
    if(!qa_source_save_bool(io,&present)) return false;
    if(!present) {*value=NULL;return true;}
    qa_bytes text=io->direction==QA_SOURCE_SAVE_READ?(qa_bytes){0}:script_bytes(*value);
    if(!span(io,arena,&text) || (text.size && memchr(text.data,0,text.size))) return fail(io,"Invalid source token profile reason");
    if(io->direction==QA_SOURCE_SAVE_READ) *value=(const char *)text.data;
    return true;
}
static bool signature(qa_source_save_io *io)
{
    static const uint8_t expected[8]={'Q','A','S','D','E','F','S',0};uint8_t bytes[8];memcpy(bytes,expected,8);
    return qa_source_save_bytes(io,bytes,8) && !memcmp(bytes,expected,8);
}
static bool fields(qa_source_save_io *io,qa_script_checkpoint *saved,qa_arena *arena)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!qa_source_save_u32(io,&saved->next_define_pointer) || !qa_source_save_u32(io,&saved->next_token_pointer) ||
       !qa_source_save_u32(io,&saved->define_first) || !saved->next_define_pointer || !saved->next_token_pointer ||
       saved->define_first>=saved->next_define_pointer || !qa_source_save_count(io,&saved->macro_count,SIZE_MAX)) return false;
    if(reading && saved->macro_count>(io->input.size-io->offset)/54) return fail(io,"Truncated global define records");
    qa_script_macro_state *macros=(qa_script_macro_state *)saved->macros;
    if(reading && saved->macro_count) {
        macros=qa_arena_alloc(arena,saved->macro_count*sizeof(*macros),_Alignof(qa_script_macro_state),io->error);
        if(!macros) {io->failed=true;return false;}memset(macros,0,saved->macro_count*sizeof(*macros));saved->macros=macros;
    }
    for(size_t i=0;i<saved->macro_count;++i) {
        qa_script_macro_state value=reading?(qa_script_macro_state){0}:macros[i];
        if(!qa_source_save_u32(io,&value.pointer) || !qa_source_save_count(io,&value.memory_reference,SIZE_MAX) ||
           !qa_source_save_bool(io,&value.function) || !span(io,arena,&value.record) || value.record.size<34 || value.record.size>1056 ||
           qa_load_u32le(value.record.data)!=32 || !value.pointer || value.pointer>=saved->next_define_pointer ||
           !memchr(value.record.data+32,0,value.record.size-32) || qa_load_u32le(value.record.data+8)>4 ||
           qa_load_u32le(value.record.data+12)>128) return fail(io,"Invalid global define allocation");
        for(size_t j=0;j<i;++j) if(macros[j].pointer==value.pointer ||
            (value.memory_reference!=SIZE_MAX && macros[j].memory_reference==value.memory_reference)) return fail(io,"Duplicate global define owner identity");
        if(reading) macros[i]=value;
    }
    if(!qa_source_save_count(io,&saved->queue_count,SIZE_MAX)) return false;
    if(reading && saved->queue_count>(io->input.size-io->offset)/1108) return fail(io,"Truncated global token allocations");
    qa_script_queued_state *tokens=(qa_script_queued_state *)saved->queue;
    if(reading && saved->queue_count) {
        tokens=qa_arena_alloc(arena,saved->queue_count*sizeof(*tokens),_Alignof(qa_script_queued_state),io->error);
        if(!tokens) {io->failed=true;return false;}memset(tokens,0,saved->queue_count*sizeof(*tokens));saved->queue=tokens;
    }
    for(size_t i=0;i<saved->queue_count;++i) {
        qa_script_queued_state value=reading?(qa_script_queued_state){.expansion=SIZE_MAX}:tokens[i];
        if(!qa_source_save_u32(io,&value.pointer) || !qa_source_save_count(io,&value.memory_reference,SIZE_MAX) ||
           !qa_source_save_count(io,&value.text_extent,SIZE_MAX) || !qa_source_save_bytes(io,value.bytes,SCRIPT_TOKEN_BYTES) ||
           !location(io,arena,&value.token.location) || !span(io,arena,&value.token.leading_whitespace) ||
           !profile(io,arena,&value.unsupported) ||
           !value.pointer || value.pointer>=saved->next_token_pointer) return fail(io,"Invalid global token allocation");
        for(size_t j=0;j<i;++j) if(tokens[j].pointer==value.pointer ||
            (value.memory_reference!=SIZE_MAX && tokens[j].memory_reference==value.memory_reference)) return fail(io,"Duplicate global token owner identity");
        if(reading && !script_token_load(value.bytes,value.text_extent,value.token.location,value.token.leading_whitespace,arena,&value.token,io->error)) {io->failed=true;return false;}
        if(reading) tokens[i]=value;
    }
    return true;
}
bool qa_script_defines_save_capture(const qa_script_defines *owner,qa_buffer *out,qa_error *error)
{
    if(!owner || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Global continuation requires its actual owner and output");return false;}
    qa_arena arena={0};qa_script_checkpoint saved={0};qa_source_save_io io={0};
    bool ok=script_table_capture((script_macro_table *)&owner->table,&saved,&arena,error) &&
        qa_source_save_writer(&io,NULL,error) && signature(&io) && fields(&io,&saved,&arena) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);qa_arena_destroy(&arena);return ok;
}
bool qa_script_defines_save_restore(qa_bytes bytes,qa_script_defines **out,qa_error *error)
{
    if(!out || *out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Global continuation requires a fresh owner output");return false;}
    qa_script_defines *owner=NULL;qa_arena arena={0};qa_script_checkpoint saved={0};qa_source_save_io io={0};
    bool ok=qa_script_defines_create(&owner,error) && qa_source_save_reader(&io,NULL,bytes,error) &&
        signature(&io) && fields(&io,&saved,&arena) && qa_source_save_finish(&io,NULL) &&
        script_table_saved_valid(&saved,true,error);
    if(ok) {
        owner->table.deferred=true;
        ok=script_queue_restore(&owner->table,&saved,NULL,error) && script_table_restore(&owner->table,&saved,error);
        uint32_t pointer=owner->table.first;size_t remaining=owner->table.next_define_pointer;
        while(ok && pointer && remaining--) {
            script_macro *macro=script_macro_resolve(&owner->table,pointer);if(!macro) break;
            ++owner->table.count;pointer=script_macro_word(macro,24);
        }
    }
    if(ok) *out=owner;else qa_script_defines_release(owner);
    qa_source_save_dispose(&io);qa_arena_destroy(&arena);return ok;
}
static void publish(qa_script_defines *owner,qa_script_defines *decoded)
{
    script_macro_table *candidate=&decoded->table;
    bool retained=owner->table.retained;owner->table.retained=false;
    script_table_dispose(&owner->table,false);
    owner->table=*candidate;owner->table.retained=retained;owner->table.deferred=false;
    for(script_macro *macro=owner->table.records;macro;macro=macro->registry_next) macro->owner=&owner->table;
    *candidate=(script_macro_table){0};
}
bool qa_script_defines_save_restore_into(qa_script_defines *owner,qa_bytes bytes,qa_error *error)
{
    if(!owner) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Global import requires its retained actual owner");return false;}
    qa_script_defines *decoded=NULL;if(!qa_script_defines_save_restore(bytes,&decoded,error)) return false;
    script_macro_table *candidate=&decoded->table;
    candidate->memory=owner->table.memory;
    if(candidate->memory.context && !script_table_adopt(candidate,false,error)) {
        qa_script_defines_release(decoded);return false;
    }
    publish(owner,decoded);qa_script_defines_release(decoded);return true;
}
struct qa_script_defines_prepared {
    qa_script_defines *owner,*decoded;
};
static bool prepare_record(script_lexer_allocation *record,void *context,
    qa_script_defines_alias alias,qa_error *error)
{
    if(!record->bytes) return true;
    qa_script_memory_allocation allocation;qa_script_memory_span span;
    if(!alias(context,record->reference,(qa_bytes){record->bytes,record->size},&allocation,&span,error)) return false;
    if(span.size!=record->size || !span.data) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Prepared script backing has the wrong extent");return false;
    }
    free(record->bytes);record->bytes=span.data;record->allocation=allocation;record->detached=false;return true;
}
bool qa_script_defines_save_prepare(qa_script_defines *owner,qa_bytes bytes,void *context,
    qa_script_defines_alias alias,qa_script_defines_prepared **out,qa_error *error)
{
    if(!owner || !owner->table.memory.context || !alias || !out || *out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Global history requires its bound owner, MEMORY aliases and empty output");return false;
    }
    qa_script_defines_prepared *plan=calloc(1,sizeof(*plan));
    if(!plan) {qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing global precompiler history");return false;}
    if(!qa_script_defines_save_restore(bytes,&plan->decoded,error)) {free(plan);return false;}
    plan->owner=owner;qa_script_defines_retain(owner);
    script_macro_table *candidate=&plan->decoded->table;candidate->memory=owner->table.memory;
    bool ok=true;
    for(script_macro *macro=candidate->records;ok && macro;macro=macro->registry_next)
        ok=prepare_record(&macro->record,context,alias,error);
    for(size_t i=0;ok && i<candidate->queue_records;++i)
        ok=prepare_record(&candidate->queue[i].record,context,alias,error);
    if(!ok) {qa_script_defines_save_finish(plan,false);return false;}
    *out=plan;return true;
}
void qa_script_defines_save_finish(qa_script_defines_prepared *plan,bool commit)
{
    if(!plan) return;
    if(commit) publish(plan->owner,plan->decoded);
    qa_script_defines_release(plan->decoded);qa_script_defines_release(plan->owner);free(plan);
}

/* Persistent definitions contain semantic tokens, never allocator aliases or
 * unreachable parser cells. History above retains its independent raw owner. */
static bool semantic_token(qa_source_save_io *io,qa_arena *arena,script_queued_token *token) {
    uint32_t kind=(uint32_t)token->token.kind;
    if(!qa_source_save_u32(io,&kind) || kind>QA_SCRIPT_PUNCTUATION ||
       !qa_source_save_u32(io,&token->token.subtype) || !qa_source_save_i32(io,&token->token.integer) ||
       !qa_source_save_f64(io,&token->token.number) || !span(io,arena,&token->token.text) ||
       token->token.text.size>=1024 || !location(io,arena,&token->token.location) ||
       !profile(io,arena,&token->unsupported)) return fail(io,"Invalid semantic global token");
    token->token.kind=(qa_script_token_kind)kind;
    return true;
}
static bool semantic_chain(qa_source_save_io *io,script_macro *macro,size_t offset,qa_arena *arena,size_t expected) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;script_macro_table *table=macro->owner;
    uint32_t pointer=reading?0:script_macro_word(macro,offset),last=0;size_t count=0;
    for(uint32_t at=pointer;at;) {
        script_token_record *node=script_heap_token(table,at);
        if(++count>table->queue_count || !script_heap_token_bytes(table,node,io->error)) return fail(io,"Global token chain is not live");
        at=qa_load_u32le(node->record.bytes+1064);
    }
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(expected!=SIZE_MAX && count!=expected) return fail(io,"Global parameter chain differs from its actual count");
    if(reading && count>(io->input.size-io->offset)/36) return fail(io,"Truncated semantic global token chain");
    for(size_t i=0;i<count;++i) {
        script_queued_token token=script_local_token();
        if(!reading) {
            script_token_record *node=script_heap_token(table,pointer);
            if(!script_heap_token_bytes(table,node,io->error) ||
               !script_token_load(node->record.bytes,node->extent,node->location,node->whitespace,arena,&token.token,io->error)) return false;
            token.unsupported=node->unsupported;pointer=qa_load_u32le(node->record.bytes+1064);
        }
        if(!semantic_token(io,arena,&token)) return false;
        if(reading && !script_macro_add_token(macro,offset,token,&last,io->error)) return false;
    }
    return true;
}
static bool semantic_definitions(qa_source_save_io *io,qa_script_defines *owner,qa_arena *arena) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;script_macro_table *table=&owner->table;
    size_t count=reading?0:table->count;uint32_t pointer=reading?0:table->first,previous=0;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(reading && count>(io->input.size-io->offset)/29) return fail(io,"Truncated semantic global definitions");
    for(size_t i=0;i<count;++i) {
        script_macro *macro=reading?NULL:script_macro_resolve(table,pointer);qa_bytes name={0};
        uint32_t flags=0,builtin=0,parameters=0;
        if(!reading) {
            if(!script_macro_bind(macro,io->error)) return false;
            name=script_macro_name(macro);flags=script_macro_word(macro,4);builtin=script_macro_word(macro,8);
            parameters=script_macro_word(macro,12);pointer=script_macro_word(macro,24);
        }
        if(!span(io,arena,&name) || !name.size || name.size>1023 || memchr(name.data,0,name.size) ||
           !qa_source_save_u32(io,&flags) || !qa_source_save_u32(io,&builtin) || builtin>4 ||
           !qa_source_save_u32(io,&parameters) || parameters>128)
            return fail(io,"Invalid semantic global definition");
        if(reading) {
            if(!script_macro_allocate(table,name,true,&macro,io->error)) return false;
            script_macro_word_set(macro,4,flags);script_macro_word_set(macro,8,builtin);script_macro_word_set(macro,12,parameters);
            macro->published=true;
            if(previous) script_macro_word_set(script_macro_resolve(table,previous),24,macro->pointer);
            else table->first=macro->pointer;
            previous=macro->pointer;++table->count;
        }
        if(!semantic_chain(io,macro,16,arena,parameters) || !semantic_chain(io,macro,20,arena,SIZE_MAX)) return false;
    }
    return reading || !pointer ? true : fail(io,"Global definition list differs from its actual count");
}
bool qa_script_defines_state_capture(const qa_script_defines *owner,qa_buffer *out,qa_error *error) {
    if(!owner || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Global state requires its actual owner/output");return false;}
    qa_source_save_io io={0};qa_arena arena={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && signature(&io) &&
        semantic_definitions(&io,(qa_script_defines *)owner,&arena) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);qa_arena_destroy(&arena);return ok;
}
bool qa_script_defines_state_restore_into(qa_script_defines *owner,qa_bytes bytes,qa_error *error) {
    if(!owner) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Global state requires its retained owner");return false;}
    qa_script_defines *decoded=NULL;qa_source_save_io io={0};qa_arena arena={0};
    bool ok=qa_script_defines_create(&decoded,error);
    if(ok) decoded->table.memory=owner->table.memory;
    if(ok) ok=qa_source_save_reader(&io,NULL,bytes,error) && signature(&io) &&
        semantic_definitions(&io,decoded,&arena) && qa_source_save_finish(&io,NULL);
    if(ok) publish(owner,decoded);
    qa_script_defines_release(decoded);qa_source_save_dispose(&io);qa_arena_destroy(&arena);return ok;
}
