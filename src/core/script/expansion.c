#include "internal.h"

typedef struct token_chain {uint32_t first,last;size_t count;} token_chain;
static bool value(qa_script *source,uint32_t pointer,script_queued_token *out,qa_error *error) {
    script_token_record *token=script_heap_token(&source->macros,pointer);
    if(!script_heap_token_bytes(&source->macros,token,error)) return false;
    *out=(script_queued_token){.raw=true,.expansion=token->expansion,.unsupported=token->unsupported};
    memcpy(out->bytes,token->record.bytes,SCRIPT_TOKEN_BYTES);
    return script_token_load(out->bytes,token->extent,token->location,token->whitespace,&source->arena,&out->token,error);
}
static bool chain_value(qa_script *source,uint32_t pointer,size_t *remaining,script_queued_token *out,qa_error *error) {
    if(!(*remaining)--) {qa_error_set(error,QA_ERROR_FORMAT,0,"Macro token chain contains a cycle");return false;}
    return value(source,pointer,out,error);
}
static bool next(qa_script *source,uint32_t pointer,uint32_t *out,qa_error *error) {
    script_token_record *token=script_heap_token(&source->macros,pointer);
    if(!script_heap_token_bytes(&source->macros,token,error)) return false;
    *out=qa_load_u32le(token->record.bytes+1064);return true;
}
static bool append(qa_script *source,token_chain *chain,script_queued_token value,qa_script_location location,qa_error *error) {
    script_token_record *copied;
    if(!script_heap_copy_token(&source->macros,value,&copied,error)) return false;
    /* CopyToken precedes the expanded-list limit check. A refused append leaves
     * the reached allocation in the actual heap without publishing a link. */
    if(chain->count>=source->options.maximum_queued_tokens)
        return script_fail(source,location,"Macro expansion queue exceeds configured limit",error);
    uint32_t pointer=copied->pointer;
    if(chain->last) {
        script_token_record *last=script_heap_token(&source->macros,chain->last);
        if(!script_heap_token_bytes(&source->macros,last,error)) return false;
        qa_store_u32le(last->record.bytes+1064,pointer);
    } else chain->first=pointer;
    chain->last=pointer;++chain->count;return true;
}
static int parameter(qa_script *source,script_macro *macro,const qa_script_token *candidate,qa_error *error) {
    if(candidate->kind!=QA_SCRIPT_NAME) return -1;
    uint32_t pointer=script_macro_word(macro,16);size_t remaining=source->macros.queue_count;int index=0;
    while(pointer) {
        script_queued_token token;
        if(!chain_value(source,pointer,&remaining,&token,error)) return -2;
        if(script_bytes_equal(token.token.text,candidate->text)) return index;
        if(!next(source,pointer,&pointer,error)) return -2;
        ++index;
    }
    return -1;
}
static bool arguments(qa_script *source,script_macro *macro,qa_script_location location,uint32_t heads[128],qa_error *error) {
    script_queued_token token;bool found;
    if(!script_raw(source,&token,&found,error)) return false;
    uint32_t count=script_macro_word(macro,12);
    if(count>128) return script_fail(source,location,"Macro has more than 128 parameters",error);
    if(!found || !qa_script_token_is(&token.token,"(")) {
        if(found && !script_push(source,token,error)) return false;
        return script_fail(source,location,"Function macro requires arguments",error);
    }
    memset(heads,0,128*sizeof(*heads));size_t total=0;int depth=0;bool done=false;
    for(size_t argument=0;!done;++argument) {
        if(argument>=128) return script_fail(source,location,"Too many macro parameters",error);
        if(argument>=script_macro_word(macro,12)) {
            script_warn(source,location,"Too many macro arguments");
            source->source_failure=true;qa_error_set(error,QA_ERROR_FORMAT,location.offset,"Too many macro arguments");return false;
        }
        uint32_t last=0;bool last_comma=true;
        while(!done) {
            if(!script_raw(source,&token,&found,error)) return false;
            if(!found) return script_fail(source,location,"Unterminated macro arguments",error);
            if(qa_script_token_is(&token.token,",") && depth<=0) {
                if(last_comma) script_warn(source,token.token.location,"Too many commas in macro arguments");
                break;
            }
            last_comma=false;
            if(qa_script_token_is(&token.token,"(")) {
                if(depth==INT_MAX) return script_fail(source,location,"Macro argument nesting overflow",error);
                ++depth;continue;
            }
            if(qa_script_token_is(&token.token,")") && --depth<=0) {
                count=script_macro_word(macro,12);
                if(!count || count>128) return script_fail(source,location,"Macro parameter count exceeds its argument table",error);
                if(!heads[count-1]) script_warn(source,token.token.location,"Too few macro arguments");
                done=true;break;
            }
            if(total++>=source->options.maximum_queued_tokens)
                return script_fail(source,token.token.location,"Macro argument queue exceeds configured limit",error);
            script_token_record *copied;
            if(!script_heap_copy_token(&source->macros,token,&copied,error)) return false;
            uint32_t pointer=copied->pointer;
            if(last) {
                script_token_record *previous=script_heap_token(&source->macros,last);
                if(!script_heap_token_bytes(&source->macros,previous,error)) return false;
                qa_store_u32le(previous->record.bytes+1064,pointer);
            } else heads[argument]=pointer;
            last=pointer;
        }
    }
    return true;
}
static qa_bytes joined(qa_script *source,qa_bytes left,qa_bytes right,size_t left_trim,size_t right_trim,qa_error *error) {
    if(left.size<left_trim || right.size<right_trim || left.size-left_trim>SIZE_MAX-(right.size-right_trim)) {
        script_fail(source,qa_script_position(source),"Invalid merged macro token",error);return (qa_bytes){0};
    }
    size_t length=left.size-left_trim+right.size-right_trim;
    if(length>=source->options.token_limit-1 || length>=1024) {
        script_fail(source,qa_script_position(source),"Merged macro token exceeds token limit",error);return (qa_bytes){0};
    }
    uint8_t *text=qa_arena_alloc(&source->arena,length+1,1,error);if(!text) return (qa_bytes){0};
    if(left.size!=left_trim) memcpy(text,left.data,left.size-left_trim);
    if(right.size!=right_trim) memcpy(text+left.size-left_trim,right.data+right_trim,right.size-right_trim);
    text[length]=0;return (qa_bytes){text,length};
}
static bool stringize(qa_script *source,uint32_t head,qa_script_location location,script_queued_token *out,qa_error *error) {
    size_t length=2,remaining=source->macros.queue_count;uint32_t pointer=head;
    while(pointer) {
        script_queued_token token;
        if(!chain_value(source,pointer,&remaining,&token,error)) return false;
        size_t size=strlen((const char *)token.bytes);
        if(size>SIZE_MAX-length) return script_fail(source,location,"Macro string size overflow",error);
        length+=size;if(!next(source,pointer,&pointer,error)) return false;
    }
    if(length>=source->options.token_limit) return script_fail(source,location,"Stringized macro exceeds token limit",error);
    if(length>=1024) {qa_error_set(error,QA_ERROR_UNSUPPORTED,location.offset,"Stringized token does not fit source token_t");return false;}
    char *text=qa_arena_alloc(&source->arena,length+1,1,error);if(!text) return false;
    size_t at=1;text[0]='"';pointer=head;remaining=source->macros.queue_count;
    while(pointer) {
        script_queued_token token;
        if(!chain_value(source,pointer,&remaining,&token,error)) return false;
        size_t size=strlen((const char *)token.bytes);
        memcpy(text+at,token.bytes,size);at+=size;
        if(!next(source,pointer,&pointer,error)) return false;
    }
    text[at++]='"';text[at]=0;
    /* localToken starts with physical zero bytes. Stringizing writes only the
     * string and type; its remaining fields have no supported public profile. */
    memset(out->bytes,0,SCRIPT_TOKEN_BYTES);memcpy(out->bytes,text,length);
    qa_store_u32le(out->bytes+1024,QA_SCRIPT_STRING);out->raw=true;
    out->unsupported="macro stringizing leaves subtype and numeric fields uninitialized";
    location.line=0;location.offset=0;
    return script_token_load(out->bytes,SIZE_MAX,location,(qa_bytes){0},&source->arena,&out->token,error);
}
static bool builtin(qa_script *s, script_macro *macro, script_queued_token invocation,
                    token_chain *chain,qa_error *e) {
    script_token_record *copied;
    if(!script_heap_copy_token(&s->macros,invocation,&copied,e)) return false;
    uint32_t pointer=copied->pointer;
    qa_script_token token = invocation.token;
    char number[32];
    const char *value = NULL;
    switch (script_macro_word(macro,8)) {
    case 1:
        if (!qa_format_number(token.location.line, number, e))
            return false;
        value = number;
        token.kind = QA_SCRIPT_NUMBER;
        token.subtype = QA_SCRIPT_DECIMAL | QA_SCRIPT_INTEGER;
        memcpy(&token.integer, &token.location.line, sizeof(token.integer));
        token.number = token.location.line;
        break;
    case 2:
        value = qa_script_position(s).path;
        if (value == NULL)
            value = "";
        token.kind = QA_SCRIPT_NAME;
        break;
    case 3:
    case 4: {
        const char *part = script_macro_word(macro,8) == 3 ? s->services.date : s->services.time;
        if (part == NULL)
            return script_fail(s, token.location,
                               "Date/time builtin requires a captured source date/time", e);
        size_t length = strlen(part);
        if (length > SIZE_MAX - 3 || length + 3 >= s->options.token_limit)
            return script_fail(s, token.location, "Date/time builtin exceeds token limit", e);
        char *quoted = qa_arena_alloc(&s->arena, length + 3, 1, e);
        if (quoted == NULL)
            return false;
        quoted[0] = '"';
        memcpy(quoted + 1, part, length);
        quoted[length + 1] = '"';
        quoted[length + 2] = 0;
        token.kind = QA_SCRIPT_NAME;
        value = quoted;
        break;
    }
    default: return true;
    }
    size_t size = strlen(value);
    if (size >= s->options.token_limit)
        return script_fail(s, token.location, "Builtin exceeds token limit", e);
    char *text = script_string(&s->arena, value, size, e);
    if (text == NULL)
        return false;
    token.text = (qa_bytes){(const uint8_t *)text, size};
    if (token.kind == QA_SCRIPT_NAME)
        token.subtype = (uint32_t)size;
    copied=script_heap_token(&s->macros,pointer);
    if(!script_heap_token_bytes(&s->macros,copied,e)) return false;
    memcpy(copied->record.bytes,token.text.data,token.text.size);copied->record.bytes[token.text.size]=0;copied->extent=SIZE_MAX;
    qa_store_u32le(copied->record.bytes+1024,(uint32_t)token.kind);qa_store_u32le(copied->record.bytes+1028,token.subtype);
    if(script_macro_word(macro,8)==1) {
        qa_store_u32le(copied->record.bytes+1032,(uint32_t)token.integer);
        script_token_float(copied->record.bytes+1036,token.number);
    }
    *chain=(token_chain){pointer,pointer,1};return true;
}
bool script_expand(qa_script *source,script_queued_token invocation,script_macro *macro,qa_error *error) {
    if(!script_macro_bind(macro,error)) return false;
    source->empty_expansion=false;
    if(source->expansions>=source->options.maximum_expansions)
        return script_fail(source,invocation.token.location,"Macro expansion count exceeds configured limit",error);
    ++source->expansions;
    for(const script_expansion *parent=invocation.expansion;parent;parent=parent->parent)
        if(parent->macro==macro->pointer) return script_fail(source,invocation.token.location,"Recursive macro expansion",error);
    script_expansion *expansion=qa_arena_alloc(&source->arena,sizeof(*expansion),_Alignof(script_expansion),error);
    if(!expansion) return false;
    *expansion=(script_expansion){macro->pointer,invocation.expansion};invocation.expansion=expansion;
    token_chain expanded={0};uint32_t heads[128]={0};
    if(script_macro_word(macro,8)) {
        if(!builtin(source,macro,invocation,&expanded,error)) return false;
    } else {
        if(script_macro_word(macro,12) && !arguments(source,macro,invocation.token.location,heads,error)) return false;
        uint32_t pointer=script_macro_word(macro,20);size_t remaining=source->macros.queue_count;
        while(pointer) {
            script_queued_token defined;
            if(!chain_value(source,pointer,&remaining,&defined,error)) return false;
            int index=parameter(source,macro,&defined.token,error);if(index==-2) return false;
            if(index>=0) {
                if(index>=128 || (uint32_t)index>=script_macro_word(macro,12)) return script_fail(source,invocation.token.location,"Macro parameter exceeds argument table",error);
                uint32_t argument=heads[index];size_t available=source->macros.queue_count;
                while(argument) {
                    script_queued_token token;
                    if(!chain_value(source,argument,&available,&token,error)) return false;
                    token.expansion=expansion;
                    if(!append(source,&expanded,token,invocation.token.location,error) || !next(source,argument,&argument,error)) return false;
                }
            } else {
                script_queued_token copied=defined;copied.expansion=expansion;
                if(qa_script_token_is(&defined.token,"#")) {
                    uint32_t following;
                    if(!next(source,pointer,&following,error)) return false;
                    script_queued_token parameter_token={0};
                    if(following && !value(source,following,&parameter_token,error)) return false;
                    index=following?parameter(source,macro,&parameter_token.token,error):-1;if(index==-2) return false;
                    if(index<0) {
                        script_warn(source,invocation.token.location,"Stringizing operator without macro parameter");
                        if(!next(source,pointer,&pointer,error)) return false;
                        continue;
                    }
                    pointer=following;copied=(script_queued_token){.expansion=expansion};
                    if(index>=128 || (uint32_t)index>=script_macro_word(macro,12) || !stringize(source,heads[index],invocation.token.location,&copied,error)) return false;
                }
                if(!append(source,&expanded,copied,invocation.token.location,error)) return false;
            }
            if(!next(source,pointer,&pointer,error)) return false;
        }
        pointer=expanded.first;
        while(pointer) {
            script_queued_token first,merge,second;uint32_t operator_pointer,second_pointer;
            if(!value(source,pointer,&first,error) || !next(source,pointer,&operator_pointer,error)) return false;
            if(operator_pointer) {
                if(!value(source,operator_pointer,&merge,error) || !next(source,operator_pointer,&second_pointer,error)) return false;
                if(merge.token.text.size>=2 && merge.token.text.data[0]=='#' && merge.token.text.data[1]=='#' && second_pointer) {
                    if(!value(source,second_pointer,&second,error)) return false;
                    bool names=first.token.kind==QA_SCRIPT_NAME && (second.token.kind==QA_SCRIPT_NAME || second.token.kind==QA_SCRIPT_NUMBER);
                    bool strings=first.token.kind==QA_SCRIPT_STRING && second.token.kind==QA_SCRIPT_STRING;
                    if(!names && !strings) return script_fail(source,invocation.token.location,"Invalid macro token merge",error);
                    qa_bytes text=joined(source,first.token.text,second.token.text,strings?1:0,strings?1:0,error);
                    if(!text.data) return false;
                    script_token_record *left=script_heap_token(&source->macros,pointer);
                    if(!script_heap_token_bytes(&source->macros,left,error)) return false;
                    memcpy(left->record.bytes,text.data,text.size);left->record.bytes[text.size]=0;left->extent=SIZE_MAX;
                    if(!script_heap_free_token(&source->macros,script_heap_token(&source->macros,operator_pointer),error)) return false;
                    uint32_t after;
                    if(!next(source,second_pointer,&after,error)) return false;
                    qa_store_u32le(left->record.bytes+1064,after);
                    if(second_pointer==expanded.last) expanded.last=pointer;
                    if(!script_heap_free_token(&source->macros,script_heap_token(&source->macros,second_pointer),error)) return false;
                    continue;
                }
            }
            pointer=operator_pointer;
        }
        for(uint32_t index=0;index<script_macro_word(macro,12);++index) {
            if(index>=128 || (uint32_t)index>=script_macro_word(macro,12)) return script_fail(source,invocation.token.location,"Macro parameter exceeds argument table",error);
            if(!script_heap_free_chain(&source->macros,heads[index],error)) return false;
        }
    }
    source->empty_expansion=!expanded.first || !expanded.last;
    if(!source->empty_expansion) {
        script_token_record *last=script_heap_token(&source->macros,expanded.last);
        if(!script_heap_token_bytes(&source->macros,last,error)) return false;
        qa_store_u32le(last->record.bytes+1064,qa_load_u32le(source->source_record.bytes+SCRIPT_SOURCE_TOKENS));
        qa_store_u32le(source->source_record.bytes+SCRIPT_SOURCE_TOKENS,expanded.first);
    }
    return true;
}
