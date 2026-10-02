#include "source_initial_parse.h"
#include "qa/text.h"
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct initial_parser {
    qa_script *reader;
    const bot_chat_initial_parser_host *host;
    bot_chat_initial *stored;
    qa_script_location location;
    uint32_t size;
    bool source_failure,own_failure;
} initial_parser;
static bool own_fail(initial_parser *parser,qa_error *error,const char *message) {
    parser->own_failure=true;qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static bool grammar(initial_parser *parser,qa_error *error,const char *format,...) {
    va_list args,copy;va_start(args,format);va_copy(copy,args);
    int length=vsnprintf(NULL,0,format,copy);va_end(copy);
    if(length<0) {va_end(args);qa_error_set(error,QA_ERROR_ARGUMENT,0,"Formatting initial chat diagnostic");return false;}
    char *message=malloc((size_t)length+1);
    if(!message) {va_end(args);qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining initial chat source diagnostic");return false;}
    (void)vsnprintf(message,(size_t)length+1,format,args);va_end(args);
    qa_script_diagnostic issue={.severity=QA_SCRIPT_ERROR,.location=parser->location,.message=message};
    bool ok=parser->host->report(parser->host->context,&issue,error) &&
        parser->host->current(parser->host->context,error);
    if(ok) {parser->source_failure=true;qa_error_set(error,QA_ERROR_FORMAT,parser->location.offset,"%s",message);}
    free(message);return false;
}
static bool next(initial_parser *parser,qa_script_token *token,bool *found,qa_error *error) {
    if(!parser->host->current(parser->host->context,error)) return false;
    qa_error local={0};bool ok=qa_script_next(parser->reader,token,found,&local);
    if(!ok && !qa_script_source_failure(parser->reader)) {
        parser->own_failure=true;
        if(error) *error=local;
        return false;
    }
    if(!parser->host->current(parser->host->context,error)) return false;
    if(!ok) {
        *found=false;
    }
    qa_script_location position=qa_script_position(parser->reader);
    parser->location.path=position.path;parser->location.line=position.line;
    parser->location.offset=position.offset;
    if(*found) parser->location.column=token->location.column;
    return true;
}
static bool required(initial_parser *parser,qa_script_token *token,qa_error *error) {
    bool found;
    return next(parser,token,&found,error) && (found || grammar(parser,error,"couldn't read expected token"));
}
static bool unread(initial_parser *parser,const qa_script_token *token,qa_error *error) {
    return qa_script_unread(parser->reader,token,error);
}
static bool check(initial_parser *parser,const char *text,bool *matched,qa_error *error) {
    qa_script_token token;bool found;
    if(!next(parser,&token,&found,error)) return false;
    *matched=found && qa_script_token_is(&token,text);
    return !found || *matched || unread(parser,&token,error);
}
static bool token_error(initial_parser *parser,const char *prefix,const qa_script_token *token,
    bool newline,qa_error *error) {
    if(token->text.size>INT_MAX) return own_fail(parser,error,"Initial chat diagnostic exceeds native extent");
    return grammar(parser,error,"%s%.*s%s",prefix,(int)token->text.size,
        (const char *)token->text.data,newline?"\n":"");
}
static bool expect(initial_parser *parser,const char *text,qa_error *error) {
    qa_script_token token;bool found;
    if(!next(parser,&token,&found,error)) return false;
    if(!found) return grammar(parser,error,"couldn't find expected %s",text);
    if(qa_script_token_is(&token,text)) return true;
    if(token.text.size>INT_MAX) return own_fail(parser,error,"Initial chat diagnostic exceeds native extent");
    return grammar(parser,error,"expected %s, found %.*s",text,(int)token.text.size,(const char *)token.text.data);
}
static qa_bytes string_value(const qa_script_token *token) {
    qa_bytes text=qa_script_token_value(token);
    const uint8_t *end=memchr(text.data,0,text.size);
    if(end) text.size=(size_t)(end-text.data);
    return text;
}
static bool string(initial_parser *parser,qa_bytes *out,qa_error *error) {
    qa_script_token token;
    if(!required(parser,&token,error)) return false;
    if(token.kind!=QA_SCRIPT_STRING) return token_error(parser,"expected a string, found ",&token,false,error);
    *out=string_value(&token);return true;
}
static bool reserve(initial_parser *parser,uint32_t size,uint32_t *offset,qa_error *error) {
    if(size>UINT32_MAX-parser->size) return own_fail(parser,error,"Initial chat sizing exceeds source allocation extent");
    uint32_t previous=parser->size;parser->size+=size;
    if(parser->stored) {
        qa_bot_memory_span span;
        if(!qa_bot_memory_bytes(parser->stored->memory,parser->stored->allocation,&span,error)) return false;
        if(parser->size>span.size) return own_fail(parser,error,"Initial chat second pass exceeds first-pass allocation");
    }
    *offset=previous;return true;
}
static bool message(initial_parser *parser,char out[256],size_t *size,qa_error *error) {
    size_t length=0;
    for(;;) {
        qa_script_token token;
        if(!required(parser,&token,error)) return false;
        qa_bytes text;char number[32];char escape=0;
        if(token.kind==QA_SCRIPT_STRING) text=string_value(&token);
        else if(token.kind==QA_SCRIPT_NUMBER && (token.subtype&QA_SCRIPT_INTEGER)) {
            if(length+7>256) return grammar(parser,error,"chat message too long\n");
            if(!qa_format_number(token.integer,number,error)) {parser->own_failure=true;return false;}
            text=(qa_bytes){(const uint8_t *)number,strlen(number)};escape='v';
        } else if(token.kind==QA_SCRIPT_NAME) {
            if(length+7>256) return grammar(parser,error,"chat message too long\n");
            text=token.text;escape='r';
        } else return token_error(parser,"unknown message component ",&token,true,error);
        size_t extra=escape?3:0;
        if(text.size>=256-length || extra>=256-length-text.size) {
            if(token.kind==QA_SCRIPT_STRING) return grammar(parser,error,"chat message too long\n");
            return own_fail(parser,error,"Encoded chat message exceeds the source 256-byte buffer");
        }
        if(escape) {out[length++]=1;out[length++]=escape;}
        if(text.size) memcpy(out+length,text.data,text.size);
        length+=text.size;if(escape) out[length++]=1;
        bool end;if(!check(parser,";",&end,error)) return false;
        if(end) {out[length]=0;*size=length;return true;}
        if(!expect(parser,",",error)) return false;
    }
}
static bool parse(initial_parser *parser,bool previously_found,bool *not_found,qa_error *error) {
    uint32_t ignored;
    if(!reserve(parser,4,&ignored,error)) return false;
    bool selected=previously_found;
    for(;;) {
        qa_script_token token;bool found;
        if(!next(parser,&token,&found,error)) return false;
        if(!found) break;
        if(!qa_script_token_is(&token,"chat")) return token_error(parser,"unknown definition ",&token,true,error);
        qa_bytes name;
        if(!string(parser,&name,error) || !expect(parser,"{",error)) return false;
        bool same;
        if(!parser->host->name_equal(parser->host->context,name,&same,error) ||
           !parser->host->current(parser->host->context,error)) return false;
        if(!same) {
            uint32_t depth=1;
            while(depth) {
                if(!required(parser,&token,error)) return false;
                if(qa_script_token_is(&token,"{")) {
                    if(depth==UINT32_MAX) return own_fail(parser,error,"Initial chat nesting exceeds source range");
                    ++depth;
                } else if(qa_script_token_is(&token,"}")) --depth;
            }
            continue;
        }
        selected=true;
        for(;;) {
            if(!required(parser,&token,error)) return false;
            if(qa_script_token_is(&token,"}")) break;
            if(!qa_script_token_is(&token,"type")) return token_error(parser,"expected type found ",&token,true,error);
            qa_bytes type_name;uint32_t offset,type=0;
            if(!string(parser,&type_name,error) || !expect(parser,"{",error) ||
               !reserve(parser,BOT_CHAT_TYPE_BYTES,&offset,error)) return false;
            if(parser->stored && !bot_chat_initial_type_add(parser->stored,offset,type_name,&type,error)) {
                parser->own_failure=true;return false;
            }
            for(;;) {
                bool end;
                if(!check(parser,"}",&end,error)) return false;
                if(end) break;
                char text[256];size_t length;uint32_t record;
                if(!message(parser,text,&length,error) || !reserve(parser,BOT_CHAT_MESSAGE_BYTES,&offset,error)) return false;
                if(parser->stored && !bot_chat_initial_message_add(parser->stored,type,offset,&record,error)) {
                    parser->own_failure=true;return false;
                }
                if(!reserve(parser,(uint32_t)length+1,&offset,error)) return false;
                if(parser->stored && !bot_chat_initial_message_write(parser->stored,type,offset,
                    (qa_bytes){(const uint8_t *)text,length},error)) {parser->own_failure=true;return false;}
            }
        }
    }
    if(!parser->host->complete(parser->host->context,error) ||
       !parser->host->current(parser->host->context,error)) return false;
    *not_found=!selected;return true;
}
bool bot_chat_initial_parse(qa_script *reader,const char *path,
    const bot_chat_initial_parser_host *host,bot_chat_initial *stored,bool previously_found,
    uint32_t *size,bool *source_failure,bool *own_failure,bool *not_found,qa_error *error) {
    if(!reader || !path || !host || !host->current || !host->report || !host->name_equal ||
       !host->complete || !size || !source_failure || !own_failure || !not_found) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat parser requires its actual reader/callback owners");return false;
    }
    *source_failure=*own_failure=*not_found=false;
    initial_parser parser={.reader=reader,.host=host,.stored=stored,
        .location={.path=path,.line=1,.column=1}};
    bool ok=parse(&parser,previously_found,not_found,error);
    *size=parser.size;*source_failure=parser.source_failure;*own_failure=parser.own_failure;return ok;
}
