#include "source_initial_resource.h"
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error,const char *text) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",text);return false;
}
static char *copy_text(const char *text,qa_error *error) {
    size_t size=strlen(text)+1;char *copy=malloc(size);
    if(!copy) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining initial chat source text");return NULL;}
    memcpy(copy,text,size);return copy;
}
bool bot_chat_initial_resource_pure(qa_bot_memory *memory,bot_chat_initial_resource **out,
    qa_error *error) {
    if(!memory || !out || *out) return fail(error,"Pure initial chat resource requires actual MEMORY and empty output");
    bot_chat_initial_resource *resource=calloc(1,sizeof(*resource));
    if(!resource) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining pure initial chat resource");return false;}
    if(!qa_bot_memory_retain(memory,error)) {free(resource);return false;}
    resource->memory=memory;*out=resource;return true;
}
bool bot_chat_initial_resource_current(const bot_chat_initial_resource *resource,qa_error *error) {
    if(!resource || qa_bot_memory_disposed(resource->memory))
        return fail(error,"Initial chat resource has no live actual MEMORY owner");
    if(resource->report_failed) {if(error) *error=resource->report_error;return false;}
    if(!resource->host.current) return fail(error,"Initial chat resource has no installed source callbacks");
    return resource->host.current(resource->host.context,error);
}
static bool current(void *context,qa_error *error) {
    return bot_chat_initial_resource_current(context,error);
}
static bool diagnostic(bot_chat_initial_resource *resource,bot_chat_initial_diagnostic_origin origin,
    const qa_script_diagnostic *issue,qa_error *error) {
    if(!current(resource,error)) return false;
    if(!resource->host.report(resource->host.context,origin,issue,error)) {
        resource->report_failed=true;resource->report_error=error?*error:(qa_error){0};return false;
    }
    return current(resource,error);
}
static bool report(void *context,const qa_script_diagnostic *issue,qa_error *error) {
    return diagnostic(context,BOT_CHAT_INITIAL_PRINT_DIAGNOSTIC,issue,error);
}
static void source_report(void *context,const qa_script_diagnostic *issue) {
    bot_chat_initial_resource *resource=context;qa_error error={0};
    if(resource->report_failed) return;
    bool ok=current(resource,&error);
    if(ok && resource->services.diagnostic)
        resource->services.diagnostic(resource->services.context,issue);
    if(ok) ok=diagnostic(resource,BOT_CHAT_INITIAL_SOURCE_DIAGNOSTIC,issue,&error);
    if(!ok && !resource->retired_abort) {resource->report_failed=true;resource->report_error=error;}
}
static bool source_read(void *context,const qa_script_include *request,qa_script_resource *out,
    bool *found,qa_error *error) {
    bot_chat_initial_resource *resource=context;
    if(!current(resource,error)) return false;
    bot_chat_initial_acquired *pending=calloc(1,sizeof(*pending));
    if(!pending) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining initial chat source acquisition");return false;}
    bool ok=resource->services.read(resource->services.context,request,out,found,error);
    qa_error reached=error?*error:(qa_error){0};
    if(!ok) resource->service_failed=true;
    bool live=current(resource,error);
    if(!ok && error) *error=reached;
    bool invalid=ok && live && *found && request->kind==QA_SCRIPT_ROOT &&
        out->path && !*out->path && (!out->bytes.size || out->bytes.data);
    if(invalid) {
        resource->own_failure=true;
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Initial chat source has an empty canonical path");
    }
    if(ok && *found && (!live || invalid)) {
        pending->resource=*out;pending->next=resource->pending;resource->pending=pending;
        *out=(qa_script_resource){0};pending=NULL;
    }
    free(pending);
    if(ok && live && !*found && request->kind==QA_SCRIPT_ROOT) resource->missing_root=true;
    return ok && live && !invalid;
}
static void source_release(void *context,qa_script_resource *source) {
    bot_chat_initial_resource *resource=context;
    resource->services.release(resource->services.context,source);
}
qa_script_services bot_chat_initial_resource_services(bot_chat_initial_resource *resource) {
    qa_script_services services=resource->services;
    services.context=resource;services.read=source_read;services.release=source_release;
    services.diagnostic=source_report;return services;
}
static bool complete(void *context,qa_error *error) {
    bot_chat_initial_resource *resource=context;
    if(!current(resource,error)) return false;
    qa_script *reader=resource->reader;resource->reader=NULL;qa_script_close(reader);
    return current(resource,error);
}
static bool equal_name(void *context,qa_bytes name,bool *out,qa_error *error) {
    bot_chat_initial_resource *resource=context;
    return current(resource,error) &&
        resource->host.name_equal(resource->host.context,name,out,error) && current(resource,error);
}
bool bot_chat_initial_resource_create(qa_bot_memory *memory,const qa_script_services *services,
    const qa_script_options *options,const bot_chat_initial_resource_host *host,
    bot_chat_initial_resource **out,qa_error *error) {
    if(!memory || !services || !services->read || !services->release || !options || !host ||
       !host->current || !host->report || !host->name_equal || !host->path || !host->identity ||
       !host->publish || !out || *out)
        return fail(error,"Initial chat resource requires its real services/callbacks and empty output");
    bot_chat_initial_resource *resource=calloc(1,sizeof(*resource));
    if(!resource) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining actual initial chat resource owner");return false;}
    if(!qa_bot_memory_retain(memory,error)) {free(resource);return false;}
    resource->memory=memory;resource->services=*services;resource->options=*options;
    resource->services.memory=qa_bot_memory_script_services(memory);
    resource->host=*host;
    if(options->globals) qa_script_defines_retain((qa_script_defines *)options->globals);
    if(options->include_path) {
        resource->include_path=copy_text(options->include_path,error);
        if(!resource->include_path) goto failed;
        resource->options.include_path=resource->include_path;
    }
    if(services->date) {
        resource->date=copy_text(services->date,error);
        if(!resource->date) goto failed;
        resource->services.date=resource->date;
    }
    if(services->time) {
        resource->time=copy_text(services->time,error);
        if(!resource->time) goto failed;
        resource->services.time=resource->time;
    }
    *out=resource;return true;
failed:
    bot_chat_initial_resource_destroy(resource);return false;
}
void bot_chat_initial_resource_destroy(bot_chat_initial_resource *resource) {
    if(!resource) return;
    qa_script_dispose(resource->reader);
    while(resource->pending) {
        bot_chat_initial_acquired *row=resource->pending;resource->pending=row->next;
        if(row->owned) {free((void *)row->resource.path);free((void *)row->resource.bytes.data);}
        else resource->services.release(resource->services.context,&row->resource);
        free(row);
    }
    bot_chat_initial_dispose(&resource->initial);
    free(resource->path);free(resource->include_path);free(resource->date);free(resource->time);
    qa_script_defines_release((qa_script_defines *)resource->options.globals);
    (void)qa_bot_memory_release(resource->memory,NULL);free(resource);
}
bool bot_chat_initial_resource_load(bot_chat_initial_resource *resource,bool *source_failure,
    qa_error *error) {
    if(!resource || !source_failure || resource->active || resource->attempted)
        return fail(error,"Initial chat load requires its unattempted actual source owner/outcome");
    *source_failure=false;
    if(!current(resource,error)) return false;
    resource->attempted=resource->active=true;
    qa_script_services services=bot_chat_initial_resource_services(resource);
    bool ok=true;
    for(resource->pass=0;ok && resource->pass<2;++resource->pass) {
        resource->missing_root=false;
        if(resource->pass && resource->size) {
            resource->initial.memory=resource->memory;
            ok=qa_bot_memory_allocate(resource->memory,resource->size,QA_BOT_MEMORY_HEAP,true,NULL,
                &resource->initial.allocation,error);
            if(!ok) resource->service_failed=true;
            if(ok) ok=current(resource,error);
            uint32_t pointer=0;
            if(ok) ok=resource->host.identity(resource->host.context,&pointer,error) && current(resource,error);
            if(ok) ok=bot_chat_initial_bind(resource->memory,resource->initial.allocation,pointer,
                &resource->initial,error);
            if(ok) {
                ok=resource->host.publish(resource->host.context,&resource->initial,error);
                if(ok) {resource->published=true;ok=current(resource,error);}
            }
        }
        if(!ok) break;
        const char *path=NULL;
        ok=resource->host.path(resource->host.context,&path,error) && current(resource,error);
        if(!ok) break;
        if(!path) {resource->own_failure=true;ok=fail(error,"Initial chat source path is null");break;}
        ok=qa_script_open(path,&services,&resource->options,&resource->reader,error);
        qa_error reached=error?*error:(qa_error){0};
        if(!current(resource,error)) {
            if(!ok && resource->service_failed && error) *error=reached;
            ok=false;
        }
        if(!ok) {
            if(resource->missing_root && !resource->service_failed && !resource->own_failure &&
               !resource->report_failed && current(resource,error)) {
                resource->failure=BOT_CHAT_INITIAL_ROOT_MISSING;*source_failure=true;
            }
            break;
        }
        qa_script_location location=qa_script_position(resource->reader);
        if(!location.path || !*location.path) {
            resource->own_failure=true;ok=fail(error,"Initial chat source has no canonical reader path");break;
        }
        char *canonical=copy_text(location.path,error);
        if(!canonical) {ok=false;break;}
        free(resource->path);resource->path=canonical;
        bot_chat_initial_parser_host host={resource,current,report,equal_name,complete};
        bool grammar_failure=false,own_failure=false,not_found=false;
        ok=bot_chat_initial_parse(resource->reader,resource->path,&host,
            resource->pass?&resource->initial:NULL,resource->pass!=0,&resource->size,
            &grammar_failure,&own_failure,&not_found,error);
        resource->own_failure=resource->own_failure || own_failure;
        if(!ok && grammar_failure) {
            qa_error grammar_error=error?*error:(qa_error){0};
            if(complete(resource,error)) {
                resource->failure=BOT_CHAT_INITIAL_GRAMMAR_FAILURE;*source_failure=true;
                if(error) *error=grammar_error;
            }
        }
        if(ok && not_found) {
            resource->failure=BOT_CHAT_INITIAL_NAME_MISSING;*source_failure=true;ok=false;
        }
        if(!ok) break;
    }
    if(ok) resource->loaded=true;
    resource->active=false;return ok;
}
