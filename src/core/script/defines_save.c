#include "internal.h"
#include "qa/script_defines_save.h"
#include "qa/source_save.h"

static bool fail(qa_source_save_io *io,const char *message)
{
    qa_error_set(io->error,QA_ERROR_FORMAT,io->offset,"%s",message);
    io->failed=true;return false;
}

static bool span(qa_source_save_io *io,qa_arena *arena,qa_bytes *value)
{
    size_t count=value->size;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(count>io->input.size-io->offset) return fail(io,"Truncated global macro byte span");
        char *bytes=script_string(arena,io->input.data+io->offset,count,io->error);
        if(!bytes) {io->failed=true;return false;}
        *value=(qa_bytes){(const uint8_t *)bytes,count};io->offset+=count;return true;
    }
    return qa_source_save_bytes(io,(void *)value->data,count);
}

static bool text(qa_source_save_io *io,qa_arena *arena,const char **value)
{
    bool present=*value!=NULL;
    if(!qa_source_save_bool(io,&present)) return false;
    if(!present) {*value=NULL;return true;}
    qa_bytes bytes=io->direction==QA_SOURCE_SAVE_READ?(qa_bytes){0}:
        (qa_bytes){(const uint8_t *)*value,strlen(*value)};
    if(!span(io,arena,&bytes) || (bytes.size && memchr(bytes.data,0,bytes.size)))
        return fail(io,"Global macro location has no actual C string");
    if(io->direction==QA_SOURCE_SAVE_READ) *value=(const char *)bytes.data;
    return true;
}

static bool name_valid(qa_bytes name)
{
    if(!name.size || name.size>=1024 || !script_alpha(name.data[0])) return false;
    for(size_t index=1;index<name.size;++index) if(!script_name(name.data[index])) return false;
    return true;
}

static bool token(qa_source_save_io *io,qa_arena *arena,qa_script_token *value)
{
    uint32_t kind=(uint32_t)value->kind;uint64_t bits;
    memcpy(&bits,&value->number,sizeof(bits));
    if(!qa_source_save_u32(io,&kind) || kind>QA_SCRIPT_PUNCTUATION ||
       !qa_source_save_u32(io,&value->subtype) || !qa_source_save_u32(io,&value->lines_crossed) ||
       !qa_source_save_i32(io,&value->integer) || !qa_source_save_u64(io,&bits) ||
       !span(io,arena,&value->text) || !span(io,arena,&value->leading_whitespace) ||
       !text(io,arena,&value->location.path) || !qa_source_save_u32(io,&value->location.line) ||
       !qa_source_save_u32(io,&value->location.column) ||
       !qa_source_save_count(io,&value->location.offset,SIZE_MAX)) return false;
    value->kind=(qa_script_token_kind)kind;memcpy(&value->number,&bits,sizeof(bits));
    if(!value->text.size || value->text.size>=1024 || value->lines_crossed || value->leading_whitespace.size ||
       !value->location.path || !value->location.line || !value->location.column)
        return fail(io,"Invalid retained global macro token");
    return true;
}

