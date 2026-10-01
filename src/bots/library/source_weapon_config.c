#include "source_weapon_config.h"
#include <limits.h>
#include <string.h>

static bool fail(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static uint32_t read_word(const uint8_t *bytes) {
    return (uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
}
static void write_word(uint8_t *bytes,uint32_t value) {
    for(uint32_t i=0;i<4;++i) bytes[i]=(uint8_t)(value>>(8*i));
}
static int32_t signed_word(uint32_t value) {
    return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);
}
static bool cell(const bot_weapon_config_record *record,uint32_t offset,uint32_t size,
    bot_weapon_config_cell *out,qa_error *error) {
    if(!record || !out) return fail(error,"Weapon configuration cell requires actual owners/output");
    qa_bot_memory_span bytes;
    if(!qa_bot_memory_bytes(record->memory,record->allocation,&bytes,error)) return false;
    if(offset>bytes.size || size>bytes.size-offset)
        return fail(error,"Weapon configuration cell exceeds its actual allocation");
    *out=(bot_weapon_config_cell){.record=*record,.offset=offset,.size=size};return true;
}
bool bot_weapon_config_span(const bot_weapon_config_cell *value,qa_bot_memory_span *out,qa_error *error) {
    if(!value || !out) return fail(error,"Weapon configuration bytes require their cell/output");
    if(!qa_bot_memory_bytes(value->record.memory,value->record.allocation,out,error)) return false;
    if(value->offset>out->size || value->size>out->size-value->offset)
        return fail(error,"Weapon configuration view exceeds its actual allocation");
    out->data+=value->offset;out->size=value->size;return true;
}
static bool field(const bot_weapon_config_cell *value,uint32_t offset,uint32_t size,
    qa_bot_memory_span *out,qa_error *error) {
    if(!bot_weapon_config_span(value,out,error)) return false;
    if(offset>out->size || size>out->size-offset)
        return fail(error,"Weapon configuration field exceeds its source cell");
    out->data+=offset;out->size=size;return true;
}
bool bot_weapon_config_int(const bot_weapon_config_cell *value,uint32_t offset,int32_t *out,qa_error *error) {
    if(!out) return fail(error,"Weapon configuration integer requires its output");
    qa_bot_memory_span bytes;if(!field(value,offset,4,&bytes,error)) return false;
    *out=signed_word(read_word(bytes.data));return true;
}
bool bot_weapon_config_set_int(const bot_weapon_config_cell *value,uint32_t offset,int32_t input,qa_error *error) {
    qa_bot_memory_span bytes;if(!field(value,offset,4,&bytes,error)) return false;
    write_word(bytes.data,(uint32_t)input);return true;
}
bool bot_weapon_config_float(const bot_weapon_config_cell *value,uint32_t offset,float *out,qa_error *error) {
    if(!out) return fail(error,"Weapon configuration float requires its output");
    qa_bot_memory_span bytes;if(!field(value,offset,4,&bytes,error)) return false;
    uint32_t word=read_word(bytes.data);memcpy(out,&word,4);return true;
}
bool bot_weapon_config_set_float(const bot_weapon_config_cell *value,uint32_t offset,float input,qa_error *error) {
    qa_bot_memory_span bytes;if(!field(value,offset,4,&bytes,error)) return false;
    uint32_t word;memcpy(&word,&input,4);write_word(bytes.data,word);return true;
}
bool bot_weapon_config_clear(const bot_weapon_config_cell *value,qa_error *error) {
    qa_bot_memory_span bytes;if(!bot_weapon_config_span(value,&bytes,error)) return false;
    memset(bytes.data,0,bytes.size);return true;
}
static bool text_span(const bot_weapon_config_cell *value,uint32_t offset,qa_bot_memory_span *out,qa_error *error) {
    if(!bot_weapon_config_span(value,out,error)) return false;
    uint32_t start=offset<out->size?offset:out->size;
    out->data+=start;out->size-=start;if(out->size>80) out->size=80;return true;
}
bool bot_weapon_config_text(const bot_weapon_config_cell *value,uint32_t offset,char out[81],qa_error *error) {
    if(!out) return fail(error,"Weapon configuration text requires its output");
    qa_bot_memory_span bytes;if(!text_span(value,offset,&bytes,error)) return false;
    uint32_t size=0;while(size<bytes.size && bytes.data[size]) {out[size]=(char)bytes.data[size];++size;}
    out[size]=0;return true;
}
bool bot_weapon_config_set_text(const bot_weapon_config_cell *value,uint32_t offset,qa_bytes input,qa_error *error) {
    if(input.size && !input.data) return fail(error,"Weapon configuration text span is absent");
    qa_bot_memory_span bytes;if(!text_span(value,offset,&bytes,error)) return false;
    memset(bytes.data,0,bytes.size);
    size_t size=input.size<79?input.size:79;
    for(size_t i=0;i<size && input.data[i];++i) if(i<bytes.size) bytes.data[i]=input.data[i];
    return true;
}
bool bot_weapon_config_vector(const bot_weapon_config_cell *value,uint32_t offset,qa_vec3 *out,qa_error *error) {
    if(!out || offset>UINT32_MAX-8) return fail(error,"Weapon configuration vector requires its field/output");
    return bot_weapon_config_float(value,offset,&out->x,error) &&
        bot_weapon_config_float(value,offset+4,&out->y,error) &&
        bot_weapon_config_float(value,offset+8,&out->z,error);
}
bool bot_weapon_config_set_vector(const bot_weapon_config_cell *value,uint32_t offset,qa_vec3 input,qa_error *error) {
    if(offset>UINT32_MAX-8) return fail(error,"Weapon configuration vector field overflows");
    return bot_weapon_config_set_float(value,offset,input.x,error) &&
        bot_weapon_config_set_float(value,offset+4,input.y,error) &&
        bot_weapon_config_set_float(value,offset+8,input.z,error);
}
bool bot_weapon_config_header(const bot_weapon_config_record *record,bot_weapon_config_cell *out,qa_error *error) {
    return cell(record,0,BOT_WEAPON_CONFIG_BYTES,out,error);
}
bool bot_weapon_config_weapon(const bot_weapon_config_record *record,uint32_t index,
    bot_weapon_config_cell *out,qa_error *error) {
    uint64_t offset=BOT_WEAPON_CONFIG_BYTES+(uint64_t)index*BOT_WEAPON_INFO_BYTES;
    return offset<=UINT32_MAX?cell(record,(uint32_t)offset,BOT_WEAPON_INFO_BYTES,out,error):
        fail(error,"Weapon info copy exceeds the source configuration allocation");
}
bool bot_weapon_config_projectile(const bot_weapon_config_record *record,uint32_t index,
    bot_weapon_config_cell *out,qa_error *error) {
    bot_weapon_config_cell header;int32_t capacity;
    if(!bot_weapon_config_header(record,&header,error) || !bot_weapon_config_int(&header,0,&capacity,error)) return false;
    return bot_weapon_config_projectile_at(record,(uint32_t)capacity,index,out,error);
}
bool bot_weapon_config_projectile_at(const bot_weapon_config_record *record,uint32_t capacity,uint32_t index,
    bot_weapon_config_cell *out,qa_error *error) {
    uint32_t start=BOT_WEAPON_CONFIG_BYTES+capacity*BOT_WEAPON_INFO_BYTES;
    uint64_t offset=(uint64_t)start+(uint64_t)index*BOT_PROJECTILE_INFO_BYTES;
    return offset<=UINT32_MAX?cell(record,(uint32_t)offset,BOT_PROJECTILE_INFO_BYTES,out,error):
        fail(error,"Projectile copy exceeds the source configuration allocation");
}
bool bot_weapon_config_projectile_signed(const bot_weapon_config_record *record,uint32_t capacity,int32_t index,
    bot_weapon_config_cell *out,qa_error *error) {
    uint32_t start=BOT_WEAPON_CONFIG_BYTES+capacity*BOT_WEAPON_INFO_BYTES;
    int64_t offset=(int64_t)start+(int64_t)index*BOT_PROJECTILE_INFO_BYTES;
    return offset>=0 && (uint64_t)offset<=UINT32_MAX?
        cell(record,(uint32_t)offset,BOT_PROJECTILE_INFO_BYTES,out,error):
        fail(error,"Projectile clear exceeds the source configuration allocation");
}
bool bot_weapon_config_embed_projectile(const bot_weapon_config_cell *weapon,
    const bot_weapon_config_cell *projectile,qa_error *error) {
    qa_bot_memory_span from,to;
    if(!field(projectile,0,BOT_PROJECTILE_INFO_BYTES,&from,error) ||
       !field(weapon,344,BOT_PROJECTILE_INFO_BYTES,&to,error)) return false;
    memmove(to.data,from.data,BOT_PROJECTILE_INFO_BYTES);return true;
}
static qa_bytes text_bytes(const char value[80]) {
    size_t size=0;while(size<80 && value[size]) ++size;
    return (qa_bytes){.data=(const uint8_t *)value,.size=size};
}
bool bot_weapon_config_store_weapon(const bot_weapon_config_record *record,uint32_t index,
    const qa_bot_weapon_info *weapon,qa_error *error) {
    if(!record || !weapon) return fail(error,"Weapon configuration store requires its parsed value/owner");
    qa_bot_memory_span bytes;
    if(!qa_bot_memory_bytes(record->memory,record->allocation,&bytes,error)) return false;
    uint64_t offset=BOT_WEAPON_CONFIG_BYTES+(uint64_t)index*BOT_WEAPON_INFO_BYTES;
    if(offset>bytes.size) return fail(error,"Weapon clear exceeds its source configuration allocation");
    uint32_t size=bytes.size-(uint32_t)offset;if(size>BOT_WEAPON_INFO_BYTES) size=BOT_WEAPON_INFO_BYTES;
    bot_weapon_config_cell value={.record=*record,.offset=(uint32_t)offset,.size=size};
    if(!bot_weapon_config_clear(&value,error)) return false;
#define INT(offset,member) if(!bot_weapon_config_set_int(&value,offset,weapon->member,error)) return false
#define FLOAT(offset,member) if(!bot_weapon_config_set_float(&value,offset,weapon->member,error)) return false
#define TEXT(offset,member) if(!bot_weapon_config_set_text(&value,offset,text_bytes(weapon->member),error)) return false
#define VECTOR(offset,member) if(!bot_weapon_config_set_vector(&value,offset,weapon->member,error)) return false
    if(!bot_weapon_config_set_int(&value,0,weapon->valid?1:0,error)) return false;
    INT(4,number);TEXT(8,name);TEXT(88,model);INT(168,level);INT(172,weapon_inventory);
    INT(176,flags);TEXT(180,projectile);INT(260,projectile_count);
    FLOAT(264,horizontal_spread);FLOAT(268,vertical_spread);FLOAT(272,speed);FLOAT(276,acceleration);
    VECTOR(280,recoil);VECTOR(292,offset);VECTOR(304,angle_offset);
    FLOAT(316,extra_z_velocity);INT(320,ammo_amount);INT(324,ammo_inventory);
    FLOAT(328,activate);FLOAT(332,reload);FLOAT(336,spin_up);FLOAT(340,spin_down);
#undef INT
#undef FLOAT
#undef TEXT
#undef VECTOR
    /* The temporary parsed weapon's embedded projectile starts cleared. */
    qa_bot_memory_span embedded;
    if(!field(&value,344,BOT_PROJECTILE_INFO_BYTES,&embedded,error)) return false;
    memset(embedded.data,0,embedded.size);
    return bot_weapon_config_set_int(&value,0,1,error);
}
bool bot_weapon_config_allocate(qa_bot_memory *memory,uint32_t weapons,uint32_t projectiles,
    bot_weapon_config_record *out,qa_error *error) {
    if(!out || weapons>INT32_MAX || projectiles>INT32_MAX)
        return fail(error,"Weapon configuration capacities exceed their source integer domain");
    uint32_t projectile_offset=BOT_WEAPON_CONFIG_BYTES+weapons*BOT_WEAPON_INFO_BYTES;
    uint32_t size=projectile_offset+projectiles*BOT_PROJECTILE_INFO_BYTES;
    if(size>INT32_MAX) return fail(error,"Weapon configuration source allocation size is negative");
    qa_bot_memory_allocation allocation;
    if(!qa_bot_memory_allocate(memory,size,QA_BOT_MEMORY_HUNK,true,NULL,&allocation,error)) return false;
    *out=(bot_weapon_config_record){.memory=memory,.allocation=allocation};
    bot_weapon_config_cell partial={.record=*out,.size=size};
    return bot_weapon_config_set_int(&partial,0,(int32_t)weapons,error) &&
        bot_weapon_config_set_int(&partial,8,signed_word(projectile_offset),error) &&
        bot_weapon_config_set_int(&partial,12,BOT_WEAPON_CONFIG_BYTES,error);
}
bool bot_weapon_config_bind(qa_bot_memory *memory,qa_bot_memory_allocation allocation,
    bot_weapon_config_record *out,qa_error *error) {
    if(!out) return fail(error,"Weapon configuration binding requires its output");
    bot_weapon_config_record record={.memory=memory,.allocation=allocation};
    bot_weapon_config_cell header;int32_t capacity,count;qa_bot_memory_span bytes;
    if(!bot_weapon_config_header(&record,&header,error) ||
       !bot_weapon_config_int(&header,0,&capacity,error) || !bot_weapon_config_int(&header,4,&count,error) ||
       !qa_bot_memory_bytes(memory,allocation,&bytes,error)) return false;
    if(capacity<0 || count<0 || BOT_WEAPON_CONFIG_BYTES+(uint64_t)capacity*BOT_WEAPON_INFO_BYTES+
       (uint64_t)count*BOT_PROJECTILE_INFO_BYTES>bytes.size)
        return fail(error,"Saved weapon configuration header exceeds its actual allocation");
    *out=record;return true;
}
