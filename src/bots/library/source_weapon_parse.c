#include "source_weapon_parse.h"
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum weapon_field_kind {WEAPON_INT,WEAPON_FLOAT,WEAPON_TEXT,WEAPON_VECTOR} weapon_field_kind;
typedef struct weapon_field {const char *name;size_t native;uint32_t source;weapon_field_kind kind;} weapon_field;
#define INT(name,member,offset) {name,offsetof(qa_bot_weapon_info,member),offset,WEAPON_INT}
#define FLOAT(name,member,offset) {name,offsetof(qa_bot_weapon_info,member),offset,WEAPON_FLOAT}
#define TEXT(name,member,offset) {name,offsetof(qa_bot_weapon_info,member),offset,WEAPON_TEXT}
#define VECTOR(name,member,offset) {name,offsetof(qa_bot_weapon_info,member),offset,WEAPON_VECTOR}
static const weapon_field weapon_fields[]={
    INT("number",number,4),TEXT("name",name,8),INT("level",level,168),TEXT("model",model,88),
    INT("weaponindex",weapon_inventory,172),INT("flags",flags,176),TEXT("projectile",projectile,180),
    INT("numprojectiles",projectile_count,260),FLOAT("hspread",horizontal_spread,264),
    FLOAT("vspread",vertical_spread,268),FLOAT("speed",speed,272),FLOAT("acceleration",acceleration,276),
    VECTOR("recoil",recoil,280),VECTOR("offset",offset,292),VECTOR("angleoffset",angle_offset,304),
    FLOAT("extrazvelocity",extra_z_velocity,316),INT("ammoamount",ammo_amount,320),INT("ammoindex",ammo_inventory,324),
    FLOAT("activate",activate,328),FLOAT("reload",reload,332),FLOAT("spinup",spin_up,336),FLOAT("spindown",spin_down,340)
};
#undef INT
#undef FLOAT
#undef TEXT
#undef VECTOR
static const weapon_field projectile_fields[]={
    {"name",0,0,WEAPON_TEXT},{"model",0,88,WEAPON_TEXT},{"flags",0,160,WEAPON_INT},
    {"gravity",0,164,WEAPON_FLOAT},{"damage",0,168,WEAPON_INT},{"radius",0,172,WEAPON_FLOAT},
    {"visdamage",0,176,WEAPON_INT},{"damagetype",0,180,WEAPON_INT},{"healthinc",0,184,WEAPON_INT},
    {"push",0,188,WEAPON_FLOAT},{"detonation",0,192,WEAPON_FLOAT},{"bounce",0,196,WEAPON_FLOAT},
    {"bouncefric",0,200,WEAPON_FLOAT},{"bouncestop",0,204,WEAPON_FLOAT}
};
typedef struct weapon_parser {
    qa_script *source;
    const char *path;
    const bot_weapon_parser_host *host;
    qa_script_location location;
    bool language_failure;
} weapon_parser;
static bool argument(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static bool fail(weapon_parser *parser,qa_error *error,const char *format,...) {
    va_list args,copy;va_start(args,format);va_copy(copy,args);
    int length=vsnprintf(NULL,0,format,copy);va_end(copy);
    if(length<0) {va_end(args);return argument(error,"Formatting source weapon diagnostic");}
    char *message=malloc((size_t)length+1);
    if(!message) {va_end(args);qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source weapon diagnostic");return false;}
    (void)vsnprintf(message,(size_t)length+1,format,args);va_end(args);
    qa_script_diagnostic diagnostic={.severity=QA_SCRIPT_ERROR,.location=parser->location,.message=message};
    bool ok=parser->host->report(parser->host->context,&diagnostic,error) &&
        parser->host->current(parser->host->context,error);
    if(ok) {parser->language_failure=true;qa_error_set(error,QA_ERROR_FORMAT,parser->location.offset,"%s",message);}
    free(message);return false;
}
static bool next(weapon_parser *parser,qa_script_token *token,bool *found,qa_error *error) {
    if(!parser->host->current(parser->host->context,error)) return false;
    qa_error local={0};bool ok=qa_script_next(parser->source,token,found,&local);
    if(!parser->host->current(parser->host->context,error)) return false;
    if(ok) {
        parser->location=*found?token->location:(qa_script_location){.path=parser->path,.line=1,.column=1};return true;
    }
    if(qa_script_source_failure(parser->source)) {
        *found=false;parser->location=(qa_script_location){.path=parser->path,.line=1,.column=1};return true;
    }
    if(error) *error=local;return false;
}
static bool any(weapon_parser *parser,qa_script_token *token,const char *message,qa_error *error) {
    bool found;return next(parser,token,&found,error) && (found || fail(parser,error,"%s",message));
}
static bool token_fail(weapon_parser *parser,const char *prefix,const qa_script_token *token,qa_error *error) {
    if(token->text.size>INT_MAX) return argument(error,"Weapon diagnostic token exceeds native extent");
    return fail(parser,error,"%s%.*s",prefix,(int)token->text.size,(const char *)token->text.data);
}
static bool expect(weapon_parser *parser,const char *text,qa_error *error) {
    qa_script_token token;bool found;
    if(!next(parser,&token,&found,error)) return false;
    if(!found) return fail(parser,error,"expected %s",text);
    if(qa_script_token_is(&token,text)) return true;
    if(token.text.size>INT_MAX) return argument(error,"Weapon diagnostic token exceeds native extent");
    return fail(parser,error,"expected %s, found %.*s",text,(int)token.text.size,(const char *)token.text.data);
}
static bool number(weapon_parser *parser,bool floating,double *out,qa_error *error) {
    qa_script_token token;
    if(!any(parser,&token,"expected number",error)) return false;
    bool negative=false;
    if(token.kind==QA_SCRIPT_PUNCTUATION) {
        if(!qa_script_token_is(&token,"-")) return token_fail(parser,"unexpected punctuation ",&token,error);
        negative=true;if(!any(parser,&token,"expected number after minus sign",error)) return false;
    }
    if(token.kind!=QA_SCRIPT_NUMBER) return token_fail(parser,"expected number, found ",&token,error);
    bool is_float=(token.subtype&QA_SCRIPT_FLOAT)!=0;
    if(is_float && !floating) return fail(parser,error,"unexpected float");
    if(!is_float && negative && token.integer==INT32_MIN)
        return argument(error,"Structure negation exceeds the source signed-long range");
    double value=is_float?token.number:token.integer;if(negative) value=-value;
    if(!floating && (value<-32768 || value>32767))
        return fail(parser,error,"value %.0f out of range [-32768, 32767]",value);
    if(floating && isfinite(value) && !isfinite((float)value))
        return argument(error,"Structure value exceeds the source float conversion range");
    *out=value;return true;
}
static bool text(weapon_parser *parser,char out[80],qa_error *error) {
    qa_script_token token;
    if(!any(parser,&token,"expected string",error)) return false;
    if(token.kind!=QA_SCRIPT_STRING) return token_fail(parser,"expected string, found ",&token,error);
    qa_bytes bytes=qa_script_token_value(&token);size_t size=0;
    while(size<bytes.size && size<79 && bytes.data[size]) {out[size]=(char)bytes.data[size];++size;}
    out[size]=0;return true;
}
static bool vector(weapon_parser *parser,qa_vec3 *out,qa_error *error) {
    if(!expect(parser,"{",error)) return false;
    qa_vec3 value=*out;
    for(uint32_t i=0;i<3;++i) {
        qa_script_token token;
        if(!any(parser,&token,"expected array value or closing brace",error)) return false;
        if(qa_script_token_is(&token,"}")) {*out=value;return true;}
        if(!qa_script_unread(parser->source,&token,error)) return false;
        double number_value;if(!number(parser,true,&number_value,error)) return false;
        if(i==0) value.x=(float)number_value;else if(i==1) value.y=(float)number_value;else value.z=(float)number_value;
        if(!any(parser,&token,"expected comma or closing brace",error)) return false;
        if(qa_script_token_is(&token,"}")) {*out=value;return true;}
        if(!qa_script_token_is(&token,",")) return token_fail(parser,"expected a comma, found ",&token,error);
    }
    *out=value;return true;
}
static bool structure(weapon_parser *parser,const weapon_field *fields,size_t count,
    qa_bot_weapon_info *weapon,const bot_weapon_config_cell *projectile,qa_error *error) {
    if(!expect(parser,"{",error)) return false;
    for(;;) {
        qa_script_token token;
        if(!any(parser,&token,"expected structure field or closing brace",error)) return false;
        if(qa_script_token_is(&token,"}")) return true;
        size_t i=0;while(i<count && !qa_script_token_is(&token,fields[i].name)) ++i;
        if(i==count) return token_fail(parser,"unknown structure field ",&token,error);
        const weapon_field *field=&fields[i];uint8_t *destination=weapon?(uint8_t *)weapon+field->native:NULL;
        if(field->kind==WEAPON_TEXT) {
            char value[80]={0};if(!text(parser,value,error)) return false;
            if(weapon) memcpy(destination,value,80);
            else if(!bot_weapon_config_set_text(projectile,field->source,
                (qa_bytes){.data=(const uint8_t *)value,.size=strlen(value)},error)) return false;
        } else if(field->kind==WEAPON_VECTOR) {
            qa_vec3 value;memcpy(&value,destination,sizeof(value));
            if(!vector(parser,&value,error)) return false;memcpy(destination,&value,sizeof(value));
        } else {
            double value;if(!number(parser,field->kind==WEAPON_FLOAT,&value,error)) return false;
            if(field->kind==WEAPON_INT) {
                int32_t integer=(int32_t)value;
                if(weapon) memcpy(destination,&integer,4);
                else if(!bot_weapon_config_set_int(projectile,field->source,integer,error)) return false;
            } else {
                float number_value=(float)value;
                if(weapon) memcpy(destination,&number_value,4);
                else if(!bot_weapon_config_set_float(projectile,field->source,number_value,error)) return false;
            }
        }
    }
}
bool bot_weapon_parse(qa_script *source,const char *path,uint32_t weapons,uint32_t projectiles,
    const bot_weapon_config_record *record,const bot_weapon_parser_host *host,bool *source_failure,qa_error *error) {
    if(!source || !path || !path[0] || !record || !host || !host->current || !host->report || !host->complete || !source_failure)
        return argument(error,"Weapon parse requires its true opened PC, allocated hunk and callbacks");
    *source_failure=false;
    weapon_parser parser={.source=source,.path=path,.host=host,.location={.path=path,.line=1,.column=1}};
    bot_weapon_config_cell header;qa_bot_memory_span bytes;
    bool ok=bot_weapon_config_header(record,&header,error) && qa_bot_memory_bytes(record->memory,record->allocation,&bytes,error);
    uint64_t available=ok?((uint64_t)bytes.size-BOT_WEAPON_CONFIG_BYTES+BOT_WEAPON_INFO_BYTES-4)/BOT_WEAPON_INFO_BYTES:0;
    uint32_t limit=available<weapons?(uint32_t)available:weapons;
    uint8_t *defined=limit?calloc(limit,1):NULL;
    bot_weapon_config_cell *parsed_projectiles=NULL;size_t projectile_count=0,projectile_capacity=0;
    if(limit && !defined) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining parsed source weapon membership");ok=false;}
    while(ok) {
        qa_script_token token;bool found;
        if(!next(&parser,&token,&found,error)) {ok=false;break;}
        if(!found) break;
        if(qa_script_token_is(&token,"weaponinfo")) {
            qa_bot_weapon_info weapon={0};
            ok=structure(&parser,weapon_fields,sizeof(weapon_fields)/sizeof(*weapon_fields),&weapon,NULL,error);
            if(ok && (weapon.number<0 || (uint32_t)weapon.number>=weapons)) {
                parser.location=token.location;
                ok=fail(&parser,error,"weapon info number %d out of range in %s",weapon.number,path);
            }
            if(ok) ok=bot_weapon_config_store_weapon(record,(uint32_t)weapon.number,&weapon,error);
            if(ok) defined[(uint32_t)weapon.number]=1;
        } else if(qa_script_token_is(&token,"projectileinfo")) {
            int32_t count;bot_weapon_config_cell projectile;
            ok=bot_weapon_config_int(&header,4,&count,error);
            if(ok && count>=0 && (uint32_t)count>=projectiles)
                ok=fail(&parser,error,"more than %u projectiles defined in %s",projectiles,path);
            if(ok) ok=bot_weapon_config_projectile_signed(record,weapons,count,&projectile,error) &&
                bot_weapon_config_clear(&projectile,error) &&
                structure(&parser,projectile_fields,sizeof(projectile_fields)/sizeof(*projectile_fields),NULL,&projectile,error);
            if(ok && projectile_count==projectile_capacity) {
                size_t capacity=projectile_capacity?projectile_capacity*2:16;
                if(capacity<projectile_capacity || capacity>SIZE_MAX/sizeof(*parsed_projectiles))
                    ok=argument(error,"Parsed projectile membership exceeds native extent");
                else {
                    bot_weapon_config_cell *next=realloc(parsed_projectiles,capacity*sizeof(*next));
                    if(!next) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining parsed source projectile membership");ok=false;}
                    else {parsed_projectiles=next;projectile_capacity=capacity;}
                }
            }
            if(ok) {parsed_projectiles[projectile_count++]=projectile;ok=bot_weapon_config_set_int(&header,4,count+1,error);}
        } else {
            if(token.text.size>INT_MAX) ok=argument(error,"Weapon definition token exceeds native extent");
            else ok=fail(&parser,error,"unknown definition %.*s in %s",(int)token.text.size,(const char *)token.text.data,path);
        }
    }
    if(ok) ok=host->complete(host->context,error) && host->current(host->context,error);
    if(ok) {
        parser.location=(qa_script_location){.path=path,.line=1,.column=1};
        for(uint32_t i=0;ok && i<limit;++i) {
            if(!defined[i]) continue;
            bot_weapon_config_cell weapon;char name[81],projectile_name[81];
            ok=bot_weapon_config_weapon(record,i,&weapon,error) && bot_weapon_config_text(&weapon,8,name,error) &&
                bot_weapon_config_text(&weapon,180,projectile_name,error);
            if(ok && !name[0]) ok=fail(&parser,error,"weapon %u has no name in %s",i,path);
            if(ok && !projectile_name[0]) ok=fail(&parser,error,"weapon %s has no projectile in %s",name,path);
            bool matched=false;
            for(size_t j=0;ok && j<projectile_count;++j) {
                char candidate[81];ok=bot_weapon_config_text(&parsed_projectiles[j],0,candidate,error);
                if(ok && !strcmp(candidate,projectile_name)) {
                    bot_weapon_config_cell projectile;matched=true;
                    if(j>UINT32_MAX) ok=argument(error,"Projectile fixup exceeds its source ordinal domain");
                    else ok=bot_weapon_config_projectile_at(record,weapons,(uint32_t)j,&projectile,error) &&
                        bot_weapon_config_embed_projectile(&weapon,&projectile,error);
                    break;
                }
            }
            if(ok && !matched) ok=fail(&parser,error,"weapon %s uses undefined projectile in %s",name,path);
        }
        if(ok && available<weapons) ok=argument(error,"Weapon validation exceeds the source configuration allocation");
        if(ok && !weapons) {
            qa_script_diagnostic warning={.severity=QA_SCRIPT_WARNING,.location=parser.location,.message="no weapon info loaded"};
            ok=host->report(host->context,&warning,error) && host->current(host->context,error);
        }
    }
    free(defined);free(parsed_projectiles);*source_failure=!ok && parser.language_failure;return ok;
}
