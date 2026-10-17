#include "qa/bot_knowledge.h"
#include "qa/common_parse.h"
#include "qa/text.h"
#include "qa/id_index.h"
#include "qa/localization.h"
#include <float.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { SOURCE_INTERACTION_FLAGS=1, SOURCE_INTERACTION_HEALTH=2, SOURCE_INTERACTION_TARGET=4 };
typedef struct source_interaction {
    qa_bot_source_interaction kind;
    uint32_t spawnflags;
    uint8_t conditions, expected;
    bool any, exact_flags;
} source_interaction;
struct qa_bot_source_knowledge {
    qa_arena storage;
    qa_strings *strings;
    qa_bot_source_document documents[10];
    size_t document_count;
    qa_bot_source_skill *skills;
    size_t skill_count,skill_capacity;
    qa_bot_source_character *characters;
    size_t character_count;
    qa_id_index weapon_items;
    qa_bot_source_weapon *weapons;
    qa_id_index item_names;
    qa_bot_source_item *items;
    qa_id_index chat_types;
    qa_bot_source_chat *chats;
    qa_id_index interaction_names;
    source_interaction *interactions;
};
static void *reserve(qa_bot_source_knowledge *knowledge,void *old,size_t *capacity,
    size_t needed,size_t stride,size_t alignment,qa_error *error) {
    if(needed<=*capacity) return old;
    size_t count=*capacity?*capacity:8;
    while(count<needed && count<=SIZE_MAX/2) count*=2;
    if(count<needed || count>SIZE_MAX/stride) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Bot source records exceed addressable storage");return NULL;
    }
    void *out=qa_arena_grow(&knowledge->storage,old,*capacity*stride,count*stride,alignment,error);
    if(out) *capacity=count;
    return out;
}
/* The common unsigned parser owns token semantics. This framing adds only the
 * physical-line fields and brace separators used by the shipped bot data. */
