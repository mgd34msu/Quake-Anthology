#include "source_weapon_setup.h"
#include "../library/internal.h"
#include "../library/source_weapon_library.h"
#include "../library/source_weapon_log.h"
#include "qa/bot_weapons_source.h"
#include "../save_fields.h"
#include "qa/bot_log.h"
#include <stdio.h>

static char *text_copy(const char *text,qa_error *error) {
    size_t size=strlen(text)+1;char *copy=malloc(size);
    if(!copy) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining weapon AI diagnostic text");return NULL;}
    memcpy(copy,text,size);return copy;
}
static bool current(void *context,qa_error *error) {
    qa_bot_runtime *runtime=context;
    return runtime && !runtime->closed && runtime->memory && !qa_bot_memory_disposed(runtime->memory)?true:
        bot_runtime_fail(error,"Weapon setup no longer owns its live source runtime");
}
static bool qualify(void *context,uint64_t generation,uint64_t revision,qa_error *error) {
    qa_bot_runtime *runtime=context;
    return current(context,error) && (runtime->weapon_generation==generation &&
        runtime->weapon_setup_revision==revision?true:
        bot_runtime_fail(error,"Weapon setup was retired by a later source revision"));
}
static bool append(qa_bot_runtime *runtime,bot_weapon_diagnostic_origin origin,
    const qa_script_diagnostic *diagnostic,bool deduplicate,qa_error *error) {
    if(deduplicate) for(size_t i=0;i<runtime->weapon_diagnostic_count;++i) {
        const qa_script_diagnostic *old=&runtime->weapon_diagnostics[i].value;
        if(old->severity==diagnostic->severity && old->location.line==diagnostic->location.line &&
           old->location.column==diagnostic->location.column &&
           !strcmp(old->location.path,diagnostic->location.path) && !strcmp(old->message,diagnostic->message)) return true;
    }
    if(runtime->weapon_diagnostic_count==runtime->weapon_diagnostic_capacity) {
        size_t capacity=runtime->weapon_diagnostic_capacity?runtime->weapon_diagnostic_capacity*2:8;
        if(capacity<runtime->weapon_diagnostic_capacity || capacity>SIZE_MAX/sizeof(*runtime->weapon_diagnostics))
            return bot_runtime_fail(error,"Weapon diagnostic storage exceeds native extent");
        void *rows=realloc(runtime->weapon_diagnostics,capacity*sizeof(*runtime->weapon_diagnostics));
        if(!rows) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source weapon AI diagnostics");return false;}
        runtime->weapon_diagnostics=rows;runtime->weapon_diagnostic_capacity=capacity;
    }
    char *path=text_copy(diagnostic->location.path,error);
    if(!path) return false;
    char *message=text_copy(diagnostic->message,error);
    if(!message) {free(path);return false;}
    bot_weapon_diagnostic *row=&runtime->weapon_diagnostics[runtime->weapon_diagnostic_count++];
    *row=(bot_weapon_diagnostic){.origin=origin,.value=*diagnostic,.path=path,.message=message};
    row->value.location.path=path;row->value.message=message;return true;
}
static bool report(void *context,bot_weapon_diagnostic_origin origin,
    const qa_script_diagnostic *diagnostic,qa_error *error) {
    qa_bot_runtime *runtime=context;
    return append(runtime,origin,diagnostic,true,error) && qa_bot_log_print(runtime->log,
        diagnostic->severity==QA_SCRIPT_WARNING?QA_SCRIPT_WARNING:QA_SCRIPT_ERROR,diagnostic->message,error);
}
bot_weapon_resource_host bot_runtime_weapon_host(qa_bot_runtime *runtime) {
    return (bot_weapon_resource_host){.context=runtime,.current=current,.report=report,
        .generation=runtime->weapon_generation,.revision=runtime->weapon_setup_revision,.qualify=qualify};
}
bool bot_runtime_weapon_emit(qa_bot_runtime *runtime,qa_script_severity severity,const char *message,
    const char *path,qa_error *error) {
    qa_script_diagnostic value={.severity=severity,.message=message,
        .location={.path=path,.line=1,.column=1}};
    if(!append(runtime,BOT_WEAPON_DIAGNOSTIC_PRINT,&value,false,error)) return false;
    bool previous=runtime->busy;runtime->busy=true;
    bool ok=qa_bot_log_print(runtime->log,severity==QA_SCRIPT_WARNING?QA_SCRIPT_WARNING:QA_SCRIPT_ERROR,message,error);
    runtime->busy=previous;return ok;
}
static bool capacity(qa_bot_runtime *runtime,const char *name,int32_t *out,qa_error *error) {
    if(!bot_runtime_integer(runtime,name,"32",out,error)) return false;
    if(*out>0) return true;
    *out=32;return qa_bot_library_variable_set(runtime->library,name,"32",error);
}
bool bot_runtime_weapon_setup(qa_bot_runtime *runtime,int32_t *result,qa_error *error) {
    const qa_bot_variable *variable;
    if(!bot_runtime_variable(runtime,"weaponconfig","weapons.c",&variable,error)) return false;
    size_t size=strlen(variable->string);
    if(!size || size>63) return bot_runtime_fail(error,"Weapon config path must contain 1..63 source bytes");
    char path[64];memcpy(path,variable->string,size+1);
    if(runtime->weapon_setup_revision==UINT64_C(9007199254740991))
        return bot_runtime_fail(error,"Weapon setup revision exceeds the source checkpoint integer domain");
    ++runtime->weapon_setup_revision;
    bot_weapon_resource_host host=bot_runtime_weapon_host(runtime);
    int32_t weapons,projectiles;
    if(!capacity(runtime,"max_weaponinfo",&weapons,error) ||
       !qualify(runtime,host.generation,host.revision,error) ||
       !capacity(runtime,"max_projectileinfo",&projectiles,error) ||
       !qualify(runtime,host.generation,host.revision,error)) return false;
    qa_bot_weapons *config=NULL;bool source_failure=false;qa_error load_error={0};
    bool ok=bot_weapons_load_source(runtime->library,path,(uint32_t)weapons,(uint32_t)projectiles,
        &host,&config,&source_failure,&load_error);
    if(!ok) {
        if(!source_failure) {if(error) *error=load_error;return false;}
        if(!qualify(runtime,host.generation,host.revision,error)) return false;
        bot_weapon_resource *reached=runtime->library->weapon_configs->source;
        if(reached->missing_root) {
            char message[96];(void)snprintf(message,sizeof(message),"counldn't load %s",path);
            if(!bot_runtime_weapon_emit(runtime,QA_SCRIPT_ERROR,message,path,error) ||
               !qualify(runtime,host.generation,host.revision,error)) return false;
        } else if(reached->own_failure) {
            if(!bot_runtime_weapon_emit(runtime,QA_SCRIPT_ERROR,load_error.message,path,error) ||
               !qualify(runtime,host.generation,host.revision,error)) return false;
        }
        qa_bot_weapons_release(runtime->weapon_config);runtime->weapon_config=NULL;
        if(!bot_runtime_weapon_emit(runtime,QA_SCRIPT_FATAL,"couldn't load the weapon config",path,error)) return false;
        *result=12;return true;
    }
    if(!qualify(runtime,host.generation,host.revision,error)) {qa_bot_weapons_release(config);return false;}
    size_t canonical_size=strlen(config->source->path);
    if(canonical_size>SIZE_MAX-8) {qa_bot_weapons_release(config);return bot_runtime_fail(error,"Weapon loaded message extent overflow");}
    char *message=malloc(canonical_size+8);
    if(!message) {qa_bot_weapons_release(config);qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining weapon loaded message");return false;}
    memcpy(message,"loaded ",7);memcpy(message+7,config->source->path,canonical_size+1);
    ok=bot_runtime_weapon_emit(runtime,QA_SCRIPT_INFO,message,config->source->path,error) &&
        qualify(runtime,host.generation,host.revision,error);
    free(message);
    if(!ok) {qa_bot_weapons_release(config);return false;}
    qa_bot_weapons_release(runtime->weapon_config);runtime->weapon_config=config;
    return !runtime->options.debug || bot_weapon_config_dump(runtime->log,config,error);
}
bool qa_bot_runtime_weapon_dump(qa_bot_runtime *runtime,qa_error *error) {
    if(!bot_runtime_mutable(runtime,error)) return false;
    if(!runtime->weapon_config) return true;
    runtime->busy=true;
    bool ok=bot_weapon_config_dump(runtime->log,runtime->weapon_config,error);
    runtime->busy=false;return ok;
}
void bot_runtime_weapon_diagnostics_clear(qa_bot_runtime *runtime) {
    bot_weapon_diagnostics_dispose(runtime->weapon_diagnostics,runtime->weapon_diagnostic_count);
    runtime->weapon_diagnostics=NULL;
    runtime->weapon_diagnostic_count=runtime->weapon_diagnostic_capacity=0;
}
void bot_weapon_diagnostics_dispose(bot_weapon_diagnostic *rows,size_t count) {
    if(!rows) return;
    for(size_t i=0;i<count;++i) {free(rows[i].path);free(rows[i].message);}
    free(rows);
}
bool bot_weapon_diagnostics_copy(const bot_weapon_diagnostic *rows,size_t count,
    bot_weapon_diagnostic **out,qa_error *error) {
    if(!out || *out || (count && !rows) || count>SIZE_MAX/sizeof(*rows))
        return bot_runtime_fail(error,"Invalid source weapon diagnostic history extent/output");
    if(!count) return true;
    bot_weapon_diagnostic *copy=calloc(count,sizeof(*copy));
    if(!copy) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source weapon diagnostic history");return false;}
    for(size_t i=0;i<count;++i) {
        copy[i].origin=rows[i].origin;copy[i].value=rows[i].value;
        copy[i].path=text_copy(rows[i].path,error);
        if(copy[i].path) copy[i].message=text_copy(rows[i].message,error);
        if(!copy[i].path || !copy[i].message) {bot_weapon_diagnostics_dispose(copy,count);return false;}
        copy[i].value.location.path=copy[i].path;copy[i].value.message=copy[i].message;
    }
    *out=copy;return true;
}
bool bot_runtime_weapon_diagnostics_fields(qa_source_save_io *io,qa_bot_runtime *runtime) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t count=runtime->weapon_diagnostic_count;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(reading) {
        if(runtime->weapon_diagnostics || runtime->weapon_diagnostic_count ||
           io->offset>io->input.size || count>(io->input.size-io->offset)/34 ||
           count>SIZE_MAX/sizeof(*runtime->weapon_diagnostics))
            return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid retained weapon AI diagnostic count");
        if(count) {
            runtime->weapon_diagnostics=calloc(count,sizeof(*runtime->weapon_diagnostics));
            if(!runtime->weapon_diagnostics) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring weapon AI diagnostics");
        }
        runtime->weapon_diagnostic_count=runtime->weapon_diagnostic_capacity=count;
    }
    for(size_t i=0;i<count;++i) {
        bot_weapon_diagnostic *row=&runtime->weapon_diagnostics[i];
        uint32_t origin=row->origin,severity=row->value.severity;
        const char *path=reading?NULL:row->path,*message=reading?NULL:row->message;
        bool ok=qa_source_save_u32(io,&origin) && origin<=BOT_WEAPON_DIAGNOSTIC_PRINT &&
            qa_source_save_u32(io,&severity) && severity<=QA_SCRIPT_FATAL &&
            bot_save_text(io,&path) && path && bot_save_text(io,&message) && message;
        if(reading) {
            row->origin=(bot_weapon_diagnostic_origin)origin;row->value.severity=(qa_script_severity)severity;
            row->path=(char *)path;row->message=(char *)message;
            row->value.location.path=path;row->value.message=message;
        }
        if(ok) ok=qa_source_save_u32(io,&row->value.location.line) &&
            qa_source_save_u32(io,&row->value.location.column) &&
            qa_source_save_count(io,&row->value.location.offset,SIZE_MAX);
        if(!ok) return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid retained weapon AI diagnostic row");
    }
    return true;
}
