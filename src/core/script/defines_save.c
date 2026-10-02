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
        if(size>io->input.size-io->offset) return fail(io,"Truncated global precompiler bytes");
        char *bytes=script_string(arena,io->input.data+io->offset,size,io->error);
        if(!bytes) {io->failed=true;return false;}
        *value=(qa_bytes){(uint8_t *)bytes,size};io->offset+=size;return true;
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
static bool signature(qa_source_save_io *io)
{
    static const uint8_t expected[8]={'Q','A','S','D','E','F','S',0};uint8_t bytes[8];memcpy(bytes,expected,8);uint32_t version=2;
    return qa_source_save_bytes(io,bytes,8) && !memcmp(bytes,expected,8) && qa_source_save_u32(io,&version) && version==2;
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
