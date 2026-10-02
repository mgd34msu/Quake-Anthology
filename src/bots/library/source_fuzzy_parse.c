#include "internal.h"
#include "source_fuzzy_parse.h"
#include "source_fuzzy_operations.h"
#include <stdio.h>

typedef struct fuzzy_parser {
    qa_script *source;
    bot_fuzzy_heap *heap;
    bool language_failure;
    const bot_fuzzy_parser_host *host;
    uint32_t column;
} fuzzy_parser;
typedef struct fuzzy_switch {
    int32_t inventory;
    uint32_t first,last;
    bool has_default,free_on_error,body_braced;
    unsigned stage;
    qa_script_location location;
} fuzzy_switch;
static bool diagnostic(fuzzy_parser *parser,qa_script_severity severity,const char *message,qa_error *error) {
    qa_script_diagnostic value={severity,qa_script_position(parser->source),message};
    value.location.column=parser->column;
    return parser->host->report(parser->host->context,&value,error);
}
static bool fail(fuzzy_parser *parser,const char *message,qa_error *error) {
    if(!diagnostic(parser,QA_SCRIPT_ERROR,message,error)) {parser->language_failure=false;return false;}
    parser->language_failure=true;
    return bot_fail(parser->source,message,error);
}
static bool token_failure(fuzzy_parser *parser,const char *prefix,const qa_script_token *token,
    const char *suffix,qa_error *error) {
    size_t first=strlen(prefix),last=strlen(suffix);
    if(token->text.size>SIZE_MAX-first-last-1) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Fuzzy diagnostic exceeds native storage");return false;
    }
    size_t size=first+token->text.size+last;
    char *message=malloc(size+1);
    if(!message) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source fuzzy diagnostic");return false;}
    memcpy(message,prefix,first);
    if(token->text.size) memcpy(message+first,token->text.data,token->text.size);
    memcpy(message+first+token->text.size,suffix,last+1);
    bool ok=fail(parser,message,error);free(message);return ok;
}
static bool invalid(fuzzy_parser *parser,const qa_script_token *token,qa_error *error) {
    return token_failure(parser,"invalid name ",token,"\n",error);
}
static bool next(fuzzy_parser *parser,qa_script_token *token,bool *found,qa_error *error) {
    if(!parser->host->current(parser->host->context,error)) return false;
    qa_error local={0};
    bool ok=qa_script_next(parser->source,token,found,&local);
    if(!parser->host->current(parser->host->context,error)) return false;
    parser->column=ok && *found?token->location.column:1;
    if(ok) return true;
    if(qa_script_source_failure(parser->source)) {*found=false;return true;}
    if(error) *error=local;
    return false;
}
static bool any(fuzzy_parser *parser,qa_script_token *token,qa_error *error) {
    bool found;
    return next(parser,token,&found,error) && (found || fail(parser,"couldn't read expected token",error));
}
static bool expect(fuzzy_parser *parser,const char *text,qa_error *error) {
    qa_script_token token;bool found;
    if(!next(parser,&token,&found,error)) return false;
    if(found && qa_script_token_is(&token,text)) return true;
    char message[256];
    if(!found) {
        (void)snprintf(message,sizeof(message),"couldn't find expected %s",text);
        return fail(parser,message,error);
    }
    (void)snprintf(message,sizeof(message),"expected %s, found ",text);
    return token_failure(parser,message,&token,"",error);
}
static bool integer(fuzzy_parser *parser,int32_t *out,qa_error *error) {
    qa_script_token token;
    if(!any(parser,&token,error)) return false;
    if(token.kind!=QA_SCRIPT_NUMBER) return token_failure(parser,"expected a number, found ",&token,"",error);
    if((token.subtype&QA_SCRIPT_INTEGER)==0) {
        qa_error_set(error,QA_ERROR_ARGUMENT,token.location.offset,"Fuzzy integer token has no integer subtype");return false;
    }
    *out=token.integer;return true;
}
static bool value_from(fuzzy_parser *parser,qa_script_token token,float *out,qa_error *error) {
    if(qa_script_token_is(&token,"-")) {
        if(!diagnostic(parser,QA_SCRIPT_WARNING,"negative value set to zero\n",error)) return false;
        if(!any(parser,&token,error)) return false;
        if(token.kind!=QA_SCRIPT_NUMBER) return token_failure(parser,"expected a number, found ",&token,"",error);
    }
    if(token.kind!=QA_SCRIPT_NUMBER) return token_failure(parser,"invalid return value ",&token,"\n",error);
    if(!isfinite(token.number) || !isfinite((float)token.number))
        return fail(parser,"return value exceeds finite float32 range",error);
    *out=(float)token.number;return true;
}
static bool value(fuzzy_parser *parser,float *out,qa_error *error) {
    qa_script_token token;
    return any(parser,&token,error) && value_from(parser,token,out,error);
}
static bool parse_return(fuzzy_parser *parser,const bot_fuzzy_separator *separator,qa_error *error) {
    qa_script_token token;bool found;
    if(!next(parser,&token,&found,error)) return false;
    bool balanced=found && qa_script_token_is(&token,"balance");
    if(!bot_fuzzy_separator_word_write(separator,BOT_FUZZY_BALANCED,balanced?1:0,error)) return false;
    float weight;
    if(balanced) {
        if(!expect(parser,"(",error) || !value(parser,&weight,error) ||
           !bot_fuzzy_separator_float_write(separator,BOT_FUZZY_WEIGHT,weight,error) ||
           !expect(parser,",",error) || !value(parser,&weight,error) ||
           !bot_fuzzy_separator_float_write(separator,BOT_FUZZY_MINIMUM,weight,error) ||
           !expect(parser,",",error) || !value(parser,&weight,error) ||
           !bot_fuzzy_separator_float_write(separator,BOT_FUZZY_MAXIMUM,weight,error) ||
           !expect(parser,")",error)) return false;
    } else {
        if(!(found?value_from(parser,token,&weight,error):value(parser,&weight,error)) ||
           !bot_fuzzy_separator_float_write(separator,BOT_FUZZY_WEIGHT,weight,error) ||
           !bot_fuzzy_separator_float_read(separator,BOT_FUZZY_WEIGHT,&weight,error) ||
           !bot_fuzzy_separator_float_write(separator,BOT_FUZZY_MINIMUM,weight,error) ||
           !bot_fuzzy_separator_float_read(separator,BOT_FUZZY_WEIGHT,&weight,error) ||
           !bot_fuzzy_separator_float_write(separator,BOT_FUZZY_MAXIMUM,weight,error)) return false;
    }
    return expect(parser,";",error);
}
static bool push_switch(fuzzy_parser *parser,fuzzy_switch **stack,size_t *count,size_t *capacity,
    qa_script_location location,qa_error *error) {
    int32_t inventory;
    if(!expect(parser,"(",error) || !integer(parser,&inventory,error) ||
       !expect(parser,")",error) || !expect(parser,"{",error) ||
       !bot_grow((void **)stack,capacity,*count+1,sizeof(**stack),error)) return false;
    (*stack)[(*count)++]=(fuzzy_switch){.inventory=inventory,.free_on_error=true,.location=location};return true;
}
static bool link_separator(fuzzy_parser *parser,fuzzy_switch *frame,bot_fuzzy_separator *out,qa_error *error) {
    if(!bot_fuzzy_separator_allocate(parser->heap,out,error) ||
       !bot_fuzzy_separator_word_write(out,BOT_FUZZY_INVENTORY,(uint32_t)frame->inventory,error)) return false;
    if(frame->last) {
        bot_fuzzy_separator previous;
        if(!bot_fuzzy_separator_bind(parser->heap,frame->last,&previous,error) ||
           !bot_fuzzy_separator_word_write(&previous,BOT_FUZZY_NEXT,out->pointer,error)) return false;
    } else frame->first=out->pointer;
    frame->last=out->pointer;return true;
}
static bool parse_switch(fuzzy_parser *parser,qa_script_location location,uint32_t *out,qa_error *error) {
    fuzzy_switch *stack=NULL;size_t count=0,capacity=0;
    bool ok=push_switch(parser,&stack,&count,&capacity,location,error);
    while(ok && count) {
        fuzzy_switch *frame=&stack[count-1];qa_script_token token;
        if(frame->stage==1) {
            frame->stage=2;
            if(frame->body_braced) ok=expect(parser,"}",error);
            continue;
        }
        if(!any(parser,&token,error)) {ok=false;break;}
        if(frame->stage==2 && qa_script_token_is(&token,"}")) {
            if(!frame->has_default) {
                parser->column=frame->location.column;
                if(!diagnostic(parser,QA_SCRIPT_WARNING,"switch without default\n",error)) {ok=false;break;}
                bot_fuzzy_separator extra;
                ok=link_separator(parser,frame,&extra,error) &&
                    bot_fuzzy_separator_word_write(&extra,BOT_FUZZY_THRESHOLD,999999,error);
                if(!ok) break;
            }
            uint32_t first=frame->first;
            --count;
            if(!count) *out=first;
            else {
                bot_fuzzy_separator parent;
                ok=bot_fuzzy_separator_bind(parser->heap,stack[count-1].last,&parent,error) &&
                    bot_fuzzy_separator_word_write(&parent,BOT_FUZZY_CHILD,first,error);
            }
            continue;
        }
        bool is_default=qa_script_token_is(&token,"default");
        if(!is_default && !qa_script_token_is(&token,"case")) {
            frame->free_on_error=false;
            ok=bot_fuzzy_separator_tree_free(parser->heap,frame->first,error);
            if(ok) ok=invalid(parser,&token,error);
            break;
        }
        bot_fuzzy_separator separator;
        if(!link_separator(parser,frame,&separator,error)) {ok=false;break;}
        int32_t threshold;
        if(is_default) {
            if(frame->has_default) {ok=fail(parser,"switch already has a default\n",error);break;}
            threshold=999999;frame->has_default=true;
        } else if(!integer(parser,&threshold,error)) {ok=false;break;}
        ok=bot_fuzzy_separator_word_write(&separator,BOT_FUZZY_THRESHOLD,(uint32_t)threshold,error) &&
            expect(parser,":",error) && any(parser,&token,error);
        if(!ok) break;
        frame->body_braced=qa_script_token_is(&token,"{");
        if(frame->body_braced && !any(parser,&token,error)) {ok=false;break;}
        frame->stage=1;
        if(qa_script_token_is(&token,"return")) ok=parse_return(parser,&separator,error);
        else if(qa_script_token_is(&token,"switch"))
            ok=push_switch(parser,&stack,&count,&capacity,token.location,error);
        else {frame->free_on_error=false;ok=invalid(parser,&token,error);}
    }
    if(!ok && parser->language_failure) {
        for(size_t index=count;index;--index) if(stack[index-1].free_on_error) {
            qa_error cleanup={0};
            if(!bot_fuzzy_separator_tree_free(parser->heap,stack[index-1].first,&cleanup)) {
                if(error) *error=cleanup;
                parser->language_failure=false;break;
            }
        }
    }
    free(stack);return ok;
}
bool bot_fuzzy_parse(qa_bot_library *library,qa_script *source,bot_fuzzy_heap *heap,const char *filename,
    bot_fuzzy_config *out,bool *source_failure,const bot_fuzzy_parser_host *host,qa_error *error) {
    if(!library || !source || !heap || heap->memory!=library->memory || !filename || !out ||
       out->heap || !source_failure || !host || !host->current || !host->report) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Fuzzy parse requires its true library memory and opened source");return false;
    }
    *source_failure=false;fuzzy_parser parser={.source=source,.heap=heap,.host=host,.column=1};
    if(!bot_fuzzy_config_allocate(heap,(qa_bytes){(const uint8_t *)filename,strlen(filename)},out,error)) return false;
    bool ok=true;
    for(;;) {
        qa_script_token token;bool found;
        if(!next(&parser,&token,&found,error)) {ok=false;break;}
        if(!found) break;
        if(!qa_script_token_is(&token,"weight")) {ok=invalid(&parser,&token,error);break;}
        int32_t index;
        if(!bot_fuzzy_config_count(out,&index,error)) {ok=false;break;}
        if(index>=BOT_FUZZY_WEIGHTS) {ok=diagnostic(&parser,QA_SCRIPT_WARNING,"too many fuzzy weights\n",error);break;}
        if(!any(&parser,&token,error)) {ok=false;break;}
        if(token.kind!=QA_SCRIPT_STRING) {ok=token_failure(&parser,"expected a string, found ",&token,"",error);break;}
        uint32_t name,root;
        ok=bot_fuzzy_name_allocate(heap,qa_script_token_value(&token),&name,error) &&
            bot_fuzzy_config_pointer_write(out,index,false,name,error) && any(&parser,&token,error);
        if(!ok) break;
        bool braced=qa_script_token_is(&token,"{");
        if(braced && !any(&parser,&token,error)) {ok=false;break;}
        if(qa_script_token_is(&token,"switch")) ok=parse_switch(&parser,token.location,&root,error);
        else if(qa_script_token_is(&token,"return")) {
            bot_fuzzy_separator separator;
            ok=bot_fuzzy_separator_allocate(heap,&separator,error) &&
                bot_fuzzy_separator_word_write(&separator,BOT_FUZZY_INVENTORY,0,error) &&
                bot_fuzzy_separator_word_write(&separator,BOT_FUZZY_THRESHOLD,999999,error);
            if(ok) {
                root=separator.pointer;ok=parse_return(&parser,&separator,error);
                if(!ok && parser.language_failure) {
                    qa_error cleanup={0};
                    if(!bot_fuzzy_pointer_free(heap,root,BOT_FUZZY_SEPARATOR,&cleanup)) {
                        if(error) *error=cleanup;
                        parser.language_failure=false;
                    }
                }
            }
        } else ok=invalid(&parser,&token,error);
        if(!ok) break;
        ok=bot_fuzzy_config_pointer_write(out,index,true,root,error) &&
            (!braced || expect(&parser,"}",error)) && bot_fuzzy_config_count_write(out,index+1,error);
        if(!ok) break;
    }
    *source_failure=parser.language_failure;return ok;
}