static bool line_tokens(qa_bot_source_knowledge *knowledge,qa_bytes line,qa_arena *scratch,
    qa_string_id **out,size_t *count,qa_error *error) {
    qa_arena_reset(scratch);*count=0;*out=NULL;
    if(line.size>(SIZE_MAX-1)/3) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Bot source line exceeds addressable storage");return false;
    }
    uint8_t *text=qa_arena_alloc(scratch,line.size*3+1,1,error);
    if(!text) return false;
    bool quoted=false;size_t written=0;
    for(size_t i=0;i<line.size;++i) {
        uint8_t byte=line.data[i];
        if(byte=='"') quoted=!quoted;
        if(!quoted && byte=='/' && i+1<line.size && line.data[i+1]=='/') break;
        bool brace=!quoted && (byte=='{' || byte=='}');
        if(brace) text[written++]=' ';
        text[written++]=byte;
        if(brace) text[written++]=' ';
    }
    text[written]=0;
    qa_common_parser parser={0};qa_common_cursor cursor;
    if(!qa_common_cursor_init(&cursor,(qa_bytes){text,written},QA_COMMON_TERMINATED,error)) return false;
    cursor.rule=QA_COMMON_SHADER;
    size_t capacity=0;qa_string_id *tokens=NULL;
    while(!cursor.ended) {
        if(!qa_common_parse(&parser,&cursor,true,error)) return false;
        if(!cursor.token.found) break;
        if(*count==capacity) {
            size_t next=capacity?capacity*2:8;
            if(next<capacity || next>SIZE_MAX/sizeof(*tokens)) {
                qa_error_set(error,QA_ERROR_MEMORY,0,"Bot source tokens exceed addressable storage");return false;
            }
            qa_string_id *grown=qa_arena_grow(scratch,tokens,capacity*sizeof(*tokens),
                next*sizeof(*tokens),_Alignof(qa_string_id),error);
            if(!grown) return false;
            tokens=grown;capacity=next;
        }
        if(!qa_strings_intern(knowledge->strings,
                (qa_bytes){(const uint8_t *)parser.token,parser.token_length},tokens+*count,error)) return false;
        ++*count;
    }
    *out=tokens;return true;
}
static qa_string_id *copy_ids(qa_bot_source_knowledge *knowledge,const qa_string_id *source,
    size_t count,qa_error *error) {
    if(!count) return NULL;
    qa_string_id *out=qa_arena_alloc(&knowledge->storage,count*sizeof(*out),_Alignof(qa_string_id),error);
    if(out) memcpy(out,source,count*sizeof(*out));
    return out;
}
static bool document_parse(qa_bot_source_knowledge *knowledge,qa_bot_source_document *document,qa_error *error) {
    qa_arena scratch={0};qa_bot_source_block current={0},*blocks=NULL;
    qa_bot_source_field *fields=NULL;qa_string_id *header=NULL;
    size_t field_capacity=0,header_capacity=0,header_count=0,block_capacity=0;
    qa_string_id open,close;
    if(!qa_strings_intern_cstr(knowledge->strings,"{",&open,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"}",&close,error)) return false;
    bool inside=false,okay=true;uint32_t number=1;
    for(size_t at=0;okay && at<document->source.size;++number) {
        size_t end=at;while(end<document->source.size && document->source.data[end]!='\n') ++end;
        qa_string_id *tokens;size_t count;
        okay=line_tokens(knowledge,(qa_bytes){document->source.data+at,end-at},&scratch,&tokens,&count,error);
        at=end<document->source.size?end+1:end;
        if(!okay || !count) continue;
        if(!inside) {
            bool opens=tokens[count-1]==open;
            size_t words=count-(opens?1u:0u);
            for(size_t i=0;i<words;++i) if(tokens[i]==open || tokens[i]==close) {okay=false;break;}
            if(!okay) break;
            qa_string_id *grown=reserve(knowledge,header,&header_capacity,header_count+words,
                sizeof(*header),_Alignof(qa_string_id),error);
            if(header_count+words && !grown) {okay=false;break;}
            header=grown;if(words) memcpy(header+header_count,tokens,words*sizeof(*tokens));header_count+=words;
            if(opens) {
                current=(qa_bot_source_block){.header=copy_ids(knowledge,header,header_count,error),
                    .header_count=header_count,.line=number};
                if(header_count && !current.header) {okay=false;break;}
                header_count=0;fields=NULL;field_capacity=0;inside=true;
            }
            continue;
        }
        if(count==1 && tokens[0]==close) {
            qa_bot_source_block *grown=reserve(knowledge,blocks,&block_capacity,document->block_count+1,
                sizeof(*blocks),_Alignof(qa_bot_source_block),error);
            if(!grown) {okay=false;break;}
            blocks=grown;current.fields=fields;blocks[document->block_count++]=current;inside=false;continue;
        }
        int32_t depth=0;
        for(size_t i=1;i<count;++i) {
            if(tokens[i]==open) ++depth;
            else if(tokens[i]==close && --depth<0) break;
        }
        if(tokens[0]==open || tokens[0]==close || depth!=0) {okay=false;break;}
        qa_bot_source_field *grown=reserve(knowledge,fields,&field_capacity,current.field_count+1,
            sizeof(*fields),_Alignof(qa_bot_source_field),error);
        if(!grown) {okay=false;break;}
        fields=grown;const qa_string_id *values=copy_ids(knowledge,tokens+1,count-1,error);
        if(count>1 && !values) {okay=false;break;}
        fields[current.field_count++]=(qa_bot_source_field){.key=tokens[0],.values=values,.count=count-1,.line=number};
    }
    if(inside || header_count) okay=false;
    document->blocks=blocks;qa_arena_destroy(&scratch);
    if(!okay && (!error || !error->code))
        qa_error_set(error,QA_ERROR_FORMAT,number,"Malformed bot source block");
    return okay;
}
typedef struct setting_field {const char *name;size_t offset;bool boolean;} setting_field;
#define FLOAT(name,field) {name,offsetof(qa_bot_source_skill,field),false}
#define BOOL(name,field) {name,offsetof(qa_bot_source_skill,field),true}
static const setting_field setting_fields[]={
    BOOL("behaviors.allow_combat",allow_combat),
    BOOL("behaviors.allow_melee",allow_melee),
    BOOL("behaviors.allow_grab_items",items.allow_grab_items),
    BOOL("behaviors.allow_grab_items_in_combat",items.allow_grab_items_in_combat),
    BOOL("behaviors.combat.allow_grab_items",items.allow_grab_items_in_combat),
    BOOL("behaviors.allow_grab_power_items",items.allow_grab_power_items),
    BOOL("behaviors.defer_power_items_to_humans",items.defer_power_items_to_humans),
    FLOAT("behaviors.min_respawn_time",min_respawn_time),
    FLOAT("behaviors.max_respawn_time",max_respawn_time),
    BOOL("behaviors.combat.grab_weapons",items.combat_grab_weapons),
    FLOAT("behaviors.combat.max_item_dist",items.combat_max_item_distance),
    FLOAT("behaviors.combat.min_health_pct",items.combat_min_health_pct),
    FLOAT("behaviors.combat.min_armor_pct",items.combat_min_armor_pct),
    FLOAT("behaviors.combat.min_ammo_pct",items.combat_min_ammo_pct),
    BOOL("aiming.lead_targets",lead_targets),
    FLOAT("aiming.max_acceleration",aiming.maximum_speed),FLOAT("aiming.spring_stiffness",aiming.spring_stiffness),
    FLOAT("aiming.damping",aiming.damping),FLOAT("aiming.velocity_offset",aiming.velocity_offset),
    FLOAT("aiming.modifier.max_angle",aiming.modifier_maximum_angle),FLOAT("aiming.modifier.apply_time",aiming.modifier_apply_time),
    FLOAT("aiming.modifier.accel_scalar",aiming.modifier_acceleration_scalar),
    FLOAT("aiming.modifier.spring_scalar",aiming.modifier_spring_scalar),FLOAT("aiming.modifier.damping_scalar",aiming.modifier_damping_scalar),
    FLOAT("senses.fov_angle",senses.fov_angle),FLOAT("senses.sight_time",senses.sight_time),
    FLOAT("senses.sight_decay_time",senses.sight_decay_time),FLOAT("senses.sound_time",senses.sound_time),
    FLOAT("senses.sound_decay_time",senses.sound_decay_time),FLOAT("senses.forget_non_vis_enemy_time",senses.forget_non_visible_time),
    FLOAT("senses.sound_persist_time",senses.sound_persist_time),FLOAT("senses.sound_range",senses.sound_range),
    FLOAT("senses.invis_enemy_sight_scalar",senses.invisible_sight_scalar),
    FLOAT("senses.max_invis_enemy_sight_dist",senses.maximum_invisible_distance),
    FLOAT("weapons.fov_angle",weapons.fov_angle),FLOAT("weapons.sight_time",weapons.sight_time),
    FLOAT("weapons.decay_time",weapons.decay_time),FLOAT("weapons.fov_scalar",weapon_fov_scalar),
    BOOL("movement.allow_jumping_in_combat",movement.allow_combat_jumping),BOOL("movement.walk_only",movement.walk_only),
    BOOL("movement.allow_rocket_jumping",movement.allow_rocket_jumping),
    FLOAT("movement.jump_chance",movement.jump_chance),FLOAT("movement.jump_cooldown",movement.jump_cooldown)
};
#undef FLOAT
#undef BOOL
static bool skills_parse(qa_bot_source_knowledge *knowledge,const qa_bot_source_document *document,qa_error *error) {
    qa_string_id skill_word,true_word,false_word,keys[sizeof(setting_fields)/sizeof(*setting_fields)];
    if(!qa_strings_intern_cstr(knowledge->strings,"skill",&skill_word,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"true",&true_word,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"false",&false_word,error)) return false;
    for(size_t i=0;i<sizeof(keys)/sizeof(*keys);++i)
        if(!qa_strings_intern_cstr(knowledge->strings,setting_fields[i].name,keys+i,error)) return false;
    for(size_t i=0;i<document->block_count;++i) {
        const qa_bot_source_block *block=document->blocks+i;
        if(block->header_count!=2 || block->header[0]!=skill_word) continue;
        qa_bot_source_skill *grown=reserve(knowledge,knowledge->skills,&knowledge->skill_capacity,
            knowledge->skill_count+1,sizeof(*grown),_Alignof(qa_bot_source_skill),error);
        if(!grown) return false;
        knowledge->skills=grown;qa_bot_source_skill *skill=grown+knowledge->skill_count++;
        *skill=(qa_bot_source_skill){.name=block->header[1],.items={.combat_grab_weapons=true,
            .combat_max_item_distance=512,.combat_min_health_pct=100,
            .combat_min_armor_pct=100,.combat_min_ammo_pct=100}};
        for(size_t j=0;j<block->field_count;++j) {
            const qa_bot_source_field *field=block->fields+j;
            for(size_t k=0;k<sizeof(keys)/sizeof(*keys);++k) if(field->key==keys[k]) {
                if(field->count!=1) break;
                uint8_t *target=(uint8_t *)skill+setting_fields[k].offset;
                if(setting_fields[k].boolean) {
                    if(field->values[0]==true_word || field->values[0]==false_word)
                        *(bool *)target=field->values[0]==true_word;
                } else {
                    double value;
                    if(qa_parse_number(qa_strings_text(knowledge->strings,field->values[0]),&value,NULL) &&
                       isfinite(value) && value>=-FLT_MAX && value<=FLT_MAX) *(float *)target=(float)value;
                }
                break;
            }
        }
    }
    for(size_t i=0;i<knowledge->skill_count;++i)
        knowledge->skills[i].best_aim_head=i+2>=knowledge->skill_count;
    return true;
}
static bool characters_parse(qa_bot_source_knowledge *knowledge,const qa_bot_source_document *document,
    qa_error *error)
{
    static const struct {const char *name;size_t offset;} texts[]={
        {"fun_name",offsetof(qa_bot_source_character,fun_name)},
        {"name",offsetof(qa_bot_source_character,name)},
        {"skin",offsetof(qa_bot_source_character,skin)},
        {"dogtag",offsetof(qa_bot_source_character,dogtag)}};
    qa_string_id keys[sizeof(texts)/sizeof(*texts)],shirt,pants;
    for(size_t i=0;i<sizeof(keys)/sizeof(*keys);++i)
        if(!qa_strings_intern_cstr(knowledge->strings,texts[i].name,keys+i,error)) return false;
    if(!qa_strings_intern_cstr(knowledge->strings,"shirt_color",&shirt,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"pants_color",&pants,error)) return false;
    size_t count=document->block_count;
    knowledge->characters=count?qa_arena_alloc(&knowledge->storage,
        count*sizeof(*knowledge->characters),_Alignof(qa_bot_source_character),error):NULL;
    if(count && !knowledge->characters) return false;
    knowledge->character_count=count;
    for(size_t i=0;i<count;++i) {
        qa_bot_source_character *character=knowledge->characters+i;
        *character=(qa_bot_source_character){0};
        const qa_bot_source_block *block=document->blocks+i;
        for(size_t j=0;j<block->field_count;++j) {
            const qa_bot_source_field *field=block->fields+j;
            if(field->count!=1) continue;
            for(size_t k=0;k<sizeof(keys)/sizeof(*keys);++k) if(field->key==keys[k])
                *(qa_string_id *)((uint8_t *)character+texts[k].offset)=field->values[0];
            if(field->key==shirt || field->key==pants) {
                double value;
                if(qa_parse_number(qa_strings_text(knowledge->strings,field->values[0]),&value,NULL) &&
                   isfinite(value) && value>=-FLT_MAX && value<=FLT_MAX)
                    *(field->key==shirt?&character->shirt_color:&character->pants_color)=(float)value;
            }
        }
    }
    return true;
}
typedef struct source_item_weapon {
    qa_string_id name,ammo;
    qa_item_id item;
} source_item_weapon;
static bool weapons_parse(qa_bot_source_knowledge *knowledge,const qa_bot_source_document *document,
    qa_ruleset_id rules,qa_bot_source_weapon_resolve resolve,void *context,
    qa_arena *binding_storage,source_item_weapon **bindings,qa_error *error)
{
    knowledge->weapon_items.storage=&knowledge->storage;
    if(!qa_id_index_reserve(&knowledge->weapon_items,document->block_count,error)) return false;
    knowledge->weapons=document->block_count?qa_arena_alloc(&knowledge->storage,
        document->block_count*sizeof(*knowledge->weapons),_Alignof(qa_bot_source_weapon),error):NULL;
    if(document->block_count && !knowledge->weapons) return false;
    *bindings=document->block_count?qa_arena_alloc(binding_storage,
        document->block_count*sizeof(**bindings),_Alignof(source_item_weapon),error):NULL;
    if(document->block_count && !*bindings) return false;
    static const struct {const char *name;size_t offset;} numbers[]={
        {"ideal_fov",offsetof(qa_bot_source_weapon,ideal_fov)},
        {"priority",offsetof(qa_bot_source_weapon,priority)},
        {"min_range",offsetof(qa_bot_source_weapon,min_range)},
        {"max_range",offsetof(qa_bot_source_weapon,max_range)},
        {"min_height",offsetof(qa_bot_source_weapon,min_height)},
        {"max_height",offsetof(qa_bot_source_weapon,max_height)},
        {"min_ammo",offsetof(qa_bot_source_weapon,min_ammo)},
        {"max_ammo",offsetof(qa_bot_source_weapon,maximum_ammo)},
        {"trigger_hold",offsetof(qa_bot_source_weapon,trigger_hold)},
        {"trigger_cooldown",offsetof(qa_bot_source_weapon,trigger_cooldown)}};
    qa_string_id name_key,number_key,flags_key,ammo_key,trigger_key,aim_key,
        melee,electric,explosive,none,continuous,release;
    qa_string_id points[QA_BOT_AIM_BEST+1];
    static const char *const point_names[]={"center","head","feet","best"};
    qa_string_id keys[sizeof(numbers)/sizeof(*numbers)];
    if(!qa_strings_intern_cstr(knowledge->strings,"name",&name_key,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"number",&number_key,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"flags",&flags_key,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"trigger_type",&trigger_key,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"aim_point",&aim_key,error) ||
       !qa_strings_intern_cstr(knowledge->strings,rules==QA_RULESET_NETQUAKE?"ammo":"ammo_name",&ammo_key,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"melee",&melee,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"electric",&electric,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"explosive",&explosive,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"continuous",&continuous,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"hold_and_release",&release,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"none",&none,error)) return false;
    for(size_t i=0;i<sizeof(keys)/sizeof(*keys);++i)
        if(!qa_strings_intern_cstr(knowledge->strings,numbers[i].name,keys+i,error)) return false;
    for(size_t i=0;i<sizeof(points)/sizeof(*points);++i)
        if(!qa_strings_intern_cstr(knowledge->strings,point_names[i],points+i,error)) return false;
    for(size_t i=0;i<document->block_count;++i) {
        qa_string_id name=0;uint32_t number=0;
        source_item_weapon *binding=*bindings+i;*binding=(source_item_weapon){0};
        qa_bot_source_weapon *weapon=knowledge->weapons+i;
        *weapon=(qa_bot_source_weapon){.ordinal=i};
        const qa_bot_source_block *block=document->blocks+i;
        for(size_t j=0;j<block->field_count;++j) {
            const qa_bot_source_field *field=block->fields+j;
            if(field->key==flags_key) {
                weapon->melee=weapon->electric=weapon->explosive=false;
                for(size_t k=0;k<field->count;++k) {
                    weapon->melee|=field->values[k]==melee;
                    weapon->electric|=field->values[k]==electric;
                    weapon->explosive|=field->values[k]==explosive;
                }
                continue;
            }
            if(field->count!=1) continue;
            if(field->key==name_key) name=field->values[0];
            else if(field->key==aim_key) {
                weapon->aim_point=QA_BOT_AIM_CENTER;
                for(size_t k=0;k<sizeof(points)/sizeof(*points);++k)
                    if(field->values[0]==points[k]) {weapon->aim_point=(qa_bot_aim_point)k;break;}
            }
            else if(field->key==trigger_key) {
                if(field->values[0]==continuous || field->values[0]==release)
                    weapon->hold_and_release=field->values[0]==release;
            }
            else if(field->key==ammo_key) {
                binding->ammo=field->values[0];
                weapon->needs_ammo=field->values[0]!=none &&
                    qa_strings_text(knowledge->strings,field->values[0]).size!=0;
            }
            else if(field->key==number_key) {
                double value;
                if(qa_parse_number(qa_strings_text(knowledge->strings,field->values[0]),&value,NULL) &&
                   isfinite(value) && value>=0 && value<=UINT32_MAX && trunc(value)==value) number=(uint32_t)value;
            } else for(size_t k=0;k<sizeof(keys)/sizeof(*keys);++k) if(field->key==keys[k]) {
                double value;
                if(qa_parse_number(qa_strings_text(knowledge->strings,field->values[0]),&value,NULL) &&
                   isfinite(value) && value>=-FLT_MAX && value<=FLT_MAX)
                    *(float *)((uint8_t *)weapon+numbers[k].offset)=(float)value;
                break;
            }
        }
        qa_item_id key=0;
        if(resolve) {
            if(!resolve(context,name,number,&key,error)) return false;
        } else if(name && rules!=QA_RULESET_NETQUAKE) {
            qa_bytes source=qa_strings_text(knowledge->strings,name);
            char *item=qa_arena_alloc(&knowledge->storage,source.size+4,1,error);
            if(!item) return false;
            memcpy(item,"q2:",3);memcpy(item+3,source.data,source.size);item[source.size+3]=0;
            if(!qa_strings_intern_cstr(knowledge->strings,item,&key,error)) return false;
        }
        binding->name=name;binding->item=key;
        if(!key) continue;
        qa_id_index_entry *old=qa_id_index_find(&knowledge->weapon_items,key,0);
        if(old) old->value=i;
        else qa_id_index_insert(&knowledge->weapon_items,key,0,i);
    }
    return true;
}
static size_t weapon_item_match(qa_bytes item,qa_bytes weapon)
{
    if(item.size>=7 && !memcmp(item.data,"weapon_",7)) {item.data+=7;item.size-=7;}
    size_t a=0,b=0,length=0;
    while(a<item.size && b<weapon.size) {
        if(item.data[a]=='_') {++a;continue;}
        if(weapon.data[b]=='_') {++b;continue;}
        if(item.data[a++]!=weapon.data[b++]) return 0;
    }
    while(a<item.size && item.data[a]=='_') ++a;
    while(b<weapon.size && weapon.data[b]=='_') ++b;
    if(a<item.size && b<weapon.size) return 0;
    for(size_t i=0;i<weapon.size;++i) length+=weapon.data[i]!='_';
    return length;
}
static bool items_parse(qa_bot_source_knowledge *knowledge,const qa_bot_source_document *document,
    qa_ruleset_id rules,const source_item_weapon *weapons,size_t weapon_count,qa_error *error)
{
    qa_string_id name_key,flags_key,sight_key,spawnflags_key,team_key,equal,mega;
    static const struct {const char *name;uint32_t bit;} flags[]={
        {"health",QA_BOT_SOURCE_ITEM_HEALTH},{"armor",QA_BOT_SOURCE_ITEM_ARMOR},
        {"ammo",QA_BOT_SOURCE_ITEM_AMMO},{"weapon",QA_BOT_SOURCE_ITEM_WEAPON},
        {"powerup",QA_BOT_SOURCE_ITEM_POWERUP},{"mega_item",QA_BOT_SOURCE_ITEM_MEGA},
        {"backpack",QA_BOT_SOURCE_ITEM_BACKPACK},{"objective",QA_BOT_SOURCE_ITEM_OBJECTIVE},
        {"rune",QA_BOT_SOURCE_ITEM_RUNE}};
    qa_string_id flag_names[sizeof(flags)/sizeof(*flags)];
    if(!qa_strings_intern_cstr(knowledge->strings,"name",&name_key,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"flags",&flags_key,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"sight_dist",&sight_key,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"spawnflags",&spawnflags_key,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"team",&team_key,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"=",&equal,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"mega_item",&mega,error)) return false;
    for(size_t i=0;i<sizeof(flags)/sizeof(*flags);++i)
        if(!qa_strings_intern_cstr(knowledge->strings,flags[i].name,flag_names+i,error)) return false;
    knowledge->item_names.storage=&knowledge->storage;
    if(!qa_id_index_reserve(&knowledge->item_names,document->block_count,error)) return false;
    knowledge->items=document->block_count?qa_arena_alloc(&knowledge->storage,
        document->block_count*sizeof(*knowledge->items),_Alignof(qa_bot_source_item),error):NULL;
    if(document->block_count && !knowledge->items) return false;
    for(size_t i=0;i<document->block_count;++i) {
        qa_bot_source_item *item=knowledge->items+i;*item=(qa_bot_source_item){.sight_distance=FLT_MAX};
        qa_string_id name=0;const qa_string_id *item_flags=NULL;size_t flag_count=0;bool mega_seen=false;
        const qa_bot_source_block *block=document->blocks+i;
        for(size_t j=0;j<block->field_count;++j) {
            const qa_bot_source_field *field=block->fields+j;
            if(field->key==flags_key) {
                item->flags=0;item_flags=field->values;flag_count=field->count;
                for(size_t k=0;k<flag_count;++k) for(size_t n=0;n<sizeof(flags)/sizeof(*flags);++n)
                    if(item_flags[k]==flag_names[n]) item->flags|=flags[n].bit;
            } else if(field->key==spawnflags_key && field->count==3 && field->values[1]==equal &&
                      field->values[2]==mega && !mega_seen) {
                double value;
                if(qa_parse_number(qa_strings_text(knowledge->strings,field->values[0]),&value,NULL) &&
                   isfinite(value) && value>=INT32_MIN && value<=UINT32_MAX && trunc(value)==value) {
                    item->mega_spawnflag=(uint32_t)(int64_t)value;mega_seen=true;
                }
            } else if(field->count==1) {
                double value;
                if(field->key==name_key) name=field->values[0];
                else if((field->key==sight_key || field->key==team_key) &&
                        qa_parse_number(qa_strings_text(knowledge->strings,field->values[0]),&value,NULL) && isfinite(value)) {
                    if(field->key==sight_key && value>=-FLT_MAX && value<=FLT_MAX) item->sight_distance=(float)value;
                    else if(field->key==team_key && value>=INT32_MIN && value<=INT32_MAX && trunc(value)==value) {
                        item->team=(int32_t)value;item->has_team=true;
                    }
                }
            }
        }
        if(item->flags&QA_BOT_SOURCE_ITEM_WEAPON) {
            size_t best=0;
            for(size_t j=0;j<weapon_count;++j) {
                if(weapons[j].name==name) {item->weapon=weapons[j].item;break;}
                if(rules!=QA_RULESET_NETQUAKE) continue;
                size_t match=weapon_item_match(qa_strings_text(knowledge->strings,name),
                    qa_strings_text(knowledge->strings,weapons[j].name));
                if(match>best) {item->weapon=weapons[j].item;best=match;}
            }
        }
        if(item->flags&QA_BOT_SOURCE_ITEM_AMMO) {
            qa_item_id *matches=NULL;size_t capacity=0;
            for(size_t j=0;j<weapon_count;++j) {
                const qa_bot_source_weapon *weapon=qa_bot_source_weapon_read(knowledge,weapons[j].item);
                if(!weapon || !weapon->needs_ammo) continue;
                bool match=rules!=QA_RULESET_NETQUAKE?name==weapons[j].ammo:false;
                if(rules==QA_RULESET_NETQUAKE)
                    for(size_t k=0;k<flag_count;++k) match|=item_flags[k]==weapons[j].ammo;
                if(!match) continue;
                qa_item_id *next=reserve(knowledge,matches,&capacity,item->ammo_weapon_count+1,
                    sizeof(*matches),_Alignof(qa_item_id),error);
                if(!next) return false;
                matches=next;matches[item->ammo_weapon_count++]=weapons[j].item;
            }
            item->ammo_weapons=matches;
        }
        if(!name) continue;
        qa_id_index_entry *old=qa_id_index_find(&knowledge->item_names,name,0);
        if(old) old->value=i;
        else qa_id_index_insert(&knowledge->item_names,name,0,i);
    }
    return true;
}
static bool chat_text(qa_bot_source_knowledge *knowledge,const qa_localization *catalog,
    const char *key,const char *fallback,qa_string_id *out,qa_error *error)
{
    if(!qa_localization_find(catalog,key))
        return qa_strings_intern_cstr(knowledge->strings,fallback,out,error);
    size_t length=strlen(key);
    char *base=qa_arena_alloc(&knowledge->storage,length+2,1,error);
    if(!base) return false;
    base[0]='$';memcpy(base+1,key,length+1);
    qa_buffer text={0};
    return qa_localize_presentation_arena(catalog,base,NULL,0,false,&knowledge->storage,&text,error) &&
        qa_strings_intern(knowledge->strings,(qa_bytes){text.data,text.size},out,error);
}
static bool chats_parse(qa_bot_source_knowledge *knowledge,const qa_bot_source_document *document,
    qa_vfs *files,qa_ruleset_id rules,const char *language,qa_error *error)
{
    qa_localization_pool *pool=qa_localization_pool_create(error);
    if(!pool) return false;
    qa_localization *catalog=NULL;
    bool okay=qa_localization_acquire(pool,files,language && *language?language:"english",
        &(qa_localization_options){.profile=rules==QA_RULESET_NETQUAKE?
            QA_LOCALIZATION_Q1_RERELEASE:QA_LOCALIZATION_Q2_RERELEASE},&catalog,error);
    qa_string_id keys[5],yes;
    const char *const names[]={"type","locstring","time","chance","team"};
    for(size_t i=0;okay && i<5;++i)
        okay=qa_strings_intern_cstr(knowledge->strings,names[i],keys+i,error);
    if(okay) okay=qa_strings_intern_cstr(knowledge->strings,"true",&yes,error);
    knowledge->chat_types.storage=&knowledge->storage;
    if(okay) okay=qa_id_index_reserve(&knowledge->chat_types,document->block_count,error);
    if(okay && document->block_count) {
        knowledge->chats=qa_arena_alloc(&knowledge->storage,document->block_count*sizeof(*knowledge->chats),
            _Alignof(qa_bot_source_chat),error);
        okay=knowledge->chats!=NULL;
    }
    for(size_t i=0;okay && i<document->block_count;++i) {
        const qa_bot_source_block *block=document->blocks+i;
        qa_bot_source_chat *chat=knowledge->chats+i;*chat=(qa_bot_source_chat){0};
        qa_string_id name=0;
        for(size_t j=0;j<block->field_count;++j) {
            const qa_bot_source_field *field=block->fields+j;
            if(field->count!=1) continue;
            if(field->key==keys[0]) chat->type=field->values[0];
            else if(field->key==keys[1]) name=field->values[0];
            else if(field->key==keys[4]) chat->team_only=field->values[0]==yes;
            else {
                double value;
                if(!qa_parse_number(qa_strings_text(knowledge->strings,field->values[0]),&value,NULL) ||
                    !isfinite(value)) continue;
                if(field->key==keys[2]) chat->delay_ms=value;
                else if(field->key==keys[3] && value>=-FLT_MAX && value<=FLT_MAX) chat->chance=(float)value;
            }
        }
        const char *raw=name?qa_strings_cstr(knowledge->strings,name):"";
        const char *key=*raw=='$'?raw+1:raw;
        qa_string_id variants[64];size_t count=0;
        size_t length=strlen(key);
        char *numbered=qa_arena_alloc(&knowledge->storage,length+5,1,error);
        if(!numbered) {okay=false;break;}
        for(size_t j=0;j<64;++j) {
            snprintf(numbered,length+5,"%s_%zu",key,j);
            if(!qa_localization_find(catalog,numbered)) break;
            if(!chat_text(knowledge,catalog,numbered,numbered,variants+count,error)) {okay=false;break;}
            ++count;
        }
        chat->random_text=count!=0;
        if(okay && !count) {
            okay=chat_text(knowledge,catalog,key,rules==QA_RULESET_NETQUAKE?key:raw,variants,error);
            count=1;
        }
        if(okay) {
            chat->texts=copy_ids(knowledge,variants,count,error);chat->text_count=count;
            okay=chat->texts!=NULL;
        }
        if(okay && chat->type) qa_id_index_insert(&knowledge->chat_types,chat->type,0,i);
    }
    qa_localization_release(catalog);qa_localization_pool_destroy(pool);return okay;
}
static bool interactions_parse(qa_bot_source_knowledge *knowledge,const qa_bot_source_document *document,
    qa_error *error)
{
    static const char *const fields[]={"name","interaction","spawnflags","health","targetname","logic_op"};
    static const char *const kinds[]={"push","use","shoot"};
    qa_string_id keys[6],values[3],yes,no,any;
    for(size_t i=0;i<6;++i)
        if(!qa_strings_intern_cstr(knowledge->strings,fields[i],keys+i,error)) return false;
    for(size_t i=0;i<3;++i)
        if(!qa_strings_intern_cstr(knowledge->strings,kinds[i],values+i,error)) return false;
    if(!qa_strings_intern_cstr(knowledge->strings,"true",&yes,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"false",&no,error) ||
       !qa_strings_intern_cstr(knowledge->strings,"or",&any,error)) return false;
    knowledge->interaction_names.storage=&knowledge->storage;
    if(!qa_id_index_reserve(&knowledge->interaction_names,document->block_count,error)) return false;
    knowledge->interactions=document->block_count?qa_arena_alloc(&knowledge->storage,
        document->block_count*sizeof(*knowledge->interactions),_Alignof(source_interaction),error):NULL;
    if(document->block_count && !knowledge->interactions) return false;
    for(size_t i=0;i<document->block_count;++i) {
        source_interaction *row=knowledge->interactions+i;*row=(source_interaction){0};
        qa_string_id name=0;const qa_bot_source_block *block=document->blocks+i;
        for(size_t j=0;j<block->field_count;++j) {
            const qa_bot_source_field *field=block->fields+j;
            if(field->count!=1) continue;
            qa_string_id value=field->values[0];
            if(field->key==keys[0]) name=value;
            else if(field->key==keys[1]) {
                row->kind=QA_BOT_SOURCE_INTERACTION_NONE;
                for(size_t k=0;k<3;++k) if(value==values[k])
                    row->kind=(qa_bot_source_interaction)(QA_BOT_SOURCE_INTERACTION_PUSH+k);
            } else if(field->key==keys[2]) {
                double flags;
                if(qa_parse_number(qa_strings_text(knowledge->strings,value),&flags,NULL) && isfinite(flags)) {
                    int32_t mask=qa_source_to_i32(flags);
                    row->spawnflags=(uint32_t)mask;row->exact_flags=(double)mask==flags;
                    row->conditions|=SOURCE_INTERACTION_FLAGS;
                }
            } else if(field->key==keys[3] || field->key==keys[4]) {
                if(value==yes || value==no) {
                    uint8_t bit=field->key==keys[3]?SOURCE_INTERACTION_HEALTH:SOURCE_INTERACTION_TARGET;
                    row->conditions|=bit;
                    if(value==yes) row->expected|=bit;else row->expected&=(uint8_t)~bit;
                }
            } else if(field->key==keys[5]) row->any=value==any;
        }
        if(name) qa_id_index_insert(&knowledge->interaction_names,name,0,i);
    }
    return true;
}
static bool document_load(qa_bot_source_knowledge *knowledge,qa_vfs *files,const char *path,
    bool *found,qa_error *error) {
    *found=false;qa_resource *resource=NULL;qa_error local={0};
    if(!qa_vfs_acquire(files,path,&resource,NULL,&local)) {
        if(local.code==QA_ERROR_NOT_FOUND) return true;
        if(error) *error=local;
        return false;
    }
    qa_bytes bytes=qa_resource_bytes(resource);
    while(bytes.size && !bytes.data[bytes.size-1]) --bytes.size;
    uint8_t *copy=qa_arena_alloc(&knowledge->storage,bytes.size+1,1,error);
    if(!copy) {qa_resource_release(resource);return false;}
    if(bytes.size) memcpy(copy,bytes.data,bytes.size);
    copy[bytes.size]=0;
    qa_bot_source_document *document=knowledge->documents+knowledge->document_count;
    *document=(qa_bot_source_document){.source={copy,bytes.size}};
    bool okay=qa_strings_intern_cstr(knowledge->strings,path,&document->path,error);
    qa_resource_release(resource);
    if(okay) okay=document_parse(knowledge,document,error);
    if(okay) {++knowledge->document_count;*found=true;}
    return okay;
}
bool qa_bot_source_knowledge_load(qa_vfs *files,qa_strings *strings,qa_ruleset_id rules,const char *language,
    qa_bot_source_weapon_resolve resolve,void *context,
    qa_bot_source_knowledge **out,bool *found,qa_error *error) {
    if(!files || !strings || !out || !found) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Bot source knowledge requires mounted files and session strings");return false;
    }
    *found=false;qa_bot_source_knowledge *knowledge=calloc(1,sizeof(*knowledge));
    if(!knowledge) {qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating bot source knowledge");return false;}
    knowledge->strings=strings;bool present;qa_arena binding_storage={0};source_item_weapon *bindings=NULL;
    if(!document_load(knowledge,files,"bots/weapons.txt",&present,error)) goto failed;
    if(!present) goto missing;
    if(!weapons_parse(knowledge,knowledge->documents,rules,resolve,context,&binding_storage,&bindings,error)) goto failed;
    const char *const settings[]={"bots/settings_PC.txt","bots/settings_Consoles.txt","bots/settings_Nintendo.txt"};
    size_t count=rules==QA_RULESET_NETQUAKE?3:1;
    for(size_t i=0;i<count;++i) {
        if(!document_load(knowledge,files,settings[i],&present,error)) goto failed;
        if(present) break;
    }
    if(!present) goto missing;
    if(!skills_parse(knowledge,knowledge->documents+knowledge->document_count-1,error)) goto failed;
    if(!knowledge->skill_count) goto missing;
    const char *const optional[]={"bots/characters.txt","bots/items.txt","bots/monsters.txt","bots/interactables.txt",
        "bots/game_rules.txt","bots/teams.txt","bots/chats.txt","bots/dangers.txt"};
    for(size_t i=0;i<sizeof(optional)/sizeof(*optional);++i) {
        if(!document_load(knowledge,files,optional[i],&present,error)) goto failed;
        if(present && i==0 && !characters_parse(knowledge,knowledge->documents+knowledge->document_count-1,
            error)) goto failed;
        if(present && i==1 && !items_parse(knowledge,knowledge->documents+knowledge->document_count-1,
            rules,bindings,knowledge->documents[0].block_count,error)) goto failed;
        if(present && i==3 && !interactions_parse(knowledge,knowledge->documents+knowledge->document_count-1,
            error)) goto failed;
        if(present && i==6 && !chats_parse(knowledge,knowledge->documents+knowledge->document_count-1,
            files,rules,language,error)) goto failed;
    }
    qa_arena_destroy(&binding_storage);
    qa_arena_seal(&knowledge->storage);*out=knowledge;*found=true;return true;
missing:
    qa_arena_destroy(&binding_storage);qa_bot_source_knowledge_destroy(knowledge);return true;
failed:
    qa_arena_destroy(&binding_storage);qa_bot_source_knowledge_destroy(knowledge);return false;
}
void qa_bot_source_knowledge_destroy(qa_bot_source_knowledge *knowledge) {
    if(knowledge) {qa_id_index_destroy(&knowledge->weapon_items);qa_id_index_destroy(&knowledge->item_names);
        qa_id_index_destroy(&knowledge->chat_types);
        qa_id_index_destroy(&knowledge->interaction_names);
        qa_arena_destroy(&knowledge->storage);free(knowledge);}
}
const qa_bot_source_weapon *qa_bot_source_weapon_read(const qa_bot_source_knowledge *knowledge,qa_item_id item)
{
    if(!knowledge) return NULL;
    qa_id_index_cursor cursor=qa_id_index_begin(&knowledge->weapon_items,item,0);size_t at;
    return qa_id_index_next(&knowledge->weapon_items,&cursor,&at)?knowledge->weapons+at:NULL;
}
const qa_bot_source_item *qa_bot_source_item_read(const qa_bot_source_knowledge *knowledge,qa_string_id classname)
{
    if(!knowledge) return NULL;
    qa_id_index_cursor cursor=qa_id_index_begin(&knowledge->item_names,classname,0);size_t at;
    return qa_id_index_next(&knowledge->item_names,&cursor,&at)?knowledge->items+at:NULL;
}
qa_bot_source_interaction qa_bot_source_interaction_read(const qa_bot_source_knowledge *knowledge,
    qa_string_id classname,uint32_t spawnflags,bool has_health,bool has_targetname)
{
    if(!knowledge) return QA_BOT_SOURCE_INTERACTION_NONE;
    qa_id_index_cursor cursor=qa_id_index_begin(&knowledge->interaction_names,classname,0);
    size_t at,first=SIZE_MAX;qa_bot_source_interaction kind=QA_BOT_SOURCE_INTERACTION_NONE;
    while(qa_id_index_next(&knowledge->interaction_names,&cursor,&at)) {
        const source_interaction *row=knowledge->interactions+at;
        uint8_t matched=0;
        if(row->exact_flags && (spawnflags&row->spawnflags)==row->spawnflags) matched|=SOURCE_INTERACTION_FLAGS;
        if(has_health==((row->expected&SOURCE_INTERACTION_HEALTH)!=0)) matched|=SOURCE_INTERACTION_HEALTH;
        if(has_targetname==((row->expected&SOURCE_INTERACTION_TARGET)!=0)) matched|=SOURCE_INTERACTION_TARGET;
        bool matches=!row->conditions || (row->any?(matched&row->conditions)!=0:
            (matched&row->conditions)==row->conditions);
        /* The shared index visits newest first; authored rules match first. */
        if(matches && at<first) {first=at;kind=row->kind;}
    }
    return kind;
}
size_t qa_bot_source_chat_count(const qa_bot_source_knowledge *knowledge,qa_string_id type)
{
    if(!knowledge) return 0;
    qa_id_index_cursor cursor=qa_id_index_begin(&knowledge->chat_types,type,0);size_t at,count=0;
    while(qa_id_index_next(&knowledge->chat_types,&cursor,&at)) ++count;
    return count;
}
const qa_bot_source_chat *qa_bot_source_chat_at(const qa_bot_source_knowledge *knowledge,
    qa_string_id type,size_t index)
{
    size_t count=qa_bot_source_chat_count(knowledge,type);
    if(index>=count) return NULL;
    qa_id_index_cursor cursor=qa_id_index_begin(&knowledge->chat_types,type,0);size_t at;
    /* Bucket chains run newest first; callers observe authored admission order. */
    for(size_t reverse=count-1;qa_id_index_next(&knowledge->chat_types,&cursor,&at);--reverse)
        if(reverse==index) return knowledge->chats+at;
    return NULL;
}
size_t qa_bot_source_skill_count(const qa_bot_source_knowledge *knowledge) {return knowledge?knowledge->skill_count:0;}
const qa_bot_source_skill *qa_bot_source_skill_at(const qa_bot_source_knowledge *knowledge,size_t index) {
    return knowledge && index<knowledge->skill_count?knowledge->skills+index:NULL;
}
size_t qa_bot_source_character_count(const qa_bot_source_knowledge *knowledge) {
    return knowledge?knowledge->character_count:0;
}
const qa_bot_source_character *qa_bot_source_character_at(const qa_bot_source_knowledge *knowledge,size_t index) {
    return knowledge && index<knowledge->character_count?knowledge->characters+index:NULL;
}
size_t qa_bot_source_document_count(const qa_bot_source_knowledge *knowledge) {return knowledge?knowledge->document_count:0;}
const qa_bot_source_document *qa_bot_source_document_at(const qa_bot_source_knowledge *knowledge,size_t index) {
    return knowledge && index<knowledge->document_count?knowledge->documents+index:NULL;
}
