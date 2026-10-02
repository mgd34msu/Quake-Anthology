#include "source_weapon_view.h"
#include <string.h>

static bool text(const bot_weapon_config_cell *cell,uint32_t offset,char out[80],qa_error *error) {
    char source[81];
    if(!bot_weapon_config_text(cell,offset,source,error)) return false;
    size_t size=strlen(source);if(size>79) size=79;
    memset(out,0,80);memcpy(out,source,size);return true;
}
bool bot_weapon_projectile_value(const bot_weapon_config_cell *cell,qa_bot_projectile_info *out,
    qa_error *error) {
    if(!cell || !out || cell->size!=BOT_PROJECTILE_INFO_BYTES) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Projectile projection requires its source cell/output");return false;
    }
    *out=(qa_bot_projectile_info){0};
    if(!text(cell,0,out->name,error) || !text(cell,80,out->model,error)) return false;
#define INT(offset,field) if(!bot_weapon_config_int(cell,offset,&out->field,error)) return false
#define FLOAT(offset,field) if(!bot_weapon_config_float(cell,offset,&out->field,error)) return false
    INT(160,flags);FLOAT(164,gravity);INT(168,damage);FLOAT(172,radius);
    INT(176,visible_damage);INT(180,damage_type);INT(184,health_increase);
    FLOAT(188,push);FLOAT(192,detonation);FLOAT(196,bounce);FLOAT(200,bounce_friction);FLOAT(204,bounce_stop);
#undef INT
#undef FLOAT
    return true;
}
bool bot_weapon_info_value(const bot_weapon_config_cell *cell,qa_bot_weapon_info *out,
    qa_bot_projectile_info *projectile,qa_error *error) {
    if(!cell || !out || !projectile || cell->size!=BOT_WEAPON_INFO_BYTES || cell->offset>UINT32_MAX-344) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Weapon projection requires its source cell/outputs");return false;
    }
    *out=(qa_bot_weapon_info){.projectile_index=QA_BOT_NO_INDEX};int32_t valid;
    if(!bot_weapon_config_int(cell,0,&valid,error)) return false;
    out->valid=valid!=0;
#define INT(offset,field) if(!bot_weapon_config_int(cell,offset,&out->field,error)) return false
#define FLOAT(offset,field) if(!bot_weapon_config_float(cell,offset,&out->field,error)) return false
#define TEXT(offset,field) if(!text(cell,offset,out->field,error)) return false
#define VECTOR(offset,field) if(!bot_weapon_config_vector(cell,offset,&out->field,error)) return false
    INT(4,number);TEXT(8,name);TEXT(88,model);INT(168,level);INT(172,weapon_inventory);
    INT(176,flags);TEXT(180,projectile);INT(260,projectile_count);
    FLOAT(264,horizontal_spread);FLOAT(268,vertical_spread);FLOAT(272,speed);FLOAT(276,acceleration);
    VECTOR(280,recoil);VECTOR(292,offset);VECTOR(304,angle_offset);FLOAT(316,extra_z_velocity);
    INT(320,ammo_amount);INT(324,ammo_inventory);FLOAT(328,activate);FLOAT(332,reload);
    FLOAT(336,spin_up);FLOAT(340,spin_down);
#undef INT
#undef FLOAT
#undef TEXT
#undef VECTOR
    bot_weapon_config_cell embedded={.record=cell->record,.offset=cell->offset+344,
        .size=BOT_PROJECTILE_INFO_BYTES};
    return bot_weapon_projectile_value(&embedded,projectile,error);
}
bool bot_weapon_resource_info(const bot_weapon_resource *resource,uint32_t index,
    qa_bot_weapon_info *out,qa_bot_projectile_info *projectile,qa_error *error) {
    if(!resource || !resource->bound) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Weapon projection has no bound source resource");return false;
    }
    bot_weapon_config_cell cell;
    return bot_weapon_config_weapon(&resource->record,index,&cell,error) &&
        bot_weapon_info_value(&cell,out,projectile,error);
}
