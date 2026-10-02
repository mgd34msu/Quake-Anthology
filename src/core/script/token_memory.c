#include "internal.h"

void script_token_float(uint8_t *bytes,double number)
{
    uint64_t bits;memcpy(&bits,&number,8);
    uint64_t fraction=bits&UINT64_C(0xfffffffffffff),significand;
    uint16_t exponent=(uint16_t)((bits>>52)&0x7ff),word=(uint16_t)((bits>>48)&0x8000);
    if(!exponent && !fraction) significand=0;
    else if(!exponent) {
        unsigned highest=0;for(uint64_t probe=fraction;probe>>=1;) ++highest;
        significand=fraction<<(63-highest);exponent=(uint16_t)(highest+15309);
    } else {
        significand=(fraction|UINT64_C(0x10000000000000))<<11;
        exponent=exponent==0x7ff?0x7fff:(uint16_t)(exponent+15360);
    }
    qa_store_u64le(bytes,significand);qa_store_u16le(bytes+8,(uint16_t)(word|exponent));
}
static bool float_value(const uint8_t *bytes,double *out,qa_error *error)
{
    uint64_t significand=qa_load_u64le(bytes);uint16_t word=qa_load_u16le(bytes+8);
    uint16_t exponent=word&0x7fff;uint64_t bits=(uint64_t)(word&0x8000)<<48;
    if(exponent || significand) {
        if(!(significand&UINT64_C(0x8000000000000000))) goto unsupported;
        if(exponent==0x7fff || (exponent>=15361 && exponent<=17406)) {
            if(significand&2047) goto unsupported;
            bits|=(uint64_t)(exponent==0x7fff?0x7ff:exponent-15360)<<52;
            bits|=(significand>>11)&UINT64_C(0xfffffffffffff);
        } else if(exponent>=15309 && exponent<=15360) {
            unsigned shift=63-(unsigned)(exponent-15309);
            if(significand&((UINT64_C(1)<<shift)-1)) goto unsupported;
            bits|=significand>>shift;
        } else goto unsupported;
    }
    memcpy(out,&bits,8);return true;
unsupported:
    qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"token_t long double exceeds the binary64 source profile");return false;
}
bool script_token_store(uint8_t *bytes,const qa_script_token *token,uint32_t start,uint32_t end,qa_error *error)
{
    if(token->text.size>=1024 || (token->text.size && !token->text.data) ||
       token->kind<QA_SCRIPT_PRIMITIVE || token->kind>QA_SCRIPT_PUNCTUATION) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Token does not fit the source token_t profile");return false;
    }
    memset(bytes,0,SCRIPT_TOKEN_BYTES);
    if(token->text.size) memcpy(bytes,token->text.data,token->text.size);
    qa_store_u32le(bytes+1024,(uint32_t)token->kind);qa_store_u32le(bytes+1028,token->subtype);
    qa_store_u32le(bytes+1032,(uint32_t)token->integer);script_token_float(bytes+1036,token->number);
    qa_store_u32le(bytes+1048,start);qa_store_u32le(bytes+1052,end);
    qa_store_u32le(bytes+1056,token->location.line);qa_store_u32le(bytes+1060,token->lines_crossed);
    return true;
}
static bool token_load(const uint8_t *bytes,size_t extent,qa_script_location location,qa_bytes whitespace,
    qa_arena *arena,qa_script_token *out,bool partial,qa_error *error)
{
    uint32_t type=qa_load_u32le(bytes+1024);const uint8_t *zero=memchr(bytes,0,1024);
    if(type>QA_SCRIPT_PUNCTUATION || (!zero && !partial) || (extent!=SIZE_MAX && extent>=1024)) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Retained lexer token has an invalid raw type or text extent");return false;
    }
    size_t terminated=zero?(size_t)(zero-bytes):1024;
    if(extent==SIZE_MAX || extent<terminated) extent=terminated;
    char *text=script_string(arena,bytes,extent,error);
    if(!text) return false;
    uint32_t integer=qa_load_u32le(bytes+1032);int32_t signed_integer;
    memcpy(&signed_integer,&integer,4);double number;
    if(!float_value(bytes+1036,&number,error)) return false;
    location.line=qa_load_u32le(bytes+1056);
    *out=(qa_script_token){.kind=(qa_script_token_kind)type,.subtype=qa_load_u32le(bytes+1028),
        .lines_crossed=qa_load_u32le(bytes+1060),.integer=signed_integer,.number=number,
        .text={(uint8_t *)text,extent},.leading_whitespace=whitespace,.location=location};
    return true;
}

