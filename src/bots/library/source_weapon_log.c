#include "internal.h"
#include "source_weapon_log.h"
#include "qa/text.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum dump_kind {DUMP_INT,DUMP_FLOAT,DUMP_TEXT} dump_kind;
typedef enum dump_result {DUMP_ERROR,DUMP_STOP,DUMP_OK} dump_result;
typedef struct dump_field {const char *name;uint32_t offset,count;dump_kind kind;} dump_field;
#define INT(name,offset) {name,offset,1,DUMP_INT}
#define FLOAT(name,offset) {name,offset,1,DUMP_FLOAT}
#define TEXT(name,offset) {name,offset,1,DUMP_TEXT}
#define VECTOR(name,offset) {name,offset,3,DUMP_FLOAT}
static const dump_field projectile_fields[]={
    TEXT("name",0),TEXT("model",88),INT("flags",160),FLOAT("gravity",164),INT("damage",168),
    FLOAT("radius",172),INT("visdamage",176),INT("damagetype",180),INT("healthinc",184),
    FLOAT("push",188),FLOAT("detonation",192),FLOAT("bounce",196),FLOAT("bouncefric",200),FLOAT("bouncestop",204)
};
static const dump_field weapon_fields[]={
    INT("number",4),TEXT("name",8),INT("level",168),TEXT("model",88),INT("weaponindex",172),
    INT("flags",176),TEXT("projectile",180),INT("numprojectiles",260),FLOAT("hspread",264),
    FLOAT("vspread",268),FLOAT("speed",272),FLOAT("acceleration",276),VECTOR("recoil",280),
    VECTOR("offset",292),VECTOR("angleoffset",304),FLOAT("extrazvelocity",316),INT("ammoamount",320),
    INT("ammoindex",324),FLOAT("activate",328),FLOAT("reload",332),FLOAT("spinup",336),FLOAT("spindown",340)
};
#undef INT
#undef FLOAT
#undef TEXT
#undef VECTOR
static dump_result write_text(qa_bot_log_file *file,const char *text,qa_error *error) {
    int64_t written;
    if(!qa_bot_log_file_write(file,text,&written,error)) return DUMP_ERROR;
    return written<0?DUMP_STOP:DUMP_OK;
}
static dump_result write_string(qa_bot_log_file *file,const bot_weapon_config_cell *cell,uint32_t offset,qa_error *error) {
    qa_bot_memory_span span;
    if(!bot_weapon_config_span(cell,&span,error)) return DUMP_ERROR;
    if(offset>=span.size) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Weapon dump string exceeds its source cell");return DUMP_ERROR;}
    size_t end=offset;
    while(end<span.size && span.data[end]) ++end;
    if(end==span.size) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Weapon dump string has no source terminator");return DUMP_ERROR;}
    size_t size=end-offset;
    if(size>SIZE_MAX-3) {qa_error_set(error,QA_ERROR_MEMORY,0,"Weapon dump string extent overflow");return DUMP_ERROR;}
    char *text=malloc(size+3);
    if(!text) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source weapon dump string");return DUMP_ERROR;}
    text[0]='"';memcpy(text+1,span.data+offset,size);
    text[size+1]='"';text[size+2]=0;
    dump_result result=write_text(file,text,error);free(text);return result;
}
static dump_result write_float(qa_bot_log_file *file,float value,qa_error *error) {
    char text[64];
    if(isnan(value) && signbit(value)) memcpy(text,"-nan",5);
    else if(!qa_format_fixed(value,6,text,sizeof(text),error)) return DUMP_ERROR;
    if(isfinite(value)) {
        size_t size=strlen(text);
        while(size>1 && (text[size-1]=='0' || text[size-1]=='.')) {
            bool dot=text[size-1]=='.';text[--size]=0;if(dot) break;
        }
    }
    return write_text(file,text,error);
}
#define DUMP_WRITE(call) do {dump_result result=(call);if(result!=DUMP_OK) return result;} while(0)
static dump_result write_structure(qa_bot_log_file *file,const bot_weapon_config_cell *cell,
    const dump_field *fields,size_t count,qa_error *error) {
    DUMP_WRITE(write_text(file,"{\r\n",error));
    for(size_t i=0;i<count;++i) {
        const dump_field *field=&fields[i];
        DUMP_WRITE(write_text(file,"\t",error));
        char name[48];(void)snprintf(name,sizeof(name),"%s\t",field->name);
        DUMP_WRITE(write_text(file,name,error));
        if(field->count>1) DUMP_WRITE(write_text(file,"{",error));
        for(uint32_t index=0;index<field->count;++index) {
            uint32_t offset=field->offset+index*4;
            if(field->kind==DUMP_TEXT) {
                DUMP_WRITE(write_string(file,cell,offset,error));
            } else if(field->kind==DUMP_INT) {
                int32_t value;char text[16];
                if(!bot_weapon_config_int(cell,offset,&value,error)) return DUMP_ERROR;
                (void)snprintf(text,sizeof(text),"%d",value);
                DUMP_WRITE(write_text(file,text,error));
            } else {
                float value;
                if(!bot_weapon_config_float(cell,offset,&value,error)) return DUMP_ERROR;
                DUMP_WRITE(write_float(file,value,error));
            }
            if(field->count>1) DUMP_WRITE(write_text(file,index+1<field->count?",":"}",error));
        }
        DUMP_WRITE(write_text(file,"\r\n",error));
    }
    return write_text(file,"}\r\n",error);
}
#undef DUMP_WRITE
bool bot_weapon_config_dump(qa_bot_log *log,const qa_bot_weapons *config,qa_error *error) {
    if(!config || !config->source || !config->source->bound) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Weapon dump requires its bound actual source configuration");return false;
    }
    qa_bot_log_file *file=qa_bot_log_file_pointer(log);
    if(!file) return true;
    for(uint32_t index=0;index<config->source->projectile_count;++index) {
        bot_weapon_config_cell cell;
        if(!bot_weapon_config_projectile(&config->source->record,index,&cell,error) ||
           write_structure(file,&cell,projectile_fields,sizeof(projectile_fields)/sizeof(*projectile_fields),error)==DUMP_ERROR ||
           !qa_bot_log_flush(log,error)) return false;
    }
    for(uint32_t index=0;;++index) {
        int32_t capacity;bot_weapon_config_cell cell;
        if(!bot_weapons_source_capacity(config,&capacity,error)) return false;
        if(capacity<=0 || index>=(uint32_t)capacity) break;
        if(!bot_weapon_config_weapon(&config->source->record,index,&cell,error) ||
           write_structure(file,&cell,weapon_fields,sizeof(weapon_fields)/sizeof(*weapon_fields),error)==DUMP_ERROR ||
           !qa_bot_log_flush(log,error)) return false;
    }
    return true;
}
