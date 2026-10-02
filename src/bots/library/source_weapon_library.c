#include "internal.h"
#include "source_weapon_library.h"
#include "source_weapon_view.h"
#include "qa/bot_log.h"
#include <stdio.h>

static bool fail(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static bool library_current(void *context,qa_error *error) {
    qa_bot_library *library=context;
    return library && !qa_bot_memory_disposed(library->memory)?true:
        fail(error,"Weapon source library is no longer current");
}
static bool library_report(void *context,bot_weapon_diagnostic_origin origin,
    const qa_script_diagnostic *diagnostic,qa_error *error) {
    qa_bot_library *library=context;(void)origin;
    if(!library_current(library,error)) return false;
    qa_bot_log *log=qa_bot_library_log(library);
    if(!log) return true;
    const char *path=diagnostic->location.path,*message=diagnostic->message;
    int length=snprintf(NULL,0,"file %s, line %u: %s\n",path,diagnostic->location.line,message);
    if(length<0) return fail(error,"Formatting source weapon diagnostic");
    char *text=malloc((size_t)length+1);
    if(!text) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source weapon diagnostic text");return false;}
    (void)snprintf(text,(size_t)length+1,"file %s, line %u: %s\n",path,diagnostic->location.line,message);
    bool ok=qa_bot_log_print(log,diagnostic->severity,text,error);free(text);return ok;
}
bot_weapon_resource_host bot_weapons_library_host(qa_bot_library *library) {
    return (bot_weapon_resource_host){.context=library,.current=library_current,
        .report=library_report};
}
bool bot_weapons_load_source(qa_bot_library *library,const char *path,size_t weapons,size_t projectiles,
    const bot_weapon_resource_host *host,qa_bot_weapons **out,bool *source_failure,qa_error *error) {
    if(!library || !path || !*path || !out || !source_failure || weapons>INT32_MAX || projectiles>INT32_MAX)
        return fail(error,"Weapon load requires actual library, source path, capacities and outputs");
    *source_failure=false;
    if(!qa_bot_library_idle(library)) return fail(error,"Weapon source library is executing another operation");
    qa_bot_weapons *config=calloc(1,sizeof(*config));
    if(!config) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining actual weapon configuration binding");return false;}
    atomic_init(&config->references,1);
    bot_weapon_resource_host fallback=bot_weapons_library_host(library);
    if(!bot_weapon_resource_create(library->memory,&library->options.scripts,&library->options.preprocessor,
        host?host:&fallback,&config->source,error)) {qa_bot_weapons_release(config);return false;}
    /* Retain the actual reached owner before the first resolver callback. */
    config->next=library->weapon_configs;library->weapon_configs=config;
    if(!bot_weapon_resource_load(config->source,path,(uint32_t)weapons,(uint32_t)projectiles,source_failure,error))
        return false;
    qa_bot_weapons_retain(config);*out=config;return true;
}
bool bot_weapons_source_capacity(const qa_bot_weapons *config,int32_t *out,qa_error *error) {
    return config && config->source?bot_weapon_resource_capacity(config->source,out,error):
        fail(error,"Weapon capacity has no actual source configuration");
}
bool bot_weapons_source_member(const qa_bot_weapons *config,uint32_t index,bool *out,qa_error *error) {
    if(!config || !config->source || !config->source->bound || !out)
        return fail(error,"Weapon member has no actual bound source configuration/output");
    *out=index<config->source->weapon_count && config->source->defined[index];return true;
}
bool bot_weapons_source_valid(const qa_bot_weapons *config,uint32_t index,bool *out,qa_error *error) {
    if(!bot_weapons_source_member(config,index,out,error)) return false;
    if(!*out) return true;
    bot_weapon_config_cell cell;int32_t valid;
    if(!bot_weapon_config_weapon(&config->source->record,index,&cell,error) ||
       !bot_weapon_config_int(&cell,0,&valid,error)) return false;
    *out=valid!=0;return true;
}
bool bot_weapons_source_name(const qa_bot_weapons *config,uint32_t index,char out[81],qa_error *error) {
    bool found;bot_weapon_config_cell cell;
    if(!config || !config->source || !out) return fail(error,"Weapon name requires its source/output");
    if(!bot_weapon_resource_weapon(config->source,index,&found,&cell,error)) return false;
    if(!found) {out[0]=0;return true;}
    return bot_weapon_config_text(&cell,8,out,error);
}
bool bot_weapons_source_bytes(const qa_bot_weapons *config,uint32_t index,qa_bytes *out,qa_error *error) {
    if(!config || !config->source || !config->source->bound || !out)
        return fail(error,"Weapon byte copy requires its bound source/output");
    bot_weapon_config_cell cell;qa_bot_memory_span span;
    if(!bot_weapon_config_weapon(&config->source->record,index,&cell,error) ||
       !bot_weapon_config_span(&cell,&span,error)) return false;
    *out=(qa_bytes){span.data,span.size};return true;
}
bool bot_weapons_source_info(const qa_bot_weapons *config,uint32_t index,qa_bot_weapon_info *weapon,
    qa_bot_projectile_info *projectile,qa_error *error) {
    return config && config->source?bot_weapon_resource_info(config->source,index,weapon,projectile,error):
        fail(error,"Weapon info has no actual source configuration");
}
bool bot_weapons_source_free(qa_bot_weapons *config,qa_error *error) {
    return config && config->source?bot_weapon_resource_free(config->source,error):
        fail(error,"Weapon free has no actual source configuration");
}
const qa_bot_weapons_view *bot_weapons_source_view(qa_bot_weapons *config,qa_error *error) {
    if(!config || !config->source || !config->source->bound) {
        fail(error,"Weapon metadata has no bound source configuration");return NULL;
    }
    bot_weapon_resource *source=config->source;
    size_t count=source->weapon_count,projectiles=source->projectile_count;
    if(count>SIZE_MAX/sizeof(*config->weapons) || projectiles>SIZE_MAX/sizeof(*config->projectiles)) {
        fail(error,"Weapon metadata table exceeds native storage");return NULL;
    }
    qa_bot_weapon_info *weapons=count?calloc(count,sizeof(*weapons)):NULL;
    qa_bot_projectile_info *projectile_values=projectiles?calloc(projectiles,sizeof(*projectile_values)):NULL;
    if((count && !weapons) || (projectiles && !projectile_values)) {
        free(weapons);free(projectile_values);qa_error_set(error,QA_ERROR_MEMORY,0,"Projecting source weapon metadata");return NULL;
    }
    bool ok=true;
    for(uint32_t i=0;ok && i<source->projectile_count;++i) {
        bot_weapon_config_cell cell;
        ok=bot_weapon_config_projectile_at(&source->record,source->weapon_count,i,&cell,error) &&
            bot_weapon_projectile_value(&cell,&projectile_values[i],error);
    }
    for(uint32_t i=0;ok && i<source->weapon_count;++i) {
        if(!source->defined[i]) continue;
        qa_bot_projectile_info embedded;
        ok=bot_weapon_resource_info(source,i,&weapons[i],&embedded,error);
        bot_weapon_config_cell weapon_cell;char name[81];
        if(ok) ok=bot_weapon_config_weapon(&source->record,i,&weapon_cell,error) &&
            bot_weapon_config_text(&weapon_cell,180,name,error);
        for(uint32_t p=0;ok && p<source->projectile_count;++p) {
            bot_weapon_config_cell cell;char candidate[81];
            ok=bot_weapon_config_projectile_at(&source->record,source->weapon_count,p,&cell,error) &&
                bot_weapon_config_text(&cell,0,candidate,error);
            if(ok && !strcmp(name,candidate)) {weapons[i].projectile_index=p;break;}
        }
    }
    if(!ok) {free(weapons);free(projectile_values);return NULL;}
    free(config->weapons);free(config->projectiles);
    config->weapons=weapons;config->projectiles=projectile_values;
    config->view=(qa_bot_weapons_view){.path=source->path,.weapons=weapons,.projectiles=projectile_values,
        .weapon_capacity=count,.weapon_count=source->defined_count,
        .projectile_capacity=projectiles,.projectile_count=projectiles};
    return &config->view;
}
