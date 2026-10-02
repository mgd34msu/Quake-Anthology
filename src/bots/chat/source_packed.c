#include "internal.h"
#include "../save_fields.h"
#include "../memory/internal.h"
#include "qa/bots_allocator_save.h"
#include "qa/text.h"
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

static bool fail(qa_error *error,const char *text) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",text);return false;
}
static uint32_t word(const uint8_t *bytes) {
    return (uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
}
static void put(uint8_t *bytes,uint32_t value) {
    for(uint32_t index=0;index<4;++index) bytes[index]=(uint8_t)(value>>(index*8));
}
static char *copy_text(const char *value,qa_error *error) {
    if(!value) return NULL;
    size_t size=strlen(value)+1;char *copy=malloc(size);
    if(!copy) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining packed chat source service text");return NULL;}
    memcpy(copy,value,size);return copy;
}
static bool current(bot_chat_packed *packed,qa_error *error) {
    if(!packed || qa_bot_memory_disposed(packed->memory))
        return fail(error,"Packed chat source has no live MEMORY owner");
    if(packed->report_failed) {if(error) *error=packed->report_error;return false;}
    if(packed->loading_system && (packed->loading_system->retired ||
       packed->loading_system->revision!=packed->loading_revision)) {
        packed->retired_abort=true;return fail(error,"Packed chat setup retired during its callback");
    }
    return true;
}
static bool print_issue(bot_chat_packed *packed,const qa_script_diagnostic *issue,qa_error *error) {
    if(!current(packed,error)) return false;
    qa_bot_library *library=packed->asset->source_library;
    bool ok=true;
    if(library->log) ok=qa_bot_log_print(library->log,issue->severity,issue->message,error);
    else if(library->options.scripts.diagnostic)
        library->options.scripts.diagnostic(library->options.scripts.context,issue);
    if(!ok) {packed->report_failed=true;packed->report_error=error?*error:(qa_error){0};return false;}
    return current(packed,error);
}
static bool source_read(void *context,const qa_script_include *request,qa_script_resource *out,
    bool *found,qa_error *error) {
    bot_chat_packed *packed=context;
    if(!current(packed,error)) return false;
    bot_chat_initial_acquired *pending=calloc(1,sizeof(*pending));
    if(!pending) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining packed chat source acquisition");return false;}
    const qa_script_services *source=&packed->services;
    bool ok=source->read(source->context,request,out,found,error);
    qa_error reached=error?*error:(qa_error){0};
    if(!ok) packed->service_failed=true;
    bool live=current(packed,error);
    if(!ok && error) *error=reached;
    if(ok && *found && !live) {
        pending->resource=*out;pending->next=packed->pending;packed->pending=pending;
        pending=NULL;*out=(qa_script_resource){0};
    }
    free(pending);
    if(ok && live && !*found && request->kind==QA_SCRIPT_ROOT) packed->missing_root=true;
    return ok && live;
}
static void source_release(void *context,qa_script_resource *resource) {
    bot_chat_packed *packed=context;
    const qa_script_services *source=&packed->services;
    source->release(source->context,resource);
}
static void source_report(void *context,const qa_script_diagnostic *issue) {
    bot_chat_packed *packed=context;qa_error error={0};
    if(!current(packed,&error)) return;
    const qa_script_services *source=&packed->services;
    if(source->diagnostic) source->diagnostic(source->context,issue);
    (void)current(packed,&error);
}
static qa_script_services services(bot_chat_packed *packed) {
    qa_script_services result=packed->services;
    result.context=packed;result.read=source_read;result.release=source_release;result.diagnostic=source_report;
    return result;
}
bool bot_chat_packed_owner(qa_bot_chat_asset *asset,qa_bot_library *library,qa_bot_memory *memory,
    qa_error *error) {
    if(!asset || asset->packed_source || !memory || (library && library->memory!=memory) ||
       (asset->view.kind!=QA_BOT_CHAT_SYNONYMS && asset->view.kind!=QA_BOT_CHAT_RANDOMS))
        return fail(error,"Packed chat requires its actual single source/MEMORY owner");
    bot_chat_packed *packed=calloc(1,sizeof(*packed));
    if(!packed) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining packed chat source owner");return false;}
    if(!qa_bot_memory_retain(memory,error)) {free(packed);return false;}
    packed->memory=memory;packed->asset=asset;
    if(library) {
        packed->services=library->options.scripts;packed->options=library->options.preprocessor;
        qa_script_defines_retain((qa_script_defines *)packed->options.globals);
        if(packed->options.include_path && !(packed->include_path=copy_text(packed->options.include_path,error))) goto failed;
        if(packed->services.date && !(packed->date=copy_text(packed->services.date,error))) goto failed;
        if(packed->services.time && !(packed->time=copy_text(packed->services.time,error))) goto failed;
        packed->options.include_path=packed->include_path;packed->services.date=packed->date;packed->services.time=packed->time;
    }
    asset->source_library=library;asset->packed_source=packed;return true;
failed:
    bot_chat_packed_destroy(packed);return false;
}
void bot_chat_packed_destroy(bot_chat_packed *packed) {
    if(!packed) return;
    qa_script_close(packed->reader);
    while(packed->pending) {
        bot_chat_initial_acquired *row=packed->pending;packed->pending=row->next;
        if(row->owned) {free((void *)row->resource.path);free((void *)row->resource.bytes.data);}
        else source_release(packed,&row->resource);
        free(row);
    }
    for(size_t index=0;packed->groups && index<packed->group_count;++index) free(packed->groups[index].value);
    for(size_t index=0;packed->entries && index<packed->entry_count;++index) free(packed->entries[index].value);
    free(packed->groups);free(packed->entries);
    qa_script_defines_release((qa_script_defines *)packed->options.globals);
    free(packed->include_path);free(packed->date);free(packed->time);
    (void)qa_bot_memory_release(packed->memory,NULL);free(packed);
}
static bool span(const bot_chat_packed *packed,uint32_t offset,size_t extent,qa_bot_memory_span *out,
    qa_error *error) {
    if(!qa_bot_memory_bytes(packed->memory,packed->allocation,out,error)) return false;
    if(offset>out->size || extent>out->size-offset) return fail(error,"Packed chat view exceeds its actual allocation");
    out->data+=offset;out->size=(uint32_t)extent;return true;
}
static bool text(const bot_chat_packed *packed,uint32_t offset,const char **out,qa_error *error) {
    qa_bot_memory_span bytes;
    if(!qa_bot_memory_bytes(packed->memory,packed->allocation,&bytes,error)) return false;
    if(offset>=bytes.size || !memchr(bytes.data+offset,0,bytes.size-offset))
        return fail(error,"Packed chat string lacks its actual allocation terminator");
    *out=(const char *)bytes.data+offset;return true;
}
typedef struct packed_parser {
    bot_chat_packed *packed;
    qa_script_location location;
    uint32_t size;
} packed_parser;
static bool grammar(packed_parser *parser,qa_error *error,const char *format,...) {
    va_list args,copy;va_start(args,format);va_copy(copy,args);
    int length=vsnprintf(NULL,0,format,copy);va_end(copy);
    if(length<0) {va_end(args);return fail(error,"Formatting packed chat source diagnostic");}
    char *message=malloc((size_t)length+1);
    if(!message) {va_end(args);qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining packed chat source diagnostic");return false;}
    (void)vsnprintf(message,(size_t)length+1,format,args);va_end(args);
    qa_script_diagnostic issue={.severity=QA_SCRIPT_ERROR,.location=parser->location,.message=message};
    bool ok=print_issue(parser->packed,&issue,error);
    if(ok) {parser->packed->source_failure=true;qa_error_set(error,QA_ERROR_FORMAT,parser->location.offset,"%s",message);}
    free(message);return false;
}
static bool next(packed_parser *parser,qa_script_token *token,bool *found,qa_error *error) {
    bot_chat_packed *packed=parser->packed;
    if(!current(packed,error)) return false;
    qa_error local={0};bool ok=qa_script_next(packed->reader,token,found,&local);
    if(!ok && !qa_script_source_failure(packed->reader)) {
        packed->service_failed=true;if(error) *error=local;return false;
    }
    if(!current(packed,error)) return false;
    if(!ok) *found=false;
    qa_script_location location=qa_script_position(packed->reader);
    parser->location.path=location.path;parser->location.line=location.line;parser->location.offset=location.offset;
    if(*found) parser->location.column=token->location.column;
    return true;
}
static bool required(packed_parser *parser,qa_script_token *token,qa_error *error) {
    bool found;
    return next(parser,token,&found,error) && (found || grammar(parser,error,"couldn't read expected token"));
}
static bool token_error(packed_parser *parser,const char *prefix,const qa_script_token *token,
    const char *suffix,qa_error *error) {
    if(token->text.size>INT_MAX) return fail(error,"Packed chat token diagnostic exceeds native extent");
    return grammar(parser,error,"%s%.*s%s",prefix,(int)token->text.size,(const char *)token->text.data,suffix);
}
static bool expect(packed_parser *parser,const char *expected,qa_error *error) {
    qa_script_token token;bool found;
    if(!next(parser,&token,&found,error)) return false;
    if(!found) return grammar(parser,error,"couldn't find expected %s",expected);
    if(qa_script_token_is(&token,expected)) return true;
    if(token.text.size>INT_MAX) return fail(error,"Packed chat token diagnostic exceeds native extent");
    return grammar(parser,error,"expected %s, found %.*s",expected,(int)token.text.size,(const char *)token.text.data);
}
static bool check(packed_parser *parser,const char *expected,bool *matched,qa_error *error) {
    qa_script_token token;bool found;
    if(!next(parser,&token,&found,error)) return false;
    *matched=found && qa_script_token_is(&token,expected);
    return !found || *matched || qa_script_unread(parser->packed->reader,&token,error);
}
static qa_bytes string_value(const qa_script_token *token) {
    qa_bytes value=qa_script_token_value(token);
    const uint8_t *end=value.size?memchr(value.data,0,value.size):NULL;
    if(end) value.size=(size_t)(end-value.data);
    return value;
}
static bool string(packed_parser *parser,qa_bytes *out,qa_error *error) {
    qa_script_token token;
    if(!required(parser,&token,error)) return false;
    if(token.kind!=QA_SCRIPT_STRING) return token_error(parser,"expected a string, found ",&token,"",error);
    *out=string_value(&token);return true;
}
static bool reserve(packed_parser *parser,uint32_t extent,uint32_t *out,qa_error *error) {
    if(extent>UINT32_MAX-parser->size) return fail(error,"Packed chat sizing exceeds source allocation extent");
    *out=parser->size;parser->size+=extent;
    if(parser->packed->allocation.owner) {
        qa_bot_memory_span bytes;
        if(!qa_bot_memory_bytes(parser->packed->memory,parser->packed->allocation,&bytes,error)) return false;
        if(parser->size>bytes.size) return fail(error,"Packed chat second pass exceeds first-pass allocation");
    }
    return true;
}
static bool store_word(packed_parser *parser,uint32_t offset,uint32_t value,qa_error *error) {
    if(!parser->packed->allocation.owner) return true;
    qa_bot_memory_span bytes;if(!span(parser->packed,offset,4,&bytes,error)) return false;
    put(bytes.data,value);return true;
}
static bool store_float(packed_parser *parser,uint32_t offset,float value,qa_error *error) {
    uint32_t bits;memcpy(&bits,&value,4);return store_word(parser,offset,bits,error);
}
static bool store_string(packed_parser *parser,qa_bytes value,uint32_t *offset,qa_error *error) {
    if(value.size>=UINT32_MAX) return fail(error,"Packed chat text exceeds source allocation extent");
    if(!reserve(parser,(uint32_t)value.size+1,offset,error)) return false;
    if(!parser->packed->allocation.owner) return true;
    qa_bot_memory_span bytes;if(!span(parser->packed,*offset,value.size+1,&bytes,error)) return false;
    if(value.size) memcpy(bytes.data,value.data,value.size);
    bytes.data[value.size]=0;return true;
}
static bool member(packed_parser *parser,bool group,bot_chat_packed_member value,size_t *index,qa_error *error) {
    bot_chat_packed *packed=parser->packed;
    if(!packed->pass) {*index=0;return true;}
    if(packed->allocation.owner) {value.word=0;value.number=0;value.value=NULL;}
    else if(value.value) {
        value.value=copy_text(value.value,error);
        if(!value.value) return false;
    }
    bool ok=chat_append(group?(void **)&packed->groups:(void **)&packed->entries,
        group?&packed->group_capacity:&packed->entry_capacity,
        group?&packed->group_count:&packed->entry_count,sizeof(value),&value,error);
    if(ok) *index=(group?packed->group_count:packed->entry_count)-1;
    else free(value.value);
    return ok;
}
static bool message(packed_parser *parser,char output[256],size_t *size,qa_error *error) {
    size_t length=0;
    for(;;) {
        qa_script_token token;if(!required(parser,&token,error)) return false;
        qa_bytes value;char number[32],marker=0;
        if(token.kind==QA_SCRIPT_STRING) value=string_value(&token);
        else if(token.kind==QA_SCRIPT_NUMBER && (token.subtype&QA_SCRIPT_INTEGER)) {
            if(length+7>256) return grammar(parser,error,"chat message too long\n");
            if(!qa_format_ecmascript_number(token.integer,number,error)) return false;
            value=(qa_bytes){(const uint8_t *)number,strlen(number)};marker='v';
        } else if(token.kind==QA_SCRIPT_NAME) {
            if(length+7>256) return grammar(parser,error,"chat message too long\n");
            value=token.text;marker='r';
        } else return token_error(parser,"unknown message component ",&token,"\n",error);
        size_t extra=marker?3:0;
        if(value.size>=256-length || extra>=256-length-value.size) {
            if(!marker) return grammar(parser,error,"chat message too long\n");
            return fail(error,"Encoded chat message exceeds the source 256-byte buffer");
        }
        if(marker) {output[length++]=1;output[length++]=marker;}
        if(value.size) memcpy(output+length,value.data,value.size);
        length+=value.size;if(marker) output[length++]=1;
        bool end;if(!check(parser,";",&end,error)) return false;
        if(end) {output[length]=0;*size=length;return true;}
        if(!expect(parser,",",error)) return false;
    }
}
static bool synonyms(packed_parser *parser,qa_error *error) {
    uint32_t contexts[31],context=0;size_t depth=0;
    for(;;) {
        qa_script_token token;bool found;
        if(!next(parser,&token,&found,error)) return false;
        if(!found) break;
        if(token.kind==QA_SCRIPT_NUMBER) {
            if(depth==31) return grammar(parser,error,"more than 32 context levels");
            context|=(uint32_t)token.integer;contexts[depth++]=(uint32_t)token.integer;
            if(!expect(parser,"{",error)) return false;
        } else if(token.kind==QA_SCRIPT_PUNCTUATION) {
            if(qa_script_token_is(&token,"}")) {
                if(!depth) return grammar(parser,error,"too many }");
                context&=~contexts[--depth];
            } else if(qa_script_token_is(&token,"[")) {
                uint32_t offset;size_t group;
                if(!reserve(parser,16,&offset,error) || !store_word(parser,offset,context,error) ||
                   !store_float(parser,offset+4,0,error) || !member(parser,true,
                    (bot_chat_packed_member){.offset=offset,.first=(uint32_t)parser->packed->entry_count,.word=context},&group,error)) return false;
                float total=0;uint32_t count=0;
                for(;;) {
                    qa_bytes value;uint32_t record,at;size_t entry;
                    if(!expect(parser,"(",error) || !string(parser,&value,error)) return false;
                    if(!value.size) return grammar(parser,error,"empty string");
                    if(!reserve(parser,12,&record,error) || !store_string(parser,value,&at,error) ||
                       !expect(parser,",",error) || !required(parser,&token,error)) return false;
                    if(token.kind!=QA_SCRIPT_NUMBER) return token_error(parser,"expected a number, found ",&token,"",error);
                    float weight=(float)token.number;
                    if(!isfinite(weight)) return fail(error,"Chat number exceeds finite source range");
                    if(!expect(parser,")",error)) return false;
                    total=(float)(total+weight);
                    char *fallback=NULL;
                    if(parser->packed->pass && !parser->packed->allocation.owner) {
                        fallback=malloc(value.size+1);
                        if(!fallback) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining allocation-free synonym getter");return false;}
                        memcpy(fallback,value.data,value.size);fallback[value.size]=0;
                    }
                    bool stored=store_float(parser,record+4,weight,error) && store_float(parser,offset+4,total,error) &&
                        member(parser,false,(bot_chat_packed_member){.offset=record,.text=at,.number=weight,.value=fallback},&entry,error);
                    free(fallback);if(!stored) return false;
                    ++count;
                    if(parser->packed->pass) {
                        parser->packed->groups[group].count=count;
                        if(!parser->packed->allocation.owner) parser->packed->groups[group].number=total;
                    }
                    bool end;if(!check(parser,"]",&end,error)) return false;
                    if(end) break;
                    if(!expect(parser,",",error)) return false;
                }
                if(count<2) return grammar(parser,error,"synonym must have at least two entries\n");
            } else return token_error(parser,"unexpected ",&token,"",error);
        }
    }
    qa_script *reader=parser->packed->reader;parser->packed->reader=NULL;qa_script_close(reader);
    if(!current(parser->packed,error)) return false;
    if(depth) return fail(error,"BotLoadSynonyms reports a missing context brace through its freed source");
    return true;
}
static bool randoms(packed_parser *parser,qa_error *error) {
    for(;;) {
        qa_script_token token;bool found;
        if(!next(parser,&token,&found,error)) return false;
        if(!found) return true;
        if(token.kind!=QA_SCRIPT_NAME) return token_error(parser,"unknown random ",&token,"",error);
        uint32_t record,at;size_t group;
        qa_bytes name=string_value(&token);char *fallback=NULL;
        if(parser->packed->pass && !parser->packed->allocation.owner) {
            fallback=malloc(name.size+1);
            if(!fallback) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining allocation-free random list getter");return false;}
            memcpy(fallback,name.data,name.size);fallback[name.size]=0;
        }
        bool stored=reserve(parser,16,&record,error) && store_string(parser,name,&at,error) &&
            store_word(parser,record+4,0,error) && member(parser,true,
            (bot_chat_packed_member){.offset=record,.text=at,.first=(uint32_t)parser->packed->entry_count,.value=fallback},&group,error);
        free(fallback);if(!stored || !expect(parser,"=",error) || !expect(parser,"{",error)) return false;
        uint32_t count=0;
        for(;;) {
            bool end;if(!check(parser,"}",&end,error)) return false;
            if(end) break;
            char value[256];size_t length,entry;uint32_t node;
            if(!message(parser,value,&length,error) || !reserve(parser,8,&node,error) ||
               !store_string(parser,(qa_bytes){(const uint8_t *)value,length},&at,error) ||
               !member(parser,false,(bot_chat_packed_member){.offset=node,.text=at,.value=value},&entry,error) ||
               !store_word(parser,record+4,++count,error)) return false;
            if(parser->packed->pass) parser->packed->groups[group].count=count;
        }
    }
}
bool bot_chat_packed_load(qa_bot_chat_asset *asset,qa_bot_chat_system *system,uint64_t revision,
    bool *source_failure,qa_error *error) {
    bot_chat_packed *packed=asset?asset->packed_source:NULL;
    if(!packed || !asset->source_library || !source_failure || packed->attempted)
        return fail(error,"Packed chat load requires its retained unattempted source owner");
    *source_failure=false;packed->loading_system=system;packed->loading_revision=revision;
    packed->active=packed->attempted=true;bool ok=true;
    qa_script_services source=services(packed);
    for(;ok && packed->pass<2;++packed->pass) {
        if(!current(packed,error)) {ok=false;break;}
        if(packed->pass && packed->size) {
            ok=qa_bot_memory_allocate(packed->memory,packed->size,QA_BOT_MEMORY_HUNK,true,NULL,&packed->allocation,error);
            if(!ok) packed->service_failed=true;
            if(ok) ok=current(packed,error);
        }
        if(ok) ok=qa_script_open(asset->view.path,&source,&packed->options,&packed->reader,error);
        if(!ok) {
            if(packed->missing_root && !packed->service_failed && !packed->report_failed && current(packed,error))
                packed->source_failure=true;
            break;
        }
        if(!current(packed,error)) {ok=false;break;}
        packed_parser parser={.packed=packed,.location={.path=asset->view.path,.line=1,.column=1}};
        ok=asset->view.kind==QA_BOT_CHAT_SYNONYMS?synonyms(&parser,error):randoms(&parser,error);
        packed->size=parser.size;
        if(ok || packed->source_failure) {
            qa_script *reader=packed->reader;packed->reader=NULL;qa_script_close(reader);
            if(!current(packed,error)) {ok=false;packed->source_failure=false;}
        }
    }
    *source_failure=packed->source_failure;
    if(ok) {packed->loaded=true;asset->source_loaded=true;ok=bot_chat_packed_refresh(asset,error);}
    else if(!packed->source_failure && !packed->retired_abort && !packed->service_failed && !packed->report_failed)
        packed->own_failure=true;
    packed->active=false;packed->loading_system=NULL;return ok;
}
bool bot_chat_packed_group(const qa_bot_chat_asset *asset,uint32_t index,qa_bot_chat_synonyms *out,
    qa_error *error) {
    const bot_chat_packed *packed=asset->packed_source;
    if(!packed) {*out=asset->groups[index];return true;}
    if(index>=packed->group_count) return fail(error,"Synonym group has no actual source member");
    const bot_chat_packed_member *member=&packed->groups[index];qa_bot_memory_span bytes;
    if(!packed->allocation.owner) {
        *out=(qa_bot_chat_synonyms){.context=member->word,.total_weight=member->number,.entries={member->first,member->count}};return true;
    }
    if(!span(packed,member->offset,16,&bytes,error)) return false;
    uint32_t bits=word(bytes.data+4);float total;memcpy(&total,&bits,4);
    *out=(qa_bot_chat_synonyms){.context=word(bytes.data),.total_weight=total,.entries={member->first,member->count}};return true;
}
bool bot_chat_packed_entry(const qa_bot_chat_asset *asset,uint32_t index,qa_bot_chat_synonym *out,
    qa_error *error) {
    const bot_chat_packed *packed=asset->packed_source;
    if(!packed) {*out=asset->synonyms[index];return true;}
    if(index>=packed->entry_count) return fail(error,"Synonym entry has no actual source member");
    const bot_chat_packed_member *member=&packed->entries[index];qa_bot_memory_span bytes;
    if(!packed->allocation.owner) {*out=(qa_bot_chat_synonym){.text=member->value,.weight=member->number};return true;}
    if(!span(packed,member->offset,12,&bytes,error) || !text(packed,member->text,&out->text,error)) return false;
    uint32_t bits=word(bytes.data+4);memcpy(&out->weight,&bits,4);return true;
}
bool bot_chat_packed_list(const qa_bot_chat_asset *asset,uint32_t index,const char **name,int32_t *count,
    qa_error *error) {
    const bot_chat_packed *packed=asset->packed_source;
    if(!packed) {*name=asset->lists[index].name;*count=(int32_t)asset->lists[index].messages.count;return true;}
    if(index>=packed->group_count) return fail(error,"Random chat list has no actual source member");
    const bot_chat_packed_member *member=&packed->groups[index];qa_bot_memory_span bytes;
    if(!packed->allocation.owner) {*name=member->value;*count=(int32_t)member->count;return true;}
    if(!span(packed,member->offset,16,&bytes,error) || !text(packed,member->text,name,error)) return false;
    uint32_t bits=word(bytes.data+4);memcpy(count,&bits,4);return true;
}
bool bot_chat_packed_weight(const qa_bot_chat_asset *asset,uint32_t index,float *out,qa_error *error) {
    const bot_chat_packed *packed=asset->packed_source;
    if(!packed) {*out=asset->synonyms[index].weight;return true;}
    if(index>=packed->entry_count) return fail(error,"Synonym weight has no actual source member");
    if(!packed->allocation.owner) {*out=packed->entries[index].number;return true;}
    qa_bot_memory_span bytes;
    if(!span(packed,packed->entries[index].offset+4,4,&bytes,error)) return false;
    uint32_t bits=word(bytes.data);memcpy(out,&bits,4);return true;
}
bool bot_chat_packed_count(const qa_bot_chat_asset *asset,uint32_t index,int32_t *out,qa_error *error) {
    const bot_chat_packed *packed=asset->packed_source;
    if(!packed) {*out=(int32_t)asset->lists[index].messages.count;return true;}
    if(index>=packed->group_count) return fail(error,"Random count has no actual source member");
    if(!packed->allocation.owner) {*out=(int32_t)packed->groups[index].count;return true;}
    qa_bot_memory_span bytes;
    if(!span(packed,packed->groups[index].offset+4,4,&bytes,error)) return false;
    uint32_t bits=word(bytes.data);memcpy(out,&bits,4);return true;
}
bool bot_chat_packed_message(const qa_bot_chat_asset *asset,uint32_t index,const char **out,qa_error *error) {
    const bot_chat_packed *packed=asset->packed_source;
    if(!packed) {*out=asset->messages[index];return true;}
    if(index>=packed->entry_count) return fail(error,"Random message has no actual source member");
    if(!packed->allocation.owner) {*out=packed->entries[index].value;return true;}
    return text(packed,packed->entries[index].text,out,error);
}
bool bot_chat_packed_refresh(qa_bot_chat_asset *asset,qa_error *error) {
    bot_chat_packed *packed=asset->packed_source;
    if(!packed || !packed->loaded) return true;
    if(asset->view.kind==QA_BOT_CHAT_SYNONYMS) {
        if(!bot_grow((void **)&asset->groups,&asset->group_capacity,packed->group_count,sizeof(*asset->groups),error) ||
           !bot_grow((void **)&asset->synonyms,&asset->synonym_capacity,packed->entry_count,sizeof(*asset->synonyms),error)) return false;
        asset->view.group_count=packed->group_count;asset->view.synonym_count=packed->entry_count;
        for(uint32_t index=0;index<packed->group_count;++index)
            if(!bot_chat_packed_group(asset,index,&asset->groups[index],error)) return false;
        for(uint32_t index=0;index<packed->entry_count;++index)
            if(!bot_chat_packed_entry(asset,index,&asset->synonyms[index],error)) return false;
    } else {
        if(!bot_grow((void **)&asset->lists,&asset->list_capacity,packed->group_count,sizeof(*asset->lists),error) ||
           !bot_grow((void **)&asset->messages,&asset->message_capacity,packed->entry_count,sizeof(*asset->messages),error)) return false;
        asset->view.list_count=packed->group_count;asset->view.message_count=packed->entry_count;
        for(uint32_t index=0;index<packed->group_count;++index) {
            const bot_chat_packed_member *member=&packed->groups[index];const char *name;int32_t count;
            if(!bot_chat_packed_list(asset,index,&name,&count,error)) return false;
            asset->lists[index]=(qa_bot_chat_list){.name=name,.messages={member->first,member->count}};
            for(uint32_t entry=0;entry<member->count;++entry)
                if(!bot_chat_packed_message(asset,member->first+entry,
                    &asset->messages[member->first+member->count-1-entry],error)) return false;
        }
    }
    chat_asset_view(asset);return true;
}

static bool reader_fields(qa_source_save_io *io,bot_chat_packed *packed) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=!reading && packed->reader;
    if(!qa_source_save_bool(io,&present) || !present) return !io->failed;
    if(!packed->asset->source_library) return bot_save_fail(io,QA_ERROR_FORMAT,"Retained packed PC requires actual library services");
    qa_script_checkpoint checkpoint={0};qa_buffer bytes={0};size_t extent=0;
    bool ok=reading || (qa_script_capture(packed->reader,&checkpoint,io->error) &&
        qa_script_checkpoint_encode(&checkpoint,&bytes,io->error));
    if(!reading) extent=bytes.size;
    if(ok) ok=qa_source_save_count(io,&extent,SIZE_MAX);
    if(ok && reading) {
        if(io->offset>io->input.size || extent>io->input.size-io->offset)
            ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated packed chat PC");
        else {
            qa_script_services source=services(packed);
            ok=qa_script_checkpoint_decode((qa_bytes){io->input.data+io->offset,extent},&checkpoint,io->error) &&
                qa_script_restore(&source,&checkpoint,&packed->reader,io->error);
            if(ok) io->offset+=extent;
        }
    } else if(ok) ok=qa_source_save_bytes(io,bytes.data,extent);
    qa_script_checkpoint_free(&checkpoint);qa_buffer_free(&bytes);
    if(!ok) io->failed=true;
    return ok;
}
static bool pending_fields(qa_source_save_io *io,bot_chat_packed *packed) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;size_t count=0;
    if(!reading) for(bot_chat_initial_acquired *row=packed->pending;row;row=row->next) ++count;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/9))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated packed chat acquisition list");
    bot_chat_initial_acquired **tail=&packed->pending;
    for(size_t index=0;index<count;++index) {
        if(reading) {
            *tail=calloc(1,sizeof(**tail));
            if(!*tail) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring packed chat acquisition");
            (*tail)->owned=true;
        }
        bot_chat_initial_acquired *row=*tail;
        const char *path=reading?NULL:row->resource.path;
        size_t extent=reading?0:row->resource.bytes.size;
        bool ok=bot_save_text(io,&path) && qa_source_save_count(io,&extent,SIZE_MAX);
        if(reading) row->resource.path=path;
        if(ok && reading && extent) {
            if(io->offset>io->input.size || extent>io->input.size-io->offset)
                ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated packed acquired bytes");
            else {
                uint8_t *bytes=malloc(extent);
                if(!bytes) ok=bot_save_fail(io,QA_ERROR_MEMORY,"Restoring packed acquired bytes");
                else row->resource.bytes=(qa_bytes){bytes,extent};
            }
        }
        if(ok && extent && !row->resource.bytes.data)
            ok=bot_save_fail(io,QA_ERROR_FORMAT,"Packed acquired source has no bytes");
        if(ok) ok=qa_source_save_bytes(io,(void *)row->resource.bytes.data,extent);
        if(!ok) return false;
        tail=&row->next;
    }
    return true;
}
static bool members(qa_source_save_io *io,bot_chat_packed *packed,bool groups) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    bot_chat_packed_member **data=groups?&packed->groups:&packed->entries;
    size_t *count=groups?&packed->group_count:&packed->entry_count;
    size_t *capacity=groups?&packed->group_capacity:&packed->entry_capacity;
    if(!qa_source_save_count(io,count,UINT32_MAX)) return false;
    if(reading && *count) {
        if(io->offset>io->input.size || *count>(io->input.size-io->offset)/16 || *count>SIZE_MAX/sizeof(**data))
            return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated packed chat member table");
        *data=calloc(*count,sizeof(**data));
        if(!*data) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring packed chat source members");
        *capacity=*count;
    }
    for(size_t index=0;index<*count;++index) {
        bot_chat_packed_member *value=&(*data)[index];
        if(!qa_source_save_u32(io,&value->offset) || !qa_source_save_u32(io,&value->text) ||
           !qa_source_save_u32(io,&value->first) || !qa_source_save_u32(io,&value->count)) return false;
        uint32_t bits=0;
        if(!reading) memcpy(&bits,&value->number,4);
        const char *closure=reading?NULL:value->value;
        bool ok=qa_source_save_u32(io,&value->word) && qa_source_save_u32(io,&bits) && bot_save_text(io,&closure);
        if(reading) {memcpy(&value->number,&bits,4);value->value=(char *)closure;}
        if(!ok) return false;
        if(!packed->allocation.owner) {
            if((!groups || packed->asset->view.kind==QA_BOT_CHAT_RANDOMS) && !value->value)
                return bot_save_fail(io,QA_ERROR_FORMAT,"Packed closure has no actual source string");
            continue;
        }
        if(value->word || bits || value->value)
            return bot_save_fail(io,QA_ERROR_FORMAT,"Packed allocation cannot retain a second scalar authority");
        qa_bot_memory_span bytes;
        size_t extent=groups?16:packed->asset->view.kind==QA_BOT_CHAT_SYNONYMS?12:8;
        if(!packed->allocation.owner || !span(packed,value->offset,extent,&bytes,io->error)) {io->failed=true;return false;}
        for(size_t prior=0;prior<index;++prior)
            if((*data)[prior].offset==value->offset)
                return bot_save_fail(io,QA_ERROR_FORMAT,"Duplicate packed chat member offset");
        const char *value_text;
        if((!groups || packed->asset->view.kind==QA_BOT_CHAT_RANDOMS) && !text(packed,value->text,&value_text,io->error)) {
            io->failed=true;return false;
        }
    }
    return true;
}
static bool resource_fields(qa_source_save_io *io,bot_chat_packed *packed) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ,allocated=!reading && packed->allocation.owner;
    uint32_t code=(uint32_t)packed->report_error.code;
    bool ok=qa_source_save_bool(io,&packed->attempted) && qa_source_save_bool(io,&packed->loaded) &&
        qa_source_save_bool(io,&packed->missing_root) && qa_source_save_bool(io,&packed->source_failure) &&
        qa_source_save_bool(io,&packed->own_failure) && qa_source_save_bool(io,&packed->retired_abort) &&
        qa_source_save_bool(io,&packed->service_failed) && qa_source_save_bool(io,&packed->report_failed) &&
        qa_source_save_u32(io,&packed->size) && qa_source_save_u32(io,&packed->pass) && packed->pass<=2 &&
        qa_source_save_u32(io,&code) && code<=QA_ERROR_NOT_FOUND &&
        qa_source_save_count(io,&packed->report_error.offset,SIZE_MAX) &&
        qa_source_save_bytes(io,packed->report_error.message,sizeof(packed->report_error.message)) &&
        memchr(packed->report_error.message,0,sizeof(packed->report_error.message)) && qa_source_save_bool(io,&allocated);
    if(reading) packed->report_error.code=(qa_status)code;
    if(ok && allocated) {
        size_t reference=0;
        ok=(reading || qa_bot_memory_reference(packed->memory,packed->allocation,&reference,io->error)) &&
            qa_source_save_count(io,&reference,SIZE_MAX);
        if(ok && reading) ok=qa_bot_memory_resolve(packed->memory,reference,&packed->allocation,io->error);
        if(ok) {
            bot_memory_record *record=bot_memory_record_get(packed->memory,packed->allocation);
            ok=record && record->kind==QA_BOT_MEMORY_HUNK;
        }
    }
    if(ok) ok=members(io,packed,true) && members(io,packed,false);
    for(size_t index=0;ok && index<packed->group_count;++index) {
        const bot_chat_packed_member *group=&packed->groups[index];
        ok=group->first<=packed->entry_count && group->count<=packed->entry_count-group->first;
        if(ok && packed->loaded && packed->asset->view.kind==QA_BOT_CHAT_SYNONYMS) ok=group->count>=2;
    }
    if(ok) ok=reader_fields(io,packed) && pending_fields(io,packed);
    if(ok && (packed->active || (packed->loaded && (packed->pass!=2 || packed->reader ||
        packed->source_failure || packed->own_failure || packed->retired_abort || packed->service_failed || packed->report_failed)) ||
        (!packed->attempted && (allocated || packed->group_count || packed->entry_count || packed->reader || packed->pending ||
         packed->source_failure || packed->own_failure || packed->retired_abort || packed->service_failed || packed->report_failed)) ||
        (!allocated && (packed->group_count || packed->entry_count) && packed->pass==0))) ok=false;
    if(ok && packed->loaded) ok=bot_chat_packed_refresh(packed->asset,io->error);
    if(!ok && !io->failed) return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid reached packed chat source stage");
    return ok;
}
static bool memory_fields(qa_source_save_io *io,qa_bot_memory *memory) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;qa_buffer bytes={0};size_t extent=0;
    bool ok=reading || qa_bot_memory_capture(memory,&bytes,io->error);
    if(!reading) extent=bytes.size;
    if(ok) ok=qa_source_save_count(io,&extent,SIZE_MAX);
    if(ok && reading) {
        if(io->offset>io->input.size || extent>io->input.size-io->offset)
            ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated standalone packed MEMORY");
        else {
            ok=qa_bot_memory_restore(memory,(qa_bytes){io->input.data+io->offset,extent},io->error);
            if(ok) io->offset+=extent;
        }
    } else if(ok) ok=qa_source_save_bytes(io,bytes.data,extent);
    qa_buffer_free(&bytes);if(!ok) io->failed=true;return ok;
}
bool bot_chat_packed_fields(qa_source_save_io *io,const qa_bot_chat_asset *source,
    qa_bot_library *library,qa_bot_memory *memory,qa_bot_chat_asset **out) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ,standalone=memory==NULL;
    if((reading && (!out || *out)) || (!reading && (!source || !source->packed_source)) ||
       (library && library->memory!=memory)) return bot_save_fail(io,QA_ERROR_ARGUMENT,"Packed chat codec requires actual owners/output");
    uint32_t kind=reading?0:(uint32_t)source->view.kind;
    const char *path=reading?NULL:source->view.path,*name=reading?NULL:source->view.name;
    qa_bot_chat_asset *asset=(qa_bot_chat_asset *)source;
    bool ok=qa_source_save_u32(io,&kind) && (kind==QA_BOT_CHAT_SYNONYMS || kind==QA_BOT_CHAT_RANDOMS) &&
        bot_save_text(io,&path) && path && bot_save_text(io,&name) && name;
    if(ok && reading) ok=chat_asset_allocate((qa_bot_chat_asset_kind)kind,path,name,&asset,io->error);
    if(reading) {free((void *)path);free((void *)name);}
    if(ok && standalone) {
        if(reading) ok=qa_bot_memory_create(NULL,&memory,io->error);
        else memory=source->packed_source->memory;
        if(ok) ok=memory_fields(io,memory);
    }
    if(ok && reading) ok=bot_chat_packed_owner(asset,library,memory,io->error);
    if(ok && !reading && asset->packed_source->memory!=memory) ok=false;
    if(ok) ok=resource_fields(io,asset->packed_source);
    if(reading && standalone && memory) (void)qa_bot_memory_release(memory,NULL);
    if(reading) {
        if(ok) *out=asset;
        else qa_bot_chat_asset_release(asset);
    }
    if(!ok && !io->failed) return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid packed chat continuation");
    return ok;
}
bool bot_chat_packed_copy(const bot_chat_packed *source,const qa_bot_memory_prepared *memory,
    bot_chat_packed **out,qa_error *error) {
    if(!source || source->active || !out || *out) return fail(error,"Packed chat history requires its idle source owner/output");
    bot_chat_packed *target=calloc(1,sizeof(*target));
    if(!target) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining packed chat history");return false;}
    if(!qa_bot_memory_retain(source->memory,error)) {free(target);return false;}
    *target=*source;target->reader=NULL;target->pending=NULL;target->groups=target->entries=NULL;
    target->include_path=target->date=target->time=NULL;
    qa_script_defines_retain((qa_script_defines *)target->options.globals);
    target->loading_system=NULL;bool ok=true;
    if(source->include_path && !(target->include_path=copy_text(source->include_path,error))) ok=false;
    if(source->date && !(target->date=copy_text(source->date,error))) ok=false;
    if(source->time && !(target->time=copy_text(source->time,error))) ok=false;
    target->options.include_path=target->include_path;target->services.date=target->date;target->services.time=target->time;
    if(source->allocation.owner && memory)
        ok=qa_bot_memory_checkpoint_resolve(memory,source->allocation,&target->allocation,error);
    if(ok && source->group_count) {
        target->groups=malloc(source->group_count*sizeof(*target->groups));ok=target->groups!=NULL;
        if(ok) {
            memcpy(target->groups,source->groups,source->group_count*sizeof(*target->groups));
            for(size_t index=0;index<source->group_count;++index) target->groups[index].value=NULL;
            for(size_t index=0;ok && index<source->group_count;++index)
                if(source->groups[index].value && !(target->groups[index].value=copy_text(source->groups[index].value,error))) ok=false;
        }
    }
    if(ok && source->entry_count) {
        target->entries=malloc(source->entry_count*sizeof(*target->entries));ok=target->entries!=NULL;
        if(ok) {
            memcpy(target->entries,source->entries,source->entry_count*sizeof(*target->entries));
            for(size_t index=0;index<source->entry_count;++index) target->entries[index].value=NULL;
            for(size_t index=0;ok && index<source->entry_count;++index)
                if(source->entries[index].value && !(target->entries[index].value=copy_text(source->entries[index].value,error))) ok=false;
        }
    }
    target->group_capacity=target->group_count;target->entry_capacity=target->entry_count;
    if(ok && source->reader) {
        qa_script_checkpoint checkpoint={0};qa_script_services wrapped=services(target);
        ok=qa_script_capture(source->reader,&checkpoint,error) && qa_script_restore(&wrapped,&checkpoint,&target->reader,error);
        qa_script_checkpoint_free(&checkpoint);
    }
    bot_chat_initial_acquired **tail=&target->pending;
    for(const bot_chat_initial_acquired *row=source->pending;ok && row;row=row->next) {
        *tail=calloc(1,sizeof(**tail));
        if(!*tail) {ok=false;break;}
        bot_chat_initial_acquired *item=*tail;item->owned=true;item->resource.bytes.size=row->resource.bytes.size;
        if(row->resource.path) {
            size_t length=strlen(row->resource.path)+1;char *path=malloc(length);
            item->resource.path=path;if(path) memcpy(path,row->resource.path,length);else ok=false;
        }
        if(row->resource.bytes.size) {
            uint8_t *bytes=malloc(row->resource.bytes.size);item->resource.bytes.data=bytes;
            if(!bytes || !row->resource.bytes.data) ok=false;
            else memcpy(bytes,row->resource.bytes.data,row->resource.bytes.size);
        }
        tail=&item->next;
    }
    if(!ok) {
        if(!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining packed chat history aliases");
        bot_chat_packed_destroy(target);return false;
    }
    *out=target;return true;
}