static bool macro(qa_source_save_io *io,qa_arena *arena,qa_script_macro_state *value)
{
    uint32_t builtin=value->builtin;
    if(!span(io,arena,&value->name) || !name_valid(value->name) ||
       !qa_source_save_u32(io,&builtin) || builtin ||
       !qa_source_save_bool(io,&value->function) || !qa_source_save_bool(io,&value->fixed) ||
       !qa_source_save_count(io,&value->parameter_count,128)) return false;
    value->builtin=builtin;
    if(value->fixed) return fail(io,"Global macro has no actual fixed-definition producer");
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(value->parameter_count && !value->function) return fail(io,"Object global macro has parameters");
    qa_bytes *parameters=(qa_bytes *)value->parameters;
    if(reading && value->parameter_count) {
        if(value->parameter_count>(io->input.size-io->offset)/8)
            return fail(io,"Truncated global macro parameters");
        parameters=qa_arena_alloc(arena,value->parameter_count*sizeof(*parameters),_Alignof(qa_bytes),io->error);
        if(!parameters) {io->failed=true;return false;}
        memset(parameters,0,value->parameter_count*sizeof(*parameters));value->parameters=parameters;
    }
    for(size_t index=0;index<value->parameter_count;++index) {
        if(!span(io,arena,parameters+index) || !name_valid(parameters[index])) return false;
        for(size_t prior=0;prior<index;++prior)
            if(script_bytes_equal(parameters[prior],parameters[index])) return fail(io,"Duplicate global macro parameter");
    }
    if(!qa_source_save_count(io,&value->token_count,SIZE_MAX/sizeof(qa_script_token))) return false;
    qa_script_token *tokens=(qa_script_token *)value->tokens;
    if(reading && value->token_count) {
        if(value->token_count>(io->input.size-io->offset)/49) return fail(io,"Truncated global macro body");
        tokens=qa_arena_alloc(arena,value->token_count*sizeof(*tokens),_Alignof(qa_script_token),io->error);
        if(!tokens) {io->failed=true;return false;}
        memset(tokens,0,value->token_count*sizeof(*tokens));value->tokens=tokens;
    }
    for(size_t index=0;index<value->token_count;++index) {
        qa_script_token cell=reading?(qa_script_token){0}:tokens[index];
        if(!token(io,arena,&cell)) return false;
        if(reading) tokens[index]=cell;
    }
    return true;
}

static bool fields(qa_source_save_io *io,qa_script_defines *owner)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t total=owner->table.count;
    if(!qa_source_save_count(io,&total,SIZE_MAX)) return false;
    if(reading && total>(io->input.size-io->offset)/31) return fail(io,"Global macro count exceeds its payload");
    qa_arena scratch={0};bool okay=true;
    script_macro *cell=owner->first;script_macro **tail=&owner->first;
    for(size_t index=0;okay && index<total;++index) {
        if(!reading && !cell) {okay=fail(io,"Global macro chain is shorter than its actual count");break;}
        qa_script_macro_state value=reading?(qa_script_macro_state){0}:
            (qa_script_macro_state){.name=cell->name,.parameters=cell->parameters,.tokens=cell->tokens,
                .parameter_count=cell->parameter_count,.token_count=cell->token_count,
                .builtin=cell->builtin,.function=cell->function,.fixed=cell->fixed};
        okay=macro(io,&scratch,&value);
        if(!okay) break;
        if(reading) {
            script_macro *restored;
            okay=script_macro_copy(&owner->table,&value,&restored,io->error);
            if(!okay) {io->failed=true;break;}
            *tail=restored;tail=&restored->next;++owner->table.count;
        } else cell=cell->next;
    }
    if(okay && !reading && cell) okay=fail(io,"Global macro chain exceeds its actual count");
    qa_arena_destroy(&scratch);return okay;
}

static bool signature(qa_source_save_io *io)
{
    static const uint8_t expected[8]={'Q','A','S','D','E','F','S',0};
    uint8_t bytes[8];memcpy(bytes,expected,sizeof(bytes));uint32_t version=1;
    return qa_source_save_bytes(io,bytes,sizeof(bytes)) && !memcmp(bytes,expected,sizeof(bytes)) &&
        qa_source_save_u32(io,&version) && version==1;
}

bool qa_script_defines_save_capture(const qa_script_defines *owner,qa_buffer *out,qa_error *error)
{
    if(!owner || !out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Global macro capture needs its actual owner and output");return false;}
    qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,NULL,error) && signature(&io) &&
        fields(&io,(qa_script_defines *)owner) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);return okay;
}

bool qa_script_defines_save_restore(qa_bytes bytes,qa_script_defines **out,qa_error *error)
{
    if(!out || *out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Global macro restore needs a fresh owner output");return false;}
    qa_script_defines *owner=NULL;qa_source_save_io io={0};
    bool okay=qa_script_defines_create(&owner,error) && qa_source_save_reader(&io,NULL,bytes,error) &&
        signature(&io) && fields(&io,owner) && qa_source_save_finish(&io,NULL);
    if(okay) *out=owner;else qa_script_defines_release(owner);
    if(!okay && (!error || error->code==QA_OK)) qa_error_set(error,QA_ERROR_FORMAT,io.offset,"Invalid actual global macro continuation");
    qa_source_save_dispose(&io);return okay;
}
