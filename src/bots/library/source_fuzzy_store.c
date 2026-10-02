#include "internal.h"
#include "source_fuzzy_store.h"
#include "source_fuzzy_parse.h"
#include "source_fuzzy_operations.h"
#include "qa/bot_log.h"
#include <stdio.h>

static bool fail(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static bool idle(bot_fuzzy_store *store,qa_error *error) {
    return store && !store->closed && !store->active && store->library &&
        !qa_bot_memory_disposed(store->heap.memory) ? true :
        fail(error,"Fuzzy store is absent, closed or active");
}
static void release_store(bot_fuzzy_store *store) {
    if(store && --store->references==0) {
        (void)bot_fuzzy_heap_clear(&store->heap,NULL);free(store);
    }
}
static void diagnostics_free(bot_fuzzy_diagnostic *reported,size_t count) {
    if(!reported) return;
    for(size_t index=0;index<count;++index) {
        free(reported[index].path);free(reported[index].message);
    }
    free(reported);
}
void bot_fuzzy_owned_retain(bot_fuzzy_owned *config) {if(config) ++config->references;}
void bot_fuzzy_owned_release(bot_fuzzy_owned *config) {
    if(config && --config->references==0) {
        bot_fuzzy_store *store=config->store;
        diagnostics_free(config->reported,config->reported_count);
        free(config->path);free(config);release_store(store);
    }
}
bool bot_fuzzy_owned_open(const bot_fuzzy_owned *config,qa_error *error) {
    if(!config || config->disposed) return fail(error,"Fuzzy configuration has been freed");
    int32_t count;return bot_fuzzy_config_count(&config->source,&count,error);
}
bool bot_fuzzy_store_create(qa_bot_library *library,bot_fuzzy_store **out,qa_error *error) {
    if(!library || !out || *out) return fail(error,"Fuzzy store construction requires its actual library and empty output");
    bot_fuzzy_store *store=calloc(1,sizeof(*store));
    if(!store) {qa_error_set(error,QA_ERROR_MEMORY,0,"Constructing source fuzzy store");return false;}
    if(!bot_fuzzy_heap_bind(&store->heap,library->memory,error)) {free(store);return false;}
    store->references=1;store->library=library;*out=store;return true;
}
static bool current(const bot_fuzzy_store *store,uint64_t generation,qa_error *error) {
    return !store->closed && generation==store->generation ? true :
        fail(error,"Fuzzy source owner changed during its synchronous operation");
}
static bool print_path(bot_fuzzy_store *store,qa_script_severity severity,const char *prefix,
    const char *path,const char *suffix,qa_error *error) {
    size_t first=strlen(prefix),size=strlen(path),last=strlen(suffix);
    if(size>SIZE_MAX-first-last-1) return fail(error,"Fuzzy source message exceeds native storage");
    char *message=malloc(first+size+last+1);
    if(!message) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source fuzzy message");return false;}
    memcpy(message,prefix,first);memcpy(message+first,path,size);memcpy(message+first+size,suffix,last+1);
    qa_bot_log *log=qa_bot_library_log(store->library);
    bool ok=!log || qa_bot_log_print(log,severity,message,error);
    free(message);return ok;
}
static bool reader_current(void *context,qa_error *error) {
    bot_fuzzy_reader *reader=context;
    if(reader->report_failed) {if(error) *error=reader->report_error;return false;}
    return current(reader->store,reader->generation,error);
}
static bool diagnostic_retain(bot_fuzzy_reader *reader,const qa_script_diagnostic *diagnostic,
    qa_error *error) {
    if(!diagnostic || !diagnostic->location.path || !diagnostic->message)
        return fail(error,"Fuzzy diagnostic requires actual source path and message");
    if(!bot_grow((void **)&reader->reported,&reader->reported_capacity,
        reader->reported_count+1,sizeof(*reader->reported),error)) return false;
    size_t path_size=strlen(diagnostic->location.path),message_size=strlen(diagnostic->message);
    char *path=malloc(path_size+1),*message=malloc(message_size+1);
    if(!path || !message) {
        free(path);free(message);qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source fuzzy diagnostic");return false;
    }
    memcpy(path,diagnostic->location.path,path_size+1);memcpy(message,diagnostic->message,message_size+1);
    bot_fuzzy_diagnostic *row=&reader->reported[reader->reported_count++];
    *row=(bot_fuzzy_diagnostic){.value=*diagnostic,.path=path,.message=message};
    row->value.location.path=path;row->value.message=message;return true;
}
static bool diagnostic_print(bot_fuzzy_reader *reader,const qa_script_diagnostic *diagnostic,
    qa_error *error) {
    if(!reader_current(reader,error)) return false;
    char line[32];(void)snprintf(line,sizeof(line),", line %u: ",diagnostic->location.line);
    size_t path_size=strlen(diagnostic->location.path),message_size=strlen(diagnostic->message),line_size=strlen(line);
    if(message_size>SIZE_MAX-line_size-7 || path_size>SIZE_MAX-message_size-line_size-7)
        return fail(error,"Fuzzy diagnostic exceeds native storage");
    size_t size=5+path_size+line_size+message_size+1;
    char *message=malloc(size+1);
    if(!message) {qa_error_set(error,QA_ERROR_MEMORY,0,"Formatting source fuzzy diagnostic");return false;}
    memcpy(message,"file ",5);memcpy(message+5,diagnostic->location.path,path_size);
    memcpy(message+5+path_size,line,line_size);memcpy(message+5+path_size+line_size,diagnostic->message,message_size);
    message[size-1]='\n';message[size]='\0';
    qa_bot_log *log=qa_bot_library_log(reader->store->library);
    bool ok=!log || qa_bot_log_print(log,diagnostic->severity==QA_SCRIPT_WARNING?QA_SCRIPT_WARNING:QA_SCRIPT_ERROR,message,error);
    free(message);return ok && reader_current(reader,error);
}
static bool reader_report(void *context,const qa_script_diagnostic *diagnostic,qa_error *error) {
    bot_fuzzy_reader *reader=context;
    return diagnostic_retain(reader,diagnostic,error) && diagnostic_print(reader,diagnostic,error);
}
static void source_diagnostic(void *context,const qa_script_diagnostic *diagnostic) {
    bot_fuzzy_reader *reader=context;qa_error error={0};
    if(reader->report_failed) return;
    bool ok=diagnostic_retain(reader,diagnostic,&error);
    if(ok && reader->services.diagnostic) reader->services.diagnostic(reader->services.context,diagnostic);
    if(ok) ok=diagnostic_print(reader,diagnostic,&error);
    if(!ok) {reader->report_failed=true;reader->report_error=error;}
}
static bool source_read(void *context,const qa_script_include *request,qa_script_resource *resource,
    bool *found,qa_error *error) {
    bot_fuzzy_reader *reader=context;
    if(!reader_current(reader,error)) return false;
    bool ok=reader->services.read(reader->services.context,request,resource,found,error);
    bool qualified=reader_current(reader,error);
    if(ok && qualified && !*found && request->kind==QA_SCRIPT_ROOT) reader->missing_root=true;
    return qualified && ok;
}
static void source_release(void *context,qa_script_resource *resource) {
    bot_fuzzy_reader *reader=context;
    if(reader->services.release) reader->services.release(reader->services.context,resource);
}
bool bot_fuzzy_reader_create(bot_fuzzy_store *store,uint64_t generation,bot_fuzzy_reader **out,qa_error *error) {
    if(!store || !store->library || !out || *out) return fail(error,"Fuzzy reader requires its actual store and empty output");
    bot_fuzzy_reader *reader=calloc(1,sizeof(*reader));
    if(!reader) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining fuzzy source owner");return false;}
    reader->store=store;reader->services=store->library->options.scripts;
    reader->generation=generation;reader->next=store->readers;store->readers=reader;*out=reader;return true;
}
qa_script_services bot_fuzzy_reader_services(bot_fuzzy_reader *reader) {
    qa_script_services services=reader->services;
    services.context=reader;services.read=source_read;services.release=source_release;services.diagnostic=source_diagnostic;
    return services;
}
static void close_reader(bot_fuzzy_store *store,bot_fuzzy_reader *reader) {
    bot_fuzzy_reader **link=&store->readers;
    while(*link && *link!=reader) link=&(*link)->next;
    if(*link) *link=reader->next;
    qa_script_close(reader->source);diagnostics_free(reader->reported,reader->reported_count);free(reader);
}
static void unlink_owned(bot_fuzzy_store *store,bot_fuzzy_owned *config) {
    bot_fuzzy_owned **link=&store->first,*previous=NULL;
    while(*link && *link!=config) {previous=*link;link=&(*link)->next;}
    if(*link) {
        *link=config->next;if(store->last==config) store->last=previous;
        config->next=NULL;config->owned=false;bot_fuzzy_owned_release(config);
    }
}
static bool read_config(bot_fuzzy_store *store,const char *path,size_t available,
    uint64_t generation,bot_fuzzy_owned **out,bool *source_failure,qa_error *error) {
    bot_fuzzy_reader *reader=NULL;
    if(!bot_fuzzy_reader_create(store,generation,&reader,error)) return false;
    qa_script_services services=bot_fuzzy_reader_services(reader);
    bool opened=qa_script_open(path,&services,&store->library->options.preprocessor,&reader->source,error);
    if(!reader_current(reader,error) || !opened) {
        if(reader->missing_root && !reader->report_failed) {
            qa_error print_error={0};
            if(!print_path(store,QA_SCRIPT_ERROR,"counldn't load ",path,"\n",&print_error)) {
                if(error) *error=print_error;
            } else *source_failure=true;
        }
        close_reader(store,reader);
        return false;
    }
    qa_script_location location=qa_script_position(reader->source);
    if(!location.path || !location.path[0]) return fail(error,"Fuzzy resolver returned an empty canonical path");
    size_t size=strlen(location.path);char *canonical=malloc(size+1);
    if(!canonical) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining fuzzy canonical path");return false;}
    memcpy(canonical,location.path,size+1);
    bot_fuzzy_config record={0};bool language_failure=false;
    bot_fuzzy_parser_host host={reader,reader_current,reader_report};
    bool ok=reader_current(reader,error) &&
        bot_fuzzy_parse(store->library,reader->source,&store->heap,path,&record,&language_failure,&host,error);
    if(!ok) {
        if(language_failure) {
            bool reload=bot_reload_characters(store->library);
            if(!current(store,generation,error)) {free(canonical);return false;}
            if(reload) {
                qa_error cleanup={0};
                if(!bot_fuzzy_config_free(&record,&cleanup)) {
                    if(error) *error=cleanup;
                    free(canonical);return false;
                }
            }
            close_reader(store,reader);
            *source_failure=true;
        }
        /* Service/allocator failure leaves the actual opened source and raw
         * record at their reached source stage. Source ownership integration
         * retains that reader until its explicit pure disposal. */
        free(canonical);return false;
    }
    bot_fuzzy_diagnostic *reported=reader->reported;size_t reported_count=reader->reported_count;
    reader->reported=NULL;reader->reported_count=reader->reported_capacity=0;
    close_reader(store,reader);
    if(!current(store,generation,error)) {
        (void)bot_fuzzy_config_free(&record,NULL);diagnostics_free(reported,reported_count);free(canonical);return false;
    }
    bot_fuzzy_owned *config=calloc(1,sizeof(*config));
    if(!config) {diagnostics_free(reported,reported_count);free(canonical);qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source fuzzy configuration identity");return false;}
    *config=(bot_fuzzy_owned){.references=1,.store=store,.source=record,.path=canonical,
        .reported=reported,.reported_count=reported_count,.owned=true};
    ++store->references;
    if(store->last) store->last->next=config;else store->first=config;
    store->last=config;
    if(!print_path(store,QA_SCRIPT_INFO,"loaded ",path,"\n",error) || !current(store,generation,error)) return false;
    bool reload=bot_reload_characters(store->library);
    if(!current(store,generation,error)) return false;
    if(!reload) {
        bot_fuzzy_owned_release(store->cached[available]);
        bot_fuzzy_owned_retain(config);store->cached[available]=config;
        if(store->cached_count<=available) store->cached_count=available+1;
    }
    bot_fuzzy_owned_retain(config);*out=config;return true;
}
bool bot_fuzzy_store_load_result(bot_fuzzy_store *store,const char *path,bot_fuzzy_owned **out,
    bool *source_failure,qa_error *error) {
    if(!source_failure) return fail(error,"Fuzzy load requires its reached source failure output");
    *source_failure=false;
    if(!idle(store,error) || !path || !out) return fail(error,"Fuzzy load requires its true idle owner and source path/output");
    uint64_t generation=store->generation;store->active=true;
    bool reload=bot_reload_characters(store->library),ok=current(store,generation,error);
    size_t available=0;
    if(ok && !reload) {
        available=SIZE_MAX;qa_bytes filename={(const uint8_t *)path,strlen(path)};
        for(size_t index=0;index<128;++index) {
            bot_fuzzy_owned *config=store->cached[index];
            if(!config) {if(available==SIZE_MAX) available=index;continue;}
            bool matches;
            ok=bot_fuzzy_owned_open(config,error) &&
                bot_fuzzy_config_matches_filename(&config->source,filename,&matches,error);
            if(!ok) break;
            if(matches) {bot_fuzzy_owned_retain(config);*out=config;store->active=false;return true;}
        }
        if(ok && available==SIZE_MAX) {
            ok=print_path(store,QA_SCRIPT_ERROR,"weightFileList was full trying to load ",path,"\n",error);
            if(ok) {*source_failure=true;qa_error_set(error,QA_ERROR_FORMAT,0,"Fuzzy source cache is full at 128 entries");ok=false;}
        }
    }
    if(ok) ok=read_config(store,path,available,generation,out,source_failure,error);
    store->active=false;return ok;
}
bool bot_fuzzy_store_load(bot_fuzzy_store *store,const char *path,bot_fuzzy_owned **out,qa_error *error) {
    bool source_failure;return bot_fuzzy_store_load_result(store,path,out,&source_failure,error);
}
bool bot_fuzzy_store_free(bot_fuzzy_store *store,bot_fuzzy_owned *config,qa_error *error) {
    if(!idle(store,error) || !config || config->store!=store || !config->owned)
        return fail(error,"Fuzzy release configuration is not owned by this actual store");
    store->active=true;
    bool reload=bot_reload_characters(store->library),ok=true;
    if(reload) {
        ok=bot_fuzzy_config_free(&config->source,error);
        if(ok) {config->disposed=true;unlink_owned(store,config);}
    }
    store->active=false;return ok;
}
bool bot_fuzzy_store_shutdown(bot_fuzzy_store *store,qa_error *error) {
    if(!idle(store,error)) return false;
    if(store->generation==UINT64_MAX) return fail(error,"Fuzzy source generation is exhausted");
    ++store->generation;store->active=true;bool ok=true;
    for(size_t index=0;ok && index<128;++index) {
        bot_fuzzy_owned *config=store->cached[index];
        if(!config) continue;
        ok=bot_fuzzy_config_free(&config->source,error);
        if(ok) {
            config->disposed=true;unlink_owned(store,config);
            store->cached[index]=NULL;bot_fuzzy_owned_release(config);
        }
    }
    if(ok) store->cached_count=0;
    store->active=false;return ok;
}
void bot_fuzzy_store_dispose(bot_fuzzy_store *store) {
    if(!store || store->active || store->closed) return;
    store->closed=true;store->library=NULL;
    while(store->readers) close_reader(store,store->readers);
    for(size_t index=0;index<128;++index) {
        bot_fuzzy_owned *config=store->cached[index];store->cached[index]=NULL;bot_fuzzy_owned_release(config);
    }
    while(store->first) {
        bot_fuzzy_owned *config=store->first;store->first=config->next;
        config->next=NULL;config->owned=false;bot_fuzzy_owned_release(config);
    }
    store->last=NULL;store->cached_count=0;release_store(store);
}
