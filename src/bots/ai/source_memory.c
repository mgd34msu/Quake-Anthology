#include "qa/bots_memory.h"
#include "../save_fields.h"
#include <stddef.h>
#include <string.h>

static bool bounds(qa_bot_source_record record,qa_error *error) {
    if(record.offset>QA_BOT_GAME_MEMORY_BYTES || record.length>QA_BOT_GAME_MEMORY_BYTES-record.offset) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"bot source alias exceeds its real GAME memory pool");return false;
    }
    return true;
}
bool qa_bot_source_record_allocate(const qa_bot_source_memory *memory,qa_bot_source_record *out,qa_error *error) {
    if(!memory || !memory->allocate || !memory->read || !memory->write || !out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"bot state allocation requires its actual GAME pool owner");return false;
    }
    uint32_t offset;
    if(!memory->allocate(memory->context,QA_BOT_STATE_SOURCE_BYTES,&offset,error)) return false;
    qa_bot_source_record record={offset,QA_BOT_STATE_SOURCE_BYTES};
    if(!bounds(record,error)) return false;
    *out=record;return true;
}
bool qa_bot_source_record_alias(qa_bot_source_record record,uint32_t offset,uint32_t length,
    qa_bot_source_record *out,qa_error *error) {
    if(!out || !bounds(record,error) || offset>record.length || length>record.length-offset) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"embedded bot source record exceeds its actual allocation view");return false;
    }
    *out=(qa_bot_source_record){record.offset+offset,length};return true;
}
bool qa_bot_source_record_span(const qa_bot_source_memory *memory,qa_bot_source_record record,
    qa_bot_source_span *out,qa_error *error) {
    if(!memory || !memory->borrow_span || !out || !bounds(record,error)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"bot source span requires its actual bounded GAME pool owner");return false;
    }
    uint8_t *bytes=NULL;
    if(!memory->borrow_span(memory->context,record.offset,record.length,&bytes,error)) return false;
    if(!bytes && record.length) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"bot source span has no actual retained GAME bytes");return false;
    }
    *out=(qa_bot_source_span){bytes,record.length};return true;
}
bool qa_bot_source_record_read(const qa_bot_source_memory *memory,qa_bot_source_record record,void *out,qa_error *error) {
    if(!memory || !memory->read || (!out && record.length) || !bounds(record,error)) return false;
    return memory->read(memory->context,record.offset,out,record.length,error);
}
bool qa_bot_source_record_write(const qa_bot_source_memory *memory,qa_bot_source_record record,const void *in,qa_error *error) {
    if(!memory || !memory->write || (!in && record.length) || !bounds(record,error)) return false;
    return memory->write(memory->context,record.offset,in,record.length,error);
}
static bool word(const qa_bot_source_memory *memory,qa_bot_source_record record,uint32_t offset,
    uint32_t *value,bool write,qa_error *error) {
    qa_bot_source_record field;if(!value || !qa_bot_source_record_alias(record,offset,4,&field,error)) return false;
    uint8_t bytes[4];
    if(write) {
        for(uint32_t i=0;i<4;++i) bytes[i]=(uint8_t)(*value>>(i*8));
        return qa_bot_source_record_write(memory,field,bytes,error);
    }
    if(!qa_bot_source_record_read(memory,field,bytes,error)) return false;
    *value=(uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);return true;
}
bool qa_bot_source_record_i32(const qa_bot_source_memory *memory,qa_bot_source_record record,uint32_t offset,
    int32_t *value,bool write,qa_error *error) {
    if(!value) return false;
    uint32_t bits=0;if(write) memcpy(&bits,value,4);
    if(!word(memory,record,offset,&bits,write,error)) return false;
    if(!write) memcpy(value,&bits,4);return true;
}
bool qa_bot_source_record_f32(const qa_bot_source_memory *memory,qa_bot_source_record record,uint32_t offset,
    float *value,bool write,qa_error *error) {
    if(!value) return false;
    uint32_t bits=0;if(write) memcpy(&bits,value,4);
    if(!word(memory,record,offset,&bits,write,error)) return false;
    if(!write) memcpy(value,&bits,4);return true;
}
bool qa_bot_source_record_bool(const qa_bot_source_memory *memory,qa_bot_source_record record,uint32_t offset,
    bool *value,bool write,qa_error *error) {
    if(!value) return false;
    int32_t bits=write && *value?1:0;
    if(!qa_bot_source_record_i32(memory,record,offset,&bits,write,error)) return false;
    if(!write) *value=bits!=0;return true;
}
bool qa_bot_source_record_vec3(const qa_bot_source_memory *memory,qa_bot_source_record record,uint32_t offset,
    qa_vec3 *value,bool write,qa_error *error) {
    return value && qa_bot_source_record_f32(memory,record,offset,&value->x,write,error) &&
        qa_bot_source_record_f32(memory,record,offset+4,&value->y,write,error) &&
        qa_bot_source_record_f32(memory,record,offset+8,&value->z,write,error);
}
bool qa_bot_source_record_text_read(const qa_bot_source_memory *memory,qa_bot_source_record record,uint32_t offset,
    char *out,size_t capacity,qa_error *error) {
    if(!out || !capacity || !bounds(record,error) || offset>record.length) return false;
    uint32_t start=record.offset+offset;
    for(uint32_t i=0;i<QA_BOT_GAME_MEMORY_BYTES-start;++i) {
        uint8_t byte;qa_bot_source_record field={start+i,1};
        if(!qa_bot_source_record_read(memory,field,&byte,error)) return false;
        if((size_t)i<capacity-1) out[i]=(char)byte;
        if(!byte) {out[(size_t)i<capacity-1?(size_t)i:capacity-1]=0;return true;}
    }
    qa_error_set(error,QA_ERROR_FORMAT,0,"bot string reads beyond its real source memory pool");return false;
}
bool qa_bot_source_record_text_write(const qa_bot_source_memory *memory,qa_bot_source_record record,uint32_t offset,
    uint32_t length,const char *text,qa_error *error) {
    qa_bot_source_record field;
    if(!text || !length || !qa_bot_source_record_alias(record,offset,length,&field,error)) return false;
    size_t remaining=strlen(text);if(remaining>length-1) remaining=length-1;
    if(remaining && !qa_bot_source_record_write(memory,(qa_bot_source_record){field.offset,(uint32_t)remaining},text,error)) return false;
    uint8_t zeros[144]={0};uint32_t at=(uint32_t)remaining;
    while(at<length) {
        uint32_t size=length-at;if(size>sizeof(zeros)) size=sizeof(zeros);
        if(!qa_bot_source_record_write(memory,(qa_bot_source_record){field.offset+at,size},zeros,error)) return false;
        at+=size;
    }
    return true;
}
bool qa_bot_source_record_goal(const qa_bot_source_memory *memory,qa_bot_source_record record,uint32_t offset,
    qa_bot_goal *goal,bool write,qa_error *error) {
    qa_bot_source_record field;
    if(!goal || !qa_bot_source_record_alias(record,offset,56,&field,error)) return false;
    return qa_bot_source_record_vec3(memory,field,0,&goal->origin,write,error) &&
        qa_bot_source_record_i32(memory,field,12,&goal->area,write,error) &&
        qa_bot_source_record_vec3(memory,field,16,&goal->mins,write,error) &&
        qa_bot_source_record_vec3(memory,field,28,&goal->maxs,write,error) &&
        qa_bot_source_record_i32(memory,field,40,&goal->entity,write,error) &&
        qa_bot_source_record_i32(memory,field,44,&goal->number,write,error) &&
        qa_bot_source_record_i32(memory,field,48,&goal->flags,write,error) &&
        qa_bot_source_record_i32(memory,field,52,&goal->item_info,write,error);
}
typedef struct player_field { uint32_t source;size_t native;uint32_t count;bool floating; } player_field;
#define PLAYER_INT(name,source,count) {source,offsetof(qa_q3_player,name),count,false}
#define PLAYER_FLOAT(name,source,count) {source,offsetof(qa_q3_player,name),count,true}
static const player_field player_fields[]={
    PLAYER_INT(commandTime,0,1),PLAYER_INT(pmType,4,1),PLAYER_INT(bobCycle,8,1),
    PLAYER_INT(pmFlags,12,1),PLAYER_INT(pmTime,16,1),PLAYER_FLOAT(origin,20,3),
    PLAYER_FLOAT(velocity,32,3),PLAYER_INT(weaponTime,44,1),PLAYER_INT(gravity,48,1),
    PLAYER_INT(speed,52,1),PLAYER_INT(deltaAngles,56,3),PLAYER_INT(groundEntityNum,68,1),
    PLAYER_INT(legsTimer,72,1),PLAYER_INT(legsAnim,76,1),PLAYER_INT(torsoTimer,80,1),
    PLAYER_INT(torsoAnim,84,1),PLAYER_INT(movementDir,88,1),PLAYER_FLOAT(grapplePoint,92,3),
    PLAYER_INT(eFlags,104,1),PLAYER_INT(eventSequence,108,1),PLAYER_INT(events,112,2),
    PLAYER_INT(eventParms,120,2),PLAYER_INT(externalEvent,128,1),PLAYER_INT(externalEventParm,132,1),
    PLAYER_INT(externalEventTime,136,1),PLAYER_INT(clientNum,140,1),PLAYER_INT(weapon,144,1),
    PLAYER_INT(weaponState,148,1),PLAYER_FLOAT(viewangles,152,3),PLAYER_INT(viewheight,164,1),
    PLAYER_INT(damageEvent,168,1),PLAYER_INT(damageYaw,172,1),PLAYER_INT(damagePitch,176,1),
    PLAYER_INT(damageCount,180,1),PLAYER_INT(stats,184,16),PLAYER_INT(persistant,248,16),
    PLAYER_INT(powerups,312,16),PLAYER_INT(ammo,376,16),PLAYER_INT(generic1,440,1),
    PLAYER_INT(loopSound,444,1),PLAYER_INT(jumppadEnt,448,1),PLAYER_INT(ping,452,1),
    PLAYER_INT(pmoveFramecount,456,1),PLAYER_INT(jumppadFrame,460,1),PLAYER_INT(entityEventSequence,464,1)
};
#undef PLAYER_INT
#undef PLAYER_FLOAT
bool qa_bot_source_record_player(const qa_bot_source_memory *memory,qa_bot_source_record record,
    qa_q3_player *player,bool write,qa_error *error) {
    if(!player || record.length!=468 || !bounds(record,error)) return false;
    for(size_t i=0;i<sizeof(player_fields)/sizeof(*player_fields);++i) {
        const player_field *field=player_fields+i;
        for(uint32_t j=0;j<field->count;++j) {
            uint8_t *native=(uint8_t *)player+field->native+j*4;
            bool ok=field->floating?
                qa_bot_source_record_f32(memory,record,field->source+j*4,(float *)native,write,error):
                qa_bot_source_record_i32(memory,record,field->source+j*4,(int32_t *)native,write,error);
            if(!ok) return false;
        }
        if(!write && ((field->source==4 && (player->pmType<0 || player->pmType>6)) ||
            (field->source==144 && (player->weapon<0 || player->weapon>13)) ||
            (field->source==148 && (player->weaponState<0 || player->weaponState>3)))) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"bot player source record contains an unsupported enum");return false;
        }
    }
    return true;
}
bool qa_bot_source_record_command(const qa_bot_source_memory *memory,qa_bot_source_record record,
    qa_q3_usercmd *command,bool write,qa_error *error) {
    if(!command || record.length!=24 || !bounds(record,error)) return false;
    if(!qa_bot_source_record_i32(memory,record,0,&command->serverTime,write,error)) return false;
    for(uint32_t i=0;i<3;++i)
        if(!qa_bot_source_record_i32(memory,record,4+i*4,command->angles+i,write,error)) return false;
    if(!qa_bot_source_record_i32(memory,record,16,&command->buttons,write,error)) return false;
    uint8_t bytes[4];
    if(write) {
        bytes[0]=command->weapon;
        memcpy(bytes+1,&command->forwardmove,1);memcpy(bytes+2,&command->rightmove,1);memcpy(bytes+3,&command->upmove,1);
        return qa_bot_source_record_write(memory,(qa_bot_source_record){record.offset+20,4},bytes,error);
    }
    if(!qa_bot_source_record_read(memory,(qa_bot_source_record){record.offset+20,4},bytes,error)) return false;
    command->weapon=bytes[0];
    memcpy(&command->forwardmove,bytes+1,1);memcpy(&command->rightmove,bytes+2,1);memcpy(&command->upmove,bytes+3,1);
    return true;
}
bool qa_bot_source_record_activation(const qa_bot_source_memory *memory,qa_bot_source_record record,uint32_t index,
    qa_bot_source_activation *activation,bool write,qa_error *error) {
    qa_bot_source_record field;
    if(index>=8 || !activation || !qa_bot_source_record_alias(record,7120+index*244,244,&field,error)) return false;
    if(!qa_bot_source_record_bool(memory,field,0,&activation->inuse,write,error) ||
       !qa_bot_source_record_goal(memory,field,4,&activation->goal,write,error) ||
       !qa_bot_source_record_f32(memory,field,60,&activation->time,write,error) ||
       !qa_bot_source_record_f32(memory,field,64,&activation->start_time,write,error) ||
       !qa_bot_source_record_f32(memory,field,68,&activation->just_used_time,write,error) ||
       !qa_bot_source_record_bool(memory,field,72,&activation->shoot,write,error) ||
       !qa_bot_source_record_i32(memory,field,76,&activation->weapon,write,error) ||
       !qa_bot_source_record_vec3(memory,field,80,&activation->target,write,error) ||
       !qa_bot_source_record_vec3(memory,field,92,&activation->origin,write,error)) return false;
    for(uint32_t i=0;i<32;++i)
        if(!qa_bot_source_record_i32(memory,field,104+i*4,activation->areas+i,write,error)) return false;
    return qa_bot_source_record_i32(memory,field,232,&activation->area_count,write,error) &&
        qa_bot_source_record_bool(memory,field,236,&activation->areas_disabled,write,error);
}
bool qa_bot_source_record_presence(const qa_bot_source_memory *memory,qa_bot_source_record record,
    int32_t *presence,bool write,qa_error *error) {
    if(!qa_bot_source_record_i32(memory,record,4932,presence,write,error)) return false;
    if(!write && *presence!=0 && *presence!=1 && *presence!=2 && *presence!=4) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"unsupported bot source presence type %d",*presence);return false;
    }
    return true;
}
static bool strncpy_source(const qa_bot_source_memory *memory,qa_bot_source_record record,
    uint32_t offset,const char *text,uint32_t count,qa_error *error) {
    qa_bot_source_record field;if(!text || count>32 || !qa_bot_source_record_alias(record,offset,count,&field,error)) return false;
    uint8_t bytes[32]={0};size_t length=strlen(text);if(length>count) length=count;
    memcpy(bytes,text,length);return qa_bot_source_record_write(memory,field,bytes,error);
}
static bool byte_source(const qa_bot_source_memory *memory,uint32_t offset,uint8_t *byte,bool write,qa_error *error) {
    qa_bot_source_record field={offset,1};
    return write?qa_bot_source_record_write(memory,field,byte,error):qa_bot_source_record_read(memory,field,byte,error);
}
bool qa_bot_source_record_team_leader(const qa_bot_source_memory *memory,qa_bot_source_record record,
    const char *text,bool overflow,bool clean,qa_error *error) {
    uint32_t count=overflow?32:31;uint8_t zero=0;
    if(record.length!=9088 || !strncpy_source(memory,record,6900,text,count,error) ||
       !byte_source(memory,record.offset+6900+count,&zero,true,error)) return false;
    if(!clean) return true;
    uint32_t read=record.offset+6900,write=read;
    for(;;) {
        uint8_t current,next;
        if(!byte_source(memory,read,&current,false,error)) return false;
        if(!current) break;
        if(!byte_source(memory,read+1,&next,false,error)) return false;
        if(current=='^' && next && next!='^') read+=2;
        else {
            if(current>=32 && current<=126 && !byte_source(memory,write++,&current,true,error)) return false;
            ++read;
        }
    }
    return byte_source(memory,write,&zero,true,error);
}
bool qa_bot_source_record_subteam(const qa_bot_source_memory *memory,qa_bot_source_record record,
    const char *text,bool clear,qa_error *error) {
    uint8_t zero=0;if(record.length!=9088 || !bounds(record,error)) return false;
    if(clear) return byte_source(memory,record.offset+6980,&zero,true,error);
    return strncpy_source(memory,record,6980,text,32,error) &&
        byte_source(memory,record.offset+7011,&zero,true,error);
}
bool qa_bot_source_record_clear(const qa_bot_source_memory *memory,qa_bot_source_record record,bool decision_only,qa_error *error) {
    if(record.length!=QA_BOT_STATE_SOURCE_BYTES || !bounds(record,error)) return false;
    static const uint32_t ranges[][2]={{4,8},{484,4608},{4900,6064},{6068,6520},{6540,9088}};
    uint8_t zeros[256]={0};
    size_t count=decision_only?sizeof(ranges)/sizeof(*ranges):1;
    for(size_t i=0;i<count;++i) {
        uint32_t offset=decision_only?ranges[i][0]:0,end=decision_only?ranges[i][1]:record.length;
        while(offset<end) {
            uint32_t size=end-offset;if(size>sizeof(zeros)) size=sizeof(zeros);
            if(!qa_bot_source_record_write(memory,(qa_bot_source_record){record.offset+offset,size},zeros,error)) return false;
            offset+=size;
        }
    }
    return true;
}
bool qa_bot_source_record_fields(qa_source_save_io *io,const qa_bot_source_memory *memory,qa_bot_source_record *record) {
    if(!io || !record || !qa_source_save_u32(io,&record->offset) || !qa_source_save_u32(io,&record->length) ||
       record->length!=QA_BOT_STATE_SOURCE_BYTES || !bounds(*record,io->error)) return false;
    uint8_t bytes[QA_BOT_STATE_SOURCE_BYTES],actual[QA_BOT_STATE_SOURCE_BYTES];
    if(!qa_bot_source_record_read(memory,*record,actual,io->error)) return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE) memcpy(bytes,actual,sizeof(bytes));
    if(!qa_source_save_bytes(io,bytes,sizeof(bytes))) return false;
    return !memcmp(bytes,actual,sizeof(bytes))?true:bot_save_fail(io,QA_ERROR_FORMAT,"bot source bytes disagree with restored GAME allocation");
}
