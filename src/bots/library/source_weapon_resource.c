#include "source_weapon_resource.h"
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static char *copy_text(const char *text,qa_error *error) {
    size_t size=strlen(text);
    if(size==SIZE_MAX) {fail(error,"Weapon source text exceeds native storage");return NULL;}
    char *copy=malloc(size+1);
    if(!copy) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining weapon source text");return NULL;}
    memcpy(copy,text,size+1);return copy;
}
bool bot_weapon_resource_current(const bot_weapon_resource *resource,qa_error *error) {
    if(!resource || qa_bot_memory_disposed(resource->memory))
        return fail(error,"Weapon resource has no live source memory owner");
    if(resource->report_failed) {if(error) *error=resource->report_error;return false;}
    return (!resource->host.current || resource->host.current(resource->host.context,error)) &&
        (!resource->host.qualify || resource->host.qualify(resource->host.context,
            resource->host.generation,resource->host.revision,error));
}
static bool reader_current(void *context,qa_error *error) {
    return bot_weapon_resource_current(context,error);
}
static bool retain_diagnostic(bot_weapon_resource *resource,bot_weapon_diagnostic_origin origin,
    const qa_script_diagnostic *diagnostic,qa_error *error) {
    if(!diagnostic || !diagnostic->location.path || !diagnostic->message)
        return fail(error,"Weapon diagnostic requires its actual path and message");
    if(resource->diagnostic_count==resource->diagnostic_capacity) {
        size_t capacity=resource->diagnostic_capacity?resource->diagnostic_capacity*2:8;
        if(capacity<resource->diagnostic_capacity || capacity>SIZE_MAX/sizeof(*resource->diagnostics))
            return fail(error,"Weapon diagnostic count exceeds native storage");
        void *next=realloc(resource->diagnostics,capacity*sizeof(*resource->diagnostics));
        if(!next) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining weapon source diagnostics");return false;}
        resource->diagnostics=next;resource->diagnostic_capacity=capacity;
    }
    char *path=copy_text(diagnostic->location.path,error);
    if(!path) return false;
    char *message=copy_text(diagnostic->message,error);
    if(!message) {free(path);return false;}
    bot_weapon_diagnostic *row=&resource->diagnostics[resource->diagnostic_count++];
    *row=(bot_weapon_diagnostic){.origin=origin,.value=*diagnostic,.path=path,.message=message};
    row->value.location.path=path;row->value.message=message;return true;
}
static bool report(bot_weapon_resource *resource,bot_weapon_diagnostic_origin origin,
    const qa_script_diagnostic *diagnostic,qa_error *error) {
    if(!bot_weapon_resource_current(resource,error) ||
       !retain_diagnostic(resource,origin,diagnostic,error)) return false;
    return (!resource->host.report ||
        resource->host.report(resource->host.context,origin,diagnostic,error)) &&
        bot_weapon_resource_current(resource,error);
}
static bool parser_report(void *context,const qa_script_diagnostic *diagnostic,qa_error *error) {
    return report(context,BOT_WEAPON_DIAGNOSTIC_PRINT,diagnostic,error);
}
static void source_diagnostic(void *context,const qa_script_diagnostic *diagnostic) {
    bot_weapon_resource *resource=context;qa_error error={0};
    if(resource->report_failed) return;
    bool ok=bot_weapon_resource_current(resource,&error);
    if(ok && resource->services.diagnostic)
        resource->services.diagnostic(resource->services.context,diagnostic);
    if(ok) ok=report(resource,BOT_WEAPON_DIAGNOSTIC_SOURCE,diagnostic,&error);
    if(!ok) {resource->report_failed=true;resource->report_error=error;}
}
static bool source_read(void *context,const qa_script_include *request,qa_script_resource *out,
    bool *found,qa_error *error) {
    bot_weapon_resource *resource=context;
    if(!bot_weapon_resource_current(resource,error)) return false;
    bot_weapon_acquired_source *pending=calloc(1,sizeof(*pending));
    if(!pending) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining weapon source acquisition");return false;}
    bool ok=resource->services.read(resource->services.context,request,out,found,error);
    bool current=bot_weapon_resource_current(resource,error);
    bool invalid=ok && current && *found && request->kind==QA_SCRIPT_ROOT &&
        out->path && !*out->path && (!out->bytes.size || out->bytes.data);
    if(invalid) {
        resource->invalid_root_path=true;resource->own_failure=true;
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"weapon config resolver returned an empty canonical path");
    }
    if(ok && *found && (!current || invalid)) {
        pending->value=*out;pending->next=resource->pending;resource->pending=pending;
        *out=(qa_script_resource){0};pending=NULL;
    }
    free(pending);
    if(ok && current && !*found && request->kind==QA_SCRIPT_ROOT) resource->missing_root=true;
    return current && ok && !invalid;
}
static void source_release(void *context,qa_script_resource *source) {
    bot_weapon_resource *resource=context;
    resource->services.release(resource->services.context,source);
}
static bool staged_open(void *context,const qa_script_include *request,qa_script_file *out,bool *found,qa_error *error) {
    bot_weapon_resource *resource=context;if(!bot_weapon_resource_current(resource,error)) return false;
    bool ok=resource->services.file_open(resource->services.context,request,out,found,error);
    qa_error reached=error?*error:(qa_error){0};bool live=bot_weapon_resource_current(resource,error);
    if(!ok && error) *error=reached;
    if(ok && live && !*found && request->kind==QA_SCRIPT_ROOT) resource->missing_root=true;
    if(ok && live && *found && request->kind==QA_SCRIPT_ROOT && out->path.data && !out->path.data[0]) {
        resource->own_failure=true;resource->invalid_root_path=true;qa_error_set(error,QA_ERROR_ARGUMENT,0,"Bot config open returned an empty canonical path");return false;
    }
    return ok && live;
}
static bool staged_read(void *context,const qa_script_file *file,qa_script_memory_span span,qa_error *error) {
    bot_weapon_resource *resource=context;if(!bot_weapon_resource_current(resource,error)) return false;
    bool ok=resource->services.file_read(resource->services.context,file,span,error);
    qa_error reached=error?*error:(qa_error){0};bool live=bot_weapon_resource_current(resource,error);
    if(!ok && error) *error=reached;
    return ok && live;
}
static bool staged_close(void *context,const qa_script_file *file,qa_error *error) {
    bot_weapon_resource *resource=context;if(!bot_weapon_resource_current(resource,error)) return false;
    bool ok=resource->services.file_close(resource->services.context,file,error);
    qa_error reached=error?*error:(qa_error){0};bool live=bot_weapon_resource_current(resource,error);
    if(!ok && error) *error=reached;
    return ok && live;
}
qa_script_services bot_weapon_resource_services(bot_weapon_resource *resource) {
    qa_script_services services=resource->services;
    services.file_open=resource->services.file_open?staged_open:NULL;
    services.file_read=resource->services.file_read?staged_read:NULL;
    services.file_close=resource->services.file_close?staged_close:NULL;

    services.context=resource;services.read=source_read;services.release=source_release;
    services.diagnostic=source_diagnostic;return services;
}
static bool complete(void *context,qa_error *error) {
    bot_weapon_resource *resource=context;
    if(!bot_weapon_resource_current(resource,error)) return false;
    qa_script *reader=resource->reader;resource->reader=NULL;
    qa_script_close(reader);
    return bot_weapon_resource_current(resource,error);
}
bool bot_weapon_resource_create(qa_bot_memory *memory,const qa_script_services *services,
    const qa_script_options *options,const bot_weapon_resource_host *host,
    bot_weapon_resource **out,qa_error *error) {
    if(!memory || !qa_script_services_valid(services) || !options || !out || *out)
        return fail(error,"Weapon resource requires its actual memory, source services and empty output");
    bot_weapon_resource *resource=calloc(1,sizeof(*resource));
    if(!resource) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining weapon resource owner");return false;}
    if(!qa_bot_memory_retain(memory,error)) {free(resource);return false;}
    resource->memory=memory;resource->services=*services;resource->options=*options;
    resource->services.memory=qa_bot_memory_script_services(memory);
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
    if(host) resource->host=*host;
    *out=resource;return true;
failed:
    bot_weapon_resource_destroy(resource);return false;
}
void bot_weapon_resource_destroy(bot_weapon_resource *resource) {
    if(!resource) return;
    qa_script_dispose(resource->reader);
    while(resource->pending) {
        bot_weapon_acquired_source *row=resource->pending;resource->pending=row->next;
        if(row->owned) {free((void *)row->value.path);free((void *)row->value.bytes.data);}
        else resource->services.release(resource->services.context,&row->value);
        free(row);
    }
    for(size_t i=0;i<resource->diagnostic_count;++i) {
        free(resource->diagnostics[i].path);free(resource->diagnostics[i].message);
    }
    free(resource->diagnostics);free(resource->defined);free(resource->path);
    free(resource->include_path);free(resource->date);free(resource->time);
    qa_script_defines_release((qa_script_defines *)resource->options.globals);
    (void)qa_bot_memory_release(resource->memory,NULL);free(resource);
}
bool bot_weapon_resource_bind(bot_weapon_resource *resource,bot_weapon_config_record record,
    const char *path,qa_error *error) {
    if(!resource || resource->bound || !path || !*path || record.memory!=resource->memory)
        return fail(error,"Weapon binding requires an unbound resource and its actual allocation/path");
    bot_weapon_config_record qualified;
    if(!bot_weapon_config_bind(resource->memory,record.allocation,&qualified,error)) return false;
    bot_weapon_config_cell header;int32_t capacity,count;
    if(!bot_weapon_config_header(&qualified,&header,error) ||
       !bot_weapon_config_int(&header,0,&capacity,error) ||
       !bot_weapon_config_int(&header,4,&count,error)) return false;
    uint8_t *defined=calloc(capacity?(uint32_t)capacity:1,1);
    if(!defined) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining weapon source array membership");return false;}
    uint32_t defined_count=0;
    for(uint32_t index=0;index<(uint32_t)capacity;++index) {
        bot_weapon_config_cell weapon;int32_t valid;
        if(!bot_weapon_config_weapon(&qualified,index,&weapon,error) ||
           !bot_weapon_config_int(&weapon,0,&valid,error)) {free(defined);return false;}
        defined[index]=valid!=0;if(valid) ++defined_count;
    }
    char *canonical=copy_text(path,error);
    if(!canonical) {free(defined);return false;}
    free(resource->path);resource->path=canonical;resource->record=qualified;
    resource->defined=defined;resource->weapon_count=(uint32_t)capacity;
    resource->projectile_count=(uint32_t)count;resource->defined_count=defined_count;
    resource->bound=true;return true;
}
bool bot_weapon_resource_load(bot_weapon_resource *resource,const char *path,uint32_t weapons,
    uint32_t projectiles,bool *source_failure,qa_error *error) {
    if(!resource || !source_failure || !path || !*path || resource->attempted || resource->active || resource->bound ||
       !qa_script_services_valid(&resource->services))
        return fail(error,"Weapon load requires an unattempted source owner, path and outcome");
    *source_failure=false;
    if(weapons>INT32_MAX || projectiles>INT32_MAX)
        return fail(error,"Weapon capacities exceed their validated source integer domain");
    if(!bot_weapon_resource_current(resource,error)) return false;
    resource->attempted=true;resource->active=true;
    qa_script_services services=bot_weapon_resource_services(resource);
    bool ok=qa_script_open(path,&services,&resource->options,&resource->reader,error);
    if(!bot_weapon_resource_current(resource,error)) ok=false;
    if(ok) {
        qa_script_location location=qa_script_position(resource->reader);
        if(!location.path || !*location.path) {
            resource->own_failure=true;ok=fail(error,"weapon config resolver returned an empty canonical path");
        }
        else resource->path=copy_text(location.path,error);
        if(!resource->path) ok=false;
    }
    if(ok) {
        uint32_t size=BOT_WEAPON_CONFIG_BYTES+weapons*BOT_WEAPON_INFO_BYTES+projectiles*BOT_PROJECTILE_INFO_BYTES;
        if(size>INT32_MAX-4) {
            resource->own_failure=true;ok=fail(error,"Bot memory allocation must fit a signed source size with its four-byte prefix");
        } else {
            ok=bot_weapon_config_allocate(resource->memory,weapons,projectiles,&resource->record,error);
            if(!ok && resource->record.allocation.owner && bot_weapon_resource_current(resource,error))
                resource->own_failure=true;
        }
    }
    if(ok) {
        bot_weapon_parser_host host={resource,reader_current,parser_report,complete};
        ok=bot_weapon_parse(resource->reader,resource->path,weapons,projectiles,
            &resource->record,&host,source_failure,&resource->own_failure,error);
    }
    if(ok) {
        bot_weapon_config_record qualified;
        ok=bot_weapon_config_bind(resource->memory,resource->record.allocation,&qualified,error);
        if(!ok && bot_weapon_resource_current(resource,error)) resource->own_failure=true;
        else if(ok) ok=bot_weapon_resource_bind(resource,qualified,resource->path,error);
    }
    if(!ok && *source_failure) {
        qa_error reached=error?*error:(qa_error){0};
        if(!bot_weapon_resource_current(resource,error) ||
           !qa_bot_memory_free(resource->memory,resource->record.allocation,error) ||
           !complete(resource,error)) *source_failure=false;
        else if(error) *error=reached;
    }
    if(!ok && resource->missing_root && !resource->report_failed &&
       bot_weapon_resource_current(resource,error)) *source_failure=true;
    if(!ok && resource->invalid_root_path) {
        resource->own_failure=true;
        if(!resource->report_failed) qa_error_set(error,QA_ERROR_ARGUMENT,0,"weapon config resolver returned an empty canonical path");
    }
    if(!ok && resource->own_failure && !resource->report_failed &&
       bot_weapon_resource_current(resource,error)) *source_failure=true;
    resource->active=false;return ok;
}
bool bot_weapon_resource_free(bot_weapon_resource *resource,qa_error *error) {
    if(!resource || !resource->bound || resource->active)
        return fail(error,"Weapon free requires its bound idle source resource");
    return qa_bot_memory_free(resource->memory,resource->record.allocation,error);
}
bool bot_weapon_resource_weapon(const bot_weapon_resource *resource,uint32_t index,bool *found,
    bot_weapon_config_cell *out,qa_error *error) {
    if(!resource || !resource->bound || !found || !out)
        return fail(error,"Weapon member read requires its bound resource and outputs");
    *found=index<resource->weapon_count && resource->defined[index];
    return !*found || bot_weapon_config_weapon(&resource->record,index,out,error);
}
bool bot_weapon_resource_capacity(const bot_weapon_resource *resource,int32_t *out,qa_error *error) {
    if(!resource || !resource->bound) return fail(error,"Weapon capacity has no bound source configuration");
    bot_weapon_config_cell header;
    return bot_weapon_config_header(&resource->record,&header,error) &&
        bot_weapon_config_int(&header,0,out,error);
}
bool bot_weapon_resource_projectile_bytes(const bot_weapon_resource *resource,uint32_t index,
    qa_bot_memory_span *out,qa_error *error) {
    if(!resource || !resource->bound || index>=resource->projectile_count)
        return fail(error,"Projectile dump exceeds its captured source array membership");
    bot_weapon_config_cell cell;
    return bot_weapon_config_projectile(&resource->record,index,&cell,error) &&
        bot_weapon_config_span(&cell,out,error);
}
