#include "internal.h"

uint32_t script_macro_hash(qa_bytes name)
{
    uint32_t value=0;
    for(size_t i=0;i<name.size;++i) value+=(uint32_t)name.data[i]*(uint32_t)(119+i);
    return (value^(value>>10)^(value>>20))&1023;
}
bool script_macro_lookup(const script_macro_table *table,qa_bytes name,script_macro **out,qa_error *error)
{
    *out=NULL;
    if(!table->global && !script_table_hash_bind(table,error)) return false;
    uint32_t pointer=table->global?table->first:
        (table->hash.bytes?qa_load_u32le(table->hash.bytes+script_macro_hash(name)*4):0);
    size_t remaining=table->global?table->next_define_pointer:table->count;
    while(pointer) {
        script_macro *macro=script_macro_resolve(table,pointer);
        if(!remaining-- || !macro || !script_macro_bind(macro,error)) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"Define lookup reaches a dangling pointer or cycle");return false;
        }
        if(script_bytes_equal(script_macro_name(macro),name)) {*out=macro;return true;}
        pointer=script_macro_word(macro,table->global?24:28);
    }
    return true;
}
script_macro *script_macro_find(const script_macro_table *table,qa_bytes name)
{
    script_macro *macro=NULL;(void)script_macro_lookup(table,name,&macro,NULL);return macro;
}
bool script_macro_remove(script_macro_table *table,qa_bytes name,bool *fixed,qa_error *error)
{
    if(!table->global && !script_table_hash_bind(table,error)) return false;
    *fixed=false;uint32_t bucket=script_macro_hash(name);
    uint32_t pointer=table->global?table->first:(table->hash.bytes?qa_load_u32le(table->hash.bytes+bucket*4):0);
    uint8_t *link=table->global?NULL:(table->hash.bytes?table->hash.bytes+bucket*4:NULL);
    size_t remaining=table->next_define_pointer;
    while(pointer) {
        script_macro *macro=script_macro_resolve(table,pointer);
        if(!macro || !remaining-- || !script_macro_bind(macro,error)) {qa_error_set(error,QA_ERROR_FORMAT,0,"Define chain contains a dangling pointer");return false;}
        if(script_bytes_equal(script_macro_name(macro),name)) {
            if(!table->global && (script_macro_word(macro,4)&1)) {*fixed=true;return false;}
            /* PC_RemoveGlobalDefine reaches FreeDefine without unlinking. */
            if(!table->global) qa_store_u32le(link,script_macro_word(macro,28));
            if(!script_macro_free(macro,error)) return false;
            --table->count;return true;
        }
        if(!table->global) link=macro->record.bytes+28;
        pointer=script_macro_word(macro,table->global?24:28);
    }
    return false;
}
static bool source_empty(void *,bool *,qa_error *);
static bool source_redefine(void *,const script_queued_token *,qa_error *);
static bool parse_define(qa_script *source,size_t limit,script_macro **result,qa_error *error)
{
    script_macro_table *table=&source->macros;script_queued_token token={0};bool found;
    if(!script_line_token(source,&token,&found,error)) return false;
    if(!found || token.token.kind!=QA_SCRIPT_NAME) {
        if(found && !script_push(source,token,error)) return false;
        qa_error_set(error,QA_ERROR_FORMAT,0,"Script define requires a name");return false;
    }
    script_macro *existing=NULL;if(!script_macro_lookup(table,token.token.text,&existing,error)) return false;
    if(existing && (script_macro_word(existing,4)&1)) {qa_error_set(error,QA_ERROR_FORMAT,0,"Cannot redefine a fixed script macro");return false;}
    if(!existing && table->count>=limit) {qa_error_set(error,QA_ERROR_FORMAT,0,"Script define count exceeds configured limit");return false;}
    if(existing) {
        if(!source_redefine(source,&token,error)) return false;
        bool fixed;if(!script_macro_remove(table,token.token.text,&fixed,error)) return false;
    }
    script_macro *macro;
    if(!script_macro_allocate(table,token.token.text,true,&macro,error) || !script_macro_publish(table,macro,error)) return false;
    if(result) *result=macro;
    if(!script_line_token(source,&token,&found,error)) return false;
    if(!found) return true;
    uint32_t last=0;
    if(qa_script_token_is(&token.token,"(") && (token.raw?
        qa_load_u32le(token.bytes+1052)<=qa_load_u32le(token.bytes+1048):!token.token.leading_whitespace.size)) {
        bool empty=false;
        if(!source_empty(source,&empty,error)) return false;
        if(!empty) for(;;) {
            if(!script_line_token(source,&token,&found,error)) return false;
            if(!found || token.token.kind!=QA_SCRIPT_NAME || script_macro_word(macro,12)>=128) {
                qa_error_set(error,QA_ERROR_FORMAT,0,"Invalid script macro parameter");return false;
            }
            size_t remaining=table->queue_count;
            for(uint32_t pointer=script_macro_word(macro,16);pointer;) {
                if(!remaining--) {qa_error_set(error,QA_ERROR_FORMAT,0,"Parameter chain contains a cycle");return false;}
                script_token_record *node=script_heap_token(table,pointer);
                if(!script_heap_token_bytes(table,node,error)) return false;
                const uint8_t *zero=memchr(node->record.bytes,0,1024);
                if(zero && script_bytes_equal((qa_bytes){node->record.bytes,(size_t)(zero-node->record.bytes)},token.token.text)) {
                    qa_error_set(error,QA_ERROR_FORMAT,0,"Duplicate script macro parameter");return false;
                }
                pointer=qa_load_u32le(node->record.bytes+1064);
            }
            if(!script_macro_add_token(macro,16,token,&last,error)) return false;
            script_macro_word_set(macro,12,script_macro_word(macro,12)+1);
            if(!script_line_token(source,&token,&found,error)) return false;
            if(found && qa_script_token_is(&token.token,")")) break;
            if(!found || !qa_script_token_is(&token.token,",")) {qa_error_set(error,QA_ERROR_FORMAT,0,"Unterminated script macro parameters");return false;}
        }
        if(!script_line_token(source,&token,&found,error)) return false;
    }
    last=0;
    while(found) {
        if(token.token.kind==QA_SCRIPT_NAME && script_bytes_equal(token.token.text,script_macro_name(macro))) {
            /* The original copy occurs before the recursive token is discarded. */
            script_token_record *discarded;token.expansion=NULL;
            if(!script_heap_copy_token(table,token,&discarded,error)) return false;
        } else if(!script_macro_add_token(macro,20,token,&last,error)) return false;
        if(!script_line_token(source,&token,&found,error)) return false;
    }
    if(last) {
        script_token_record *first=script_heap_token(table,script_macro_word(macro,20));
        script_token_record *tail=script_heap_token(table,last);
        if(!script_heap_token_bytes(table,first,error) || !script_heap_token_bytes(table,tail,error)) return false;
        if(!strcmp((char *)first->record.bytes,"##") || !strcmp((char *)tail->record.bytes,"##")) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"Misplaced script macro merge operator");return false;
        }
    }
    return true;
}
static bool source_empty(void *context,bool *empty,qa_error *error)
{
    qa_script *source=context;script_queued_token token={0};bool found;
    if(!script_read_nested(source,&token,&found,error)) return false;
    *empty=found && qa_script_token_is(&token.token,")");
    return !found || *empty || script_push(source,token,error);
}
static bool source_redefine(void *context,const script_queued_token *token,qa_error *error)
{
    qa_script *source=context;
    static const char prefix[]="redefinition of ";size_t length=sizeof(prefix)-1;
    char *message=qa_arena_alloc(&source->arena,length+token->token.text.size+1,1,error);if(!message) return false;
    memcpy(message,prefix,length);memcpy(message+length,token->token.text.data,token->token.text.size);message[length+token->token.text.size]=0;
    script_warn(source,token->token.location,message);
    if(!script_push(source,*token,error)) return false;
    script_queued_token copied;bool found;
    return script_line_token(source,&copied,&found,error) && found;
}
bool script_define_stream(qa_script *source,qa_script_location location,qa_error *error)
{
    qa_error failure={0};bool ok=parse_define(source,source->options.maximum_defines,NULL,&failure);
    if(!ok && failure.code==QA_ERROR_FORMAT) return script_fail(source,location,failure.message,error);
    if(!ok && error) *error=failure;
    return ok;
}
static bool extern_read(void *context,const qa_script_include *request,qa_script_resource *out,bool *found,qa_error *error)
{
    (void)context;(void)request;(void)error;*out=(qa_script_resource){0};*found=false;return true;
}
static void extern_release(void *context,qa_script_resource *resource)
{
    (void)context;*resource=(qa_script_resource){0};
}
static bool from_text(script_macro_table *table,const char *text,size_t limit,script_macro **out,qa_error *error)
{
    if(!text) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Missing macro definition");return false;}
    qa_script_lexer_options options={.memory=table->memory.context?&table->memory:NULL};qa_script_lexer *lexer;
    if(!qa_script_lexer_open("*extern",script_bytes(text),&options,&lexer,error)) return false;
    script_macro_table temporary={0};
    if(!script_table_open(&temporary,table->memory.context?&table->memory:NULL,false,error)) {qa_script_lexer_close(lexer);return false;}
    /* The original extern engine owns a stack source_t, a real cleared hash,
     * and this same heap. Its checked parameter read uses the full reader. */
    uint8_t record[SCRIPT_SOURCE_BYTES]={0};memcpy(record,"*extern",7);
    qa_store_u32le(record+SCRIPT_SOURCE_STACK,1);qa_store_u32le(record+SCRIPT_SOURCE_HASH,1);
    script_frame frame={.resource={"*extern",script_bytes(text),NULL},.lexer=lexer,.active=true};size_t stack=0;
    qa_script source={.memory=table->memory,.macros=*table,.source_record={.bytes=record,.detached=true},
        .frames=&frame,.frame_count=1,.stack=&stack,.stack_count=1,.next_condition_pointer=1,
        .services={.read=extern_read,.release=extern_release},
        .options={.token_limit=1024,.include_path="",.maximum_include_depth=SIZE_MAX,
            .maximum_expansions=SIZE_MAX,.maximum_queued_tokens=SIZE_MAX,.maximum_output_tokens=SIZE_MAX,
            .maximum_defines=limit,.maximum_expression_tokens=SIZE_MAX,.maximum_source_tokens=SIZE_MAX}};
    source.services.memory=source.memory.context?&source.memory:NULL;
    script_lexer_allocation prior_hash=table->hash;bool prior_global=table->global;size_t prior_count=table->count;
    script_macro *prior_records=table->records;
    source.macros.hash=temporary.hash;source.macros.global=false;source.macros.count=0;source.macros.source=&source;
    script_macro *macro=NULL;
    bool ok=parse_define(&source,limit,&macro,error);
    if(ok) for(size_t bucket=0;bucket<1024;++bucket) {
        uint32_t pointer=qa_load_u32le(source.macros.hash.bytes+bucket*4);
        if(pointer) {macro=script_macro_resolve(&source.macros,pointer);break;}
    }
    for(script_macro *row=source.macros.records;row!=prior_records;row=row->registry_next) row->published=false;
    script_queue_close(&source,true);
    temporary.hash=source.macros.hash;
    if(table->memory.context) (void)table->memory.free(table->memory.context,temporary.hash.allocation,NULL);
    else free(temporary.hash.bytes);
    temporary.hash=(script_lexer_allocation){0};script_table_dispose(&temporary,false);
    qa_script_lexer_close(lexer);
    source.macros.hash=prior_hash;source.macros.global=prior_global;source.macros.count=prior_count;source.macros.source=table->source;*table=source.macros;
    for(script_macro *row=table->records;row;row=row->registry_next) row->owner=table;
    script_conditions_close(&source,false);free(source.reads);qa_arena_destroy(&source.arena);
    if(ok) {
        if(!macro) {qa_error_set(error,QA_ERROR_FORMAT,0,"Extern definition has no reached hash entry");return false;}
        script_macro_word_set(macro,28,0);
        if(out) *out=macro;
        else ok=script_macro_publish(table,macro,error);
    }
    return ok;
}
bool script_macro_text(script_macro_table *table,const char *definition,size_t limit,qa_error *error)
{
    return from_text(table,definition,limit,NULL,error);
}
bool script_globals_import(script_macro_table *to,const qa_script_defines *from,qa_error *error)
{
    script_macro_table *owner=(script_macro_table *)&from->table;
    if(!script_table_adopt(owner,false,error)) return false;
    uint32_t pointer=owner->first;size_t remaining=owner->next_define_pointer;
    while(pointer) {
        script_macro *original=script_macro_resolve(owner,pointer),*copy;
        if(!original || !remaining-- || !script_macro_bind(original,error)) {qa_error_set(error,QA_ERROR_FORMAT,0,"Global copy reaches a dangling define");return false;}
        if(!script_macro_allocate(to,script_macro_name(original),false,&copy,error)) return false;
        for(size_t offset=4;offset<=12;offset+=4) script_macro_word_set(copy,offset,script_macro_word(original,offset));
        script_macro_word_set(copy,24,0);script_macro_word_set(copy,28,0);script_macro_word_set(copy,20,0);
        for(size_t chain=0;chain<2;++chain) {
            size_t offset=chain?16:20;uint32_t last=0,token_pointer=script_macro_word(original,offset);
            if(chain) script_macro_word_set(copy,16,0);
            size_t token_remaining=owner->queue_count;
            while(token_pointer) {
                script_token_record *node=script_heap_token(owner,token_pointer),*copied;
                if(!token_remaining-- || !script_heap_token_bytes(owner,node,error)) return false;
                script_queued_token token={.raw=true};memcpy(token.bytes,node->record.bytes,SCRIPT_TOKEN_BYTES);
                if(!script_token_load(token.bytes,node->extent,node->location,node->whitespace,&to->arena,&token.token,error) ||
                   !script_heap_copy_token(to,token,&copied,error)) return false;
                if(last) qa_store_u32le(script_heap_token(to,last)->record.bytes+1064,copied->pointer);
                else script_macro_word_set(copy,offset,copied->pointer);
                last=copied->pointer;token_pointer=qa_load_u32le(node->record.bytes+1064);
            }
        }
        if(!script_macro_publish(to,copy,error)) return false;
        pointer=script_macro_word(original,24);
    }
    return true;
}
bool qa_script_defines_create(qa_script_defines **out,qa_error *error)
{
    if(!out) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Missing global define output");return false;}
    qa_script_defines *owner=calloc(1,sizeof(*owner));
    if(!owner) {qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating global define owner");return false;}
    atomic_init(&owner->references,1);
    if(!script_table_open(&owner->table,NULL,true,error)) {free(owner);return false;}
    *out=owner;return true;
}
void qa_script_defines_retain(qa_script_defines *owner)
{
    if(owner) atomic_fetch_add_explicit(&owner->references,1,memory_order_relaxed);
}
void qa_script_defines_release(qa_script_defines *owner)
{
    if(owner && atomic_fetch_sub_explicit(&owner->references,1,memory_order_acq_rel)==1) {
        script_table_dispose(&owner->table,true);free(owner);
    }
}
bool qa_script_defines_add(qa_script_defines *owner,const char *definition,qa_error *error)
{
    if(!owner || !definition) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Missing global define owner");return false;}
    if(!script_table_adopt(&owner->table,false,error)) return false;
    script_macro *macro;
    return from_text(&owner->table,definition,SIZE_MAX,&macro,error) && script_macro_publish(&owner->table,macro,error);
}
bool qa_script_defines_remove(qa_script_defines *owner,const char *name,qa_error *error)
{
    if(!owner || !name) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Missing global define name/owner");return false;}
    if(!script_table_adopt(&owner->table,false,error)) return false;
    bool fixed;return script_macro_remove(&owner->table,script_bytes(name),&fixed,error);
}
bool qa_script_defines_clear(qa_script_defines *owner,qa_error *error)
{
    return !owner || (script_table_adopt(&owner->table,false,error) && script_table_clear(&owner->table,error));
}
const qa_script_memory *qa_script_defines_memory(const qa_script_defines *owner)
{
    return owner && owner->table.memory.context?&owner->table.memory:NULL;
}
