#include "source_weapon_standalone.h"
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_FORMAT,0,"%s",message);return false;
}
bool bot_weapon_resource_pure(qa_bot_memory *memory,bot_weapon_resource **out,qa_error *error) {
    if(!memory || !out || *out) return fail(error,"Pure weapon construction requires actual MEMORY and empty output");
    bot_weapon_resource *resource=calloc(1,sizeof(*resource));
    if(!resource) {qa_error_set(error,QA_ERROR_MEMORY,0,"Constructing pure weapon resource");return false;}
    if(!qa_bot_memory_retain(memory,error)) {free(resource);return false;}
    resource->memory=memory;*out=resource;return true;
}
static bool text(const bot_weapon_config_cell *cell,uint32_t offset,const char value[80],qa_error *error) {
    const char *end=memchr(value,0,80);
    if(!end) return fail(error,"Weapon value import contains unterminated source text");
    return bot_weapon_config_set_text(cell,offset,(qa_bytes){(const uint8_t *)value,(size_t)(end-value)},error);
}
static bool projectile_store(const bot_weapon_config_cell *cell,const qa_bot_projectile_info *value,
    qa_error *error) {
    if(!text(cell,0,value->name,error) || !text(cell,80,value->model,error)) return false;
#define INT(offset,field) if(!bot_weapon_config_set_int(cell,offset,value->field,error)) return false
#define FLOAT(offset,field) if(!bot_weapon_config_set_float(cell,offset,value->field,error)) return false
    INT(160,flags);FLOAT(164,gravity);INT(168,damage);FLOAT(172,radius);
    INT(176,visible_damage);INT(180,damage_type);INT(184,health_increase);
    FLOAT(188,push);FLOAT(192,detonation);FLOAT(196,bounce);FLOAT(200,bounce_friction);FLOAT(204,bounce_stop);
#undef INT
#undef FLOAT
    return true;
}
bool bot_weapon_standalone_restore(const qa_bot_weapons_view *view,bot_weapon_resource **out,
    qa_error *error) {
    if(!view || !out || *out || !view->path || !*view->path || view->weapon_capacity>INT32_MAX ||
       view->projectile_capacity>INT32_MAX || view->projectile_count>view->projectile_capacity ||
       (view->weapon_capacity && !view->weapons) || (view->projectile_count && !view->projectiles))
        return fail(error,"Weapon value import has invalid source capacities or tables");
    qa_bot_memory *memory=NULL;bot_weapon_resource *resource=NULL;
    bool ok=qa_bot_memory_create(NULL,&memory,error) && bot_weapon_resource_pure(memory,&resource,error);
    if(memory) (void)qa_bot_memory_release(memory,NULL);
    if(ok) ok=bot_weapon_config_allocate(resource->memory,(uint32_t)view->weapon_capacity,
        (uint32_t)view->projectile_capacity,&resource->record,error);
    for(uint32_t index=0;ok && index<view->projectile_count;++index) {
        bot_weapon_config_cell cell;
        ok=bot_weapon_config_projectile_at(&resource->record,(uint32_t)view->weapon_capacity,index,&cell,error) &&
            projectile_store(&cell,&view->projectiles[index],error);
    }
    bot_weapon_config_cell header;
    if(ok) ok=bot_weapon_config_header(&resource->record,&header,error) &&
        bot_weapon_config_set_int(&header,4,(int32_t)view->projectile_count,error);
    for(uint32_t index=0;ok && index<view->weapon_capacity;++index) {
        const qa_bot_weapon_info *weapon=&view->weapons[index];
        if(!weapon->valid) continue;
        if(weapon->number!=(int32_t)index || !memchr(weapon->name,0,80) ||
           !memchr(weapon->model,0,80) || !memchr(weapon->projectile,0,80) ||
           !weapon->name[0] || !weapon->projectile[0]) {
            ok=fail(error,"Weapon value import has invalid defined source fields");break;
        }
        size_t projectile=0;
        while(projectile<view->projectile_count &&
              strcmp(view->projectiles[projectile].name,weapon->projectile)) ++projectile;
        if(projectile==view->projectile_count) {
            ok=fail(error,"Weapon value import names an undefined projectile");break;
        }
        bot_weapon_config_cell cell,projectile_cell;
        ok=bot_weapon_config_store_weapon(&resource->record,index,weapon,error) &&
            bot_weapon_config_weapon(&resource->record,index,&cell,error) &&
            bot_weapon_config_projectile_at(&resource->record,(uint32_t)view->weapon_capacity,
                (uint32_t)projectile,&projectile_cell,error) &&
            bot_weapon_config_embed_projectile(&cell,&projectile_cell,error);
    }
    if(ok) ok=bot_weapon_resource_bind(resource,resource->record,view->path,error);
    if(ok && resource->defined_count!=view->weapon_count)
        ok=fail(error,"Weapon value import defined count differs from actual source rows");
    if(!ok) {bot_weapon_resource_destroy(resource);return false;}
    *out=resource;return true;
}