bool script_token_load(const uint8_t *bytes,size_t extent,qa_script_location location,qa_bytes whitespace,
    qa_arena *arena,qa_script_token *out,qa_error *error) {
    return token_load(bytes,extent,location,whitespace,arena,out,false,error);
}
bool script_token_output(const uint8_t *bytes,size_t extent,qa_script_location location,qa_bytes whitespace,
    qa_arena *arena,qa_script_token *out,qa_error *error) {
    return token_load(bytes,extent,location,whitespace,arena,out,true,error);
}

script_token_record *script_heap_token(const script_macro_table *s,uint32_t pointer)
{
    for(size_t i=0;i<s->queue_records;++i)
        if(s->queue[i].record.bytes && s->queue[i].pointer==pointer) return s->queue+i;
    return NULL;
}
bool script_heap_token_bytes(const script_macro_table *s,script_token_record *node,qa_error *error)
{
    if(!node || !node->record.bytes) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Source token pointer has no actual live token_t");return false;
    }
    if(s->memory.context && !node->record.detached) {
        qa_script_memory_span span;
        if(!s->memory.bytes(s->memory.context,node->record.allocation,&span,error)) return false;
        if(span.size!=SCRIPT_TOKEN_BYTES) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"Source token allocation differs from token_t extent");return false;
        }
        node->record.bytes=span.data;
    }
    return true;
}
static bool queue_slot(script_macro_table *s,script_token_record **out,qa_error *error)
{
    size_t slot=0;
    while(slot<s->queue_records && s->queue[slot].record.bytes) ++slot;
    if(slot==s->queue_records) {
        if(!script_grow((void **)&s->queue,&s->queue_capacity,slot+1,sizeof(*s->queue),error)) return false;
        ++s->queue_records;
    }
    s->queue[slot]=(script_token_record){.record={.reference=SIZE_MAX,.size=SCRIPT_TOKEN_BYTES}};
    *out=s->queue+slot;return true;
}
static bool queue_context(script_macro_table *s,script_token_record *node,const qa_script_token *token,qa_error *error)
{
    node->location=token->location;
    const char *path=token->location.path?token->location.path:"";
    node->location.path=script_string(&s->arena,path,strlen(path),error);
    node->whitespace=(qa_bytes){(uint8_t *)script_string(&s->arena,token->leading_whitespace.data,
        token->leading_whitespace.size,error),token->leading_whitespace.size};
    node->extent=token->text.size && memchr(token->text.data,0,token->text.size)?token->text.size:SIZE_MAX;
    return node->location.path && node->whitespace.data;
}
bool script_heap_copy_token(script_macro_table *s,script_queued_token token,script_token_record **out,qa_error *error)
{
    if(!s->next_token_pointer || s->next_token_pointer==UINT32_MAX) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Source token pointer identities exhausted");return false;
    }
    script_token_record *node;
    if(!queue_slot(s,&node,error) || !queue_context(s,node,&token.token,error)) return false;
    if(!token.raw && !script_token_store(token.bytes,&token.token,0,0,error)) return false;
    if(token.unsupported) {
        node->unsupported=script_string(&s->arena,token.unsupported,strlen(token.unsupported),error);
        if(!node->unsupported) return false;
    }
    node->pointer=s->next_token_pointer++;
    if(s->memory.context) {
        if(!s->memory.allocate(s->memory.context,SCRIPT_TOKEN_BYTES,false,&node->record.allocation,error)) return false;
        qa_script_memory_span span={0};
        bool borrowed=s->memory.bytes(s->memory.context,node->record.allocation,&span,error);
        if(!borrowed || span.size!=SCRIPT_TOKEN_BYTES) {
            (void)s->memory.free(s->memory.context,node->record.allocation,NULL);
            if(borrowed) qa_error_set(error,QA_ERROR_FORMAT,0,"Allocated token differs from token_t extent");
            return false;
        }
        node->record.bytes=span.data;
    } else {
        node->record.bytes=malloc(SCRIPT_TOKEN_BYTES);
        if(!node->record.bytes) {qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating raw source token");return false;}
    }
    memcpy(node->record.bytes,token.bytes,SCRIPT_TOKEN_BYTES);node->expansion=token.expansion;
    qa_store_u32le(node->record.bytes+1064,0);++s->queue_count;*out=node;return true;
}
bool script_push(qa_script *s,script_queued_token token,qa_error *error)
{
    if(!script_memory_enter(s,error)) return false;
    size_t count=0;uint32_t pointer=qa_load_u32le(s->source_record.bytes+SCRIPT_SOURCE_TOKENS);
    while(pointer) {
        script_token_record *node=script_heap_token(&s->macros,pointer);
        if(!script_heap_token_bytes(&s->macros,node,error)) return false;
        if(count++>=s->options.maximum_queued_tokens || count>s->macros.queue_count)
            return script_fail(s,token.token.location,"Script queue exceeds configured limit",error);
        pointer=qa_load_u32le(node->record.bytes+1064);
    }
    if(count>=s->options.maximum_queued_tokens)
        return script_fail(s,token.token.location,"Script queue exceeds configured limit",error);
    script_token_record *node;
    if(!script_heap_copy_token(&s->macros,token,&node,error)) return false;
    qa_store_u32le(node->record.bytes+1064,qa_load_u32le(s->source_record.bytes+SCRIPT_SOURCE_TOKENS));
    qa_store_u32le(s->source_record.bytes+SCRIPT_SOURCE_TOKENS,node->pointer);return true;
}
bool script_heap_free_token(script_macro_table *table,script_token_record *node,qa_error *error)
{
    if(!script_heap_token_bytes(table,node,error)) return false;
    if(table->memory.context && !node->record.detached) {
        if(!table->memory.free(table->memory.context,node->record.allocation,error)) return false;
    } else free(node->record.bytes);
    node->record.bytes=NULL;--table->queue_count;return true;
}
bool script_queue_pop(qa_script *s,script_queued_token *out,qa_error *error)
{
    script_token_record *node=script_heap_token(&s->macros,qa_load_u32le(s->source_record.bytes+SCRIPT_SOURCE_TOKENS));
    if(!script_heap_token_bytes(&s->macros,node,error)) return false;
    uint32_t next=qa_load_u32le(node->record.bytes+1064);
    if(next && !script_heap_token(&s->macros,next)) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Source token next pointer has no live token_t");return false;
    }
    *out=(script_queued_token){.expansion=node->expansion,.raw=true,.unsupported=node->unsupported};
    memcpy(out->bytes,node->record.bytes,SCRIPT_TOKEN_BYTES);
    if(!script_token_load(out->bytes,node->extent,node->location,node->whitespace,&s->arena,&out->token,error)) return false;
    qa_store_u32le(s->source_record.bytes+SCRIPT_SOURCE_TOKENS,next);
    return script_heap_free_token(&s->macros,node,error);
}
bool script_queue_snapshot(const script_macro_table *s,qa_script_queued_state *out,qa_arena *arena,qa_error *error)
{
    size_t count=0;
    for(size_t i=0;i<s->queue_records;++i) {
        script_token_record *node=s->queue+i;
        if(!node->record.bytes) continue;
        if(!script_heap_token_bytes(s,node,error)) return false;
        qa_script_queued_state *saved=out+count++;
        saved->pointer=node->pointer;saved->memory_reference=node->record.reference;saved->text_extent=node->extent;
        if(node->unsupported) {
            saved->unsupported=script_string(arena,node->unsupported,strlen(node->unsupported),error);
            if(!saved->unsupported) return false;
        }
        memcpy(saved->bytes,node->record.bytes,SCRIPT_TOKEN_BYTES);
        if(s->memory.context && !node->record.detached && !s->memory.reference(s->memory.context,
            node->record.allocation,&saved->memory_reference,error)) return false;
        if(!script_token_load(saved->bytes,node->extent,node->location,node->whitespace,arena,&saved->token,error)) return false;
    }
    return count==s->queue_count;
}
bool script_queue_restore(script_macro_table *s,const qa_script_checkpoint *checkpoint,const script_expansion *expansions,qa_error *error)
{
    s->next_token_pointer=checkpoint->next_token_pointer;
    for(size_t i=0;i<checkpoint->queue_count;++i) {
        const qa_script_queued_state *saved=checkpoint->queue+i;script_token_record *node;
        if(!queue_slot(s,&node,error) || !queue_context(s,node,&saved->token,error)) return false;
        if(saved->unsupported) {
            node->unsupported=script_string(&s->arena,saved->unsupported,strlen(saved->unsupported),error);
            if(!node->unsupported) return false;
        }
        node->pointer=saved->pointer;node->extent=saved->text_extent;
        node->expansion=saved->expansion==SIZE_MAX?NULL:expansions+saved->expansion;
        node->record.reference=saved->memory_reference;
        if(s->memory.context && !s->deferred) {
            qa_script_memory_span span;
            if(saved->memory_reference==SIZE_MAX ||
                !s->memory.resolve(s->memory.context,saved->memory_reference,&node->record.allocation,error) ||
                !s->memory.bytes(s->memory.context,node->record.allocation,&span,error)) return false;
            if(span.size!=SCRIPT_TOKEN_BYTES || memcmp(span.data,saved->bytes,SCRIPT_TOKEN_BYTES)) {
                qa_error_set(error,QA_ERROR_FORMAT,i,"Saved source token differs from restored MEMORY");return false;
            }
            node->record.bytes=span.data;
        } else {
            node->record.bytes=malloc(SCRIPT_TOKEN_BYTES);
            if(!node->record.bytes) {qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring detached source token");return false;}
            memcpy(node->record.bytes,saved->bytes,SCRIPT_TOKEN_BYTES);node->record.detached=true;
        }
        ++s->queue_count;
    }
    return true;
}
bool script_queue_adopt(script_macro_table *s,bool history,qa_error *error)
{
    if(!s->memory.context) return true;
    for(size_t i=0;i<s->queue_records;++i) {
        script_token_record *node=s->queue+i;
        if(!node->record.bytes || !node->record.detached) continue;
        qa_script_memory_span span;
        if(node->record.reference==SIZE_MAX || !(history?s->memory.resolve_history(s->memory.context,node->record.reference,&node->record.allocation,error):
            s->memory.resolve(s->memory.context,node->record.reference,&node->record.allocation,error)) ||
            !s->memory.bytes(s->memory.context,node->record.allocation,&span,error)) return false;
        if(span.size!=SCRIPT_TOKEN_BYTES || memcmp(span.data,node->record.bytes,SCRIPT_TOKEN_BYTES)) {
            qa_error_set(error,QA_ERROR_FORMAT,i,"History source token differs from committed MEMORY");return false;
        }
        free(node->record.bytes);node->record.bytes=span.data;node->record.detached=false;
    }
    return true;
}
void script_queue_close(qa_script *s,bool source)
{
    if(source && s->source_record.bytes) {
        uint32_t pointer=qa_load_u32le(s->source_record.bytes+SCRIPT_SOURCE_TOKENS);
        size_t visited=0,total=s->macros.queue_count;
        while(pointer && visited++<total) {
            script_token_record *node=script_heap_token(&s->macros,pointer);
            if(!script_heap_token_bytes(&s->macros,node,NULL)) break;
            pointer=qa_load_u32le(node->record.bytes+1064);
            qa_store_u32le(s->source_record.bytes+SCRIPT_SOURCE_TOKENS,pointer);
            if(!script_heap_free_token(&s->macros,node,NULL)) break;
        }
    }
}
bool script_token_saved_valid(const qa_script_queued_state *saved,bool partial,qa_error *error)
{
    qa_arena arena={0};qa_script_token projected;
    bool ok=token_load(saved->bytes,saved->text_extent,saved->token.location,
        saved->token.leading_whitespace,&arena,&projected,partial,error);
    if(ok) {
        const qa_script_token *token=&saved->token;
        ok=projected.kind==token->kind && projected.subtype==token->subtype &&
            projected.integer==token->integer && projected.lines_crossed==token->lines_crossed &&
            projected.location.line==token->location.line && script_bytes_equal(projected.text,token->text) &&
            !memcmp(&projected.number,&token->number,sizeof(token->number));
        if(!ok) qa_error_set(error,QA_ERROR_FORMAT,0,"Queued token projection differs from actual token_t bytes");
    }
    qa_arena_destroy(&arena);return ok;
}
